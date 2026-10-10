// SPDX-License-Identifier: GPL-3.0-or-later
// Native engine using the GPL-licensed ampr_emu format and packing semantics.
#if defined(_WIN32) && !defined(AMPR_PORTABLE_BACKEND)
#include <windows.h>
#include <bcrypt.h>
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <csignal>
#include <sys/stat.h>
#include <functional>
#include <atomic>
#include <array>
#include "ampr_emu_pack_format.h"
#include "lz4.h"
#include "lz4hc.h"
#include "profile.h"
#include "io_metrics.h"
namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
std::atomic<int> cancelled{0};
std::function<void(uint64_t,uint64_t)> progressHook;
std::function<bool()> cancellationHook;
std::function<void(size_t,const AmprPackFileRecord&,const std::vector<AmprPackChunkRecord>&,const std::vector<uint32_t>&)> fileCompleteHook;
void stop(int) { cancelled = 1; }
void check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
void ensureActive(){if(cancellationHook&&cancellationHook())cancelled=1;check(!cancelled,"cancelled");}
uint32_t crc(const void* ptr, size_t size) {
    static const auto table=[](){std::array<uint32_t,256> t{};for(uint32_t i=0;i<256;i++){auto c=i;for(int bit=0;bit<8;bit++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));t[i]=c;}return t;}();
    uint32_t c=~0u;auto p=static_cast<const unsigned char*>(ptr);while(size--)c=(c>>8)^table[(c^*p++)&255];return ~c;
}
uint64_t align(uint64_t x, uint64_t n) { return (x+n-1)&~(n-1); }
template<class T> void append(Bytes& b, const T& x) {
    auto p = reinterpret_cast<const unsigned char*>(&x); b.insert(b.end(),p,p+sizeof(T));
}
template<class T> Bytes records(const std::vector<T>& v) { Bytes b; for(auto& x:v) append(b,x); return b; }
Bytes read(const fs::path& p) {
    auto size = fs::file_size(p); check(size <= 512ull*1024*1024,"metadata exceeds 512 MiB limit");
    Bytes b(static_cast<size_t>(size)); std::ifstream f(p,std::ios::binary);
    if(size) f.read(reinterpret_cast<char*>(b.data()),b.size()); check(bool(f),"cannot read metadata"); return b;
}
template<class T> T at(const Bytes& b, uint64_t offset) {
    check(offset<=b.size() && sizeof(T)<=b.size()-offset,"truncated record"); T x; memcpy(&x,b.data()+offset,sizeof x); return x;
}
std::string writingSource;
#if defined(AMPR_PORTABLE_BACKEND) || !defined(_WIN32)
#include "platform_portable.h"
#else
class VolumeOutput {
    Bytes buffered;static constexpr size_t writeBatch=1024*1024;
    void flushBuffer(){if(!buffered.empty()){directBytes(buffered.data(),buffered.size());buffered.clear();}}

    HANDLE handle=INVALID_HANDLE_VALUE;fs::path path;uint64_t offset=0;
    void fail(const char* action,DWORD code) {
        LPWSTR message=nullptr;FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,(LPWSTR)&message,0,nullptr);
        std::string detail=message?fs::path(message).u8string():"unknown Windows error";if(message)LocalFree(message);
        throw std::runtime_error(std::string(action)+": "+path.u8string()+" at byte "+std::to_string(offset)+"; Windows error "+std::to_string(code)+": "+detail);
    }
