#!/usr/bin/env python3
"""Portable CI lint/secret checks. Downloaded executables are verified before execution."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def command(args, **kwargs):
    print('+', ' '.join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def tool(name):
    system = 'windows' if os.name == 'nt' else 'linux'
    if platform.machine().lower() not in ('x86_64', 'amd64'):
        raise RuntimeError('CI tool binaries require x86-64')
    spec = json.loads((ROOT / '.github/ci-tools.json').read_text())[name][system]
    dest = ROOT / '.tools/ci-bin' / (name + ('.exe' if os.name == 'nt' else ''))
    stamp = dest.with_suffix('.sha256')
    if dest.exists() and stamp.exists():
        saved = json.loads(stamp.read_text())
        if saved['archive'] == spec['sha256'] and saved['binary'] == hashlib.sha256(dest.read_bytes()).hexdigest():
            return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as temporary:
        archive = Path(temporary) / 'download'
        if not spec['url'].startswith('https://github.com/'):
            raise ValueError('CI tools must come from an official pinned GitHub release')
        with urllib.request.urlopen(spec['url'], timeout=120) as source, archive.open('wb') as output:  # nosec B310 # validated HTTPS release URL
            shutil.copyfileobj(source, output)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != spec['sha256']:
            raise ValueError('CI tool archive checksum mismatch: ' + name)
        if spec['url'].endswith('.zip'):
            with zipfile.ZipFile(archive) as package:
                data = package.read(dest.name)
        else:
            with tarfile.open(archive) as package:
                member = package.getmember(dest.name)
                if not member.isfile():
                    raise ValueError('Expected regular executable in tool archive')
                data = package.extractfile(member).read()
        dest.write_bytes(data)
        dest.chmod(0o755)
        stamp.write_text(json.dumps({'archive': spec['sha256'], 'binary': hashlib.sha256(data).hexdigest()}))
    return dest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lint-only', action='store_true')
    parser.add_argument('--install-tool', choices=['actionlint', 'gitleaks', 'trivy'])
    args = parser.parse_args()
    if args.install_tool:
        print(tool(args.install_tool))
        return
    reports = ROOT / 'ci-reports'
    reports.mkdir(exist_ok=True)
    workflows = sorted((ROOT / '.github/workflows').glob('*.y*ml'))
    command([tool('actionlint'), '-color', *workflows], cwd=ROOT)
    # No GitHub API token is exposed to these local static checks.
    command(['zizmor', '--offline', '--pedantic', ROOT / '.github/workflows'], cwd=ROOT)
    files = [p for folder in ['scripts', 'deploy'] for p in (ROOT / folder).rglob('*.py')]
    if files:
        command(['ruff', 'check', '--select', 'E9,F63,F7,F82', *files], cwd=ROOT)
        command(['bandit', '-q', '-ll', '-ii', *files], cwd=ROOT)
    if not args.lint_only:
        command([tool('gitleaks'), 'git', '--redact', '--no-banner', '--log-opts=--all',
                 '--report-format=json', '--report-path', reports / 'gitleaks.json', ROOT], cwd=ROOT)


if __name__ == '__main__':
    main()
