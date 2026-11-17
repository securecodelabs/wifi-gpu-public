#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "platform.hpp"
#ifdef _WIN32
#include <shellapi.h>
#endif
#include "gpu.hpp"
#include "cpu.hpp"
#include "crypto.hpp"
#include "recovery.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

namespace {
constexpr const char* version=WIFI_GPU_VERSION;
struct Options {
    std::string command="help",kernel;
    std::size_t device=0,batch=1024,group=64,repeats=3,threads=0;
    std::uint32_t chunk=256;
    wifi::Backend backend=wifi::default_backend();
    bool cpu_only=false,allow_software=false;
};
void require(bool ok,const std::string& message) { if(!ok) throw std::runtime_error(message); }
std::size_t integer(const std::string& text) {
    std::size_t n=0; const auto r=std::from_chars(text.data(),text.data()+text.size(),n);
    if(r.ec!=std::errc{} || r.ptr!=text.data()+text.size()) throw std::invalid_argument("Invalid unsigned integer: "+text);
    return n;
}
Options options(int argc,char** argv) {
    Options o;
    if(argc<2) return o;
    o.command=argv[1];
    const std::set<std::string> commands={"help","--help","--version","devices","selftest","benchmark"};
    if(!commands.count(o.command)) throw std::invalid_argument("Unknown command: "+o.command);
    const std::set<std::string> common={"--device","--kernel"};
    const std::set<std::string> bench={"--batch","--group","--chunk","--repeats","--threads"};
    std::set<std::string> used;
    for(int i=2;i<argc;++i) {
        std::string key=argv[i];
        if(!used.insert(key).second) throw std::invalid_argument("Duplicate option: "+key);
        if((key=="--cpu-only" && o.command=="selftest") || (key=="--cpu" && o.command=="benchmark")) { o.cpu_only=true; continue; }
        if(key=="--allow-software-vulkan" && (o.command=="devices" || o.command=="selftest" || o.command=="benchmark")) { o.allow_software=true; continue; }
        bool allowed=(key=="--backend" && (o.command=="devices" || o.command=="selftest" || o.command=="benchmark")) || ((o.command=="selftest" || o.command=="benchmark") && common.count(key)) ||
                     (o.command=="benchmark" && bench.count(key));
        if(!allowed) throw std::invalid_argument("Unsupported option for "+o.command+": "+key);
        if(i+1>=argc) throw std::invalid_argument("Missing value for "+key);
        std::string value=argv[++i];
        if(key=="--backend") { o.backend=wifi::parse_backend(value); continue; }
        if(key=="--kernel") { o.kernel=value; continue; }
        auto n=integer(value);
        if(key=="--threads") { if(n<1 || n>64) throw std::invalid_argument("--threads must be 1..64"); o.threads=n; }
        else if(key=="--device") o.device=n;
        else if(key=="--batch") { if(n<1 || n>wifi::gpu_batch_limit(wifi::Backend::OpenCl)) throw std::invalid_argument("--batch must be 1..262144"); o.batch=n; }
        else if(key=="--group") { if(n<1 || n>256) throw std::invalid_argument("--group must be 1..256"); o.group=n; }
        else if(key=="--chunk") { if(n<1 || n>wifi::gpu_chunk_limit(wifi::Backend::Vulkan)) throw std::invalid_argument("--chunk must be 1..1024 (OpenCL/Vulkan) or 1..256 (HIP)"); o.chunk=static_cast<std::uint32_t>(n); }
        else if(key=="--repeats") { if(n<1 || n>10) throw std::invalid_argument("--repeats must be 1..10"); o.repeats=n; }
    }
    if(o.cpu_only && (used.count("--backend") || used.count("--device") || used.count("--kernel") || used.count("--group") || used.count("--chunk")))
        throw std::invalid_argument("CPU mode cannot be combined with GPU options");
    if(used.count("--threads") && !o.cpu_only) throw std::invalid_argument("--threads requires --cpu");
    if(o.chunk>wifi::gpu_chunk_limit(o.backend)) throw std::invalid_argument("--chunk exceeds backend limit (OpenCL/Vulkan: 1024; HIP: 256)");
    if(o.allow_software && (o.cpu_only || o.backend!=wifi::Backend::Vulkan)) throw std::invalid_argument("--allow-software-vulkan requires Vulkan mode");
    if(!used.count("--batch") && !o.cpu_only && o.backend==wifi::Backend::Hip) o.batch=262144;
    if(o.batch>(o.cpu_only ? 4096 : wifi::gpu_batch_limit(o.backend)))
        throw std::invalid_argument("--batch exceeds backend limit (GPU: 262144; CPU: 4096)");
    return o;
}
std::string source(const Options& o) {
    std::filesystem::path path=std::filesystem::u8path(o.kernel);
    if(path.empty()) {
        path=wifi::executable_directory()/"kernels"/(o.backend==wifi::Backend::Vulkan ? "pbkdf2.spv" : "pbkdf2.cl");
    }
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::runtime_error("Cannot open kernel: "+path.string());
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
struct Tests {
    unsigned passed=0;
    void check(bool ok,const std::string& label) { require(ok,"FAIL: "+label); ++passed; }
    template<class F> void rejects(F f,const std::string& label) {
        try { f(); } catch(const std::invalid_argument&) { ++passed; return; }
        throw std::runtime_error("FAIL: did not reject "+label);
    }
};
void cpu_tests(Tests& tests) {
    using namespace wifi;
    const auto pass=bytes("password"),salt=bytes("salt");
    const std::pair<unsigned,const char*> vectors[]={
        {1,"0c60c80f961f0e71f3a9b524af6012062fe037a6"},
        {2,"ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957"},
        {4096,"4b007901b765489abead49d926f721d065a429c1"}};
    for(auto v:vectors) tests.check(hex(reference(pass,salt,v.first)).substr(0,40)==v.second,"RFC 6070 iteration "+std::to_string(v.first));
    tests.check(hex(reference(bytes("password"),bytes("IEEE"),4096))==
        "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e","WPA full 32-byte PMK");
    tests.check(hex(reference(Bytes{'p','a','s','s',0,'w','o','r','d'},Bytes{'s','a',0,'l','t'},4096)).substr(0,32)==
        "56fa6aa75548099dcc37d7f03425e0c3","RFC 6070 embedded NUL");
    tests.rejects([&]{reference(pass,salt,0);},"zero iterations");
    tests.rejects([&]{reference(Bytes(64,1),salt,1);},"oversized password");
    tests.rejects([&]{reference(pass,Bytes(33,1),1);},"oversized SSID");
    tests.rejects([]{integer("-1");},"negative integer");
    tests.rejects([]{integer("12tail");},"trailing integer text");
    tests.rejects([]{integer("");},"empty integer");
    tests.rejects([]{integer("999999999999999999999999999999999");},"integer overflow");
    tests.check(integer("0")==0 && integer("4096")==4096,"valid integer options");
    // All worker counts preserve ordering and byte-oriented PBKDF2 behavior.
    std::vector<Bytes> inputs;
    for(unsigned n=0;n<=63;++n) {
        Bytes p(n); for(unsigned i=0;i<n;++i) p[i]=static_cast<std::uint8_t>(i*71+n);
        inputs.push_back(p);
    }
    for(std::size_t workers:{1U,2U,3U,12U}) {
        Cpu cpu(workers);
        tests.check(cpu.threads()==workers,"CPU worker count");
        for(const auto& ssid:{Bytes{},bytes("IEEE"),Bytes(32,255)}) {
            auto batch=cpu.derive(inputs,ssid,129);
            tests.check(batch.pmks.size()==inputs.size(),"CPU batch size");
            for(std::size_t i=0;i<inputs.size();++i)
                tests.check(batch.pmks[i]==reference(inputs[i],ssid,129),"CPU worker output/order");
        }
        std::atomic<bool> cancelled{true};
        tests.check(cpu.derive(inputs,salt,4096,&cancelled).pmks.empty(),"CPU cancelled batch");
        cancelled=false;
        auto one=cpu.derive({pass},salt,4096,&cancelled);
        tests.check(one.pmks[0]==reference(pass,salt,4096),"CPU reuse after cancellation/shrink");
        tests.rejects([&]{cpu.derive({},salt);},"empty CPU batch");
        tests.rejects([&]{cpu.derive(std::vector<Bytes>(4097,pass),salt);},"large CPU batch");
        tests.rejects([&]{cpu.derive({Bytes(64,0)},salt);},"long CPU password");
        tests.rejects([&]{cpu.derive({pass},Bytes(33,0));},"long CPU salt");
        tests.rejects([&]{cpu.derive({pass},salt,0);},"zero CPU iterations");
        tests.rejects([&]{cpu.derive({pass},salt,4097);},"large CPU iterations");
    }
    tests.rejects([]{Cpu cpu(65);},"excess CPU workers");
}
void gpu_filter_tests(wifi::Gpu& gpu,Tests& tests) {
    using namespace wifi;
    Bytes context(76);
    for(std::size_t i=0;i<context.size();++i) context[i]=static_cast<std::uint8_t>(i*37);
    auto tag_for=[&](const Pmk& pmk,const Bytes& message) {
        auto input=bytes("Pairwise key expansion"); input.push_back(0);
        input.insert(input.end(),context.begin(),context.end()); input.push_back(0);
        auto kck=digest(Algorithm::HmacSha1,input,Bytes(pmk.begin(),pmk.end())); kck.resize(16);
        auto tag=digest(Algorithm::HmacSha1,message,kck); tag.resize(16); return tag;
    };
    std::size_t compared=0;
    for(auto length:{99U,111U,112U,119U,120U,127U,128U,4096U}) {
        Bytes message(length);
        for(std::size_t i=0;i<message.size();++i) message[i]=static_cast<std::uint8_t>(i*29+length);
        std::vector<Pmk> pmks(length==99 ? 65536 : 65);
        for(std::size_t i=0;i<pmks.size();++i)
            for(std::size_t j=0;j<32;++j) pmks[i][j]=static_cast<std::uint8_t>((i>>(8*(j%2)))+j*11);
        const auto tag=tag_for(pmks.back(),message);
        auto flags=gpu.filter_wpa2(pmks,context,message,tag,64);
        if(!flags) return; // Optional kernel: other vendors/backends keep CPU verification.
        tests.check(flags->size()==pmks.size(),"WPA2 filter output count");
        for(std::size_t i=0;i<pmks.size();++i) {
            tests.check(bool(flags->at(i))==(tag_for(pmks[i],message)==tag),"WPA2 GPU/CPU MIC mismatch");
            ++compared;
        }
        // Reuse the engine after shrinking, with a different PMK and an altered tag.
        auto wrong=tag; wrong[0]^=1;
        auto negative=gpu.filter_wpa2({pmks.back()},context,message,wrong,3);
        tests.check(negative && negative->size()==1 && negative->at(0)==0,"WPA2 wrong MIC rejected");
    }
    const auto message=Bytes(99),tag=Bytes(16);
    tests.rejects([&]{gpu.filter_wpa2({},context,message,tag);},"empty MIC filter batch");
    tests.rejects([&]{gpu.filter_wpa2({Pmk{}},Bytes(75),message,tag);},"invalid MIC context");
    tests.rejects([&]{gpu.filter_wpa2({Pmk{}},context,Bytes(4097),tag);},"oversized MIC message");
    tests.rejects([&]{gpu.filter_wpa2({Pmk{}},context,message,Bytes(15));},"invalid MIC size");
    tests.rejects([&]{gpu.filter_wpa2({Pmk{}},context,message,tag,0);},"zero MIC work group");
    std::cout<<"PASS: "<<compared<<" WPA2 MIC filter comparisons against CPU crypto\n";
    for(const auto& ssid:{Bytes{},bytes("IEEE"),Bytes(32,255)}) {
        std::vector<Bytes> passwords;
        for(unsigned i=0;i<65;++i) {
            Bytes password(8+i%56);
            for(std::size_t j=0;j<password.size();++j) password[j]=static_cast<std::uint8_t>(i*71+j*13);
            passwords.push_back(std::move(password));
        }
        passwords.front()=passwords.back();
        std::vector<Pmk> expected;
        for(const auto& password:passwords) expected.push_back(reference(password,ssid,4096));
        const Bytes eapol(ssid.empty()?112:4096,37);
        const auto mic=tag_for(expected.back(),eapol);
        auto filtered=gpu.derive_wpa2(passwords,ssid,context,eapol,mic,256,64);
        tests.check(filtered && filtered->count==passwords.size(),"resident PMK batch count");
        std::size_t hit=0;
        for(std::size_t i=0;i<expected.size();++i) if(tag_for(expected[i],eapol)==mic) {
            tests.check(hit<filtered->hits.size() && filtered->hits[hit].index==i && filtered->hits[hit].pmk==expected[i],
                        "resident sparse match/index/full PMK against CPU");
            ++hit;
        }
        tests.check(hit==filtered->hits.size(),"resident filter has no extra matches");
        auto wrong=mic;wrong[0]^=1;
        auto none=gpu.derive_wpa2({passwords.back()},ssid,context,eapol,wrong,64,3);
        tests.check(none && none->count==1 && none->hits.empty(),"resident shrink and negative match");
        auto dense=gpu.derive_wpa2(std::vector<Bytes>(20,passwords.back()),ssid,context,eapol,mic);
        tests.check(dense && dense->count==20 && dense->hits.size()==20,"resident dense readback count");
        for(std::size_t i=0;i<dense->hits.size();++i)
            tests.check(dense->hits[i].index==i && dense->hits[i].pmk==expected.back(),"resident dense readback full PMK/order");
        auto external=gpu.filter_wpa2({Pmk{}},context,eapol,mic);
        tests.check(external && bool(external->at(0))==(tag_for(Pmk{},eapol)==mic),"external filter must not reuse resident keys");
    }
    const auto pass=bytes("resident-test-password"),ssid=bytes("resident-test");
    const auto expected=reference(pass,ssid,4096);
    const auto mic=tag_for(expected,message);
    std::vector<Bytes> large(262144,bytes("absent-password"));large.back()=pass;
    auto last=gpu.derive_wpa2(large,ssid,context,message,mic,256,256);
    tests.check(last && last->count==large.size() && last->hits.size()==1 &&
                last->hits[0].index==large.size()-1 && last->hits[0].pmk==expected,"resident maximum batch last match");
    tests.rejects([&]{gpu.derive_wpa2({},ssid,context,message,mic);},"empty resident batch");
    tests.rejects([&]{gpu.derive_wpa2({pass},ssid,Bytes(75),message,mic);},"invalid resident context");
    tests.rejects([&]{gpu.derive_wpa2({pass},ssid,context,Bytes(4097),mic);},"invalid resident EAPOL");
    tests.rejects([&]{gpu.derive_wpa2({pass},ssid,context,message,Bytes(15));},"invalid resident MIC");
    tests.rejects([&]{gpu.derive_wpa2({pass},ssid,context,message,mic,0);},"invalid resident chunk");
    std::cout<<"PASS: device-resident sparse/dense/empty results, buffer reuse and maximum-batch PMKs against CPU crypto\n";
}
void gpu_tests(wifi::Gpu& gpu,Tests& tests,wifi::Backend backend) {
    using namespace wifi;
    auto compare=[&](const std::vector<Bytes>& passwords,const Bytes& ssid,unsigned iterations,unsigned chunk,std::size_t group) {
        auto got=gpu.derive(passwords,ssid,iterations,chunk,group);
        tests.check(got.pmks.size()==passwords.size(),"output count");
        const auto dispatches=passwords.size()>4096 ?
            2+iterations-1 : 2+(iterations-1+chunk-1)/chunk;
        tests.check(got.submissions>=2 && got.submissions<=dispatches,"bounded GPU submission count");
        tests.check(got.max_submission_ms>=got.max_launch_ms && got.gpu_ms>=got.max_submission_ms,
                    "GPU timing partitions agree");
        for(std::size_t i=0;i<passwords.size();++i)
            tests.check(got.pmks[i]==reference(passwords[i],ssid,iterations),
                "GPU/CPU reference mismatch at index "+std::to_string(i)+" iterations="+std::to_string(iterations)+
                " chunk="+std::to_string(chunk)+" SSID bytes="+std::to_string(ssid.size()));
    };
    compare({bytes("password")},bytes("IEEE"),4096,128,64);
    std::vector<Bytes> lengths;
    for(std::size_t n=0;n<=63;++n) {
        Bytes p(n); for(std::size_t i=0;i<n;++i) p[i]=static_cast<std::uint8_t>(n*17+i*29);
        lengths.push_back(p);
    }
    // Exercise every supported byte length, non-ASCII bytes, both output blocks,
    // non-aligned SSIDs and the iterations-1 remainder after U1 initialization.
    for(unsigned n:{0U,1U,3U,4U,31U,32U}) {
        Bytes ssid(n); for(unsigned i=0;i<n;++i) ssid[i]=static_cast<std::uint8_t>(i*19);
        compare(lengths,ssid,4096,255,64);
    }
    // Change the batch maximum across every packed-key word boundary, then
    // shrink to an empty key on the same engine. Mixed shorter keys must keep
    // their zero padding even when the previous batch used a wider stride.
    if(backend==Backend::Vulkan || backend==Backend::OpenCl) {
        for(std::size_t n=64;n-- >0;)
            compare({lengths[n],Bytes(n/2,0),Bytes{}},Bytes{0,255},17,8,3);
    }
    const std::vector<Bytes> few={bytes("12345678"),bytes(std::string(63,'x')),Bytes{'a',0,'b',255}};
    // Exhaust small loop/chunk combinations against the independent reference:
    // rotated HMAC pad state must restart correctly at every dispatch boundary.
    for(unsigned rounds=1;rounds<=9;++rounds)
        for(unsigned chunk=1;chunk<=5;++chunk)
            compare(few,Bytes{0,1,255},rounds,chunk,3);
    for(unsigned rounds:{1U,2U,17U,128U,129U,256U,257U,4096U})
        for(unsigned chunk:{1U,17U,256U}) {
            if(rounds==4096 && chunk==1) continue; // avoid thousands of tiny launches
            compare(few,bytes("salt"),rounds,chunk,64);
        }
    for(std::size_t count:{63U,65U,129U,4096U}) {
        std::vector<Bytes> p;
        for(std::size_t i=0;i<count;++i) p.push_back(bytes("testpass-"+std::to_string(i)));
        compare(p,bytes("SSID"),17,8,64);
    }
    if(backend==Backend::Vulkan || backend==Backend::OpenCl) {
        for(unsigned chunk:{257U,511U,512U,1024U})
            compare(few,bytes("Long chunks"),4096,chunk,64);
        std::vector<Bytes> wider(4097,bytes("long-chunk-password"));
        wider.front()=Bytes(63,255); wider.back()=Bytes{};
        compare(wider,Bytes{0,255},1025,1024,128);
    }
    {
        for(std::size_t count:{4097U,8193U,65536U,262144U}) {
            std::vector<Bytes> p;
            for(std::size_t i=0;i<count;++i) p.push_back(bytes("large-batch-"+std::to_string(i)));
            compare(p,bytes("SSID"),17,8,128);
        }
    }
    for(std::size_t group:{1U,3U,32U,128U,256U}) compare(few,bytes("SSID"),129,64,group);
    // Probe, grouped dispatches, exact group boundaries and short final chunks.
    for(unsigned rounds:{32U,33U,34U,64U,65U,66U,128U,129U,130U,136U,137U,138U}) compare(few,bytes("Grouped"),rounds,8,3);
    // Full production workload at the original 4096 capacity, then shrink on the same
    // engine. Every PMK is independently checked, including both output blocks.
    std::vector<Bytes> full;
    for(unsigned i=0;i<4096;++i) full.push_back(bytes("full-batch-"+std::to_string(i)));
    compare(full,Bytes(32,255),4096,128,128);
    // Reuse one backend repeatedly with different inputs and buffer sizes.
    compare({bytes("password")},Bytes(32,255),2,1,64);
    compare({Bytes(63,255),Bytes(8,0)},Bytes{},4096,128,64);
    const auto pass=bytes("password"),ssid=bytes("SSID");
    tests.rejects([&]{gpu.derive({},ssid);},"empty GPU batch");
    tests.rejects([&]{gpu.derive(std::vector<Bytes>(gpu_batch_limit(backend)+1,pass),ssid);},"large GPU batch");
    tests.rejects([&]{gpu.derive({Bytes(64,1)},ssid);},"long GPU password");
    tests.rejects([&]{gpu.derive({pass},Bytes(33,1));},"long GPU SSID");
    tests.rejects([&]{gpu.derive({pass},ssid,0);},"zero GPU iterations");
    tests.rejects([&]{gpu.derive({pass},ssid,4097);},"large GPU iterations");
    tests.rejects([&]{gpu.derive({pass},ssid,1,0);},"zero GPU chunk");
    tests.rejects([&]{gpu.derive({pass},ssid,1,gpu_chunk_limit(backend)+1);},"large GPU chunk");
    tests.rejects([&]{gpu.derive({pass},ssid,1,128,0);},"zero work group");
    tests.rejects([&]{gpu.derive({pass},ssid,1,128,65536);},"large work group");
    gpu_filter_tests(gpu,tests);
}
void print_device(std::size_t index,const wifi::Device& d) {
    if(d.software) std::cout<<"SOFTWARE VULKAN: CPU execution; not GPU acceleration\n";
    std::cout<<"Device "<<index<<": "<<d.name<<" ("<<d.vendor<<")\n"
        <<"  driver="<<d.driver<<" runtime="<<d.version<<" language="<<d.language<<'\n'
        <<"  compute_units="<<(d.compute_units ? std::to_string(d.compute_units) : "unavailable")<<" memory_MiB="<<d.memory/(1024*1024)
        <<(d.language=="SPIR-V 1.0" ? " max_storage_buffer_MiB=" : " max_allocation_MiB=")<<d.max_allocation/(1024*1024)<<" max_work_group="<<d.max_work_group<<'\n';
}
void benchmark(wifi::Gpu* gpu,wifi::Cpu* cpu,const Options& o) {
    using namespace wifi;
    std::vector<Bytes> candidates;
    for(std::size_t i=0;i<o.batch;++i) candidates.push_back(bytes("benchmark-pass-"+std::to_string(i)));
    const auto ssid=bytes("wifi-gpu-benchmark");
    std::map<std::size_t,Pmk> expected;
    for(auto i:{std::size_t{0},o.batch/2,o.batch-1}) expected[i]=reference(candidates[i],ssid,4096);
    auto verify=[&](const Batch& b) { for(auto& pair:expected) require(b.pmks.at(pair.first)==pair.second,"Benchmark verification failed"); };
    std::cout<<"PBKDF2-HMAC-SHA1 only; 4096 iterations, 32 bytes, one SSID. No MIC/PMKID checks.\n"
        <<"batch="<<o.batch<<" repeats="<<o.repeats;
    if(gpu) std::cout<<" chunk="<<o.chunk<<" group="<<o.group;
    else std::cout<<" threads="<<cpu->threads();
    std::cout<<'\n';
    auto derive=[&]{return gpu?gpu->derive(candidates,ssid,4096,o.chunk,o.group):cpu->derive(candidates,ssid);};
    auto warmup=derive(); verify(warmup);
    std::cout<<"warmup_ms="<<warmup.wall_ms<<'\n';
    std::vector<double> rates;
    for(std::size_t repeat=0;repeat<o.repeats;++repeat) {
        auto result=derive(); verify(result);
        const auto rate=1000.0*static_cast<double>(o.batch)/result.wall_ms;
        rates.push_back(rate);
        std::cout<<"sample="<<repeat+1<<" pmk_per_second="<<rate<<" batch_wall_ms="<<result.wall_ms;
        if(gpu) std::cout<<" gpu_kernel_ms="<<result.gpu_ms<<" max_launch_ms="<<result.max_launch_ms
            <<" max_submission_ms="<<result.max_submission_ms<<" submissions="<<result.submissions;
        std::cout<<" checked_pmks="<<expected.size()<<'\n';
    }
    std::sort(rates.begin(),rates.end());
    double median=rates[rates.size()/2];
    if(rates.size()%2==0) median=(median+rates[rates.size()/2-1])/2;
    std::cout<<"median_pmk_per_second="<<median<<" min="<<rates.front()<<" max="<<rates.back()<<'\n';
}
}

int run_main(int argc,char** argv) {
    try {
        if(argc>1 && (std::string(argv[1])=="recover" || std::string(argv[1])=="inspect"))
            return wifi::recovery_cli(argc,argv);
        const auto o=options(argc,argv);
        std::cout<<std::fixed<<std::setprecision(3);
        if(o.command=="help" || o.command=="--help") {
            std::cout<<"wifi-gpu "<<version<<" - HC22000 recovery for Windows/Linux, CPU/OpenCL/Vulkan/HIP\n"
                <<"  devices [--backend opencl|vulkan|hip]\n  selftest [--cpu-only | --device N] [--kernel PATH]\n"
                <<"  benchmark [--cpu [--threads 1..64] | --device N] [--batch N] [--group 1..256]\n"
                <<"            [--chunk 1..1024 (OpenCL/Vulkan) / 1..256 (HIP)] [--repeats 1..10] [--kernel PATH]\n"
                <<"  --version\n--allow-software-vulkan explicitly enables CPU Vulkan devices for testing.\nGPU indices start at 0 within each backend. Default: "<<wifi::backend_name(wifi::default_backend())<<".\n  selftest, benchmark and recover accept --backend opencl|vulkan|hip.\n  HIP requires a Linux build with ROCm 7.2.\n";
            wifi::recovery_help();
            return 0;
        }
        if(o.command=="--version") { std::cout<<version<<'\n'; return 0; }
        if(o.command=="devices") {
            const auto devices=wifi::Gpu::devices(o.backend,o.allow_software);
            require(!devices.empty(),std::string("No ")+wifi::backend_name(o.backend)+" GPU detected");
            for(std::size_t i=0;i<devices.size();++i) print_device(i,devices[i]);
            return 0;
        }
        Tests tests;
        if(o.command=="selftest") { cpu_tests(tests); auto n=wifi::recovery_unit_tests(); std::cout<<"PASS: "<<n<<" recovery crypto/parser/mask checks\n"; }
        if(o.cpu_only) {
            if(o.command=="benchmark") { wifi::Cpu cpu(o.threads); std::cout<<"CPU "<<wifi::cpu_provider()<<"; threads="<<cpu.threads()<<"; GPU runtimes disabled\n"; benchmark(nullptr,&cpu,o); }
            else std::cout<<"PASS: "<<tests.passed<<" CPU checks; GPU was not tested\n";
            return 0;
        }
        const auto start=std::chrono::steady_clock::now();
        wifi::Gpu gpu(o.device,source(o),o.backend,o.allow_software);
        auto startup=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        print_device(o.device,gpu.info());
        std::cout<<"startup_ms="<<startup<<" (includes runtime/kernel setup; driver cache may be warm)\n";
        if(o.command=="selftest") {
            gpu_tests(gpu,tests,o.backend);
            std::cout<<"PASS: "<<tests.passed<<" CPU/GPU checks; full 32-byte PMKs compared against "<<wifi::cpu_provider()<<"\n";
        } else benchmark(&gpu,nullptr,o);
        return 0;
    } catch(const std::exception& error) { std::cerr<<"error: "<<error.what()<<'\n'; return 1; }
}

#ifdef _WIN32
int main() {
    // Windows CRT narrow argv may lose Unicode paths. Parse the original wide
    // command line and use UTF-8 consistently for recovery options and filenames.
    int argc=0; auto wide=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!wide) { std::cerr<<"error: cannot parse Windows command line\n"; return 1; }
    std::vector<std::string> strings;
    for(int i=0;i<argc;++i) {
        auto length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,nullptr,0,nullptr,nullptr);
        if(!length) { LocalFree(wide); std::cerr<<"error: invalid Unicode argument\n"; return 1; }
        std::string value(static_cast<std::size_t>(length),'\0');
        WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,value.data(),length,nullptr,nullptr);
        value.pop_back(); strings.push_back(std::move(value));
    }
    LocalFree(wide);
    std::vector<char*> arguments;
    for(auto& text:strings) arguments.push_back(text.data());
    return run_main(argc,arguments.data());
}

#else
int main(int argc,char** argv) { return run_main(argc,argv); }
#endif
