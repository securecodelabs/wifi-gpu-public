"""Check recovery orchestration complexity locally or in CI, without a GPU build."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang-tidy', default='clang-tidy')
    args = parser.parse_args()
    tool = shutil.which(args.clang_tidy)
    if not tool:
        parser.error('Install the checker: python -m pip install -r .github/requirements-complexity.txt')
    root = Path(__file__).resolve().parents[1]
    version = re.search(r'project\(wifi_gpu VERSION ([\d.]+) ', (root/'CMakeLists.txt').read_text())
    if not version:
        parser.error('Cannot read the project version')
    config = {
        'Checks': '-*,readability-function-cognitive-complexity',
        'WarningsAsErrors': 'readability-function-cognitive-complexity',
        'CheckOptions': {'readability-function-cognitive-complexity.Threshold': 25},
    }
    result = subprocess.run([
        tool, str(root/'src/recovery.cpp'), '--config='+json.dumps(config), '--',
        '-std=c++17', '-I'+str(root/'src'), '-DWIFI_GPU_VERSION="'+version[1]+'"',
    ], cwd=root, check=False)
    if result.returncode:
        return result.returncode
    print('PASS: recovery.cpp functions have cognitive complexity <= 25 (clang-tidy)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
