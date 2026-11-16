#pragma once
#include "files.hpp"
#include <array>
#include <limits>
#include <optional>
namespace wifi {
struct Mask {
    std::vector<Bytes> positions;
    std::uint64_t size=1;
    Bytes at(std::uint64_t index) const;
};
Mask compile_mask(const std::string& text,const std::array<std::string,4>& custom);
struct CandidateConfig {
    std::vector<std::string> wordlists,masks,mask_files;
    std::array<std::string,4> custom{};
    bool hexadecimal=false;
    std::string hybrid="append";
};
class Candidates {
public:
    // Bytes hashed and counted across all wordlists, excluding masks, hash
    // records and subsequent resume seeking. Called at start/end and at most
    // once a second between them; elapsed time is monotonic scan time.
    using Scan = std::function<void(std::uint64_t,std::uint64_t,double)>;
    explicit Candidates(const CandidateConfig& config,const Scan& scan={});
    ~Candidates();
    bool next(Bytes& password);
    void seek(std::uint64_t index);
    std::uint64_t position() const;
    std::string identity() const;
    std::optional<std::uint64_t> keyspace() const;
    std::optional<std::uint64_t> valid_total() const;
    std::uint64_t valid_position() const;
    std::vector<std::filesystem::path> input_paths() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
