#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace wifi {
// Counts and elapsed time belong to this invocation. A resumed search gets a
// fresh runtime/candidate budget, while its source offset remains cumulative.
struct Progress {
    std::uint64_t run_started_at_ms=0;
    double elapsed_seconds=0;
    std::optional<double> candidates_per_second,eta_seconds,fraction;
    std::optional<double> search_eta_seconds,runtime_remaining_seconds,candidate_limit_eta_seconds;
    std::string eta_basis,progress_basis;
};
namespace progress_detail {
inline void apply_budget(Progress& p,double fraction,std::optional<double> seconds,const char* basis) {
    fraction=std::clamp(fraction,0.0,1.0);
    if(!p.fraction || fraction>*p.fraction) { p.fraction=fraction; p.progress_basis=basis; }
    if(seconds && std::isfinite(*seconds) && (!p.eta_seconds || *seconds<*p.eta_seconds)) {
        p.eta_seconds=std::max(0.0,*seconds); p.eta_basis=basis;
    }
    if(seconds && std::isfinite(*seconds)) {
        auto bounded=std::max(0.0,*seconds);
        if(std::string(basis)=="search") p.search_eta_seconds=bounded;
        else if(std::string(basis)=="time") p.runtime_remaining_seconds=bounded;
        else if(std::string(basis)=="candidates") p.candidate_limit_eta_seconds=bounded;
    }
}
inline void apply_candidate_budget(Progress& p,std::uint64_t done,std::uint64_t count,
                                   const char* basis,std::uint64_t rate_count) {
    std::optional<double> seconds;
    if(done>=count) seconds=0;
    else if(rate_count && p.elapsed_seconds>0)
        seconds=static_cast<double>(count-done)*p.elapsed_seconds/static_cast<double>(rate_count);
    apply_budget(p,count?static_cast<double>(done)/static_cast<double>(count):1.0,seconds,basis);
}
}
inline Progress search_progress(std::uint64_t next,std::uint64_t start,std::uint64_t valid,
                                double elapsed,std::uint64_t limit,unsigned runtime,
                                std::optional<std::uint64_t> total,std::uint64_t started_at_ms=0,
                                std::optional<std::uint64_t> valid_total=std::nullopt,
                                std::optional<std::uint64_t> tested_total=std::nullopt) {
    Progress p;
    p.run_started_at_ms=started_at_ms;
    p.elapsed_seconds=std::isfinite(elapsed)?std::max(0.0,elapsed):0.0;
    const auto processed=next>=start?next-start:0;
    if(processed && p.elapsed_seconds>0)
        p.candidates_per_second=static_cast<double>(valid)/p.elapsed_seconds;
    if(valid_total && tested_total) progress_detail::apply_candidate_budget(p,*tested_total,*valid_total,"search",valid);
    else if(total) progress_detail::apply_candidate_budget(p,next,*total,"search",processed);
    if(limit) progress_detail::apply_candidate_budget(p,processed,limit,"candidates",processed);
    if(runtime) progress_detail::apply_budget(p,p.elapsed_seconds/runtime,std::max(0.0,runtime-p.elapsed_seconds),"time");
    return p;
}
inline std::string progress_json(const Progress& p) {
    auto number=[](const std::optional<double>& n) { return n?std::to_string(*n):"null"; };
    return "{\"run_started_at_ms\":"+std::to_string(p.run_started_at_ms)+
        ",\"elapsed_seconds\":"+std::to_string(p.elapsed_seconds)+
        ",\"candidates_per_second\":"+number(p.candidates_per_second)+
        ",\"search_eta_seconds\":"+number(p.search_eta_seconds)+
        ",\"runtime_remaining_seconds\":"+number(p.runtime_remaining_seconds)+
        ",\"candidate_limit_eta_seconds\":"+number(p.candidate_limit_eta_seconds)+
        ",\"eta_seconds\":"+number(p.eta_seconds)+",\"eta_basis\":\""+p.eta_basis+
        "\",\"fraction\":"+number(p.fraction)+",\"progress_basis\":\""+p.progress_basis+"\"}";
}
}
