// SPDX-License-Identifier: GPL-3.0-or-later
#include "update_common.h"
#include <thread>
#include <chrono>
#include <signal.h>
using namespace updates;
#ifdef STUDIO_HOST_TEST
constexpr int appPort=15912,managerPort=18084;
constexpr const char* autoloadPath=TEST_UPDATE_AUTOLOAD;
static void updatePause(){std::this_thread::sleep_for(std::chrono::milliseconds(10));}
#else
constexpr int appPort=5911,managerPort=8084;
constexpr const char* autoloadPath="/data/pldmgr/autoload.txt";
static void updatePause(){std::this_thread::sleep_for(std::chrono::seconds(1));}
#endif
static void report(const std::string& phase,const std::string& message){atomicWrite(fs::path(folder)/"result.json",Json{{"phase",phase},{"message",message}}.dump());}
static bool listening(){studio::Socket s(socket(AF_INET,SOCK_STREAM,0));if(s.fd<0)return false;studio::timeout(s.fd,2);sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(appPort);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);return connect(s.fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0;}
static bool online(){try{return studio::getLoopback(appPort,"/api/control/handoff-state").first.find(" 200 ")!=std::string::npos;}catch(...){return false;}}
int main(){signal(SIGPIPE,SIG_IGN);bool validated=false;try{
 auto request=Json::parse(read(fs::path(folder)/"handoff.json"));studio::require(request.value("payload",std::string())==payload,"Invalid updater handoff");validated=true;
 report("restarting","Restarting the app...");
 for(int n=0;n<30&&listening();n++)updatePause();
 studio::require(!listening(),"Old app did not stop; update files retained for recovery");
 auto response=studio::getLoopback(managerPort,std::string("/loadpayload:")+payload);studio::require(response.first.find(" 200 ")!=std::string::npos,"Payload Manager could not start update");
 for(int n=0;n<20;n++){updatePause();if(!online())continue;auto state=Json::parse(studio::getLoopback(appPort,"/api/gc/ampr-lz4/update").body);
 studio::require(version(state.at("currentVersion"))==version(request.at("version")),"Unexpected app version after restart; check Payload Manager");
 // Change autoload only after the replacement service is healthy.
 auto autoload=fs::path(autoloadPath);std::string prior=fs::exists(autoload)?read(autoload):"";std::istringstream input(prior);std::string line,out;
 while(std::getline(input,line)){if(line.rfind("LZ4-Game-Compressor",0)==0||line=="lz4-game-compressor.elf")continue;out+=line+"\n";}out+=std::string(assetName)+"\n";atomicWrite(autoload,out);
 report("complete","Update installed. The app is ready.");return 0;}
 throw std::runtime_error("New app did not become ready");
 }catch(const std::exception&e){try{
  std::string message=e.what();
  if(validated&&!listening()&&fs::is_regular_file(fs::path(folder)/"previous.elf")){
   atomicWrite(payload,read(fs::path(folder)/"previous.elf"),0755);
   auto response=studio::getLoopback(managerPort,std::string("/loadpayload:")+payload);
   message+=response.first.find(" 200 ")!=std::string::npos?". Previous build restored; reload the browser.":". Previous build restored; load LZ4-Game-Compressor.elf in Payload Manager.";
  }
  report("error",message);
 }catch(...){try{report("error","Restart failed. Load LZ4-Game-Compressor.elf in Payload Manager; recovery copy is in /data/AMPRStudio/update/previous.elf.");}catch(...){}}return 1;}}
