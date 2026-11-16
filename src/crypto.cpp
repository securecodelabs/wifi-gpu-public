#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "crypto.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace wifi {
namespace {
void ok(NTSTATUS s,const char* name) {
    if(s<0) throw std::runtime_error(std::string(name)+" failed, NTSTATUS="+std::to_string(static_cast<unsigned long>(s)));
}
struct Provider {
    BCRYPT_ALG_HANDLE handle=nullptr;
    Provider(const wchar_t* name,ULONG flags) { ok(BCryptOpenAlgorithmProvider(&handle,name,nullptr,flags),"BCryptOpenAlgorithmProvider"); }
    ~Provider() { if(handle) BCryptCloseAlgorithmProvider(handle,0); }
    Provider(const Provider&)=delete;
    Provider& operator=(const Provider&)=delete;
};
BCRYPT_ALG_HANDLE provider(Algorithm a) {
    // Lazily opened once; each Hash owns its independent hash state.
    switch(a) {
    case Algorithm::Sha256: { static Provider p(BCRYPT_SHA256_ALGORITHM,0); return p.handle; }
    case Algorithm::HmacSha1: { static Provider p(BCRYPT_SHA1_ALGORITHM,BCRYPT_ALG_HANDLE_HMAC_FLAG); return p.handle; }
    case Algorithm::HmacSha256: { static Provider p(BCRYPT_SHA256_ALGORITHM,BCRYPT_ALG_HANDLE_HMAC_FLAG); return p.handle; }
    case Algorithm::HmacMd5: { static Provider p(BCRYPT_MD5_ALGORITHM,BCRYPT_ALG_HANDLE_HMAC_FLAG); return p.handle; }
    case Algorithm::Cmac: { static Provider p(BCRYPT_AES_CMAC_ALGORITHM,0); return p.handle; }
    }
    throw std::invalid_argument("Unknown crypto algorithm");
}
}
struct Hash::Impl {
    BCRYPT_HASH_HANDLE handle=nullptr;
    ULONG size=0;
    bool finished=false;
    ~Impl() { if(handle) BCryptDestroyHash(handle); }
};
Hash::Hash(Algorithm algorithm,const Bytes& key):impl_(std::make_unique<Impl>()) {
    if(key.size()>std::numeric_limits<ULONG>::max()) throw std::invalid_argument("Crypto key too long");
    auto p=provider(algorithm); ULONG got=0;
    ok(BCryptGetProperty(p,BCRYPT_HASH_LENGTH,reinterpret_cast<PUCHAR>(&impl_->size),sizeof(impl_->size),&got,0),"hash length");
    if(impl_->size==0 || impl_->size>64) throw std::runtime_error("Unexpected digest length");
    ok(BCryptCreateHash(p,&impl_->handle,nullptr,0,key.empty()?nullptr:const_cast<PUCHAR>(key.data()),static_cast<ULONG>(key.size()),0),"BCryptCreateHash");
}
Hash::~Hash()=default;
void Hash::update(const void* data,std::size_t length) {
    if(impl_->finished) throw std::logic_error("Hash already finished");
    const auto* p=static_cast<const std::uint8_t*>(data);
    while(length) {
        auto n=static_cast<ULONG>(std::min<std::size_t>(length,1048576));
        ok(BCryptHashData(impl_->handle,const_cast<PUCHAR>(p),n,0),"BCryptHashData");
        p+=n; length-=n;
    }
}
Bytes Hash::finish() {
    if(impl_->finished) throw std::logic_error("Hash already finished");
    Bytes out(impl_->size); ok(BCryptFinishHash(impl_->handle,out.data(),impl_->size,0),"BCryptFinishHash");
    impl_->finished=true; return out;
}
}
