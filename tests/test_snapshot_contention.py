"""Real Windows reader/writer contention: bounded retries and intact checkpoints."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time


def validate_session(path):
    data=path.read_bytes()
    marker=data.rfind(b'sha256\t')
    assert marker>=0 and data[marker+7:]==hashlib.sha256(data[:marker]).hexdigest().encode()+b'\n'
    assert data.startswith(b'WIFI-GPU-SESSION\t1\n')


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--exe',type=Path,required=True)
    p.add_argument('--backend',choices=('cpu','vulkan','opencl'),default='cpu')
    args=p.parse_args(); exe=args.exe.resolve()
    engine=['--cpu','--threads','3'] if args.backend=='cpu' else ['--backend',args.backend]
    fixture=(Path(__file__).parent/'fixtures/all-types.hc22000').read_text().splitlines()[6]
    checks=0
    with tempfile.TemporaryDirectory(prefix='wifi-snapshot-contention-') as temporary:
        root=Path(temporary); hashes=root/'one.hc22000'; hashes.write_text(fixture+'\n')
        for kind in ('session','output'):
            for persistent in (False,True):
                folder=root/f'{kind}-{persistent}'; folder.mkdir()
                session=folder/'progress.session'; output=folder/'results.json'
                target=session if kind=='session' else output
                base=[str(exe),'recover','--hashes',str(hashes),'--mask','absent?d?d?d?d?d?d',*engine,'--batch','16']
                proc=subprocess.Popen([*base,'--runtime','1','--session',str(session),'--output',str(output)],
                                      stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                reader=None
                try:
                    deadline=time.monotonic()+10
                    while reader is None and proc.poll() is None and time.monotonic()<deadline:
                        try: reader=target.open('rb') # ordinary Windows reader denies replacement
                        except (FileNotFoundError,PermissionError): time.sleep(.001)
                    if reader is None:
                        if proc.poll() is None: proc.kill()
                        raise AssertionError(('No snapshot to hold',proc.communicate()))
                    before=reader.read()
                    if not persistent:
                        time.sleep(.25)
                        reader.close(); reader=None
                    stdout,stderr=proc.communicate(timeout=7)
                    assert proc.returncode==(1 if persistent else 4),(kind,persistent,stdout,stderr)
                    if persistent:
                        assert b'Cannot publish snapshot after retrying for 1 second' in stderr,(kind,stderr)
                        reader.close(); reader=None
                        assert target.read_bytes()==before,'Existing checkpoint/output changed after failed publish'
                    else:
                        assert b'status=runtime_limit' in stderr
                        assert json.loads(output.read_text())['status']=='runtime_limit'
                    validate_session(session); json.loads(output.read_text())
                    assert not list(folder.glob('*.tmp.*')) and not list(folder.glob('*.lock'))
                    checks+=1
                    # The preserved state remains usable after releasing either lock.
                    resumed=subprocess.run([*base,'--resume',str(session),'--output',str(output),'--limit','1'],
                                           capture_output=True,timeout=15)
                    assert resumed.returncode==4,(kind,persistent,resumed.stdout,resumed.stderr)
                    validate_session(session)
                    assert json.loads(output.read_text())['status']=='candidate_limit'
                    assert not list(folder.glob('*.tmp.*')) and not list(folder.glob('*.lock'))
                    checks+=1
                finally:
                    if reader: reader.close()
                    if proc.poll() is None: proc.kill(); proc.communicate()
    print(f'PASS: {checks} snapshot contention/resume scenarios; backend={args.backend}')


if __name__=='__main__': main()
