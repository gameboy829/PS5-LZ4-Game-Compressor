// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "http.h"
#include "../../ampr-native/third_party/config/json.hpp"
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>
#include <cstring>
#include <sys/stat.h>
#include <sys/statvfs.h>
#ifndef STUDIO_HOST_TEST
#include <sys/mount.h>
#include <sys/uio.h>
#endif
namespace studio {
namespace libraryfs=std::filesystem;
using LibraryJson=nlohmann::json;
inline bool pathBelow(const libraryfs::path& parent,const libraryfs::path& child){auto a=libraryfs::weakly_canonical(parent),b=libraryfs::weakly_canonical(child);auto i=a.begin(),j=b.begin();while(i!=a.end()&&j!=b.end()&&*i==*j){++i;++j;}return i==a.end();}
inline bool validTitle(const std::string& id){return id.size()==9&&id.substr(0,4)=="PPSA"&&std::all_of(id.begin()+4,id.end(),[](char c){return c>='0'&&c<='9';});}
inline std::vector<libraryfs::path> libraryRoots(){
#ifdef STUDIO_LIBRARY_ROOT
    return {STUDIO_LIBRARY_ROOT};
#else
    std::vector<libraryfs::path> roots={"/data/homebrew","/mnt/ext1/games","/mnt/ext1/homebrew","/mnt/ext1/etaHEN/games"};
    for(int n=0;n<8;n++){auto usb=libraryfs::path("/mnt/usb"+std::to_string(n));roots.push_back(usb);}
    return roots;
#endif
}
inline std::string storageName(const std::string& path){if(path.rfind("/data/",0)==0)return "Internal SSD";if(path.rfind("/mnt/ext1/",0)==0)return "M.2 SSD";if(path.rfind("/mnt/usb",0)==0&&path.size()>8&&path[8]>='0'&&path[8]<='7')return "USB "+path.substr(8,1);return "Other storage";}
inline LibraryJson folderGame(const libraryfs::path& input){
    struct stat st{};if(lstat(input.c_str(),&st)!=0||!S_ISDIR(st.st_mode))return LibraryJson();
    bool allowed=false;for(const auto& root:libraryRoots())if(pathBelow(root,input)&&libraryfs::weakly_canonical(root)!=libraryfs::weakly_canonical(input)){allowed=true;break;}
    if(!allowed||libraryfs::exists(input/"ampr_assets.index")||libraryfs::exists(input/"ampr_emu.index"))return LibraryJson();
    auto metadata=input/"sce_sys/param.json";std::error_code ec;auto size=libraryfs::file_size(metadata,ec);if(ec||size>262144||!libraryfs::is_regular_file(input/"eboot.bin"))return LibraryJson();
    if(libraryfs::is_symlink(metadata)||libraryfs::is_symlink(input/"eboot.bin")||libraryfs::is_symlink(input/"sce_sys"))return LibraryJson();
    std::ifstream file(metadata);auto p=LibraryJson::parse(file,nullptr,false);if(p.is_discarded()||!p.is_object()||!p.contains("titleId")||!p["titleId"].is_string())return LibraryJson();
    auto id=p["titleId"].get<std::string>();if(!validTitle(id))return LibraryJson();std::string name=input.filename().string();
    if(p.contains("titleName")&&p["titleName"].is_string())name=p["titleName"].get<std::string>();
    else if(p.contains("localizedParameters")&&p["localizedParameters"].is_object()){auto& l=p["localizedParameters"];auto lang=l.value("defaultLanguage",std::string("en-US"));if(l.contains(lang)&&l[lang].is_object()&&l[lang].contains("titleName")&&l[lang]["titleName"].is_string())name=l[lang]["titleName"].get<std::string>();}
    auto path=libraryfs::canonical(input).string();return LibraryJson{{"title_id",id},{"title_name",name},{"path",path},{"runtime_path",path},{"source_available",true},{"managed",false},{"image_backed",false}};
}
inline std::vector<LibraryJson> scanFolderGames(){
    std::vector<LibraryJson> games;std::set<std::string> seen;size_t visited=0;
    for(const auto& root:libraryRoots()){
        std::error_code ec;if(!libraryfs::is_directory(root,ec))continue;
        libraryfs::recursive_directory_iterator it(root,libraryfs::directory_options::skip_permission_denied,ec),end;
        while(it!=end&&!ec&&visited++<20000){auto path=it->path();bool link=it->is_symlink(ec),dir=it->is_directory(ec);if(link||it.depth()>=2)it.disable_recursion_pending();
            if(dir&&!link){try{auto game=folderGame(path);if(!game.is_null()){it.disable_recursion_pending();if(seen.insert(game["path"].get<std::string>()).second)games.push_back(std::move(game));}else if(libraryfs::exists(path/"sce_sys/param.json")||libraryfs::exists(path/"ampr_assets.index"))it.disable_recursion_pending();}catch(const std::exception&){}}
            it.increment(ec);
        }
    }return games;
}
inline bool folderViewReadOnly(const libraryfs::path& view,const libraryfs::path& source){
#ifndef STUDIO_HOST_TEST
    struct statfs mount{};struct stat a{},b{};return statfs(view.c_str(),&mount)==0&&std::string(mount.f_fstypename)=="nullfs"&&std::string(mount.f_mntonname)==view.string()&&(mount.f_flags&MNT_RDONLY)&&stat(source.c_str(),&a)==0&&stat(view.c_str(),&b)==0&&a.st_ino==b.st_ino;
#else
    return false;
#endif
}
inline libraryfs::path folderView(const libraryfs::path& stateRoot){return stateRoot/"folder-source";}
inline std::string currentFolderSource(const libraryfs::path& stateRoot){
    auto record=stateRoot/"folder-source.json";if(!libraryfs::is_regular_file(record))return {};
    std::ifstream file(record);auto j=LibraryJson::parse(file,nullptr,false);if(j.is_discarded()||!j.is_object()||!j.contains("source")||!j["source"].is_string())return {};return j["source"].get<std::string>();
}
inline void releaseFolderView(const libraryfs::path& stateRoot){
#ifndef STUDIO_HOST_TEST
    auto view=folderView(stateRoot);struct statfs mounted{};if(statfs(view.c_str(),&mounted)!=0||std::string(mounted.f_mntonname)!=view.string())return;
    auto source=currentFolderSource(stateRoot);require(!source.empty()&&folderViewReadOnly(view,source),"Folder source mount changed; refusing to unmount it");
    require(unmount(view.c_str(),0)==0,std::string("Cannot release folder source: ")+strerror(errno));
    libraryfs::remove(stateRoot/"folder-source.json");
#endif
}
inline void mountFolderView(const libraryfs::path& stateRoot,const libraryfs::path& source){
    require(!folderGame(source).is_null(),"Folder dump is missing, compressed, or outside supported game folders");releaseFolderView(stateRoot);
#ifndef STUDIO_HOST_TEST
    auto view=folderView(stateRoot);libraryfs::create_directories(view);require(libraryfs::is_empty(view),"Source mount folder is not empty");
    std::string type="nullfs",target=view.string(),original=libraryfs::canonical(source).string();
    iovec pairs[]={{const_cast<char*>("fstype"),7},{type.data(),type.size()+1},{const_cast<char*>("fspath"),7},{target.data(),target.size()+1},{const_cast<char*>("target"),7},{original.data(),original.size()+1}};
    require(nmount(pairs,6,MNT_RDONLY)==0,std::string("Cannot create read-only folder view: ")+strerror(errno));
    try{require(folderViewReadOnly(view,source),"The folder source could not be verified read-only");std::ofstream record(stateRoot/"folder-source.json");record<<LibraryJson{{"source",original}}.dump();record.flush();require(bool(record),"Cannot save folder source mount record");}catch(...){unmount(view.c_str(),0);throw;}
#else
    throw std::runtime_error("Read-only folder mounts require the PS5 kernel");
#endif
}
}
