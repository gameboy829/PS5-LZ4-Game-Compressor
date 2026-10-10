#include "ampr_backend.h"
#include <cstdio>
#include <string>
static void log(void*,const char*text){fputs(text,stdout);}
struct State {bool cancelVerify=false,verifying=false;};
static void phase(void *p,const char *name){auto s=(State*)p;s->verifying=std::string(name)=="verifying";}
static int cancelled(void *p){auto s=(State*)p;return s->cancelVerify&&s->verifying;}
int main(int argc,char**argv){if(argc!=4&&argc!=5)return 2;char error[2048];State state;state.cancelVerify=argc==5;ampr_backend_callbacks cb{};cb.context=&state;cb.log=log;cb.phase=phase;cb.cancelled=cancelled;int result=ampr_backend_pack(argv[1],argv[2],argv[3],9,&cb,error,sizeof error);if(result)fprintf(stderr,"%s\n",error);return result?1:0;}
