#pragma once
#include "gpu.hpp"
#include <atomic>

namespace wifi {
// CPU-only PBKDF2. Workers never load OpenCL/Vulkan or read shader files.
// One coordinator calls derive; candidates keep their original output order.
class Cpu {
public:
    explicit Cpu(std::size_t threads=0); // zero selects available logical CPUs, capped at 64
    ~Cpu();
    Cpu(const Cpu&)=delete;
    Cpu& operator=(const Cpu&)=delete;
    std::size_t threads() const;
    // Cancellation returns an empty batch only after all workers have stopped.
    Batch derive(const std::vector<Bytes>&,const Bytes&,std::uint32_t iterations=4096,
                 const std::atomic<bool>* cancelled=nullptr);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
