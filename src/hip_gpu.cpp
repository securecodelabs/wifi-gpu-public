#include "gpu_backend.hpp"
#include <stdexcept>

#ifndef WIFI_GPU_ENABLE_HIP
namespace wifi {
std::vector<Device> hip_devices() {
    throw std::runtime_error("HIP backend not built; configure Linux with -DWIFI_GPU_ENABLE_HIP=ON and ROCm 7 headers");
}
std::unique_ptr<GpuEngine> hip_engine(std::size_t,const std::string&) {
    hip_devices();
    return nullptr;
}
}
#else
#include "platform.hpp"
#include <hip/hip_runtime_api.h>
#include <hip/hiprtc.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>

namespace wifi {
namespace {
void check(hipError_t code,const char* operation) {
    if(code!=hipSuccess) throw std::runtime_error(std::string(operation)+" failed (HIP "+std::to_string(code)+")");
}
void rtc_check(hiprtcResult code,const char* operation) {
    if(code!=HIPRTC_SUCCESS) throw std::runtime_error(std::string(operation)+" failed (HIPRTC "+std::to_string(code)+")");
}
#define HIP_FUNCTIONS(X) \
    X(hipGetDeviceCount) X(hipGetDeviceProperties) X(hipSetDevice) X(hipRuntimeGetVersion) \
    X(hipStreamCreateWithFlags) X(hipStreamDestroy) X(hipStreamSynchronize) \
    X(hipEventCreate) X(hipEventDestroy) X(hipEventRecord) X(hipEventSynchronize) X(hipEventElapsedTime) \
    X(hipModuleLoadData) X(hipModuleUnload) X(hipModuleGetFunction) X(hipModuleLaunchKernel) \
    X(hipFuncGetAttribute) X(hipFree) X(hipMemcpy)
#define RTC_FUNCTIONS(X) \
    X(hiprtcCreateProgram) X(hiprtcDestroyProgram) X(hiprtcCompileProgram) \
    X(hiprtcGetProgramLogSize) X(hiprtcGetProgramLog) X(hiprtcGetCodeSize) X(hiprtcGetCode)
// HIP headers rename some APIs (notably hipGetDevicePropertiesR0600). Resolve
// the expanded symbol, so the function ABI matches the installed SDK headers.
#define HIP_SYMBOL_IMPL(name) #name
#define HIP_SYMBOL(name) HIP_SYMBOL_IMPL(name)
struct Api {
    DynamicLibrary runtime{L"amdhip64_7.dll","libamdhip64.so.7"};
    DynamicLibrary compiler{L"hiprtc0700.dll","libhiprtc.so.7"};
    template<class T> T resolve(const DynamicLibrary& library,const char* name) {
        const auto address=library.symbol(name);
        if(!address) throw std::runtime_error(std::string("HIP function missing: ")+name);
        T result=nullptr;
        static_assert(sizeof(result)==sizeof(address),"Loader function-pointer ABI");
        std::memcpy(&result,&address,sizeof(result));
        return result;
    }
#define DECLARE(name) decltype(&::name) name=nullptr;
    HIP_FUNCTIONS(DECLARE)
    RTC_FUNCTIONS(DECLARE)
#undef DECLARE
    decltype(static_cast<hipError_t (*)(void**,std::size_t)>(&::hipMalloc)) hipMalloc=nullptr;
    Api() {
#define RESOLVE_HIP(name) name=resolve<decltype(name)>(runtime,HIP_SYMBOL(name));
#define RESOLVE_RTC(name) name=resolve<decltype(name)>(compiler,HIP_SYMBOL(name));
        HIP_FUNCTIONS(RESOLVE_HIP)
        RTC_FUNCTIONS(RESOLVE_RTC)
        hipMalloc=resolve<decltype(hipMalloc)>(runtime,"hipMalloc");
#undef RESOLVE_HIP
#undef RESOLVE_RTC
    }
};
#undef HIP_FUNCTIONS
#undef RTC_FUNCTIONS
#undef HIP_SYMBOL
#undef HIP_SYMBOL_IMPL

template<class T> struct Resource {
    T value=nullptr;
    hipError_t (*release)(T)=nullptr;
    Resource()=default;
    ~Resource() { if(value && release) (void)release(value); }
    Resource(const Resource&)=delete;
    Resource& operator=(const Resource&)=delete;
};
struct Program {
    Api& api;
    hiprtcProgram value=nullptr;
    explicit Program(Api& a):api(a) {}
    ~Program() { if(value) (void)api.hiprtcDestroyProgram(&value); }
    Program(const Program&)=delete;
    Program& operator=(const Program&)=delete;
};
Device describe(Api& api,int id) {
    hipDeviceProp_t props{};
    check(api.hipGetDeviceProperties(&props,id),"hipGetDeviceProperties");
    int version=0;
    check(api.hipRuntimeGetVersion(&version),"hipRuntimeGetVersion");
    const auto version_text=std::to_string(version/10000000)+"."+
        std::to_string((version/100000)%100)+"."+std::to_string(version%100000);
    return {props.name,"AMD",version_text,"HIP "+version_text,
            std::string("HIPRTC C++17 ")+props.gcnArchName,static_cast<std::uint32_t>(props.multiProcessorCount),
            props.totalGlobalMem,props.totalGlobalMem,static_cast<std::size_t>(props.maxThreadsPerBlock)};
}
struct Password { std::uint8_t bytes[64]{}; std::uint32_t length=0; };
static_assert(sizeof(Password)==68,"Host/HIP password layout must match");
}

struct HipEngine final : GpuEngine {
    Api api;
    int device=0;
    Device description;
    Resource<hipStream_t> stream;
    Resource<hipModule_t> module;
    Resource<hipEvent_t> begin_event,end_event;
    struct Kernel {
        hipFunction_t value=nullptr;
        struct Argument { alignas(void*) std::array<unsigned char,8> data{}; };
        std::array<Argument,8> arguments{};
    };
    Kernel init,loop,finish,filter;
    std::size_t loop_items=2;
    struct Buffer { Resource<void*> memory; std::size_t capacity=0; };
    Buffer input_buffer,salt_buffer,state_buffer,output_buffer;
    Buffer filter_keys,filter_ptk,filter_message,filter_tag,filter_flags;
    std::vector<Password> host_input;
    bool stats=[] { const auto* v=std::getenv("WIFI_GPU_HIP_STATS"); return v && std::string(v)=="1"; }();
    std::size_t kernel_group_limit=256;
    HipEngine(std::size_t index,const std::string& source) {
        int count=0;
        check(api.hipGetDeviceCount(&count),"hipGetDeviceCount");
        if(count<0 || index>=static_cast<std::size_t>(count)) throw std::invalid_argument("GPU index out of range; run devices");
        if(source.empty()) throw std::invalid_argument("Empty kernel source");
        device=static_cast<int>(index);
        select();
        description=describe(api,device);
        hipDeviceProp_t props{};
        check(api.hipGetDeviceProperties(&props,device),"hipGetDeviceProperties");
        stream.release=api.hipStreamDestroy;
        check(api.hipStreamCreateWithFlags(&stream.value,hipStreamNonBlocking),"hipStreamCreateWithFlags");
        begin_event.release=end_event.release=api.hipEventDestroy;
        check(api.hipEventCreate(&begin_event.value),"hipEventCreate(start)");
        check(api.hipEventCreate(&end_event.value),"hipEventCreate(end)");
        Program program(api);
        rtc_check(api.hiprtcCreateProgram(&program.value,source.c_str(),"pbkdf2.hip",0,nullptr,nullptr),"hiprtcCreateProgram");
        const auto arch=std::string("--gpu-architecture=")+props.gcnArchName;
        std::vector<const char*> options={arch.c_str(),"-std=c++17","-O3","-DWIFI_GPU_HIP=1"};
        // The gfx1200 SHA-1 schedule benefits from instruction parallelism.
        // Other architectures retain HIPRTC's default scheduling policy.
        const auto target=std::string(props.gcnArchName);
        const bool paired=target.substr(0,target.find(':'))=="gfx1200";
        if(paired) {
            options.push_back("-mllvm"); options.push_back("-misched=gcn-max-ilp");
            options.push_back("-DWIFI_GPU_HIP_PAIR=1");
        }
        const auto status=api.hiprtcCompileProgram(program.value,static_cast<int>(options.size()),options.data());
        if(status!=HIPRTC_SUCCESS) {
            std::size_t size=0;
            rtc_check(api.hiprtcGetProgramLogSize(program.value,&size),"hiprtcGetProgramLogSize");
            std::vector<char> log(size+1,0);
            if(size) rtc_check(api.hiprtcGetProgramLog(program.value,log.data()),"hiprtcGetProgramLog");
            throw std::runtime_error("HIP kernel compilation failed: "+std::string(log.data()));
        }
        std::size_t size=0;
        rtc_check(api.hiprtcGetCodeSize(program.value,&size),"hiprtcGetCodeSize");
        if(size==0) throw std::runtime_error("HIPRTC returned empty code");
        std::vector<char> code(size);
        rtc_check(api.hiprtcGetCode(program.value,code.data()),"hiprtcGetCode");
        module.release=api.hipModuleUnload;
        check(api.hipModuleLoadData(&module.value,code.data()),"hipModuleLoadData");
        kernel_group_limit=std::min(kernel_group_limit,description.max_work_group);
        const char* loop_name="pbkdf2_loop_split";
        if(paired) {
            // Custom kernels predating the paired ABI still launch two items
            // per password. Only a missing symbol permits that fallback.
            const auto result=api.hipModuleGetFunction(&loop.value,module.value,"pbkdf2_loop_pair");
            if(result==hipSuccess) { loop_name="pbkdf2_loop_pair"; loop_items=1; }
            else if(result!=hipErrorNotFound) check(result,"HIP paired kernel lookup");
        }
        const std::pair<Kernel*,const char*> kernels[]={
            {&init,"pbkdf2_init"},{&loop,loop_name},{&finish,"pbkdf2_finish"},{&filter,"wpa2_sha1_filter"}};
        for(auto item:kernels) {
            const auto result=api.hipModuleGetFunction(&item.first->value,module.value,item.second);
            if(item.first==&filter && result==hipErrorNotFound) continue;
            check(result,"hipModuleGetFunction");
            int limit=0;
            check(api.hipFuncGetAttribute(&limit,HIP_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK,item.first->value),"HIP kernel block limit");
            if(limit<=0) throw std::runtime_error("Invalid HIP kernel block limit");
            kernel_group_limit=std::min(kernel_group_limit,static_cast<std::size_t>(limit));
            if(stats) {
                int regs=0,local=0;
                check(api.hipFuncGetAttribute(&regs,HIP_FUNC_ATTRIBUTE_NUM_REGS,item.first->value),"HIP kernel register count");
                check(api.hipFuncGetAttribute(&local,HIP_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES,item.first->value),"HIP kernel private memory");
                std::cerr<<"hip_kernel name="<<item.second<<" registers="<<regs<<" local_bytes="<<local<<" block_limit="<<limit<<'\n';
            }
        }
    }
    ~HipEngine() override {
        // Members release allocations before unloading the runtime. Ensure the
        // owning device is current even if another engine was used in between.
        (void)api.hipSetDevice(device);
        if(stream.value) (void)api.hipStreamSynchronize(stream.value);
    }
    void select() { check(api.hipSetDevice(device),"hipSetDevice"); }
    Device info() const override { return description; }
    Batch derive(const std::vector<Bytes>&,const Bytes&,std::uint32_t,std::uint32_t,std::size_t) override;
    Batch derive_impl(const std::vector<Bytes>&,const Bytes&,std::uint32_t,std::uint32_t,std::size_t,bool);
    std::optional<Bytes> filter_wpa2(const std::vector<Pmk>&,const Bytes&,const Bytes&,const Bytes&,std::size_t) override;
    std::optional<FilteredBatch> derive_wpa2(const std::vector<Bytes>&,const Bytes&,const Bytes&,const Bytes&,const Bytes&,std::uint32_t,std::size_t) override;
    void validate_filter(std::size_t,const Bytes&,const Bytes&,const Bytes&,std::size_t) const;
    Bytes filter_buffer(void*,std::size_t,const Bytes&,const Bytes&,const Bytes&,std::size_t,std::chrono::steady_clock::time_point);
    void* buffer(Buffer& b,std::size_t size,const void* input=nullptr) {
        if(b.capacity<size) {
            Resource<void*> next; next.release=api.hipFree;
            check(api.hipMalloc(&next.value,size),"hipMalloc");
            if(b.memory.value) check(api.hipFree(b.memory.value),"hipFree resize");
            b.memory.value=std::exchange(next.value,nullptr); b.memory.release=api.hipFree; b.capacity=size;
        }
        if(input) check(api.hipMemcpy(b.memory.value,input,size,hipMemcpyHostToDevice),"hipMemcpy upload");
        return b.memory.value;
    }
    void read(void* source,std::size_t offset,std::size_t size,void* destination) {
        check(api.hipMemcpy(destination,static_cast<unsigned char*>(source)+offset,size,hipMemcpyDeviceToHost),"HIP readback");
    }
    template<class T> void arg(Kernel& kernel,std::size_t index,const T& value) {
        static_assert(sizeof(T)<=8,"HIP kernel argument size");
        std::memcpy(kernel.arguments.at(index).data.data(),&value,sizeof(value));
    }
    double launch(Kernel& kernel,std::size_t count,std::size_t group) {
        std::array<void*,8> params{};
        for(std::size_t i=0;i<params.size();++i) params[i]=kernel.arguments[i].data.data();
        check(api.hipEventRecord(begin_event.value,stream.value),"hipEventRecord(start)");
        check(api.hipModuleLaunchKernel(kernel.value,static_cast<unsigned>((count+group-1)/group),1,1,
            static_cast<unsigned>(group),1,1,0,stream.value,params.data(),nullptr),"hipModuleLaunchKernel");
        check(api.hipEventRecord(end_event.value,stream.value),"hipEventRecord(end)");
        check(api.hipEventSynchronize(end_event.value),"hipEventSynchronize");
        float ms=0;
        check(api.hipEventElapsedTime(&ms,begin_event.value,end_event.value),"hipEventElapsedTime");
        if(!std::isfinite(ms) || ms<0) throw std::runtime_error("Invalid HIP event timing");
        return ms;
    }
};

std::vector<Device> hip_devices() {
    Api api;
    int count=0; check(api.hipGetDeviceCount(&count),"hipGetDeviceCount");
    std::vector<Device> result;
    for(int id=0;id<count;++id) result.push_back(describe(api,id));
    return result;
}
std::unique_ptr<GpuEngine> hip_engine(std::size_t index,const std::string& source) {
    return std::make_unique<HipEngine>(index,source);
}

std::optional<Bytes> HipEngine::filter_wpa2(const std::vector<Pmk>& pmks,const Bytes& wpa_context,
                                             const Bytes& eapol,const Bytes& mic,std::size_t group) {
    select();
    if(!filter.value) return std::nullopt;
    const auto started=std::chrono::steady_clock::now();
    validate_filter(pmks.size(),wpa_context,eapol,mic,group);
    auto keys=buffer(filter_keys,pmks.size()*sizeof(Pmk),pmks.data());
    return filter_buffer(keys,pmks.size(),wpa_context,eapol,mic,group,started);
}
void HipEngine::validate_filter(std::size_t count,const Bytes& wpa_context,const Bytes& eapol,
                                   const Bytes& mic,std::size_t group) const {
    if(count==0 || count>gpu_batch_limit(Backend::Hip) || wpa_context.size()!=76 ||
       eapol.size()<99 || eapol.size()>4096 || mic.size()!=16 || group==0 || group>kernel_group_limit)
        throw std::invalid_argument("Invalid WPA2 GPU filter input");
}
Bytes HipEngine::filter_buffer(void* keys,std::size_t size,const Bytes& wpa_context,
    const Bytes& eapol,const Bytes& mic,std::size_t group,std::chrono::steady_clock::time_point started) {
    auto padded=[](Bytes message) {
        const auto bits=static_cast<std::uint64_t>(64+message.size())*8;
        message.push_back(0x80);
        while(message.size()%64!=56) message.push_back(0);
        for(int shift=56;shift>=0;shift-=8) message.push_back(static_cast<std::uint8_t>(bits>>shift));
        std::vector<std::uint32_t> words(message.size()/4);
        for(std::size_t i=0;i<message.size();++i) words[i/4]|=static_cast<std::uint32_t>(message[i])<<(24-8*(i%4));
        return words;
    };
    auto data=bytes("Pairwise key expansion"); data.push_back(0);
    data.insert(data.end(),wpa_context.begin(),wpa_context.end()); data.push_back(0);
    const auto ptk=padded(std::move(data)),message=padded(eapol);
    std::array<std::uint32_t,4> tag{};
    for(std::size_t i=0;i<mic.size();++i) tag[i/4]|=static_cast<std::uint32_t>(mic[i])<<(24-8*(i%4));
    auto ptk_buffer=buffer(filter_ptk,ptk.size()*sizeof(std::uint32_t),ptk.data());
    auto message_buffer=buffer(filter_message,message.size()*sizeof(std::uint32_t),message.data());
    auto tag_buffer=buffer(filter_tag,sizeof(tag),tag.data());
    auto flags=buffer(filter_flags,size);
    const auto count=static_cast<std::uint32_t>(size);
    const auto ptk_blocks=static_cast<std::uint32_t>(ptk.size()/16),message_blocks=static_cast<std::uint32_t>(message.size()/16);
    arg(filter,0,keys); arg(filter,1,ptk_buffer); arg(filter,2,ptk_blocks);
    arg(filter,3,message_buffer); arg(filter,4,message_blocks);
    arg(filter,5,tag_buffer); arg(filter,6,flags); arg(filter,7,count);
    const auto kernel_ms=launch(filter,count,group);
    if(kernel_ms>250.0) throw std::runtime_error("Verification kernel exceeded 250 ms launch budget");
    Bytes result(size);
    read(flags,0,result.size(),result.data());
    if(stats) std::cerr<<"hip_filter count="<<count<<" kernel_ms="<<kernel_ms<<" wall_ms="
        <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<'\n';
    return result;
}

std::optional<FilteredBatch> HipEngine::derive_wpa2(const std::vector<Bytes>& passwords,const Bytes& ssid,
    const Bytes& context_data,const Bytes& eapol,const Bytes& mic,std::uint32_t chunk,std::size_t group) {
    select();
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
        read(output_buffer.memory.value,0,all.size()*sizeof(Pmk),all.data());
        for(auto& hit:result.hits) hit.pmk=all[hit.index];
    } else for(auto& hit:result.hits) {
        read(output_buffer.memory.value,hit.index*sizeof(Pmk),sizeof(Pmk),hit.pmk.data());
    }
    if(stats) std::cerr<<"hip_resident count="<<passwords.size()<<" hits="<<result.hits.size()
        <<" readback_pmks="<<(result.hits.size()>16?passwords.size():result.hits.size())<<'\n';
    return result;
}

