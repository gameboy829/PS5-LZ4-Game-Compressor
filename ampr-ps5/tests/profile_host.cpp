// SPDX-License-Identifier: GPL-3.0-or-later
#define AMPR_PORTABLE_BACKEND
#define AMPR_NO_MAIN
#include "../../ampr-native/src/main.cpp"
#include "../../ampr-native/src/index_builder.h"
#include "../../ampr-native/src/universal_profile.h"
int main(int argc,char**argv){try{if(argc<3)return 2;std::string command=argv[1];if(command=="profile"&&argc==4)generateUniversalProfile(argv[2],argv[3],7,false);else if(command=="index"&&argc==4)makeIndex(argv[2],argv[3]);else if(command=="pack"&&argc==6)packProfile(argv[2],argv[3],argv[4],argv[5],{});else if(command=="verify-required"&&argc==4){productionPack=true;verify(fs::path(argv[2])/"ampr_assets.index",argv[3]);}else if(command=="verify"&&argc==4)verify(fs::path(argv[2])/"ampr_assets.index",argv[3]);else return 2;return 0;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
