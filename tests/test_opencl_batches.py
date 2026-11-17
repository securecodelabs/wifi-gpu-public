"""Check large OpenCL batches and resume boundaries using synthetic WPA2 data."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

import make_fixtures as oracle


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--backend', choices=('opencl','hip','vulkan'), default='opencl')
    args = parser.parse_args()
    exe = args.exe.resolve()
    with tempfile.TemporaryDirectory(prefix='wifi-gpu-opencl-batches-') as temporary:
        root = Path(temporary)
        positions = [4095, 4096, 8192, 65535, 65536, 262143, 262144]
        passwords = [f'{position:010X}'.encode('ascii') for position in positions]
        hashes = root/'synthetic.hc22000'
        hashes.write_text('\n'.join(oracle.record(password=password) for password in passwords)+'\n',
                          encoding='ascii')
        base = ['recover', '--hashes', str(hashes), '--mask', '?H'*10]

        def run(arguments, expected):
            result = subprocess.run([str(exe), *arguments], capture_output=True, text=True,
                                    timeout=180)
            assert result.returncode == expected, (arguments, result.stdout, result.stderr)
            return result

        # Check parsing after all options, including --batch before --backend.
        for options in (['--batch', '262145', '--backend', args.backend],
                        ['--batch', '262145', '--backend', 'vulkan'],
                        ['--batch', '4097', '--cpu'],
                        ['--backend', args.backend, '--batch', '0']):
            run(['benchmark', *options], 1)
            run([*base, *options], 1)

        session = root/'run.session'
        first = root/'first.json'
        result = run([*base, '--batch', '262144', '--backend', args.backend, '--group', '256',
                      '--chunk', '256', '--limit', '262144', '--session', str(session),
                      '--output', str(first)], 4)
        assert 'derivations=262144' in result.stderr
        snapshot = json.loads(first.read_text())
        assert snapshot['status'] == 'candidate_limit' and snapshot['recovered'] == 6
        assert {match['password_hex'] for match in snapshot['matches']} == {
            password.hex() for password in passwords[:-1]}
        assert all(match['verified'] for match in snapshot['matches'])

        # A smaller resumed batch must consume exactly the first remaining item.
        resumed = root/'resumed.json'
        result = run([*base, '--backend', args.backend, '--batch', '8192', '--group', '128',
                      '--chunk', '64', '--limit', '1', '--resume', str(session),
                      '--output', str(resumed)], 0)
        snapshot = json.loads(resumed.read_text())
        assert snapshot['status'] == 'recovered' and snapshot['recovered'] == 7
        assert 'next=262145' in result.stderr and 'derivations=1' in result.stderr
        assert {match['password_hex'] for match in snapshot['matches']} == {
            password.hex() for password in passwords}
        assert all(match['verified'] for match in snapshot['matches'])
    print('PASS: '+args.backend+' batch limits, boundary matches, maximum batch and resume')


if __name__ == '__main__':
    main()
