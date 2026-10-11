// SPDX-License-Identifier: GPL-3.0-or-later
static std::mutex updateLock;
static J updateState={{"ok",true},{"currentVersion",APP_VERSION},{"phase","idle"},{"available",false},{"message",""}};
static J selectedUpdate=nullptr;
static void updateMessage(const std::string& phase,const std::string& message){std::lock_guard<std::mutex> lock(updateLock);updateState["phase"]=phase;updateState["message"]=message;}
static J updaterStatus(){std::lock_guard<std::mutex> lock(updateLock);auto s=updateState;s["busy"]=updateBusy.load();auto file=fs::path(updates::folder)/"result.json";if(!updateBusy&&fs::is_regular_file(file)){auto result=J::parse(updates::read(file),nullptr,false);if(result.is_object())s["lastResult"]=result;}return s;}
static void updateStart(bool install){
 require(!updateBusy.exchange(true),"An update operation is already running");
 try{if(install){require(!selectedUpdate.is_null(),"Check for an available update first");require(fs::is_regular_file(updates::payload)&&!fs::is_symlink(updates::payload),"Load LZ4-Game-Compressor.elf from its stable Payload Manager folder before using automatic updates");require(!gameRunning(),"Close the game before updating");}
 updateMessage(install?"downloading":"checking",install?"Downloading update...":"Checking GitHub releases...");
 auto release=selectedUpdate;
 startDetached([install,release](){bool replaced=false;try{
  if(!install){auto data=updates::download("https://api.github.com/repos/gameboy829/PS5-LZ4-Game-Compressor/releases?per_page=100",4*1024*1024);auto found=updates::select(J::parse(data),APP_VERSION);std::lock_guard<std::mutex> lock(updateLock);selectedUpdate=found;updateState["available"]=!found.is_null();updateState["release"]=found;updateState["phase"]="ready";updateState["message"]=found.is_null()?"No newer release with an installable ELF is available.":"Update available: "+found.at("version").get<std::string>();}
  else{
   require(freeSpace("/data")>=release.at("size").get<uint64_t>()*3+8*1024*1024,"Not enough internal space for update and backup");
   auto data=updates::download(release.at("url"),64*1024*1024,[](size_t n){std::lock_guard<std::mutex> lock(updateLock);updateState["downloadedBytes"]=n;});updates::verify(data,release);
   updateMessage("installing","Checksum verified. Installing update...");
   {std::lock_guard<std::mutex> guard(control);updates::atomicWrite(fs::path(AMPR_STATE_ROOT)/"shared-setup.json",sharedSetup.dump());}
   updates::atomicWrite(fs::path(updates::folder)/"previous.elf",updates::read(updates::payload),0755);
   updates::atomicWrite(updates::payload,data,0755);replaced=true;
   const EmbeddedAsset* helper=nullptr;for(const auto& a:embeddedAssets)if(std::string(a.name)=="update-helper.elf")helper=&a;require(helper,"Restart helper is unavailable");
   auto helperPath=fs::path(updates::payload).parent_path()/"update-helper.elf";updates::atomicWrite(helperPath,std::string(reinterpret_cast<const char*>(helper->data),helper->size),0755);
   updates::atomicWrite(fs::path(updates::folder)/"handoff.json",J{{"payload",updates::payload},{"version",release.at("version")}}.dump());
   updates::atomicWrite(fs::path(updates::folder)/"result.json",J{{"phase","restarting"},{"message","Restarting the app..."}}.dump());
   auto response=getLoopback(8084,"/loadpayload:"+helperPath.string());require(response.first.find(" 200 ")!=std::string::npos&&response.body.find("OK")!=std::string::npos,"Payload Manager could not start restart helper");
   updateMessage("restarting","Restarting the app. This page will reconnect automatically.");std::this_thread::sleep_for(std::chrono::seconds(3));_exit(0);
  }
 }catch(const std::exception&e){if(replaced){try{updates::atomicWrite(updates::payload,updates::read(fs::path(updates::folder)/"previous.elf"),0755);}catch(...){} }updateMessage("error",e.what());}updateBusy=false;});
 }catch(...){updateBusy=false;throw;}
}
