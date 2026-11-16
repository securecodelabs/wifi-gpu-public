"""CPU Vulkan must be an explicit opt-in and visibly identified."""
import argparse
import json
import tempfile
import make_fixtures as oracle
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('--exe',type=Path,required=True)
exe=str(parser.parse_args().exe.resolve())
def run(args):
    return subprocess.run([exe,*args],capture_output=True,text=True,timeout=30)
enabled=run(['devices','--backend','vulkan','--allow-software-vulkan'])
assert enabled.returncode==0 and 'SOFTWARE VULKAN: CPU execution' in enabled.stdout,(enabled.stdout,enabled.stderr)
default=run(['devices','--backend','vulkan'])
assert 'llvmpipe' not in default.stdout and 'SOFTWARE VULKAN' not in default.stdout
if default.returncode:
    assert default.returncode==1 and 'No Vulkan GPU detected' in default.stderr,default.stderr
for args in (['devices','--allow-software-vulkan','--backend','opencl'],
             ['selftest','--allow-software-vulkan','--cpu-only'],
             ['benchmark','--allow-software-vulkan','--cpu'],
             ['devices','--allow-software-vulkan','--allow-software-vulkan']):
    result=run(args)
    assert result.returncode==1 and result.stderr.startswith('error:'),(args,result)
print('PASS: 6 software Vulkan opt-in/label/CLI scenarios')

with tempfile.TemporaryDirectory(prefix='wifi-hardware-') as tmp:
    folder=Path(tmp)
    oracle.make(folder/'fixtures')
    output=folder/'results.json'
    result=run(['recover','--backend','vulkan','--allow-software-vulkan',
                '--hashes',str(folder/'fixtures/all-types.hc22000'),'--mask','labpass42',
                '--output',str(output)])
    assert result.returncode==0,(result.stdout,result.stderr)
    value=json.loads(output.read_text())
    hw=value['hardware']
    assert hw['backend']=='vulkan' and hw['compute_kind']=='cpu' and hw['software'] is True,hw
    assert hw['device']['name'] and hw['device']['driver'],hw
    assert value['recovered']==37
print('PASS: initialized software Vulkan hardware telemetry and recovery')
