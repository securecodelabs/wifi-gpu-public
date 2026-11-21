#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "platform.hpp"
#ifdef _WIN32
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#endif
#include "recovery.hpp"
#include "cpu.hpp"
#include "hc22000.hpp"
#include "candidates.hpp"
#include "progress.hpp"
#include "hardware.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace wifi {
namespace {
using Clock=std::chrono::steady_clock;
std::atomic<bool> cancelled{false};
#ifdef _WIN32
BOOL WINAPI control(DWORD event) {
    if(event==CTRL_C_EVENT || event==CTRL_BREAK_EVENT) { cancelled=true; return TRUE; }
    return FALSE;
}
struct Control {
    Control() {
        cancelled=false;
        // Some launchers inherit the process attribute that ignores Ctrl+C.
        // Clear it for this process before installing our checkpointing handler.
        if(!SetConsoleCtrlHandler(nullptr,FALSE) || !SetConsoleCtrlHandler(control,TRUE))
            throw std::runtime_error("Cannot install Ctrl+C handler");
    }
    ~Control() { SetConsoleCtrlHandler(control,FALSE); }
};
#else
static_assert(std::atomic<bool>::is_always_lock_free,"Signal handler requires lock-free cancellation");
void control(int) { cancelled.store(true,std::memory_order_relaxed); }
struct Control {
    struct sigaction previous_int{},previous_term{};
    Control() {
        cancelled=false;
        struct sigaction action{}; action.sa_handler=control; sigemptyset(&action.sa_mask);
        if(sigaction(SIGINT,&action,&previous_int)<0) throw std::runtime_error("Cannot install SIGINT handler");
        if(sigaction(SIGTERM,&action,&previous_term)<0) {
            sigaction(SIGINT,&previous_int,nullptr);
            throw std::runtime_error("Cannot install SIGTERM handler");
        }
    }
    ~Control() { sigaction(SIGINT,&previous_int,nullptr); sigaction(SIGTERM,&previous_term,nullptr); }
};
#endif
struct Config {
    std::string command,hashes,output,session,kernel;
    Backend backend=default_backend();
    CandidateConfig candidates;
    bool cpu=false,overwrite=false,resume=false,allow_software=false,activity_json=false;
    std::size_t device=0,batch=1024,group=64;
    unsigned chunk=256,runtime=0,status_every=1,threads=0;
    std::optional<unsigned> corrections;
    std::uint64_t skip=0,limit=0;
};
bool parse_flag(Config& c,std::string_view key) {
    if(key=="--allow-software-vulkan") { c.allow_software=true; return true; }
    if(key=="--cpu") { c.cpu=true; return true; }
    if(key=="--activity-json") { c.activity_json=true; return true; }
    if(key=="--overwrite") { c.overwrite=true; return true; }
    if(key=="--hex-wordlist") { c.candidates.hexadecimal=true; return true; }
    return false;
}
void parse_number(Config& c,const std::string& key,const std::string& value) {
    unsigned max=0;
    if(key=="--threads") max=64;
    else if(key=="--batch") max=static_cast<unsigned>(gpu_batch_limit(Backend::OpenCl));
    else if(key=="--group") max=256;
    else if(key=="--chunk") max=gpu_chunk_limit(Backend::Vulkan);
    else if(key=="--runtime") max=86400;
    else if(key=="--status-every") max=3600;
    else if(key=="--nonce-error-corrections") max=1024;
    else throw std::invalid_argument("Unknown option: "+key);
    auto n=unsigned_number(value);
    if(n>max || (n==0 && key!="--nonce-error-corrections" && key!="--runtime")) throw std::invalid_argument("Out-of-range value for "+key);
    if(key=="--threads") c.threads=static_cast<unsigned>(n);
    else if(key=="--batch") c.batch=static_cast<std::size_t>(n);
    else if(key=="--group") c.group=static_cast<std::size_t>(n);
    else if(key=="--chunk") c.chunk=static_cast<unsigned>(n);
    else if(key=="--runtime") c.runtime=static_cast<unsigned>(n);
    else if(key=="--status-every") c.status_every=static_cast<unsigned>(n);
    else c.corrections=static_cast<unsigned>(n);
}
void parse_value(Config& c,const std::string& key,const std::string& value) {
    if(key=="--hashes") c.hashes=value;
    else if(key=="--output") c.output=value;
    else if(key=="--session") c.session=value;
    else if(key=="--resume") { c.session=value; c.resume=true; }
    else if(key=="--backend") c.backend=parse_backend(value);
    else if(key=="--kernel") c.kernel=value;
    else if(key=="--wordlist") c.candidates.wordlists.push_back(value);
    else if(key=="--mask") c.candidates.masks.push_back(value);
    else if(key=="--mask-file") c.candidates.mask_files.push_back(value);
    else if(key=="--hybrid") c.candidates.hybrid=value;
    else if(key=="--charset1" || key=="--charset2" || key=="--charset3" || key=="--charset4") c.candidates.custom[static_cast<unsigned>(key.back()-'1')]=value;
    else if(key=="--device") c.device=static_cast<std::size_t>(unsigned_number(value));
    else if(key=="--skip") c.skip=unsigned_number(value);
    else if(key=="--limit") { c.limit=unsigned_number(value); }
    else parse_number(c,key,value);
}
void validate_source_options(const Config& c,const std::set<std::string,std::less<>>& used) {
    if(c.candidates.hexadecimal && c.candidates.wordlists.empty()) throw std::invalid_argument("--hex-wordlist requires --wordlist");
    if(used.count("--hybrid") && (c.candidates.wordlists.empty() || (c.candidates.masks.empty() && c.candidates.mask_files.empty()))) throw std::invalid_argument("--hybrid requires both a wordlist and a mask");
    if(c.command=="inspect") {
        for(const auto& key:used) if(key!="--hashes" && key!="--nonce-error-corrections") throw std::invalid_argument("Unsupported inspect option: "+key);
        if(!c.candidates.wordlists.empty() || !c.candidates.masks.empty() || !c.candidates.mask_files.empty()) throw std::invalid_argument("inspect does not accept candidate sources");
    }
}
void validate_backend_options(const Config& c,const std::set<std::string,std::less<>>& used) {
    if(c.cpu && (used.count("--backend") || used.count("--device") || used.count("--kernel") || used.count("--group") || used.count("--chunk"))) throw std::invalid_argument("GPU options cannot be combined with --cpu");
    if(used.count("--threads") && !c.cpu) throw std::invalid_argument("--threads requires --cpu");
    if(c.chunk>gpu_chunk_limit(c.backend)) throw std::invalid_argument("--chunk exceeds backend limit (OpenCL/Vulkan: 1024; HIP: 256)");
}
void apply_backend_defaults(Config& c,const std::set<std::string,std::less<>>& used) {
    if(c.allow_software && (c.cpu || c.backend!=Backend::Vulkan)) throw std::invalid_argument("--allow-software-vulkan requires Vulkan mode");
    if(!used.count("--batch") && !c.cpu && c.backend==Backend::Hip) c.batch=262144;
    if(c.batch>(c.cpu ? 4096 : gpu_batch_limit(c.backend)))
        throw std::invalid_argument("--batch exceeds backend limit (GPU: 262144; CPU: 4096)");
    if(c.hashes.empty()) throw std::invalid_argument("--hashes FILE is required");
}
Config parse(int argc,char** argv) {
    Config c; c.command=argv[1]; std::set<std::string,std::less<>> used;
    const std::set<std::string,std::less<>> multi={"--wordlist","--mask","--mask-file"};
    int i=2;
    while(i<argc) {
        std::string key=argv[i++];
        if(!multi.count(key) && !used.insert(key).second) throw std::invalid_argument("Duplicate option: "+key);
        if(parse_flag(c,key)) continue;
        if(i>=argc) throw std::invalid_argument("Missing value for "+key);
        std::string value=argv[i++];
        if(value.empty()) throw std::invalid_argument("Empty value for "+key);
        parse_value(c,key,value);
    }
    if(used.count("--resume") && used.count("--session")) throw std::invalid_argument("Use either --session or --resume");
    if(c.resume && (used.count("--skip") || c.overwrite)) throw std::invalid_argument("--resume cannot be combined with --skip or --overwrite");
    validate_backend_options(c,used);
    validate_source_options(c,used);
    apply_backend_defaults(c,used);
    return c;
}

std::string kernel_source(const std::string& override_path,Backend backend) {
    std::filesystem::path path;
    if(!override_path.empty()) path=absolute_path(override_path);
    else {
        path=executable_directory()/"kernels"/(backend==Backend::Vulkan ? "pbkdf2.spv" : "pbkdf2.cl");
    }
    Input file(path); return file.contents(1024*1024);
}
using Matches=std::map<std::string,Bytes,std::less<>>;
std::string session_text(const std::string& identity,std::uint64_t next,const Matches& matches) {
    std::string text="WIFI-GPU-SESSION\t1\njob\t"+identity+"\nnext\t"+std::to_string(next)+"\n";
    for(const auto& m:matches) text+="match\t"+m.first+"\t"+hex_bytes(m.second)+"\n";
    return text+"sha256\t"+hex_bytes(digest(Algorithm::Sha256,bytes(text)))+"\n";
}
void read_session(const std::string& value,const std::string& identity,std::uint64_t& next,Matches& matches,const std::vector<Record>& records) {
    auto marker=value.rfind("sha256\t");
    if(marker==std::string::npos || value.substr(marker+7)!=hex_bytes(digest(Algorithm::Sha256,bytes(value.substr(0,marker))))+"\n") throw std::invalid_argument("Session checksum failed");
    std::istringstream in(value.substr(0,marker)); std::string line;
    if(!std::getline(in,line) || line!="WIFI-GPU-SESSION\t1") throw std::invalid_argument("Unsupported session format");
    if(!std::getline(in,line) || line!="job\t"+identity) throw std::invalid_argument("Session inputs/settings differ; use the original hash file, wordlists and masks");
    if(!std::getline(in,line) || line.rfind("next\t",0)!=0) throw std::invalid_argument("Session missing candidate offset");
    next=unsigned_number(line.substr(5));
    while(std::getline(in,line)) {
        if(line.rfind("match\t",0)!=0 || line.size()<72 || line[70]!='\t') throw std::invalid_argument("Malformed session match");
        auto id=line.substr(6,64); auto password=unhex(line.substr(71));
        if(password.size()<8 || password.size()>63 || !matches.emplace(id,password).second) throw std::invalid_argument("Invalid/duplicate session match");
        auto record=std::find_if(records.begin(),records.end(),[&](const Record& r){return r.id==id;});
        if(record==records.end() || !record->verify(reference(password,record->ssid,4096))) throw std::invalid_argument("Session match failed independent password verification");
    }
}
std::string result_json(const std::string& identity,const std::string& status,std::uint64_t next,const Matches& matches,const std::vector<Record>& records,const Progress& progress,const std::string& hardware,const std::string& latest,std::uint64_t tested,std::optional<std::uint64_t> total) {
    std::string json="{\"schema\":1,\"job_id\":"+json_string(identity)+",\"version\":"+json_string(WIFI_GPU_VERSION)+",\"status\":"+json_string(status)+",\"next_candidate\":"+std::to_string(next)+",\"total_records\":"+std::to_string(records.size())+",\"recovered\":"+std::to_string(matches.size())+",\"matches\":[";
    bool first=true;
    for(const auto& record:records) {
        auto hit=matches.find(record.id); if(hit==matches.end()) continue;
        if(!first) json+=',';
        first=false;
        json+="{\"record_id\":"+json_string(record.id)+",\"line\":"+std::to_string(record.line)+",\"type\":"+std::to_string(record.type)+",\"ssid_hex\":"+json_string(hex_bytes(record.ssid))+",\"ssid_display\":"+json_string(display(record.ssid))+",\"bssid\":"+json_string(hex_bytes(record.ap))+",\"client\":"+json_string(hex_bytes(record.client))+",\"password_hex\":"+json_string(hex_bytes(hit->second))+",\"password_display\":"+json_string(display(hit->second))+",\"verified\":true,\"hash\":"+json_string(record.original)+"}";
    }
    return json+"],\"telemetry\":"+progress_json(progress)+",\"hardware\":"+hardware+",\"latest_candidate_hex\":"+(latest.empty()?"null":json_string(latest))+",\"tested_candidates\":"+std::to_string(tested)+",\"total_candidates\":"+(total?std::to_string(*total):"null")+"}\n";
}
bool same_path(const std::filesystem::path& a,const std::filesystem::path& b) {
    if(std::filesystem::exists(a) && std::filesystem::exists(b) && std::filesystem::equivalent(a,b)) return true;
#ifdef _WIN32
    auto x=std::filesystem::weakly_canonical(a).wstring(),y=std::filesystem::weakly_canonical(b).wstring();
    return CompareStringOrdinal(x.c_str(),static_cast<int>(x.size()),y.c_str(),static_cast<int>(y.size()),TRUE)==CSTR_EQUAL;
#else
    return std::filesystem::weakly_canonical(a)==std::filesystem::weakly_canonical(b);
#endif
}
// Owns one worker and its exception result. Destruction always joins, while
// wait() also propagates failures to the recovery/checkpoint error path.
class BackgroundTask {
public:
    BackgroundTask()=default;
    BackgroundTask(const BackgroundTask&)=delete;
    BackgroundTask& operator=(const BackgroundTask&)=delete;
    ~BackgroundTask() { if(worker.joinable()) worker.join(); }
    bool valid() const { return result.valid(); }
    void wait() {
        if(worker.joinable()) worker.join();
        if(result.valid()) result.get();
    }
    template<class Work> void start(Work&& work) {
        wait();
        std::packaged_task<void()> task(std::forward<Work>(work));
        auto next=task.get_future();
        worker=std::thread(std::move(task));
        result=std::move(next);
    }
private:
    std::thread worker;
    std::future<void> result;
};

class RecoveryError final:public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CheckpointError final:public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct CandidateBatch {
    std::vector<Bytes> passwords;
    std::uint64_t end=0;
    std::uint64_t rejected=0;
    bool eof=false;
};

// Only this producer advances the candidate cursor. Consumers commit batch.end
// after verification, never the speculative position of the prefetched batch.
class CandidatePipeline {
public:
    CandidatePipeline(const Config& config,Candidates& source,std::uint64_t begin,bool overlap)
        : c(config),candidates(source),start(begin),prefetch(overlap) {
        for(auto& batch:batches) batch.passwords.reserve(c.batch);
    }
    CandidatePipeline(const CandidatePipeline&)=delete;
    CandidatePipeline& operator=(const CandidatePipeline&)=delete;
    const CandidateBatch& next() {
        if(pending.valid()) pending.wait(); else prepare(batches[current]);
        return batches[current];
    }
    bool exhausted(const CandidateBatch& batch) const {
        auto space=candidates.keyspace();
        return batch.eof || (space.has_value() && batch.end==*space);
    }
    void prepare_next(bool eof) {
        if(!prefetch || eof || (c.limit && batches[current].end-start>=c.limit)) return;
        auto* next=&batches[current^1];
        pending.start([this,next] { prepare(*next); });
    }
    void advance() { if(prefetch) current^=1; }
private:
    void prepare(CandidateBatch& batch) {
        auto& passwords=batch.passwords;
        batch.rejected=0; batch.eof=false;
        std::size_t count=0;
        const auto before=candidates.position();
        while(count<c.batch && candidates.position()-before<std::max<std::size_t>(65536,c.batch) && (!c.limit || candidates.position()-start<c.limit) && !cancelled) {
            if(count==passwords.size()) passwords.emplace_back();
            auto& next=passwords[count];
            if(!candidates.next(next)) { batch.eof=true; break; }
            if(next.size()<8 || next.size()>63) { ++batch.rejected; continue; }
            ++count;
        }
        passwords.resize(count); batch.end=candidates.position();
    }
    const Config& c;
    Candidates& candidates;
    const std::uint64_t start;
    const bool prefetch;
    std::size_t current=0;
    std::array<CandidateBatch,2> batches;
    // Declared last: destruction joins the producer before its buffers die.
    BackgroundTask pending;
};

Candidates::Scan source_scan(const Config& c) {
    if(c.activity_json) std::cout<<R"json(WIFI_ACTIVITY {"stage":"loading_sources","batch_size":)json"<<c.batch
        <<R"json(,"latest_candidate_hex":null})json"<<'\n'<<std::flush;
    std::cerr<<"Loading candidate sources and hashing inputs for session identity...\n";
    if(!c.activity_json) return {};
    return [&c](std::uint64_t scanned,std::uint64_t total,double elapsed) {
        std::cout<<R"json(WIFI_ACTIVITY {"stage":"loading_sources","batch_size":)json"<<c.batch
            <<R"json(,"latest_candidate_hex":null,"wordlist_scan":{"bytes_scanned":)json"<<scanned
            <<R"json(,"total_bytes":)json"<<total<<R"json(,"elapsed_seconds":)json"<<elapsed
            <<R"json(,"cpu_threads":1}})json"<<'\n'<<std::flush;
    };
}

void validate_output_paths(const Config& c,const std::vector<std::filesystem::path>& inputs) {
    std::vector<std::filesystem::path> outputs;
    for(const auto& name:{c.output,c.session}) {
        if(name.empty()) continue;
        auto path=absolute_path(name); auto lock=path; lock+=".lock";
        for(const auto& input:inputs)
            if(same_path(path,input) || same_path(lock,input)) throw std::invalid_argument("Output/session would overwrite an input file");
        for(const auto& output:outputs)
            if(same_path(path,output) || same_path(lock,output)) throw std::invalid_argument("Output and session paths overlap");
        outputs.push_back(path); outputs.push_back(lock);
    }
}

struct BatchVerification {
    bool complete=true;
    std::optional<std::size_t> tested;
    std::optional<std::size_t> furthest_tested;
    void mark_tested(std::size_t index) {
        tested=index;
        furthest_tested=std::max(furthest_tested.value_or(0),index);
    }
};

bool exact_wpa2(const Record& record) {
    return record.type==2 && record.key_version==2 && record.contexts.size()==1;
}

struct RecoveryProgress {
    std::uint64_t committed=0;
    std::uint64_t start=0;
    std::uint64_t valid_start=0;
    std::uint64_t run_started_at_ms=0;
    std::uint64_t rejected=0;
    std::uint64_t valid=0;
    std::uint64_t derivations=0;
    std::uint64_t tested_count=0;
    std::optional<std::uint64_t> valid_total;
    std::optional<Clock::time_point> started;
    Clock::time_point reported{};
};

class RecoveryRun {
public:
    RecoveryRun(const Config& config,const std::vector<Record>& input,Input* hashes,const std::string& stdin_hash)
        : c(config),records(input),candidates(c.candidates,source_scan(c)) {
        progress.committed=c.skip;
        auto paths=candidates.input_paths();
        if(hashes) paths.push_back(hashes->path);
        validate_output_paths(c,paths);
        std::string configuration="wifi-gpu-search-v1\n"+(hashes?hashes->sha256():stdin_hash)+"\n"+candidates.identity()+"\nnonce="+(c.corrections.has_value()?std::to_string(*c.corrections):"auto")+"\n";
        identity=hex_bytes(digest(Algorithm::Sha256,bytes(configuration)));
        restore_session();
        open_output();
        activity("restoring_position");
        candidates.seek(progress.committed);
        progress.start=progress.committed;
        progress.valid_start=candidates.valid_position();
        progress.valid_total=candidates.valid_total();
        if(progress.valid_total.has_value() && !c.resume) *progress.valid_total-=progress.valid_start;
        progress.run_started_at_ms=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        for(std::size_t i=0;i<records.size();++i) groups[records[i].ssid].push_back(i);
    }
    int run(std::size_t duplicates) {
        report_inputs(duplicates);
        if(matches.size()==records.size()) {
            save("recovered");
            std::cout<<"All records already verified in the session.\n";
            return 0;
        }
        initialize_device();
        progress.started=Clock::now(); progress.reported=*progress.started;
        save("running");
        activity("recovering");
        CandidatePipeline pipeline(c,candidates,progress.start,gpu!=nullptr);
        try {
            run_batches(pipeline);
            save(status); report();
        } catch(...) {
            save_error();
            throw;
        }
        return exit_code();
    }
private:
    using Group=std::pair<const Bytes,std::vector<std::size_t>>;
    void activity(const std::string& stage) const {
        if(c.activity_json) std::cout<<R"json(WIFI_ACTIVITY {"stage":)json"<<json_string(stage)
            <<R"json(,"batch_size":)json"<<c.batch<<R"json(,"latest_candidate_hex":)json"<<(latest.empty()?"null":json_string(latest))<<"}\n"<<std::flush;
    }
    void restore_session() {
        if(c.session.empty()) return;
        auto path=absolute_path(c.session);
        // Acquire the sidecar lock before reading state, preventing stale resumes.
        session=std::make_unique<Snapshot>(path,c.resume || c.overwrite);
        if(c.resume) { Input saved(path); read_session(saved.contents(32*1024*1024),identity,progress.committed,matches,records); }
    }
    void open_output() {
        if(c.output.empty()) return;
        auto path=absolute_path(c.output);
        output=std::make_unique<Snapshot>(path,c.overwrite || c.resume);
        if(!c.resume || !std::filesystem::exists(path)) return;
        Input saved(path); Bytes prefix; bool large=false;
        saved.line(prefix,200,large);
        auto expected=R"json({"schema":1,"job_id":)json"+json_string(identity)+",";
        std::string value(prefix.begin(),prefix.end());
        if(value.rfind(expected,0)!=0) throw std::invalid_argument("Existing output belongs to a different job; choose another --output path");
    }
    double elapsed() const {
        return progress.started?std::chrono::duration<double>(Clock::now()-*progress.started).count():0.0;
    }
    std::string output_text(const std::string& state) const {
        const auto tested=(c.resume?progress.valid_start:0)+progress.tested_count;
        auto telemetry=search_progress(progress.committed,progress.start,progress.valid,elapsed(),c.limit,c.runtime,candidates.keyspace(),
            progress.run_started_at_ms,progress.valid_total,tested);
        return result_json(identity,state,progress.committed,matches,records,telemetry,hardware,latest,tested,progress.valid_total);
    }
    void save(const std::string& state,bool asynchronous=false) try {
        // One immutable verified snapshot at a time. Join before another write
        // or a terminal checkpoint; the writer never reads live recovery state.
        if(checkpoint.valid()) checkpoint.wait();
        auto session_data=session?session_text(identity,progress.committed,matches):std::string{};
        auto output_data=output?output_text(state):std::string{};
        auto write=[s=session.get(),o=output.get(),
                    session_data=std::move(session_data),output_data=std::move(output_data)] {
            if(s) s->write(session_data);
            if(o) o->write(output_data);
        };
        if(asynchronous && (session || output)) checkpoint.start(std::move(write));
        else write();
    } catch(const std::bad_alloc&) {
        throw;
    } catch(const std::exception& e) {
        // Normalize serialization, worker and filesystem failures at the
        // checkpoint boundary, preserving their original diagnostic text.
        throw CheckpointError(e.what());
    }
    void save_error() {
        // Failed/interrupted batches replay on resume; independently verified
        // hits survive, but speculative candidate offsets never become durable.
        try { save("error"); }
        catch(const CheckpointError& e) { std::cerr<<"Could not save recovery state: "<<e.what()<<'\n'; }
        catch(const std::bad_alloc&) { std::cerr<<"Not enough memory to save recovery state\n"; }
    }
    void report_inputs(std::size_t duplicates) const {
        std::cerr<<"records="<<records.size()<<" duplicates="<<duplicates<<" SSIDs="<<groups.size()<<" start="<<progress.committed;
        if(auto space=candidates.keyspace(); space.has_value()) std::cerr<<" keyspace="<<*space;
        std::cerr<<" backend="<<(c.cpu?std::string("CPU ")+cpu_provider():std::string(backend_name(c.backend))+" PBKDF2 + CPU MIC/PMKID verification")<<'\n';
    }
    void initialize_device() {
        activity("initializing_device");
        if(c.cpu) {
            cpu=std::make_unique<Cpu>(c.threads);
            hardware=cpu_hardware_json(cpu->threads());
            std::cerr<<"CPU threads="<<cpu->threads()<<"; GPU runtimes disabled\n";
            return;
        }
        gpu=std::make_unique<Gpu>(c.device,kernel_source(c.kernel,c.backend),c.backend,c.allow_software);
        if(auto test=gpu->derive({bytes("password")},bytes("IEEE"),4096,c.chunk,c.group);
           test.pmks[0]!=reference(bytes("password"),bytes("IEEE"),4096)) throw RecoveryError("GPU startup self-test failed");
        if(gpu->info().software) std::cerr<<"SOFTWARE VULKAN: CPU execution; not GPU acceleration\n";
        hardware=gpu_hardware_json(c.backend,c.device,gpu->info());
        std::cerr<<"GPU="<<gpu->info().name<<"; startup self-test passed\n";
    }
    void report() const {
        auto seconds=elapsed();
        std::cerr<<"status="<<status<<" next="<<progress.committed<<" candidates="<<progress.valid<<" rejected="<<progress.rejected
            <<" recovered="<<matches.size()<<'/'<<records.size()<<" derivations="<<progress.derivations
            <<" elapsed_s="<<seconds<<" derivations_per_second="<<(seconds>0?static_cast<double>(progress.derivations)/seconds:0)<<'\n';
    }
    void report_if_due() {
        if(std::chrono::duration<double>(Clock::now()-progress.reported).count()<c.status_every) return;
        report(); progress.reported=Clock::now();
    }
    void report_filter() {
        if(filter_reported) return;
        std::cerr<<"GPU WPA2 EAPOL MIC filter enabled; hits independently CPU verified\n";
        filter_reported=true;
    }
    bool stop_requested() {
        if(cancelled) { status="interrupted"; return true; }
        if(c.runtime && elapsed()>=c.runtime) { status="runtime_limit"; return true; }
        if(c.limit && progress.committed-progress.start>=c.limit) { status="candidate_limit"; return true; }
        return false;
    }
    void run_batches(CandidatePipeline& pipeline) {
        while(matches.size()<records.size()) {
            if(stop_requested()) return;
            const auto& batch=pipeline.next();
            progress.rejected+=batch.rejected;
            if(cancelled) { status="interrupted"; return; }
            const bool eof=pipeline.exhausted(batch);
            pipeline.prepare_next(eof);
            auto verification=verify_batch(batch.passwords);
            if(verification.tested.has_value()) { latest=hex_bytes(batch.passwords[*verification.tested]); activity("recovering"); }
            if(!verification.complete) { status="interrupted"; return; }
            progress.committed=batch.end; progress.valid+=batch.passwords.size();
            if(verification.furthest_tested.has_value()) progress.tested_count+=*verification.furthest_tested+1;
            status="running";
            if(matches.size()==records.size()) { status="recovered"; save(status); return; }
            if(eof) { status="exhausted"; save(status); return; }
            save(status,gpu!=nullptr);
            report_if_due();
            pipeline.advance();
        }
    }
    bool group_recovered(const Group& group) const {
        return std::all_of(group.second.begin(),group.second.end(),[this](auto i){return matches.count(records[i].id)!=0;});
    }
    BatchVerification verify_batch(const std::vector<Bytes>& passwords) {
        BatchVerification verification;
        if(passwords.empty()) return verification;
        for(const auto& group:groups) {
            if(cancelled) { verification.complete=false; return verification; }
            if(group_recovered(group)) continue;
            if(!verify_device_group(group,passwords,verification)) verify_shared_group(group,passwords,verification);
            if(!verification.complete) break;
        }
        return verification;
    }
    bool verify_device_group(const Group& group,const std::vector<Bytes>& passwords,BatchVerification& verification) {
        // A single exact-nonce WPA2 record can be filtered without downloading
        // all PMKs. Unsupported backends/record combinations use shared PMKs.
        if(!gpu || group.second.size()!=1) return false;
        const auto& record=records[group.second.front()];
        if(!exact_wpa2(record)) return false;
        auto result=gpu->derive_wpa2(passwords,group.first,record.contexts[0],record.eapol,record.tag,c.chunk,c.group);
        if(!result) return false;
        if(result->count!=passwords.size()) throw RecoveryError("GPU filtered batch count mismatch");
        progress.derivations+=result->count;
        report_filter();
        if(cancelled) { verification.complete=false; return true; }
        auto tested=passwords.size()-1;
        if(!result->hits.empty()) {
            const auto& hit=result->hits.front();
            if(hit.index>=passwords.size()) throw RecoveryError("GPU filtered batch index out of range");
            if(!record.verify(hit.pmk)) throw RecoveryError("GPU MIC filter hit failed CPU verification");
            tested=hit.index; record_match(record,passwords[hit.index],hit.pmk);
        }
        verification.mark_tested(tested);
        return true;
    }
    void verify_shared_group(const Group& group,const std::vector<Bytes>& passwords,BatchVerification& verification) {
        auto pmks=gpu?gpu->derive(passwords,group.first,4096,c.chunk,c.group).pmks
                     :cpu->derive(passwords,group.first,4096,&cancelled).pmks;
        if(!gpu && pmks.empty()) { verification.complete=false; return; }
        progress.derivations+=pmks.size();
        for(auto index:group.second) {
            const auto& record=records[index];
            if(matches.count(record.id)) continue;
            verify_record(record,passwords,pmks,verification);
            if(!verification.complete) return;
        }
    }
    static bool filter_matches(const Record& record,const Pmk& pmk,const std::optional<Bytes>& filtered,std::size_t index) {
        if(!filtered) return record.verify(pmk);
        if(!filtered->at(index)) return false;
        if(!record.verify(pmk)) throw RecoveryError("GPU MIC filter hit failed CPU verification");
        return true;
    }
    void verify_record(const Record& record,const std::vector<Bytes>& passwords,const std::vector<Pmk>& pmks,BatchVerification& verification) {
        std::optional<Bytes> filtered;
        if(gpu && exact_wpa2(record)) filtered=gpu->filter_wpa2(pmks,record.contexts[0],record.eapol,record.tag,c.group);
        if(filtered) report_filter();
        for(std::size_t i=0;i<pmks.size();++i) {
            if(cancelled) { verification.complete=false; return; }
            verification.mark_tested(i);
            if(!filter_matches(record,pmks[i],filtered,i)) continue;
            record_match(record,passwords[i],pmks[i]);
            return;
        }
    }
    void record_match(const Record& record,const Bytes& password,const Pmk& pmk) {
        if(auto independent=reference(password,record.ssid,4096);
           independent!=pmk || !record.verify(independent)) throw RecoveryError("GPU hit failed independent CPU verification");
        matches.try_emplace(record.id,password);
        std::cout<<"FOUND line="<<record.line<<" ssid="<<json_string(display(record.ssid))<<" bssid="<<hex_bytes(record.ap)
            <<" password="<<json_string(display(password))<<" password_hex="<<hex_bytes(password)<<'\n'<<std::flush;
    }
    int exit_code() const {
        if(status=="recovered") return 0;
        if(status=="interrupted") return 3;
        if(status=="exhausted") return 2;
        return 4;
    }
    const Config& c;
    const std::vector<Record>& records;
    Candidates candidates;
    std::string identity;
    std::string latest;
    std::string hardware="null";
    std::string status="exhausted";
    Matches matches;
    RecoveryProgress progress;
    std::map<Bytes,std::vector<std::size_t>> groups;
    std::unique_ptr<Snapshot> session;
    std::unique_ptr<Snapshot> output;
    std::unique_ptr<Gpu> gpu;
    std::unique_ptr<Cpu> cpu;
    bool filter_reported=false;
    // Declared last: join the writer before the snapshots and their locks die.
    BackgroundTask checkpoint;
};

int recover(const Config& c,const std::vector<Record>& records,Input* hashes,const std::string& stdin_hash,std::size_t duplicates) {
    Control controls;
    RecoveryRun run(c,records,hashes,stdin_hash);
    return run.run(duplicates);
}
}
void recovery_help() {
    std::cout<<"\nHC22000 commands (WPA record types 01..04):\n"
        <<"  inspect --hashes FILE [--nonce-error-corrections N]\n"
        <<"  recover --hashes FILE --wordlist WORDS [--wordlist MORE_WORDS]\n"
        <<"  recover --hashes FILE --mask 'prefix?d?d?d?d'\n"
        <<"  recover --hashes FILE --wordlist WORDS --mask '?d?d' [--hybrid append|prepend]\n"
        <<"  --hashes - reads HC22000 from stdin until EOF (inspect and recover).\n"
        <<"Candidate options: --mask (repeatable), --mask-file FILE (repeatable),\n"
        <<"  --charset1 SET .. --charset4 SET, --hex-wordlist, --skip N, --limit N (0=unlimited).\n"
        <<"Mask tokens: ?l ?u ?d ?s ?a ?b ?h ?H ?1..?4; ?? is literal ?.\n"
        <<"Session/output: --output results.json, --session run.session,\n"
        <<"  --resume run.session (repeat original input options), --overwrite.\n"
        <<"Runtime: --cpu OR --backend opencl|vulkan|hip (default "<<backend_name(default_backend())<<"), --device N, --batch N (GPU: 1..262144; CPU: 1..4096), --group 1..256,\n"
        <<"  --threads 1..64 (CPU only; default logical CPUs, capped at 64),\n"
        <<"  --chunk 1..1024 (OpenCL/Vulkan) / 1..256 (HIP), --kernel PATH, --runtime SECONDS (0=unlimited), --status-every SECONDS,\n"
        <<"  --nonce-error-corrections 0..1024 (default: flags select 0 or 8).\n"
        <<"  --activity-json emits WIFI_ACTIVITY JSON lines with stages and the latest tested candidate (hex).\n"
        <<"Testing: --allow-software-vulkan permits CPU Vulkan devices and labels them explicitly.\n"
        <<"Recovery exit codes: 0 all recovered, 1 error, 2 exhausted, 3 interrupted, 4 limit.\n";
}
int recovery_cli(int argc,char** argv) {
    if(argc==3 && std::string(argv[2])=="--help") { recovery_help(); return 0; }
    auto c=parse(argc,argv); std::size_t duplicates=0;
    std::unique_ptr<Input> file; std::string stdin_hash;
    std::vector<Record> records;
    if(c.hashes=="-") {
#ifdef _WIN32
        // Preserve CRLF and Ctrl+Z bytes for validation and session identity.
        if(_setmode(_fileno(stdin),_O_BINARY)==-1) throw std::ios_base::failure("Cannot set stdin binary mode");
#endif
        records=read_records(std::cin,duplicates,stdin_hash);
    } else {
        file=std::make_unique<Input>(absolute_path(c.hashes));
        records=read_records(*file,duplicates);
    }
    for(auto& r:records) r.prepare(c.corrections);
    if(c.command=="inspect") {
        std::cout<<"records="<<records.size()<<" duplicates="<<duplicates<<'\n';
        for(const auto& r:records) std::cout<<"line="<<r.line<<" type="<<r.type<<" key_version="<<r.key_version<<" pair="<<r.message_pair
            <<" ssid="<<json_string(display(r.ssid))<<" ssid_hex="<<hex_bytes(r.ssid)<<" bssid="<<hex_bytes(r.ap)<<" nonce_variants="<<r.contexts.size()<<'\n';
        return 0;
    }
    return recover(c,records,file.get(),stdin_hash,duplicates);
}
}
