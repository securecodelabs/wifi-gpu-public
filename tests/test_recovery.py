"""CLI integration tests. Only synthetic/public vectors are read or generated."""
import argparse
import ctypes
import hashlib
import json
import os
import shutil
import signal
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import make_fixtures as oracle
from snapshot_reader import read_snapshot


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--cpu-only', action='store_true')
    parser.add_argument('--backend', choices=('opencl', 'vulkan', 'hip'), default='vulkan')
    args = parser.parse_args()
    engine = ['--cpu'] if args.cpu_only else ['--backend', args.backend]
    exe = args.exe.resolve()
    cases = 0
    with tempfile.TemporaryDirectory(prefix='wifi-gpu-tests-') as temporary:
        root = Path(temporary)
        vectors = oracle.make(root/'fixtures')
        source = root/'fixtures/all-types.hc22000'
        output = root/'results.json'

        def run(arguments, code=0, timeout=60):
            nonlocal cases
            result = subprocess.run([str(exe), *map(str, arguments)], cwd=root, capture_output=True,
                                    text=True, encoding='utf-8', timeout=timeout)
            assert result.returncode == code, (arguments, result.returncode, code, result.stdout[-4000:], result.stderr[-4000:])
            cases += 1
            return result

        def put(name, data):
            path = root/name
            path.write_bytes(data if isinstance(data, bytes) else data.encode('ascii'))
            return path

        def recover(hashfile, extra, code=0, cpu=None):
            selected = engine if cpu is None else (['--cpu'] if cpu else ['--backend', args.backend])
            return run(['recover', '--hashes', hashfile, *extra, *selected], code)

        reported_version = run(['--version']).stdout.strip()
        run(['inspect', '--hashes', source])
        # All 37 independent EAPOL/PMKID/FT vectors, six message pair types,
        # SHA1/SHA256/MD5/CMAC, both nonce endiannesses and three SSID groups.
        common = ['--mask', 'labpass?d?d', '--batch', '16', '--output', output]
        recover(source, common)
        result = json.loads(output.read_text())
        assert result['version'] == reported_version
        assert result['status'] == 'recovered' and result['recovered'] == 37
        assert all(x['password_hex'] == b'labpass42'.hex() and x['verified'] for x in result['matches'])
        hardware = result['hardware']
        assert hardware['backend'] == ('cpu' if args.cpu_only else args.backend)
        assert hardware['compute_kind'] == ('cpu' if args.cpu_only else 'gpu')
        assert hardware['software'] is False
        if args.cpu_only:
            assert hardware['cpu_threads'] >= 1 and hardware['cpu_provider']
            assert hardware['device'] is None
        else:
            assert hardware['device']['name'] and hardware['device']['driver']
        telemetry = result['telemetry']
        assert telemetry['elapsed_seconds'] > 0
        assert telemetry['candidates_per_second'] > 0
        assert telemetry['progress_basis'] == 'search'
        assert 0 < telemetry['fraction'] < 1  # Found early; never fake 100%.
        assert telemetry['eta_basis'] == 'search' and telemetry['eta_seconds'] > 0
        assert telemetry['search_eta_seconds'] == telemetry['eta_seconds']
        assert telemetry['runtime_remaining_seconds'] is None
        assert all(telemetry[key] is None or telemetry[key] >= 0 for key in
                   ('search_eta_seconds', 'runtime_remaining_seconds', 'candidate_limit_eta_seconds'))
        saved = output.read_bytes()
        recover(source, common, 1)
        assert output.read_bytes() == saved
        if not args.cpu_only:
            recover(source, common+['--overwrite'], cpu=True)
            assert json.loads(output.read_text())['matches'] == result['matches']

        one = put('one.hc22000', vectors[2]+'\n')
        # Activity reports real preparation stages and only tested candidates.
        activity_output=root/'activity.json'
        stream=recover(one,['--mask','labpass?d?d','--batch','16','--activity-json','--output',activity_output])
        events=[json.loads(line.removeprefix('WIFI_ACTIVITY ')) for line in stream.stdout.splitlines() if line.startswith('WIFI_ACTIVITY ')]
        assert [event['stage'] for event in events[:4]]==['loading_sources','restoring_position','initializing_device','recovering']
        assert all(event['latest_candidate_hex'] is None for event in events[:4])
        assert events[-1]['latest_candidate_hex']==b'labpass42'.hex()
        result=json.loads(activity_output.read_text())
        assert result['tested_candidates']==43 and result['total_candidates']==100,result
        assert result['telemetry']['fraction']==0.43,result
        # Exact totals use the same BOM/CRLF/length rules as enumeration; no extra disk scan.
        counted=put('counted.txt',b'\xef\xbb\xbfwrongpass\r\nshort\n'+b'x'*64+b'\nlabpass42\r\ntrailing9')
        empty=put('empty.txt',b'')
        extra=put('extra.txt',b'short\nwrongpass\n')
        stream=recover(one,['--wordlist',empty,'--wordlist',counted,'--wordlist',extra,'--activity-json'])
        events=[json.loads(line.removeprefix('WIFI_ACTIVITY ')) for line in stream.stdout.splitlines() if line.startswith('WIFI_ACTIVITY ')]
        scans=[event['wordlist_scan'] for event in events if 'wordlist_scan' in event]
        size=sum(path.stat().st_size for path in (empty,counted,extra))
        assert len(scans)>=2 and scans[0]['bytes_scanned']==0 and scans[-1]['bytes_scanned']==size,scans
        assert all(scan['total_bytes']==size and scan['cpu_threads']==1 for scan in scans)
        assert all(a['bytes_scanned']<=b['bytes_scanned'] and a['elapsed_seconds']<=b['elapsed_seconds'] for a,b in zip(scans,scans[1:]))
        assert scans[-1]['elapsed_seconds']>0
        assert all(event['stage']=='loading_sources' for event in events if 'wordlist_scan' in event)
        assert all('wordlist_scan' not in event for event in events if event['stage']!='loading_sources')
        stream=recover(one,['--wordlist',empty,'--activity-json'],2)
        scans=[json.loads(line.removeprefix('WIFI_ACTIVITY '))['wordlist_scan'] for line in stream.stdout.splitlines() if '"wordlist_scan"' in line]
        assert len(scans)==2 and all(scan['bytes_scanned']==scan['total_bytes']==0 for scan in scans)
        counted_session=root/'counted.session'
        recover(one,['--wordlist',counted,'--limit','3','--batch','2','--session',counted_session,'--output',activity_output,'--overwrite'],4)
        result=json.loads(activity_output.read_text())
        assert result['tested_candidates']==1 and result['total_candidates']==3,result
        recover(one,['--wordlist',counted,'--batch','2','--resume',counted_session,'--output',activity_output])
        result=json.loads(activity_output.read_text())
        assert result['tested_candidates']==2 and result['total_candidates']==3,result
        assert result['latest_candidate_hex']==b'labpass42'.hex()
        hybrid_count=put('counted-hybrid.txt',b'123456\n'+b'x'*62+b'\n'+b'x'*64+b'\n')
        recover(one,['--wordlist',hybrid_count,'--mask','?d','--mask','?d?d','--output',activity_output,'--overwrite'],2)
        result=json.loads(activity_output.read_text())
        assert result['total_candidates']==110 and result['tested_candidates']==110,result
        hexadecimal=put('counted-hex.txt',b'77726f6e6770617373\r\n00\n6c6162706173733432')
        recover(one,['--wordlist',hexadecimal,'--hex-wordlist','--output',activity_output,'--overwrite'])
        result=json.loads(activity_output.read_text())
        assert result['total_candidates']==2 and result['tested_candidates']==2,result
        # CPU worker counts preserve matches and can change across a checkpoint.
        for threads in (1, 3):
            recover(one, ['--mask', 'labpass?d?d', '--threads', str(threads)], cpu=True)
        cpu_session=root/'cpu-threads.session'
        recover(one, ['--mask', 'labpass?d?d', '--limit', '0', '--runtime', '0'], cpu=True)
        recover(one, ['--mask', 'labpass?d?d', '--limit', '16', '--threads', '1', '--session', cpu_session], 4, cpu=True)
        recover(one, ['--mask', 'labpass?d?d', '--threads', '3', '--resume', cpu_session], cpu=True)
        # A standalone exe with no kernels directory remains fully CPU-capable.
        isolated=root/'isolated'; isolated.mkdir()
        bare=isolated/exe.name; shutil.copy2(exe,bare)
        for command in (['recover', '--cpu', '--threads', '2', '--hashes', str(one), '--mask', 'labpass42'],
                        ['benchmark', '--cpu', '--threads', '2', '--batch', '3', '--repeats', '1']):
            p=subprocess.run([str(bare),*command],capture_output=True,text=True,timeout=30)
            assert p.returncode==0,(p.stdout,p.stderr)
            assert 'GPU runtimes disabled' in p.stdout+p.stderr
            cases+=1
        if not args.cpu_only and args.backend=='vulkan' and 'Default: Vulkan.' in run(['--help']).stdout:
            default=run(['recover', '--hashes', one, '--mask', 'labpass42'])
            assert 'backend=Vulkan' in default.stderr
            assert 'Vulkan' in run(['devices']).stdout
        words = put('words.txt', b'\xef\xbb\xbfshort\r\nwrongpass\r\n'+b'X'*100000+b'\nlabpass42')
        r = recover(one, ['--wordlist', words, '--batch', '2'])
        assert 'rejected=2' in r.stderr and 'password_hex=6c6162706173733432' in r.stdout
        later = put('later.txt', 'labpass42\n')
        first = put('first.txt', 'incorrect\n')
        unlimited = root/'unlimited.json'
        recover(one, ['--wordlist', first, '--limit', '0', '--runtime', '0', '--output', unlimited], 2)
        assert json.loads(unlimited.read_text())['status'] == 'exhausted'
        recover(one, ['--wordlist', first, '--wordlist', later])
        stem = put('stems.txt', 'wrong\nlabpass\n')
        recover(one, ['--wordlist', stem, '--mask', '?d?d', '--hybrid', 'append'])
        suffix = put('suffix.txt', 'pass42\n')
        recover(one, ['--wordlist', suffix, '--mask', 'lab', '--hybrid', 'prepend'])
        recover(one, ['--mask', 'labpass?1?1', '--charset1', '422'])
        masks = put('masks.txt', b'\xef\xbb\xbf# comment\r\n\r\nlabpass?d?d\r\n')
        recover(one, ['--mask-file', masks])
        recover(one, ['--mask', 'wrongpass', '--mask', 'labpass?d?d'])
        recover(one, ['--mask', 'labpass?d?d', '--skip', '42', '--limit', '1'])
        recover(one, ['--mask', 'labpass?d?d', '--skip', '100'], 2)
        recover(one, ['--mask', 'labpass?d?d', '--skip', '101'], 1)

        # File identities and byte-accurate saved matches survive stop/resume.
        session = root/'progress.session'
        resumed_output = root/'resumed.json'
        resume_source = ['--mask', 'labpass?d?d', '--batch', '8']
        recover(one, [*resume_source, '--limit', '20', '--session', session, '--output', resumed_output], 4)
        assert '\nnext\t20\n' in session.read_text()
        recover(one, [*resume_source, '--resume', session, '--output', resumed_output])
        assert json.loads(resumed_output.read_text())['recovered'] == 1
        recover(one, [*resume_source, '--resume', session, '--output', resumed_output])
        recover(one, ['--mask', 'different?d', '--resume', session], 1)
        altered = session.read_text().split('sha256\t')[0].replace(b'labpass42'.hex(), b'wrongpass'.hex())
        forged = put('forged.session', altered+'sha256\t'+hashlib.sha256(altered.encode()).hexdigest()+'\n')
        recover(one, [*resume_source, '--resume', forged], 1)
        corrupt = put('corrupt.session', session.read_bytes().replace(b'next\t48', b'next\t49'))
        # Corrupt deterministically even if a changed batch policy alters offset.
        damaged = bytearray(corrupt.read_bytes()); damaged[-3] ^= 1; corrupt.write_bytes(damaged)
        recover(one, [*resume_source, '--resume', corrupt], 1)
        unrelated = put('unrelated.json', '{"unrelated":true}')
        recover(one, [*resume_source, '--resume', session, '--output', unrelated], 1)
        assert unrelated.read_text() == '{"unrelated":true}'
        recover(one, ['--wordlist', later, '--output', later, '--overwrite'], 1)
        assert later.read_bytes() == b'labpass42\n'
        recover(one, ['--mask', 'labpass42', '--session', root/'same', '--output', root/'same'], 1)
        reserved = put('reserved.json.lock', 'unrelated existing file')
        recover(one, ['--mask', 'labpass42', '--output', root/'reserved.json'], 1)
        assert reserved.read_text() == 'unrelated existing file'
        reserved.unlink()  # Only the test-owned sentinel, after preservation was verified.

        # Known negatives: a wrong MIC, wrong SSID, wrong client or wrong nonce
        # must never be reported as a recovered password.
        for field in (2, 4, 5, 6):
            parts = vectors[6].split('*')
            value = bytearray.fromhex(parts[field]); value[-1] ^= 1; parts[field] = value.hex()
            bad = put(f'negative-{field}.hc22000', '*'.join(parts)+'\n')
            r = recover(bad, ['--mask', 'labpass42'], 2)
            assert 'FOUND' not in r.stdout

        # Flag-driven automatic nonce correction and an explicit exact-only run.
        corrected = put('correction.hc22000', oracle.record(correction=2, little=True)+'\n')
        recover(corrected, ['--mask', 'labpass42', '--nonce-error-corrections', '0'], 2)
        recover(corrected, ['--mask', 'labpass42', '--nonce-error-corrections', '8'])
        unflagged = corrected.read_text().strip().split('*'); unflagged[8] = '00'
        unflagged = put('unflagged.hc22000', '*'.join(unflagged)+'\n')
        recover(unflagged, ['--mask', 'labpass42'], 2)
        recover(unflagged, ['--mask', 'labpass42', '--nonce-error-corrections', '8'])

        # Binary candidates and SSIDs remain bytes throughout the full pipeline.
        binary = b'a\x00b\xffc\ndZ'
        binary_hash = put('binary.hc22000', oracle.record(password=binary, ssid=b'ab\x00\xffcd')+'\n')
        hex_words = put('hex-words.txt', b'wrongpass'.hex()+'\n'+binary.hex()+'\n')
        recover(binary_hash, ['--wordlist', hex_words, '--hex-wordlist'])
        boundary = put('boundary.hc22000', oracle.record(password=b'Z'*63, version=1)+'\n')
        boundary_words = put('boundary.txt', b'Y'*64+b'\n'+b'Z'*63+b'\n')
        recover(boundary, ['--wordlist', boundary_words])
        # Reused buffers and block-wise line reads must retain exact offsets,
        # including CRLF/BOM, oversized lines and a final line without LF.
        for edge in (65530, 65535, 65536, 65537):
            crossing = put(f'crossing-{edge}.txt',
                           b'\xef\xbb\xbf' + b'x' * (edge - 3) + b'\r\n' + b'labpass42')
            crossing_result = root/f'crossing-{edge}.json'
            recover(one, ['--wordlist', crossing, '--batch', '1', '--output', crossing_result])
            value = json.loads(crossing_result.read_text())
            assert value['next_candidate'] == 2 and value['tested_candidates'] == 1
            assert value['total_candidates'] == 1 and value['matches'][0]['password_hex'] == b'labpass42'.hex()
        literal_hash = put('literal.hc22000', oracle.record(password=b'labpass?')+'\n')
        recover(literal_hash, ['--mask', 'labpass??'])
        unicode_path = root/'žodis with spaces.txt'; unicode_path.write_bytes(b'labpass42\n')
        recover(one, ['--wordlist', unicode_path])
        unicode_password = 'labpaš42'.encode('utf-8')
        unicode_hash = put('unicode.hc22000', oracle.record(password=unicode_password)+'\n')
        recover(unicode_hash, ['--mask', 'labpaš42'])
        hybrid_session = root/'hybrid.session'
        hybrid_source = ['--wordlist', stem, '--mask', '?d?d', '--batch', '8']
        recover(one, [*hybrid_source, '--limit', '120', '--session', hybrid_session], 4)
        assert '\nnext\t120\n' in hybrid_session.read_text()
        recover(one, [*hybrid_source, '--resume', hybrid_session])
        mixed = put('two-passwords.hc22000', oracle.record()+'\n'+oracle.record(password=b'newpass99')+'\n')
        r = recover(mixed, ['--mask', 'labpass42', '--mask', 'newpass99'])
        assert r.stdout.count('FOUND') == 2

        # Input validation fails before attempting a search, without overwriting
        # any existing output. Duplicates are removed by canonical record bytes.
        duplicate = put('duplicate.hc22000', '# comment\n'+vectors[0]+'\n'+vectors[0].upper()+'\n')
        r = run(['inspect', '--hashes', duplicate]); assert 'records=1 duplicates=1' in r.stdout
        base = vectors[6].split('*')
        mutations = []
        for field, value in ((0, 'OTHER'), (1, '05'), (2, '00'), (3, 'xx'*6), (5, 'aa'*33), (8, '08'), (8, 'e0')):
            parts = base.copy(); parts[field] = value; mutations.append('*'.join(parts))
        for index, replacement in ((2, 0), (4, 9), (5, 0), (81, 1), (97, 255)):
            parts = base.copy(); eapol = bytearray.fromhex(parts[7]); eapol[index] = replacement
            # index 2 is already zero in short records: modify length low byte instead.
            if index == 2:
                eapol[3] ^= 1
            parts[7] = eapol.hex(); mutations.append('*'.join(parts))
        for i, line in enumerate(mutations):
            bad = put(f'malformed-{i}.hc22000', line+'\n')
            run(['inspect', '--hashes', bad], 1)
        huge = put('huge.hc22000', 'x'*20000+'\n'); run(['inspect', '--hashes', huge], 1)
        empty = put('empty.hc22000', '# no records\n'); run(['inspect', '--hashes', empty], 1)
        utf16 = put('utf16.txt', b'\xff\xfel\x00a\x00b\x00')
        recover(one, ['--wordlist', utf16], 1)
        invalid_hex = put('invalid-hex.txt', 'oops\n')
        recover(one, ['--wordlist', invalid_hex, '--hex-wordlist'], 1)

        # Public upstream mode-22000 self-test and our converter's exported data.
        public = root/'fixtures/hashcat-public.hc22000'
        recover(public, ['--mask', 'hashcat!'])
        if not args.cpu_only and args.backend=='vulkan':
            recover(one, ['--mask', 'labpass42', '--chunk', '1024'])
        # Check backend-specific ceilings in either option order, before any
        # device is opened. This also runs on hosts without HIP or OpenCL.
        for backend,limit in (('vulkan',1024),('opencl',256),('hip',256)):
            for options in (['--chunk',str(limit+1),'--backend',backend],
                            ['--backend',backend,'--chunk',str(limit+1)]):
                run(['benchmark',*options],1)
                run(['recover','--hashes',one,'--mask','labpass42',*options],1)
        converter = Path(__file__).parent/'fixtures/converter-export.hc22000'
        if converter.exists():
            recover(converter.resolve(), ['--mask', 'synthetic-test-password'])

        for extra in (['--limit', '-1'], ['--chunk', '0'], ['--device', '-1'],
                      ['--nonce-error-corrections', '1025'], ['--mask', 'short'],
                      ['--mask', '?d'*20], ['--mask', 'labpass?2'],
                      ['--resume', session, '--skip', '1'], ['--session', session, '--resume', session],
                      ['--hex-wordlist'], ['--hybrid', 'prepend'], ['--resume', ''], ['--output', '']):
            recover(one, extra, 1)
        run(['inspect', '--hashes', one, '--wordlist', later], 1)
        run(['recover', '--hashes', one, '--cpu', '--device', '0', '--mask', 'labpass42'], 1)

        # A bounded run exercises checkpointing under load and ensures input
        # files cannot be truncated by another process while recovery uses them.
        locked_words = put('locked.txt', 'labpass\n')
        lock_session = root/'locking.session'
        proc = subprocess.Popen([str(exe), 'recover', '--hashes', str(one), '--wordlist', str(locked_words),
                                 '--mask', '?d?d?d?d?d?d', '--runtime', '2', *engine, '--batch', '32', '--session', str(lock_session)],
                                cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic()+10
            while not lock_session.exists() and proc.poll() is None and time.monotonic()<deadline:
                time.sleep(0.02)
            assert lock_session.exists(), 'Session was not created'
            if os.name == 'nt':
                try:
                    with locked_words.open('wb'):
                        raise AssertionError('Input was writable while in use')
                except PermissionError:
                    pass
            else:
                import fcntl
                with locked_words.open('rb') as reader:
                    try:
                        fcntl.flock(reader.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
                        raise AssertionError('Input advisory lock missing')
                    except BlockingIOError:
                        pass
            stdout, stderr = proc.communicate(timeout=15)
            assert proc.returncode == 4, (stdout, stderr)
            assert locked_words.read_bytes() == b'labpass\n'
            assert 'next\t0\n' not in lock_session.read_text()
            cases += 1
        finally:
            if proc.poll() is None:
                proc.kill(); proc.communicate()

        if not args.cpu_only:
            # CPU-created checkpoints can resume on either GPU backend.
            cross = root/'cross.session'
            source_options = ['--mask', 'labpass?d?d', '--batch', '8']
            recover(one, [*source_options, '--limit', '16', '--session', cross], 4, cpu=True)
            recover(one, [*source_options, '--resume', cross])
            # GPU-created checkpoints remain compatible with CPU recovery.
            recover(one, [*source_options, '--limit', '16', '--session', cross, '--overwrite'], 4)
            recover(one, [*source_options, '--resume', cross], cpu=True)
            run(['devices', '--backend', args.backend])
            recover(one, ['--mask', 'labpass42', '--device', '999999'], 1)
            recover(one, ['--mask', 'labpass42', '--kernel', root/'absent-kernel'], 1)
            if args.backend == 'vulkan':
                shader = (exe.parent/'kernels/pbkdf2.spv').read_bytes()
                assert b'wifi_pbkdf2_v4' in shader
                for version in (b'wifi_pbkdf2_v1', b'wifi_pbkdf2_v2', b'wifi_pbkdf2_v3'):
                    legacy = put('legacy.spv', shader.replace(b'wifi_pbkdf2_v4', version))
                    result = recover(one, ['--mask', 'labpass42', '--kernel', legacy], 1)
                    assert 'ABI mismatch' in result.stderr
                for index, payload in enumerate((b'', b'not SPIR-V', b'\x00'*20,
                    bytes.fromhex('0302230700060100000000000100000000000000'))):
                    shader = put(f'bad-{index}.spv', payload)
                    recover(one, ['--mask', 'labpass42', '--kernel', shader], 1)
        for bad in (['--backend', 'cuda'], ['--backend', ''],
                    ['--cpu', '--backend', 'vulkan'],
                    ['--threads', '2'], ['--cpu', '--threads', '0'],
                    ['--cpu', '--threads', '65'], ['--cpu', '--threads', '-1'],
                    ['--backend', 'vulkan', '--backend', 'opencl']):
            run(['recover', '--hashes', one, '--mask', 'labpass42', *bad], 1)

        # Changed wordlists cannot reuse a saved candidate offset.
        locked_words.write_bytes(b'different\n')
        recover(one, ['--wordlist', locked_words, '--mask', '?d?d?d?d?d?d', '--resume', lock_session], 1)

        # Deliver a real Windows CTRL_C_EVENT to a hidden isolated console. The
        # signal helper attaches only to this test child, never to the user's console.
        interrupt_session = root/'interrupted.session'
        child_options = {}
        if os.name == 'nt':
            si = subprocess.STARTUPINFO()
            si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            si.wShowWindow = 0
            child_options = dict(creationflags=subprocess.CREATE_NEW_CONSOLE,startupinfo=si)
        proc = subprocess.Popen([str(exe), 'recover', '--hashes', str(one), '--mask', 'absent?d?d?d?d?d?d',
                                 '--runtime', '10', *engine, '--batch', '16', '--session', str(interrupt_session)],
                                cwd=root, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                **child_options)
        try:
            deadline = time.monotonic()+5
            while time.monotonic()<deadline and proc.poll() is None:
                if interrupt_session.exists() and '\nnext\t0\n' not in read_snapshot(interrupt_session):
                    break
                time.sleep(0.02)
            assert interrupt_session.exists()
            if os.name == 'nt':
                signal_report=root/'signal.json'
                helper=subprocess.run([sys.executable,str(Path(__file__).with_name('console_signal.py')),
                                       '--pid',str(proc.pid),'--report',str(signal_report)],
                                      capture_output=True,text=True,timeout=5)
                if helper.returncode:
                    # Preserve the child's error before cleanup, instead of hiding
                    # the actual recovery failure behind an AttachConsole assertion.
                    already_exited=proc.poll()
                    if already_exited is None: proc.kill()
                    stdout,stderr=proc.communicate(timeout=5)
                    raise AssertionError({'signal':signal_report.read_text() if signal_report.exists() else 'no report',
                                          'helper_stderr':helper.stderr,'child_exit_before_cleanup':already_exited,
                                          'child_stdout':stdout.decode(errors='replace'),'child_stderr':stderr.decode(errors='replace')})
            else:
                proc.send_signal(signal.SIGINT)
            stdout, stderr = proc.communicate(timeout=5)
            assert proc.returncode == 3 and b'status=interrupted' in stderr, (stdout, stderr)
            cases += 1
        finally:
            if proc.poll() is None:
                proc.kill(); proc.communicate()
        recover(one, ['--mask', 'absent?d?d?d?d?d?d', '--resume', interrupt_session, '--limit', '1'], 4)
        assert not list(root.glob('*.tmp.*')), 'Snapshot temporaries leaked'
        if os.name == 'nt':
            assert not list(root.glob('*.lock')), 'Session/output locks leaked'
        else:
            assert all(p.stat().st_size == 0 for p in root.glob('*.lock'))
    print(f"PASS: {cases} recovery integration scenarios; synthetic vectors only; backend={'CPU' if args.cpu_only else args.backend}")


if __name__ == '__main__':
    main()
