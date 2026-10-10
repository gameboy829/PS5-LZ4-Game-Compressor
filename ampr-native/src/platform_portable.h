// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cerrno>
#include <fcntl.h>
#include <limits>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
#include "portable_sha256.h"
bool portableUtf8(const std::string& s){size_t i=0;while(i<s.size()){unsigned char c=s[i++];if(c<128)continue;unsigned n;uint32_t value,min;
    if(c>=0xc2&&c<=0xdf){n=1;value=c&31;min=128;}else if(c>=0xe0&&c<=0xef){n=2;value=c&15;min=2048;}else if(c>=0xf0&&c<=0xf4){n=3;value=c&7;min=65536;}else return false;
    if(n>s.size()-i)return false;while(n--){unsigned char next=s[i++];if((next&0xc0)!=0x80)return false;value=(value<<6)|(next&63);}if(value<min||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;}return true;}
int64_t portableMtime(const fs::path& path){
#ifdef _WIN32
    struct _stat64 st{};check(_wstat64(path.c_str(),&st)==0,"cannot stat source");
#else
    struct stat st{};check(::stat(path.c_str(),&st)==0,"cannot stat source");
#endif
    return st.st_mtime;
}
class VolumeOutput {
    Bytes buffered;static constexpr size_t writeBatch=1024*1024;
    void flushBuffer(){if(!buffered.empty()){directBytes(buffered.data(),buffered.size());buffered.clear();}}

    int descriptor=-1;fs::path path;uint64_t offset=0;
    void fail(const char* action,int code){throw std::runtime_error(std::string(action)+": "+path.u8string()+" at byte "+std::to_string(offset)+"; errno "+std::to_string(code)+": "+strerror(code));}
public:
    ~VolumeOutput(){if(descriptor>=0){
#ifdef _WIN32
        _close(descriptor);
#else
        ::close(descriptor);
#endif
    }}
    void open(const fs::path& p){path=p;
#ifdef _WIN32
        descriptor=_wopen(p.c_str(),_O_CREAT|_O_EXCL|_O_RDWR|_O_BINARY,_S_IREAD|_S_IWRITE);
#else
        descriptor=::open(p.c_str(),O_CREAT|O_EXCL|O_RDWR,0644);
#endif
        if(descriptor<0)fail("cannot create volume",errno);
#ifndef _WIN32
        // The game runs under a different user than the packing payload.
        // Set the final permissions explicitly even with a restrictive umask.
        if(::fchmod(descriptor,0644))fail("cannot make volume game-readable",errno);
#endif
    }
    void directBytes(const void* p,size_t size){IoMeasure timer(ioMetrics.writeNs);ioMetrics.writeBytes+=size;auto bytes=static_cast<const unsigned char*>(p);while(size){auto n=std::min<size_t>(size,1024*1024);
#ifdef _WIN32
        auto written=_write(descriptor,bytes,unsigned(n));
#else
        auto written=::write(descriptor,bytes,n);
#endif
        if(written<0){if(errno==EINTR)continue;fail("output write failed",errno);}if(!written)fail("output write stalled",EIO);offset+=written;bytes+=written;size-=written;}}
    void bytes(const void* p,size_t n){auto data=static_cast<const unsigned char*>(p);while(n){auto take=std::min(n,writeBatch-buffered.size());buffered.insert(buffered.end(),data,data+take);data+=take;n-=take;if(buffered.size()==writeBatch)flushBuffer();}}
    void seekp(uint64_t position){flushBuffer();check(position<=uint64_t(std::numeric_limits<int64_t>::max()),"offset exceeds signed 64-bit range");
#ifdef _WIN32
        auto result=_lseeki64(descriptor,int64_t(position),SEEK_SET);
#else
        static_assert(sizeof(off_t)>=8,"64-bit offsets required");auto result=::lseek(descriptor,off_t(position),SEEK_SET);
#endif
        if(result<0)fail("output seek failed",errno);offset=position;
    }
    void flush(){flushBuffer();}
    void releaseBuffer(){flushBuffer();Bytes{}.swap(buffered);}
    void close(){flushBuffer();
#ifdef _WIN32
        if(_commit(descriptor))fail("output flush failed",errno);auto result=_close(descriptor);
#else
        if(::fsync(descriptor))fail("output flush failed",errno);auto result=::close(descriptor);
#endif
        descriptor=-1;if(result)fail("output close failed",errno);
    }
};
