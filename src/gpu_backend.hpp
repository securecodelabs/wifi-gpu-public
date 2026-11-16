#pragma once
#include "gpu.hpp"

namespace wifi {
// Internal backend contract: identical byte-oriented PMKs; backend-specific batch limits.
class GpuEngine {
public:
    virtual ~GpuEngine()=default;
    virtual Device info() const=0;
    virtual Batch derive(const std::vector<Bytes>&,const Bytes&,std::uint32_t,std::uint32_t,std::size_t)=0;
    virtual std::optional<Bytes> filter_wpa2(const std::vector<Pmk>&,const Bytes&,
                                          const Bytes&,const Bytes&,std::size_t) { return std::nullopt; }
    virtual std::optional<FilteredBatch> derive_wpa2(const std::vector<Bytes>&,const Bytes&,const Bytes&,
                                                    const Bytes&,const Bytes&,std::uint32_t,std::size_t) { return std::nullopt; }
};
std::vector<std::uint32_t> validate_vulkan_shader(const std::string&);
std::vector<Device> opencl_devices();
std::vector<Device> hip_devices();
std::vector<Device> vulkan_devices(bool allow_software);
std::unique_ptr<GpuEngine> opencl_engine(std::size_t,const std::string&);
std::unique_ptr<GpuEngine> hip_engine(std::size_t,const std::string&);
std::unique_ptr<GpuEngine> vulkan_engine(std::size_t,const std::string&,bool allow_software);
}
