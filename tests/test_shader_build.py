"""Check the real build guards without modifying project shader artifacts."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--cmake', type=Path)
    args=parser.parse_args()
    project=Path(__file__).resolve().parent.parent
    checks=0
    with tempfile.TemporaryDirectory(prefix='wifi-shader-guards-') as tmp:
        root=Path(tmp); (root/'scripts').mkdir(); (root/'kernels').mkdir()
        for name in ('verify-shaders.ps1','verify-shaders.cmake'):
            shutil.copy2(project/'scripts'/name,root/'scripts'/name)
        original={name:(project/'kernels'/name).read_bytes()
                  for name in ('pbkdf2.comp','pbkdf2.spv','checksums.sha256')}

        def reset():
            for name,data in original.items(): (root/'kernels'/name).write_bytes(data)

        def verify(ok):
            nonlocal checks
            commands = []
            if os.name == 'nt':
                commands.append(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass',
                                 '-File',str(root/'scripts/verify-shaders.ps1')])
            if args.cmake:
                commands.append([str(args.cmake),f'-DROOT={root.as_posix()}',
                                 '-P',str(root/'scripts/verify-shaders.cmake')])
            if not commands:
                raise RuntimeError('Pass --cmake to exercise shader guards on Linux')
            for command in commands:
                result=subprocess.run(command,capture_output=True,text=True,timeout=15)
                assert (result.returncode==0)==ok,(command,result.stdout,result.stderr)
                checks+=1

        reset(); verify(True)
        for name in ('pbkdf2.comp','pbkdf2.spv'):
            reset(); path=root/'kernels'/name
            path.write_bytes(path.read_bytes()+b'\n'); verify(False)
            reset(); path.unlink(); verify(False)
        reset(); (root/'kernels/checksums.sha256').write_text('invalid\n'); verify(False)
        reset(); manifest=root/'kernels/checksums.sha256'
        manifest.write_text('\n'.join(reversed(manifest.read_text().splitlines()))+'\n')
        verify(False)
        reset(); verify(True)
    print(f'PASS: {checks} shader build guard checks')


if __name__=='__main__': main()
