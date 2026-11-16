"""Linux process, filesystem and checkpoint regressions using synthetic inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--backend', choices=('cpu', 'opencl', 'vulkan'), default='cpu')
    args = parser.parse_args()
    exe = args.exe.resolve()
    engine = ['--cpu', '--threads', '2'] if args.backend == 'cpu' else ['--backend', args.backend]
    checks = 0
    with tempfile.TemporaryDirectory(prefix='wifi-posix-') as directory:
        root = Path(directory)
        hashes = root/'one.hc22000'
        hashes.write_text((Path(__file__).parent/'fixtures/all-types.hc22000').read_text().splitlines()[6]+'\n')
        stems = root/'stems.txt'; stems.write_text('absent\nunused\n')
        # A hybrid source also exercises speculative GPU wordlist prefetch:
        # interruption/resume must use committed offsets, never the read cursor.
        base = [str(exe),'recover',*engine,'--hashes',str(hashes),'--wordlist',str(stems),
                '--mask','?d?d?d?d?d?d','--batch','16']

        def call(args, expected):
            result = subprocess.run(args, capture_output=True, text=True, timeout=20)
            assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
            return result

        def offset(path):
            data = path.read_bytes()
            marker = data.rfind(b'sha256\t')
            assert marker >= 0 and data[marker+7:] == hashlib.sha256(data[:marker]).hexdigest().encode()+b'\n'
            return int(next(line.split(b'\t')[1] for line in data.splitlines() if line.startswith(b'next\t')))

        def wait_ready(proc, path):
            deadline = time.monotonic()+15
            while time.monotonic() < deadline and proc.poll() is None:
                if path.exists() and offset(path) > 0:
                    return
                time.sleep(.01)
            raise AssertionError(('no checkpoint', proc.poll()))

        for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGKILL):
            session = root/f'{sig}.session'
            output = root/f'{sig}.json'
            proc = subprocess.Popen([*base,'--session',str(session),'--output',str(output)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            try:
                wait_ready(proc,session)
                before = offset(session)
                # A second writer cannot acquire a live session, even with overwrite.
                rejected = call([*base,'--resume',str(session),'--limit','1'],1)
                assert 'lock' in rejected.stderr.lower()
                checks += 1
                proc.send_signal(sig)
                stdout,stderr = proc.communicate(timeout=10)
                assert proc.returncode == (-sig if sig==signal.SIGKILL else 3), (stdout,stderr)
                stopped = offset(session)
                assert stopped >= before
                call([*base,'--resume',str(session),'--output',str(output),'--limit','1'],4)
                assert offset(session)==stopped+1
                assert json.loads(output.read_text())['status']=='candidate_limit'
                assert (session.with_name(session.name+'.lock')).stat().st_size==0
                checks += 1
            finally:
                if proc.poll() is None:
                    proc.kill(); proc.communicate()

        # Atomic rename allows readers to keep the old inode and observe valid data.
        session=root/'readers.session'
        call([*base,'--session',str(session),'--limit','1'],4)
        with session.open('rb') as reader:
            original=reader.read()
            call([*base,'--resume',str(session),'--limit','1'],4)
            reader.seek(0)
            assert reader.read()==original and session.read_bytes()!=original
        checks += 1

        # Case-sensitive paths, Unicode paths, and input aliases keep Linux semantics.
        words=root/'Žodis.txt'; words.write_text('labpass42\n')
        call([str(exe),'recover','--cpu','--hashes',str(hashes),'--wordlist',str(words),'--output',str(root/'žodis.txt')],0)
        alias=root/'alias'; alias.symlink_to(hashes)
        call([*base,'--output',str(alias),'--overwrite','--limit','1'],1)
        linked=root/'hardlink'; os.link(hashes,linked)
        call([*base,'--output',str(linked),'--overwrite','--limit','1'],1)
        checks += 3

        # A crash must not leave a stale lock that prevents restart, and a lock
        # symlink must never redirect writes to an unrelated file.
        bad=root/'bad.session'; lock=root/'bad.session.lock'; lock.symlink_to(words)
        call([*base,'--session',str(bad),'--limit','1'],1)
        assert words.read_text()=='labpass42\n'
        checks += 1
    print(f'PASS: {checks} POSIX lifecycle/atomicity/path scenarios')


if __name__ == '__main__':
    main()
