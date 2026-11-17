"""Hold actual snapshot fsync calls to test overlap, shutdown and failed writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import threading
import time

import make_fixtures as oracle


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--shim',type=Path,required=True)
    parser.add_argument('--backend',choices=('opencl','hip','vulkan'),default='opencl')
    args=parser.parse_args()
    batch=8192;password=f'{batch*2+1:010X}'.encode()
    with tempfile.TemporaryDirectory(prefix='wifi-checkpoint-overlap-') as directory:
        root=Path(directory)
        hashes=root/'synthetic.hc22000';hashes.write_text(oracle.record(password=password)+'\n')
        for mode in ('release','terminate','kill','failure'):
            folder=root/mode;folder.mkdir()
            session=folder/'run.session';output=folder/'run.json';gate=folder/'gate';marker=folder/'waiting'
            def set_gate(value):
                temporary=gate.with_suffix('.new');temporary.write_text(value);temporary.replace(gate)
            set_gate('hold')
            base=[str(args.exe.resolve()),'recover','--backend',args.backend,'--hashes',str(hashes),
                  '--mask','?H'*10,'--batch',str(batch),'--limit',str(batch*3),'--output',str(output),'--activity-json']
            env=dict(os.environ,LD_PRELOAD=str(args.shim.resolve()),WIFI_TEST_CHECKPOINT_PREFIX=str(session)+'.tmp.',
                     WIFI_TEST_CHECKPOINT_GATE=str(gate),WIFI_TEST_CHECKPOINT_MARKER=str(marker))
            proc=subprocess.Popen([*base,'--session',str(session)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            activities=[];errors=[]
            def read_out():
                for line in proc.stdout:
                    if line.startswith('WIFI_ACTIVITY '):
                        event=json.loads(line.removeprefix('WIFI_ACTIVITY '))
                        if event.get('latest_candidate_hex'):activities.append(event)
            def read_err():
                errors.extend(proc.stderr)
            readers=[threading.Thread(target=read_out),threading.Thread(target=read_err)]
            for reader in readers:reader.start()
            def offset():
                data=session.read_bytes();start=data.rfind(b'sha256\t')
                assert start>=0 and data[start+7:]==hashlib.sha256(data[:start]).hexdigest().encode()+b'\n'
                return int(next(line.split(b'\t')[1] for line in data.splitlines() if line.startswith(b'next\t')))
            try:
                deadline=time.monotonic()+20
                while time.monotonic()<deadline and proc.poll() is None:
                    if marker.exists() and len(activities)>=2:break
                    time.sleep(.01)
                assert marker.exists() and len(activities)>=2,('GPU did not advance during blocked fsync',mode,errors)
                assert offset()==0,'Blocked checkpoint must not publish speculative progress'
                # There is one pending write, not an unbounded queue of snapshots.
                time.sleep(.05)
                assert len(activities)==2
                if mode=='kill':proc.kill()
                elif mode=='terminate':proc.send_signal(signal.SIGTERM);set_gate('release')
                else:set_gate('fail' if mode=='failure' else 'release')
                proc.wait(timeout=20)
                for reader in readers:reader.join()
                expected={'release':0,'terminate':3,'kill':-signal.SIGKILL,'failure':1}[mode]
                assert proc.returncode==expected,(mode,proc.returncode,errors)
                if mode=='release':
                    assert json.loads(output.read_text())['status']=='recovered'
                elif mode=='terminate':
                    assert offset()==batch*2
                    assert json.loads(output.read_text())['status']=='interrupted'
                else:
                    assert offset()==0
                    if mode=='failure':assert 'Cannot flush snapshot' in ''.join(errors)
                # Every mode resumes to the same independently verified hit.
                resumed=subprocess.run([*base,'--resume',str(session)],capture_output=True,text=True,timeout=30)
                assert resumed.returncode==0,(mode,resumed.stdout,resumed.stderr)
                result=json.loads(output.read_text())
                assert result['status']=='recovered' and result['recovered']==1
                assert result['matches'][0]['verified'] and result['matches'][0]['password_hex']==password.hex()
                assert offset()>=batch*2+2
            finally:
                set_gate('release')
                if proc.poll() is None:proc.kill();proc.wait(timeout=10)
                for reader in readers:reader.join(timeout=10)
    print('PASS: checkpoint/GPU overlap, bounded writes, final drain, SIGTERM, SIGKILL, write failure and verified resume')


if __name__=='__main__':main()
