#!/usr/bin/env python3
"""Create a versioned Linux/Windows release archive with provenance and checksums."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
import zipfile

ROOT=Path(__file__).resolve().parents[1]

def check_runtime_version(binary, version):
    actual = subprocess.check_output([str(binary.resolve()), "--version"], text=True, timeout=15).strip()
    if actual != version:
        raise SystemExit(f"Release binary reports {actual!r}; expected {version!r}")

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag',required=True)
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--platform', choices=['linux-amd64', 'windows-amd64'], default='linux-amd64')
    parser.add_argument('--commit',help='Local packaging only; CI uses the checked-out tag')
    parser.add_argument('--out',type=Path,default=Path('dist'))
    args=parser.parse_args()
    spec=json.loads((ROOT/'release.json').read_text())
    if (ROOT/'Cargo.toml').exists():
        version=re.search(r'^version\s*=\s*"([^"]+)"',(ROOT/'Cargo.toml').read_text(),re.M)[1]
    else:
        version=re.search(r'project\(wifi_gpu VERSION ([\d.]+)',(ROOT/'CMakeLists.txt').read_text())[1]
    if args.tag!='v'+version or not re.fullmatch(r'v\d+\.\d+\.\d+',args.tag):
        raise SystemExit('Release tag must match the package version')
    def git(*command):
        return subprocess.check_output(['git','-C',str(ROOT),*command],text=True).strip()
    commit=args.commit
    if commit is None:
        if Path(git('rev-parse','--show-toplevel')).resolve()!=ROOT:
            raise SystemExit('Run from this component checkout')
        commit=git('rev-parse','HEAD')
        if args.tag not in git('tag','--points-at','HEAD').splitlines():
            raise SystemExit('HEAD is not the requested release tag')
    if not re.fullmatch('[0-9a-f]{40}',commit):
        raise SystemExit('Expected a full source commit SHA')
    binary=args.binary.read_bytes()
    if args.platform == 'linux-amd64':
        if binary[:6] != b'\x7fELF\x02\x01' or int.from_bytes(binary[18:20], 'little') != 62:
            raise SystemExit('Release binary must be Linux x86-64 ELF')
    else:
        pe = int.from_bytes(binary[60:64], 'little') if len(binary) >= 64 else 0
        if binary[:2] != b'MZ' or pe < 64 or binary[pe:pe+6] != b'PE\0\0\x64\x86':
            raise SystemExit('Release binary must be Windows x86-64 PE')
    # Local --commit packaging supports synthetic format fixtures; release CI
    # must execute the actual artifact and verify its embedded version.
    if args.commit is None:
        check_runtime_version(args.binary, version)
    files={'bin/'+spec['project']+('.exe' if args.platform == 'windows-amd64' else ''):(binary,0o755)}
    for destination,source in spec.get('files',{}).items():
        files[destination]=((ROOT/source).read_bytes(),0o644)
    metadata={'schema_version':1,'project':spec['project'],'version':version,'tag':args.tag,
              'commit':commit,'platform':args.platform}
    files['component.json']=((json.dumps(metadata,indent=2,sort_keys=True)+'\n').encode(),0o644)
    files['SHA256SUMS']=(''.join(hashlib.sha256(data).hexdigest()+'  '+name+'\n'
        for name,(data,_) in sorted(files.items())).encode(),0o644)
    args.out.mkdir(parents=True,exist_ok=True)
    name=f"{spec['project']}-{args.tag}-{args.platform}" + (".zip" if args.platform == "windows-amd64" else ".tar.gz")
    output=args.out/name
    if args.platform == 'windows-amd64':
        with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, (data, mode) in sorted(files.items()):
                member = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                member.external_attr = mode << 16
                archive.writestr(member, data, compress_type=zipfile.ZIP_DEFLATED)
    else:
        with output.open('xb') as raw,gzip.GzipFile(fileobj=raw,mode='wb',mtime=0) as compressed,tarfile.open(fileobj=compressed,mode='w') as archive:
            for name,(data,mode) in sorted(files.items()):
                member=tarfile.TarInfo(name);member.size=len(data);member.mode=mode;member.mtime=0
                archive.addfile(member,io.BytesIO(data))
    checksum=hashlib.sha256(output.read_bytes()).hexdigest()
    output.with_name(output.name+'.sha256').write_text(f'{checksum}  {output.name}\n',encoding='ascii')
    print(output)

if __name__=='__main__':main()
