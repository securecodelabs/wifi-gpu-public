#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define CL_TARGET_OPENCL_VERSION 120
#include "platform.hpp"
#include <CL/cl.h>
#include "gpu_backend.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace wifi {
namespace {
void check(cl_int code,const char* operation) {
    if(code!=CL_SUCCESS) throw std::runtime_error(std::string(operation)+" failed (OpenCL "+std::to_string(code)+")");
}
#define CL_FUNCTIONS(X) \
    X(clGetPlatformIDs) X(clGetDeviceIDs) X(clGetDeviceInfo) \
    X(clCreateContext) X(clReleaseContext) X(clCreateCommandQueue) X(clReleaseCommandQueue) \
    X(clCreateProgramWithSource) X(clBuildProgram) X(clGetProgramBuildInfo) X(clReleaseProgram) \
    X(clCreateKernel) X(clGetKernelWorkGroupInfo) X(clReleaseKernel) X(clSetKernelArg) \
    X(clCreateBuffer) X(clReleaseMemObject) X(clEnqueueNDRangeKernel) X(clEnqueueReadBuffer) X(clEnqueueWriteBuffer) \
    X(clWaitForEvents) X(clGetEventProfilingInfo) X(clReleaseEvent)
struct Api {
    DynamicLibrary library{L"OpenCL.dll","libOpenCL.so.1"};
    template<class T> T resolve(const char* name) {
        // Windows gives GetProcAddress a generic function-pointer type. Preserve
        // its representation while using the exact Khronos ABI at the call site.
        const auto address=library.symbol(name);
        if(!address) throw std::runtime_error(std::string("OpenCL function missing: ")+name);
        T result=nullptr;
        static_assert(sizeof(result)==sizeof(address),"Loader function-pointer ABI");
        std::memcpy(&result,&address,sizeof(result));
        return result;
    }
    #define DECLARE(name) decltype(&::name) name=nullptr;
    CL_FUNCTIONS(DECLARE)
    #undef DECLARE
    Api() {
        #define RESOLVE(name) name=resolve<decltype(name)>(#name);
        CL_FUNCTIONS(RESOLVE)
        #undef RESOLVE
    }
    Api(const Api&)=delete;
    Api& operator=(const Api&)=delete;
};
#undef CL_FUNCTIONS

template<class T> struct Resource {
    T value=nullptr;
    cl_int (CL_API_CALL *release)(T)=nullptr;
    Resource()=default;
    Resource(T v,cl_int(CL_API_CALL *r)(T)):value(v),release(r) {}
    ~Resource() { if(value && release) release(value); }
    Resource(const Resource&)=delete;
    Resource& operator=(const Resource&)=delete;
};

template<class T> T number(Api& api,cl_device_id id,cl_device_info key) {
    T v{}; check(api.clGetDeviceInfo(id,key,sizeof(v),&v,nullptr),"clGetDeviceInfo"); return v;
}
std::string string_info(Api& api,cl_device_id id,cl_device_info key) {
    std::size_t n=0;
    check(api.clGetDeviceInfo(id,key,0,nullptr,&n),"clGetDeviceInfo(size)");
    if(n==0 || n>65536) throw std::runtime_error("Invalid OpenCL info length");
    std::vector<char> buf(n);
    check(api.clGetDeviceInfo(id,key,n,buf.data(),nullptr),"clGetDeviceInfo(text)");
    if(buf.back()!='\0') throw std::runtime_error("Unterminated OpenCL info");
    return std::string(buf.data());
}
Device describe(Api& api,cl_device_id id) {
    return {string_info(api,id,CL_DEVICE_NAME),string_info(api,id,CL_DEVICE_VENDOR),
        string_info(api,id,CL_DRIVER_VERSION),string_info(api,id,CL_DEVICE_VERSION),
        string_info(api,id,CL_DEVICE_OPENCL_C_VERSION),number<cl_uint>(api,id,CL_DEVICE_MAX_COMPUTE_UNITS),
        number<cl_ulong>(api,id,CL_DEVICE_GLOBAL_MEM_SIZE),number<cl_ulong>(api,id,CL_DEVICE_MAX_MEM_ALLOC_SIZE),
        number<std::size_t>(api,id,CL_DEVICE_MAX_WORK_GROUP_SIZE)};
}
std::vector<cl_device_id> enumerate(Api& api) {
    cl_uint n=0;
    auto status=api.clGetPlatformIDs(0,nullptr,&n);
    if(status==-1001) return {}; // CL_PLATFORM_NOT_FOUND_KHR
    check(status,"clGetPlatformIDs");
    if(n==0) return {};
    std::vector<cl_platform_id> platforms(n);
    check(api.clGetPlatformIDs(n,platforms.data(),nullptr),"clGetPlatformIDs(list)");
    std::vector<cl_device_id> result;
    for(auto platform:platforms) {
        n=0; status=api.clGetDeviceIDs(platform,CL_DEVICE_TYPE_GPU,0,nullptr,&n);
        if(status==CL_DEVICE_NOT_FOUND) continue;
        check(status,"clGetDeviceIDs");
        if(n==0) continue;
        std::vector<cl_device_id> ids(n);
        check(api.clGetDeviceIDs(platform,CL_DEVICE_TYPE_GPU,n,ids.data(),nullptr),"clGetDeviceIDs(list)");
        result.insert(result.end(),ids.begin(),ids.end());
    }
    return result;
}
struct Password { std::uint8_t bytes[64]{}; cl_uint length=0; };
static_assert(sizeof(Password)==68,"Host/OpenCL password layout must match");
}

