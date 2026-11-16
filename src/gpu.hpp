#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace wifi {
enum class Backend { OpenCl, Vulkan, Hip };
Backend default_backend();
constexpr std::size_t gpu_batch_limit(Backend backend) { return backend==Backend::Vulkan ? 4096 : 262144; }
Backend parse_backend(const std::string&);
const char* backend_name(Backend);
using Pmk = std::array<std::uint8_t,32>;
using Bytes = std::vector<std::uint8_t>;
struct Device {
    std::string name, vendor, driver, version, language;
    std::uint32_t compute_units;
    std::uint64_t memory, max_allocation;
    std::size_t max_work_group;
    bool software=false;
};
struct Batch {
    std::vector<Pmk> pmks;
    double wall_ms=0, gpu_ms=0, max_launch_ms=0, max_submission_ms=0;
    std::size_t submissions=0;
};
struct PmkMatch { std::size_t index; Pmk pmk; };
struct FilteredBatch { std::size_t count; std::vector<PmkMatch> hits; };
// Windows/Linux OpenCL/Vulkan and optional Linux HIP compute backends.
class Gpu {
public:
    static std::vector<Device> devices(Backend backend=default_backend(),bool allow_software=false);
    Gpu(std::size_t device_index, const std::string& source, Backend backend=default_backend(),bool allow_software=false);
    ~Gpu();
    Gpu(const Gpu&)=delete;
    Gpu& operator=(const Gpu&)=delete;
    Device info() const;
    Batch derive(const std::vector<Bytes>& passwords, const Bytes& ssid,
                 std::uint32_t iterations=4096, std::uint32_t chunk=256,
                 std::size_t work_group=64);
    // Optional accelerator for exact-nonce WPA2 EAPOL v2. Nullopt selects CPU
    // verification; returned flags are only a filter, never verified results.
    std::optional<Bytes> filter_wpa2(const std::vector<Pmk>& pmks,const Bytes& context,
                                   const Bytes& eapol,const Bytes& mic,std::size_t work_group=64);
    // Optional 4096-round derivation + exact-nonce WPA2 filter with device-local
    // PMKs. Only possible hits are returned; callers must independently verify.
    std::optional<FilteredBatch> derive_wpa2(const std::vector<Bytes>& passwords,const Bytes& ssid,
                                            const Bytes& context,const Bytes& eapol,const Bytes& mic,
                                            std::uint32_t chunk=256,std::size_t work_group=64);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Pmk reference(const Bytes& password, const Bytes& ssid, std::uint32_t iterations);
std::string hex(const Pmk& pmk);
Bytes bytes(const std::string& value);
}
