// Test-only LD_PRELOAD hook: hold/fail the first nonzero session checkpoint.
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int fsync(int fd) {
    using Flush=int (*)(int);
    static const auto flush=reinterpret_cast<Flush>(dlsym(RTLD_NEXT,"fsync"));
    if(!flush) { errno=ENOSYS; return -1; }
    const auto* prefix=std::getenv("WIFI_TEST_CHECKPOINT_PREFIX");
    const auto* gate=std::getenv("WIFI_TEST_CHECKPOINT_GATE");
    const auto* marker=std::getenv("WIFI_TEST_CHECKPOINT_MARKER");
    char link[64]{},path[4096]{},data[1024]{};
    std::snprintf(link,sizeof(link),"/proc/self/fd/%d",fd);
    const auto size=readlink(link,path,sizeof(path)-1);
    if(prefix && gate && marker && size>0 && std::strncmp(path,prefix,std::strlen(prefix))==0 &&
       pread(fd,data,sizeof(data)-1,0)>0 && std::strstr(data,"\nnext\t") && !std::strstr(data,"\nnext\t0\n")) {
        const int signal=open(marker,O_WRONLY|O_CREAT|O_CLOEXEC,0600);
        if(signal>=0) close(signal);
        for(unsigned attempt=0;attempt<3000;++attempt) {
            char mode[16]{};
            const int control=open(gate,O_RDONLY|O_CLOEXEC);
            if(control>=0) { const auto ignored=read(control,mode,sizeof(mode)-1); (void)ignored; close(control); }
            if(std::strncmp(mode,"fail",4)==0) { errno=EIO; return -1; }
            if(std::strncmp(mode,"hold",4)!=0) return flush(fd);
            usleep(10000);
        }
        errno=ETIMEDOUT; return -1;
    }
    return flush(fd);
}