struct OpenClEngine final : GpuEngine {
    Api api;
    cl_device_id device=nullptr;
    Resource<cl_context> context;
    Resource<cl_command_queue> queue;
    Resource<cl_program> program;
    Resource<cl_kernel> init,loop,finish,filter;
    struct Buffer { Resource<cl_mem> memory; std::size_t capacity=0; };
    Buffer input_buffer,salt_buffer,state_buffer,output_buffer;
    Buffer filter_keys,filter_ptk,filter_message,filter_tag,filter_flags;
    std::vector<Password> host_input;
    bool amd_optimized=false,compact_input=false;
    std::size_t loop_items=2;
    std::vector<cl_uchar> packed_input;
    bool stats=[] {
#ifdef _WIN32
        char value[2]{};
        return GetEnvironmentVariableA("WIFI_GPU_OPENCL_STATS",value,sizeof(value))==1 && value[0]=='1';
#else
        const auto* value=std::getenv("WIFI_GPU_OPENCL_STATS");
        return value && std::string(value)=="1";
#endif
    }();
    std::size_t kernel_group_limit=0;
    OpenClEngine(std::size_t index,const std::string& source) {
        const auto all=enumerate(api);
        if(index>=all.size()) throw std::invalid_argument("GPU index out of range; run devices");
        if(source.empty()) throw std::invalid_argument("Empty kernel source");
        device=all[index];
        cl_int status=0;
        context.release=api.clReleaseContext;
        context.value=api.clCreateContext(nullptr,1,&device,nullptr,nullptr,&status);
        check(status,"clCreateContext");
        queue.release=api.clReleaseCommandQueue;
        queue.value=api.clCreateCommandQueue(context.value,device,CL_QUEUE_PROFILING_ENABLE,&status);
        check(status,"clCreateCommandQueue");
        const char* code=source.c_str(); const auto len=source.size();
        program.release=api.clReleaseProgram;
        program.value=api.clCreateProgramWithSource(context.value,1,&code,&len,&status);
        check(status,"clCreateProgramWithSource");
        const auto vendor=string_info(api,device,CL_DEVICE_VENDOR);
        amd_optimized=vendor=="Advanced Micro Devices, Inc." || vendor=="AMD";
        // Keep the measured RDNA 4 compiler profile specific to gfx1200.
        // ROCclr forwards -Wb options to LLVM; other vendors use standard C1.2.
        const auto* standard=amd_optimized ? "-cl-std=CL1.2 -DWIFI_GPU_AMD_OPENCL=1" : "-cl-std=CL1.2";
        bool tuned=amd_optimized && string_info(api,device,CL_DEVICE_NAME)=="gfx1200";
        status=api.clBuildProgram(program.value,1,&device,tuned ?
            "-cl-std=CL1.2 -DWIFI_GPU_AMD_OPENCL=1 -DWIFI_GPU_OPENCL_GFX1200=1 -Wb,-misched=gcn-max-ilp" : standard,nullptr,nullptr);
        if(tuned && status!=CL_SUCCESS) {
            // A driver without the optional scheduler can still use the
            // original AMD kernel. Invalid source must also pass this build.
            tuned=false;
            status=api.clBuildProgram(program.value,1,&device,standard,nullptr,nullptr);
        }
        if(status!=CL_SUCCESS) {
            std::size_t n=0;
            api.clGetProgramBuildInfo(program.value,device,CL_PROGRAM_BUILD_LOG,0,nullptr,&n);
            std::vector<char> log(n+1,0);
            if(n) api.clGetProgramBuildInfo(program.value,device,CL_PROGRAM_BUILD_LOG,n,log.data(),nullptr);
            throw std::runtime_error("OpenCL kernel compilation failed: "+std::string(log.data()));
        }
        const char* init_name="pbkdf2_init";
        const char* loop_name="pbkdf2_loop_split";
        if(tuned) {
            auto probe=api.clCreateKernel(program.value,"pbkdf2_init_compact",&status);
            if(probe) { api.clReleaseKernel(probe); init_name="pbkdf2_init_compact"; compact_input=true; }
            else if(status!=CL_INVALID_KERNEL_NAME) check(status,"compact kernel lookup");
            probe=api.clCreateKernel(program.value,"pbkdf2_loop_pair",&status);
            if(probe) { api.clReleaseKernel(probe); loop_name="pbkdf2_loop_pair"; loop_items=1; }
            else if(status!=CL_INVALID_KERNEL_NAME) check(status,"paired kernel lookup");
        }
        if(stats) std::cerr<<"opencl_setup gfx1200_profile="<<tuned<<" compact_input="<<compact_input
            <<" loop_items="<<loop_items<<'\n';
        const std::pair<Resource<cl_kernel>*,const char*> kernels[]={
            {&init,init_name},{&loop,loop_name},{&finish,"pbkdf2_finish"}};
        kernel_group_limit=describe(api,device).max_work_group;
        for(auto item:kernels) {
            item.first->release=api.clReleaseKernel;
            item.first->value=api.clCreateKernel(program.value,item.second,&status);
            check(status,"clCreateKernel");
            std::size_t limit=0;
            check(api.clGetKernelWorkGroupInfo(item.first->value,device,CL_KERNEL_WORK_GROUP_SIZE,
                sizeof(limit),&limit,nullptr),"clGetKernelWorkGroupInfo");
            kernel_group_limit=std::min(kernel_group_limit,limit);
        }
        if(amd_optimized) {
            filter.release=api.clReleaseKernel;
            filter.value=api.clCreateKernel(program.value,"wpa2_sha1_filter",&status);
            // Custom PBKDF2 kernels can omit the optional verification filter.
            if(status!=CL_INVALID_KERNEL_NAME) check(status,"clCreateKernel(filter)");
            if(filter.value) {
                std::size_t limit=0;
                check(api.clGetKernelWorkGroupInfo(filter.value,device,CL_KERNEL_WORK_GROUP_SIZE,
                    sizeof(limit),&limit,nullptr),"clGetKernelWorkGroupInfo(filter)");
                kernel_group_limit=std::min(kernel_group_limit,limit);
            }
        }
    }
    Device info() const override;
    Batch derive(const std::vector<Bytes>&,const Bytes&,std::uint32_t,std::uint32_t,std::size_t) override;
    Batch derive_impl(const std::vector<Bytes>&,const Bytes&,std::uint32_t,std::uint32_t,std::size_t,bool);
    std::optional<Bytes> filter_wpa2(const std::vector<Pmk>&,const Bytes&,
                                   const Bytes&,const Bytes&,std::size_t) override;
    std::optional<FilteredBatch> derive_wpa2(const std::vector<Bytes>&,const Bytes&,const Bytes&,
                                          const Bytes&,const Bytes&,std::uint32_t,std::size_t) override;
    void validate_filter(std::size_t,const Bytes&,const Bytes&,const Bytes&,std::size_t) const;
    Bytes filter_buffer(cl_mem,std::size_t,const Bytes&,const Bytes&,const Bytes&,std::size_t,
                        std::chrono::steady_clock::time_point);
    cl_mem buffer(Buffer& buffer,std::size_t size,cl_mem_flags flags,const void* input=nullptr) {
        if(buffer.capacity<size) {
            cl_int status=0;
            auto next=api.clCreateBuffer(context.value,flags,size,nullptr,&status);
            check(status,"clCreateBuffer");
            if(buffer.memory.value) api.clReleaseMemObject(buffer.memory.value);
            buffer.memory.value=next; buffer.memory.release=api.clReleaseMemObject;
            buffer.capacity=size;
        }
        if(input) check(api.clEnqueueWriteBuffer(queue.value,buffer.memory.value,CL_TRUE,0,size,input,
            0,nullptr,nullptr),"clEnqueueWriteBuffer");
        return buffer.memory.value;
    }
    template<class T> void arg(cl_kernel kernel,cl_uint index,const T& value) {
        check(api.clSetKernelArg(kernel,index,sizeof(value),&value),"clSetKernelArg");
    }
    double launch(cl_kernel kernel,std::size_t count,std::size_t group) {
        const std::size_t global=((count+group-1)/group)*group;
        Resource<cl_event> event; event.release=api.clReleaseEvent;
        check(api.clEnqueueNDRangeKernel(queue.value,kernel,1,nullptr,&global,&group,0,nullptr,&event.value),
              "clEnqueueNDRangeKernel");
        check(api.clWaitForEvents(1,&event.value),"clWaitForEvents");
        cl_ulong start=0,end=0;
        check(api.clGetEventProfilingInfo(event.value,CL_PROFILING_COMMAND_START,sizeof(start),&start,nullptr),"profiling start");
        check(api.clGetEventProfilingInfo(event.value,CL_PROFILING_COMMAND_END,sizeof(end),&end,nullptr),"profiling end");
        if(end<start) throw std::runtime_error("Non-monotonic OpenCL event clock");
        return static_cast<double>(end-start)/1e6;
    }
};

