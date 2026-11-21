#include "hc22000.hpp"
#include <algorithm>
#include <istream>
#include <set>
#include <stdexcept>

namespace wifi {
namespace {
void demand(bool condition,const char* message) { if(!condition) throw std::invalid_argument(message); }
void append(Bytes& out,const Bytes& value) { out.insert(out.end(),value.begin(),value.end()); }
Bytes exact(const std::string& value,std::size_t n,const char* field) {
    demand(value.size()==n*2,field); return unhex(value);
}
bool nonzero(const Bytes& value) { return std::any_of(value.begin(),value.end(),[](auto b){return b!=0;}); }
unsigned be16(const Bytes& b,std::size_t i) { return (static_cast<unsigned>(b[i])<<8)|b[i+1]; }
Bytes first(Bytes value,std::size_t n) { value.resize(n); return value; }
void sorted_append(Bytes& out,const Bytes& a,const Bytes& b) {
    if(a<b) { append(out,a); append(out,b); } else { append(out,b); append(out,a); }
}
std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out; std::size_t start=0;
    while(true) {
        auto at=line.find('*',start);
        out.push_back(line.substr(start,at==std::string::npos?at:at-start));
        if(at==std::string::npos) return out;
        start=at+1;
    }
}
}
Bytes kdf_sha256(const Bytes& key,const std::string& label,const Bytes& context,unsigned bits) {
    demand(bits>0 && bits<=512 && bits%8==0,"Invalid KDF length");
    Bytes output;
    for(unsigned counter=1;output.size()<bits/8;++counter) {
        Bytes data{static_cast<std::uint8_t>(counter),static_cast<std::uint8_t>(counter>>8)};
        append(data,bytes(label)); append(data,context);
        data.push_back(static_cast<std::uint8_t>(bits)); data.push_back(static_cast<std::uint8_t>(bits>>8));
        append(output,digest(Algorithm::HmacSha256,data,key));
    }
    output.resize(bits/8); return output;
}
Record parse_record(const std::string& line) {
    demand(line.size()<=16384,"HC22000 line exceeds 16384 bytes");
    auto f=split(line);
    demand(f.size()==9 || f.size()==12,"HC22000 requires 9 fields, or 12 for FT");
    demand(f[0]=="WPA","Only HC22000 WPA records are supported");
    auto type=exact(f[1],1,"Record type must be two hexadecimal characters")[0];
    demand(type>=1 && type<=4,"Unsupported HC22000 record type (expected 01..04)");
    demand(f.size()==(type>=3?12U:9U),"Unexpected FT fields for this record type");
    Record r; r.type=type; r.original=line;
    r.tag=exact(f[2],16,"MIC/PMKID must contain 16 bytes");
    r.ap=exact(f[3],6,"AP address must contain six bytes"); r.client=exact(f[4],6,"Client address must contain six bytes");
    r.ssid=unhex(f[5]); demand(r.ssid.size()<=32,"SSID exceeds 32 bytes");
    demand(f[8].empty() || f[8].size()==2,"Message pair must be empty or one byte");
    r.message_pair=f[8].empty()?0:unhex(f[8])[0];
    if(type>=3) {
        r.mdid=exact(f[9],2,"FT mobility domain must contain two bytes");
        r.r0kh=unhex(f[10]); r.r1kh=unhex(f[11]);
        demand(!r.r0kh.empty() && r.r0kh.size()<=48,"FT R0KH-ID must contain 1..48 bytes");
        demand(r.r1kh.size()==6,"FT R1KH-ID must contain six bytes");
    }
    if(type==1 || type==3) {
        demand(f[6].empty() && f[7].empty(),"PMKID nonce and EAPOL fields must be empty");
        demand(r.message_pair<=7,"Unsupported PMKID flags");
    } else {
        demand(!f[8].empty(),"EAPOL message pair is required");
        r.nonce=exact(f[6],32,"Nonce field must contain 32 bytes");
        r.eapol=unhex(f[7]);
        demand(r.eapol.size()>=99 && r.eapol.size()<=4096,"EAPOL size must be 99..4096 bytes");
        demand(r.eapol[0]>=1 && r.eapol[0]<=3 && r.eapol[1]==3,"Expected an EAPOL-Key packet");
        demand(be16(r.eapol,2)+4==r.eapol.size(),"EAPOL length field disagrees with packet size");
        demand(r.eapol[4]==2 || r.eapol[4]==254,"Unsupported EAPOL key descriptor type");
        demand(be16(r.eapol,97)+99==r.eapol.size(),"EAPOL key-data length disagrees with packet size");
        auto info=be16(r.eapol,5); r.key_version=info&7;
        demand(r.key_version>=1 && r.key_version<=3,"Unsupported EAPOL key descriptor version");
        demand((info&0x108)==0x108 && !(info&0xc00),"Expected a pairwise MIC-bearing handshake message");
        demand(type!=4 || r.key_version==3,"FT EAPOL requires key descriptor version 3");
        demand((r.message_pair&7)<=5 && !(r.message_pair&8),"Unsupported EAPOL message pair");
        demand((r.message_pair&0x60)!=0x60,"Conflicting nonce endianness flags");
        auto pair=r.message_pair&7;
        demand(bool(info&0x80)==(pair==3 || pair==4),"Message pair does not match EAPOL sender (ACK bit)");
        demand(std::all_of(r.eapol.begin()+81,r.eapol.begin()+97,[](auto b){return b==0;}),"EAPOL MIC field must be zeroed in HC22000");
        Bytes frame_nonce(r.eapol.begin()+17,r.eapol.begin()+49);
        demand(nonzero(r.nonce) && nonzero(frame_nonce),"Missing nonce; this handshake cannot verify a password");
    }
    // Normalize hex case for duplicate identity without changing byte semantics.
    std::string canonical=line;
    for(std::size_t i=4;i<canonical.size();++i) if(canonical[i]>='A' && canonical[i]<='F') canonical[i]+=32;
    r.id=hex_bytes(digest(Algorithm::Sha256,bytes(canonical)));
    r.prepare(std::nullopt); return r;
}
void Record::prepare(std::optional<unsigned> corrections) {
    contexts.clear(); ft_r0_context.clear(); ft_r1_context.clear();
    if(type>=3) {
        ft_r0_context.push_back(static_cast<std::uint8_t>(ssid.size())); append(ft_r0_context,ssid); append(ft_r0_context,mdid);
        ft_r0_context.push_back(static_cast<std::uint8_t>(r0kh.size())); append(ft_r0_context,r0kh); append(ft_r0_context,client);
        ft_r1_context=r1kh; append(ft_r1_context,client);
    }
    if(type==1 || type==3) return;
    auto pair=message_pair&7;
    unsigned count=corrections.value_or((message_pair&0x80) && !(message_pair&0x10)?8:0);
    demand(count<=1024,"Nonce corrections exceed 1024");
    if(pair==3 || pair==4) {
        demand(!corrections || *corrections==0,"Nonce correction is not supported for M3-based EAPOL pairs; use exact nonces");
        count=0;
    }
    std::set<Bytes> variants;
    variants.insert(nonce);
    for(bool little:{false,true}) {
        if((message_pair&0x20) && !little) continue;
        if((message_pair&0x40) && little) continue;
        std::uint32_t base=0;
        for(unsigned i=0;i<4;++i) base|=static_cast<std::uint32_t>(nonce[28+i]) << (little?8*i:24-8*i);
        for(unsigned step=0;step<=count;++step) {
            auto n=base-static_cast<std::uint32_t>(count/2)+step;
            Bytes corrected=nonce;
            for(unsigned i=0;i<4;++i) corrected[28+i]=static_cast<std::uint8_t>(n>>(little?8*i:24-8*i));
            variants.insert(std::move(corrected));
        }
    }
    Bytes frame_nonce(eapol.begin()+17,eapol.begin()+49);
    auto add=[&](const Bytes& other) {
        Bytes ctx;
        if(type==4) {
            append(ctx,(pair==3 || pair==4)?other:frame_nonce);
            append(ctx,(pair==3 || pair==4)?frame_nonce:other);
            append(ctx,ap); append(ctx,client);
        } else { sorted_append(ctx,ap,client); sorted_append(ctx,other,frame_nonce); }
        contexts.push_back(std::move(ctx));
    };
    add(nonce); // Exact nonce first; corrections follow deterministically.
    for(const auto& v:variants) if(v!=nonce) add(v);
}
bool Record::verify(const Pmk& pmk) const {
    Bytes key(pmk.begin(),pmk.end());
    if(type==1) {
        auto data=bytes("PMK Name"); append(data,ap); append(data,client);
        return first(digest((message_pair&2)?Algorithm::HmacSha256:Algorithm::HmacSha1,data,key),16)==tag;
    }
    if(type>=3) {
        auto r0=kdf_sha256(key,"FT-R0",ft_r0_context,384);
        if(type==3) {
            auto n0=bytes("FT-R0N"); n0.insert(n0.end(),r0.begin()+32,r0.end());
            auto r0name=first(digest(Algorithm::Sha256,n0),16);
            auto n1=bytes("FT-R1N"); append(n1,r0name); append(n1,ft_r1_context);
            return first(digest(Algorithm::Sha256,n1),16)==tag;
        }
        r0.resize(32); key=kdf_sha256(r0,"FT-R1",ft_r1_context,256);
    }
    for(const auto& ctx:contexts) {
        Bytes kck;
        if(key_version<=2) {
            auto input=bytes("Pairwise key expansion"); input.push_back(0); append(input,ctx); input.push_back(0);
            kck=first(digest(Algorithm::HmacSha1,input,key),16);
        } else kck=first(kdf_sha256(key,type==4?"FT-PTK":"Pairwise key expansion",ctx,384),16);
        auto algorithm=key_version==1?Algorithm::HmacMd5:key_version==2?Algorithm::HmacSha1:Algorithm::Cmac;
        if(first(digest(algorithm,eapol,kck),16)==tag) return true;
    }
    return false;
}
namespace {
template<class NextLine>
std::vector<Record> read_records_from(NextLine next_line,const std::string& name,std::size_t& duplicates) {
    std::vector<Record> out; std::set<std::string> ids; Bytes b; bool too_long=false;
    std::size_t line=0,total=0; duplicates=0;
    while(next_line(b,16384,too_long)) {
        ++line; total+=b.size();
        if(too_long || total>64*1024*1024 || line>1000000) throw std::invalid_argument("HC22000 file exceeds input limits at line "+std::to_string(line));
        if(line==1 && b.size()>=3 && b[0]==0xef && b[1]==0xbb && b[2]==0xbf) b.erase(b.begin(),b.begin()+3);
        std::string s(b.begin(),b.end());
        auto begin=s.find_first_not_of(" \t");
        if(begin==std::string::npos || s[begin]=='#') continue;
        s=s.substr(begin,s.find_last_not_of(" \t")-begin+1);
        try {
            auto record=parse_record(s); record.line=line;
            if(ids.insert(record.id).second) out.push_back(std::move(record)); else ++duplicates;
        } catch(const std::exception& e) { throw std::invalid_argument(name+":"+std::to_string(line)+": "+e.what()); }
        if(out.size()>100000) throw std::invalid_argument("Too many HC22000 records (limit 100000)");
    }
    if(out.empty()) throw std::invalid_argument("No usable HC22000 records in "+name);
    return out;
}
}
std::vector<Record> read_records(Input& input,std::size_t& duplicates) {
    return read_records_from([&](Bytes& line,std::size_t maximum,bool& oversized) {
        return input.line(line,maximum,oversized);
    },input.path.u8string(),duplicates);
}
std::vector<Record> read_records(std::istream& input,std::size_t& duplicates,std::string& sha256) {
    Hash hash(Algorithm::Sha256);
    auto records=read_records_from([&](Bytes& line,std::size_t maximum,bool& oversized) {
        line.clear();
        for(;;) {
            auto value=input.get();
            if(value==std::char_traits<char>::eof()) {
                if(input.bad() || !input.eof()) throw std::runtime_error("Cannot read HC22000 from stdin");
                break;
            }
            line.push_back(static_cast<std::uint8_t>(value));
            // Bound allocation even if an upstream producer never sends a newline.
            if(value=='\n' || line.size()>maximum+2) break;
        }
        const bool any=!line.empty();
        hash.update(line.data(),line.size());
        if(!line.empty() && line.back()=='\n') line.pop_back();
        if(!line.empty() && line.back()=='\r') line.pop_back();
        oversized=line.size()>maximum;
        return any;
    },"stdin",duplicates);
    sha256=hex_bytes(hash.finish());
    return records;
}
}
