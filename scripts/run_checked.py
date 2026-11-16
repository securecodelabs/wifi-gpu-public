"""Persist complete child diagnostics before reporting failures through PowerShell."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--logs',type=Path,required=True)
    p.add_argument('--name',required=True)
    p.add_argument('command',nargs=argparse.REMAINDER)
    args=p.parse_args()
    command=args.command[1:] if args.command[:1]==['--'] else args.command
    if not command or not args.name.replace('-','').isalnum(): p.error('A command and a simple log name are required')
    args.logs.mkdir(parents=True,exist_ok=True)
    result=subprocess.run(command,capture_output=True) # communicate drains both pipes concurrently
    (args.logs/(args.name+'.stdout.log')).write_bytes(result.stdout)
    (args.logs/(args.name+'.stderr.log')).write_bytes(result.stderr)
    (args.logs/(args.name+'.json')).write_text(json.dumps({
        'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'command':command,'exit_code':result.returncode},indent=2)+'\n',encoding='utf-8')
    # Native stderr becomes a terminating ErrorRecord in some PowerShell 5
    # wrappers. Emit the retained diagnostics through stdout, then return failure.
    sys.stdout.buffer.write(result.stdout)
    sys.stdout.buffer.write(result.stderr)
    sys.stdout.buffer.flush()
    if result.returncode: print(f'FAILED: {args.name}; exit={result.returncode}; complete logs: {args.logs}',flush=True)
    return result.returncode


if __name__=='__main__': sys.exit(main())
