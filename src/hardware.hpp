#pragma once
#include "hc22000.hpp"
#include "platform.hpp"

namespace wifi {
// Serialize only the device used by the initialized recovery engine. Software
// Vulkan is CPU execution even though it uses the Vulkan backend.
inline std::string cpu_hardware_json(std::size_t threads) {
    return "{\"backend\":\"cpu\",\"compute_kind\":\"cpu\",\"software\":false,\"cpu_threads\":"+
        std::to_string(threads)+",\"cpu_provider\":"+json_string(cpu_provider())+",\"device\":null}";
}
inline std::string gpu_hardware_json(Backend backend,std::size_t index,const Device& d) {
    auto count=[](std::uint64_t n) { return n?std::to_string(n):"null"; };
    return "{\"backend\":"+json_string(backend==Backend::Hip?"hip":(backend==Backend::Vulkan?"vulkan":"opencl"))+
        ",\"compute_kind\":"+json_string(d.software?"cpu":"gpu")+
        ",\"software\":"+(d.software?"true":"false")+",\"cpu_threads\":null,\"device_index\":"+
        std::to_string(index)+",\"device\":{\"name\":"+json_string(d.name)+
        ",\"vendor\":"+json_string(d.vendor)+",\"driver\":"+json_string(d.driver)+
        ",\"runtime\":"+json_string(d.version)+",\"memory_bytes\":"+count(d.memory)+
        ",\"compute_units\":"+count(d.compute_units)+"}}";
}
}
