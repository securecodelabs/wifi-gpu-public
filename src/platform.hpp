#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace wifi {
inline const char* cpu_provider() {
#ifdef _WIN32
    return "BCrypt";
#else
    return "OpenSSL";
#endif
}
inline std::filesystem::path executable_directory() {
#ifdef _WIN32
    std::wstring path(32768,L'\0');
    const auto n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!n || n>=path.size()) throw std::runtime_error("Cannot locate executable");
    path.resize(n); return std::filesystem::path(path).parent_path();
#else
    return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
}
class DynamicLibrary {
#ifdef _WIN32
    HMODULE handle=nullptr;
#else
    void* handle=nullptr;
#endif
public:
    DynamicLibrary(const wchar_t* windows_name,const char* linux_name) {
#ifdef _WIN32
        (void)linux_name;
        handle=LoadLibraryExW(windows_name,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
        (void)windows_name;
        handle=dlopen(linux_name,RTLD_NOW|RTLD_LOCAL);
#endif
        if(!handle) throw std::runtime_error(std::string("Cannot load GPU runtime ")+linux_name+"; install a compatible runtime/driver or use --cpu");
    }
    ~DynamicLibrary() {
#ifdef _WIN32
        if(handle) FreeLibrary(handle);
#else
        if(handle) dlclose(handle);
#endif
    }
    auto symbol(const char* name) const {
#ifdef _WIN32
        return GetProcAddress(handle,name);
#else
        return dlsym(handle,name);
#endif
    }
    DynamicLibrary(const DynamicLibrary&)=delete;
    DynamicLibrary& operator=(const DynamicLibrary&)=delete;
};
}