public:
    ~VolumeOutput(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
    void open(const fs::path& p){path=p;handle=CreateFileW(p.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(handle==INVALID_HANDLE_VALUE)fail("cannot create volume",GetLastError());}
    void directBytes(const void* p,size_t n){IoMeasure timer(ioMetrics.writeNs);ioMetrics.writeBytes+=n;auto data=(const unsigned char*)p;while(n){DWORD size=DWORD(std::min<size_t>(n,1024*1024)),done=0;if(!WriteFile(handle,data,size,&done,nullptr))fail("output write failed",GetLastError());if(!done)fail("output write stalled",ERROR_WRITE_FAULT);offset+=done;data+=done;n-=done;}}
    void bytes(const void* p,size_t n){auto data=static_cast<const unsigned char*>(p);while(n){auto take=std::min(n,writeBatch-buffered.size());buffered.insert(buffered.end(),data,data+take);data+=take;n-=take;if(buffered.size()==writeBatch)flushBuffer();}}
    void seekp(uint64_t pos){flushBuffer();LARGE_INTEGER at;at.QuadPart=pos;if(!SetFilePointerEx(handle,at,nullptr,FILE_BEGIN))fail("output seek failed",GetLastError());offset=pos;}
    void flush(){flushBuffer();}
    void releaseBuffer(){flushBuffer();Bytes{}.swap(buffered);}
    void close(){flushBuffer();if(!FlushFileBuffers(handle))fail("output flush failed",GetLastError());CloseHandle(handle);handle=INVALID_HANDLE_VALUE;}
};
#endif
void write(VolumeOutput& f,const void* p,size_t n){f.bytes(p,n);}
void zeros(VolumeOutput& f,uint64_t n){static constexpr char buf[65536]{};while(n){auto k=std::min<uint64_t>(n,sizeof buf);write(f,buf,k);n-=k;}}
void write(std::ostream& f,const void* p,size_t n) { if(n) f.write(static_cast<const char*>(p),n); check(bool(f),"metadata output write failed"); }
void zeros(std::ostream& f,uint64_t n) { static constexpr char buf[65536]{}; while(n) { auto k=std::min<uint64_t>(n,sizeof buf); write(f,buf,k); n-=k; } }
#if defined(_WIN32) && !defined(AMPR_PORTABLE_BACKEND)
struct Sha {
    BCRYPT_ALG_HANDLE alg{}; BCRYPT_HASH_HANDLE hash{};
    Sha() { check(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0,"SHA provider failed"); check(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)==0,"SHA init failed"); }
    ~Sha() { if(hash) BCryptDestroyHash(hash); if(alg) BCryptCloseAlgorithmProvider(alg,0); }
    void add(const void* p,size_t n) { check(n<=ULONG_MAX,"hash input too large"); check(BCryptHashData(hash,(PUCHAR)p,(ULONG)n,0)==0,"SHA failed"); }
    Bytes finish() { Bytes b(32); check(BCryptFinishHash(hash,b.data(),32,0)==0,"SHA finish failed"); return b; }
};
#endif
uint64_t pathhash(std::string s) { uint64_t h=0xcbf29ce484222325ull; for(unsigned char c:s) { if(c>='A'&&c<='Z') c+=32; h=(h^c)*0x100000001b3ull; } return h?h:1; }
uint64_t indexhash(std::string s) { uint64_t h=1469598103934665603ull; for(unsigned char c:s) { if(c>='A'&&c<='Z') c+=32; h=(h^c)*0x100000001b3ull; } return h?h:1; }
fs::path safe(const fs::path& root,const std::string& rel) {
    check(!rel.empty() && rel.find('\\')==std::string::npos && rel.find(':')==std::string::npos && rel.front()!='/' && rel.back()!='/',"unsafe path");
    fs::path p=fs::u8path(rel); for(auto part:p) check(part!=".." && part!="." && !part.empty(),"unsafe path component");
    auto result=root/p; auto r=fs::weakly_canonical(root); auto c=fs::weakly_canonical(result);
    auto ri=r.begin(),ci=c.begin(); for(;ri!=r.end();++ri,++ci) check(ci!=c.end() && *ri==*ci,"path escapes root");
    return result;
}
std::string stringat(const Bytes& b,uint64_t off,uint64_t len) {
    check(off<b.size() && len<b.size()-off && b[off+len]==0,"invalid terminated string");
    check(std::find(b.begin()+off,b.begin()+off+len,0)==b.begin()+off+len,"embedded NUL");
    std::string s((char*)b.data()+off,size_t(len));
#if defined(_WIN32) && !defined(AMPR_PORTABLE_BACKEND)
    check(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0)>0,"invalid UTF-8");
#else
    check(portableUtf8(s),"invalid UTF-8");
