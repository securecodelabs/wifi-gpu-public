#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_NO_PROTOTYPES
#include "platform.hpp"
#include <vulkan/vulkan_core.h>
#include "gpu_backend.hpp"
#include "vulkan_properties.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace wifi {
namespace {
constexpr unsigned max_loop_dispatches=16;
void check(VkResult r,const char* op) {
    if(r!=VK_SUCCESS) throw std::runtime_error(std::string(op)+" failed (Vulkan "+std::to_string(r)+")");
}
template<class T,class U> T pointer_cast(U address) {
    T result=nullptr;
    static_assert(sizeof(result)==sizeof(address),"Loader function-pointer ABI");
    std::memcpy(&result,&address,sizeof(result)); return result;
}
#define INSTANCE_FUNCTIONS(X) \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkCreateDevice) X(vkGetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X) \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) \
    X(vkFlushMappedMemoryRanges) X(vkInvalidateMappedMemoryRanges) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateComputePipelines) X(vkDestroyPipeline) \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets) \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) X(vkResetCommandBuffer) \
    X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) \
    X(vkCmdPushConstants) X(vkCmdDispatch) X(vkCmdPipelineBarrier) \
    X(vkCreateFence) X(vkDestroyFence) X(vkResetFences) X(vkWaitForFences) X(vkQueueSubmit) \
    X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkCmdResetQueryPool) X(vkCmdWriteTimestamp) X(vkGetQueryPoolResults)

