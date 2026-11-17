#!/usr/bin/env python3
"""Select AMD's ROCm OpenCL ICD and validate/benchmark synthetic recovery on Linux."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
MASK = '?H' * 10


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def amd_icd(path):
    """Keep the supplied ICD's library selection; do not alter system registration."""
    lines = path.read_text(encoding='utf-8').splitlines()
    if len(lines) != 1 or not lines[0].strip():
        raise ValueError('The AMD ICD must contain exactly one library path or soname.')
    library = lines[0].strip()
    if not re.fullmatch(r'libamdocl64\.so(?:\.[A-Za-z0-9._-]+)?', Path(library).name):
        raise ValueError('Expected an AMD libamdocl64.so ICD; another runtime cannot validate ROCm.')
    if '/' in library and not Path(library).is_absolute():
        raise ValueError('Use an absolute AMD library path or its loader soname.')
    if Path(library).is_absolute() and not Path(library).is_file():
        raise ValueError('The AMD library named by the ICD does not exist.')
    return library


def runtime_environment(directory, library, inherited=None):
    directory.mkdir()
    (directory/'amdocl64.icd').write_text(library+'\n', encoding='utf-8')
    env = dict(os.environ if inherited is None else inherited)
    # Both the Khronos and ocl-icd Linux loaders support this isolated directory.
    # Additional ICDs and optional layers must not change the selected device.
    for key in ('OCL_ICD_FILENAMES', 'OPENCL_LAYERS', 'OPENCL_LAYER_PATH',
                'OCL_ICD_ASSUME_ICD_EXTENSION'):
        env.pop(key, None)
    env['OCL_ICD_VENDORS'] = str(directory)
    env['OPENCL_LAYERS'] = ''
    env['WIFI_GPU_VULKAN_STATS'] = '0'
    return env


def selected_amd(output, index):
    devices = dict(re.findall(r'^Device (\d+): (.+)$', output, re.MULTILINE))
    label = devices.get(str(index), '')
    if not re.search(r'\((?:AMD|Advanced Micro Devices,? Inc\.?)\)$', label):
        raise ValueError(f'OpenCL device {index} is not an enumerated AMD GPU: {label!r}')
    if re.search(r'llvmpipe|lavapipe|pocl|software', label, re.I):
        raise ValueError('Software OpenCL is not an AMD hardware benchmark.')
    return label


def recovery_sample(output, count, elapsed):
    matches = re.findall(
        r'status=candidate_limit next=(\d+) candidates=(\d+) rejected=(\d+) '
        r'recovered=0/1 derivations=(\d+) elapsed_s=([\d.e+-]+) '
        r'derivations_per_second=([\d.e+-]+)', output)
    if 'FOUND' in output or not matches:
        raise ValueError('Synthetic negative recovery did not finish at the candidate limit.')
    row = matches[-1]
    if tuple(map(int, row[:4])) != (count, count, 0, count):
        raise ValueError('Recovery skipped candidates, rejected input, or derived an unexpected count.')
    seconds, rate = map(float, row[4:])
    if not all(math.isfinite(value) and value > 0 for value in (seconds, rate, elapsed)):
        raise ValueError('Recovery reported an invalid duration or rate.')
    if not math.isclose(rate, count / seconds, rel_tol=0.001):
        raise ValueError('Recovery rate does not agree with candidate count and elapsed time.')
    return {'candidates': count, 'search_seconds': seconds,
            'search_candidates_per_second': rate, 'process_seconds': elapsed,
            'process_candidates_per_second': count / elapsed}


