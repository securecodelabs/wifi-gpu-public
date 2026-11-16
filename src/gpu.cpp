#include "gpu_backend.hpp"
#include <stdexcept>

namespace wifi {
Backend default_backend() {
#ifdef WIFI_GPU_ENABLE_HIP
    return Backend::Hip;
#else
    return Backend::Vulkan;
#endif
}
Backend parse_backend(const std::string& value) {
    if(value=="opencl") return Backend::OpenCl;
    if(value=="vulkan") return Backend::Vulkan;
    if(value=="hip") return Backend::Hip;
    throw std::invalid_argument("--backend must be opencl, vulkan or hip");
}
const char* backend_name(Backend b) { return b==Backend::Hip ? "HIP" : (b==Backend::Vulkan ? "Vulkan" : "OpenCL"); }
struct Gpu::Impl { std::unique_ptr<GpuEngine> engine; };
std::vector<Device> Gpu::devices(Backend b,bool allow_software) {
    if(b==Backend::Hip) return hip_devices();
    return b==Backend::Vulkan ? vulkan_devices(allow_software) : opencl_devices();
}
Gpu::Gpu(std::size_t index,const std::string& source,Backend backend,bool allow_software):impl_(std::make_unique<Impl>()) {
    if(backend==Backend::Hip) { impl_->engine=hip_engine(index,source); return; }
    impl_->engine=backend==Backend::Vulkan ? vulkan_engine(index,source,allow_software) : opencl_engine(index,source);
}
Gpu::~Gpu()=default;
Device Gpu::info() const { return impl_->engine->info(); }
Batch Gpu::derive(const std::vector<Bytes>& p,const Bytes& s,std::uint32_t i,std::uint32_t c,std::size_t g) {
    return impl_->engine->derive(p,s,i,c,g);
}
std::optional<Bytes> Gpu::filter_wpa2(const std::vector<Pmk>& p,const Bytes& c,const Bytes& e,
                                    const Bytes& m,std::size_t g) {
    return impl_->engine->filter_wpa2(p,c,e,m,g);
}
std::optional<FilteredBatch> Gpu::derive_wpa2(const std::vector<Bytes>& p,const Bytes& s,const Bytes& c,
                                           const Bytes& e,const Bytes& m,std::uint32_t rounds,std::size_t g) {
    return impl_->engine->derive_wpa2(p,s,c,e,m,rounds,g);
}
}