// Only the selected backend loads its driver. CPU and OpenCL need no Vulkan DLL.
struct Runtime {
    bool stats=[] {
#ifdef _WIN32
        char value[2]{};
        return GetEnvironmentVariableA("WIFI_GPU_VULKAN_STATS",value,sizeof(value))==1 && value[0]=='1';
#else
        const auto* value=std::getenv("WIFI_GPU_VULKAN_STATS");
        return value && std::string(value)=="1";
#endif
    }();
    DynamicLibrary library{L"vulkan-1.dll","libvulkan.so.1"};
    VkInstance instance=VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr get=nullptr;
    PFN_vkGetPhysicalDeviceProperties2 properties2=nullptr;
    PFN_vkEnumerateDeviceExtensionProperties device_extensions=nullptr;
    #define DECLARE(n) PFN_##n n=nullptr;
    INSTANCE_FUNCTIONS(DECLARE)
    #undef DECLARE
    Runtime() {
        try {
            get=pointer_cast<PFN_vkGetInstanceProcAddr>(library.symbol("vkGetInstanceProcAddr"));
            if(!get) throw std::runtime_error("Missing vkGetInstanceProcAddr");
            const auto create=pointer_cast<PFN_vkCreateInstance>(get(VK_NULL_HANDLE,"vkCreateInstance"));
            if(!create) throw std::runtime_error("Missing vkCreateInstance");
            VkApplicationInfo app{}; app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
            app.pApplicationName="wifi-gpu"; app.applicationVersion=VK_MAKE_VERSION(0,7,0);
            const auto support=vulkan_detail::instance_properties(
                pointer_cast<PFN_vkEnumerateInstanceVersion>(get(VK_NULL_HANDLE,"vkEnumerateInstanceVersion")),
                pointer_cast<PFN_vkEnumerateInstanceExtensionProperties>(get(VK_NULL_HANDLE,"vkEnumerateInstanceExtensionProperties")));
            app.apiVersion=stats ? VK_API_VERSION_1_1 : support.api_version;
            const char* properties_extension=VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
            VkInstanceCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; info.pApplicationInfo=&app;
            if(support.khr) {
                info.enabledExtensionCount=1; info.ppEnabledExtensionNames=&properties_extension;
            }
            check(create(&info,nullptr,&instance),"vkCreateInstance");
            #define RESOLVE(n) n=pointer_cast<PFN_##n>(get(instance,#n)); if(!n) throw std::runtime_error("Missing " #n);
            INSTANCE_FUNCTIONS(RESOLVE)
            #undef RESOLVE
            if(app.apiVersion>=VK_API_VERSION_1_1 || support.khr) {
                properties2=pointer_cast<PFN_vkGetPhysicalDeviceProperties2>(get(instance,
                    app.apiVersion>=VK_API_VERSION_1_1 ? "vkGetPhysicalDeviceProperties2" : "vkGetPhysicalDeviceProperties2KHR"));
            }
            device_extensions=pointer_cast<PFN_vkEnumerateDeviceExtensionProperties>(get(instance,"vkEnumerateDeviceExtensionProperties"));
        } catch(...) { cleanup(); throw; }
    }
    void cleanup() {
        if(instance && vkDestroyInstance) vkDestroyInstance(instance,nullptr);
        instance=VK_NULL_HANDLE;
    }
    ~Runtime() { cleanup(); }
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
};
std::vector<VkQueueFamilyProperties> queues(Runtime& r,VkPhysicalDevice d) {
    std::uint32_t n=0; r.vkGetPhysicalDeviceQueueFamilyProperties(d,&n,nullptr);
    std::vector<VkQueueFamilyProperties> result(n);
    if(n) r.vkGetPhysicalDeviceQueueFamilyProperties(d,&n,result.data());
    result.resize(n); return result;
}
std::vector<VkPhysicalDevice> enumerate(Runtime& r,bool allow_software) {
    // Retry enumeration if devices change between count/list queries.
    for(unsigned attempt=0;attempt<4;++attempt) {
        std::uint32_t n=0; check(r.vkEnumeratePhysicalDevices(r.instance,&n,nullptr),"vkEnumeratePhysicalDevices(count)");
        if(!n) return {};
        std::vector<VkPhysicalDevice> all(n);
        const auto result=r.vkEnumeratePhysicalDevices(r.instance,&n,all.data());
        if(result==VK_INCOMPLETE) continue;
        check(result,"vkEnumeratePhysicalDevices"); all.resize(n);
        all.erase(std::remove_if(all.begin(),all.end(),[&](auto d) {
            VkPhysicalDeviceProperties props{}; r.vkGetPhysicalDeviceProperties(d,&props);
            auto q=queues(r,d);
            return (!allow_software && props.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU) ||
                std::none_of(q.begin(),q.end(),[](const auto& f){return f.queueCount && (f.queueFlags&VK_QUEUE_COMPUTE_BIT);});
        }),all.end());
        return all;
    }
    throw std::runtime_error("Vulkan device list kept changing; retry devices");
}
std::string version(std::uint32_t v) {
    return std::to_string(VK_VERSION_MAJOR(v))+"."+std::to_string(VK_VERSION_MINOR(v))+"."+std::to_string(VK_VERSION_PATCH(v));
}
Device describe(Runtime& r,VkPhysicalDevice d) {
    VkPhysicalDeviceProperties p{}; r.vkGetPhysicalDeviceProperties(d,&p);
    VkPhysicalDeviceMemoryProperties m{}; r.vkGetPhysicalDeviceMemoryProperties(d,&m);
    std::uint64_t memory=0;
    for(std::uint32_t i=0;i<m.memoryHeapCount;++i) if(m.memoryHeaps[i].flags&VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) memory+=m.memoryHeaps[i].size;
    std::string vendor="vendor "+std::to_string(p.vendorID),driver=std::to_string(p.driverVersion);
    if(p.vendorID==0x10de) {
        vendor="NVIDIA";
        driver=std::to_string(p.driverVersion>>22)+"."+std::to_string((p.driverVersion>>14)&255)+"."+
            std::to_string((p.driverVersion>>6)&255)+"."+std::to_string(p.driverVersion&63);
    } else if(p.vendorID==0x1002) vendor="AMD";
    else if(p.vendorID==0x8086) vendor="Intel";
    const auto compute_units=vulkan_detail::compute_units(d,r.device_extensions,r.properties2);
    return {p.deviceName,vendor,driver,"Vulkan "+version(p.apiVersion),"SPIR-V 1.0",compute_units,memory,
        p.limits.maxStorageBufferRange,std::min(p.limits.maxComputeWorkGroupInvocations,p.limits.maxComputeWorkGroupSize[0]),p.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU};
}
struct Context {
    Runtime runtime;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    std::uint32_t family=0,timestamp_bits=0;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    VkShaderModule shader=VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkDescriptorPool pool=VK_NULL_HANDLE;
    VkDescriptorSet set=VK_NULL_HANDLE;
    VkCommandPool commands=VK_NULL_HANDLE;
    VkCommandBuffer command=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    VkQueryPool queries=VK_NULL_HANDLE;
    PFN_vkGetPipelineExecutablePropertiesKHR executable_properties=nullptr;
    PFN_vkGetPipelineExecutableStatisticsKHR executable_statistics=nullptr;
    std::map<std::size_t,std::array<VkPipeline,4>> pipelines;
    #define DECLARE(n) PFN_##n n=nullptr;
    DEVICE_FUNCTIONS(DECLARE)
    #undef DECLARE
    ~Context() {
        if(!device) return;
        if(vkDeviceWaitIdle) vkDeviceWaitIdle(device);
        for(auto& entry:pipelines) for(auto p:entry.second) if(p) vkDestroyPipeline(device,p,nullptr);
        if(queries) vkDestroyQueryPool(device,queries,nullptr);
        if(fence) vkDestroyFence(device,fence,nullptr);
        if(commands) vkDestroyCommandPool(device,commands,nullptr);
        if(pool) vkDestroyDescriptorPool(device,pool,nullptr);
        if(layout) vkDestroyPipelineLayout(device,layout,nullptr);
        if(descriptors) vkDestroyDescriptorSetLayout(device,descriptors,nullptr);
        if(shader) vkDestroyShaderModule(device,shader,nullptr);
        if(vkDestroyDevice) vkDestroyDevice(device,nullptr);
    }
    void setup(std::size_t index,const std::vector<std::uint32_t>& words,bool allow_software) {
        const auto all=enumerate(runtime,allow_software);
        if(index>=all.size()) throw std::invalid_argument("GPU index out of range; run devices --backend vulkan");
        physical=all[index];
        runtime.vkGetPhysicalDeviceProperties(physical,&properties);
        runtime.vkGetPhysicalDeviceMemoryProperties(physical,&memory);
        const auto q=queues(runtime,physical);
        for(std::size_t i=0;i<q.size();++i) if(q[i].queueCount && (q[i].queueFlags&VK_QUEUE_COMPUTE_BIT)) {
            family=static_cast<std::uint32_t>(i); timestamp_bits=q[i].timestampValidBits; break;
        }
        if(!timestamp_bits || timestamp_bits>64 || !(properties.limits.timestampPeriod>0))
            throw std::runtime_error("Vulkan compute timestamps are required for bounded dispatch timing; use OpenCL on this device");
        const float priority=1;
        VkDeviceQueueCreateInfo qi{}; qi.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex=family; qi.queueCount=1; qi.pQueuePriorities=&priority;
        VkDeviceCreateInfo di{}; di.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        di.queueCreateInfoCount=1; di.pQueueCreateInfos=&qi;
        VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR profile{};
        profile.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR;
        const char* extension=VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME;
        if(runtime.stats) {
            if(properties.apiVersion<VK_API_VERSION_1_1) throw std::runtime_error("Vulkan shader statistics require a Vulkan 1.1 device");
            const auto enumerate_extensions=pointer_cast<PFN_vkEnumerateDeviceExtensionProperties>(runtime.get(runtime.instance,"vkEnumerateDeviceExtensionProperties"));
            const auto features2=pointer_cast<PFN_vkGetPhysicalDeviceFeatures2>(runtime.get(runtime.instance,"vkGetPhysicalDeviceFeatures2"));
            if(!enumerate_extensions || !features2) throw std::runtime_error("Vulkan statistics need Vulkan 1.1 and executable-properties support");
            std::uint32_t n=0; check(enumerate_extensions(physical,nullptr,&n,nullptr),"device extensions(count)");
            std::vector<VkExtensionProperties> extensions(n);
            check(enumerate_extensions(physical,nullptr,&n,extensions.data()),"device extensions");
            if(std::none_of(extensions.begin(),extensions.end(),[&](const auto& e){return std::strcmp(e.extensionName,extension)==0;}))
                throw std::runtime_error("Vulkan shader statistics unavailable on this device");
            VkPhysicalDeviceFeatures2 features{}; features.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; features.pNext=&profile;
            features2(physical,&features);
            if(!profile.pipelineExecutableInfo) throw std::runtime_error("Vulkan executable statistics feature unavailable");
            di.pNext=&profile; di.enabledExtensionCount=1; di.ppEnabledExtensionNames=&extension;
        }
        check(runtime.vkCreateDevice(physical,&di,nullptr,&device),"vkCreateDevice");
        #define RESOLVE(n) n=pointer_cast<PFN_##n>(runtime.vkGetDeviceProcAddr(device,#n)); if(!n) throw std::runtime_error("Missing " #n);
        DEVICE_FUNCTIONS(RESOLVE)
        #undef RESOLVE
        if(runtime.stats) {
            executable_properties=pointer_cast<PFN_vkGetPipelineExecutablePropertiesKHR>(runtime.vkGetDeviceProcAddr(device,"vkGetPipelineExecutablePropertiesKHR"));
            executable_statistics=pointer_cast<PFN_vkGetPipelineExecutableStatisticsKHR>(runtime.vkGetDeviceProcAddr(device,"vkGetPipelineExecutableStatisticsKHR"));
            if(!executable_properties || !executable_statistics) throw std::runtime_error("Missing Vulkan executable statistics functions");
        }
        vkGetDeviceQueue(device,family,0,&queue);
        VkShaderModuleCreateInfo si{}; si.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        si.codeSize=words.size()*sizeof(words[0]); si.pCode=words.data();
        check(vkCreateShaderModule(device,&si,nullptr,&shader),"vkCreateShaderModule");
        std::array<VkDescriptorSetLayoutBinding,4> bindings{};
        for(std::uint32_t i=0;i<bindings.size();++i) bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo bi{}; bi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        bi.bindingCount=4; bi.pBindings=bindings.data();
        check(vkCreateDescriptorSetLayout(device,&bi,nullptr,&descriptors),"vkCreateDescriptorSetLayout");
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,12};
        VkPipelineLayoutCreateInfo li{}; li.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount=1; li.pSetLayouts=&descriptors; li.pushConstantRangeCount=1; li.pPushConstantRanges=&push;
        check(vkCreatePipelineLayout(device,&li,nullptr,&layout),"vkCreatePipelineLayout");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4};
        VkDescriptorPoolCreateInfo pi{}; pi.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.maxSets=1; pi.poolSizeCount=1; pi.pPoolSizes=&size;
        check(vkCreateDescriptorPool(device,&pi,nullptr,&pool),"vkCreateDescriptorPool");
        VkDescriptorSetAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool=pool; ai.descriptorSetCount=1; ai.pSetLayouts=&descriptors;
        check(vkAllocateDescriptorSets(device,&ai,&set),"vkAllocateDescriptorSets");
        VkCommandPoolCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; ci.queueFamilyIndex=family;
        check(vkCreateCommandPool(device,&ci,nullptr,&commands),"vkCreateCommandPool");
        VkCommandBufferAllocateInfo cai{}; cai.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool=commands; cai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount=1;
        check(vkAllocateCommandBuffers(device,&cai,&command),"vkAllocateCommandBuffers");
        VkFenceCreateInfo fi{}; fi.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check(vkCreateFence(device,&fi,nullptr,&fence),"vkCreateFence");
        VkQueryPoolCreateInfo query{}; query.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query.queryType=VK_QUERY_TYPE_TIMESTAMP; query.queryCount=max_loop_dispatches+1;
        check(vkCreateQueryPool(device,&query,nullptr,&queries),"vkCreateQueryPool");
    }
    void print_statistics(VkPipeline p,unsigned phase,std::size_t group) {
        if(!runtime.stats) return;
        VkPipelineInfoKHR info{}; info.sType=VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR; info.pipeline=p;
        std::uint32_t n=0; check(executable_properties(device,&info,&n,nullptr),"executable properties(count)");
        std::vector<VkPipelineExecutablePropertiesKHR> pipeline_properties(n);
        for(auto& value:pipeline_properties) value.sType=VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;
        check(executable_properties(device,&info,&n,pipeline_properties.data()),"executable properties");
        for(std::uint32_t i=0;i<n;++i) {
            std::cerr<<"VULKAN_STATS phase="<<phase<<" group="<<group<<" subgroup_size="<<pipeline_properties[i].subgroupSize<<"\n";
            VkPipelineExecutableInfoKHR ei{}; ei.sType=VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR;
            ei.pipeline=p; ei.executableIndex=i;
            std::uint32_t count=0; check(executable_statistics(device,&ei,&count,nullptr),"executable statistics(count)");
            std::vector<VkPipelineExecutableStatisticKHR> stats(count);
            for(auto& value:stats) value.sType=VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
            check(executable_statistics(device,&ei,&count,stats.data()),"executable statistics");
            for(std::uint32_t j=0;j<count;++j) {
                const auto& v=stats[j];
                std::cerr<<"VULKAN_STATS phase="<<phase<<" group="<<group<<" name=\""<<v.name<<"\" value=";
                switch(v.format) {
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR: std::cerr<<v.value.b32; break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR: std::cerr<<v.value.i64; break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR: std::cerr<<v.value.u64; break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR: std::cerr<<v.value.f64; break;
                    default: std::cerr<<"unknown";
                }
                std::cerr<<" description=\""<<v.description<<"\"\n";
            }
        }
    }
    const std::array<VkPipeline,4>& pipeline(std::size_t group) {
        auto found=pipelines.find(group); if(found!=pipelines.end()) return found->second;
        auto& result=pipelines[group];
        try {
            for(std::uint32_t phase=0;phase<4;++phase) {
                const std::uint32_t data[]={static_cast<std::uint32_t>(group),phase};
                const VkSpecializationMapEntry entries[]={{0,0,4},{1,4,4}};
                const VkSpecializationInfo spec{2,entries,sizeof(data),data};
                VkComputePipelineCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
                if(runtime.stats) ci.flags|=VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
                ci.layout=layout; ci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module=shader;
                ci.stage.pName="wifi_pbkdf2_v4"; ci.stage.pSpecializationInfo=&spec;
                check(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&ci,nullptr,&result[phase]),"vkCreateComputePipelines");
                print_statistics(result[phase],phase,group);
            }
        } catch(...) {
            for(auto p:result) if(p) vkDestroyPipeline(device,p,nullptr);
            pipelines.erase(group); throw;
        }
        return result;
    }
    std::vector<double> launch(VkPipeline pipeline,std::uint32_t count,std::uint32_t salt,std::uint32_t rounds,std::size_t group,bool last,unsigned repeats) {
        if(repeats<1 || repeats>max_loop_dispatches || (last && repeats!=1)) throw std::logic_error("Invalid Vulkan submission length");
        check(vkResetCommandBuffer(command,0),"vkResetCommandBuffer");
        VkCommandBufferBeginInfo begin{}; begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command,&begin),"vkBeginCommandBuffer");
        vkCmdResetQueryPool(command,queries,0,repeats+1);
        VkMemoryBarrier barrier{}; barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queries,0);
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);
        const std::uint32_t args[]={count,salt,rounds};
        vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(args),args);
        for(unsigned i=0;i<repeats;++i) {
            if(i) {
                barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
                barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0,1,&barrier,0,nullptr,0,nullptr);
            }
            vkCmdDispatch(command,static_cast<std::uint32_t>((count*2+group-1)/group),1,1);
            if(last) {
                barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
                    0,1,&barrier,0,nullptr,0,nullptr);
            }
            vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,queries,i+1);
        }
        check(vkEndCommandBuffer(command),"vkEndCommandBuffer");
        check(vkResetFences(device,1,&fence),"vkResetFences");
        VkSubmitInfo submit{}; submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount=1; submit.pCommandBuffers=&command;
        check(vkQueueSubmit(queue,1,&submit,fence),"vkQueueSubmit");
        // Never free buffers that a submitted command may still access.
        const auto waited=vkWaitForFences(device,1,&fence,VK_TRUE,std::numeric_limits<std::uint64_t>::max());
        check(waited,"vkWaitForFences");
        std::array<std::uint64_t,max_loop_dispatches+1> times{};
        check(vkGetQueryPoolResults(device,queries,0,repeats+1,(repeats+1)*sizeof(times[0]),times.data(),sizeof(times[0]),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT),"vkGetQueryPoolResults");
        const auto mask=timestamp_bits==64 ? ~std::uint64_t{0} : (std::uint64_t{1}<<timestamp_bits)-1;
        std::vector<double> measured;
        for(unsigned i=0;i<repeats;++i) measured.push_back(static_cast<double>((times[i+1]-times[i])&mask)*properties.limits.timestampPeriod/1e6);
        return measured;
    }
};
struct Buffer {
    Context& c; VkBuffer buffer=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE;
    void* mapped=nullptr; bool coherent=false; VkDeviceSize size;
    Buffer(Context& context,VkDeviceSize length,bool host):c(context),size(length) {
        try {
            if(!length || length>c.properties.limits.maxStorageBufferRange) throw std::runtime_error("Vulkan storage buffer exceeds device limit");
            VkBufferCreateInfo bi{}; bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size=length; bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            check(c.vkCreateBuffer(c.device,&bi,nullptr,&buffer),"vkCreateBuffer");
            VkMemoryRequirements req{}; c.vkGetBufferMemoryRequirements(c.device,buffer,&req);
            std::uint32_t type=UINT32_MAX; int score=-1;
            for(std::uint32_t i=0;i<c.memory.memoryTypeCount;++i) {
                const auto flags=c.memory.memoryTypes[i].propertyFlags;
                if(!(req.memoryTypeBits&(1u<<i)) || (host && !(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))) continue;
                const int value=host ? ((flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)?2:0)+((flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)?1:0) : ((flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)?1:0);
                if(value>score) { score=value; type=i; }
            }
            if(type==UINT32_MAX) throw std::runtime_error("No compatible Vulkan buffer memory type");
            coherent=(c.memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
            VkMemoryAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize=req.size; ai.memoryTypeIndex=type;
            check(c.vkAllocateMemory(c.device,&ai,nullptr,&memory),"vkAllocateMemory");
            check(c.vkBindBufferMemory(c.device,buffer,memory,0),"vkBindBufferMemory");
            if(host) check(c.vkMapMemory(c.device,memory,0,VK_WHOLE_SIZE,0,&mapped),"vkMapMemory");
        } catch(...) { cleanup(); throw; }
    }
    void cleanup() {
        if(mapped) c.vkUnmapMemory(c.device,memory);
        if(buffer) c.vkDestroyBuffer(c.device,buffer,nullptr);
        if(memory) c.vkFreeMemory(c.device,memory,nullptr);
        mapped=nullptr; buffer=VK_NULL_HANDLE; memory=VK_NULL_HANDLE;
    }
    ~Buffer() { cleanup(); }
    Buffer(const Buffer&)=delete;
    Buffer& operator=(const Buffer&)=delete;
    void visibility(bool upload) {
        if(coherent) return;
        VkMappedMemoryRange range{}; range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory=memory; range.offset=0; range.size=VK_WHOLE_SIZE;
        if(upload) check(c.vkFlushMappedMemoryRanges(c.device,1,&range),"vkFlushMappedMemoryRanges");
        else check(c.vkInvalidateMappedMemoryRanges(c.device,1,&range),"vkInvalidateMappedMemoryRanges");
    }
};
struct VulkanEngine final : GpuEngine {
    // A separate RAII member guarantees cleanup if setup throws partway through.
    std::unique_ptr<Context> c=std::make_unique<Context>();
    std::unique_ptr<Buffer> input_buffer,salt_buffer,state_buffer,output_buffer;
    std::size_t capacity=0;
    void reserve(std::size_t count) {
        if(count<=capacity) return;
        std::size_t next=1;
        while(next<count) next*=2;
        // Allocate a replacement set before releasing old buffers. Mappings stay
        // valid across batches; the preceding submission always completed.
        auto input=std::make_unique<Buffer>(*c,next*64,true);
        // Reuse the salt binding for the WPA2 tag, two PTK blocks, and up to
        // 65 padded EAPOL blocks. Keep the Vulkan 1.0 minimum of four SSBOs.
        auto salt=std::make_unique<Buffer>(*c,(4+32+65*16)*4,true);
        auto states=std::make_unique<Buffer>(*c,next*160,false);
        auto output=std::make_unique<Buffer>(*c,next*36,true);
        input_buffer=std::move(input); salt_buffer=std::move(salt);
        state_buffer=std::move(states); output_buffer=std::move(output);
        capacity=next;
        const Buffer* buffers[]={input_buffer.get(),salt_buffer.get(),state_buffer.get(),output_buffer.get()};
        std::array<VkDescriptorBufferInfo,4> infos{};
        std::array<VkWriteDescriptorSet,4> writes{};
        for(std::size_t i=0;i<4;++i) {
            infos[i]={buffers[i]->buffer,0,buffers[i]->size};
            writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet=c->set;
            writes[i].dstBinding=static_cast<std::uint32_t>(i); writes[i].descriptorCount=1;
            writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo=&infos[i];
        }
        c->vkUpdateDescriptorSets(c->device,4,writes.data(),0,nullptr);
    }
    VulkanEngine(std::size_t index,const std::vector<std::uint32_t>& words,bool allow_software) { c->setup(index,words,allow_software); }
    Device info() const override { return describe(c->runtime,c->physical); }
    Batch derive(const std::vector<Bytes>& passwords,const Bytes& ssid,std::uint32_t iterations,std::uint32_t chunk,std::size_t group) override {
        return derive_impl(passwords,ssid,iterations,chunk,group,true);
    }
    Pmk read_pmk(std::size_t index) const {
        const auto* words=static_cast<const std::uint32_t*>(output_buffer->mapped)+index*8;
        Pmk pmk{};
        for(std::size_t j=0;j<pmk.size();++j) pmk[j]=static_cast<std::uint8_t>(words[j/4]>>(24-8*(j%4)));
        return pmk;
    }
    void validate_filter(std::size_t count,const Bytes& context,const Bytes& eapol,const Bytes& mic,std::size_t group) const {
        if(!count || count>gpu_batch_limit(Backend::Vulkan) || context.size()!=76 ||
           eapol.size()<99 || eapol.size()>4096 || mic.size()!=16 || !group || group>info().max_work_group)
            throw std::invalid_argument("Invalid WPA2 GPU filter input");
        if((count*2+group-1)/group>c->properties.limits.maxComputeWorkGroupCount[0])
            throw std::invalid_argument("Dispatch exceeds Vulkan device limit");
    }
    Bytes filter_buffer(std::size_t count,const Bytes& context,const Bytes& eapol,const Bytes& mic,std::size_t group) {
        auto padded=[](Bytes message) {
            const auto bits=static_cast<std::uint64_t>(64+message.size())*8;
            message.push_back(0x80);
            while(message.size()%64!=56) message.push_back(0);
            for(int shift=56;shift>=0;shift-=8) message.push_back(static_cast<std::uint8_t>(bits>>shift));
            return message;
        };
        auto ptk=bytes("Pairwise key expansion"); ptk.push_back(0);
        ptk.insert(ptk.end(),context.begin(),context.end()); ptk.push_back(0);
        ptk=padded(std::move(ptk));
        const auto message=padded(eapol);
        Bytes data=mic; data.insert(data.end(),ptk.begin(),ptk.end());
        data.insert(data.end(),message.begin(),message.end());
        std::vector<std::uint32_t> words(data.size()/4);
        for(std::size_t i=0;i<data.size();++i) words[i/4]|=std::uint32_t(data[i])<<(24-8*(i%4));
        std::memcpy(salt_buffer->mapped,words.data(),words.size()*sizeof(words[0])); salt_buffer->visibility(true);
        const auto measured=c->launch(c->pipeline(group)[3],static_cast<std::uint32_t>(count),
            static_cast<std::uint32_t>(message.size()/64),0,group,true,1);
        const double budget=c->properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU ? 30000.0 : 250.0;
        if(measured[0]>budget) throw std::runtime_error("Verification kernel exceeded Vulkan submission budget");
        output_buffer->visibility(false);
        const auto* out=static_cast<const std::uint32_t*>(output_buffer->mapped);
        Bytes flags(count);
        for(std::size_t i=0;i<count;++i) flags[i]=out[count*8+i]!=0;
        return flags;
    }
    std::optional<Bytes> filter_wpa2(const std::vector<Pmk>& pmks,const Bytes& context,
        const Bytes& eapol,const Bytes& mic,std::size_t group) override {
        validate_filter(pmks.size(),context,eapol,mic,group);
        reserve(pmks.size());
        auto* out=static_cast<std::uint32_t*>(output_buffer->mapped);
        for(std::size_t i=0;i<pmks.size();++i) for(std::size_t word=0;word<8;++word) {
            std::uint32_t value=0;
            for(std::size_t byte=0;byte<4;++byte) value|=std::uint32_t(pmks[i][word*4+byte])<<(24-8*byte);
            out[i*8+word]=value;
        }
        output_buffer->visibility(true);
        return filter_buffer(pmks.size(),context,eapol,mic,group);
    }
    std::optional<FilteredBatch> derive_wpa2(const std::vector<Bytes>& passwords,const Bytes& ssid,
        const Bytes& context,const Bytes& eapol,const Bytes& mic,std::uint32_t chunk,std::size_t group) override {
        validate_filter(passwords.size(),context,eapol,mic,group);
        derive_impl(passwords,ssid,4096,chunk,group,false);
        const auto flags=filter_buffer(passwords.size(),context,eapol,mic,group);
        FilteredBatch result{passwords.size(),{}};
        for(std::size_t i=0;i<flags.size();++i) if(flags[i]) result.hits.push_back({i,read_pmk(i)});
        return result;
    }
    Batch derive_impl(const std::vector<Bytes>& passwords,const Bytes& ssid,std::uint32_t iterations,std::uint32_t chunk,std::size_t group,bool readback) {
        if(passwords.empty() || passwords.size()>gpu_batch_limit(Backend::Vulkan)) throw std::invalid_argument("Batch must contain 1..262144 passwords");
        if(ssid.size()>32) throw std::invalid_argument("SSID exceeds 32 bytes");
        if(iterations==0 || iterations>4096) throw std::invalid_argument("Iterations must be 1..4096");
        if(chunk==0 || chunk>gpu_chunk_limit(Backend::Vulkan)) throw std::invalid_argument("Chunk must be 1..1024 rounds");
        if(!group || group>info().max_work_group) throw std::invalid_argument("Work group exceeds kernel/device limit or is zero");
        if((passwords.size()*2+group-1)/group>c->properties.limits.maxComputeWorkGroupCount[0]) throw std::invalid_argument("Dispatch exceeds Vulkan device limit");
        // Transfer only the longest key's occupied words. The init shader
        // supplies the rest of the 64-byte HMAC key block as zeroes.
        std::size_t word_count=1;
        for(const auto& password:passwords) {
            if(password.size()>63) throw std::invalid_argument("Password exceeds 63 bytes");
            word_count=std::max(word_count,(password.size()+3)/4);
        }
        std::vector<std::uint32_t> packed(passwords.size()*word_count,0);
        for(std::size_t i=0;i<passwords.size();++i) {
            const auto& password=passwords[i];
            auto* destination=packed.data()+i*word_count;
            std::size_t j=0;
            for(;j+4<=password.size();j+=4)
                destination[j/4]=(std::uint32_t(password[j])<<24)|(std::uint32_t(password[j+1])<<16)|
                                  (std::uint32_t(password[j+2])<<8)|password[j+3];
            std::uint32_t tail=0;
            for(std::size_t k=0;j+k<password.size();++k) tail|=std::uint32_t(password[j+k])<<(24-8*k);
            if(j<password.size()) destination[j/4]=tail;
        }
        const auto start=std::chrono::steady_clock::now();
        const auto& pipelines=c->pipeline(group);
        reserve(passwords.size());
        auto& input=*input_buffer; auto& salt=*salt_buffer; auto& output=*output_buffer;
        std::array<std::uint32_t,8> salt_words{};
        for(std::size_t i=0;i<ssid.size();++i) salt_words[i/4]|=std::uint32_t(ssid[i])<<(24-8*(i%4));
        std::memcpy(input.mapped,packed.data(),packed.size()*sizeof(std::uint32_t)); input.visibility(true);
        std::memcpy(salt.mapped,salt_words.data(),32); salt.visibility(true);
        Batch result;
        double slowest_round_ms=0;
        // Longer chunks are explicit opt-in. Keep the existing 50 ms target
        // for ordinary settings, and retain the independent 250 ms hard bound.
        const double target_ms=chunk>256 ? 100.0 : 50.0;
        const auto probe=chunk>256 ? 32u : 8u;
        auto run=[&](unsigned phase,std::uint32_t rounds,unsigned repeats=1) {
            const auto measured=c->launch(pipelines[phase],static_cast<std::uint32_t>(passwords.size()),static_cast<std::uint32_t>(ssid.size()),rounds,group,phase==2,repeats);
            double submission_ms=0;
            for(auto ms:measured) {
                submission_ms+=ms;
                result.max_launch_ms=std::max(result.max_launch_ms,ms);
                if(phase==1) slowest_round_ms=std::max(slowest_round_ms,ms/rounds);
            }
            result.gpu_ms+=submission_ms;
            result.max_submission_ms=std::max(result.max_submission_ms,submission_ms);
            ++result.submissions;
            // CPU Vulkan is an explicit test mode and has no display watchdog.
            const double budget_ms=c->properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU ? 30000.0 : 250.0;
            if(submission_ms>budget_ms) throw std::runtime_error("Vulkan submission exceeded "+std::to_string(budget_ms)+" ms budget; reduce --batch or --chunk");
        };
        run(0,static_cast<std::uint32_t>(word_count));
        for(std::uint32_t done=1;done<iterations;) {
            // Probe large batches before scaling toward the submission target.
            auto bounded_chunk=chunk;
            if(passwords.size()>4096 && slowest_round_ms==0) bounded_chunk=std::min(chunk,probe);
            if(passwords.size()>4096 && slowest_round_ms>0) bounded_chunk=static_cast<std::uint32_t>(std::clamp(target_ms/slowest_round_ms,1.0,static_cast<double>(chunk)));
            const auto rounds=std::min(bounded_chunk,iterations-done);
            // Measure the first loop alone. Then amortize CPU/driver waits while
            // targeting bounded measured work and at most 16 dispatches.
            unsigned repeats=1;
            if(slowest_round_ms>0) repeats=static_cast<unsigned>(std::clamp(target_ms/(slowest_round_ms*rounds),1.0,static_cast<double>(max_loop_dispatches)));
            repeats=std::min(repeats,(iterations-done)/rounds);
            run(1,rounds,repeats); done+=rounds*repeats;
        }
        run(2,0);
        if(readback) {
            output.visibility(false);
            result.pmks.resize(passwords.size());
            for(std::size_t i=0;i<passwords.size();++i) result.pmks[i]=read_pmk(i);
        }
        result.wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        return result;
    }
};
}
// Validate instruction framing and the entry-point ABI before loading a driver.
// This is not a replacement for SPIRV-Tools validation of developer shaders.
std::vector<std::uint32_t> validate_vulkan_shader(const std::string& code) {
    if(code.size()<20 || code.size()%4 || code.size()>1024*1024)
        throw std::invalid_argument("Vulkan kernel must be a SPIR-V binary (use kernels/pbkdf2.spv)");
    std::vector<std::uint32_t> words(code.size()/4);
    std::memcpy(words.data(),code.data(),code.size());
    if(words[0]!=0x07230203 || words[1]!=0x00010000 || !words[3] || words[4]!=0)
        throw std::invalid_argument("Vulkan kernel must use little-endian SPIR-V 1.0");
    bool compatible=false;
    for(std::size_t pos=5;pos<words.size();) {
        const auto count=words[pos]>>16, opcode=words[pos]&65535;
        if(!count || count>words.size()-pos) throw std::invalid_argument("Malformed SPIR-V instruction length");
        if(opcode==15) { // OpEntryPoint: model, function id, NUL-terminated name, interfaces.
            if(count<4) throw std::invalid_argument("Malformed SPIR-V entry point");
            const auto* name=reinterpret_cast<const char*>(words.data()+pos+3);
            const auto* end=static_cast<const char*>(std::memchr(name,0,(count-3)*4));
            if(!end) throw std::invalid_argument("Unterminated SPIR-V entry point");
            if(words[pos+1]==5 && words[pos+2] && words[pos+2]<words[3] &&
               std::string(name,static_cast<std::size_t>(end-name))=="wifi_pbkdf2_v4") compatible=true;
        }
        pos+=count;
    }
    if(!compatible) throw std::invalid_argument("Vulkan kernel ABI mismatch: expected wifi_pbkdf2_v4; update kernels/pbkdf2.spv with the executable");
    return words;
}
std::vector<Device> vulkan_devices(bool allow_software) {
    Runtime r; std::vector<Device> result;
    for(auto d:enumerate(r,allow_software)) result.push_back(describe(r,d));
    return result;
}
std::unique_ptr<GpuEngine> vulkan_engine(std::size_t index,const std::string& code,bool allow_software) {
    auto words=validate_vulkan_shader(code);
    return std::make_unique<VulkanEngine>(index,words,allow_software);
}
}