class Run:
    def __init__(self, folder, environment):
        self.folder, self.environment = folder, environment
        self.report = {'schema': 1, 'started_utc': datetime.now(timezone.utc).isoformat(),
                       'status': 'running', 'hardware_validated': False,
                       'host': platform.node(), 'system': platform.platform(), 'commands': []}

    def save(self):
        self.report['updated_utc'] = datetime.now(timezone.utc).isoformat()
        (self.folder/'report.json').write_text(json.dumps(self.report, indent=2)+'\n', encoding='utf-8')

    def command(self, name, arguments, expected=0, required=True, timeout=300):
        args = list(map(str, arguments))
        print(name, flush=True)
        entry = {'name': name, 'command': args, 'log': name+'.txt'}
        self.report['commands'].append(entry)
        started = time.perf_counter()
        try:
            result = subprocess.run(args, env=self.environment, capture_output=True, text=True,
                                    encoding='utf-8', errors='replace', timeout=timeout)
            output = result.stdout + result.stderr
            entry['exit_code'] = result.returncode
        except subprocess.TimeoutExpired as error:
            chunks = [error.stdout or b'', error.stderr or b'']
            output = ''.join(value.decode('utf-8', errors='replace') if isinstance(value, bytes)
                             else value for value in chunks) + '\nTimed out.\n'
            entry['error'] = 'timeout'
        except OSError as error:
            output = str(error)+'\n'
            entry['error'] = str(error)
        entry['process_seconds'] = time.perf_counter() - started
        (self.folder/entry['log']).write_text(output, encoding='utf-8')
        self.save()
        if required and entry.get('exit_code') != expected:
            raise RuntimeError(f'{name} failed; see {self.folder/entry["log"]}')
        return output, entry['process_seconds']


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--icd', type=Path, required=True, help='Installed AMD OpenCL .icd file')
    parser.add_argument('--device', type=int, default=0, help='Index within the isolated AMD ICD')
    parser.add_argument('--batch', type=int, default=65536)
    parser.add_argument('--group', type=int, default=256)
    parser.add_argument('--chunk', type=int, default=256)
    parser.add_argument('--limit', type=int, default=1048576)
    parser.add_argument('--repeats', type=int, default=5)
    parser.add_argument('--check-only', action='store_true')
    parser.add_argument('--output-dir', type=Path, default=ROOT/'build/rocm-opencl')
    args = parser.parse_args(argv)
    if platform.system() != 'Linux':
        parser.error('Run this command inside the Linux GPU guest.')
    if os.geteuid() == 0:
        parser.error('Run as the normal GPU job user so device permissions are checked.')
    if not (args.device >= 0 and 1 <= args.batch <= 262144 and 1 <= args.group <= 256
            and 1 <= args.chunk <= 1024 and 1 <= args.repeats <= 10
            and args.batch <= args.limit <= 16777216):
        parser.error('Invalid device/batch/group/chunk/repeats/limit; limit must cover a full batch.')
    exe, icd = args.exe.expanduser().resolve(), args.icd.expanduser().resolve()
    try:
        library = amd_icd(icd)
        if not exe.is_file():
            raise ValueError('Engine executable does not exist.')
    except (OSError, ValueError) as error:
        parser.error(str(error))
    args.output_dir.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix='run-', dir=args.output_dir)).resolve()
    run = Run(folder, runtime_environment(folder/'icd', library))
    try:
        run.report.update({'backend': 'opencl', 'runtime_selection': 'explicit-amdocl64-icd',
                           'icd': str(icd), 'icd_sha256': sha256(icd), 'library': library,
                           'runtime_environment': {key: run.environment[key] for key in
                               ('OCL_ICD_VENDORS', 'LD_LIBRARY_PATH', 'ROCR_VISIBLE_DEVICES',
                                'GPU_DEVICE_ORDINAL', 'HIP_VISIBLE_DEVICES',
                                'HSA_OVERRIDE_GFX_VERSION', 'HSA_ENABLE_SDMA')
                               if key in run.environment},
                           'exe_sha256': sha256(exe),
                           'kernel_sha256': sha256(exe.parent/'kernels/pbkdf2.cl'),
                           'parameters': {key: getattr(args, key) for key in
                                          ('device', 'batch', 'group', 'chunk', 'limit', 'repeats')}})
        if Path(library).is_absolute():
            run.report['library_sha256'] = sha256(Path(library))
        run.report['version'] = run.command('version', [exe, '--version'])[0].strip()
        for name, command in (
                ('kernel', ['uname', '-a']), ('identity', ['id']),
                ('packages', ['dpkg-query', '-W', '*rocm*', '*opencl*', '*hsa*', '*amdgpu*']),
                ('pci', ['lspci', '-nnk']), ('clinfo', ['clinfo']),
                ('amd-smi-before', ['amd-smi', 'metric'])):
            run.command(name, command, required=False, timeout=30)
        devices, _ = run.command('devices', [exe, 'devices', '--backend', 'opencl'])
        run.report['selected_device'] = selected_amd(devices, args.device)
        if args.check_only:
            run.report['status'] = 'preflight_passed'
        else:
            gpu = ['--backend', 'opencl', '--device', str(args.device)]
            run.command('selftest', [exe, 'selftest', *gpu], timeout=600)
            # Use the independent hashlib/hmac fixture generator already used by tests.
            sys.path.insert(0, str(ROOT/'tests'))
            import make_fixtures as oracle
            positive = folder/'positive.hc22000'
            positive.write_text(oracle.record(password=b'000000FFFF')+'\n', encoding='ascii')
            result_path = folder/'positive.json'
            options = ['--mask', MASK, '--batch', str(args.batch), '--group', str(args.group),
                       '--chunk', str(args.chunk)]
            run.command('known-positive', [exe, 'recover', *gpu, '--hashes', positive, *options,
                                          '--limit', '65536', '--output', result_path])
            result = json.loads(result_path.read_text())
            if not (result['status'] == 'recovered' and result['recovered'] == 1
                    and len(result['matches']) == 1 and result['matches'][0]['verified']
                    and result['matches'][0]['password_hex'] == b'000000FFFF'.hex()
                    and result['hardware']['backend'] == 'opencl'
                    and result['hardware']['compute_kind'] == 'gpu'):
                raise ValueError('Known-positive synthetic recovery verification failed.')
            run.report['hardware_validated'] = True
            negative = folder/'absent.hc22000'
            negative.write_text(oracle.record(password=b'absent-benchmark')+'\n', encoding='ascii')
            command = [exe, 'recover', *gpu, '--hashes', negative, *options,
                       '--limit', str(args.limit)]
            samples = []
            for index in range(-1, args.repeats):
                name = 'warmup' if index < 0 else f'sample-{index+1}'
                output, elapsed = run.command(name, command, expected=4)
                sample = recovery_sample(output, args.limit, elapsed)
                if index >= 0:
                    samples.append(sample)
                    run.report['samples'] = samples
                    print(f'{sample["search_candidates_per_second"]:.1f} candidates/s', flush=True)
            rates = [sample['search_candidates_per_second'] for sample in samples]
            run.report.update({'status': 'passed', 'median_candidates_per_second': statistics.median(rates),
                               'range_candidates_per_second': [min(rates), max(rates)],
                               'median_process_candidates_per_second': statistics.median(
                                   sample['process_candidates_per_second'] for sample in samples)})
            run.command('amd-smi-after', ['amd-smi', 'metric'], required=False, timeout=30)
            print(f'Median full recovery: {statistics.median(rates):.1f} candidates/s', flush=True)
    except (OSError, RuntimeError, ValueError, KeyError, IndexError) as error:
        run.report.update({'status': 'failed', 'error': str(error)})
        print(f'ERROR: {error}', file=sys.stderr)
    finally:
        run.save()
        print(f'Report: {folder/"report.json"}', flush=True)
    return 0 if run.report['status'] in ('passed', 'preflight_passed') else 1


if __name__ == '__main__':
    raise SystemExit(main())
