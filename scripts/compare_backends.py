"""Interleaved, checked full-recovery comparison on synthetic absent candidates."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import time


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--exe',required=True,type=Path)
    parser.add_argument('--baseline-exe',type=Path)
    parser.add_argument('--output-dir',required=True,type=Path)
    parser.add_argument('--repeats',default=5,type=int)
    parser.add_argument('--limit',default=32768,type=int)
    parser.add_argument('--batch',default=4096,type=int)
    parser.add_argument('--group',default=128,type=int)
    parser.add_argument('--chunk',default=128,type=int)
    parser.add_argument('--baseline-chunk',type=int)
    parser.add_argument('--include-cpu',action='store_true')
    parser.add_argument('--cpu-threads',default=min(os.cpu_count() or 1,64),type=int)
    args=parser.parse_args()
    if not (1<=args.repeats<=10 and 1<=args.limit<=1000000 and
            1<=args.batch<=4096 and 1<=args.group<=256 and 1<=args.chunk<=256 and
            1<=args.cpu_threads<=64 and (args.baseline_chunk is None or 1<=args.baseline_chunk<=256)):
        parser.error('Invalid repeat/limit/batch/group/chunk value')
    root=Path(__file__).resolve().parent.parent
    output=args.output_dir.resolve(); output.mkdir(parents=True,exist_ok=True)
    fixture=output/'synthetic.hc22000'
    row=(root/'tests/fixtures/all-types.hc22000').read_text().splitlines()[6]
    assert row.startswith('WPA*02*') and bytes.fromhex(row.split('*')[7])[6]&7==2
    fixture.write_text(row+'\n',encoding='ascii')
    cases=[('opencl',args.exe.resolve(),'opencl'),('vulkan',args.exe.resolve(),'vulkan')]
    if args.baseline_exe: cases.append(('vulkan_baseline',args.baseline_exe.resolve(),'vulkan'))
    if args.include_cpu: cases.append(('cpu',args.exe.resolve(),'cpu'))
    environment=os.environ.copy(); environment['WIFI_GPU_VULKAN_STATS']='0'
    report={'measured_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'workload':'one exact-nonce WPA*02 key-version-2 record; absent generated passwords; selected-backend PBKDF2 plus CPU MIC verification',
            'parameters':{'limit':args.limit,'batch':args.batch,'group':args.group,'chunk':args.chunk},
            'fixture_sha256':sha(fixture),'statistics_capture_enabled':False,
            'warmup':'one unrecorded 4096-candidate recovery per case',
            'cpu_threads':args.cpu_threads if args.include_cpu else None,
            'baseline_chunk':args.baseline_chunk or args.chunk,
            'cases':{},'execution_order':[]}
    for label,exe,backend in cases:
        if backend!='cpu':
            device=subprocess.run([str(exe),'devices','--backend',backend],env=environment,capture_output=True,text=True,check=True)
            (output/f'{label}-devices.txt').write_text(device.stdout,encoding='utf-8')
        report['cases'][label]={'exe':str(exe),'exe_sha256':sha(exe),
            'version':subprocess.check_output([str(exe),'--version'],env=environment,text=True).strip(),
            'kernel_sha256':None if backend=='cpu' else sha(exe.parent/'kernels'/('pbkdf2.cl' if backend=='opencl' else 'pbkdf2.spv')),
            'samples':[]}

    def run(case,iteration=None):
        label,exe,backend=case
        limit=min(4096,args.limit) if iteration is None else args.limit
        options=['--cpu','--threads',str(args.cpu_threads)] if backend=='cpu' else [
            '--backend',backend,'--group',str(args.group),'--chunk',str((args.baseline_chunk or args.chunk) if label=='vulkan_baseline' else args.chunk)]
        command=[str(exe),'recover','--hashes',str(fixture),*options,
                 '--mask','benchmark?d?d?d?d?d?d','--limit',str(limit),
                 '--batch',str(args.batch)]
        start=time.perf_counter()
        process=subprocess.run(command,env=environment,capture_output=True,text=True,timeout=180)
        elapsed=time.perf_counter()-start
        if process.returncode not in (2,4): raise RuntimeError((command,process.returncode,process.stdout,process.stderr))
        assert 'FOUND' not in process.stdout and 'VULKAN_STATS' not in process.stderr
        found=re.findall(r'status=\S+ next=(\d+) candidates=(\d+) rejected=(\d+) recovered=0/1 derivations=(\d+) elapsed_s=([\d.e+-]+) derivations_per_second=([\d.e+-]+)',process.stderr)
        assert found,process.stderr
        next_index,count,rejected,derived=map(int,found[-1][:4])
        assert next_index==count==derived==limit and rejected==0
        if iteration is None: return
        name=f'{label}-{iteration+1}.txt'
        (output/name).write_text(process.stdout+process.stderr,encoding='utf-8')
        sample={'round':iteration+1,'candidates':count,'search_seconds':float(found[-1][4]),
                'search_candidates_per_second':float(found[-1][5]),'process_seconds':elapsed,
                'process_candidates_per_second':count/elapsed,'log':name,'command':command,'exit_code':process.returncode}
        report['cases'][label]['samples'].append(sample)
        report['execution_order'].append(label)
        print(f"round={iteration+1} {label}: search={sample['search_candidates_per_second']:.1f}/s process={count/elapsed:.1f}/s",flush=True)

    for case in cases: run(case)
    for iteration in range(args.repeats):
        shift=(iteration//2)%len(cases)
        order=cases[shift:]+cases[:shift]
        if iteration%2: order=list(reversed(order))
        for case in order: run(case,iteration)
    for case in report['cases'].values():
        samples=case['samples']
        for metric in ('search_candidates_per_second','process_candidates_per_second'):
            case['median_'+metric]=statistics.median(s[metric] for s in samples)
            case['range_'+metric]=[min(s[metric] for s in samples),max(s[metric] for s in samples)]
    report['vulkan_vs_opencl_search_ratio']=report['cases']['vulkan']['median_search_candidates_per_second']/report['cases']['opencl']['median_search_candidates_per_second']
    (output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    for name,case in report['cases'].items():
        print(name,'median search',case['median_search_candidates_per_second'],
              'median whole process',case['median_process_candidates_per_second'])
    print('Vulkan/OpenCL search ratio:',report['vulkan_vs_opencl_search_ratio'])


if __name__=='__main__': main()
