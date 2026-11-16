"""Stress Ctrl+C/checkpoint races with ordinary readers and varying CPU worker counts."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--exe',type=Path,required=True)
    p.add_argument('--iterations',type=int,default=30)
    args=p.parse_args()
    if not 1<=args.iterations<=200: p.error('iterations must be 1..200')
    exe=args.exe.resolve(); tests=Path(__file__).resolve().parent
    fixture=(tests/'fixtures/all-types.hc22000').read_text().splitlines()[6]
    counts=(1,2,3,12,64)
    with tempfile.TemporaryDirectory(prefix='wifi-cpu-cancel-') as temporary:
        root=Path(temporary); hashes=root/'one.hc22000'; hashes.write_text(fixture+'\n')
        for n in range(args.iterations):
            folder=root/str(n); folder.mkdir(); session=folder/'progress.session'
            report=folder/'signal.json'; threads=counts[n%len(counts)]
            base=[str(exe),'recover','--cpu','--threads',str(threads),'--hashes',str(hashes),
                  '--mask','absent?d?d?d?d?d?d','--batch','16']
            si=subprocess.STARTUPINFO(); si.dwFlags|=subprocess.STARTF_USESHOWWINDOW; si.wShowWindow=0
            proc=subprocess.Popen([*base,'--runtime','10','--session',str(session)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                                  creationflags=subprocess.CREATE_NEW_CONSOLE,startupinfo=si)
            try:
                deadline=time.monotonic()+5; observed=False
                while proc.poll() is None and time.monotonic()<deadline:
                    try:
                        # Deliberately deny replacement briefly, reproducing the
                        # old reader race instead of avoiding it with shared delete.
                        with session.open('rb') as reader:
                            data=reader.read(); time.sleep(.003)
                        if b'\nnext\t0\n' not in data:
                            observed=True; break
                    except (FileNotFoundError,PermissionError): pass
                    time.sleep(.003)
                if not observed:
                    if proc.poll() is None: proc.kill()
                    raise AssertionError(('no completed batch',n,threads,proc.communicate()))
                helper=subprocess.run([sys.executable,str(tests/'console_signal.py'),'--pid',str(proc.pid),'--report',str(report)],
                                      capture_output=True,timeout=5)
                if helper.returncode:
                    exited=proc.poll()
                    if exited is None: proc.kill()
                    raise AssertionError(('signal failed',n,threads,exited,helper.stderr,report.read_text(),proc.communicate()))
                stdout,stderr=proc.communicate(timeout=5)
                assert proc.returncode==3 and b'status=interrupted' in stderr,(n,threads,stdout,stderr)
                data=session.read_bytes(); marker=data.rfind(b'sha256\t')
                assert data[marker+7:]==hashlib.sha256(data[:marker]).hexdigest().encode()+b'\n'
                previous=int(next(x.split(b'\t')[1] for x in data.splitlines() if x.startswith(b'next\t')))
                # Switch worker count on resume and check the committed offset.
                resume=[str(exe),'recover','--cpu','--threads',str(counts[(n+1)%len(counts)]),'--hashes',str(hashes),
                        '--mask','absent?d?d?d?d?d?d','--resume',str(session),'--limit','1']
                resumed=subprocess.run(resume,capture_output=True,timeout=15)
                assert resumed.returncode==4,(n,resumed.stdout,resumed.stderr)
                assert f'\nnext\t{previous+1}\n' in session.read_text()
                assert not list(folder.glob('*.tmp.*')) and not list(folder.glob('*.lock'))
            finally:
                if proc.poll() is None: proc.kill(); proc.communicate()
            if (n+1)%10==0: print(f'CPU cancellation stress: {n+1}/{args.iterations} passed',flush=True)
    print(f'PASS: {args.iterations} CPU cancel/reader-race/resume cycles; worker counts 1,2,3,12,64')


if __name__=='__main__': main()
