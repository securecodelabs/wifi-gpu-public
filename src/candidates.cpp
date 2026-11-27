#include "candidates.hpp"
#include <algorithm>
#include <chrono>
#include <optional>
#include <stdexcept>

namespace wifi {
namespace {
Bytes builtin(char token) {
    Bytes out;
    if(token=='d') return bytes("0123456789");
    if(token=='l') return bytes("abcdefghijklmnopqrstuvwxyz");
    if(token=='u') return bytes("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    if(token=='h') return bytes("0123456789abcdef");
    if(token=='H') return bytes("0123456789ABCDEF");
    if(token=='?') return Bytes{'?'};
    if(token=='a') { for(unsigned i=32;i<=126;++i) out.push_back(static_cast<std::uint8_t>(i)); return out; }
    if(token=='b') { for(unsigned i=0;i<256;++i) out.push_back(static_cast<std::uint8_t>(i)); return out; }
    if(token=='s') {
        for(unsigned i=32;i<=126;++i)
            if(!((i>='a'&&i<='z') || (i>='A'&&i<='Z') || (i>='0'&&i<='9'))) out.push_back(static_cast<std::uint8_t>(i));
        return out;
    }
    throw std::invalid_argument(std::string("Unknown mask token ?")+token);
}
Bytes charset(const std::string& text) {
    Bytes out;
    for(std::size_t i=0;i<text.size();++i) {
        Bytes add;
        if(text[i]=='?') {
            if(++i==text.size()) throw std::invalid_argument("Trailing ? in custom charset");
            add=builtin(text[i]); // Custom sets cannot recursively reference other sets.
        } else add.push_back(static_cast<std::uint8_t>(text[i]));
        for(auto c:add) if(std::find(out.begin(),out.end(),c)==out.end()) out.push_back(c);
    }
    return out;
}
void bom(Bytes& line) {
    if(line.size()>=2 && ((line[0]==0xff && line[1]==0xfe) || (line[0]==0xfe && line[1]==0xff)))
        throw std::invalid_argument("UTF-16 wordlist/mask file; save it as UTF-8 first");
    if(line.size()>=3 && line[0]==0xef && line[1]==0xbb && line[2]==0xbf) line.erase(line.begin(),line.begin()+3);
}
std::vector<Bytes> mask_positions(const std::string& text,const std::array<std::string,4>& custom) {
    if(text.empty()) throw std::invalid_argument("Empty mask");
    std::vector<Bytes> positions;
    for(std::size_t i=0;i<text.size();++i) {
        Bytes set;
        if(text[i]=='?') {
            if(++i==text.size()) throw std::invalid_argument("Trailing ? in mask; use ?? for a literal question mark");
            if(text[i]>='1' && text[i]<='4') set=charset(custom[static_cast<unsigned>(text[i]-'1')]);
            else set=builtin(text[i]);
        } else set.push_back(static_cast<std::uint8_t>(text[i]));
        if(set.empty()) throw std::invalid_argument("Mask references an undefined or empty custom charset");
        positions.push_back(std::move(set));
        if(positions.size()>63) throw std::invalid_argument("Mask exceeds WPA's 63-byte limit");
    }
    return positions;
}
Mask prefix_mask(const std::vector<Bytes>& positions,std::size_t length) {
    Mask mask;
    mask.positions.assign(positions.begin(),positions.begin()+static_cast<std::ptrdiff_t>(length));
    for(const auto& set:mask.positions) {
        if(mask.size>std::numeric_limits<std::uint64_t>::max()/set.size())
            throw std::invalid_argument("Mask keyspace exceeds 64-bit indexing; split it into smaller masks");
        mask.size*=set.size();
    }
    return mask;
}
void validate_increment(const CandidateConfig& c) {
    if(!c.increment) {
        if(c.increment_min || c.increment_max) throw std::invalid_argument("Increment bounds require --increment");
        return;
    }
    if(c.masks.empty() && c.mask_files.empty()) throw std::invalid_argument("--increment requires a mask or mask file");
    const std::size_t floor=c.wordlists.empty()?8:1;
    const auto minimum=c.increment_min.value_or(floor),maximum=c.increment_max.value_or(63);
    if(minimum<floor || maximum>63 || minimum>maximum)
        throw std::invalid_argument("Increment range must be ordered within 8..63 mask bytes (1..63 for hybrids)");
}
std::vector<Mask> candidate_masks(const std::string& text,const CandidateConfig& c) {
    const auto positions=mask_positions(text,c.custom);
    if(!c.increment) return {prefix_mask(positions,positions.size())};
    const auto minimum=c.increment_min.value_or(c.wordlists.empty()?8:1);
    const auto maximum=std::min(c.increment_max.value_or(positions.size()),positions.size());
    if(minimum>maximum) throw std::invalid_argument("Mask is shorter than --increment-min");
    std::vector<Mask> masks;
    for(auto length=minimum;length<=maximum;++length) masks.push_back(prefix_mask(positions,length));
    return masks;
}
}
Mask compile_mask(const std::string& text,const std::array<std::string,4>& custom) {
    const auto positions=mask_positions(text,custom);
    return prefix_mask(positions,positions.size());
}
Bytes Mask::at(std::uint64_t index) const {
    if(index>=size) throw std::out_of_range("Mask index outside keyspace");
    Bytes out(positions.size());
    for(std::size_t i=positions.size();i>0;--i) {
        const auto& set=positions[i-1]; out[i-1]=set[static_cast<std::size_t>(index%set.size())]; index/=set.size();
    }
    return out;
}
struct Candidates::Impl {
    CandidateConfig config;
    std::vector<Mask> masks;
    std::vector<std::unique_ptr<Input>> words,mask_files;
    std::vector<std::string> hashes;
    std::uint64_t mask_count=0,index=0,mask_index=0;
    std::size_t word_file=0;
    bool first_line=true,have_word=false;
    Bytes word;
    std::array<std::uint64_t,65> lengths{};
    std::optional<std::uint64_t> raw_total=0,valid_count=0;
    std::uint64_t valid_index=0;
    void decode_word(Bytes& line,bool large,bool first,const std::filesystem::path& path) const {
        if(first) bom(line);
        if(config.hexadecimal) {
            if(large || line.size()>126) { line.assign(64,0); return; }
            try { line=unhex(std::string_view(reinterpret_cast<const char*>(line.data()),line.size())); }
            catch(const std::exception&) { throw std::invalid_argument("Invalid hexadecimal wordlist line in "+path.u8string()); }
        }
        if(large || line.size()>63) line.assign(64,0);
    }
    std::uint64_t valid_masks(std::size_t length,std::uint64_t count) const {
        std::uint64_t valid=0;
        for(const auto& mask:masks) {
            const auto n=std::min(count,mask.size);
            if(length+mask.positions.size()>=8 && length+mask.positions.size()<=63) valid+=n;
            count-=n; if(!count) break;
        }
        return valid;
    }
    explicit Impl(const CandidateConfig& c,const Scan& scan):config(c) {
        if(c.hybrid!="append" && c.hybrid!="prepend") throw std::invalid_argument("--hybrid must be append or prepend");
        validate_increment(c);
        auto add=[&](const std::string& text) {
            for(auto& m:candidate_masks(text,c)) {
                if(c.wordlists.empty() && m.positions.size()<8) throw std::invalid_argument("A standalone WPA mask must contain at least eight bytes");
                if(mask_count>std::numeric_limits<std::uint64_t>::max()-m.size) throw std::invalid_argument("Combined mask keyspace exceeds 64-bit indexing");
                mask_count+=m.size; masks.push_back(std::move(m));
                if(masks.size()>10000) throw std::invalid_argument("Too many expanded masks (limit 10000)");
            }
        };
        for(const auto& text:c.masks) add(text);
        for(const auto& path:c.mask_files) {
            auto file=std::make_unique<Input>(absolute_path(path)); hashes.push_back(file->sha256());
            Bytes line; bool large=false,first=true;
            while(file->line(line,4096,large)) {
                if(first) { bom(line); first=false; }
                if(large) throw std::invalid_argument("Mask-file line exceeds 4096 bytes");
                if(line.empty() || line[0]=='#') continue;
                add(std::string(line.begin(),line.end()));
            }
            mask_files.push_back(std::move(file));
        }
        if(masks.empty()) {
            if(c.wordlists.empty()) throw std::invalid_argument("Provide --wordlist, --mask or --mask-file");
            if(!c.masks.empty() || !c.mask_files.empty()) throw std::invalid_argument("No masks in the supplied mask files");
            masks.emplace_back(); mask_count=1; // Empty suffix for dictionary-only mode.
        }
        if(c.wordlists.size()>32 || c.mask_files.size()>32) throw std::invalid_argument("At most 32 input files of each kind are supported");
        std::uint64_t scanned=0,total_bytes=0;
        for(const auto& path:c.wordlists) {
            auto file=std::make_unique<Input>(absolute_path(path));
            if(file->size()>std::numeric_limits<std::uint64_t>::max()-total_bytes)
                throw std::invalid_argument("Combined wordlist size exceeds 64-bit indexing");
            total_bytes+=file->size();
            words.push_back(std::move(file));
        }
        using Clock=std::chrono::steady_clock;
        const auto started=Clock::now(); auto reported=started;
        if(scan && !words.empty()) scan(0,total_bytes,0.0);
        for(const auto& file:words) {
            // Count lengths during the existing identity hash pass; never expand masks.
            Bytes line; bool large=false,first=true,any=false;
            const std::size_t maximum=c.hexadecimal?132:66;
            auto finish=[&] {
                if(!line.empty() && line.back()=='\r' && !large) line.pop_back();
                large=large || line.size()>maximum;
                decode_word(line,large,first,file->path);
                ++lengths[line.size()];
                line.clear(); large=false; first=false; any=false;
            };
            hashes.push_back(file->sha256([&](const std::uint8_t* data,std::size_t size) {
                for(std::size_t i=0;i<size;++i) {
                    if(data[i]=='\n') {finish(); continue;}
                    any=true;
                    if(line.size()<=maximum) line.push_back(data[i]); else large=true;
                }
                scanned+=size;
                if(scan) {
                    const auto now=Clock::now();
                    if(now-reported>=std::chrono::seconds(1)) {
                        scan(scanned,total_bytes,std::chrono::duration<double>(now-started).count());
                        reported=now;
                    }
                }
            }));
            if(any) finish();
        }
        if(scan && !words.empty()) scan(scanned,total_bytes,std::chrono::duration<double>(Clock::now()-started).count());
        if(words.empty()) {raw_total=mask_count; valid_count=mask_count;}
        else {
            auto accumulate=[](std::optional<std::uint64_t>& total,std::uint64_t n,std::uint64_t factor) {
                if(!total) return;
                if(factor && n>(std::numeric_limits<std::uint64_t>::max()-*total)/factor) total.reset();
                else *total+=n*factor;
            };
            for(std::size_t length=0;length<lengths.size();++length) {
                accumulate(raw_total,lengths[length],mask_count);
                accumulate(valid_count,lengths[length],valid_masks(length,mask_count));
            }
        }
    }
    // Sequential enumeration uses mixed-radix carry instead of dividing the
    // full 64-bit index at every character. Random seek still decodes once.
    const Mask* cached_mask=nullptr;
    std::uint64_t cached_next=0,cached_remaining=0;
    Bytes cached_value;
    std::vector<std::size_t> cached_digits;
    void mask_at(std::uint64_t i,Bytes& result) {
        const auto absolute=i;
        if(!cached_remaining || i!=cached_next) {
            cached_mask=nullptr;
            for(const auto& m:masks) {
                if(i<m.size) { cached_mask=&m; break; }
                i-=m.size;
            }
            if(!cached_mask) throw std::logic_error("Invalid combined mask offset");
            cached_remaining=cached_mask->size-i;
            cached_value.resize(cached_mask->positions.size());
            cached_digits.resize(cached_value.size());
            for(std::size_t at=cached_value.size();at>0;--at) {
                const auto& set=cached_mask->positions[at-1];
                cached_digits[at-1]=static_cast<std::size_t>(i%set.size()); i/=set.size();
                cached_value[at-1]=set[cached_digits[at-1]];
            }
        }
        result=cached_value;
        cached_next=absolute+1;
        if(--cached_remaining) {
            for(std::size_t at=cached_value.size();at>0;--at) {
                const auto& set=cached_mask->positions[at-1];
                auto& digit=cached_digits[at-1];
                if(++digit<set.size()) { cached_value[at-1]=set[digit]; break; }
                digit=0; cached_value[at-1]=set[0];
            }
        }
    }
    bool read_word() {
        bool large=false;
        while(word_file<words.size()) {
            // Retain the word's storage across lines. Allocating for every byte-length growth
            // leaves the GPU idle between large dictionary batches.
            if(!words[word_file]->line(word,config.hexadecimal?132:66,large)) { ++word_file; first_line=true; continue; }
            decode_word(word,large,first_line,words[word_file]->path);
            first_line=false;
            return true;
        }
        return false;
    }
};
Candidates::Candidates(const CandidateConfig& c,const Scan& scan):impl_(std::make_unique<Impl>(c,scan)) {}
Candidates::~Candidates()=default;
std::uint64_t Candidates::position() const { return impl_->index; }
std::optional<std::uint64_t> Candidates::keyspace() const {
    return impl_->raw_total;
}
std::optional<std::uint64_t> Candidates::valid_total() const {return impl_->valid_count;}
std::uint64_t Candidates::valid_position() const {return impl_->valid_index;}
bool Candidates::next(Bytes& password) {
    auto& p=*impl_;
    if(p.words.empty()) {
        if(p.index==p.mask_count) return false;
        p.mask_at(p.index++,password); ++p.valid_index; return true;
    }
    if(!p.have_word) { if(!p.read_word()) return false; p.have_word=true; }
    if(p.index==std::numeric_limits<std::uint64_t>::max()) throw std::invalid_argument("Candidate offset exceeds 64-bit indexing");
    Bytes mask;
    if(!p.masks[0].positions.empty()) p.mask_at(p.mask_index,mask);
    if(p.word.size()+mask.size()>63) password=Bytes(64,0);
    else if(p.config.hybrid=="append") { password=p.word; password.insert(password.end(),mask.begin(),mask.end()); }
    else { password=std::move(mask); password.insert(password.end(),p.word.begin(),p.word.end()); }
    ++p.index;
    if(password.size()>=8 && password.size()<=63) ++p.valid_index;
    if(++p.mask_index==p.mask_count) { p.mask_index=0; p.have_word=false; }
    return true;
}
void Candidates::seek(std::uint64_t index) {
    auto& p=*impl_; p.word_file=0; p.first_line=true; p.have_word=false; p.mask_index=0; p.index=0; p.valid_index=0;
    p.cached_remaining=0;
    for(auto& word:p.words) word->rewind();
    if(p.words.empty()) {
        if(index>p.mask_count) throw std::invalid_argument("Start offset exceeds mask keyspace");
        p.valid_index=index;
    } else {
        auto lines=index/p.mask_count;
        for(std::uint64_t i=0;i<lines;++i) {
            if(!p.read_word()) throw std::invalid_argument("Start offset exceeds wordlist keyspace");
            p.valid_index+=p.valid_masks(p.word.size(),p.mask_count);
        }
        if(index%p.mask_count) {
            if(!p.read_word()) throw std::invalid_argument("Start offset exceeds wordlist keyspace");
            p.have_word=true; p.mask_index=index%p.mask_count;
            p.valid_index+=p.valid_masks(p.word.size(),p.mask_index);
        }
    }
    p.index=index;
}
std::string Candidates::identity() const {
    auto& p=*impl_;
    std::string text="candidates-v1\n"+p.config.hybrid+"\n"+(p.config.hexadecimal?"hex\n":"raw\n");
    for(const auto& file:p.mask_files) text+="maskfile:"+hex_bytes(bytes(file->path.u8string()))+"\n";
    for(const auto& file:p.words) text+="wordlist:"+hex_bytes(bytes(file->path.u8string()))+"\n";
    for(const auto& h:p.hashes) text+="sha256:"+h+"\n";
    for(const auto& mask:p.masks) {
        text+="mask:";
        for(const auto& set:mask.positions) text+=hex_bytes(set)+"/";
        text+='\n';
    }
    return hex_bytes(digest(Algorithm::Sha256,bytes(text)));
}
std::vector<std::filesystem::path> Candidates::input_paths() const {
    std::vector<std::filesystem::path> out;
    for(const auto& f:impl_->words) out.push_back(f->path);
    for(const auto& f:impl_->mask_files) out.push_back(f->path);
    return out;
}
}
