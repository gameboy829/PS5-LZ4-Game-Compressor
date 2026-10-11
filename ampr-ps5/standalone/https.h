// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "http.h"
#include <functional>
#include <cstdint>
#include <vector>
#ifndef STUDIO_HOST_TEST
#include "update-ca.h"
extern "C" {
int sceNetInit();int sceNetPoolCreate(const char*,int,int);int sceNetPoolDestroy(int);
int sceSslInit(size_t);int sceSslTerm(int);
int sceHttpInit(int,int,size_t);int sceHttpTerm(int);
int sceHttpCreateTemplate(int,const char*,int,int);int sceHttpDeleteTemplate(int);
int sceHttpCreateConnectionWithURL(int,const char*,int);int sceHttpDeleteConnection(int);
int sceHttpCreateRequestWithURL(int,int,const char*,uint64_t);int sceHttpDeleteRequest(int);
int sceHttpSetAutoRedirect(int,int);int sceHttpSetConnectTimeOut(int,uint32_t);int sceHttpSetRecvTimeOut(int,uint32_t);int sceHttpSetSendTimeOut(int,uint32_t);
int sceHttpsLoadCert(int,int,const UpdateCa**,const UpdateCa*,const UpdateCa*);
int sceHttpSendRequest(int,const void*,size_t);int sceHttpGetStatusCode(int,int*);int sceHttpReadData(int,void*,size_t);
}
#endif
namespace updates {
inline std::string download(const std::string& url,size_t limit,const std::function<void(size_t)>& progress={}){
 studio::require(url.rfind("https://",0)==0,"Updates require HTTPS");
#ifdef STUDIO_HOST_TEST
 throw std::runtime_error("Console HTTPS is unavailable in host tests");
#else
 struct Context {int pool=-1,ssl=-1,http=-1,tmpl=-1,conn=-1,req=-1;~Context(){if(req>=0)sceHttpDeleteRequest(req);if(conn>=0)sceHttpDeleteConnection(conn);if(tmpl>=0)sceHttpDeleteTemplate(tmpl);if(http>=0)sceHttpTerm(http);if(ssl>=0)sceSslTerm(ssl);if(pool>=0)sceNetPoolDestroy(pool);}} c;
 sceNetInit();c.pool=sceNetPoolCreate("LZ4Updater",256*1024,0);studio::require(c.pool>=0,"Cannot initialize update networking");
 c.ssl=sceSslInit(4*1024*1024);studio::require(c.ssl>=0,"Cannot initialize HTTPS");
 c.http=sceHttpInit(c.pool,c.ssl,1024*1024);studio::require(c.http>=0,"Cannot initialize HTTP client");
 std::vector<const UpdateCa*> roots;for(const auto& cert:updateCas)roots.push_back(&cert);
 int certResult=sceHttpsLoadCert(c.http,int(roots.size()),roots.data(),nullptr,nullptr);char certError[96];snprintf(certError,sizeof(certError),"Cannot load update certificate authorities (0x%08x)",unsigned(certResult));studio::require(certResult==0,certError);
 c.tmpl=sceHttpCreateTemplate(c.http,"LZ4-Game-Compressor-Updater",2,1);studio::require(c.tmpl>=0,"Cannot create HTTPS template");
 studio::require(sceHttpSetAutoRedirect(c.tmpl,1)==0,"Cannot enable GitHub redirects");
 studio::require(sceHttpSetConnectTimeOut(c.tmpl,15000000)==0&&sceHttpSetRecvTimeOut(c.tmpl,30000000)==0&&sceHttpSetSendTimeOut(c.tmpl,30000000)==0,"Cannot set update timeouts");
 c.conn=sceHttpCreateConnectionWithURL(c.tmpl,url.c_str(),0);studio::require(c.conn>=0,"Cannot create HTTPS connection");
 c.req=sceHttpCreateRequestWithURL(c.conn,0,url.c_str(),0);studio::require(c.req>=0,"Cannot create update request");
 int sent=sceHttpSendRequest(c.req,nullptr,0);char error[96];snprintf(error,sizeof(error),"GitHub request failed (0x%08x); check internet access",unsigned(sent));studio::require(sent==0,error);
 int code=0;studio::require(sceHttpGetStatusCode(c.req,&code)==0&&code==200,"GitHub returned HTTP "+std::to_string(code));
 std::string data;char buffer[32768];for(;;){int n=sceHttpReadData(c.req,buffer,sizeof(buffer));studio::require(n>=0,"Update download interrupted");if(!n)break;studio::require(data.size()+size_t(n)<=limit,"Update response exceeds limit");data.append(buffer,size_t(n));if(progress)progress(data.size());}return data;
#endif
}
}