Batch HipEngine::derive(const std::vector<Bytes>& passwords,const Bytes& ssid,
                  std::uint32_t iterations,std::uint32_t chunk,std::size_t group) {
    return derive_impl(passwords,ssid,iterations,chunk,group,true);
}
Batch HipEngine::derive_impl(const std::vector<Bytes>& passwords,const Bytes& ssid,
                  std::uint32_t iterations,std::uint32_t chunk,std::size_t group,bool readback) {
    select();
    if(passwords.empty() || passwords.size()>gpu_batch_limit(Backend::Hip)) throw std::invalid_argument("HIP batch must contain 1..262144 passwords");
    if(ssid.size()>32) throw std::invalid_argument("SSID exceeds 32 bytes");
    if(iterations==0 || iterations>4096) throw std::invalid_argument("Iterations must be 1..4096");
    if(chunk==0 || chunk>256) throw std::invalid_argument("Chunk must be 1..256 rounds");
    if(group==0 || group>kernel_group_limit) throw std::invalid_argument("Work group exceeds kernel/device limit or is zero");
    host_input.resize(passwords.size());
    auto& input=host_input;
    for(std::size_t i=0;i<input.size();++i) {
        if(passwords[i].size()>63) throw std::invalid_argument("Password exceeds prototype limit of 63 bytes");
        std::copy(passwords[i].begin(),passwords[i].end(),input[i].bytes);
        input[i].length=static_cast<std::uint32_t>(passwords[i].size());
    }
    const auto started=std::chrono::steady_clock::now();
    auto& p=*this;
    const std::uint32_t count=static_cast<std::uint32_t>(passwords.size());
    auto in=buffer(input_buffer,input.size()*sizeof(Password),input.data());
    std::array<std::uint8_t,32> salt{};
    std::copy(ssid.begin(),ssid.end(),salt.begin());
    auto sb=buffer(salt_buffer,salt.size(),salt.data());
    auto states=buffer(state_buffer,passwords.size()*30*sizeof(std::uint32_t));
    auto out=buffer(output_buffer,passwords.size()*32);
    const auto salt_length=static_cast<std::uint32_t>(ssid.size());
    p.arg(p.init,0,in); p.arg(p.init,1,sb); p.arg(p.init,2,salt_length);
    p.arg(p.init,3,states); p.arg(p.init,4,count);
    p.arg(p.loop,0,states); p.arg(p.loop,2,count);
    p.arg(p.finish,0,states); p.arg(p.finish,1,out); p.arg(p.finish,2,count);
    Batch result;
    auto run=[&](Kernel& kernel) {
        auto ms=p.launch(kernel,passwords.size()*(&kernel==&p.loop ? p.loop_items : 1),group);
        result.gpu_ms+=ms; result.max_launch_ms=std::max(result.max_launch_ms,ms);
        result.max_submission_ms=result.max_launch_ms; ++result.submissions;
        if(ms>250.0) throw std::runtime_error("Kernel exceeded 250 ms launch budget; reduce --batch or --chunk");
        return ms;
    };
    run(p.init);
    double slowest_round_ms=0;
    for(std::uint32_t done=1;done<iterations;) {
        std::uint32_t rounds=std::min(chunk,iterations-done);
        if(passwords.size()>4096) {
            // Probe with at most eight rounds before expanding large launches.
            // Treat --chunk as a ceiling and target 50 ms using the slowest
            // observed round. The existing 250 ms hard check remains in force.
            const auto safe=slowest_round_ms>0 ?
                static_cast<std::uint32_t>(std::max(1.0,std::min(256.0,50.0/slowest_round_ms))) : 8U;
            rounds=std::min(rounds,safe);
        }
        p.arg(p.loop,1,rounds);
        const auto ms=run(p.loop);
        slowest_round_ms=std::max(slowest_round_ms,ms/rounds);
        done+=rounds;
    }
    run(p.finish);
    static_assert(sizeof(Pmk)==32,"PMK output layout");
    if(readback) {
        result.pmks.resize(passwords.size());
        read(out,0,result.pmks.size()*32,result.pmks.data());
    }
    result.wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    if(stats) std::cerr<<"hip_derive count="<<count<<" kernel_ms="<<result.gpu_ms<<" wall_ms="<<result.wall_ms<<'\n';
    return result;
}
}
#endif
