// SPDX-License-Identifier: GPL-3.0-or-later
// Ordered parallel native implementation of pinned Python pack planning/layout.
#include "profile_config.h"
#include "loose_permissions.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <deque>
#include "pipeline_diagnostics.h"
#include "pipeline_io.h"
Bytes shaFileNative(const fs::path&p){std::ifstream in(p,std::ios::binary);check(bool(in),"cannot hash "+p.u8string());Sha hash;char buffer[65536];while(in){ensureActive();in.read(buffer,sizeof buffer);auto n=in.gcount();if(n)hash.add(buffer,n);}check(in.eof(),"hash read failed");return hash.finish();}
std::string hexNative(const Bytes&b){const char*digits="0123456789abcdef";std::string s;for(auto c:b){s+=digits[c>>4];s+=digits[c&15];}return s;}
int64_t diskMtime(const fs::path&p){
#if defined(_WIN32) && !defined(AMPR_PORTABLE_BACKEND)
    struct _stat64 st{};check(_wstat64(p.c_str(),&st)==0,"cannot stat "+p.u8string());return st.st_mtime;
#else
    return portableMtime(p);
#endif
}
struct EncodedBlock{Bytes stored;uint32_t checksum=0;bool compressed=false;};
EncodedBlock encodeBlock(const Bytes&raw,const Json&r,uint64_t page){
    EncodedBlock b;b.checksum=crc(raw.data(),raw.size());b.stored=raw;if(r["action"]=="store")return b;
    Bytes compressed(LZ4_compressBound(int(raw.size())));int n=r["mode"]=="fast"?LZ4_compress_fast((char*)raw.data(),(char*)compressed.data(),int(raw.size()),int(compressed.size()),r["acceleration"].get<int>()):LZ4_compress_HC((char*)raw.data(),(char*)compressed.data(),int(raw.size()),int(compressed.size()),r["level"].get<int>());check(n>0,"LZ4 compression failed");
    auto minBytes=r["min_savings_bytes"].get<uint64_t>();auto ratio=r["min_savings_ratio"].get<double>();if(resolvedLayout(r)!="streaming"&&(uint64_t(n)+page-1)/page>=(raw.size()+page-1)/page){minBytes=std::max(minBytes,r["io_neutral_min_savings_bytes"].get<uint64_t>());ratio=std::max(ratio,r["io_neutral_min_savings_ratio"].get<double>());}
    int64_t saved=int64_t(raw.size())-n;if(saved>=int64_t(minBytes)&&double(saved)/raw.size()>=ratio){compressed.resize(n);b.stored=std::move(compressed);b.compressed=true;}return b;
}
// Each stage reserves one coordinator; the whole pipeline has at most eight threads.
struct StageThreads { unsigned readers,workers,writers; };
struct PipelineThreads {
    StageThreads packing{2,3,2},loose{3,0,4},verification{3,4,0};
};
inline PipelineThreads pipelineThreads;
inline void validatePipeline(const PipelineThreads& p){
    auto valid=[](StageThreads s){return s.readers>=1&&s.readers<=6&&s.workers<=6&&s.writers<=6&&s.readers+s.workers+s.writers<=7;};
    check(valid(p.packing)&&p.packing.workers>=1&&p.packing.writers>=1,"Packing must use at most eight threads including one coordinator.");
    check(valid(p.loose)&&p.loose.workers==0&&p.loose.writers>=1,"Loose copying must use at most eight threads including one coordinator.");
    check(valid(p.verification)&&p.verification.writers==0&&p.verification.workers>=1,"Verification must use at most eight threads including one coordinator.");
}
class BlockWorkers {
    std::mutex lock;std::condition_variable ready;bool stopping=false;
    std::deque<std::function<void()>> jobs;std::vector<std::thread> threads;
public:
    explicit BlockWorkers(unsigned count){ioMetrics.workers=count;try{for(unsigned i=0;i<count;i++)threads.emplace_back([this](){for(;;){std::function<void()> job;{std::unique_lock<std::mutex> g(lock);{DiagTimer idle(pipelineDiagnostics.workerIdle);ready.wait(g,[&]{return stopping||!jobs.empty();});}if(stopping)return;job=std::move(jobs.front());jobs.pop_front();diagGauge(pipelineDiagnostics.workerQueue,-1);}{DiagRoleScope role(DiagRole::worker);ioMetrics.busy++;IoMeasure timer(ioMetrics.workerNs);job();ioMetrics.busy--;}}});}catch(...){shutdown();throw;}}
    void shutdown(){ {std::lock_guard<std::mutex> g(lock);stopping=true;jobs.clear();}ready.notify_all();for(auto&t:threads)if(t.joinable())t.join(); }
    ~BlockWorkers(){shutdown();}
    struct Result{EncodedBlock block;Bytes digest;uint64_t size;};
    template<class F> auto task(F fn)->std::future<decltype(fn())>{using T=decltype(fn());auto task=std::make_shared<std::packaged_task<T()>>(std::move(fn));auto future=task->get_future();{std::lock_guard<std::mutex> g(lock);jobs.emplace_back([task](){(*task)();});diagGauge(pipelineDiagnostics.workerQueue,1);}ready.notify_one();return future;}
    std::future<std::vector<Result>> submitBatch(std::vector<Bytes> blocks,Json rule,uint64_t page){return task([blocks=std::move(blocks),rule=std::move(rule),page](){std::vector<Result> results;results.reserve(blocks.size());for(auto&raw:blocks){Sha hash;hash.add(raw.data(),raw.size());results.push_back({encodeBlock(raw,rule,page),hash.finish(),raw.size()});}return results;});}
    std::future<Result> submit(Bytes raw,Json rule,uint64_t page){auto task=std::make_shared<std::packaged_task<Result()>>([raw=std::move(raw),rule=std::move(rule),page](){Sha hash;hash.add(raw.data(),raw.size());return Result{encodeBlock(raw,rule,page),hash.finish(),raw.size()};});auto future=task->get_future();{std::lock_guard<std::mutex> g(lock);jobs.emplace_back([task](){(*task)();});diagGauge(pipelineDiagnostics.workerQueue,1);}ready.notify_one();return future;}
};
std::atomic<unsigned> compressionWorkerLimit{6};
unsigned compressionWorkerCount(){auto count=std::thread::hardware_concurrency();unsigned target=compressionWorkerLimit.load();if(const char*setting=std::getenv("AMPR_PACK_WORKERS")){unsigned parsed=unsigned(std::atoi(setting));if(parsed>=1&&parsed<=8)target=parsed;}return std::max(1u,std::min(target,count?count:target));}
#ifdef AMPR_PIPELINE_TRACE
static void pipelineTrace(const char*message){int fd=::open("/data/AMPRStudio/pipeline-trace.txt",O_WRONLY|O_CREAT|O_APPEND,0600);if(fd>=0){::write(fd,message,strlen(message));::write(fd,"\n",1);::close(fd);}}
#else
static void pipelineTrace(const char*){}
#endif
// Fixed reader pool; each reader owns its file handles and bounded read caches.
class AsyncReaders {
public:
    struct Context {
        struct Cache { fs::path path;uint64_t offset=0;Bytes bytes;PipelineInput file; };
        std::deque<std::unique_ptr<Cache>> caches;
        Bytes readRange(const fs::path&path,uint64_t offset,size_t n,size_t batch=0){
            check(!cancelled.load(),"cancelled");if(!batch)batch=size_t(pipelineIo.sourceRead);Cache*cache=nullptr;
            for(auto&c:caches)if(c->path==path){cache=c.get();break;}
            if(!cache){if(caches.size()>=9)caches.pop_front();auto c=std::make_unique<Cache>();c->path=path;c->file.open(path,std::ios::binary);check(bool(c->file),"Cannot read "+path.u8string());cache=c.get();caches.push_back(std::move(c));}
            if(cache->bytes.empty()||offset<cache->offset||offset+n>cache->offset+cache->bytes.size()){
                auto size=fs::file_size(path);check(offset<=size&&n<=size-offset,"Read outside file: "+path.u8string());cache->offset=offset;uint64_t wanted=std::min<uint64_t>(size-offset,std::max(n,batch));for(auto it=caches.begin();it!=caches.end();){uint64_t used=std::max<uint64_t>(wanted,cache->bytes.capacity())+std::max<uint64_t>(cache->file.cacheMemory(),benchmarkDirectReads?((wanted+65535)/65536*65536):0);for(auto&c:caches)if(c.get()!=cache)used+=c->bytes.capacity()+c->file.cacheMemory();if(used<=pipelineIo.readerCache)break;if(it->get()==cache){++it;continue;}it=caches.erase(it);}cache->bytes.resize(wanted);
                IoMeasure timer(ioMetrics.readNs);cache->file.clear();cache->file.seekg(offset);cache->file.read((char*)cache->bytes.data(),cache->bytes.size());check(cache->file.gcount()==int64_t(cache->bytes.size()),"Truncated read: "+path.u8string());ioMetrics.readBytes+=cache->bytes.size();
            }
            return Bytes(cache->bytes.begin()+offset-cache->offset,cache->bytes.begin()+offset-cache->offset+n);
        }
    };
private:
    std::mutex lock;std::condition_variable ready;bool stopping=false;
    std::deque<std::function<void(Context&)>> jobs;std::vector<std::thread> threads;
public:
    explicit AsyncReaders(unsigned count){check(count>=1&&count<=6,"Invalid reader count");try{for(unsigned n=0;n<count;n++)threads.emplace_back([this](){Context context;for(;;){std::function<void(Context&)> job;{std::unique_lock<std::mutex> g(lock);{DiagTimer idle(pipelineDiagnostics.readerIdle);ready.wait(g,[&]{return stopping||!jobs.empty();});}if(stopping)return;job=std::move(jobs.front());jobs.pop_front();diagGauge(pipelineDiagnostics.readerQueue,-1);}DiagRoleScope role(DiagRole::reader);DiagTimer taskTime(pipelineDiagnostics.readerTask);job(context);}});}catch(...){shutdown();throw;}}
    template<class F> auto task(F fn)->std::future<decltype(fn(std::declval<Context&>()))>{using T=decltype(fn(std::declval<Context&>()));auto job=std::make_shared<std::packaged_task<T(Context&)>>(std::move(fn));auto result=job->get_future();{std::lock_guard<std::mutex> g(lock);check(!stopping,"readers stopped");jobs.push_back([job](Context&c){(*job)(c);});diagGauge(pipelineDiagnostics.readerQueue,1);}ready.notify_one();return result;}
    void shutdown(){ {std::lock_guard<std::mutex> g(lock);stopping=true;jobs.clear();}ready.notify_all();for(auto&t:threads)if(t.joinable())t.join();}
    ~AsyncReaders(){shutdown();}
};
// Two independent readers fetch 2 MiB batches concurrently. Publication stays
// in source order so compressed blocks and all archive metadata remain stable.
class PackReader {
    std::mutex lock;std::condition_variable ready,space;bool stopping=false;
    unsigned readerCount=0,finished=0;uint64_t nextBatch=0,publishBatch=0;
    std::exception_ptr failure;std::deque<std::future<std::vector<BlockWorkers::Result>>> results;std::vector<BlockWorkers::Result> batchResults;size_t resultAt=0;std::vector<std::thread> readers;
    void shutdown(){ {std::lock_guard<std::mutex> g(lock);stopping=true;}space.notify_all();ready.notify_all();for(auto&t:readers)if(t.joinable())t.join();}
public:
    PackReader(BlockWorkers&workers,const fs::path&path,uint64_t size,unsigned shift,Json rule,uint64_t page,unsigned capacity,uint64_t baseOffset=0,bool exactEnd=true){readerCount=pipelineThreads.packing.readers;try{pipelineTrace("reader constructor start");for(unsigned n=0;n<readerCount;++n){pipelineTrace("before reader thread create");readers.emplace_back([this,&workers,path,size,shift,rule,page,capacity,baseOffset,exactEnd](){DiagRoleScope role(DiagRole::reader);DiagTimer taskTime(pipelineDiagnostics.readerTask);pipelineTrace("reader thread entered");try{PipelineInput in(path,std::ios::binary);check(bool(in),"source open failed: "+path.u8string());pipelineTrace("reader source opened");const uint64_t batchSize=pipelineIo.packRead;for(;;){uint64_t batchId;{std::lock_guard<std::mutex> g(lock);if(stopping||cancelled.load()||failure)break;batchId=nextBatch++;}uint64_t offset=batchId*batchSize;if(offset>=size)break;Bytes batch(std::min<uint64_t>(size-offset,batchSize));{IoMeasure timer(ioMetrics.readNs);pipelineTrace("reader before seek");in.seekg(baseOffset+offset);pipelineTrace("reader after seek");check(bool(in),"source seek failed");in.read((char*)batch.data(),batch.size());pipelineTrace("reader after read");check(in.gcount()==int64_t(batch.size()),"source truncated: "+path.u8string());ioMetrics.readBytes+=batch.size();if(exactEnd&&offset+batch.size()==size)check(in.peek()==EOF,"source grew during packing");}std::unique_lock<std::mutex> g(lock);{DiagTimer wait(pipelineDiagnostics.readerOrder);space.wait(g,[&]{return stopping||cancelled.load()||failure||batchId==publishBatch;});}if(stopping||cancelled.load()||failure)break;for(size_t at=0;at<batch.size();){{DiagTimer wait(pipelineDiagnostics.readerBlocked);space.wait(g,[&]{return stopping||cancelled.load()||failure||results.size()<capacity;});}if(stopping||cancelled.load()||failure)break;std::vector<Bytes> blocks;for(unsigned n=0;n<pipelineIo.packJobBlocks&&at<batch.size();n++){auto count=std::min<size_t>(uint64_t(1)<<shift,batch.size()-at);blocks.emplace_back(batch.begin()+at,batch.begin()+at+count);at+=count;}results.push_back(workers.submitBatch(std::move(blocks),rule,page));diagGauge(pipelineDiagnostics.packQueue,1);ready.notify_one();}++publishBatch;space.notify_all();}}catch(...){std::lock_guard<std::mutex> g(lock);if(!failure)failure=std::current_exception();}{std::lock_guard<std::mutex> g(lock);++finished;}ready.notify_all();space.notify_all();});pipelineTrace("after reader thread create");}pipelineTrace("reader constructor complete");}catch(...){shutdown();throw;}}
    BlockWorkers::Result next(){if(resultAt<batchResults.size())return std::move(batchResults[resultAt++]);DiagTimer wait(pipelineDiagnostics.coordinatorInput);pipelineTrace("coordinator next");std::future<std::vector<BlockWorkers::Result>> result;for(;;){ensureActive();std::unique_lock<std::mutex> g(lock);if(failure)std::rethrow_exception(failure);if(!results.empty()){result=std::move(results.front());results.pop_front();diagGauge(pipelineDiagnostics.packQueue,-1);space.notify_all();break;}if(finished==readerCount)throw std::runtime_error("readers ended before expected data");ready.wait_for(g,std::chrono::milliseconds(100));}while(result.wait_for(std::chrono::milliseconds(100))!=std::future_status::ready)ensureActive();batchResults=result.get();resultAt=0;check(!batchResults.empty(),"Empty packing batch");return std::move(batchResults[resultAt++]);}
    void finish(){for(auto&t:readers)if(t.joinable())t.join();if(failure)std::rethrow_exception(failure);}
    ~PackReader(){shutdown();}
};
// Each archive belongs to one writer for its entire lifetime. The coordinator
// decides placement; writers only execute ordered payload writes. Both queues
// are bounded to 4 MiB, and different archives can be written concurrently.
class ArchiveWriters {
    struct Job{std::function<void()> run;size_t bytes;std::string source;};
    std::mutex lock;std::condition_variable ready,space;std::deque<Job> jobs[6];
    size_t queued[6]{};unsigned active[6]{},fileLeases[6]{};unsigned lanes;bool stopping=false;std::exception_ptr failure;std::vector<std::thread> threads;
public:
    ArchiveWriters(unsigned count=pipelineThreads.packing.writers):lanes(count){check(count>=1&&count<=6,"Invalid writer count");try{for(unsigned lane=0;lane<lanes;++lane)threads.emplace_back([this,lane](){for(;;){Job job;{std::unique_lock<std::mutex> g(lock);{DiagTimer idle(pipelineDiagnostics.writerIdle);ready.wait(g,[&]{return stopping||failure||!jobs[lane].empty();});}if(stopping||failure)return;job=std::move(jobs[lane].front());jobs[lane].pop_front();queued[lane]-=job.bytes;diagGauge(pipelineDiagnostics.writerBytes,-int64_t(job.bytes));++active[lane];space.notify_all();}try{DiagRoleScope role(DiagRole::writer);DiagTimer taskTime(pipelineDiagnostics.writerTask);job.run();}catch(const std::exception&e){std::lock_guard<std::mutex> g(lock);if(!failure)failure=std::make_exception_ptr(std::runtime_error("Packing "+job.source+": "+e.what()));}catch(...){std::lock_guard<std::mutex> g(lock);if(!failure)failure=std::current_exception();}{std::lock_guard<std::mutex> g(lock);--active[lane];}space.notify_all();ready.notify_all();}});}catch(...){shutdown();throw;}}
    unsigned reserveFileLane(){std::lock_guard<std::mutex> g(lock);unsigned chosen=0;for(unsigned i=1;i<lanes;i++)if(fileLeases[i]<fileLeases[chosen]||(fileLeases[i]==fileLeases[chosen]&&queued[i]<queued[chosen]))chosen=i;++fileLeases[chosen];return chosen;}
    void releaseFileLane(unsigned lane){std::lock_guard<std::mutex> g(lock);check(lane<lanes&&fileLeases[lane],"Invalid writer file lease");--fileLeases[lane];}
    void throwIfFailed(){std::lock_guard<std::mutex> g(lock);if(failure)std::rethrow_exception(failure);}
    void enqueue(unsigned id,std::function<void()> fn,size_t bytes,const std::string&source,std::atomic<bool>*stop=nullptr){unsigned lane=id%lanes;std::unique_lock<std::mutex> g(lock);while(!failure&&!stopping&&queued[lane]+bytes>pipelineIo.writeQueue){g.unlock();if(stop)check(!stop->load(),"cancelled");else ensureActive();g.lock();{DiagTimer wait(diagRole==DiagRole::reader?pipelineDiagnostics.readerBlocked:pipelineDiagnostics.coordinatorOutput);space.wait_for(g,std::chrono::milliseconds(100));}}if(failure)std::rethrow_exception(failure);check(!stopping&&(!stop||!stop->load()),"writers stopped");jobs[lane].push_back({std::move(fn),bytes,source});queued[lane]+=bytes;diagGauge(pipelineDiagnostics.writerBytes,int64_t(bytes));ready.notify_all();}
    void submit(unsigned id,VolumeOutput*output,uint64_t padding,Bytes data,const std::string&source){size_t bytes=data.size();enqueue(id,[output,padding,data=std::move(data)](){zeros(*output,padding);write(*output,data.data(),data.size());},bytes,source);}
    bool outstanding(){for(unsigned i=0;i<lanes;i++)if(!jobs[i].empty()||active[i])return true;return false;}
    void drain(){DiagTimer wait(pipelineDiagnostics.coordinatorOutput);std::unique_lock<std::mutex> g(lock);while(!failure&&outstanding()){g.unlock();ensureActive();g.lock();space.wait_for(g,std::chrono::milliseconds(100));}if(failure)std::rethrow_exception(failure);}
    void shutdown(){ {std::lock_guard<std::mutex> g(lock);stopping=true;for(unsigned i=0;i<lanes;i++)jobs[i].clear();}ready.notify_all();space.notify_all();for(auto&t:threads)if(t.joinable())t.join();}
    ~ArchiveWriters(){shutdown();}
};
// Bounded read-ahead overlaps source reads with destination writes. Each stream
// holds at most three queued 2 MiB buffers, plus the current read/write buffers.
class LooseReader {
    std::mutex lock;std::condition_variable ready,space;bool stopping=false,finished=false;
    std::exception_ptr failure;std::deque<Bytes> buffers;std::thread reader;
public:
    LooseReader(const fs::path&path,uint64_t size,std::atomic<bool>&stop){reader=std::thread([this,path,size,&stop](){try{std::ifstream in(path,std::ios::binary);check(bool(in),"loose source open failed");uint64_t left=size;while(left){std::unique_lock<std::mutex> g(lock);space.wait_for(g,std::chrono::milliseconds(100),[&]{return stopping||stop.load()||buffers.size()<3;});if(stopping||stop.load())break;if(buffers.size()>=3)continue;g.unlock();Bytes data(std::min<uint64_t>(left,2*1024*1024));{IoMeasure timer(ioMetrics.readNs);in.read((char*)data.data(),data.size());check(in.gcount()==int64_t(data.size()),"loose source truncated");ioMetrics.readBytes+=data.size();}left-=data.size();g.lock();buffers.push_back(std::move(data));ready.notify_one();}if(!left)check(in.peek()==EOF,"loose source grew");}catch(...){std::lock_guard<std::mutex> g(lock);failure=std::current_exception();}{std::lock_guard<std::mutex> g(lock);finished=true;}ready.notify_all();});}
    Bytes next(std::atomic<bool>&stop){std::unique_lock<std::mutex> g(lock);for(;;){check(!stop.load(),"cancelled");if(!buffers.empty()){auto data=std::move(buffers.front());buffers.pop_front();space.notify_one();return data;}if(finished){if(failure)std::rethrow_exception(failure);throw std::runtime_error("loose reader ended early");}ready.wait_for(g,std::chrono::milliseconds(100));}}
    void finish(){if(reader.joinable())reader.join();if(failure)std::rethrow_exception(failure);}
    ~LooseReader(){{std::lock_guard<std::mutex> g(lock);stopping=true;}space.notify_all();ready.notify_all();if(reader.joinable())reader.join();}
};
void copyLooseStream(const fs::path&src,const fs::path&dst,std::atomic<uint64_t>&completed,std::atomic<bool>&stop){
    fs::create_directories(dst.parent_path());check(!fs::exists(dst),"loose destination collision: "+dst.u8string());auto before=fs::last_write_time(src);auto size=fs::file_size(src);std::ofstream out(dst,std::ios::binary);check(bool(out),"cannot copy loose file: "+src.u8string());LooseReader reader(src,size,stop);uint64_t left=size;
    while(left){check(!stop.load(),"cancelled");auto data=reader.next(stop);{IoMeasure timer(ioMetrics.writeNs);write(out,data.data(),data.size());ioMetrics.writeBytes+=data.size();}left-=data.size();completed+=data.size();}reader.finish();out.flush();check(bool(out)&&fs::file_size(src)==size&&fs::last_write_time(src)==before,"loose copy failed or source changed");out.close();check(bool(out),"loose output close failed");fs::last_write_time(dst,before);portableLoosePermissions(src,dst);
}
void copyLoose(const fs::path&src,const fs::path&dst,uint64_t&done,uint64_t total){
    fs::create_directories(dst.parent_path());check(!fs::exists(dst),"loose destination collision: "+dst.u8string());auto before=fs::last_write_time(src);auto size=fs::file_size(src);std::ifstream in(src,std::ios::binary);std::ofstream out(dst,std::ios::binary);check(bool(in)&&bool(out),"cannot copy loose file: "+src.u8string());Bytes buffer(2*1024*1024);uint64_t left=size;
    while(left){ensureActive();auto n=std::min<uint64_t>(left,buffer.size());{IoMeasure timer(ioMetrics.readNs);in.read((char*)buffer.data(),n);ioMetrics.readBytes+=n;}check(in.gcount()==int64_t(n),"loose source truncated");{IoMeasure timer(ioMetrics.writeNs);write(out,buffer.data(),n);ioMetrics.writeBytes+=n;}left-=n;done+=n;if(progressHook)progressHook(done,total);}out.flush();check(bool(out)&&fs::file_size(src)==size&&fs::last_write_time(src)==before,"loose copy failed or source changed");out.close();fs::last_write_time(dst,before);portableLoosePermissions(src,dst);
}
struct ProfileVolume{std::unique_ptr<VolumeOutput> output;std::string name;uint64_t pos,payload,page,limit;uint32_t flags=2;int id;};
void verifyInstalledRuntime(const fs::path& output){
    auto runtime=output/"fakelib/libSceAmpr.sprx";
    check(fs::is_regular_file(runtime),"Required AMPR runtime missing: "+runtime.u8string());
    check(hexNative(shaFileNative(runtime))=="69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a","Installed AMPR runtime SHA-256 mismatch");
#ifndef _WIN32
    struct stat st{};check(stat(runtime.c_str(),&st)==0&&(st.st_mode&0555)==0555,"AMPR runtime is not readable/executable by the game");
#endif
}
void packProfile(const fs::path&root,const fs::path&index,const fs::path&out,const fs::path&profile,const fs::path&runtime){
    check(fs::is_directory(root)&&!fs::exists(out),"source must exist and output must be new");auto rr=fs::weakly_canonical(root),oo=fs::weakly_canonical(out);auto ri=rr.begin(),oi=oo.begin();for(;ri!=rr.end()&&oi!=oo.end()&&*ri==*oi;++ri,++oi){}check(ri!=rr.end(),"output cannot be inside source");
    auto config=loadNativeConfig(profile);auto&c=config.canonical;auto entries=sourceindex(index);auto indexBytes=read(index);bool install=false;
    for(auto&e:entries)if(e.path=="/app0/fakelib/libSceAmpr.sprx")install=true;
    if(install||!runtime.empty()){check(fs::is_regular_file(runtime),"bundled AMPR runtime missing: "+runtime.u8string());check(hexNative(shaFileNative(runtime))=="69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a","bundled AMPR runtime SHA-256 mismatch");}
    struct Plan{Json rule;int lane=0,group=0;fs::path src;};std::vector<Plan> plan;Json safety=Json::parse(safety_json);std::map<std::string,int> groupIds;for(size_t i=0;i<c["groups"].size();i++)groupIds[c["groups"][i]["name"].get<std::string>()]=int(i);
    phase("Planning file rules and sampling");if(progressHook)progressHook(0,entries.size());
    for(size_t i=0;i<entries.size();++i){ensureActive();auto&e=entries[i];auto relative=e.path.substr(6);auto src=safe(root,relative);check(fs::is_regular_file(src)&&(fs::file_size(src)==e.size||(install&&relative=="fakelib/libSceAmpr.sprx")),"source size/path mismatch: "+relative);
        auto r=normalizedRule(Json{{"action",c["default_action"]}},c,profile.parent_path());for(auto&rule:c["rules"])if(globs(relative,rule["include"])&&!globs(relative,rule["exclude"]))r=rule;
        // The original One Shot frontend adds these exclusions independently of TOML.
        if(globs(relative,safety))r["action"]="loose";
        if(relative=="fakelib/libSceAmpr.sprx"&&install){src=runtime;e.size=fs::file_size(runtime);e.mtime=diskMtime(runtime);auto record=at<SourceEntry>(indexBytes,sizeof(SourceHeader)+i*24);record.size=e.size;record.mtime=e.mtime;memcpy(indexBytes.data()+sizeof(SourceHeader)+i*24,&record,24);}
        int gid=groupIds.at(r["group"].get<std::string>());auto&g=c["groups"][gid];auto page=g["io_page_size"].get<uint64_t>();if(!page)page=c["io_page_size"];
        if(c["auto_loose_large_files"].get<bool>()&&r["action"]=="compress"&&!r["force_pack"].get<bool>()&&(!r["hot"].get<bool>()||c["auto_loose_hot_files"].get<bool>())&&e.size>=c["auto_loose_min_file_size"].get<uint64_t>()&&e.size){
            auto before=fs::last_write_time(src);auto bs=uint64_t(1)<<r["block_shift"].get<int>(),blocks=(e.size+bs-1)/bs;auto samples=std::min(c["auto_loose_sample_blocks"].get<uint64_t>(),std::max<uint64_t>(1,c["auto_loose_sample_bytes"].get<uint64_t>()/bs));samples=std::min(samples,blocks);uint64_t sampled=0,stored=0,rawBlocks=0;std::ifstream in(src,std::ios::binary);check(bool(in),"sample open failed");
            for(uint64_t s=0;s<samples;s++){ensureActive();auto block=blocks<=samples?s:samples==1?blocks/2:s*(blocks-1)/(samples-1);auto offset=block*bs;Bytes raw(std::min(bs,e.size-offset));in.seekg(offset);in.read((char*)raw.data(),raw.size());check(in.gcount()==int64_t(raw.size()),"source truncated during sampling");auto b=encodeBlock(raw,r,page);sampled+=raw.size();stored+=b.stored.size();rawBlocks+=!b.compressed;}
            check(fs::file_size(src)==e.size&&fs::last_write_time(src)==before,"source changed during sampling");if(!config.selfContained&&(1.0-double(stored)/sampled<c["auto_loose_min_savings_ratio"].get<double>()||double(rawBlocks)/samples>=c["auto_loose_max_raw_ratio"].get<double>())){r["action"]="loose";r["hot"]=false;r["streaming"]=false;r["layout"]="mixed";std::cout<<"Auto-loose: "<<relative<<"\n";}}
        check(!(r["action"]=="loose"&&globs(relative,config.required)),"required_packed file would be loose: "+relative);plan.push_back({r,0,gid,src});if(progressHook)progressHook(i+1,entries.size());
    }
    for(auto&pair:groupIds){auto gid=pair.second;auto&g=c["groups"][gid];std::vector<size_t> ids;for(size_t i=0;i<plan.size();i++)if(plan[i].group==gid&&plan[i].rule["action"]!="loose")ids.push_back(i);int count=g["pack_count"];
        if(g["assignment"]=="balanced"){std::sort(ids.begin(),ids.end(),[&](size_t a,size_t b){if(entries[a].size!=entries[b].size)return entries[a].size>entries[b].size;auto x=entries[a].path,y=entries[b].path;for(char&ch:x)ch=char(tolower((unsigned char)ch));for(char&ch:y)ch=char(tolower((unsigned char)ch));return x==y?a<b:x<y;});std::vector<uint64_t> loads(count);for(auto id:ids){int lane=int(std::min_element(loads.begin(),loads.end())-loads.begin());plan[id].lane=lane;loads[lane]+=entries[id].size;}}
        else for(auto id:ids)plan[id].lane=g["assignment"]=="hash"?int(pathhash(entries[id].path.substr(6))%count):int(id%count);
    }
    fs::create_directories(out.parent_path());check(fs::create_directory(out),"output must be new");if(outputCreatedHook)outputCreatedHook();activeVolumeNames.clear();std::vector<ProfileVolume> volumes;ArchiveWriters archiveWriters;std::map<std::pair<int,int>,int> current,numbers;std::set<std::string> names;std::map<std::string,AmprPackChunkRecord> dedupe;
    auto createVolume=[&](int gid,int lane,bool striped)->int{pipelineTrace("coordinator creating archive");check(volumes.size()<65536,"too many volumes");auto&g=c["groups"][gid];auto key=std::make_pair(gid,lane);auto previous=current.find(key);if(previous!=current.end()){archiveWriters.drain();volumes[previous->second].output->releaseBuffer();}int volume=numbers[key]++,id=int(volumes.size());auto name=renderedName(c["pack_pattern"],g["name"],lane,volume,id);check(names.insert(name).second,"duplicate volume name");auto dest=safe(out,name);check(!fs::exists(safe(root,name)),"pack name collides with source");fs::create_directories(dest.parent_path());auto page=g["io_page_size"].get<uint64_t>();if(!page)page=c["io_page_size"];auto payload=align(64,std::max(page,c["payload_alignment"].get<uint64_t>()));auto stream=std::make_unique<VolumeOutput>();pipelineTrace("archive before open");stream->open(dest);pipelineTrace("archive before initial padding");zeros(*stream,payload);pipelineTrace("archive after initial padding");volumes.push_back({std::move(stream),name,payload,payload,page,g["max_pack_size"],uint32_t(2|striped),id});activeVolumeNames.push_back(name);current[key]=id;pipelineTrace("archive created");return id;};
    std::vector<AmprPackFileRecord> files;std::vector<AmprPackChunkRecord> chunks;std::vector<uint32_t> checksums;Bytes strings;std::vector<size_t> looseFiles;uint64_t total=0,looseTotal=0,done=0;size_t packedCount=0,packedDone=0;for(size_t i=0;i<entries.size();++i){if(plan[i].rule["action"]=="loose")looseTotal+=entries[i].size;else{total+=entries[i].size;++packedCount;}}validatePipeline(pipelineThreads);unsigned workerCount=pipelineThreads.packing.workers;BlockWorkers workers(workerCount);std::cout<<"Packing pipeline: "<<pipelineThreads.packing.readers<<" readers + "<<workerCount<<" compression workers + "<<pipelineThreads.packing.writers<<" archive writers + 1 coordinator\n";phase("Packing game files");ioMetrics.workers=workerCount;ioMetrics.readers=pipelineThreads.packing.readers;ioMetrics.writers=pipelineThreads.packing.writers;if(progressHook)progressHook(0,total);
    auto placement=[&](const ProfileVolume&v,uint64_t n,const std::string&layout,bool extent){auto pos=extent?align(v.pos,v.page):v.pos;auto off=align(pos,c["chunk_alignment"].get<uint64_t>());if(layout!="streaming"&&(n>=v.page||off/v.page!=(off+n-1)/v.page))off=align(pos,v.page);return off;};
    for(size_t i=0;i<entries.size();i++){ensureActive();auto&e=entries[i];auto&p=plan[i];auto&r=p.rule;auto relative=e.path.substr(6);writingSource=relative;
        AmprPackFileRecord f{};f.pathHash=pathhash(e.path);f.logicalSize=e.size;f.mtime=e.mtime;f.pathOffset=uint32_t(strings.size());f.pathLength=uint32_t(e.path.size());strings.insert(strings.end(),e.path.begin(),e.path.end());strings.push_back(0);
        if(r["action"]=="loose"){files.push_back(f);looseFiles.push_back(i);continue;}
        std::cout<<"Packing "<<++packedDone<<"/"<<packedCount<<": "<<relative<<" ["<<r["action"].get<std::string>()<<"]\n";
        auto layout=resolvedLayout(r);f.flags=1|(r["action"]=="store"?2:0)|(layout=="streaming"?4:0)|(r["hot"].get<bool>()?8:0)|(layout=="random"?16:0);f.blockShift=r["block_shift"];f.packingClass=p.group;f.firstChunk=uint32_t(chunks.size());if(config.preserve)f.mtime=diskMtime(p.src);
        auto&g=c["groups"][p.group];bool striped=g["stripe_large_files"].get<bool>()&&g["pack_count"].get<int>()>1&&e.size>=g["stripe_threshold"].get<uint64_t>();auto page=g["io_page_size"].get<uint64_t>();if(!page)page=c["io_page_size"];auto before=fs::last_write_time(p.src);uint64_t left=e.size,blockIndex=0;
        PackReader reader(workers,p.src,e.size,f.blockShift,r,page,workerCount*3);
        while(left){ensureActive();auto encoded=reader.next();pipelineTrace("coordinator got encoded block");auto&b=encoded.block;auto rawSize=encoded.size;int lane=p.lane;if(striped)lane=(lane+blockIndex/g["stripe_group_blocks"].get<int>())%g["pack_count"].get<int>();
            bool allow=c["deduplicate"].get<bool>()&&(layout!="streaming"||c["deduplicate_streaming"].get<bool>());auto&digest=encoded.digest;std::string key=g["name"].get<std::string>()+(c["deduplicate_scope"]=="group"?"":":"+std::to_string(lane))+":"+(layout=="streaming"?"dense":"isolated")+":"+std::to_string(b.compressed)+":"+std::to_string(rawSize)+":"+hexNative(digest);AmprPackChunkRecord chunk{};auto prior=dedupe.find(key);
            if(allow&&prior!=dedupe.end()){chunk=prior->second;auto flags=1|(ampr_pack_chunk_flags(chunk)&12)|(layout=="streaming"?2:0);chunk.descriptor=(chunk.descriptor&((1u<<22)-1))|(uint32_t(flags)<<22);}
            else{auto pair=std::make_pair(p.group,lane);auto found=current.find(pair);int id=found==current.end()?createVolume(p.group,lane,striped):found->second;bool extent=layout=="streaming"&&(blockIndex==0||(striped&&blockIndex%g["stripe_group_blocks"].get<int>()==0));auto off=placement(volumes[id],b.stored.size(),layout,extent);
                if(volumes[id].limit&&align(off+b.stored.size(),volumes[id].page)>volumes[id].limit){id=createVolume(p.group,lane,striped);off=placement(volumes[id],b.stored.size(),layout,extent);check(align(off+b.stored.size(),volumes[id].page)<=volumes[id].limit,"block exceeds volume size limit");}
                auto&v=volumes[id];if(striped)v.flags|=1;auto storedSize=b.stored.size();archiveWriters.submit(id,v.output.get(),off-v.pos,std::move(b.stored),relative);v.pos=off+storedSize;check(off<(1ull<<48),"volume offset exceeds format");uint32_t flags=(layout=="streaming"?2:0)|(storedSize<=v.page&&off/v.page==(off+storedSize-1)/v.page?4:0)|(off%v.page==0?8:0);chunk={off|(uint64_t(id)<<48),uint32_t(storedSize-1)|(uint32_t(b.compressed)<<20)|(flags<<22)};if(allow)dedupe.emplace(std::move(key),chunk);}
            chunks.push_back(chunk);checksums.push_back(b.checksum);left-=rawSize;done+=rawSize;++blockIndex;if(progressHook)progressHook(done,total);check(chunks.size()<=UINT32_MAX,"too many chunks");
        }
        reader.finish();check(fs::file_size(p.src)==e.size&&fs::last_write_time(p.src)==before,"source changed during packing");f.chunkCount=uint32_t(chunks.size()-f.firstChunk);files.push_back(f);if(fileCompleteHook){archiveWriters.drain();for(auto&v:volumes)v.output->flush();fileCompleteHook(i,f,chunks,checksums);}
    }
    archiveWriters.drain();archiveWriters.shutdown();workers.shutdown();ioMetrics.workers=0;for(auto&v:volumes)v.output->releaseBuffer();
    phase("Finalizing archives and runtime");std::vector<AmprPackDataRecord> packs;for(auto&v:volumes){auto end=align(v.pos,v.page);zeros(*v.output,end-v.pos);v.pos=end;auto offset=uint32_t(strings.size());strings.insert(strings.end(),v.name.begin(),v.name.end());strings.push_back(0);packs.push_back({end-v.payload,end,offset,uint32_t(v.name.size()),v.flags,uint32_t(v.page)});}
    auto fb=records(files),cb=records(chunks),pb=records(packs),crcb=records(checksums);auto configText=c.dump(-1,' ',true);Bytes cfg(configText.begin(),configText.end());Sha hash;const char domain[]="AMPRPACK4";hash.add(domain,sizeof domain);for(auto*part:{&cfg,&fb,&cb,&crcb,&pb,&strings}){uint64_t n=part->size();hash.add(&n,8);hash.add(part->data(),part->size());}auto id=hash.finish();
    for(auto&v:volumes){AmprPackDataHeader h{};memcpy(h.magic,"AMPRDAT3",8);h.version=3;h.headerSize=64;h.packId=v.id;h.flags=v.flags;memcpy(h.buildId,id.data(),16);h.payloadOffset=v.payload;h.payloadBytes=v.pos-v.payload;h.headerCrc32=crc(&h,64);v.output->seekp(0);write(*v.output,&h,64);v.output->close();}
    Bytes payload=fb;payload.insert(payload.end(),cb.begin(),cb.end());payload.insert(payload.end(),pb.begin(),pb.end());payload.insert(payload.end(),strings.begin(),strings.end());AmprPackIndexHeader h{};memcpy(h.magic,"AMPRPAK4",8);h.version=4;h.headerSize=128;h.endianMarker=0x01020304;memcpy(h.buildId,id.data(),16);h.fileCount=files.size();h.chunkCount=chunks.size();h.packCount=packs.size();h.fileRecordSize=48;h.chunkRecordSize=12;h.packRecordSize=32;h.filesOffset=128;h.chunksOffset=128+fb.size();h.packsOffset=h.chunksOffset+cb.size();h.stringsOffset=h.packsOffset+pb.size();h.stringsSize=strings.size();h.payloadCrc32=crc(payload.data(),payload.size());h.headerCrc32=crc(&h,128);
    std::ofstream manifest(out/"ampr_assets.index",std::ios::binary);write(manifest,&h,128);write(manifest,payload.data(),payload.size());manifest.close();CrcHeader ch{};memcpy(ch.magic,"AMPRCRC1",8);ch.version=1;ch.size=48;memcpy(ch.id,id.data(),16);ch.count=chunks.size();ch.payload=crc(crcb.data(),crcb.size());ch.header=crc(&ch,48);std::ofstream side(out/"ampr_assets.index.crc",std::ios::binary);write(side,&ch,48);write(side,crcb.data(),crcb.size());side.close();std::ofstream sourceCopy(out/"ampr_emu.index",std::ios::binary);write(sourceCopy,indexBytes.data(),indexBytes.size());sourceCopy.close();
    if(!config.runtime.is_null()){auto&r=config.runtime;AmprPackRuntimeProfile rt{};memcpy(rt.magic,"AMPRCFG1",8);rt.version=1;rt.size=64;memcpy(rt.buildId,id.data(),16);rt.decodedCacheBytes=r["decoded_cache_bytes"];rt.physicalCacheBytes=r["physical_cache_bytes"];rt.workers=r["workers"];rt.latencyReserveWorkers=r["latency_reserve_workers"];rt.crc32=crc(&rt,64);std::ofstream settings(out/"ampr_assets.index.runtime",std::ios::binary);write(settings,&rt,64);}
    if(!looseFiles.empty()){
        phase("Copying loose files");ioMetrics.workers=0;done=0;total=looseTotal;if(progressHook)progressHook(0,total);
        auto distribution=pipelineThreads.loose;ioMetrics.readers=distribution.readers;ioMetrics.writers=distribution.writers;
        std::cout<<"Loose copy pipeline: "<<distribution.readers<<" readers + "<<distribution.writers<<" writers + 1 coordinator\n";
        std::atomic<uint64_t> copied{0};std::atomic<bool> stop{false};
        ArchiveWriters writers(distribution.writers);AsyncReaders readers(distribution.readers);
        std::deque<std::pair<size_t,std::future<void>>> pending;size_t next=0,finished=0;
        auto launch=[&](){auto lane=writers.reserveFileLane();auto i=looseFiles[next++];auto relative=entries[i].path.substr(6);auto src=plan[i].src,dst=safe(out,relative);writingSource=relative;
            std::cout<<"Copying loose "<<next<<"/"<<looseFiles.size()<<": "<<relative<<"\n";
            pending.emplace_back(i,readers.task([src,dst,lane,relative,&writers,&copied,&stop](AsyncReaders::Context&){
                fs::create_directories(dst.parent_path());check(!fs::exists(dst),"loose destination collision: "+dst.u8string());auto before=fs::last_write_time(src);auto size=fs::file_size(src);
                auto output=std::make_shared<std::ofstream>(dst,std::ios::binary);check(bool(*output),"cannot create loose output: "+dst.u8string());PipelineInput input(src,std::ios::binary);check(bool(input),"loose source open failed: "+src.u8string());
                uint64_t left=size;while(left){check(!stop.load(),"cancelled");Bytes data(std::min<uint64_t>(left,pipelineIo.looseRead));{IoMeasure timer(ioMetrics.readNs);input.read((char*)data.data(),data.size());check(input.gcount()==int64_t(data.size()),"loose source truncated: "+src.u8string());ioMetrics.readBytes+=data.size();}left-=data.size();size_t bytes=data.size();
                    writers.enqueue(lane,[output,data=std::move(data),&copied](){IoMeasure timer(ioMetrics.writeNs);write(*output,data.data(),data.size());ioMetrics.writeBytes+=data.size();copied+=data.size();},bytes,relative,&stop);
                }
                check(input.peek()==EOF&&fs::file_size(src)==size&&fs::last_write_time(src)==before,"loose source changed: "+src.u8string());
                auto completed=std::make_shared<std::promise<void>>();auto done=completed->get_future();writers.enqueue(lane,[output,src,dst,before,completed,lane,&writers](){output->flush();check(bool(*output),"loose output flush failed");output->close();check(bool(*output),"loose output close failed");fs::last_write_time(dst,before);portableLoosePermissions(src,dst);writers.releaseFileLane(lane);completed->set_value();},0,relative,&stop);
                while(done.wait_for(std::chrono::milliseconds(100))!=std::future_status::ready){check(!stop.load(),"cancelled");writers.throwIfFailed();}done.get();
            }));
        };
        try{while(next<looseFiles.size()||!pending.empty()){ensureActive();writers.throwIfFailed();while(next<looseFiles.size()&&pending.size()<distribution.readers)launch();bool ready=false;
            for(auto it=pending.begin();it!=pending.end();){if(it->second.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready){++it;continue;}auto i=it->first;it->second.get();writingSource=entries[i].path.substr(6);std::cout<<"Copied loose "<<++finished<<"/"<<looseFiles.size()<<": "<<writingSource<<"\n";if(fileCompleteHook)fileCompleteHook(i,files[i],chunks,checksums);it=pending.erase(it);ready=true;}
            done=copied.load();if(progressHook)progressHook(done,total);if(!ready&&!pending.empty())pending.front().second.wait_for(std::chrono::milliseconds(100));
        }writers.drain();}catch(...){stop=true;readers.shutdown();writers.shutdown();throw;}
        if(progressHook)progressHook(total,total);
    }
    if(!runtime.empty()){
        auto destination=out/"fakelib/libSceAmpr.sprx";
        if(!install){uint64_t copied=0;copyLoose(runtime,destination,copied,fs::file_size(runtime));}
        verifyInstalledRuntime(out);
    }
    auto auditName=out/(root.filename().u8string()+"_auto_profile.toml");fs::copy_file(profile,auditName);std::cout<<"Native profile pack complete: "<<packs.size()<<" volumes, "<<chunks.size()<<" chunks; loose files copied"<<(install?"; pinned AMPR runtime installed":"")<<"\n";
}

