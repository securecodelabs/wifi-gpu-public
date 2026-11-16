#pragma once
#include "gpu.hpp"
#include <memory>
#include <string_view>
namespace wifi {
enum class Algorithm { Sha256, HmacSha1, HmacSha256, HmacMd5, Cmac };
class Hash {
public:
    Hash(Algorithm algorithm, const Bytes& key={});
    ~Hash();
    void update(const void* data,std::size_t length);
    Bytes finish();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Bytes digest(Algorithm algorithm,const Bytes& data,const Bytes& key={});
std::string hex_bytes(const Bytes& data);
Bytes unhex(std::string_view value);
std::string display(const Bytes& data);
std::string json_string(const std::string& value);
std::uint64_t unsigned_number(std::string_view value);
}
