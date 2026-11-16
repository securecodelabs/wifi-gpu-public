#define VK_NO_PROTOTYPES
#include "vulkan_properties.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
uint32_t loader_version = VK_API_VERSION_1_1;
VkResult version_result = VK_SUCCESS;
VkResult count_result = VK_SUCCESS;
VkResult list_result = VK_SUCCESS;
const char* extension_name = VK_AMD_SHADER_CORE_PROPERTIES_2_EXTENSION_NAME;
unsigned incomplete_lists = 0, list_calls = 0, query_calls = 0;
bool shrink_list = false;
uint32_t enabled_units = 32;
int device_storage = 0;
const auto selected_device = reinterpret_cast<VkPhysicalDevice>(&device_storage);

VKAPI_ATTR VkResult VKAPI_CALL version(uint32_t* out) {
    *out = loader_version;
    return version_result;
}
VkResult enumerate(uint32_t* count, VkExtensionProperties* out) {
    if (!out) {
        *count = extension_name ? 1u : 0u;
        return count_result;
    }
    ++list_calls;
    if (list_calls <= incomplete_lists) return VK_INCOMPLETE;
    if (list_result != VK_SUCCESS) return list_result;
    if (shrink_list) { *count = 0; return VK_SUCCESS; }
    require(*count == 1 && extension_name, "extension buffer capacity");
    const auto length = std::strlen(extension_name) + 1;
    require(length <= sizeof(out[0].extensionName), "extension name fits");
    std::memcpy(out[0].extensionName, extension_name, length);
    *count = 1;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL instance_extensions(const char* layer, uint32_t* count,
                                                  VkExtensionProperties* out) {
    require(!layer, "global instance extension list");
    return enumerate(count, out);
}
VKAPI_ATTR VkResult VKAPI_CALL device_extensions(VkPhysicalDevice device, const char* layer,
                                                uint32_t* count, VkExtensionProperties* out) {
    require(device == selected_device && !layer, "query the selected device");
    return enumerate(count, out);
}
VKAPI_ATTR void VKAPI_CALL properties(VkPhysicalDevice device, VkPhysicalDeviceProperties2* info) {
    ++query_calls;
    require(device == selected_device, "properties belong to the selected device");
    require(info->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, "properties2 structure");
    auto* cores = static_cast<VkPhysicalDeviceShaderCoreProperties2AMD*>(info->pNext);
    require(cores && !cores->pNext &&
            cores->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CORE_PROPERTIES_2_AMD,
            "AMD shader-core property chain");
    cores->activeComputeUnitCount = enabled_units;
}
uint32_t units() {
    return wifi::vulkan_detail::compute_units(selected_device, device_extensions, properties);
}
}

int main() {
    try {
        using wifi::vulkan_detail::instance_properties;
        auto support = instance_properties(version, nullptr);
        require(support.api_version == VK_API_VERSION_1_1 && !support.khr, "core properties2");
        loader_version = VK_MAKE_VERSION(1, 3, 0);
        require(instance_properties(version, nullptr).api_version == VK_API_VERSION_1_1,
                "do not require an API newer than 1.1");
        extension_name = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
        support = instance_properties(nullptr, instance_extensions);
        require(support.api_version == VK_API_VERSION_1_0 && support.khr, "Vulkan 1.0 KHR path");
        version_result = VK_ERROR_INITIALIZATION_FAILED;
        require(instance_properties(version, instance_extensions).khr, "failed version query fallback");
        extension_name = nullptr;
        support = instance_properties(nullptr, instance_extensions);
        require(support.api_version == VK_API_VERSION_1_0 && !support.khr, "plain Vulkan 1.0");
        require(!instance_properties(nullptr, nullptr).khr, "missing optional loader functions");

        require(units() == 0 && query_calls == 0, "unsupported extension remains unknown");
        extension_name = "VK_AMD_shader_core_properties";
        require(units() == 0 && query_calls == 0, "older extension is not an active-unit count");
        extension_name = VK_AMD_SHADER_CORE_PROPERTIES_2_EXTENSION_NAME;
        require(wifi::vulkan_detail::compute_units(selected_device, nullptr, properties) == 0,
                "missing enumeration function");
        require(wifi::vulkan_detail::compute_units(selected_device, device_extensions, nullptr) == 0,
                "missing properties function");
        require(units() == 32 && query_calls == 1, "reported active compute units");
        enabled_units = 28;
        require(units() == 28, "honor the enabled count rather than assuming a GPU model");
        enabled_units = 0;
        require(units() == 0, "driver's unknown count stays unknown");
        enabled_units = 32;
        count_result = VK_ERROR_INITIALIZATION_FAILED;
        require(units() == 0, "extension count failure is optional");
        count_result = VK_SUCCESS;
        list_result = VK_ERROR_INITIALIZATION_FAILED;
        require(units() == 0, "extension list failure is optional");
        list_result = VK_SUCCESS;
        shrink_list = true;
        require(units() == 0, "extension list may shrink");
        shrink_list = false;
        incomplete_lists = 2; list_calls = 0;
        require(units() == 32 && list_calls == 3, "retry changing extension list");
        incomplete_lists = 100; list_calls = 0;
        const auto previous_queries = query_calls;
        require(units() == 0 && list_calls == 4 && query_calls == previous_queries,
                "bounded retry retains unknown without querying unsupported properties");

        std::cout << "PASS: Vulkan property negotiation, active compute units and unknown fallbacks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
