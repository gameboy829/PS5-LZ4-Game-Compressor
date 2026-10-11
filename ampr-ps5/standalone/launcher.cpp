// SPDX-License-Identifier: GPL-3.0-or-later
// Application-authored shortcut installer and updater.
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdio>
#include <sys/stat.h>
#include <ps5/kernel.h>
#include <unistd.h>
#include "launcher-assets.h"
extern "C" int sceAppInstUtilInitialize();
extern "C" int sceAppInstUtilAppInstallAll(void*);
extern "C" int sceKernelLoadStartModule(const char*,size_t,const void*,unsigned,void*,int*);
namespace fs=std::filesystem;
int installMediaShortcut(){fs::create_directories("/data/AMPRStudio");FILE* log=fopen("/data/AMPRStudio/launcher-install.log","w");try{
    fs::path folder="/user/app/LZGC50001/sce_sys";bool existing=false;
    if(fs::is_regular_file(folder/"param.json")){std::ifstream input(folder/"param.json");std::string text((std::istreambuf_iterator<char>(input)),{});existing=text.find("LZGC50001")!=std::string::npos&&text.find("http://127.0.0.1:5911/")!=std::string::npos;if(!existing)throw std::runtime_error("Existing shortcut identity changed; refusing overwrite");}
    fs::create_directories(folder);
    for(auto& asset:launcherAssets){auto file=folder/asset.name,temporary=folder/(std::string(asset.name)+".lz4-new");std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);stream.write(reinterpret_cast<const char*>(asset.data),asset.size);stream.flush();if(!stream)throw std::runtime_error("Cannot write "+file.string());stream.close();if(chmod(temporary.c_str(),0644))throw std::runtime_error("Cannot set launcher permissions");fs::rename(temporary,file);}
    // The newer firmware registration API scans staged application metadata.
    int result=sceAppInstUtilInitialize();if(log){fprintf(log,"Initialize result=0x%08x\n",unsigned(result));fflush(log);}if(result<0)throw std::runtime_error("Registration initialization failed: "+std::to_string(result));
    result=sceAppInstUtilAppInstallAll(nullptr);if(log){fprintf(log,"AppInstallAll result=0x%08x\n",unsigned(result));fflush(log);}if(result<0)throw std::runtime_error("Registration failed: "+std::to_string(result));if(log){fprintf(log,"OK shortcut registration scan completed\n");fclose(log);}return 0;}catch(const std::exception&e){if(log){fprintf(log,"ERROR %s\n",e.what());fclose(log);}return 1;}}

#ifdef LZ4_SHORTCUT_ONLY
int main(){return installMediaShortcut();}
#endif
