#include "ampr_backend.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
static bool cancelPacking=false;
static void phase(void*,const char* name){if(!strcmp(name,"Packing game files"))cancelPacking=true;}
static int cancel(void*){return cancelPacking;}
static void log(void*,const char*s){fputs(s,stdout);}
static void progress(void*,uint64_t d,uint64_t t){if(d==t)printf("PROGRESS %llu/%llu\n",(unsigned long long)d,(unsigned long long)t);}
int main(int argc,char**argv){char error[2048]={0};ampr_backend_callbacks cb{};cb.log=log;cb.progress=progress;int result=-1;
    if(argc==6&&(!strcmp(argv[1],"game")||!strcmp(argv[1],"game-skip")))result=ampr_backend_pack_game_options(argv[2],argv[3],7,argv[4],argv[5],strcmp(argv[1],"game-skip")!=0,&cb,error,sizeof(error));
    else if(argc==6&&(!strcmp(argv[1],"cancel-cleanup")||!strcmp(argv[1],"cancel-refuse"))){
        cb.phase=phase;cb.cancelled=cancel;
        result=ampr_backend_pack_game(argv[2],argv[3],7,argv[4],argv[5],&cb,error,sizeof(error));
        if(!result||!std::filesystem::exists(std::filesystem::path(argv[3])/"INCOMPLETE.txt"))return 2;
        if(!strcmp(argv[1],"cancel-refuse")){
            auto marker=std::filesystem::path(argv[3])/".ampr-owned";std::ifstream input(marker);std::string token((std::istreambuf_iterator<char>(input)),{});input.close();
            {std::ofstream changed(marker);changed<<"changed ownership";}
            if(ampr_backend_delete_partial(error,sizeof(error))==0||!std::filesystem::exists(argv[3]))return 3;
            {std::ofstream restored(marker);restored<<token;}
        }
        result=ampr_backend_delete_partial(error,sizeof(error));
        if(!result&&std::filesystem::exists(argv[3]))return 4;
    }
    else if(argc==4&&!strcmp(argv[1],"copy"))result=ampr_backend_copy_verified(argv[2],argv[3],&cb,error,sizeof(error));
    else if(argc==4&&!strcmp(argv[1],"copy-skip"))result=ampr_backend_copy_options(argv[2],argv[3],0,&cb,error,sizeof(error));
    else if(argc==3&&!strcmp(argv[1],"measure")){uint64_t logical=0,required=0;result=ampr_backend_measure(argv[2],&logical,&required,error,sizeof(error));if(!result)printf("%llu %llu\n",(unsigned long long)logical,(unsigned long long)required);}
    if(result)fprintf(stderr,"%s\n",error);return result?1:0;
}