#endif
    return s;
}
#pragma pack(push,1)
struct SourceHeader { char magic[8]; uint32_t version,entrySize; uint64_t count,pathBytes,hashOffset; uint32_t slotSize,slotCount; };
struct SourceEntry { uint32_t offset,length; uint64_t size; int64_t mtime; };
struct CrcHeader { char magic[8]; uint32_t version,size; unsigned char id[16]; uint64_t count; uint32_t payload,header; };
#pragma pack(pop)
struct Entry { std::string path; uint64_t size; int64_t mtime; };
std::vector<Entry> sourceindex(const fs::path& p) {
    auto b=read(p); auto h=at<SourceHeader>(b,0);
    check(!memcmp(h.magic,"AMPRIDX3",8)&&h.version==3&&h.entrySize==24,"unsupported source index");
    check(h.count<=0xfffffffe && h.count<=(b.size()-sizeof h)/24,"invalid source count");
    uint64_t strings=sizeof h+h.count*24; check(h.pathBytes<=b.size()-strings,"truncated source strings");
    check(h.hashOffset>=strings+h.pathBytes && h.hashOffset<=b.size(),"invalid source hash offset");
    check(!h.slotCount || (h.slotSize==16 && !(h.slotCount&(h.slotCount-1)) && uint64_t(h.slotCount)*16==b.size()-h.hashOffset),"invalid source hash table");
    Bytes blob(b.begin()+strings,b.begin()+strings+h.pathBytes); std::vector<Entry> entries; std::set<std::string> seen;
    for(uint64_t i=0;i<h.count;i++) { auto e=at<SourceEntry>(b,sizeof h+i*24); auto path=stringat(blob,e.offset,e.length);
        check(path.rfind("/app0/",0)==0,"index path outside /app0"); safe(fs::current_path(),path.substr(6));
        auto folded=path; for(char& c:folded) if(c>='A'&&c<='Z')c+=32; check(seen.insert(folded).second,"duplicate source path");
        entries.push_back({path,e.size,e.mtime}); }
    if(h.slotCount) {
        std::set<uint32_t> ids;
        for(uint32_t i=0;i<h.slotCount;i++) { auto off=h.hashOffset+uint64_t(i)*16; auto hash=at<uint64_t>(b,off); auto id=at<uint32_t>(b,off+8); auto flags=at<uint32_t>(b,off+12);
            check(!flags,"duplicate/colliding hash slots unsupported");
            if(!id) { check(!hash,"nonempty vacant hash slot"); continue; }
            check(id<=entries.size() && (hash==indexhash(entries[id-1].path)||hash==pathhash(entries[id-1].path)) && ids.insert(id).second,"invalid source hash slot");
            uint32_t probe=uint32_t(hash)&(h.slotCount-1),steps=0;
            while(probe!=i) { check(at<uint32_t>(b,h.hashOffset+uint64_t(probe)*16+8)!=0 && ++steps<h.slotCount,"invalid source hash probe chain"); probe=(probe+1)&(h.slotCount-1); }
        }
        check(ids.size()==entries.size(),"source hash table missing entries");
    }
    return entries;
}
void pack(const fs::path& root,const fs::path& index,const fs::path& out,bool fast,int level) {
    check(fs::is_directory(root),"source root missing"); check(!fs::exists(out),"destination must not exist");
    auto rr=fs::weakly_canonical(root),oo=fs::weakly_canonical(out); auto r=rr.begin(),o=oo.begin();
    for(;r!=rr.end()&&o!=oo.end()&&*r==*o;++r,++o){} check(r!=rr.end(),"destination cannot be inside source");
    auto entries=sourceindex(index); fs::create_directories(out);
    uint64_t total=0,completed=0;for(auto&e:entries)total+=e.size;if(progressHook)progressHook(0,total);
    try {
    std::vector<AmprPackFileRecord> files; std::vector<AmprPackChunkRecord> chunks; std::vector<uint32_t> checksums;
    Bytes strings; VolumeOutput data; uint64_t pos=65536; bool opened=false;
#if defined(_WIN32) && !defined(AMPR_PORTABLE_BACKEND)
    ULARGE_INTEGER available{};if(GetDiskFreeSpaceExW(out.c_str(),&available,nullptr,nullptr))std::cout<<"Output free space: "<<available.QuadPart<<" bytes\n";
    wchar_t volumePath[MAX_PATH]{},filesystemName[64]{};
    if(GetVolumePathNameW(out.c_str(),volumePath,MAX_PATH)&&GetVolumeInformationW(volumePath,nullptr,0,nullptr,nullptr,nullptr,filesystemName,64)) {
        std::cout<<"Output filesystem: "<<fs::path(filesystemName).u8string()<<"\n";
        check(_wcsicmp(filesystemName,L"FAT32")!=0,"FAT32 has a 4 GiB per-file limit; this single-volume packer requires NTFS or exFAT output");
    }
#endif
    for(auto& e:entries) {
        ensureActive(); auto relative=e.path.substr(6); auto src=safe(root,relative);writingSource=relative;
        std::cout<<"Packing "<<files.size()+1<<"/"<<entries.size()<<": "<<relative<<"\n";
        check(fs::is_regular_file(src)&&fs::file_size(src)==e.size,"source missing or size mismatch: "+relative);
        auto before=fs::last_write_time(src);
        AmprPackFileRecord f{}; f.pathHash=pathhash(e.path); f.logicalSize=e.size; f.mtime=e.mtime;
        f.pathOffset=uint32_t(strings.size()); f.pathLength=uint32_t(e.path.size()); strings.insert(strings.end(),e.path.begin(),e.path.end()); strings.push_back(0);
        bool loose=relative=="ampr_assets.index" || relative=="ampr_assets.index.crc" || relative=="ampr_assets.index.runtime" || (relative.rfind("ampr_assets-",0)==0 && relative.size()>=4 && relative.substr(relative.size()-4)==".pak");
        if(loose) { auto dst=safe(out,relative); fs::create_directories(dst.parent_path()); fs::copy_file(src,dst); files.push_back(f);if(fileCompleteHook){if(opened)data.flush();fileCompleteHook(files.size()-1,f,chunks,checksums);}completed+=e.size;if(progressHook)progressHook(completed,total);continue; }
#if defined(_WIN32) && !defined(AMPR_PORTABLE_BACKEND)
        struct _stat64 stat{}; check(_wstat64(src.c_str(),&stat)==0,"cannot stat source");
        f.mtime=stat.st_mtime;
#else
        f.mtime=portableMtime(src);
#endif
         f.flags=1; f.blockShift=16; f.firstChunk=uint32_t(chunks.size());
        Bytes inputBuffer(2*1048576);std::ifstream input;input.rdbuf()->pubsetbuf((char*)inputBuffer.data(),inputBuffer.size());input.open(src,std::ios::binary);check(bool(input),"cannot open verification source"); check(bool(input),"cannot open source");
        Bytes raw(65536),compressed(LZ4_compressBound(65536)); uint64_t left=e.size;
        while(left) {
            ensureActive(); int n=int(std::min<uint64_t>(65536,left)); input.read((char*)raw.data(),n); check(input.gcount()==n,"source truncated");
            int k=fast?LZ4_compress_fast((char*)raw.data(),(char*)compressed.data(),n,int(compressed.size()),1):LZ4_compress_HC((char*)raw.data(),(char*)compressed.data(),n,int(compressed.size()),level); check(k>0,"compression failed");
            int saved=n-k; bool use=saved>=8192 && double(saved)/n>=0.125;
            auto payload=use?compressed.data():raw.data(); uint32_t stored=use?k:n;
            if(!opened) { data.open(out/"ampr_assets-000.pak"); zeros(data,65536); opened=true; }
            auto off=align(pos,64); if(stored>=65536 || off/65536!=(off+stored-1)/65536) off=align(pos,65536);
            zeros(data,off-pos); write(data,payload,stored); pos=off+stored;
            uint32_t flags=(off/65536==(off+stored-1)/65536?4:0)|(off%65536==0?8:0);
            chunks.push_back({off,(stored-1)|(uint32_t(use)<<20)|(flags<<22)}); checksums.push_back(crc(raw.data(),n)); left-=n;
            completed+=n;if(progressHook)progressHook(completed,total);
            check(chunks.size()<=0xffffffffull,"too many chunks");
        }
        check(input.peek()==EOF && fs::file_size(src)==e.size && fs::last_write_time(src)==before,"source changed during packing");
        f.chunkCount=uint32_t(chunks.size()-f.firstChunk); files.push_back(f);if(fileCompleteHook){if(opened)data.flush();fileCompleteHook(files.size()-1,f,chunks,checksums);}std::cout<<files.size()<<"/"<<entries.size()<<" "<<relative<<"\n";
    }
    std::vector<AmprPackDataRecord> packs;
    if(opened) { auto final=align(pos,65536); zeros(data,final-pos); pos=final;
        std::string name="ampr_assets-000.pak"; auto offset=strings.size(); strings.insert(strings.end(),name.begin(),name.end()); strings.push_back(0);
        packs.push_back({pos-65536,pos,uint32_t(offset),uint32_t(name.size()),2,65536}); }
    auto fb=records(files),cb=records(chunks),pb=records(packs),crcb=records(checksums);
    Sha sha; const char domain[]="AMPRPACK4"; sha.add(domain,sizeof domain);
    std::string config=profile_json;
    auto key=config.find("\"compression_level\":9"); check(key!=std::string::npos,"invalid compiled profile"); config.replace(key,21,"\"compression_level\":"+std::to_string(level));
    if(fast) { key=config.find("\"compression_mode\":\"hc\""); check(key!=std::string::npos,"invalid compiled codec"); config.replace(key,23,"\"compression_mode\":\"fast\""); }
    Bytes cfg(config.begin(),config.end()); for(auto* part:{&cfg,&fb,&cb,&crcb,&pb,&strings}) { uint64_t n=part->size(); sha.add(&n,8); sha.add(part->data(),part->size()); }
    auto id=sha.finish();
    if(opened) { AmprPackDataHeader h{}; memcpy(h.magic,"AMPRDAT3",8); h.version=3; h.headerSize=64; h.flags=2; memcpy(h.buildId,id.data(),16); h.payloadOffset=65536; h.payloadBytes=pos-65536; h.headerCrc32=crc(&h,sizeof h); data.seekp(0); write(data,&h,sizeof h); data.close(); }
    Bytes payload=fb; payload.insert(payload.end(),cb.begin(),cb.end()); payload.insert(payload.end(),pb.begin(),pb.end()); payload.insert(payload.end(),strings.begin(),strings.end());
    AmprPackIndexHeader h{}; memcpy(h.magic,"AMPRPAK4",8); h.version=4; h.headerSize=128; h.endianMarker=0x01020304; memcpy(h.buildId,id.data(),16);
    h.fileCount=files.size(); h.chunkCount=chunks.size(); h.packCount=packs.size(); h.fileRecordSize=48; h.chunkRecordSize=12; h.packRecordSize=32;
    h.filesOffset=128; h.chunksOffset=128+fb.size(); h.packsOffset=h.chunksOffset+cb.size(); h.stringsOffset=h.packsOffset+pb.size(); h.stringsSize=strings.size(); h.payloadCrc32=crc(payload.data(),payload.size()); h.headerCrc32=crc(&h,sizeof h);
    std::ofstream manifest(out/"ampr_assets.index",std::ios::binary); write(manifest,&h,sizeof h); write(manifest,payload.data(),payload.size()); manifest.close();
    CrcHeader c{}; memcpy(c.magic,"AMPRCRC1",8); c.version=1; c.size=48; memcpy(c.id,id.data(),16); c.count=chunks.size(); c.payload=crc(crcb.data(),crcb.size()); c.header=crc(&c,sizeof c);
    std::ofstream side(out/"ampr_assets.index.crc",std::ios::binary); write(side,&c,sizeof c); write(side,crcb.data(),crcb.size()); side.close();
    fs::copy_file(index,out/"ampr_emu.index");
    std::cout<<"Native pack complete: "<<chunks.size()<<" chunks\n";
    } catch(...) { std::ofstream marker(out/"INCOMPLETE.txt"); marker<<"Packing failed or cancelled. Do not use this output.\n"; throw; }
}
#include "profile_packer.h"
void verify(const fs::path& index,const fs::path& root,const fs::path& runtimeOverride={}) {
    phase("Final verification");
    if(productionPack||fs::exists(index.parent_path()/"fakelib/libSceAmpr.sprx"))verifyInstalledRuntime(index.parent_path());
    auto b=read(index); auto h=at<AmprPackIndexHeader>(b,0); auto saved=h.headerCrc32; h.headerCrc32=0;
    check(!memcmp(h.magic,"AMPRPAK4",8)&&h.version==4&&h.headerSize==128&&h.endianMarker==0x01020304&&!h.flags&&!h.reserved,"invalid manifest header");
    check(crc(&h,sizeof h)==saved && crc(b.data()+128,b.size()-128)==h.payloadCrc32,"manifest CRC mismatch");
    check(h.fileRecordSize==48&&h.chunkRecordSize==12&&h.packRecordSize==32,"unsupported record geometry");
    check(h.fileCount<=(b.size()-128)/48&&h.chunkCount<=(b.size()-128)/12&&h.packCount<=(b.size()-128)/32,"invalid counts");
    check(h.filesOffset==128&&h.chunksOffset==128+h.fileCount*48&&h.packsOffset==h.chunksOffset+h.chunkCount*12&&h.stringsOffset==h.packsOffset+h.packCount*32&&h.stringsOffset<=b.size()&&h.stringsSize==b.size()-h.stringsOffset,"invalid manifest bounds");
    Bytes strings(b.begin()+h.stringsOffset,b.end()); auto crcPath=index;crcPath+=".crc";auto crcfile=read(crcPath); auto ch=at<CrcHeader>(crcfile,0); auto cs=ch.header; ch.header=0;
    check(!memcmp(ch.magic,"AMPRCRC1",8)&&ch.version==1&&ch.size==48&&ch.count==h.chunkCount&&!memcmp(ch.id,h.buildId,16)&&crcfile.size()==48+h.chunkCount*4,"invalid CRC sidecar");
    check(crc(&ch,48)==cs&&crc(crcfile.data()+48,crcfile.size()-48)==ch.payload,"sidecar CRC mismatch");
    std::vector<AmprPackDataRecord> packs; std::vector<fs::path> names;
    for(uint32_t i=0;i<h.packCount;i++) { auto p=at<AmprPackDataRecord>(b,h.packsOffset+i*32); auto name=stringat(strings,p.nameOffset,p.nameLength); auto path=safe(index.parent_path(),name);
        check(fs::file_size(path)==p.fileSize,"volume size mismatch"); std::ifstream in(path,std::ios::binary); AmprPackDataHeader d{}; in.read((char*)&d,64); auto dc=d.headerCrc32; d.headerCrc32=0;
        check(bool(in)&&!memcmp(d.magic,"AMPRDAT3",8)&&d.version==3&&d.headerSize==64&&d.packId==i&&!d.reserved&&d.flags==p.flags&&!(p.flags&~3u)&&!memcmp(d.buildId,h.buildId,16)&&crc(&d,64)==dc,"invalid volume header");
        check(p.ioPageSize>=4096&&p.ioPageSize<=1048576&&!(p.ioPageSize&(p.ioPageSize-1))&&(p.flags&2)&&d.payloadOffset<=p.fileSize&&d.payloadOffset>=64&&d.payloadOffset%p.ioPageSize==0&&p.fileSize%p.ioPageSize==0&&d.payloadBytes==p.payloadBytes&&p.payloadBytes==p.fileSize-d.payloadOffset,"invalid volume bounds"); packs.push_back(p); names.push_back(path); }
    auto source=sourceindex(index.parent_path()/"ampr_emu.index"); check(source.size()==h.fileCount,"source index file count mismatch");
    uint64_t expectedChunk=0,verifiedBytes=0,verifyTotal=0;for(auto&e:source)verifyTotal+=e.size;if(progressHook)progressHook(0,verifyTotal);std::set<uint64_t> hashes;validatePipeline(pipelineThreads);auto distribution=pipelineThreads.verification;
    BlockWorkers verificationWorkers(distribution.workers);AsyncReaders verificationReaders(distribution.readers);ioMetrics.readers=distribution.readers;ioMetrics.writers=0;
    std::cout<<"Verification pipeline: "<<distribution.readers<<" readers + "<<distribution.workers<<" check workers + 1 coordinator\n";
    std::deque<std::pair<std::future<std::future<void>>,uint64_t>> verified;const size_t verificationWorkersLimit=std::max(distribution.readers,distribution.workers)*2;
    auto finishCheck=[&](){while(verified.front().first.wait_for(std::chrono::milliseconds(100))!=std::future_status::ready)ensureActive();auto checked=verified.front().first.get();while(checked.wait_for(std::chrono::milliseconds(100))!=std::future_status::ready)ensureActive();checked.get();verifiedBytes+=verified.front().second;verified.pop_front();if(progressHook)progressHook(verifiedBytes,verifyTotal);};
    for(uint64_t i=0;i<h.fileCount;i++) { ensureActive(); auto f=at<AmprPackFileRecord>(b,h.filesOffset+i*48); auto path=stringat(strings,f.pathOffset,f.pathLength);
        check(path==source[i].path&&f.logicalSize==source[i].size&&pathhash(path)==f.pathHash&&hashes.insert(f.pathHash).second,"file ID/path/size/hash mismatch");
        check(!(f.flags&~31u)&&!f.reserved&&!((f.flags&4)&&(f.flags&16)),"invalid file flags"); auto src=(!runtimeOverride.empty()&&path=="/app0/fakelib/libSceAmpr.sprx")?runtimeOverride:safe(root,path.substr(6));if(path=="/app0/fakelib/libSceAmpr.sprx"&&(productionPack||fs::file_size(src)!=f.logicalSize)){auto installed=index.parent_path()/"fakelib/libSceAmpr.sprx";check(hexNative(shaFileNative(installed))=="69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a","unexpected installed runtime");src=installed;} check(fs::file_size(src)==f.logicalSize,"source size mismatch: "+path);
        if(!(f.flags&1)) { check(!f.chunkCount&&!f.firstChunk&&!f.blockShift,"invalid loose geometry"); auto loose=safe(index.parent_path(),path.substr(6));check(fs::is_regular_file(loose)&&fs::file_size(loose)==f.logicalSize,"missing loose file");
            uint64_t left=f.logicalSize,offset=0;while(left){ensureActive();auto n=std::min<uint64_t>(left,pipelineIo.verifyBatch);
                verified.emplace_back(verificationReaders.task([src,loose,offset,n,&verificationWorkers](AsyncReaders::Context&reader){auto x=reader.readRange(src,offset,n);auto y=reader.readRange(loose,offset,n);return verificationWorkers.task([x=std::move(x),y=std::move(y)](){check(x==y,"loose content mismatch");});}),n);
                left-=n;offset+=n;if(verified.size()>=verificationWorkersLimit)finishCheck();
            }while(!verified.empty())finishCheck();continue;
        }
        check(f.blockShift>=14&&f.blockShift<=20&&f.firstChunk==expectedChunk&&f.chunkCount==(f.logicalSize+((1ull<<f.blockShift)-1))/(1ull<<f.blockShift)&&f.chunkCount<=h.chunkCount-expectedChunk,"invalid file chunk geometry");
struct VerificationChunk {fs::path volume;uint64_t offset,rawsize;uint32_t storedSize,codec,checksum;};
        std::vector<VerificationChunk> batch;uint64_t batchBytes=0,batchOffset=0;
        auto submitBatch=[&](){if(batch.empty())return;auto bytes=batchBytes;
            verified.emplace_back(verificationReaders.task([src,offset=batchOffset,bytes,batch=std::move(batch),&verificationWorkers](AsyncReaders::Context&reader){
                auto original=reader.readRange(src,offset,bytes);std::vector<Bytes> stored;stored.reserve(batch.size());for(auto&chunk:batch)stored.push_back(reader.readRange(chunk.volume,chunk.offset,chunk.storedSize,size_t(pipelineIo.archiveRead)));
                return verificationWorkers.task([original=std::move(original),stored=std::move(stored),batch=std::move(batch)](){size_t offset=0;for(size_t i=0;i<batch.size();i++){auto&chunk=batch[i];Bytes raw;if(chunk.codec){raw.resize(chunk.rawsize);check(LZ4_decompress_safe((char*)stored[i].data(),(char*)raw.data(),int(stored[i].size()),int(chunk.rawsize))==int64_t(chunk.rawsize),"LZ4 decoding failed");}else{check(stored[i].size()==chunk.rawsize,"RAW geometry mismatch");raw=std::move(stored[i]);}check(crc(raw.data(),raw.size())==chunk.checksum,"decoded CRC mismatch");check(!memcmp(original.data()+offset,raw.data(),raw.size()),"decoded source mismatch");offset+=raw.size();}});
            }),bytes);batch.clear();batchOffset+=bytes;batchBytes=0;if(verified.size()>=verificationWorkersLimit)finishCheck();
        };
                uint64_t left=f.logicalSize; for(uint32_t j=0;j<f.chunkCount;j++) { ensureActive(); auto c=at<AmprPackChunkRecord>(b,h.chunksOffset+(expectedChunk+j)*12); auto pid=ampr_pack_chunk_pack_id(c); auto off=ampr_pack_chunk_offset(c); auto n=ampr_pack_chunk_stored_size(c); auto codec=ampr_pack_chunk_codec(c); auto flags=ampr_pack_chunk_flags(c); auto rawsize=std::min<uint64_t>(left,1ull<<f.blockShift);
            check(ampr_pack_chunk_descriptor_valid(c)&&codec<=1&&!(flags&~15u)&&pid<packs.size()&&n<=rawsize,"invalid chunk descriptor"); auto& p=packs[pid]; check(off>=p.fileSize-p.payloadBytes&&off%64==0&&off<=p.fileSize&&n<=p.fileSize-off,"chunk outside volume");
            check(bool(flags&2)==bool(f.flags&4)&&(!(f.flags&2)||!codec),"chunk/file flags mismatch");
            bool contained=off/p.ioPageSize==(off+n-1)/p.ioPageSize,aligned=off%p.ioPageSize==0;
            check(!(flags&4)||contained,"false page containment"); check(!(flags&8)||aligned,"false page alignment");
            check(!(flags&8)||n>p.ioPageSize||(flags&4),"small aligned chunk missing containment"); check((f.flags&4)||(flags&12),"non-streaming chunk not page safe");
            auto checksum=at<uint32_t>(crcfile,48+(expectedChunk+j)*4);
            batch.push_back({names[pid],off,rawsize,n,codec,checksum});batchBytes+=rawsize;left-=rawsize;
            if(batchBytes>=pipelineIo.verifyBatch)submitBatch();
        }submitBatch();
        while(!verified.empty())finishCheck();
        expectedChunk+=f.chunkCount;
    }
    check(expectedChunk==h.chunkCount,"unowned chunks"); std::cout<<"Native verification passed: "<<h.fileCount<<" files\n";
}
#ifndef AMPR_NO_MAIN
int wmain(int argc,wchar_t** argv) {
    std::signal(SIGINT,stop);
    try { check(argc>=2,"usage: ampr-native pack ROOT AMPRIDX3 NEW_OUTPUT [--fast] [--level 1..12] | verify MANIFEST SOURCE_ROOT");
        std::wstring command=argv[1]; if(command==L"pack") { check(argc>=5,"pack requires ROOT INDEX NEW_OUTPUT"); bool fast=false; int level=9;
            for(int i=5;i<argc;i++) { std::wstring option=argv[i]; if(option==L"--fast") fast=true; else if(option==L"--level") { check(++i<argc,"missing level"); std::wstring value=argv[i]; check(value.size()<=2&&!value.empty()&&value.find_first_not_of(L"0123456789")==std::wstring::npos,"invalid level"); level=std::stoi(value); check(level>=1&&level<=12,"level must be 1..12"); } else if(option==L"--profile"){check(++i<argc,"missing TOML profile");activeProfile=argv[i];}else if(option==L"--runtime"){check(++i<argc,"missing runtime path");activeRuntime=argv[i];}else throw std::runtime_error("unsupported setting/profile"); }
            if(!activeProfile.empty())packProfile(argv[2],argv[3],argv[4],activeProfile,activeRuntime);else pack(argv[2],argv[3],argv[4],fast,level); verify(fs::path(argv[4])/"ampr_assets.index",argv[2]); }
        else if(command==L"verify") { check(argc==4,"verify requires MANIFEST SOURCE_ROOT"); verify(argv[2],argv[3]); }
        else throw std::runtime_error("unsupported command/profile"); return 0;
    } catch(const std::exception& e) { std::cerr<<"ERROR: "<<e.what()<<"\n"; return 1; }
}

#endif

