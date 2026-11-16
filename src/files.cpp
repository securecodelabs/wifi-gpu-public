#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "files.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace wifi {
namespace {
[[noreturn]] void fail(const char* operation,const std::filesystem::path& path) {
    auto error=GetLastError(); throw std::runtime_error(std::string(operation)+": "+path.u8string()+" (Windows "+std::to_string(error)+")");
}
struct Handle {
    HANDLE h=INVALID_HANDLE_VALUE;
    ~Handle() { if(h!=INVALID_HANDLE_VALUE) CloseHandle(h); }
};
void publish_snapshot(const std::filesystem::path& temp,const std::filesystem::path& path,DWORD flags) {
    // A short-lived reader/scanner can deny rename even after all bytes have
    // been flushed. Retry the same atomic operation, never delete/truncate the
    // previous checkpoint or relax the caller's overwrite policy.
    const auto started=GetTickCount64();
    DWORD delay=5;
    for(;;) {
        if(MoveFileExW(temp.c_str(),path.c_str(),flags)) return;
        const auto error=GetLastError();
        const auto elapsed=GetTickCount64()-started;
        const bool temporary=error==ERROR_ACCESS_DENIED || error==ERROR_SHARING_VIOLATION || error==ERROR_LOCK_VIOLATION;
        if(!temporary || elapsed>=1000) {
            SetLastError(error);
            fail(temporary?"Cannot publish snapshot after retrying for 1 second":"Cannot publish snapshot",path);
        }
        Sleep(static_cast<DWORD>(std::min<ULONGLONG>(delay,1000-elapsed)));
        delay=std::min<DWORD>(delay*2,50);
    }
}
}
std::filesystem::path absolute_path(const std::string& path) {
    if(path.empty()) throw std::invalid_argument("Empty file path");
    return std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal();
}
struct Input::Impl {
    Handle handle;
    std::uint64_t size=0;
    std::array<std::uint8_t,65536> buf{};
    DWORD position=0,length=0;
};
Input::Input(const std::filesystem::path& p):path(p),impl_(std::make_unique<Impl>()) {
    impl_->handle.h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(impl_->handle.h==INVALID_HANDLE_VALUE) fail("Cannot open input",path);
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(impl_->handle.h,&info)) fail("Cannot inspect input",path);
    if(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) throw std::invalid_argument("Input is a directory: "+path.u8string());
    impl_->size=(static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
}
Input::~Input()=default;
std::uint64_t Input::size() const { return impl_->size; }
bool Input::line(Bytes& out,std::size_t maximum,bool& oversized) {
    out.clear(); oversized=false; bool any=false;
    while(true) {
        if(impl_->position==impl_->length) {
            if(!ReadFile(impl_->handle.h,impl_->buf.data(),static_cast<DWORD>(impl_->buf.size()),&impl_->length,nullptr)) fail("Cannot read input",path);
            impl_->position=0;
            if(!impl_->length) break;
        }
        const auto* begin=impl_->buf.data()+impl_->position;
        const auto available=static_cast<std::size_t>(impl_->length-impl_->position);
        const auto* newline=static_cast<const std::uint8_t*>(std::memchr(begin,'\n',available));
        const auto size=newline?static_cast<std::size_t>(newline-begin):available;
        const auto keep=std::min(size,maximum+1-out.size());
        out.insert(out.end(),begin,begin+keep);
        oversized=oversized || keep<size; any=true;
        impl_->position+=static_cast<DWORD>(size+(newline?1:0));
        if(newline) break;
    }
    if(!out.empty() && out.back()=='\r' && !oversized) out.pop_back();
    if(out.size()>maximum) oversized=true;
    return any;
}
void Input::rewind() {
    LARGE_INTEGER zero{};
    if(!SetFilePointerEx(impl_->handle.h,zero,nullptr,FILE_BEGIN)) fail("Cannot rewind input",path);
    impl_->position=impl_->length=0;
}
std::string Input::sha256(const std::function<void(const std::uint8_t*,std::size_t)>& inspect) {
    rewind(); Hash hash(Algorithm::Sha256); DWORD n=0;
    do {
        if(!ReadFile(impl_->handle.h,impl_->buf.data(),static_cast<DWORD>(impl_->buf.size()),&n,nullptr)) fail("Cannot hash input",path);
        hash.update(impl_->buf.data(),n);
        if(inspect && n) inspect(impl_->buf.data(),n);
    } while(n);
    rewind(); return hex_bytes(hash.finish());
}
std::string Input::contents(std::size_t maximum) {
    rewind(); std::string out; DWORD n=0;
    do {
        if(!ReadFile(impl_->handle.h,impl_->buf.data(),static_cast<DWORD>(impl_->buf.size()),&n,nullptr)) fail("Cannot read input",path);
        if(n>maximum-out.size()) throw std::invalid_argument("Input exceeds size limit: "+path.u8string());
        out.append(reinterpret_cast<char*>(impl_->buf.data()),n);
    } while(n);
    rewind(); return out;
}
struct Snapshot::Impl { Handle lock; bool replace=false; unsigned serial=0; };
Snapshot::Snapshot(const std::filesystem::path& p,bool replace):path(p),impl_(std::make_unique<Impl>()) {
    if(!std::filesystem::is_directory(path.parent_path())) throw std::invalid_argument("Output directory does not exist: "+path.parent_path().u8string());
    auto lock=path; lock+=L".lock";
    impl_->lock.h=CreateFileW(lock.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_FLAG_DELETE_ON_CLOSE,nullptr);
    if(impl_->lock.h==INVALID_HANDLE_VALUE) fail("Cannot acquire output/session lock (another run or an existing .lock file)",path);
    if(std::filesystem::exists(path) && !replace) throw std::invalid_argument("Refusing to overwrite "+path.u8string()+"; use --overwrite or --resume as appropriate");
    impl_->replace=replace;
}
Snapshot::~Snapshot()=default;
void Snapshot::write(const std::string& contents) {
    auto temp=path;
    temp+=L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(++impl_->serial);
    Handle handle;
    handle.h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle.h==INVALID_HANDLE_VALUE) fail("Cannot create snapshot temporary file",temp);
    try {
        std::size_t done=0;
        while(done<contents.size()) {
            DWORD wrote=0,n=static_cast<DWORD>(std::min<std::size_t>(contents.size()-done,1048576));
            if(!WriteFile(handle.h,contents.data()+done,n,&wrote,nullptr) || !wrote) fail("Cannot write snapshot",temp);
            done+=wrote;
        }
        if(!FlushFileBuffers(handle.h)) fail("Cannot flush snapshot",temp);
        CloseHandle(handle.h); handle.h=INVALID_HANDLE_VALUE;
        DWORD flags=MOVEFILE_WRITE_THROUGH|(impl_->replace?MOVEFILE_REPLACE_EXISTING:0);
        publish_snapshot(temp,path,flags);
        impl_->replace=true;
    } catch(...) {
        if(handle.h!=INVALID_HANDLE_VALUE) { CloseHandle(handle.h); handle.h=INVALID_HANDLE_VALUE; }
        DeleteFileW(temp.c_str()); throw;
    }
}
}
