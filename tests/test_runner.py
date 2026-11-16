"""The diagnostic runner preserves both pipes, failures, and spaced paths under PS5."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    runner=Path(__file__).resolve().parent.parent/'scripts/run_checked.py'
    with tempfile.TemporaryDirectory(prefix='wifi diagnostic paths ') as temporary:
        root=Path(temporary); logs=root/'retained logs'
        child=root/'failing child.py'
        child.write_text("import sys\nsys.stdout.buffer.write(b'O'*100000+b'\\nSTDOUT_END\\n')\nsys.stderr.buffer.write(b'E'*100000+b'\\nTRACEBACK_END\\n')\nsys.exit(7)\n")
        def quote(text): return "'"+str(text).replace("'","''")+"'"
        script=root/'wrapper.ps1'
        script.write_text("$ErrorActionPreference='Stop'\n& "+quote(sys.executable)+' '+quote(runner)+
                          ' --logs '+quote(logs)+' --name failure -- '+quote(sys.executable)+' '+quote(child)+
                          "\nif ($LASTEXITCODE -ne 7) { throw 'Exit code not preserved' }\nWrite-Output 'AFTER_FAILURE'\n")
        if os.name=='nt':
            result=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(script)],capture_output=True,timeout=20)
            assert result.returncode==0,(result.returncode,result.stderr)
            assert b'AFTER_FAILURE' in result.stdout and b'TRACEBACK_END' in result.stdout
        else:
            result=subprocess.run([sys.executable,str(runner),'--logs',str(logs),'--name','failure','--',sys.executable,str(child)],capture_output=True,timeout=20)
            assert result.returncode==7 and b'TRACEBACK_END' in result.stdout,(result.returncode,result.stderr)
        assert (logs/'failure.stdout.log').read_bytes()==b'O'*100000+b'\nSTDOUT_END\n'
        assert (logs/'failure.stderr.log').read_bytes()==b'E'*100000+b'\nTRACEBACK_END\n'
        assert json.loads((logs/'failure.json').read_text())['exit_code']==7
        ok=subprocess.run([sys.executable,str(runner),'--logs',str(logs),'--name','success','--',
                           sys.executable,'-c',"print('SUCCESS')"],capture_output=True,timeout=15)
        assert ok.returncode==0 and json.loads((logs/'success.json').read_text())['exit_code']==0
    print('PASS: diagnostic capture; complete stdout/stderr, spaced paths and exit codes; platform='+os.name)


if __name__=='__main__': main()
