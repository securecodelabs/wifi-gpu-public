#include "recovery.hpp"
#include "hc22000.hpp"
#include "candidates.hpp"
#include "gpu_backend.hpp"
#include "progress.hpp"
#include <cstring>
#include <stdexcept>
namespace wifi {
unsigned recovery_unit_tests() {
    unsigned checks=0;
    auto check=[&](bool ok,const char* label) { if(!ok) throw std::runtime_error(std::string("Recovery self-test failed: ")+label); ++checks; };
    auto reject=[&](auto fn,const char* label) {
        bool rejected=false;
        try { fn(); } catch(const std::invalid_argument&) { rejected=true; }
        check(rejected,label);
    };
    auto p=search_progress(200,100,80,10,500,0,1000);
    auto counted=search_progress(100,0,20,10,0,0,200,0,50,20);
    check(counted.fraction==0.4 && counted.eta_seconds==15 && counted.progress_basis=="search","counted source progress excludes rejected candidates");
    check(p.candidates_per_second==8 && p.eta_seconds==40 && p.eta_basis=="candidates",
          "speed excludes rejected words and resume offset; limit uses this run");
    check(p.fraction==0.2 && p.progress_basis=="search","source progress includes resume offset");
    p=search_progress(200,100,80,10,500,20,1000);
    check(p.fraction==0.5 && p.progress_basis=="time" && p.eta_seconds==10 && p.eta_basis=="time",
          "combined budgets show furthest consumed and earliest estimated end");
    check(p.search_eta_seconds==80 && p.runtime_remaining_seconds==10 && p.candidate_limit_eta_seconds==40,
          "independent timers survive a shorter runtime budget");
    check(counted.search_eta_seconds==15 && !counted.runtime_remaining_seconds && !counted.candidate_limit_eta_seconds,
          "counted search uses valid candidates with unlimited budgets");
    check(progress_json(p).find("\"search_eta_seconds\":80.")!=std::string::npos &&
          progress_json(p).find("\"runtime_remaining_seconds\":10.")!=std::string::npos,
          "independent timers are serialized for every backend");
    p=search_progress(950,900,50,10,500,100,1000);
    check(p.fraction==0.95 && p.eta_seconds==10 && p.eta_basis=="search","near end of resumed source");
    check(p.search_eta_seconds==10 && p.runtime_remaining_seconds==90 && p.candidate_limit_eta_seconds==90,
          "resume uses cumulative search offset and fresh run budgets");
    p=search_progress(0,0,0,0,0,0,std::nullopt);
    check(!p.candidates_per_second && !p.eta_seconds && !p.fraction,"unlimited unknown source");
    check(!p.search_eta_seconds && !p.runtime_remaining_seconds && !p.candidate_limit_eta_seconds,
          "unknown source does not create estimates");
    p=search_progress(0,0,0,0,100,20,std::nullopt);
    check(!p.candidates_per_second && p.eta_seconds==20 && p.fraction==0,"startup has no invented rate");
    check(!p.search_eta_seconds && !p.candidate_limit_eta_seconds && p.runtime_remaining_seconds==20,
          "startup runtime countdown does not require candidate speed");
    p=search_progress(200,0,0,10,0,0,std::nullopt);
    check(p.candidates_per_second==0 && !p.eta_seconds,"rejected-only batch is not password testing speed");
    p=search_progress(200,100,80,25,100,20,200);
    check(p.fraction==1 && p.eta_seconds==0,"completed budgets clamp safely");
    check(p.search_eta_seconds==0 && p.runtime_remaining_seconds==0 && p.candidate_limit_eta_seconds==0,
          "all completed timers clamp to zero");
    check(hex_bytes(digest(Algorithm::Sha256,bytes("abc")))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 known answer");
    check(hex_bytes(digest(Algorithm::HmacSha1,bytes("Hi There"),Bytes(20,0x0b)))=="b617318655057264e28bc0b6fb378c8ef146be00","HMAC-SHA1 known answer");
    check(hex_bytes(digest(Algorithm::HmacSha256,bytes("Hi There"),Bytes(20,0x0b)))=="b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7","HMAC-SHA256 known answer");
    check(hex_bytes(digest(Algorithm::HmacMd5,bytes("Hi There"),Bytes(16,0x0b)))=="9294727a3638bb1c13f48ef8158bfc9d","HMAC-MD5 known answer");
    auto key=unhex("2b7e151628aed2a6abf7158809cf4f3c");
    auto message=unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710");
    check(hex_bytes(digest(Algorithm::Cmac,{},key))=="bb1d6929e95937287fa37d129b756746","CMAC empty");
    check(hex_bytes(digest(Algorithm::Cmac,Bytes(message.begin(),message.begin()+16),key))=="070a16b46b4d4144f79bdd9dd04a287c","CMAC aligned");
    check(hex_bytes(digest(Algorithm::Cmac,Bytes(message.begin(),message.begin()+40),key))=="dfa66747de9ae63030ca32611497c827","CMAC unaligned");
    check(hex_bytes(digest(Algorithm::Cmac,message,key))=="51f0bebf7e3b9d92fc49741779363cfe","CMAC multiple blocks");
    check(unhex("00aAfF")==Bytes({0,0xaa,0xff}),"hex case");
    reject([]{unhex("0");},"odd hex"); reject([]{unhex("gg");},"invalid hex");
    check(display(Bytes{'a',0,255,'\\'})=="a\\x00\\xff\\x5c","display escaping");
    check(json_string("a\n\"\\")=="\"a\\u000a\\\"\\\\\"","JSON escaping");
    std::array<std::string,4> custom{}; custom[0]="aa?d";
    auto mask=compile_mask("labpass?d?d",custom);
    check(mask.size==100,"mask keyspace");
    check(mask.at(0)==bytes("labpass00") && mask.at(42)==bytes("labpass42") && mask.at(99)==bytes("labpass99"),"mask enumeration");
    auto chars=compile_mask("?1",custom);
    check(chars.size==11 && chars.at(0)==bytes("a") && chars.at(10)==bytes("9"),"custom set deduplication");
    check(compile_mask("??",custom).at(0)==bytes("?"),"literal question mark");
    check(compile_mask("?a",custom).size==95 && compile_mask("?b",custom).size==256,"byte charsets");
    check(compile_mask("?b",custom).at(255)==Bytes{255},"high byte mask");
    {
        CandidateConfig config;
        config.masks={"prefix?1?d","literal!","abcdef?b?1"};
        config.custom[0]="a?d";
        std::vector<Bytes> expected;
        for(const auto& text:config.masks) {
            const auto reference_mask=compile_mask(text,config.custom);
            for(std::uint64_t i=0;i<reference_mask.size;++i) expected.push_back(reference_mask.at(i));
        }
        Candidates sequential(config); Bytes next;
        for(const auto& value:expected)
            check(sequential.next(next) && next==value,"incremental mask order matches indexed enumeration");
        check(!sequential.next(next) && sequential.position()==expected.size(),"combined mask exhausted exactly");
        for(auto offset:{0U,1U,9U,10U,109U,110U,111U}) {
            sequential.seek(offset);
            for(std::size_t i=offset;i<std::min<std::size_t>(offset+3,expected.size());++i)
                check(sequential.next(next) && next==expected[i],"mask cache resets on seek and mask boundary");
        }
        config.masks={"?H?H?H?H?H?H?H?H?H?H"};
        Candidates large(config);
        const auto reference_mask=compile_mask(config.masks[0],config.custom);
        for(auto offset:{std::uint64_t{0xffffffff},reference_mask.size-2}) {
            large.seek(offset);
            for(std::uint64_t i=offset;i<std::min(offset+3,reference_mask.size);++i)
                check(large.next(next) && next==reference_mask.at(i),"incremental mask carries beyond 32-bit indices");
        }
        check(!large.next(next),"incremental mask stops at 40-bit keyspace end");
    }
    for(const char* text:{"","?","?x","?2","?a?a?a?a?a?a?a?a?a?a"}) reject([&]{compile_mask(text,custom);},"invalid mask");
    reject([&]{compile_mask(std::string(64,'a'),custom);},"long mask");
    custom[0]="?1"; reject([&]{compile_mask("?1",custom);},"recursive charset");
    const std::string pmkid="WPA*01*00112233445566778899aabbccddeeff*020000000001*020000000002*74657374***";
    auto r=parse_record(pmkid);
    check(r.type==1 && r.ssid==bytes("test"),"PMKID parsing");
    check(!r.verify(reference(bytes("password"),r.ssid,4096)),"PMKID wrong password");
    const auto bads=std::vector<std::string>{"", "WPA*05*"+pmkid.substr(7),pmkid+"*",pmkid+"g0",pmkid+"80",std::string(16385,'x')};
    for(const auto& b:bads) reject([&]{parse_record(b);},"invalid record");
    auto upper=pmkid;
    for(std::size_t i=4;i<upper.size();++i) if(upper[i]>='a' && upper[i]<='f') upper[i]-=32;
    check(parse_record(upper).id==r.id,"canonical record identity");
    // Test the shader ABI guard without a Vulkan driver. These minimal frames
    // exercise framing only; full shader validity is checked by SPIRV-Tools.
    const char entry[]="wifi_pbkdf2_v2";
    const auto name_words=(sizeof(entry)+3)/4;
    std::vector<std::uint32_t> words(8+name_words);
    words[0]=0x07230203; words[1]=0x00010000; words[3]=2;
    words[5]=static_cast<std::uint32_t>((3+name_words)<<16)|15; words[6]=5; words[7]=1;
    std::memcpy(words.data()+8,entry,sizeof(entry));
    auto binary=[](const auto& w) { return std::string(reinterpret_cast<const char*>(w.data()),w.size()*4); };
    check(validate_vulkan_shader(binary(words))==words,"shader entry-point ABI accepted");
    for(auto mutation:{0U,1U,3U,4U,5U,6U,7U}) {
        auto bad=words;
        if(mutation==0) bad[0]=0;
        else if(mutation==1) bad[1]=0x00010600;
        else if(mutation==3) bad[3]=0;
        else if(mutation==4) bad[4]=1;
        else if(mutation==5) bad[5]=15; // zero-length instruction
        else if(mutation==6) bad[6]=4; // fragment instead of compute
        else bad[7]=2; // id outside the header's bound
        reject([&]{validate_vulkan_shader(binary(bad));},"invalid shader header or entry point");
    }
    auto overrun=words; overrun[5]=(65535U<<16)|15;
    reject([&]{validate_vulkan_shader(binary(overrun));},"shader instruction overruns buffer");
    auto short_entry=words; short_entry[5]=(3U<<16)|15;
    reject([&]{validate_vulkan_shader(binary(short_entry));},"short shader entry point");
    auto unterminated=words;
    for(std::size_t i=8;i<unterminated.size();++i) unterminated[i]=0x41414141;
    reject([&]{validate_vulkan_shader(binary(unterminated));},"unterminated shader entry point");
    auto legacy=words; legacy[8]=0x6e69616d; legacy[9]=0; // main
    reject([&]{validate_vulkan_shader(binary(legacy));},"old shader ABI rejected");
    reject([&]{validate_vulkan_shader(binary(words)+"x");},"unaligned shader length");
    reject([]{validate_vulkan_shader(std::string(1024*1024+4,'x'));},"oversized shader");
    return checks;
}
}
