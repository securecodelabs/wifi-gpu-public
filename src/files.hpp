#pragma once
#include "crypto.hpp"
#include <filesystem>
#include <functional>
namespace wifi {
// Windows denies writes/deletion. Linux uses advisory locks and mutation checks;
// use immutable inputs/read-only mounts for Linux jobs.
class Input {
public:
    explicit Input(const std::filesystem::path& path);
    ~Input();
    std::uint64_t size() const;
    bool line(Bytes& out,std::size_t maximum,bool& oversized);
    std::string sha256(const std::function<void(const std::uint8_t*,std::size_t)>& inspect={});
    void rewind();
    std::string contents(std::size_t maximum);
    const std::filesystem::path path;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Atomic snapshots with an exclusive sidecar lock. Existing files require opt-in.
class Snapshot {
public:
    Snapshot(const std::filesystem::path& path,bool replace);
    ~Snapshot();
    void write(const std::string& contents);
    const std::filesystem::path path;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::filesystem::path absolute_path(const std::string& path);
}
