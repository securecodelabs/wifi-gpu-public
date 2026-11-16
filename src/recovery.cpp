#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "platform.hpp"
#ifndef _WIN32
#include <csignal>
#endif
#include "recovery.hpp"
#include "cpu.hpp"
#include "hc22000.hpp"
#include "candidates.hpp"
#include "progress.hpp"
#include "hardware.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

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
Config parse(int argc,char** argv) {
    Config c; c.command=argv[1]; std::set<std::string> used;
    const std::set<std::string> multi={"--wordlist","--mask","--mask-file"};
    for(int i=2;i<argc;++i) {
        std::string key=argv[i];
        if(!multi.count(key) && !used.insert(key).second) throw std::invalid_argument("Duplicate option: "+key);
        if(key=="--allow-software-vulkan") { c.allow_software=true; continue; }
        if(key=="--cpu") { c.cpu=true; continue; }
        if(key=="--activity-json") { c.activity_json=true; continue; }
        if(key=="--overwrite") { c.overwrite=true; continue; }
        if(key=="--hex-wordlist") { c.candidates.hexadecimal=true; continue; }
        if(i+1>=argc) throw std::invalid_argument("Missing value for "+key);
        std::string value=argv[++i];
        if(value.empty()) throw std::invalid_argument("Empty value for "+key);
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
        else {
            unsigned max=0;
            if(key=="--threads") max=64;
            else if(key=="--batch") max=static_cast<unsigned>(gpu_batch_limit(Backend::OpenCl));
            else if(key=="--group" || key=="--chunk") max=256;
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
    }
    if(used.count("--resume") && used.count("--session")) throw std::invalid_argument("Use either --session or --resume");
    if(c.resume && (used.count("--skip") || c.overwrite)) throw std::invalid_argument("--resume cannot be combined with --skip or --overwrite");
    if(c.cpu && (used.count("--backend") || used.count("--device") || used.count("--kernel") || used.count("--group") || used.count("--chunk"))) throw std::invalid_argument("GPU options cannot be combined with --cpu");
    if(used.count("--threads") && !c.cpu) throw std::invalid_argument("--threads requires --cpu");
    if(c.candidates.hexadecimal && c.candidates.wordlists.empty()) throw std::invalid_argument("--hex-wordlist requires --wordlist");
    if(used.count("--hybrid") && (c.candidates.wordlists.empty() || (c.candidates.masks.empty() && c.candidates.mask_files.empty()))) throw std::invalid_argument("--hybrid requires both a wordlist and a mask");
    if(c.command=="inspect") {
        for(const auto& key:used) if(key!="--hashes" && key!="--nonce-error-corrections") throw std::invalid_argument("Unsupported inspect option: "+key);
        if(!c.candidates.wordlists.empty() || !c.candidates.masks.empty() || !c.candidates.mask_files.empty()) throw std::invalid_argument("inspect does not accept candidate sources");
    }
    if(c.allow_software && (c.cpu || c.backend!=Backend::Vulkan)) throw std::invalid_argument("--allow-software-vulkan requires Vulkan mode");
    if(!used.count("--batch") && !c.cpu && c.backend==Backend::Hip) c.batch=262144;
    if(c.batch>(c.cpu ? 4096 : gpu_batch_limit(c.backend)))
        throw std::invalid_argument("--batch exceeds backend limit (OpenCL/HIP: 262144; CPU/Vulkan: 4096)");
    if(c.hashes.empty()) throw std::invalid_argument("--hashes FILE is required");
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
using Matches=std::map<std::string,Bytes>;
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
int recover(const Config& c,std::vector<Record>& records,Input& hashes,std::size_t duplicates) {
    Control controls;
    std::string latest;
    auto activity=[&](const std::string& stage) {
        if(c.activity_json) std::cout<<"WIFI_ACTIVITY {\"stage\":"<<json_string(stage)
            <<",\"batch_size\":"<<c.batch<<",\"latest_candidate_hex\":"<<(latest.empty()?"null":json_string(latest))<<"}\n"<<std::flush;
    };
    activity("loading_sources");
    std::cerr<<"Loading candidate sources and hashing inputs for session identity...\n";
    Candidates::Scan scan;
    if(c.activity_json) scan=[&](std::uint64_t scanned,std::uint64_t total,double elapsed) {
        std::cout<<"WIFI_ACTIVITY {\"stage\":\"loading_sources\",\"batch_size\":"<<c.batch
            <<",\"latest_candidate_hex\":null,\"wordlist_scan\":{\"bytes_scanned\":"<<scanned
            <<",\"total_bytes\":"<<total<<",\"elapsed_seconds\":"<<elapsed
            <<",\"cpu_threads\":1}}\n"<<std::flush;
    };
    Candidates candidates(c.candidates,scan);
    auto paths=candidates.input_paths(); paths.push_back(hashes.path);
    std::vector<std::filesystem::path> outputs;
    for(const auto& name:{c.output,c.session}) if(!name.empty()) {
        auto path=absolute_path(name); auto lock=path; lock+=".lock";
        for(const auto& input:paths) if(same_path(path,input) || same_path(lock,input)) throw std::invalid_argument("Output/session would overwrite an input file");
        for(const auto& output:outputs) if(same_path(path,output) || same_path(lock,output)) throw std::invalid_argument("Output and session paths overlap");
        outputs.push_back(path); outputs.push_back(lock);
    }
    std::string configuration="wifi-gpu-search-v1\n"+hashes.sha256()+"\n"+candidates.identity()+"\nnonce="+(c.corrections?std::to_string(*c.corrections):"auto")+"\n";
    auto identity=hex_bytes(digest(Algorithm::Sha256,bytes(configuration)));
    Matches matches; std::uint64_t committed=c.skip;
    std::unique_ptr<Snapshot> session,output;
    if(!c.session.empty()) {
        auto path=absolute_path(c.session);
        // The sidecar lock prevents a second process from reading stale state and racing this run.
        session=std::make_unique<Snapshot>(path,c.resume || c.overwrite);
        if(c.resume) { Input saved(path); read_session(saved.contents(32*1024*1024),identity,committed,matches,records); }
    }
    if(!c.output.empty()) {
        auto path=absolute_path(c.output);
        output=std::make_unique<Snapshot>(path,c.overwrite || c.resume);
        if(c.resume && std::filesystem::exists(path)) {
            Input saved(path); Bytes prefix; bool large=false;
            saved.line(prefix,200,large);
            auto expected="{\"schema\":1,\"job_id\":"+json_string(identity)+",";
            std::string value(prefix.begin(),prefix.end());
            if(value.rfind(expected,0)!=0) throw std::invalid_argument("Existing output belongs to a different job; choose another --output path");
        }
    }
    activity("restoring_position");
    candidates.seek(committed);
    const auto start=committed;
    const auto valid_start=candidates.valid_position();
    auto valid_total=candidates.valid_total();
    if(valid_total && !c.resume) *valid_total-=valid_start;
    const auto run_started_at_ms=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    std::uint64_t rejected=0,valid=0,derivations=0,tested_count=0;
    std::optional<Clock::time_point> started;
    std::string hardware="null";
    auto elapsed=[&]{return started?std::chrono::duration<double>(Clock::now()-*started).count():0.0;};
    // At most one immutable, verified snapshot is being written while the next
    // GPU batch runs. Join before another write or any terminal checkpoint.
    // Declared after session/output so destruction joins before their locks die.
    std::future<void> checkpoint;
    auto save=[&](const std::string& status,bool asynchronous=false) {
        if(checkpoint.valid()) checkpoint.get();
        auto session_data=session?session_text(identity,committed,matches):std::string{};
        auto output_data=output?result_json(identity,status,committed,matches,records,
            search_progress(committed,start,valid,elapsed(),c.limit,c.runtime,candidates.keyspace(),run_started_at_ms,valid_total,(c.resume?valid_start:0)+tested_count),hardware,latest,(c.resume?valid_start:0)+tested_count,valid_total):std::string{};
        auto write=[s=session.get(),o=output.get(),
                    session_data=std::move(session_data),output_data=std::move(output_data)] {
            if(s) s->write(session_data);
            if(o) o->write(output_data);
        };
        if(asynchronous && (session || output)) checkpoint=std::async(std::launch::async,std::move(write));
        else write();
    };
    std::map<Bytes,std::vector<std::size_t>> groups;
    for(std::size_t i=0;i<records.size();++i) groups[records[i].ssid].push_back(i);
    std::cerr<<"records="<<records.size()<<" duplicates="<<duplicates<<" SSIDs="<<groups.size()<<" start="<<committed;
    if(auto space=candidates.keyspace()) std::cerr<<" keyspace="<<*space;
    std::cerr<<" backend="<<(c.cpu?std::string("CPU ")+cpu_provider():std::string(backend_name(c.backend))+" PBKDF2 + CPU MIC/PMKID verification")<<'\n';
    if(matches.size()==records.size()) { save("recovered"); std::cout<<"All records already verified in the session.\n"; return 0; }
    std::unique_ptr<Gpu> gpu;
    std::unique_ptr<Cpu> cpu;
    bool filter_reported=false;
    activity("initializing_device");
    if(!c.cpu) {
        gpu=std::make_unique<Gpu>(c.device,kernel_source(c.kernel,c.backend),c.backend,c.allow_software);
        auto test=gpu->derive({bytes("password")},bytes("IEEE"),4096,c.chunk,c.group);
        if(test.pmks[0]!=reference(bytes("password"),bytes("IEEE"),4096)) throw std::runtime_error("GPU startup self-test failed");
        if(gpu->info().software) std::cerr<<"SOFTWARE VULKAN: CPU execution; not GPU acceleration\n";
        hardware=gpu_hardware_json(c.backend,c.device,gpu->info());
        std::cerr<<"GPU="<<gpu->info().name<<"; startup self-test passed\n";
    }
    if(c.cpu) { cpu=std::make_unique<Cpu>(c.threads); hardware=cpu_hardware_json(cpu->threads()); std::cerr<<"CPU threads="<<cpu->threads()<<"; GPU runtimes disabled\n"; }
    started=Clock::now(); auto reported=*started;
    save("running");
    activity("recovering");
    std::string status="exhausted";
    auto report=[&] {
        auto seconds=elapsed();
        std::cerr<<"status="<<status<<" next="<<committed<<" candidates="<<valid<<" rejected="<<rejected
            <<" recovered="<<matches.size()<<'/'<<records.size()<<" derivations="<<derivations
            <<" elapsed_s="<<seconds<<" derivations_per_second="<<(seconds>0?static_cast<double>(derivations)/seconds:0)<<'\n';
    };
    struct CandidateBatch {
        std::vector<Bytes> passwords;
        std::uint64_t end=0,rejected=0;
        bool eof=false;
    } batches[2];
    for(auto& batch:batches) batch.passwords.reserve(c.batch);
    auto prepare=[&](CandidateBatch& batch) {
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
    };
    // HIP mask recovery also benefits from preparing the next batch while
    // PBKDF2 runs. The saved batch end remains the only committed cursor.
    const bool prefetch=gpu && (!c.candidates.wordlists.empty() || c.backend==Backend::Hip);
    std::size_t current=0;
    // Only the producer accesses the mutable candidate cursor. A prefetched
    // batch is speculative: snapshots commit the verified batch's saved end.
    // Destroying the future joins its producer before candidate storage dies.
    std::future<void> pending;
    try {
        while(matches.size()<records.size()) {
            if(cancelled) { status="interrupted"; break; }
            if(c.runtime && elapsed()>=c.runtime) { status="runtime_limit"; break; }
            if(c.limit && committed-start>=c.limit) { status="candidate_limit"; break; }
            auto& batch=batches[current];
            if(pending.valid()) pending.get(); else prepare(batch);
            auto& passwords=batch.passwords;
            rejected+=batch.rejected;
            if(cancelled) { status="interrupted"; break; }
            const bool eof=batch.eof || (candidates.keyspace() && batch.end==*candidates.keyspace());
            if(prefetch && !eof && (!c.limit || batch.end-start<c.limit)) {
                auto* next=&batches[current^1];
                pending=std::async(std::launch::async,[&prepare,next] { prepare(*next); });
            }
            bool complete=true;
            std::optional<std::size_t> tested,furthest_tested;
            if(!passwords.empty()) {
                auto record_match=[&](const Record& record,std::size_t i,const Pmk& pmk) {
                    auto independent=reference(passwords[i],record.ssid,4096);
                    if(independent!=pmk || !record.verify(independent)) throw std::runtime_error("GPU hit failed independent CPU verification");
                    matches.emplace(record.id,passwords[i]);
                    std::cout<<"FOUND line="<<record.line<<" ssid="<<json_string(display(record.ssid))<<" bssid="<<hex_bytes(record.ap)
                        <<" password="<<json_string(display(passwords[i]))<<" password_hex="<<hex_bytes(passwords[i])<<'\n'<<std::flush;
                };
                for(const auto& group:groups) {
                    if(cancelled) { complete=false; break; }
                    if(std::all_of(group.second.begin(),group.second.end(),[&](auto i){return matches.count(records[i].id)!=0;})) continue;
                    // A single exact-nonce WPA2 record can be filtered before
                    // downloading keys. Other record combinations keep the
                    // shared-PMK path below, including unsupported backends.
                    if(gpu && group.second.size()==1) {
                        auto& record=records[group.second.front()];
                        if(record.type==2 && record.key_version==2 && record.contexts.size()==1) {
                            auto result=gpu->derive_wpa2(passwords,group.first,record.contexts[0],record.eapol,record.tag,c.chunk,c.group);
                            if(result) {
                                if(result->count!=passwords.size()) throw std::runtime_error("GPU filtered batch count mismatch");
                                derivations+=result->count;
                                if(!filter_reported) {
                                    std::cerr<<"GPU WPA2 EAPOL MIC filter enabled; hits independently CPU verified\n";
                                    filter_reported=true;
                                }
                                if(cancelled) { complete=false; break; }
                                tested=passwords.size()-1;
                                if(!result->hits.empty()) {
                                    const auto& hit=result->hits.front();
                                    if(hit.index>=passwords.size()) throw std::runtime_error("GPU filtered batch index out of range");
                                    if(!record.verify(hit.pmk)) throw std::runtime_error("GPU MIC filter hit failed CPU verification");
                                    tested=hit.index; record_match(record,hit.index,hit.pmk);
                                }
                                furthest_tested=std::max(furthest_tested.value_or(0),*tested);
                                continue;
                            }
                        }
                    }
                    std::vector<Pmk> pmks;
                    if(gpu) pmks=gpu->derive(passwords,group.first,4096,c.chunk,c.group).pmks;
                    else {
                        pmks=cpu->derive(passwords,group.first,4096,&cancelled).pmks;
                        if(pmks.empty()) { complete=false; break; }
                    }
                    derivations+=pmks.size();
                    for(auto index:group.second) {
                        auto& record=records[index]; if(matches.count(record.id)) continue;
                        std::optional<Bytes> filtered;
                        if(gpu && record.type==2 && record.key_version==2 && record.contexts.size()==1)
                            filtered=gpu->filter_wpa2(pmks,record.contexts[0],record.eapol,record.tag,c.group);
                        if(filtered && !filter_reported) {
                            std::cerr<<"GPU WPA2 EAPOL MIC filter enabled; hits independently CPU verified\n";
                            filter_reported=true;
                        }
                        for(std::size_t i=0;i<pmks.size();++i) {
                            if(cancelled) { complete=false; break; }
                            tested=i; furthest_tested=std::max(furthest_tested.value_or(0),i);
                            if(filtered) {
                                if(!filtered->at(i)) continue;
                                if(!record.verify(pmks[i])) throw std::runtime_error("GPU MIC filter hit failed CPU verification");
                            } else if(!record.verify(pmks[i])) continue;
                            record_match(record,i,pmks[i]);
                            break;
                        }
                        if(!complete) break;
                    }
                    if(!complete) break;
                }
            }
            if(tested) { latest=hex_bytes(passwords[*tested]); activity("recovering"); }
            if(!complete) { status="interrupted"; break; }
            committed=batch.end; valid+=passwords.size();
            if(furthest_tested) tested_count+=*furthest_tested+1;
            status="running";
            if(matches.size()==records.size()) { status="recovered"; save(status); break; }
            if(eof) { status="exhausted"; save(status); break; }
            save(status,gpu!=nullptr);
            if(std::chrono::duration<double>(Clock::now()-reported).count()>=c.status_every) { report(); reported=Clock::now(); }
            if(prefetch) current^=1;
        }
        save(status); report();
    } catch(...) {
        // committed advances only after every SSID in a batch has been verified.
        // A failed or interrupted batch is replayed on resume; verified hits remain.
        try { save("error"); } catch(const std::exception& e) { std::cerr<<"Could not save recovery state: "<<e.what()<<'\n'; }
        throw;
    }
    if(status=="recovered") return 0;
    if(status=="interrupted") return 3;
    if(status=="exhausted") return 2;
    return 4;
}
}
void recovery_help() {
    std::cout<<"\nHC22000 commands (WPA record types 01..04):\n"
        <<"  inspect --hashes FILE [--nonce-error-corrections N]\n"
        <<"  recover --hashes FILE --wordlist WORDS [--wordlist MORE_WORDS]\n"
        <<"  recover --hashes FILE --mask 'prefix?d?d?d?d'\n"
        <<"  recover --hashes FILE --wordlist WORDS --mask '?d?d' [--hybrid append|prepend]\n"
        <<"Candidate options: --mask (repeatable), --mask-file FILE (repeatable),\n"
        <<"  --charset1 SET .. --charset4 SET, --hex-wordlist, --skip N, --limit N (0=unlimited).\n"
        <<"Mask tokens: ?l ?u ?d ?s ?a ?b ?h ?H ?1..?4; ?? is literal ?.\n"
        <<"Session/output: --output results.json, --session run.session,\n"
        <<"  --resume run.session (repeat original input options), --overwrite.\n"
        <<"Runtime: --cpu OR --backend opencl|vulkan|hip (default "<<backend_name(default_backend())<<"), --device N, --batch N (OpenCL/HIP: 1..262144; CPU/Vulkan: 1..4096), --group 1..256,\n"
        <<"  --threads 1..64 (CPU only; default logical CPUs, capped at 64),\n"
        <<"  --chunk 1..256, --kernel PATH, --runtime SECONDS (0=unlimited), --status-every SECONDS,\n"
        <<"  --nonce-error-corrections 0..1024 (default: flags select 0 or 8).\n"
        <<"  --activity-json emits WIFI_ACTIVITY JSON lines with stages and the latest tested candidate (hex).\n"
        <<"Testing: --allow-software-vulkan permits CPU Vulkan devices and labels them explicitly.\n"
        <<"Recovery exit codes: 0 all recovered, 1 error, 2 exhausted, 3 interrupted, 4 limit.\n";
}
int recovery_cli(int argc,char** argv) {
    if(argc==3 && std::string(argv[2])=="--help") { recovery_help(); return 0; }
    auto c=parse(argc,argv); Input file(absolute_path(c.hashes)); std::size_t duplicates=0;
    auto records=read_records(file,duplicates);
    for(auto& r:records) r.prepare(c.corrections);
    if(c.command=="inspect") {
        std::cout<<"records="<<records.size()<<" duplicates="<<duplicates<<'\n';
        for(const auto& r:records) std::cout<<"line="<<r.line<<" type="<<r.type<<" key_version="<<r.key_version<<" pair="<<r.message_pair
            <<" ssid="<<json_string(display(r.ssid))<<" ssid_hex="<<hex_bytes(r.ssid)<<" bssid="<<hex_bytes(r.ap)<<" nonce_variants="<<r.contexts.size()<<'\n';
        return 0;
    }
    return recover(c,records,file,duplicates);
}
}
