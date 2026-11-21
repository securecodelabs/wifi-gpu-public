#pragma once
#include "files.hpp"
#include <iosfwd>
#include <optional>
namespace wifi {
struct Record {
    unsigned type=0,key_version=0,message_pair=0;
    std::size_t line=0;
    std::string original,id;
    Bytes tag,ap,client,ssid,nonce,eapol,mdid,r0kh,r1kh;
    std::vector<Bytes> contexts;
    Bytes ft_r0_context,ft_r1_context;
    void prepare(std::optional<unsigned> corrections);
    bool verify(const Pmk& pmk) const;
};
Record parse_record(const std::string& line);
std::vector<Record> read_records(Input& input,std::size_t& duplicates);
std::vector<Record> read_records(std::istream& input,std::size_t& duplicates,std::string& sha256);
Bytes kdf_sha256(const Bytes& key,const std::string& label,const Bytes& context,unsigned bits);
}
