#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <climits>
#endif
#include "gpu.hpp"
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace wifi {
Pmk reference(const Bytes& password, const Bytes& ssid, std::uint32_t iterations) {
    if(password.size()>63 || ssid.size()>32 || iterations==0)
        throw std::invalid_argument("Invalid reference PBKDF2 input");
#ifdef _WIN32
    BCRYPT_ALG_HANDLE provider=nullptr;
    auto status=BCryptOpenAlgorithmProvider(&provider,BCRYPT_SHA1_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if(status<0) throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    Pmk pmk{};
    std::uint8_t empty=0;
    auto p=password.empty()?&empty:const_cast<std::uint8_t*>(password.data());
    auto s=ssid.empty()?&empty:const_cast<std::uint8_t*>(ssid.data());
    status=BCryptDeriveKeyPBKDF2(provider,p,static_cast<ULONG>(password.size()),s,
        static_cast<ULONG>(ssid.size()),iterations,pmk.data(),static_cast<ULONG>(pmk.size()),0);
    BCryptCloseAlgorithmProvider(provider,0);
    if(status<0) throw std::runtime_error("BCryptDeriveKeyPBKDF2 failed");
    return pmk;
#else
    if(iterations>INT_MAX) throw std::invalid_argument("PBKDF2 iteration count too large");
    Pmk pmk{};
    const unsigned char empty=0;
    if(PKCS5_PBKDF2_HMAC(password.empty()?"":reinterpret_cast<const char*>(password.data()),static_cast<int>(password.size()),
        ssid.empty()?&empty:ssid.data(),static_cast<int>(ssid.size()),static_cast<int>(iterations),EVP_sha1(),static_cast<int>(pmk.size()),pmk.data())!=1)
        throw std::runtime_error("OpenSSL PBKDF2 derivation failed");
    return pmk;
#endif
}
Bytes bytes(const std::string& value) { return Bytes(value.begin(),value.end()); }
std::string hex(const Pmk& pmk) {
    std::ostringstream out;
    for(auto b:pmk) out<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<unsigned>(b);
    return out.str();
}
}
