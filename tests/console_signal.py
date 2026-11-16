"""Send Ctrl+C only to a test child's private Windows console, recording each API result."""
import argparse
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import time


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--pid',type=int,required=True)
    p.add_argument('--report',type=Path,required=True)
    args=p.parse_args()
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.FreeConsole.argtypes=[]; kernel.FreeConsole.restype=wintypes.BOOL
    kernel.AttachConsole.argtypes=[wintypes.DWORD]; kernel.AttachConsole.restype=wintypes.BOOL
    kernel.SetConsoleCtrlHandler.argtypes=[ctypes.c_void_p,wintypes.BOOL]; kernel.SetConsoleCtrlHandler.restype=wintypes.BOOL
    kernel.GenerateConsoleCtrlEvent.argtypes=[wintypes.DWORD,wintypes.DWORD]; kernel.GenerateConsoleCtrlEvent.restype=wintypes.BOOL
    report={'pid':args.pid,'operations':[]}
    def call(name,*values,required=True):
        ctypes.set_last_error(0)
        result=bool(getattr(kernel,name)(*values)); error=ctypes.get_last_error() if not result else 0
        report['operations'].append({'api':name,'success':result,'windows_error':error})
        args.report.write_text(json.dumps(report,indent=2),encoding='utf-8')
        if required and not result: raise ctypes.WinError(error)
    call('FreeConsole',required=False) # The helper may already have no console.
    call('AttachConsole',args.pid)
    call('SetConsoleCtrlHandler',None,True)
    call('GenerateConsoleCtrlEvent',0,0)
    time.sleep(.1)


if __name__=='__main__': main()
