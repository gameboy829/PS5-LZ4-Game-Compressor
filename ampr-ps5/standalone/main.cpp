// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone application service. No PS5-Game-Compressor application modules.
#include "http.h"
#include "original.h"
#include "thread.h"
#include "library.h"
#include "assets.h"
#include "update_common.h"
#include "https.h"
#include "../src/ampr_backend.h"
#include "../../ampr-native/third_party/config/json.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <chrono>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <signal.h>
using J=nlohmann::json;
namespace fs=std::filesystem;
using namespace studio;
#ifndef STUDIO_PORT
#define STUDIO_PORT 5911
#endif
#ifndef SHADOW_PORT
#define SHADOW_PORT 10101
#endif
#ifndef AMPR_STATE_ROOT
#define AMPR_STATE_ROOT "/data/AMPRStudio"
#endif
static std::mutex control;
static std::mutex stateLock;
static std::atomic<bool> abortJob{false};
static std::atomic<bool> updateBusy{false};
#ifndef STUDIO_HOST_TEST
extern "C" int sceSystemServiceGetAppIdOfRunningBigApp(void);
extern "C" int sceSystemServiceGetAppTitleId(int,char*);
#endif
static bool gameRunning(){
#ifndef STUDIO_HOST_TEST
    int app=sceSystemServiceGetAppIdOfRunningBigApp();if(app<0)return false;
    char title[32]={};if(sceSystemServiceGetAppTitleId(app,title)<0)return true;
    return std::string(title).rfind("PPSA",0)==0||std::string(title).rfind("CUSA",0)==0;
#else
    return false;
#endif
}
static J sharedSetup=J::object();
static uint64_t setupRevision=0;
static J state={{"ok",true},{"active",false},{"started",false},{"result",0},{"phase","Ready"},{"done",0},{"total",0},{"stagePercent",0.0},{"benchmark",false}};
static std::string logTail;
static auto began=std::chrono::steady_clock::now(),stageBegan=began;
static double seconds(std::chrono::steady_clock::time_point t){return std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();}
static std::string get(const Request&r,const std::string& key,const std::string& fallback=""){auto i=r.query.find(key);return i==r.query.end()?fallback:i->second;}
static int integer(const Request&r,const std::string& key,int fallback,int maximum=12){auto s=get(r,key,std::to_string(fallback));int n=int(decimal(s,size_t(maximum)));require(n>0,"Invalid "+key);return n;}
static J shadow(const std::string& operation,const J& body=J::object()){
    auto response=postLoopback(SHADOW_PORT,"/api/v1/"+operation,body.dump());
    auto json=J::parse(response.body);require(response.first.find(" 200 ")!=std::string::npos&&json.value("status",-1)==0,"ShadowMount "+operation+" failed: "+response.body);return json;
}
static J gameLibrary(){
    J games=J::array();std::set<std::string> seen;
    try{auto listing=shadow("games");for(auto& g:listing.at("games")){
        if(!g.value("source_available",false)||!validTitle(g.value("title_id",std::string())))continue;
        if(g.value("image_backed",false)&&g.value("managed",false)){games.push_back(g);seen.insert(g.value("path",std::string()));}
        else{auto folder=folderGame(g.value("path",std::string()));if(!folder.is_null()&&seen.insert(folder["path"].get<std::string>()).second)games.push_back(folder);}
    }}catch(const std::exception&){}
    for(auto& folder:scanFolderGames())if(seen.insert(folder["path"].get<std::string>()).second)games.push_back(folder);
    auto selected=currentFolderSource(AMPR_STATE_ROOT);for(auto& g:games)if(!g.value("image_backed",false)){bool mounted=g.value("path",std::string())==selected&&folderViewReadOnly(folderView(AMPR_STATE_ROOT),selected);g["mounted"]=mounted;if(mounted)g["runtime_path"]=folderView(AMPR_STATE_ROOT).string();}
    return games;
}
static J findGame(const Request&r){auto id=get(r,"titleId"),path=get(r,"sourcePath");require(validTitle(id),"Invalid title ID");for(auto& game:gameLibrary())if(game.value("title_id","")==id&&game.value("path","")==path)return game;throw std::runtime_error("The selected source is no longer available");}
static bool readOnly(const fs::path& root){struct statvfs info{};return fs::is_directory(root)&&statvfs(root.c_str(),&info)==0&&(info.f_flag&ST_RDONLY);}
static uint64_t freeSpace(const fs::path& root){struct statvfs info{};require(statvfs(root.c_str(),&info)==0,"Cannot measure destination space: "+root.string());require(!(info.f_flag&ST_RDONLY),"Destination is read-only");require(info.f_frsize&&info.f_bavail<=UINT64_MAX/info.f_frsize,"Invalid free-space result");return uint64_t(info.f_bavail)*info.f_frsize;}
static bool contains(const fs::path& root,const fs::path& path){auto a=fs::weakly_canonical(root),b=fs::weakly_canonical(path);auto i=a.begin(),k=b.begin();while(i!=a.end()&&k!=b.end()&&*i==*k){++i;++k;}return i==a.end();}
static fs::path destination(const std::string& input,const fs::path& source){fs::path path(input);require(path.is_absolute()&&path!=path.root_path(),"Choose an absolute new destination folder");require(!contains(source,path),"Destination cannot be inside the mounted game");require(!fs::exists(path),"Destination already exists; choose a new folder");auto canonical=fs::weakly_canonical(path);bool allowed=contains("/data/homebrew",canonical);if(contains("/mnt/ext1/games",canonical)||contains("/mnt/ext1/homebrew",canonical)||contains("/mnt/ext1/etaHEN/games",canonical)){struct stat disk{},parent{};require(stat("/mnt/ext1",&disk)==0&&stat("/mnt",&parent)==0&&disk.st_dev!=parent.st_dev,"M.2 SSD is unavailable");allowed=true;}for(int n=0;n<8;n++){auto usb=fs::path("/mnt/usb"+std::to_string(n));if(fs::is_directory(usb)&&contains(usb,canonical))allowed=true;}require(allowed,"Choose an game folder on the internal SSD, M.2 SSD, or a connected USB drive");fs::create_directories(canonical.parent_path());require(!fs::exists(canonical),"Destination already exists");return canonical;}
static bool option(const Request&r,const std::string& key){auto value=get(r,key,"yes");require(value=="yes"||value=="no","Invalid "+key);return value=="yes";}
static fs::path backupFolder(const Request&r,const fs::path& source,const fs::path& output){
    fs::path folder=get(r,"originalBackup","/data/game-backups");require(folder.is_absolute(),"Choose an absolute original backup folder");folder=fs::weakly_canonical(folder);
    bool allowed=contains("/data/game-backups",folder);if(contains("/mnt/ext1/game-backups",folder)){struct stat disk{},parent{};require(stat("/mnt/ext1",&disk)==0&&stat("/mnt",&parent)==0&&disk.st_dev!=parent.st_dev,"M.2 SSD is unavailable");allowed=true;}for(int n=0;n<8;n++){fs::path usb="/mnt/usb"+std::to_string(n);if(fs::is_directory(usb)&&contains(usb,folder))allowed=true;}
    require(allowed&&!contains(source,folder)&&!contains(output,folder)&&!contains(folder,output),"Choose a separate backup folder on internal storage, M.2 SSD, or USB");
    fs::create_directories(folder);return folder;
}
static J plan(const Request&r,bool measure=true){
    require(!gameRunning(),"Close the running game before conversion");auto game=findGame(r);std::string root=game.value("runtime_path","");auto mode=get(r,"mode","internal");require(mode=="internal"||mode=="m2"||mode=="usb"||mode=="usb_then_internal"||mode=="swap","Invalid destination mode");integer(r,"level",7);bool verifyOutput=option(r,"verifyOutput"),verifyCopy=option(r,"verifyCopy");auto original=get(r,"originalAction","keep");bool swapping=original=="swap";if(swapping){original="move";require(mode=="swap"&&verifyOutput&&verifyCopy,"Swap requires full conversion and copy verification");}else require(mode!="swap","Swap mode requires Swap original and converted");require(original=="keep"||original=="move"||original=="delete","Invalid original-file action");if(!game.value("image_backed",false))require(original=="keep"&&!swapping,"Folder dumps currently require Keep both; original folders are retained");
    J p={{"ok",true},{"engineReady",true},{"canStart",false},{"readOnlyVerified",false},{"logicalBytes",0},{"requiredOutputBytes",0},{"outputFreeBytes",0},{"internalFreeBytes",0},{"mountedRoot",root},{"mountExists",fs::is_directory(root)},{"message","Mount the source read-only before conversion."}};
    if(!game.value("mounted",false)||!readOnly(root))return p;p["readOnlyVerified"]=true;
    auto out=destination(get(r,"output"),root);if(!game.value("image_backed",false)){auto physical=fs::path(game.at("path").get<std::string>());require(!contains(physical,out)&&!contains(out,physical),"Destination cannot overlap the original game folder");}require(mode=="swap"||(mode=="internal"?contains("/data/homebrew",out):mode=="m2"?contains("/mnt/ext1",out):!contains("/data/homebrew",out)&&!contains("/mnt/ext1",out)),"Destination does not match selected storage mode");uint64_t logical=0,budget=0;char error[1024]={};
    if(measure){int rc=ampr_backend_measure(root.c_str(),&logical,&budget,error,sizeof(error));require(rc==0,error);}
    auto free=freeSpace(out.parent_path());p["outputFreeBytes"]=free;p["logicalBytes"]=logical;p["requiredOutputBytes"]=budget;p["output"]=out.string();
    bool enough=free>=budget;if(mode=="usb_then_internal"||mode=="swap"){auto internal=destination(get(r,"internal"),root);require(mode=="swap"||contains("/data/homebrew",internal),"Final copy must be on internal storage");if(mode=="swap"){auto identity=identifyOriginal(game.at("path").get<std::string>());struct stat target{},working{};require(stat(internal.parent_path().c_str(),&target)==0&&stat(out.parent_path().c_str(),&working)==0&&target.st_dev==identity.device&&working.st_dev!=identity.device,"Swap must use different drives and return the converted game to the original drive");}require(!contains(out,internal)&&!contains(internal,out),"Output folders overlap");auto internalFree=freeSpace(internal.parent_path());p["internalFreeBytes"]=internalFree;p["internal"]=internal.string();enough=enough&&internalFree>=budget;struct stat a{},b{};require(stat(out.parent_path().c_str(),&a)==0&&stat(internal.parent_path().c_str(),&b)==0,"Cannot inspect destination devices");require(a.st_dev!=b.st_dev,"USB working output and internal copy must be on different devices");}
    p["verifyOutput"]=verifyOutput;p["verifyCopy"]=verifyCopy;p["originalAction"]=original;
    bool verified=verifyOutput&&((mode!="usb_then_internal"&&mode!="swap")||verifyCopy);p["swapOriginal"]=swapping;p["verificationSkipped"]=!verified;
    if(original!="keep"){auto originalPath=fs::path(game.at("path").get<std::string>());auto identity=identifyOriginal(originalPath);require(originalPath!=out,"Original and output overlap");if(original=="move"){auto folder=backupFolder(r,root,out);if(mode=="usb_then_internal"||mode=="swap")require(!contains(fs::path(p.at("internal").get<std::string>()),folder)&&!contains(folder,fs::path(p.at("internal").get<std::string>())),"Backup and internal output folders overlap");require(!fs::exists(folder/originalPath.filename()),"Backup already exists; choose another folder");struct stat device{},outputDevice{},internalDevice{};require(stat(folder.c_str(),&device)==0&&stat(out.parent_path().c_str(),&outputDevice)==0,"Cannot inspect backup drive");if(swapping)require(device.st_dev==outputDevice.st_dev,"Swap must move the original to the conversion destination drive");if(device.st_dev!=identity.device){uint64_t needed=uint64_t(identity.size);if(device.st_dev==outputDevice.st_dev){require(budget<=UINT64_MAX-needed,"Space budget overflow");needed+=budget;}if(mode=="usb_then_internal"||mode=="swap"){require(stat(fs::path(p.at("internal").get<std::string>()).parent_path().c_str(),&internalDevice)==0,"Cannot inspect internal output drive");if(device.st_dev==internalDevice.st_dev){require(budget<=UINT64_MAX-needed,"Space budget overflow");needed+=budget;}}enough=enough&&freeSpace(folder)>=needed;}p["originalBackup"]=folder.string();}}
    p["canStart"]=enough;p["message"]=enough?(verified?"Plan ready. Selected original-file action runs after conversion.":"Plan ready. Verification will be skipped; output will be marked unverified."):"Not enough destination or backup space.";return p;
}
static void appendLog(const std::string& line){std::lock_guard<std::mutex> guard(stateLock);logTail+=line;if(logTail.size()>131072)logTail.erase(0,logTail.size()-131072);std::ofstream file(fs::path(AMPR_STATE_ROOT)/"logs/current.log",std::ios::app);file<<line;}
struct SourceGuard {fs::path path;dev_t device;ino_t inode;std::chrono::steady_clock::time_point checked{};bool changed=false;std::mutex lock;};
static int cancellation(void* context){if(abortJob)return 1;auto& source=*static_cast<SourceGuard*>(context);std::lock_guard<std::mutex> guard(source.lock);if(seconds(source.checked)<1)return source.changed;source.checked=std::chrono::steady_clock::now();struct stat info{};if(gameRunning()||stat(source.path.c_str(),&info)||info.st_dev!=source.device||info.st_ino!=source.inode||!readOnly(source.path)){source.changed=true;appendLog("Game started or source mount changed. Stopping.\n");}return source.changed;}
static void phaseCallback(void*,const char* value){std::lock_guard<std::mutex> guard(stateLock);std::string phase=value;if(state.value("swapOriginal",false)){if(phase=="Copying output to internal")phase="Returning converted game";if(phase=="Verifying internal copy")phase="Verifying returned game";}state["phase"]=phase;state["done"]=0;state["total"]=0;state["stagePercent"]=0.0;stageBegan=std::chrono::steady_clock::now();state["progressUnit"]=phase=="Planning file rules and sampling"||phase=="Building source index"?"files":"bytes";}
static void progressCallback(void*,uint64_t done,uint64_t total){std::lock_guard<std::mutex> guard(stateLock);state["done"]=done;state["total"]=total;state["stagePercent"]=total?std::min(100.0,100.0*done/total):0;}
static void logCallback(void*,const char* line){appendLog(line);std::string text=line;if(text.rfind("Packing ",0)==0){std::lock_guard<std::mutex> guard(stateLock);state["currentFile"]=text;}}
static J backendJson(void(*function)(char*,size_t),size_t limit=1024*1024){std::vector<char> bytes(limit);function(bytes.data(),bytes.size());return J::parse(bytes.data());}
static void restartShadowMount(const std::string& titleId){
    require(!abortJob&&!gameRunning(),"ShadowMount restart skipped: conversion canceled or a game is running");
    fs::path payload;std::ifstream autoload("/data/pldmgr/autoload.txt");std::string name;
    while(std::getline(autoload,name)){if(!name.empty()&&name.back()=='\r')name.pop_back();auto lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return std::tolower(c);});if(lower.find("shadowmount")==std::string::npos||fs::path(name).filename()!=name||fs::path(name).extension()!=".elf")continue;
        for(const auto& entry:fs::recursive_directory_iterator("/data/pldmgr/payloads")){if(entry.path().filename()!=name||!entry.is_regular_file()||entry.is_symlink())continue;require(payload.empty(),"Multiple ShadowMount payloads found");payload=entry.path();}}
    require(!payload.empty(),"ShadowMount payload was not found in Payload Manager autoload");
    phaseCallback(nullptr,"Restarting ShadowMount");appendLog("Restarting ShadowMount after conversion.\n");
    auto listing=shadow("games");for(const auto& game:listing.at("games"))if(game.value("title_id","")==titleId&&game.value("mounted",false))shadow("games/unmount",J{{"title_id",titleId}});
    require(!abortJob&&!gameRunning(),"ShadowMount restart skipped: conversion canceled or a game is running");
    std::string encoded;const char* hex="0123456789ABCDEF";for(unsigned char c:payload.string()){if(std::isalnum(c)||c=='/'||c=='-'||c=='_'||c=='.')encoded+=char(c);else{encoded+='%';encoded+=hex[c>>4];encoded+=hex[c&15];}}
    auto response=getLoopback(8084,"/loadpayload:"+encoded);require(response.first.find(" 200 ")!=std::string::npos,"Payload Manager could not reload ShadowMount");
    // Allow the replacement instance to finish its graceful handoff before requesting a scan.
    std::this_thread::sleep_for(std::chrono::seconds(3));
    for(int attempt=0;attempt<15;attempt++){try{shadow("scan");appendLog("ShadowMount restarted; library scan requested.\n");return;}catch(const std::exception&){if(attempt==14)throw;std::this_thread::sleep_for(std::chrono::seconds(1));}}
}
static void launch(const Request&r,bool benchmark){
    Request planning=r;if(benchmark){planning.query["originalAction"]="keep";if(get(planning,"mode")=="swap")planning.query["mode"]=get(planning,"output").rfind("/data/",0)==0?"internal":get(planning,"output").rfind("/mnt/ext1/",0)==0?"m2":"usb";}auto p=plan(planning,!benchmark);require(p.value("canStart",false),p.value("message","Plan is unavailable"));
    std::string root=p.at("mountedRoot"),out=p.at("output"),internal=p.value("internal","");bool verifyOutput=p.value("verifyOutput",true),verifyCopy=p.value("verifyCopy",true);std::string original=p.value("originalAction","keep"),backup=p.value("originalBackup","");auto game=findGame(r);std::string originalPath=game.value("path","");std::string titleId=game.value("title_id","");OriginalIdentity originalIdentity{};if(!benchmark&&original!="keep")originalIdentity=identifyOriginal(originalPath);bool verified=verifyOutput&&(internal.empty()||verifyCopy);
    if(!benchmark)requireDeleteConsent(original,verified,get(r,"confirmOriginalDelete")=="yes",get(r,"confirmUnverifiedDelete")=="yes");
    bool swapping=p.value("swapOriginal",false);bool restartAfter=!benchmark&&get(r,"restartShadowMount","no")=="yes";require(get(r,"restartShadowMount","no")=="yes"||get(r,"restartShadowMount","no")=="no","Invalid ShadowMount restart option");int level=integer(r,"level",7);struct stat info{};require(stat(root.c_str(),&info)==0,"Source mount disappeared");
    auto runtime=fs::path(AMPR_STATE_ROOT)/"libSceAmpr.sprx";require(fs::is_regular_file(runtime),"AMPR runtime is unavailable");
    if(benchmark)require(freeSpace(fs::path(out).parent_path())>=uint64_t(2400)*1024*1024,"Performance test needs at least 2.2 GiB free");
    sharedSetup["sourcePath"]=originalPath;sharedSetup["output"]=out;sharedSetup["internal"]=internal;sharedSetup["level"]=std::to_string(level);sharedSetup["verifyOutput"]=verifyOutput;sharedSetup["verifyCopy"]=verifyCopy;sharedSetup["restartShadowMount"]=restartAfter;sharedSetup["originalAction"]=swapping?"swap":original;
    sharedSetup["destinationStorage"]=out.rfind("/data/",0)==0?"/data/homebrew":out.rfind("/mnt/ext1/",0)==0?"/mnt/ext1/games":out.substr(0,9);++setupRevision;
    {std::lock_guard<std::mutex> guard(stateLock);state={{"ok",true},{"active",true},{"started",true},{"result",0},{"phase","Preparing"},{"done",0},{"total",0},{"stagePercent",0.0},{"benchmark",benchmark},{"destination",out},{"sourcePath",originalPath},{"gameName",game.value("title_name",titleId)},{"level",level},{"error",""},{"verified",false},{"verificationSkipped",!verified},{"originalAction",original},{"swapOriginal",swapping},{"verifyOutput",verifyOutput},{"verifyCopy",verifyCopy},{"copyToInternal",!internal.empty()},{"restartShadowMount",restartAfter},{"originalStatus","Pending"}};logTail.clear();began=stageBegan=std::chrono::steady_clock::now();state["jobId"]=std::to_string(getpid())+"-"+std::to_string(began.time_since_epoch().count());std::ofstream(fs::path(AMPR_STATE_ROOT)/"logs/current.log",std::ios::trunc);}
    abortJob=false;if(benchmark)ampr_backend_benchmark_prepare();int depth=get(r,"depth","quick")=="full"?1:get(r,"depth")=="io"?2:0;
    startDetached([root,out,internal,level,runtime,benchmark,depth,info,verifyOutput,verifyCopy,verified,original,originalPath,originalIdentity,backup,titleId,restartAfter,swapping](){SourceGuard source{root,info.st_dev,info.st_ino};ampr_backend_callbacks callbacks{&source,cancellation,phaseCallback,progressCallback,logCallback};char error[2048]={};int result=-1;DirectoryIdentity workingIdentity{},returnedIdentity{};try{if(benchmark)result=ampr_backend_benchmark_game(root.c_str(),fs::path(out).parent_path().c_str(),level,depth,nullptr,&callbacks,error,sizeof(error));else{result=ampr_backend_pack_game_options(root.c_str(),out.c_str(),level,runtime.c_str(),nullptr,verifyOutput,&callbacks,error,sizeof(error));if(!result&&swapping)workingIdentity=identifyDirectory(out);if(!result&&!internal.empty())result=ampr_backend_copy_options(out.c_str(),internal.c_str(),verifyCopy,&callbacks,error,sizeof(error));if(!result&&swapping)returnedIdentity=identifyDirectory(internal);if(!result){std::lock_guard<std::mutex> guard(stateLock);state["verified"]=verified;}if(!result&&original!="keep"){require(!abortJob&&!gameRunning(),"Original retained: canceled or game started");shadow("games/unmount",J{{"title_id",titleId}});struct stat mounted{};require(stat(root.c_str(),&mounted)!=0||mounted.st_dev!=info.st_dev||mounted.st_ino!=info.st_ino||!readOnly(root),"Original retained: source is still mounted");handleOriginal(originalPath,originalIdentity,original,backup,[](){require(!abortJob&&!gameRunning(),"Original operation canceled; original retained if not already moved");},[](const char* name){phaseCallback(nullptr,name);},[](uint64_t done,uint64_t total){progressCallback(nullptr,done,total);});}if(!result&&swapping){phaseCallback(nullptr,"Finishing swap");finishSwap(out,workingIdentity,internal,returnedIdentity,originalPath,backup,originalIdentity,[](){require(!abortJob&&!gameRunning(),"Swap copies are ready; canceled or game started, so temporary converted copy retained");});{std::lock_guard<std::mutex> guard(stateLock);state["destination"]=internal;}appendLog("Swap completed: converted game returned to the original drive; original moved to the destination drive.\n");}if(!result){std::lock_guard<std::mutex> guard(stateLock);state["originalStatus"]=original=="delete"?"Deleted":original=="move"?"Moved":"Kept in place";}}if(!result&&restartAfter){try{restartShadowMount(titleId);std::lock_guard<std::mutex> guard(stateLock);state["shadowMountStatus"]="Restarted; library scan requested";}catch(const std::exception&e){appendLog(std::string("WARNING: Conversion completed, but ")+e.what()+"\n");std::lock_guard<std::mutex> guard(stateLock);state["shadowMountStatus"]=std::string("Warning: ")+e.what();}}}catch(const std::exception&e){result=-1;appendLog(std::string("ERROR: ")+e.what()+"\n");snprintf(error,sizeof(error),"%s",e.what());if(benchmark)ampr_backend_benchmark_failed(error);}std::lock_guard<std::mutex> guard(stateLock);state["elapsedSeconds"]=seconds(began);state["stageElapsedSeconds"]=seconds(stageBegan);state["active"]=false;state["result"]=result;state["error"]=error;state["phase"]=result?(abortJob?"Canceled":"Stopped"):(verified?"Complete: output verified":"Complete: output unverified");});
}
static J status(){J s;{std::lock_guard<std::mutex> guard(stateLock);s=state;if(s.value("active",false)){s["elapsedSeconds"]=seconds(began);s["stageElapsedSeconds"]=seconds(stageBegan);}else if(!s.value("started",false)){s["elapsedSeconds"]=0;s["stageElapsedSeconds"]=0;}}s["processId"]=getpid();s["performance"]=backendJson(ampr_backend_metrics,131072);if(s.value("phase",std::string()).find("original")!=std::string::npos)s["performance"]=J::object();s["partialFolders"]=backendJson(ampr_backend_partial_info,32768);return s;}
static J setupRoute(const Request&r){
    std::lock_guard<std::mutex> operation(control);
    if(r.method=="POST"){
        {std::lock_guard<std::mutex> guard(stateLock);require(!state.value("active",false),"Settings cannot change during a job");}
        auto patch=J::parse(get(r,"settings"));require(patch.is_object()&&patch.size()<=12,"Invalid shared settings");
        static const std::set<std::string> fields={"sourcePath","destinationStorage","originalAction","output","internal","level","verifyOutput","verifyCopy","restartShadowMount","originalLocation","originalSubfolder"};
        for(auto it=patch.begin();it!=patch.end();++it){require(fields.count(it.key()),"Unknown shared setting");bool flag=it.key()=="verifyOutput"||it.key()=="verifyCopy"||it.key()=="restartShadowMount";require(flag?it.value().is_boolean():it.value().is_string(),"Invalid shared setting type");if(!flag)require(it.value().get<std::string>().size()<=4096,"Setting too long");}
        sharedSetup.merge_patch(patch);++setupRevision;
    }
    return J{{"ok",true},{"revision",setupRevision},{"settings",sharedSetup}};
}
#include "updater.inc"
static J route(const Request&r){
    auto path=r.path;bool mutation=r.method=="POST";require(mutation||r.method=="GET","Unsupported method");
    if(path=="/api/gc/ampr-lz4/update")return updaterStatus();
    if(path=="/api/gc/ampr-lz4/update/check"||path=="/api/gc/ampr-lz4/update/install"){require(mutation,"Use POST for updates");std::lock_guard<std::mutex> lock(control);{std::lock_guard<std::mutex> guard(stateLock);require(!state.value("active",false),"Wait for the conversion or performance test to finish before updating");}updateStart(path.find("/install")!=std::string::npos);return updaterStatus();}
    if(updateBusy&&mutation)throw std::runtime_error("Wait for the update to finish");
    if(path=="/api/gc/ampr-lz4/setup")return setupRoute(r);
    if(path=="/api/gc/ampr-lz4/status")return status();
    if(path=="/api/control/handoff-state"){auto s=status();return J{{"ok",true},{"busy",s["active"]}};}
    if(path=="/api/gc/ampr-lz4/benchmark/status"){auto s=backendJson(ampr_backend_benchmark_state,8*1024*1024);s["ok"]=true;return s;}
    if(path=="/api/gc/job/cancel"){require(mutation,"Use POST to cancel");abortJob=true;return J{{"ok",true},{"message","Cancellation requested"}};}
    if(path=="/api/gc/games"){J games=J::array();for(auto& g:gameLibrary())games.push_back(J{{"name",g.value("title_name",g.value("title_id",""))},{"titleId",g.value("title_id","")},{"sourcePath",g.value("path","")},{"sourceStorage",storageName(g.value("path",std::string()))},{"sourceType",g.value("image_backed",false)?"image":"folder"},{"readOnlyVerified",g.value("mounted",false)&&readOnly(g.value("runtime_path",std::string()))}});return J{{"ok",true},{"games",games}};}
    if(path=="/api/gc/ampr-lz4/capacity"){J devices=J::array();for(int i=-2;i<8;i++){fs::path p=i==-2?"/data":i==-1?"/mnt/ext1":("/mnt/usb"+std::to_string(i));J d={{"name",i==-2?"Internal SSD":i==-1?"M.2 SSD":"USB "+std::to_string(i)},{"path",i==-2?"/data/homebrew":i==-1?"/mnt/ext1/games":p.string()},{"available",false},{"freeBytes",0},{"totalBytes",0}};struct statvfs v{};if(fs::is_directory(p)&&statvfs(p.c_str(),&v)==0&&v.f_frsize&&v.f_blocks<=UINT64_MAX/v.f_frsize&&v.f_bavail<=UINT64_MAX/v.f_frsize){struct stat device{},parent{};bool mounted=i==-2||(stat(p.c_str(),&device)==0&&stat(p.parent_path().c_str(),&parent)==0&&device.st_dev!=parent.st_dev);d["available"]=mounted&&v.f_blocks>0&&!(v.f_flag&ST_RDONLY);d["freeBytes"]=uint64_t(v.f_bavail)*v.f_frsize;d["totalBytes"]=uint64_t(v.f_blocks)*v.f_frsize;}devices.push_back(d);}return J{{"ok",true},{"devices",devices}};}
    if(path=="/api/gc/ampr-lz4/size"){auto g=findGame(r);auto root=g.value("runtime_path","");require(g.value("mounted",false)&&readOnly(root),"Mount the source read-only first");uint64_t n=0,b=0;char error[1024]={};int rc=ampr_backend_measure(root.c_str(),&n,&b,error,sizeof(error));require(rc==0,error);return J{{"ok",true},{"logicalBytes",n},{"requiredOutputBytes",b}};}
    std::lock_guard<std::mutex> operation(control);require(!updateBusy,"Wait for the update to finish");bool active;{std::lock_guard<std::mutex> guard(stateLock);active=state.value("active",false);}require(!active,"Wait until the current job finishes");
    if(path=="/api/gc/ampr-lz4/mount"){require(mutation,"Use POST to mount");require(!gameRunning(),"Close the running game before changing mounts");auto selected=findGame(r);releaseFolderView(AMPR_STATE_ROOT);J listing=J::array();try{listing=shadow("games").at("games");}catch(const std::exception&){require(!selected.value("image_backed",false),"ShadowMount is unavailable");}for(auto& g:listing)if(g.value("managed",false)&&g.value("mounted",false))shadow("games/unmount",J{{"title_id",g.at("title_id")}});if(selected.value("image_backed",false))shadow("games/mount",J{{"title_id",selected.at("title_id")},{"mode","ro"}});else mountFolderView(AMPR_STATE_ROOT,selected.at("path").get<std::string>());return J{{"ok",true},{"message","Source mount prepared. Read-only verification follows."}};}
    if(path=="/api/gc/ampr-lz4/plan")return plan(r);
    if(path=="/api/gc/ampr-lz4/start"||path=="/api/gc/ampr-lz4/benchmark"){require(mutation,"Use POST to start");launch(r,path=="/api/gc/ampr-lz4/benchmark");return J{{"ok",true},{"message","Started"}};}
    if(path=="/api/gc/ampr-lz4/delete-partial"){require(mutation,"Use POST to delete partial output");char error[1024]={};int rc=ampr_backend_delete_partial(error,sizeof(error));require(rc==0,error);auto remaining=backendJson(ampr_backend_partial_info,32768);require(remaining.empty(),"Partial output remains; inspect diagnostics");return J{{"ok",true}};}
    if(path=="/api/gc/ampr-lz4/threads"){std::string input;if(mutation){J setting={{"packing",{{"readers",integer(r,"packReaders",2,6)},{"workers",integer(r,"packWorkers",3,6)},{"writers",integer(r,"packWriters",2,6)}}},{"loose",{{"workers",0},{"readers",integer(r,"looseReaders",3,6)},{"writers",integer(r,"looseWriters",4,6)}}},{"verification",{{"writers",0},{"readers",integer(r,"verifyReaders",3,6)},{"workers",integer(r,"verifyWorkers",4,6)}}}};input=setting.dump();}char output[8192]={};int rc=ampr_backend_thread_settings(mutation?input.c_str():nullptr,output,sizeof(output));require(rc==0,output);auto j=J::parse(output);j["ok"]=true;return j;}
    if(path=="/api/gc/ampr-lz4/benchmark/apply"){require(mutation,"Use POST to apply settings");char output[32768]={};int rc=ampr_backend_benchmark_apply(get(r,"id").c_str(),output,sizeof(output));require(rc==0,output);auto j=J::parse(output);j["ok"]=true;return j;}
    throw std::runtime_error("Unknown endpoint");
}
static void handle(int fd){Socket client(fd);timeout(fd,15);try{auto request=parse(receive(fd,16384));if(request.path=="/api/gc/ampr-lz4/log"){require(request.method=="GET","Use GET for logs");std::string tail;{std::lock_guard<std::mutex> guard(stateLock);tail=logTail;}respond(fd,200,tail,"text/plain; charset=utf-8");return;}if(request.path.rfind("/api/",0)==0){respond(fd,200,route(request).dump());return;}require(request.method=="GET","Use GET for assets");auto path=request.path=="/"?"/ampr-studio.html":request.path;for(auto& a:embeddedAssets){if(path=="/"+std::string(a.name)&&std::string(a.name)!="ampr-runtime.sprx"&&std::string(a.name)!="update-helper.elf"){respond(fd,200,std::string(reinterpret_cast<const char*>(a.data),a.size),a.mime);return;}}respond(fd,404,J{{"ok",false},{"error","Not found"}}.dump());}catch(const std::exception&e){try{respond(fd,400,J{{"ok",false},{"error",e.what()}}.dump());}catch(...){}}}
#ifndef STUDIO_HOST_TEST
int installMediaShortcut();
#endif
int main(){signal(SIGPIPE,SIG_IGN);try{
    fs::create_directories(fs::path(AMPR_STATE_ROOT)/"logs");auto saved=fs::path(AMPR_STATE_ROOT)/"shared-setup.json";if(fs::is_regular_file(saved)){auto setup=J::parse(updates::read(saved),nullptr,false);if(setup.is_object()){sharedSetup=setup;++setupRevision;}}for(auto& a:embeddedAssets){if(std::string(a.name)=="ampr-runtime.sprx"){auto path=fs::path(AMPR_STATE_ROOT)/"libSceAmpr.sprx";std::ofstream f(path,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(a.data),a.size);f.flush();require(bool(f),"Cannot install AMPR runtime");require(chmod(path.c_str(),0644)==0,"Cannot set runtime permissions");}}
    Socket server(socket(AF_INET,SOCK_STREAM,0));require(server.fd>=0,"Cannot create service socket");int one=1;setsockopt(server.fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(STUDIO_PORT);address.sin_addr.s_addr=htonl(INADDR_ANY);require(bind(server.fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"Port is already in use");require(listen(server.fd,16)==0,"Cannot listen");
#ifndef STUDIO_HOST_TEST
    if(installMediaShortcut())appendLog("WARNING: Media shortcut installation failed. See /data/AMPRStudio/launcher-install.log. The compressor is still available in the browser.\n");
    else appendLog("Media shortcut and icon installed.\n");
#endif
    std::mutex lock;std::condition_variable available;std::deque<int> pending;
    for(int n=0;n<4;n++)startDetached([&](){for(;;){int fd;{std::unique_lock<std::mutex> guard(lock);available.wait(guard,[&](){return !pending.empty();});fd=pending.front();pending.pop_front();}handle(fd);}},1024*1024);
    for(;;){int fd=accept(server.fd,nullptr,nullptr);if(fd<0){if(errno==EINTR)continue;throw std::runtime_error("Accept failed");}{std::lock_guard<std::mutex> guard(lock);if(pending.size()>=16){close(fd);continue;}pending.push_back(fd);}available.notify_one();}
}catch(const std::exception&e){fprintf(stderr,"LZ4 Compressor: %s\n",e.what());return 1;}}
