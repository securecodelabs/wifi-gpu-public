"""Read live Windows snapshots without requesting a delete/rename exclusion."""
import ctypes
from ctypes import wintypes

import os

if os.name == "nt":
    _kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    _kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,
                                 wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
    _kernel.CreateFileW.restype=wintypes.HANDLE
    _kernel.ReadFile.argtypes=[wintypes.HANDLE,ctypes.c_void_p,wintypes.DWORD,ctypes.POINTER(wintypes.DWORD),ctypes.c_void_p]
    _kernel.ReadFile.restype=wintypes.BOOL
    _kernel.CloseHandle.argtypes=[wintypes.HANDLE]; _kernel.CloseHandle.restype=wintypes.BOOL


def read_snapshot(path):
    if os.name != "nt":
        return path.read_text(encoding="utf-8")
    handle=_kernel.CreateFileW(str(path),0x80000000,7,None,3,0x80,None) # read; share read/write/delete
    if handle==ctypes.c_void_p(-1).value: raise ctypes.WinError(ctypes.get_last_error())
    try:
        data=bytearray(); buffer=ctypes.create_string_buffer(16384); count=wintypes.DWORD()
        while True:
            if not _kernel.ReadFile(handle,buffer,len(buffer),ctypes.byref(count),None):
                raise ctypes.WinError(ctypes.get_last_error())
            if not count.value: return data.decode('utf-8')
            data.extend(buffer.raw[:count.value])
            if len(data)>32*1024*1024: raise ValueError('Snapshot exceeds test-reader limit')
    finally:
        _kernel.CloseHandle(handle)
