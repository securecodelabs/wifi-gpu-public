#pragma once
#include <vulkan/vulkan_core.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace wifi::vulkan_detail {
// Optional diagnostics must not prevent recovery when a driver cannot answer.
template<class Enumerate>
bool has_extension(Enumerate enumerate, const char* name) {
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        uint32_t count = 0;
        if (enumerate(&count, nullptr) != VK_SUCCESS || !count) return false;
        std::vector<VkExtensionProperties> extensions(count);
        const auto result = enumerate(&count, extensions.data());
        if (result == VK_INCOMPLETE) continue;
        if (result != VK_SUCCESS || count > extensions.size()) return false;
        extensions.resize(count);
        return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
    }
    return false;
}

struct InstanceProperties {
    uint32_t api_version = VK_API_VERSION_1_0;
    bool khr = false;
};

inline InstanceProperties instance_properties(PFN_vkEnumerateInstanceVersion version,
                                              PFN_vkEnumerateInstanceExtensionProperties extensions) {
    uint32_t supported = VK_API_VERSION_1_0;
    if (version && version(&supported) == VK_SUCCESS && supported >= VK_API_VERSION_1_1)
        return {VK_API_VERSION_1_1, false};
    const bool khr = extensions && has_extension([extensions](uint32_t* count, VkExtensionProperties* out) {
        return extensions(nullptr, count, out);
    }, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    return {VK_API_VERSION_1_0, khr};
}

inline uint32_t compute_units(VkPhysicalDevice device,
                              PFN_vkEnumerateDeviceExtensionProperties extensions,
                              PFN_vkGetPhysicalDeviceProperties2 properties) {
    if (!extensions || !properties) return 0;
    if (!has_extension([=](uint32_t* count, VkExtensionProperties* out) {
        return extensions(device, nullptr, count, out);
    }, VK_AMD_SHADER_CORE_PROPERTIES_2_EXTENSION_NAME)) return 0;
    VkPhysicalDeviceShaderCoreProperties2AMD cores{};
    cores.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_2_AMD;
    VkPhysicalDeviceProperties2 info{};
    info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    info.pNext = &cores;
    properties(device, &info);
    // This is the driver's enabled-unit count, not a chip-name/specification estimate.
    return cores.activeComputeUnitCount;
}
}
