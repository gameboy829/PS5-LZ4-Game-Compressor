// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "http.h"
#include "../../ampr-native/third_party/picosha2/picosha2.h"
#include <filesystem>
#include <functional>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
namespace studio {
inline void requireDeleteConsent(const std::string& action,bool fullyVerified,bool confirmed,bool unverifiedConfirmed){if(action=="delete"){require(confirmed,"Confirm permanent original deletion before starting");if(!fullyVerified)require(unverifiedConfirmed,"Confirm deletion without full verification before starting");}}
struct OriginalIdentity {dev_t device;ino_t inode;off_t size;time_t mtime;};
inline OriginalIdentity identifyOriginal(const std::filesystem::path& path){struct stat s{};require(lstat(path.c_str(),&s)==0&&S_ISREG(s.st_mode),"Original must be a regular image file, not a link");return {s.st_dev,s.st_ino,s.st_size,s.st_mtime};}
inline void unchangedOriginal(const std::filesystem::path& path,const OriginalIdentity& expected){auto now=identifyOriginal(path);require(now.device==expected.device&&now.inode==expected.inode&&now.size==expected.size&&now.mtime==expected.mtime,"Original file changed; leaving it untouched");}
struct FileHandle {int fd;explicit FileHandle(int n):fd(n){}~FileHandle(){if(fd>=0)close(fd);}FileHandle(const FileHandle&)=delete;};
inline void closeFile(FileHandle& file){int fd=file.fd;file.fd=-1;require(close(fd)==0,"Cannot close backup file");}
inline void publishImage(const std::filesystem::path& source,const std::filesystem::path& target){
    FileHandle reservation(open(target.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644));require(reservation.fd>=0,"Backup destination already exists or is unavailable");struct stat owned{};require(fstat(reservation.fd,&owned)==0,"Cannot identify backup reservation");
    try{struct stat current{};require(lstat(target.c_str(),&current)==0&&current.st_dev==owned.st_dev&&current.st_ino==owned.st_ino&&current.st_size==0,"Backup reservation changed");closeFile(reservation);require(rename(source.c_str(),target.c_str())==0,"Cannot move image into backup folder");}catch(...){struct stat current{};if(lstat(target.c_str(),&current)==0&&current.st_dev==owned.st_dev&&current.st_ino==owned.st_ino)unlink(target.c_str());throw;}
}
inline void handleOriginal(const std::filesystem::path& source,const OriginalIdentity& identity,const std::string& action,const std::filesystem::path& backup,const std::function<void()>& poll,const std::function<void(const char*)>& phase,const std::function<void(uint64_t,uint64_t)>& progress){
    namespace filesystem=std::filesystem;
    poll();unchangedOriginal(source,identity);
    if(action=="delete"){phase("Deleting original image");poll();require(unlink(source.c_str())==0,"Cannot delete original image; it was retained");return;}
    require(action=="move","Invalid original-file action");filesystem::create_directories(backup);
    auto destination=backup/source.filename();require(!filesystem::exists(destination),"Backup image already exists; original retained");struct stat parent{};require(stat(backup.c_str(),&parent)==0,"Cannot inspect backup drive");
    if(parent.st_dev==identity.device){phase("Moving original image to backup");poll();unchangedOriginal(source,identity);publishImage(source,destination);return;}
    require(identity.size>=0,"Invalid original image size");
    auto temporary=backup/(source.filename().string()+".lz4-backup-partial");
    FileHandle input(open(source.c_str(),O_RDONLY|O_NOFOLLOW));require(input.fd>=0,"Cannot read original image");
    struct stat opened{};require(fstat(input.fd,&opened)==0&&opened.st_dev==identity.device&&opened.st_ino==identity.inode&&opened.st_size==identity.size&&opened.st_mtime==identity.mtime,"Original image changed before backup");
    FileHandle output(open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644));require(output.fd>=0,"Cannot reserve backup file");bool completed=false;
    try{phase("Backing up original image");std::vector<unsigned char> buffer(4*1024*1024);uint64_t done=0,total=uint64_t(identity.size);picosha2::hash256_one_by_one expected;
        while(done<total){poll();auto amount=std::min<uint64_t>(buffer.size(),total-done);ssize_t n=read(input.fd,buffer.data(),size_t(amount));if(n<0&&errno==EINTR)continue;require(n>0,"Original backup read failed");expected.process(buffer.begin(),buffer.begin()+n);size_t at=0;while(at<size_t(n)){poll();auto w=write(output.fd,buffer.data()+at,size_t(n)-at);if(w<0&&errno==EINTR)continue;require(w>0,"Original backup write failed");at+=size_t(w);}done+=uint64_t(n);progress(done,total);}
        require(fsync(output.fd)==0,"Cannot flush original backup");unchangedOriginal(source,identity);expected.finish();std::vector<unsigned char> a(32),b(32);expected.get_hash_bytes(a.begin(),a.end());
        phase("Verifying original backup");FileHandle check(open(temporary.c_str(),O_RDONLY|O_NOFOLLOW));require(check.fd>=0,"Cannot reopen original backup");picosha2::hash256_one_by_one actual;done=0;while(done<total){poll();auto n=read(check.fd,buffer.data(),size_t(std::min<uint64_t>(buffer.size(),total-done)));if(n<0&&errno==EINTR)continue;require(n>0,"Original backup verification read failed");actual.process(buffer.begin(),buffer.begin()+n);done+=uint64_t(n);progress(done,total);}actual.finish();actual.get_hash_bytes(b.begin(),b.end());require(a==b,"Original backup hash mismatch; original retained");poll();unchangedOriginal(source,identity);
        closeFile(check);closeFile(output);closeFile(input);publishImage(temporary,destination);completed=true;
        FileHandle directory(open(backup.c_str(),O_RDONLY|O_DIRECTORY));require(directory.fd>=0&&fsync(directory.fd)==0,"Backup ready, but cannot flush backup folder; original retained");
        poll();unchangedOriginal(source,identity);require(unlink(source.c_str())==0,"Verified backup ready, but original could not be removed");
    }catch(...){if(!completed){int fd=output.fd;output.fd=-1;if(fd>=0)close(fd);require(unlink(temporary.c_str())==0||errno==ENOENT,"Original retained; cannot remove partial backup: "+temporary.string());}throw;}
}
struct DirectoryIdentity {dev_t device;ino_t inode;};
inline DirectoryIdentity identifyDirectory(const std::filesystem::path& path){struct stat s{};require(lstat(path.c_str(),&s)==0&&S_ISDIR(s.st_mode),"Swap folder is missing or is a link");return {s.st_dev,s.st_ino};}
inline void finishSwap(const std::filesystem::path& working,const DirectoryIdentity& expected,const std::filesystem::path& returned,const DirectoryIdentity& returnedIdentity,const std::filesystem::path& original,const std::filesystem::path& backup,const OriginalIdentity& originalIdentity,const std::function<void()>& poll){
    poll();auto a=identifyDirectory(working),b=identifyDirectory(returned);
    require(a.device==expected.device&&a.inode==expected.inode&&b.device==returnedIdentity.device&&b.inode==returnedIdentity.inode&&a.device!=b.device,"Swap folder changed; completed copies retained");
    require(!std::filesystem::exists(original),"Original has not moved; converted copies retained");auto saved=identifyOriginal(backup/original.filename());require(saved.size==originalIdentity.size,"Original backup changed; converted copies retained");
    std::filesystem::remove_all(working);
}

}