std::vector<Device> opencl_devices() {
    Api api; std::vector<Device> result;
    for(auto id:enumerate(api)) result.push_back(describe(api,id));
    return result;
}
std::unique_ptr<GpuEngine> opencl_engine(std::size_t index,const std::string& source) {
    return std::make_unique<OpenClEngine>(index,source);
}
Device OpenClEngine::info() const { return describe(const_cast<Api&>(api),device); }

std::optional<Bytes> OpenClEngine::filter_wpa2(const std::vector<Pmk>& pmks,const Bytes& wpa_context,
                                             const Bytes& eapol,const Bytes& mic,std::size_t group) {
    if(!filter.value) return std::nullopt;
    const auto started=std::chrono::steady_clock::now();
    validate_filter(pmks.size(),wpa_context,eapol,mic,group);
    auto keys=buffer(filter_keys,pmks.size()*sizeof(Pmk),CL_MEM_READ_ONLY,pmks.data());
    return filter_buffer(keys,pmks.size(),wpa_context,eapol,mic,group,started);
}
void OpenClEngine::validate_filter(std::size_t count,const Bytes& wpa_context,const Bytes& eapol,
                                   const Bytes& mic,std::size_t group) const {
    if(count==0 || count>gpu_batch_limit(Backend::OpenCl) || wpa_context.size()!=76 ||
       eapol.size()<99 || eapol.size()>4096 || mic.size()!=16 || group==0 || group>kernel_group_limit)
        throw std::invalid_argument("Invalid WPA2 GPU filter input");
}
Bytes OpenClEngine::filter_buffer(cl_mem keys,std::size_t size,const Bytes& wpa_context,
    const Bytes& eapol,const Bytes& mic,std::size_t group,std::chrono::steady_clock::time_point started) {
    auto padded=[](Bytes message) {
        const auto bits=static_cast<std::uint64_t>(64+message.size())*8;
        message.push_back(0x80);
        while(message.size()%64!=56) message.push_back(0);
        for(int shift=56;shift>=0;shift-=8) message.push_back(static_cast<std::uint8_t>(bits>>shift));
        std::vector<cl_uint> words(message.size()/4);
        for(std::size_t i=0;i<message.size();++i) words[i/4]|=static_cast<cl_uint>(message[i])<<(24-8*(i%4));
        return words;
    };
    auto data=bytes("Pairwise key expansion"); data.push_back(0);
    data.insert(data.end(),wpa_context.begin(),wpa_context.end()); data.push_back(0);
    const auto ptk=padded(std::move(data)),message=padded(eapol);
    std::array<cl_uint,4> tag{};
    for(std::size_t i=0;i<mic.size();++i) tag[i/4]|=static_cast<cl_uint>(mic[i])<<(24-8*(i%4));
    auto ptk_buffer=buffer(filter_ptk,ptk.size()*sizeof(cl_uint),CL_MEM_READ_ONLY,ptk.data());
    auto message_buffer=buffer(filter_message,message.size()*sizeof(cl_uint),CL_MEM_READ_ONLY,message.data());
    auto tag_buffer=buffer(filter_tag,sizeof(tag),CL_MEM_READ_ONLY,tag.data());
    auto flags=buffer(filter_flags,size,CL_MEM_WRITE_ONLY);
    const auto count=static_cast<cl_uint>(size);
    const auto ptk_blocks=static_cast<cl_uint>(ptk.size()/16),message_blocks=static_cast<cl_uint>(message.size()/16);
    arg(filter.value,0,keys); arg(filter.value,1,ptk_buffer); arg(filter.value,2,ptk_blocks);
    arg(filter.value,3,message_buffer); arg(filter.value,4,message_blocks);
    arg(filter.value,5,tag_buffer); arg(filter.value,6,flags); arg(filter.value,7,count);
    const auto kernel_ms=launch(filter.value,count,group);
    if(kernel_ms>250.0) throw std::runtime_error("Verification kernel exceeded 250 ms launch budget");
    Bytes result(size);
    check(api.clEnqueueReadBuffer(queue.value,flags,CL_TRUE,0,result.size(),result.data(),0,nullptr,nullptr),
        "verification readback");
    if(stats) std::cerr<<"opencl_filter count="<<count<<" kernel_ms="<<kernel_ms<<" wall_ms="
        <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<'\n';
    return result;
}

