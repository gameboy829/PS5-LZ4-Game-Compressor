// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <map>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cerrno>
namespace studio {
struct Socket {
    int fd=-1;
    explicit Socket(int n=-1):fd(n){}
    ~Socket(){if(fd>=0)::close(fd);}
    Socket(const Socket&)=delete;
    Socket& operator=(const Socket&)=delete;
};
inline void require(bool yes,const std::string& message){if(!yes)throw std::runtime_error(message);}
inline std::string lower(std::string s){for(auto&c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
inline void timeout(int fd,int seconds){timeval t{seconds,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof(t));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof(t));}
inline void sendAll(int fd,const std::string& s){size_t offset=0;while(offset<s.size()){auto n=send(fd,s.data()+offset,s.size()-offset,0);if(n<0&&errno==EINTR)continue;require(n>0,"Socket write failed");offset+=size_t(n);}}
struct Message {std::string first,body;std::map<std::string,std::string> headers;};
inline size_t decimal(const std::string&s,size_t limit){require(!s.empty(),"Missing length");size_t value=0;for(char c:s){require(c>='0'&&c<='9',"Invalid length");size_t digit=size_t(c-'0');require(value<limit/10||(value==limit/10&&digit<=limit%10),"Value exceeds supported limit");value=value*10+digit;}return value;}
inline Message receive(int fd,size_t limit=4*1024*1024){
    std::string input;char buffer[8192];size_t end=std::string::npos;
    while((end=input.find("\r\n\r\n"))==std::string::npos){auto n=recv(fd,buffer,sizeof(buffer),0);if(n<0&&errno==EINTR)continue;require(n>0,"Incomplete HTTP headers");input.append(buffer,size_t(n));require(input.size()<=limit+32768,"Message too large");if(end==std::string::npos)require(input.find("\r\n\r\n")!=std::string::npos||input.size()<=32768,"Headers too large");}
    require(end<=32768,"Headers too large");Message m;auto line=input.find("\r\n");require(line<end,"Malformed HTTP start line");m.first=input.substr(0,line);size_t at=line+2;
    while(at<end){auto stop=input.find("\r\n",at),colon=input.find(':',at);require(colon<stop,"Malformed HTTP header");auto key=lower(input.substr(at,colon-at));auto value=input.substr(colon+1,stop-colon-1);while(!value.empty()&&value.front()==' ')value.erase(0,1);require(!m.headers.count(key),"Duplicate HTTP header");m.headers.emplace(key,value);at=stop+2;}
    require(!m.headers.count("transfer-encoding"),"Chunked messages are unsupported");size_t expected=m.headers.count("content-length")?decimal(m.headers.at("content-length"),limit):0;
    m.body=input.substr(end+4);require(m.body.size()<=expected,"Unexpected HTTP data");while(m.body.size()<expected){auto n=recv(fd,buffer,std::min(sizeof(buffer),expected-m.body.size()),0);if(n<0&&errno==EINTR)continue;require(n>0,"Incomplete HTTP body");m.body.append(buffer,size_t(n));}return m;
}
inline std::string decode(const std::string&s){std::string out;auto hex=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};for(size_t i=0;i<s.size();i++){char c=s[i];if(c=='%'){require(i+2<s.size(),"Invalid percent escape");int a=hex(s[++i]),b=hex(s[++i]);require(a>=0&&b>=0,"Invalid percent escape");c=char(a*16+b);}else if(c=='+')c=' ';require(c!=0&&c!='\r'&&c!='\n',"Invalid URL character");out+=c;}return out;}
struct Request {std::string method,path;std::map<std::string,std::string> query;};
inline Request parse(const Message&m){Request r;auto first=m.first.find(' '),last=m.first.rfind(' ');require(first!=std::string::npos&&first<last,"Malformed request");r.method=m.first.substr(0,first);require(m.first.substr(last+1)=="HTTP/1.1"||m.first.substr(last+1)=="HTTP/1.0","Unsupported HTTP version");auto target=m.first.substr(first+1,last-first-1);auto question=target.find('?');r.path=decode(target.substr(0,question));require(!r.path.empty()&&r.path.front()=='/',"Invalid request target");if(question!=std::string::npos){size_t at=question+1;while(at<target.size()){auto stop=target.find('&',at);if(stop==std::string::npos)stop=target.size();auto part=target.substr(at,stop-at);auto equal=part.find('=');auto key=decode(part.substr(0,equal));require(!r.query.count(key),"Duplicate query field");r.query.emplace(key,equal==std::string::npos?"":decode(part.substr(equal+1)));at=stop+1;}}return r;}
inline void respond(int fd,int code,const std::string& body,const std::string& mime="application/json"){auto reason=code==200?"OK":code==404?"Not Found":code==409?"Conflict":"Bad Request";sendAll(fd,"HTTP/1.1 "+std::to_string(code)+" "+reason+"\r\nContent-Type: "+mime+"\r\nContent-Length: "+std::to_string(body.size())+"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n"+body);}
inline Message postLoopback(int port,const std::string& path,const std::string& body){Socket s(socket(AF_INET,SOCK_STREAM,0));require(s.fd>=0,"Cannot create ShadowMount socket");timeout(s.fd,15);sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(uint16_t(port));address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);require(connect(s.fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"ShadowMount is unavailable");sendAll(s.fd,"POST "+path+" HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body);return receive(s.fd);}
inline Message getLoopback(int port,const std::string& path){Socket s(socket(AF_INET,SOCK_STREAM,0));require(s.fd>=0,"Cannot create local service socket");timeout(s.fd,15);sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(uint16_t(port));address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);require(connect(s.fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"Payload Manager is unavailable");sendAll(s.fd,"GET "+path+" HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");return receive(s.fd);}

}
