"""Measure complete synthetic WPA2 recovery with an intentionally absent password."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--limit', type=int, default=32768)
    parser.add_argument('--backend', choices=('opencl', 'vulkan', 'hip'), default='vulkan')
    parser.add_argument('--batch', type=int, default=4096)
    parser.add_argument('--group', type=int, default=128)
    parser.add_argument('--chunk', type=int, default=128)
    parser.add_argument('--mask-file', type=Path)
    parser.add_argument('--warmup', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.repeats <= 10 or not 1 <= args.limit <= 1000000:
        parser.error('repeats must be 1..10; limit must be 1..1000000')
    max_batch = 262144
    max_chunk = 1024 if args.backend == 'vulkan' else 256
    if not (1 <= args.batch <= max_batch and 1 <= args.group <= 256 and 1 <= args.chunk <= max_chunk):
        parser.error(f'batch must be 1..{max_batch}; group must be 1..256; chunk must be 1..{max_chunk}')
    project = Path(__file__).resolve().parent.parent
    exe = args.exe.resolve()
    output = (args.output_dir or project/'build/recovery-benchmark').resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = (project/'tests/fixtures/all-types.hc22000').read_text().splitlines()[6]
    assert fixture.startswith('WPA*02*') and int(bytes.fromhex(fixture.split('*')[7])[6]) & 7 == 2
    path = output/'synthetic.hc22000'
    path.write_text(fixture+'\n', encoding='ascii')
    source = ['--mask-file', str(args.mask_file.resolve())] if args.mask_file else [
        '--mask', 'benchmark?d?d?d?d?d?d']
    command = [str(exe), 'recover', '--hashes', str(path), *source,
               '--backend', args.backend, '--limit', str(args.limit), '--batch', str(args.batch),
               '--group', str(args.group), '--chunk', str(args.chunk)]
    devices = subprocess.run([str(exe), 'devices', '--backend', args.backend], capture_output=True, text=True, check=True)
    (output/'devices.txt').write_text(devices.stdout, encoding='utf-8')
    samples = []
    for index in range(-int(args.warmup), args.repeats):
        start = time.perf_counter()
        process = subprocess.run(command, capture_output=True, text=True, timeout=180)
        wall = time.perf_counter()-start
        assert process.returncode in (2, 4), (process.returncode, process.stderr)
        assert 'FOUND' not in process.stdout and 'recovered=0/1' in process.stderr
        final = re.findall(r'status=\S+ next=(\d+) candidates=(\d+) rejected=(\d+) recovered=0/1 derivations=(\d+) elapsed_s=([\d.e+-]+) derivations_per_second=([\d.e+-]+)', process.stderr)[-1]
        position, candidates, rejected, derivations = map(int, final[:4])
        assert position == candidates == derivations == args.limit and rejected == 0
        if index < 0:
            (output/'warmup.txt').write_text(process.stdout+process.stderr, encoding='utf-8')
            continue
        sample = {'candidate_count': candidates, 'search_seconds': float(final[4]),
                  'search_candidates_per_second': float(final[5]), 'process_wall_seconds': wall,
                  'process_candidates_per_second': candidates/wall}
        samples.append(sample)
        (output/f'sample-{index+1}.txt').write_text(process.stdout+process.stderr, encoding='utf-8')
        print(json.dumps(sample), flush=True)
    manifest = {'measured_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'command': command,
                'version': subprocess.check_output([str(exe), '--version'], text=True).strip(),
                'workload': 'one exact-nonce WPA*02 key-version-2 record; generated absent passwords; complete recovery using the selected backend',
                'exe_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
                'backend': args.backend, 'warmup': args.warmup,
                'mask_file_sha256': hashlib.sha256(args.mask_file.read_bytes()).hexdigest() if args.mask_file else None,
                'kernel_sha256': hashlib.sha256((exe.parent/'kernels'/('pbkdf2.spv' if args.backend == 'vulkan' else 'pbkdf2.cl')).read_bytes()).hexdigest(),
                'samples': samples,
                'median_search_candidates_per_second': statistics.median(s['search_candidates_per_second'] for s in samples),
                'median_process_candidates_per_second': statistics.median(s['process_candidates_per_second'] for s in samples)}
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print('Median search candidates/s:', manifest['median_search_candidates_per_second'])
    print('Median whole-process candidates/s:', manifest['median_process_candidates_per_second'])


if __name__ == '__main__':
    main()
