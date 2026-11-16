"""HIPRTC diagnostics and the optional GPU verification kernel contract."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

import make_fixtures as oracle


parser = argparse.ArgumentParser()
parser.add_argument('--exe', type=Path, required=True)
args = parser.parse_args()
exe = args.exe.resolve()
with tempfile.TemporaryDirectory(prefix='wifi-hip-setup-') as temporary:
    root = Path(temporary)
    source = root/'custom.cl'
    for text, diagnostic in [('', 'Empty kernel source'),
                             ('not a kernel', 'HIP kernel compilation failed'),
                             ('extern "C" __global__ void unrelated() {}', 'hipModuleGetFunction')]:
        source.write_text(text)
        result = subprocess.run([str(exe), 'benchmark', '--backend', 'hip', '--kernel', str(source),
                                 '--batch', '1', '--repeats', '1'], capture_output=True, text=True, timeout=60)
        assert result.returncode == 1 and diagnostic in result.stderr, result
    # Custom derivation kernels may omit the optional WPA filter. Recovery must
    # still produce an independently verified hit through CPU verification.
    kernel = (exe.parent/'kernels/pbkdf2.cl').read_text()
    marker = '// WPA2 EAPOL v2 with one exact nonce context.'
    assert marker in kernel
    source.write_text(kernel.split(marker)[0])
    hashes = root/'synthetic.hc22000'
    hashes.write_text(oracle.record(password=b'A1B2C3D4EF')+'\n')
    output = root/'result.json'
    # Omitted backend must select HIP in a HIP-enabled build.
    result = subprocess.run([str(exe), 'recover', '--kernel', str(source),
                             '--hashes', str(hashes), '--mask', 'A1B2C3D4?H?H', '--batch', '256',
                             '--output', str(output)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result
    recovered = json.loads(output.read_text())
    assert recovered['hardware']['backend'] == 'hip' and recovered['recovered'] == 1
    assert recovered['matches'][0]['verified']
    assert recovered['matches'][0]['password_hex'] == b'A1B2C3D4EF'.hex()
    # Older custom kernels have two loop items per candidate. Force that ABI
    # even on gfx1200, then exercise a partial group and verify the full PMK.
    source.write_text('#undef WIFI_GPU_HIP_PAIR\n'+kernel)
    output = root/'legacy-result.json'
    result = subprocess.run([str(exe), 'recover', '--backend', 'hip', '--kernel', str(source),
                             '--hashes', str(hashes), '--mask', 'A1B2C3D4?H?H', '--batch', '67',
                             '--output', str(output)], capture_output=True, text=True, timeout=60,
                            env={**os.environ, 'WIFI_GPU_HIP_STATS': '1'})
    assert result.returncode == 0, result
    assert 'pbkdf2_loop_split' in result.stderr, result.stderr
    recovered = json.loads(output.read_text())
    assert recovered['recovered'] == 1 and recovered['matches'][0]['verified']
    assert recovered['matches'][0]['password_hex'] == b'A1B2C3D4EF'.hex()
print('PASS: HIP default, HIPRTC errors, missing entry point, optional filter and legacy split-loop ABI')
