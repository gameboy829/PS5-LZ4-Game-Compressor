// SPDX-License-Identifier: GPL-3.0-or-later
// Application-authored shortcut installer and updater.
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdio>
#include <sys/stat.h>
#include <ps5/kernel.h>
#include "launcher-assets.h"
extern "C" int sceKernelLoadStartModule(const char*,size_t,const void*,unsigned,void*,int*);
namespace fs=std::filesystem;
int installMediaShortcut(){fs::create_directories("/data/AMPRStudio");FILE* log=fopen("/data/AMPRStudio/launcher-install.log","w");try{
    fs::path folder="/user/app/LZGC50001/sce_sys";bool existing=false;
    if(fs::is_regular_file(folder/"param.json")){std::ifstream input(folder/"param.json");std::string text((std::istreambuf_iterator<char>(input)),{});existing=text.find("LZGC50001")!=std::string::npos&&text.find("http://127.0.0.1:5911/")!=std::string::npos;if(!existing)throw std::runtime_error("Existing shortcut identity changed; refusing overwrite");}
    fs::create_directories(folder);
    for(auto& asset:launcherAssets){auto file=folder/asset.name,temporary=folder/(std::string(asset.name)+".lz4-new");std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);stream.write(reinterpret_cast<const char*>(asset.data),asset.size);stream.flush();if(!stream)throw std::runtime_error("Cannot write "+file.string());stream.close();if(chmod(temporary.c_str(),0644))throw std::runtime_error("Cannot set launcher permissions");fs::rename(temporary,file);}
    if(existing){if(log){fprintf(log,"OK existing Media shortcut assets refreshed\n");fclose(log);}return 0;}
    uint32_t module=0;if(kernel_dynlib_handle(-1,"libSceAppInstUtil.sprx",&module)){int loaded=sceKernelLoadStartModule("/system/common/lib/libSceAppInstUtil.sprx",0,nullptr,0,nullptr,nullptr);if(loaded<=0)throw std::runtime_error("Cannot load registration module");module=uint32_t(loaded);}
    auto initialize=reinterpret_cast<int(*)()>(kernel_dynlib_dlsym(-1,module,"sceAppInstUtilInitialize"));auto install=reinterpret_cast<int(*)(const char*,const char*,void*)>(kernel_dynlib_dlsym(-1,module,"sceAppInstUtilAppInstallTitleDir"));auto terminate=reinterpret_cast<int(*)()>(kernel_dynlib_dlsym(-1,module,"sceAppInstUtilTerminate"));
    if(!initialize||!install||!terminate)throw std::runtime_error("Title registration unavailable");int result=initialize();if(result<0)throw std::runtime_error("Registration initialization failed");result=install("LZGC50001","/user/app/",nullptr);terminate();if(result<0)throw std::runtime_error("Registration failed: "+std::to_string(result));if(log){fprintf(log,"OK new shortcut registered\n");fclose(log);}return 0;
}catch(const std::exception&e){if(log){fprintf(log,"ERROR %s\n",e.what());fclose(log);}return 1;}}

#ifdef LZ4_SHORTCUT_ONLY
int main(){return installMediaShortcut();}
#endif
