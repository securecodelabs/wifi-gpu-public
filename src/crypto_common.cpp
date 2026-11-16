#include "crypto.hpp"
#include <charconv>
#include <stdexcept>
namespace wifi {
namespace {
int nibble(char c) {
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    throw std::invalid_argument("Non-hexadecimal input");
}
constexpr const char* digits="0123456789abcdef";
}
Bytes digest(Algorithm algorithm,const Bytes& data,const Bytes& key) {
    Hash h(algorithm,key); h.update(data.data(),data.size()); return h.finish();
}
std::string hex_bytes(const Bytes& data) {
    std::string out; out.reserve(data.size()*2);
    for(auto b:data) { out+=digits[b>>4]; out+=digits[b&15]; } return out;
}
Bytes unhex(std::string_view value) {
    if(value.size()%2) throw std::invalid_argument("Odd hexadecimal length");
    Bytes out; out.reserve(value.size()/2);
    for(std::size_t i=0;i<value.size();i+=2) out.push_back(static_cast<std::uint8_t>((nibble(value[i])<<4)|nibble(value[i+1])));
    return out;
}
std::string display(const Bytes& data) {
    std::string out;
    for(auto b:data) {
        if(b>=32 && b<=126 && b!='\\') out+=static_cast<char>(b);
        else { out+="\\x"; out+=digits[b>>4]; out+=digits[b&15]; }
    }
    return out;
}
std::string json_string(const std::string& value) {
    std::string out="\"";
    for(unsigned char c:value) {
        if(c=='"' || c=='\\') { out+='\\'; out+=static_cast<char>(c); }
        else if(c<32 || c>=127) { out+="\\u00"; out+=digits[c>>4]; out+=digits[c&15]; }
        else out+=static_cast<char>(c);
    }
    out+='"'; return out;
}
std::uint64_t unsigned_number(std::string_view value) {
    std::uint64_t n=0; auto r=std::from_chars(value.data(),value.data()+value.size(),n);
    if(value.empty() || r.ec!=std::errc{} || r.ptr!=value.data()+value.size()) throw std::invalid_argument("Invalid unsigned integer: "+std::string(value));
    return n;
}
}
