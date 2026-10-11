// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "http.h"
#include "../../ampr-native/third_party/config/json.hpp"
#include "../../ampr-native/third_party/picosha2/picosha2.h"
#include <filesystem>
#include <fstream>
#include <regex>
#include <array>
#include <elf.h>
#include <cstring>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <sstream>
namespace updates {
using Json=nlohmann::json;
namespace fs=std::filesystem;
constexpr const char* assetName="LZ4-Game-Compressor.elf";
#if defined(STUDIO_HOST_TEST) && defined(TEST_UPDATE_ROOT)
constexpr const char* payload=TEST_UPDATE_PAYLOAD;
constexpr const char* folder=TEST_UPDATE_ROOT;
#else
constexpr const char* payload="/data/pldmgr/payloads/LZ4-Game-Compressor/LZ4-Game-Compressor.elf";
constexpr const char* folder="/data/AMPRStudio/update";
#endif
inline std::array<int,4> version(std::string text){
 std::smatch m;
 if(std::regex_match(text,m,std::regex("beta\\.([0-9]+)")))return {0,1,0,std::stoi(m[1])};
 studio::require(std::regex_match(text,m,std::regex("v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(-beta\\.([0-9]+))?")),"Unsupported release version");
 return {std::stoi(m[1]),std::stoi(m[2]),std::stoi(m[3]),m[5].matched?std::stoi(m[5]):INT_MAX};
}
inline Json select(const Json& releases,const std::string& installed){
 studio::require(releases.is_array(),"Invalid GitHub release response");Json best=nullptr;auto latest=version(installed);
 for(const auto& r:releases){try{if(r.value("draft",false))continue;auto v=version(r.at("tag_name").get<std::string>());if(v<=latest)continue;
 for(const auto& a:r.at("assets")){if(a.value("name",std::string())!=assetName||a.value("state",std::string())!="uploaded")continue;
 auto size=a.at("size").get<uint64_t>();auto digest=a.at("digest").get<std::string>();auto url=a.at("browser_download_url").get<std::string>();
 studio::require(size>=64&&size<=64*1024*1024,"Update size is unsupported");studio::require(std::regex_match(digest,std::regex("sha256:[a-f0-9]{64}")),"Release has no SHA-256 digest");
 studio::require(url.rfind("https://github.com/gameboy829/PS5-LZ4-Game-Compressor/releases/download/",0)==0,"Update URL is not from the official repository");
 best=Json{{"version",r.at("tag_name")},{"url",url},{"sha256",digest.substr(7)},{"size",size},{"notes",r.value("body",std::string())}};latest=v;
 }}catch(const std::exception&){} }return best;
}
inline void atomicWrite(const fs::path& path,const std::string& data,int mode=0644){
 fs::create_directories(path.parent_path());auto temp=path.string()+".tmp";
 int fd=open(temp.c_str(),O_WRONLY|O_CREAT|O_TRUNC,mode);studio::require(fd>=0,"Cannot create update file");
 try{size_t at=0;while(at<data.size()){ssize_t n=write(fd,data.data()+at,data.size()-at);studio::require(n>0,"Cannot write update file");at+=size_t(n);}studio::require(fsync(fd)==0,"Cannot flush update file");close(fd);fd=-1;studio::require(chmod(temp.c_str(),mode)==0,"Cannot set update permissions");fs::rename(temp,path);}catch(...){if(fd>=0)close(fd);fs::remove(temp);throw;}
}
inline std::string read(const fs::path& path){std::ifstream f(path,std::ios::binary);studio::require(bool(f),"Cannot read update file");return std::string(std::istreambuf_iterator<char>(f),{});}
inline void verify(const std::string& data,const Json& release){
 studio::require(data.size()==release.at("size").get<size_t>(),"Update download is incomplete");
 studio::require(data.size()>=64&&data.compare(0,6,std::string("\x7f" "ELF\x02\x01",6))==0&&static_cast<unsigned char>(data[18])==62&&data[19]==0,"Update is not a PS5 ELF");
 Elf64_Ehdr header{};std::memcpy(&header,data.data(),sizeof(header));
 studio::require(header.e_entry&&header.e_ehsize==sizeof(header)&&header.e_phentsize==sizeof(Elf64_Phdr)&&header.e_phnum>0&&header.e_phoff<=data.size()&&size_t(header.e_phnum)*sizeof(Elf64_Phdr)<=data.size()-header.e_phoff,"Update ELF has an invalid program table");
 for(size_t i=0;i<header.e_phnum;i++){Elf64_Phdr segment{};std::memcpy(&segment,data.data()+header.e_phoff+i*sizeof(segment),sizeof(segment));studio::require(segment.p_offset<=data.size()&&segment.p_filesz<=data.size()-segment.p_offset&&segment.p_filesz<=segment.p_memsz,"Update ELF contains an invalid segment");}
 studio::require(picosha2::hash256_hex_string(data)==release.at("sha256").get<std::string>(),"Update checksum failed; current app retained");
}
}