std::optional<FilteredBatch> OpenClEngine::derive_wpa2(const std::vector<Bytes>& passwords,const Bytes& ssid,
    const Bytes& context_data,const Bytes& eapol,const Bytes& mic,std::uint32_t chunk,std::size_t group) {
    if(!filter.value) return std::nullopt;
    validate_filter(passwords.size(),context_data,eapol,mic,group);
    derive_impl(passwords,ssid,4096,chunk,group,false);
    const auto flags=filter_buffer(output_buffer.memory.value,passwords.size(),context_data,eapol,mic,group,
                                    std::chrono::steady_clock::now());
    FilteredBatch result{passwords.size(),{}};
    for(std::size_t i=0;i<flags.size();++i) if(flags[i]) result.hits.push_back({i,{}});
    // Usually zero keys leave the device. Bulk-read dense matches to avoid one
    // blocking transfer per candidate when a dictionary contains many repeats.
    if(result.hits.size()>16) {
        std::vector<Pmk> all(passwords.size());
        check(api.clEnqueueReadBuffer(queue.value,output_buffer.memory.value,CL_TRUE,0,all.size()*sizeof(Pmk),
            all.data(),0,nullptr,nullptr),"matched PMK readback");
        for(auto& hit:result.hits) hit.pmk=all[hit.index];
    } else for(auto& hit:result.hits) {
        check(api.clEnqueueReadBuffer(queue.value,output_buffer.memory.value,CL_TRUE,hit.index*sizeof(Pmk),sizeof(Pmk),
            hit.pmk.data(),0,nullptr,nullptr),"matched PMK readback");
    }
    if(stats) std::cerr<<"opencl_resident count="<<passwords.size()<<" hits="<<result.hits.size()
        <<" readback_pmks="<<(result.hits.size()>16?passwords.size():result.hits.size())<<'\n';
    return result;
}

