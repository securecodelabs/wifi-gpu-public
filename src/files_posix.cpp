#include "files.hpp"
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <array>
#include <algorithm>
#include <stdexcept>

namespace wifi {
namespace {
[[noreturn]] void fail(const char* action,const std::filesystem::path& path) {
    const int error=errno;
    throw std::runtime_error(std::string(action)+": "+path.string()+" ("+std::strerror(error)+")");
}
struct Fd {
    int value=-1;
    ~Fd() { if(value>=0) close(value); }
};
}
std::filesystem::path absolute_path(const std::string& path) {
    if(path.empty()) throw std::invalid_argument("Empty file path");
    return std::filesystem::absolute(std::filesystem::u8path(path)).lexically_normal();
}
struct Input::Impl {
    Fd fd;
    struct stat initial{};
    std::array<std::uint8_t,65536> buffer{};
    std::size_t position=0,length=0;
    void unchanged(const std::filesystem::path& path) {
        struct stat now{},named{};
        if(fstat(fd.value,&now)<0 || stat(path.c_str(),&named)<0) fail("Cannot inspect input",path);
        if(now.st_dev!=named.st_dev || now.st_ino!=named.st_ino || now.st_size!=initial.st_size ||
           now.st_mtim.tv_sec!=initial.st_mtim.tv_sec || now.st_mtim.tv_nsec!=initial.st_mtim.tv_nsec ||
           now.st_ctim.tv_sec!=initial.st_ctim.tv_sec || now.st_ctim.tv_nsec!=initial.st_ctim.tv_nsec)
            throw std::runtime_error("Input changed during recovery: "+path.string());
    }
    std::size_t read_buffer(const std::filesystem::path& path) {
        unchanged(path);
        ssize_t n;
        do { n=read(fd.value,buffer.data(),buffer.size()); } while(n<0 && errno==EINTR);
        if(n<0) fail("Cannot read input",path);
        unchanged(path); return static_cast<std::size_t>(n);
    }
};
Input::Input(const std::filesystem::path& p):path(p),impl_(std::make_unique<Impl>()) {
    impl_->fd.value=open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NONBLOCK);
    if(impl_->fd.value<0) fail("Cannot open input",path);
    if(fstat(impl_->fd.value,&impl_->initial)<0) fail("Cannot inspect input",path);
    if(!S_ISREG(impl_->initial.st_mode)) throw std::invalid_argument("Input must be a regular file: "+path.string());
    if(flock(impl_->fd.value,LOCK_SH|LOCK_NB)<0) fail("Cannot lock input",path);
}
Input::~Input()=default;
std::uint64_t Input::size() const { return static_cast<std::uint64_t>(impl_->initial.st_size); }
void Input::rewind() {
    impl_->unchanged(path);
    if(lseek(impl_->fd.value,0,SEEK_SET)<0) fail("Cannot rewind input",path);
    impl_->position=impl_->length=0;
}
bool Input::line(Bytes& out,std::size_t maximum,bool& oversized) {
    out.clear(); oversized=false; bool any=false;
    while(true) {
        if(impl_->position==impl_->length) {
            impl_->length=impl_->read_buffer(path); impl_->position=0;
            if(!impl_->length) break;
        }
        const auto* begin=impl_->buffer.data()+impl_->position;
        const auto available=impl_->length-impl_->position;
        const auto* newline=static_cast<const std::uint8_t*>(std::memchr(begin,'\n',available));
        const auto size=newline?static_cast<std::size_t>(newline-begin):available;
        const auto keep=std::min(size,maximum+1-out.size());
        out.insert(out.end(),begin,begin+keep);
        oversized=oversized || keep<size; any=true;
        impl_->position+=size+(newline?1:0);
        if(newline) break;
    }
    if(!out.empty() && out.back()=='\r' && !oversized) out.pop_back();
    if(out.size()>maximum) oversized=true;
    return any;
}
std::string Input::sha256(const std::function<void(const std::uint8_t*,std::size_t)>& inspect) {
    rewind(); Hash hash(Algorithm::Sha256);
    while(auto n=impl_->read_buffer(path)) {
        hash.update(impl_->buffer.data(),n);
        if(inspect) inspect(impl_->buffer.data(),n);
    }
    rewind(); return hex_bytes(hash.finish());
}
std::string Input::contents(std::size_t maximum) {
    rewind(); std::string out;
    while(auto n=impl_->read_buffer(path)) {
        if(n>maximum-out.size()) throw std::invalid_argument("Input exceeds size limit: "+path.string());
        out.append(reinterpret_cast<const char*>(impl_->buffer.data()),n);
    }
    rewind(); return out;
}
struct Snapshot::Impl { Fd lock; bool replace=false; };
Snapshot::Snapshot(const std::filesystem::path& p,bool replace):path(p),impl_(std::make_unique<Impl>()) {
    auto lock=path; lock+=".lock";
    impl_->lock.value=open(lock.c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600);
    if(impl_->lock.value<0) fail("Cannot open snapshot lock",lock);
    struct stat info{};
    if(fstat(impl_->lock.value,&info)<0) fail("Cannot inspect snapshot lock",lock);
    if(!S_ISREG(info.st_mode) || info.st_size!=0 || info.st_nlink!=1)
        throw std::runtime_error("Invalid existing snapshot lock: "+lock.string());
    if(flock(impl_->lock.value,LOCK_EX|LOCK_NB)<0) fail("Cannot acquire output/session lock",lock);
    if(std::filesystem::exists(path) && !replace) throw std::invalid_argument("Refusing to overwrite "+path.string()+"; use --overwrite or --resume as appropriate");
    impl_->replace=replace;
}
// Keep the zero-byte sidecar inode: unlinking an advisory lock creates a race
// where waiting/new writers lock different inodes. flock releases on crash too.
Snapshot::~Snapshot()=default;
void Snapshot::write(const std::string& contents) {
    std::string temp=path.string()+".tmp.XXXXXX";
    Fd fd; fd.value=mkostemp(temp.data(),O_CLOEXEC);
    if(fd.value<0) fail("Cannot create snapshot temporary file",path);
    try {
        for(std::size_t done=0;done<contents.size();) {
            const auto n=::write(fd.value,contents.data()+done,std::min<std::size_t>(contents.size()-done,1048576));
            if(n<0 && errno==EINTR) continue;
            if(n<=0) fail("Cannot write snapshot",path);
            done+=static_cast<std::size_t>(n);
        }
        if(fsync(fd.value)<0) fail("Cannot flush snapshot",path);
        if(impl_->replace) {
            if(rename(temp.c_str(),path.c_str())<0) fail("Cannot replace snapshot",path);
        } else {
            // link is atomic and never clobbers an existing destination.
            if(link(temp.c_str(),path.c_str())<0) fail("Cannot publish snapshot without overwrite",path);
            if(unlink(temp.c_str())<0) fail("Cannot remove snapshot temporary link",path);
        }
        Fd directory; directory.value=open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        if(directory.value<0 || fsync(directory.value)<0) fail("Cannot flush snapshot directory",path);
        impl_->replace=true;
    } catch(...) { unlink(temp.c_str()); throw; }
}
}
