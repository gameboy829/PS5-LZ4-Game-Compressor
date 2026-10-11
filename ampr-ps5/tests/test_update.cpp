// SPDX-License-Identifier: GPL-3.0-or-later
#include "../standalone/update_common.h"
#include <cassert>
#include <iostream>
using namespace updates;
int main(int argc,char**argv){
 Json a={{"name",assetName},{"state","uploaded"},{"size",100},{"digest","sha256:"+std::string(64,'a')},{"browser_download_url","https://github.com/gameboy829/PS5-LZ4-Game-Compressor/releases/download/beta.3/LZ4-Game-Compressor.elf"}};
 Json release={{"draft",false},{"tag_name","beta.3"},{"assets",Json::array({a})},{"body","Test"}};
 auto selected=select(Json::array({release}),"0.1.0-beta.2");assert(selected["version"]=="beta.3");
 assert(select(Json::array({release}),"0.1.0-beta.3").is_null());
 assert(version("beta.10")>version("beta.9"));assert(version("v0.1.0")>version("beta.999"));
 release["draft"]=true;assert(select(Json::array({release}),"beta.2").is_null());release["draft"]=false;
 release["assets"][0]["browser_download_url"]="https://attacker.invalid/update.elf";assert(select(Json::array({release}),"beta.2").is_null());release["assets"][0]=a;
 release["assets"][0]["digest"]="";assert(select(Json::array({release}),"beta.2").is_null());release["assets"][0]=a;
 release["assets"][0]["name"]="LZ4-Game-Compressor-beta.3.elf";assert(select(Json::array({release}),"beta.2").is_null());
 auto data=read(argv[1]);Json expected={{"size",data.size()},{"sha256",picosha2::hash256_hex_string(data)}};verify(data,expected);
 auto rejects=[&](const std::string& d,const Json& e){bool failed=false;try{verify(d,e);}catch(...){failed=true;}assert(failed);};
 rejects(data.substr(0,data.size()-1),expected);auto damaged=data;damaged[100]^=1;rejects(damaged,expected);
 damaged=data;damaged[18]=0;expected["sha256"]=picosha2::hash256_hex_string(damaged);rejects(damaged,expected);
 damaged=data;std::fill(damaged.begin()+24,damaged.begin()+32,0);expected["sha256"]=picosha2::hash256_hex_string(damaged);rejects(damaged,expected);
 std::cout<<"PASS: release ordering, beta support, draft/foreign URL/digest/name rejection, real ELF validation, corrupt/truncated/wrong-architecture/entry rejection\n";
}