Batch OpenClEngine::derive(const std::vector<Bytes>& passwords,const Bytes& ssid,
                  std::uint32_t iterations,std::uint32_t chunk,std::size_t group) {
    return derive_impl(passwords,ssid,iterations,chunk,group,true);
}
Batch OpenClEngine::derive_impl(const std::vector<Bytes>& passwords,const Bytes& ssid,
                  std::uint32_t iterations,std::uint32_t chunk,std::size_t group,bool readback) {
    if(passwords.empty() || passwords.size()>gpu_batch_limit(Backend::OpenCl)) throw std::invalid_argument("OpenCL batch must contain 1..262144 passwords");
    if(ssid.size()>32) throw std::invalid_argument("SSID exceeds 32 bytes");
    if(iterations==0 || iterations>4096) throw std::invalid_argument("Iterations must be 1..4096");
    if(chunk==0 || chunk>gpu_chunk_limit(Backend::OpenCl)) throw std::invalid_argument("Chunk must be 1..1024 rounds");
    if(group==0 || group>kernel_group_limit) throw std::invalid_argument("Work group exceeds kernel/device limit or is zero");
    std::size_t maximum=0;
    for(const auto& password:passwords) {
        if(password.size()>63) throw std::invalid_argument("Password exceeds prototype limit of 63 bytes");
        maximum=std::max(maximum,password.size());
    }
    const cl_uint stride=static_cast<cl_uint>(maximum+1);
    const auto started=std::chrono::steady_clock::now();
    auto& p=*this;
    const cl_uint count=static_cast<cl_uint>(passwords.size());
    cl_mem in=nullptr;
    if(compact_input) {
        packed_input.resize(passwords.size()*stride);
        for(std::size_t i=0;i<passwords.size();++i) {
            auto* target=packed_input.data()+i*stride;
            target[0]=static_cast<cl_uchar>(passwords[i].size());
            std::copy(passwords[i].begin(),passwords[i].end(),target+1);
        }
        in=buffer(input_buffer,packed_input.size(),CL_MEM_READ_ONLY,packed_input.data());
    } else {
        host_input.resize(passwords.size());
        for(std::size_t i=0;i<passwords.size();++i) {
            std::copy(passwords[i].begin(),passwords[i].end(),host_input[i].bytes);
            host_input[i].length=static_cast<cl_uint>(passwords[i].size());
        }
        in=buffer(input_buffer,host_input.size()*sizeof(Password),CL_MEM_READ_ONLY,host_input.data());
    }
    std::array<std::uint8_t,32> salt{};
    std::copy(ssid.begin(),ssid.end(),salt.begin());
    auto sb=buffer(salt_buffer,salt.size(),CL_MEM_READ_ONLY,salt.data());
    auto states=buffer(state_buffer,passwords.size()*30*sizeof(cl_uint),CL_MEM_READ_WRITE);
    auto out=buffer(output_buffer,passwords.size()*32,CL_MEM_READ_WRITE);
    const auto salt_length=static_cast<cl_uint>(ssid.size());
    p.arg(p.init.value,0,in); p.arg(p.init.value,1,sb); p.arg(p.init.value,2,salt_length);
    p.arg(p.init.value,3,states); p.arg(p.init.value,4,count);
    if(compact_input) p.arg(p.init.value,5,stride);
    p.arg(p.loop.value,0,states); p.arg(p.loop.value,2,count);
    p.arg(p.finish.value,0,states); p.arg(p.finish.value,1,out); p.arg(p.finish.value,2,count);
    Batch result;
    auto run=[&](cl_kernel kernel) {
        auto ms=p.launch(kernel,passwords.size()*(kernel==p.loop.value ? p.loop_items : 1),group);
        result.gpu_ms+=ms; result.max_launch_ms=std::max(result.max_launch_ms,ms);
        result.max_submission_ms=result.max_launch_ms; ++result.submissions;
        if(ms>250.0) throw std::runtime_error("Kernel exceeded 250 ms launch budget; reduce --batch or --chunk");
        return ms;
    };
    run(p.init.value);
    double slowest_round_ms=0;
    for(cl_uint done=1;done<iterations;) {
        cl_uint rounds=std::min(chunk,iterations-done);
        if(passwords.size()>4096) {
            // Probe with at most eight rounds before expanding large launches.
            // Treat --chunk as a ceiling and target 50 ms using the slowest
            // observed round. The existing 250 ms hard check remains in force.
            const auto safe=slowest_round_ms>0 ?
                static_cast<cl_uint>(std::max(1.0,std::min(static_cast<double>(gpu_chunk_limit(Backend::OpenCl)),50.0/slowest_round_ms))) : 8U;
            rounds=std::min(rounds,safe);
        }
        p.arg(p.loop.value,1,rounds);
        const auto ms=run(p.loop.value);
        slowest_round_ms=std::max(slowest_round_ms,ms/rounds);
        done+=rounds;
    }
    run(p.finish.value);
    static_assert(sizeof(Pmk)==32,"PMK output layout");
    if(readback) {
        result.pmks.resize(passwords.size());
        check(api.clEnqueueReadBuffer(p.queue.value,out,CL_TRUE,0,result.pmks.size()*32,
            result.pmks.data(),0,nullptr,nullptr),"clEnqueueReadBuffer");
    }
    result.wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    if(stats) std::cerr<<"opencl_derive count="<<count<<" kernel_ms="<<result.gpu_ms<<" wall_ms="<<result.wall_ms<<'\n';
    return result;
}
}
