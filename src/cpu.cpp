#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#include <climits>
#endif
#include "cpu.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace wifi {
namespace {
#ifdef _WIN32
struct Provider {
    BCRYPT_ALG_HANDLE handle=nullptr;
    Provider() {
        if(BCryptOpenAlgorithmProvider(&handle,BCRYPT_SHA1_ALGORITHM,nullptr,BCRYPT_ALG_HANDLE_HMAC_FLAG)<0)
            throw std::runtime_error("CPU SHA-1 provider initialization failed");
    }
    ~Provider() { if(handle) BCryptCloseAlgorithmProvider(handle,0); }
};
Pmk derive_one(const Bytes& password,const Bytes& salt,std::uint32_t iterations) {
    // Per-worker ownership avoids sharing mutable provider state across threads.
    thread_local Provider provider;
    Pmk result{}; std::uint8_t empty=0;
    auto* p=password.empty()?&empty:const_cast<std::uint8_t*>(password.data());
    auto* s=salt.empty()?&empty:const_cast<std::uint8_t*>(salt.data());
    if(BCryptDeriveKeyPBKDF2(provider.handle,p,static_cast<ULONG>(password.size()),
        s,static_cast<ULONG>(salt.size()),iterations,result.data(),static_cast<ULONG>(result.size()),0)<0)
        throw std::runtime_error("CPU PBKDF2 derivation failed");
    return result;
}
#else
Pmk derive_one(const Bytes& password,const Bytes& salt,std::uint32_t iterations) {
    return reference(password,salt,iterations);
}
#endif
}
struct Cpu::Impl {
    std::size_t count;
    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable wake,finished;
    bool stopping=false;
    std::uint64_t generation=0;
    std::size_t pending=0;
    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;
    const std::vector<Bytes>* passwords=nullptr;
    const Bytes* salt=nullptr;
    const std::atomic<bool>* cancelled=nullptr;
    std::vector<Pmk>* output=nullptr;
    std::uint32_t iterations=0;
    explicit Impl(std::size_t requested):count(requested?requested:std::clamp(std::thread::hardware_concurrency(),1U,64U)) {
        if(count>64) throw std::invalid_argument("CPU threads must be 1..64 (or 0 for automatic)");
        if(count==1) return;
        try {
            for(std::size_t i=0;i<count;++i) workers.emplace_back([this] {
                std::uint64_t seen=0;
                for(;;) {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait(lock,[&]{return stopping || generation!=seen;});
                    if(stopping) return;
                    seen=generation; lock.unlock();
                    work();
                    lock.lock();
                    if(--pending==0) finished.notify_one();
                }
            });
        } catch(...) { shutdown(); throw; }
    }
    void shutdown() {
        { std::lock_guard<std::mutex> lock(mutex); stopping=true; }
        wake.notify_all();
        for(auto& worker:workers) if(worker.joinable()) worker.join();
    }
    ~Impl() { shutdown(); }
    void work() noexcept {
        try {
            while(!failed.load(std::memory_order_relaxed) && !(cancelled && cancelled->load())) {
                auto index=next.fetch_add(1,std::memory_order_relaxed);
                if(index>=passwords->size()) break;
                (*output)[index]=derive_one((*passwords)[index],*salt,iterations);
            }
        } catch(...) {
            failed=true;
            std::lock_guard<std::mutex> lock(mutex);
            if(!error) error=std::current_exception();
        }
    }
};
Cpu::Cpu(std::size_t threads):impl_(std::make_unique<Impl>(threads)) {}
Cpu::~Cpu()=default;
std::size_t Cpu::threads() const { return impl_->count; }
Batch Cpu::derive(const std::vector<Bytes>& passwords,const Bytes& salt,std::uint32_t iterations,
                  const std::atomic<bool>* cancelled) {
    if(passwords.empty() || passwords.size()>4096 || salt.size()>32 || !iterations || iterations>4096)
        throw std::invalid_argument("Invalid CPU PBKDF2 batch, salt or iterations");
    for(const auto& p:passwords) if(p.size()>63) throw std::invalid_argument("Password exceeds 63 bytes");
    const auto start=std::chrono::steady_clock::now();
    Batch result; result.pmks.resize(passwords.size());
    auto& p=*impl_;
    {
        std::lock_guard<std::mutex> lock(p.mutex);
        p.passwords=&passwords; p.salt=&salt; p.output=&result.pmks; p.cancelled=cancelled;
        p.iterations=iterations; p.next=0; p.failed=false; p.error=nullptr;
        p.pending=p.workers.size(); ++p.generation;
    }
    if(p.workers.empty()) p.work();
    else {
        p.wake.notify_all();
        std::unique_lock<std::mutex> lock(p.mutex);
        p.finished.wait(lock,[&]{return p.pending==0;});
    }
    if(p.error) std::rethrow_exception(p.error);
    if(cancelled && cancelled->load()) result.pmks.clear();
    result.wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    return result;
}
}
