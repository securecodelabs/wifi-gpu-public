"""Exercise optional OpenCL entry points and compiler-profile fallback."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

import make_fixtures as oracle


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    args = parser.parse_args()
    exe = args.exe.resolve()
    kernel = (exe.parent/'kernels/pbkdf2.cl').read_text()
    marker = '// WPA2 EAPOL v2 with one exact nonce context.'
    assert marker in kernel
    variants = {
        'current': kernel,
        'no-filter': kernel.split(marker)[0],
        'legacy': '#undef WIFI_GPU_OPENCL_GFX1200\n'+kernel,
        'compiler-fallback': '#ifdef WIFI_GPU_OPENCL_GFX1200\n#error unsupported optional profile\n#endif\n'+kernel,
    }
    with tempfile.TemporaryDirectory(prefix='wifi-opencl-setup-') as temporary:
        root = Path(temporary)
        hashes = root/'synthetic.hc22000'
        hashes.write_text(oracle.record(password=b'A1B2C3D4EF')+'\n')
        for name, source in variants.items():
            path = root/(name+'.cl')
            path.write_text(source)
            output = root/(name+'.json')
            # The hit falls in a partial final group after several batches.
            result = subprocess.run([
                str(exe), 'recover', '--backend', 'opencl', '--kernel', str(path),
                '--hashes', str(hashes), '--mask', 'A1B2C3D4?H?H', '--batch', '67',
                '--chunk', '512', '--output', str(output)], capture_output=True,
                text=True, timeout=90, env={**os.environ, 'WIFI_GPU_OPENCL_STATS': '1'})
            assert result.returncode == 0, (name, result.stdout, result.stderr)
            recovered = json.loads(output.read_text())
            assert recovered['hardware']['backend'] == 'opencl'
            assert recovered['recovered'] == 1 and recovered['matches'][0]['verified']
            assert recovered['matches'][0]['password_hex'] == b'A1B2C3D4EF'.hex()
            if name in ('legacy', 'compiler-fallback'):
                assert 'compact_input=0 loop_items=2' in result.stderr, result.stderr
            if name == 'compiler-fallback':
                assert 'gfx1200_profile=0' in result.stderr, result.stderr
        invalid = root/'invalid.cl'
        invalid.write_text('not an OpenCL kernel')
        result = subprocess.run([str(exe), 'benchmark', '--backend', 'opencl',
                                 '--kernel', str(invalid), '--batch', '1', '--repeats', '1'],
                                capture_output=True, text=True, timeout=90)
        assert result.returncode == 1 and 'OpenCL kernel compilation failed' in result.stderr
    print('PASS: OpenCL compact/legacy ABI, optional filter, compiler fallback and invalid source')


if __name__ == '__main__':
    main()
