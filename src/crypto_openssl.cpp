#include "crypto.hpp"
#include <openssl/evp.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <stdexcept>

namespace wifi {
namespace {
void check(int result,const char* operation) {
    if(result!=1) throw std::runtime_error(std::string("OpenSSL ")+operation+" failed");
}
}
struct Hash::Impl {
    EVP_MD_CTX* hash=nullptr;
    EVP_MAC* algorithm=nullptr;
    EVP_MAC_CTX* mac=nullptr;
    bool finished=false;
    ~Impl() { EVP_MD_CTX_free(hash); EVP_MAC_CTX_free(mac); EVP_MAC_free(algorithm); }
};
Hash::Hash(Algorithm algorithm,const Bytes& key):impl_(std::make_unique<Impl>()) {
    auto& p=*impl_;
    if(algorithm==Algorithm::Sha256) {
        p.hash=EVP_MD_CTX_new();
        if(!p.hash) throw std::runtime_error("OpenSSL hash allocation failed");
        check(EVP_DigestInit_ex(p.hash,EVP_sha256(),nullptr),"SHA256 initialization");
        return;
    }
    const bool cmac=algorithm==Algorithm::Cmac;
    const char* name=nullptr;
    switch(algorithm) {
    case Algorithm::HmacSha1: name="SHA1"; break;
    case Algorithm::HmacSha256: name="SHA256"; break;
    case Algorithm::HmacMd5: name="MD5"; break;
    case Algorithm::Cmac: name="AES-128-CBC"; break;
    default: throw std::invalid_argument("Unknown crypto algorithm");
    }
    if(cmac && key.size()!=16) throw std::invalid_argument("AES-CMAC requires a 16-byte key");
    p.algorithm=EVP_MAC_fetch(nullptr,cmac?"CMAC":"HMAC",nullptr);
    if(!p.algorithm) throw std::runtime_error("OpenSSL MAC algorithm unavailable");
    p.mac=EVP_MAC_CTX_new(p.algorithm);
    if(!p.mac) throw std::runtime_error("OpenSSL MAC allocation failed");
    OSSL_PARAM params[]={OSSL_PARAM_construct_utf8_string(cmac?OSSL_MAC_PARAM_CIPHER:OSSL_MAC_PARAM_DIGEST,const_cast<char*>(name),0),OSSL_PARAM_construct_end()};
    const unsigned char empty=0;
    check(EVP_MAC_init(p.mac,key.empty()?&empty:key.data(),key.size(),params),"MAC initialization");
}
Hash::~Hash()=default;
void Hash::update(const void* data,std::size_t length) {
    if(impl_->finished) throw std::logic_error("Hash already finished");
    if(!length) return;
    check(impl_->hash ? EVP_DigestUpdate(impl_->hash,data,length) : EVP_MAC_update(impl_->mac,static_cast<const unsigned char*>(data),length),"digest update");
}
Bytes Hash::finish() {
    if(impl_->finished) throw std::logic_error("Hash already finished");
    Bytes out(EVP_MAX_MD_SIZE); std::size_t size=0;
    if(impl_->hash) {
        unsigned int n=0; check(EVP_DigestFinal_ex(impl_->hash,out.data(),&n),"digest finish"); size=n;
    } else check(EVP_MAC_final(impl_->mac,out.data(),&size,out.size()),"MAC finish");
    if(!size || size>out.size()) throw std::runtime_error("Unexpected digest length");
    out.resize(size); impl_->finished=true; return out;
}
}
