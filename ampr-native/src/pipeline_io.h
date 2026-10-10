// SPDX-License-Identifier: GPL-3.0-or-later
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#endif
// Bounded software I/O tuning, independent of the archive format.
struct PipelineIo {
 uint64_t packRead=4*1048576,looseRead=8*1048576,sourceRead=8*1048576,archiveRead=8*1048576,verifyBatch=8*1048576,writeQueue=16*1048576,readerCache=32*1048576;
 unsigned packJobBlocks=4;
};
inline PipelineIo pipelineIo;
inline PipelineIo legacyPipelineIo(){return {2*1048576,2*1048576,2*1048576,4*1048576,2*1048576,4*1048576,36*1048576,1};}
inline std::atomic<bool> benchmarkDirectReads{false};
struct DirectReadStats {std::atomic<uint64_t> attempts{0},accepted{0},rejected{0},completed{0},fallbacks{0},bytes{0};std::atomic<int> lastError{0};void reset(){attempts=0;accepted=0;rejected=0;completed=0;fallbacks=0;bytes=0;lastError=0;}};
inline DirectReadStats directReadStats;
#if defined(O_DIRECT)
inline constexpr int pipelineDirectFlag=O_DIRECT;
#elif defined(__FreeBSD__)
// SDK sys/fcntl.h defines O_DIRECT as 0x00010000 behind __BSD_VISIBLE.
inline constexpr int pipelineDirectFlag=0x00010000;
#else
inline constexpr int pipelineDirectFlag=0;
#endif
// Ordinary conversion uses buffered reads. Timed benchmarks may request
// aligned direct reads; unsupported opens/reads fall back visibly and safely.
class PipelineInput {
 int fd=-1;uint64_t position=0;std::streamsize count=0;bool valid=false,direct=false;void*aligned=nullptr;size_t allocated=0;
public:
 PipelineInput()=default;
 explicit PipelineInput(const fs::path&p,std::ios::openmode=std::ios::binary){open(p);}
 PipelineInput(const PipelineInput&)=delete;PipelineInput&operator=(const PipelineInput&)=delete;
 ~PipelineInput(){if(aligned)std::free(aligned);if(fd>=0){
#ifdef _WIN32
 _close(fd);
#else
 ::close(fd);
#endif
 }}
 void open(const fs::path&p,std::ios::openmode=std::ios::binary){
#ifdef _WIN32
 fd=_wopen(p.c_str(),_O_RDONLY|_O_BINARY);
#else
 int flags=O_RDONLY;
 if(benchmarkDirectReads&&pipelineDirectFlag){directReadStats.attempts++;fd=::open(p.c_str(),flags|pipelineDirectFlag);if(fd>=0){int observed=fcntl(fd,F_GETFL);direct=observed>=0&&(observed&pipelineDirectFlag);if(direct)directReadStats.accepted++;else directReadStats.rejected++;}else{directReadStats.lastError=errno;directReadStats.rejected++;}}
 if(fd<0)fd=::open(p.c_str(),flags);
#endif
 valid=fd>=0;
 }
 explicit operator bool()const{return valid;}
 size_t cacheMemory()const{return allocated;}
 void clear(){valid=fd>=0;}
 void seekg(uint64_t offset){position=offset;count=0;valid=fd>=0&&offset<=uint64_t(INT64_MAX);}
 void read(char*out,std::streamsize requested){count=0;if(!valid||requested<0)return;size_t n=size_t(requested);
#ifndef _WIN32
 if(direct&&n){constexpr uint64_t alignment=65536;auto start=position/alignment*alignment;size_t skip=size_t(position-start),wanted=(skip+n+alignment-1)/alignment*alignment;if(wanted>allocated){if(aligned)std::free(aligned);aligned=nullptr;allocated=0;if(posix_memalign(&aligned,alignment,wanted)){valid=false;return;}allocated=wanted;}ssize_t got;do{got=::pread(fd,aligned,wanted,off_t(start));}while(got<0&&errno==EINTR);
 if(got>=0&&size_t(got)>=skip){count=std::streamsize(std::min(n,size_t(got)-skip));if(count)memcpy(out,(char*)aligned+skip,size_t(count));position+=uint64_t(count);directReadStats.completed++;directReadStats.bytes+=size_t(got);return;}
 directReadStats.lastError=got<0?errno:0;directReadStats.fallbacks++;
 int flags=fcntl(fd,F_GETFL);if(flags<0||fcntl(fd,F_SETFL,flags&~pipelineDirectFlag)<0){valid=false;return;}
 direct=false;
 }
#endif
 while(size_t(count)<n){
#ifdef _WIN32
 if(_lseeki64(fd,int64_t(position),SEEK_SET)<0){valid=false;return;}auto got=_read(fd,out+count,unsigned(std::min<size_t>(n-size_t(count),8*1048576)));
#else
 auto got=::pread(fd,out+count,n-size_t(count),off_t(position));
#endif
 if(got<0){if(errno==EINTR)continue;valid=false;return;}if(!got)break;count+=got;position+=uint64_t(got);
 }
 }
 std::streamsize gcount()const{return count;}
 int peek(){char byte;auto saved=position;read(&byte,1);auto value=count?int(static_cast<unsigned char>(byte)):EOF;position=saved;return value;}
};
