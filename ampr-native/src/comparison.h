// SPDX-License-Identifier: GPL-3.0-or-later
// Both outputs are independently decoded and checked against the source before comparison.
Bytes fileDigest(const fs::path& path) {
    Sha hash;std::ifstream in(path,std::ios::binary);check(bool(in),"cannot open comparison file: "+path.u8string());
    char block[65536];while(in){check(!cancelled,"cancelled");in.read(block,sizeof block);auto n=in.gcount();if(n)hash.add(block,size_t(n));}
    check(in.eof(),"comparison read failed: "+path.u8string());return hash.finish();
}
struct ComparisonInfo {AmprPackIndexHeader header;std::vector<AmprPackFileRecord> files;std::vector<std::string> paths;std::set<std::string> names;};
const std::string runtimeAsset="/app0/fakelib/libSceAmpr.sprx";
std::string hexDigest(const Bytes& bytes){const char* digits="0123456789abcdef";std::string s;for(auto c:bytes){s+=digits[c>>4];s+=digits[c&15];}return s;}
bool approvedRuntimeDifference(const fs::path& reference,const fs::path& source){
    auto r=reference/"fakelib/libSceAmpr.sprx",s=source/"fakelib/libSceAmpr.sprx";
    if(!fs::is_regular_file(r)||!fs::is_regular_file(s))return false;
    auto rh=fileDigest(r),sh=fileDigest(s);if(rh==sh)return false;
    check(hexDigest(rh)=="69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a","Python runtime differs from source AND is not the pinned intended runtime. SHA-256="+hexDigest(rh));
    std::cout<<"RUNTIME REPLACEMENT VERIFIED: "<<r.u8string()<<"\nPinned Python runtime SHA-256: "<<hexDigest(rh)<<"\nOriginal source runtime SHA-256: "<<hexDigest(sh)<<"\nPinned runtime replacement is an intentional deployment difference.\n";return true;
}
std::set<std::string> inventoryPaths(const fs::path& root) {
    auto bytes=read(root/"ampr_assets.index");auto h=at<AmprPackIndexHeader>(bytes,0);
    auto checksum=h.headerCrc32;h.headerCrc32=0;
    check(!memcmp(h.magic,"AMPRPAK4",8)&&h.version==4&&h.headerSize==128&&h.endianMarker==0x01020304&&crc(&h,sizeof h)==checksum,"invalid inventory manifest header: "+root.u8string());
    check(h.fileRecordSize==48&&h.chunkRecordSize==12&&h.packRecordSize==32&&h.fileCount<=(bytes.size()-128)/48&&h.chunkCount<=(bytes.size()-128)/12&&h.packCount<=(bytes.size()-128)/32,"invalid inventory manifest counts");
    check(h.filesOffset==128&&h.chunksOffset==128+h.fileCount*48&&h.packsOffset==h.chunksOffset+h.chunkCount*12&&h.stringsOffset==h.packsOffset+h.packCount*32&&h.stringsOffset<=bytes.size()&&h.stringsSize==bytes.size()-h.stringsOffset,"invalid inventory manifest bounds");
    check(crc(bytes.data()+128,bytes.size()-128)==h.payloadCrc32,"inventory manifest payload CRC mismatch");
    Bytes strings(bytes.begin()+h.stringsOffset,bytes.end());std::set<std::string> paths;
    for(uint64_t i=0;i<h.fileCount;i++){auto record=at<AmprPackFileRecord>(bytes,h.filesOffset+i*48);auto path=stringat(strings,record.pathOffset,record.pathLength);check(path.rfind("/app0/",0)==0,"invalid inventory asset path");safe(root,path.substr(6));check(paths.insert(path.substr(6)).second,"duplicate inventory asset path: "+path);}
    return paths;
}
struct InventoryCoverage {std::set<std::string> nativeLoose,referenceLoose;};
InventoryCoverage auditSourceInventory(const fs::path& source,const fs::path& native,const fs::path& reference) {
    phase("Checking every original file");auto nativePaths=inventoryPaths(native),referencePaths=inventoryPaths(reference);
    InventoryCoverage covered;uint64_t count=0;std::vector<std::string> failures;
    for(auto&entry:fs::recursive_directory_iterator(source)) {
        ensureActive();check(!entry.is_symlink(),"source inventory cannot follow a symbolic link: "+entry.path().u8string());if(!entry.is_regular_file())continue;
        auto relative=entry.path().lexically_relative(source).generic_u8string();count++;
        for(auto target: {std::make_pair(native,std::make_pair(&nativePaths,&covered.nativeLoose)),std::make_pair(reference,std::make_pair(&referencePaths,&covered.referenceLoose))}) {
            auto destination=safe(target.first,relative);auto label=target.first==native?"Native / PS5":"Python reference";
            if(relative=="ampr_emu.index") {if(!fs::is_regular_file(destination))failures.push_back(std::string(label)+": missing source-index metadata");continue;}
            if(target.second.first->count(relative))continue;
            if(!fs::is_regular_file(destination)) {failures.push_back(std::string(label)+": missing original file "+relative+" (not indexed or copied loose)");continue;}
            bool equal=fs::file_size(entry.path())==fs::file_size(destination)&&fileDigest(entry.path())==fileDigest(destination);
            if(!equal&&relative=="fakelib/libSceAmpr.sprx")equal=approvedRuntimeDifference(target.first,source);
            if(!equal){failures.push_back(std::string(label)+": unindexed loose file differs from original: "+relative);continue;}
            target.second.second->insert(relative);std::cout<<label<<": original file outside manifest verified loose: "<<relative<<"\n";
        }
    }
    for(auto&failure:failures)std::cout<<"SOURCE INVENTORY FAIL: "<<failure<<"\n";
    check(failures.empty(),"Full source inventory failed: "+std::to_string(failures.size())+" missing or different original-file copies. See the file names above.");
    std::cout<<"SOURCE INVENTORY PASS: "<<count<<" original files accounted for in both outputs. ampr_emu.index is regenerated metadata checked by the source-index/manifest verification, not byte identity.\n";
    return covered;
}
ComparisonInfo comparisonInfo(const fs::path& root,const std::set<std::string>& additionalLoose) {
    auto data=read(root/"ampr_assets.index");auto h=at<AmprPackIndexHeader>(data,0);Bytes strings(data.begin()+h.stringsOffset,data.end());
    ComparisonInfo info{h};info.names=additionalLoose;info.names.insert({"ampr_assets.index","ampr_assets.index.crc","ampr_emu.index"});
    for(uint64_t i=0;i<h.fileCount;i++) {auto f=at<AmprPackFileRecord>(data,h.filesOffset+i*48);auto p=stringat(strings,f.pathOffset,f.pathLength);info.files.push_back(f);info.paths.push_back(p);if(!(f.flags&1))info.names.insert(p.substr(6));}
    for(uint32_t i=0;i<h.packCount;i++) {auto p=at<AmprPackDataRecord>(data,h.packsOffset+i*32);info.names.insert(stringat(strings,p.nameOffset,p.nameLength));}
    if(fs::is_regular_file(root/"fakelib/libSceAmpr.sprx")){verifyInstalledRuntime(root);info.names.insert("fakelib/libSceAmpr.sprx");}
    auto runtime=root/"ampr_assets.index.runtime";
    if(fs::exists(runtime)){auto bytes=read(runtime);check(bytes.size()==64,"runtime sidecar must be 64 bytes");auto profile=at<AmprPackRuntimeProfile>(bytes,0);auto checksum=profile.crc32;profile.crc32=0;
        check(!memcmp(profile.magic,"AMPRCFG1",8)&&profile.version==1&&profile.size==64&&!profile.reserved&&!memcmp(profile.buildId,h.buildId,16)&&crc(&profile,64)==checksum&&profile.workers>0&&profile.workers<=16&&profile.latencyReserveWorkers<profile.workers&&profile.decodedCacheBytes%16384==0&&profile.physicalCacheBytes%16384==0,"invalid runtime sidecar/header/CRC/settings");
        info.names.insert("ampr_assets.index.runtime");std::cout<<"Runtime configuration sidecar validated: "<<runtime.u8string()<<"\n";}
    std::set<std::string> auxiliary;if(fs::is_regular_file(root/"UNVERIFIED.txt")&&!info.names.count("UNVERIFIED.txt")){auxiliary.insert("UNVERIFIED.txt");std::cout<<"Unverified-output notice found; comparison will independently check all game data.\n";}for(auto&entry:fs::directory_iterator(root)){auto name=entry.path().filename().u8string();const std::string suffix="_auto_profile.toml";if(entry.is_regular_file()&&name.size()>suffix.size()&&name.substr(name.size()-suffix.size())==suffix){if(!info.names.count(name))auxiliary.insert(name);std::cout<<"Auxiliary generated profile reported (not a game asset): "<<name<<" SHA-256="<<hexDigest(fileDigest(entry.path()))<<"\n";}}
    std::set<std::string> actual;for(auto& entry:fs::recursive_directory_iterator(root)){check(!cancelled,"cancelled");if(entry.is_regular_file())actual.insert(entry.path().lexically_relative(root).generic_u8string());}
    for(auto&name:actual)check(info.names.count(name)||auxiliary.count(name),"extra output file: "+(root/fs::u8path(name)).u8string()+" (custom runtime/deployment files are not supported by this strict comparison)");
    for(auto&name:info.names)check(actual.count(name),"missing output file: "+name);return info;
}
bool compareOutputs(const fs::path& native,const fs::path& reference,const fs::path& source,bool exact) {
    std::cout<<"Comparing native output with Python output. This reads both archives and the source.\n";
    auto coverage=auditSourceInventory(source,native,reference);
    std::cout<<"Validating native output...\n";verify(native/"ampr_assets.index",source);
    bool replaced=approvedRuntimeDifference(reference,source);
    std::cout<<"Validating Python output...\n";verify(reference/"ampr_assets.index",source,replaced?reference/"fakelib/libSceAmpr.sprx":fs::path{});
    auto a=comparisonInfo(native,coverage.nativeLoose),b=comparisonInfo(reference,coverage.referenceLoose);
    check(a.files.size()==b.files.size(),"comparison file count differs");
    std::map<std::string,size_t> referencePaths;for(size_t i=0;i<b.paths.size();i++)check(referencePaths.emplace(b.paths[i],i).second,"duplicate reference path: "+b.paths[i]);std::set<std::string> nativePaths;
    for(size_t i=0;i<a.files.size();i++) {check(nativePaths.insert(a.paths[i]).second,"duplicate native path: "+a.paths[i]);auto match=referencePaths.find(a.paths[i]);check(match!=referencePaths.end(),"Python output is missing asset: "+a.paths[i]);auto&x=a.files[i];auto&y=b.files[match->second];if(i!=match->second)std::cout<<"File ID order differs: "<<a.paths[i]<<" (content checked by path)\n";
        if(replaced&&a.paths[i]==runtimeAsset){check(!y.flags,"replacement runtime must be a loose file");std::cout<<"Runtime difference reported separately at ID "<<i+1<<"; pinned replacement validated.\n";continue;}
        check(x.logicalSize==y.logicalSize,"comparison file size mismatch: "+a.paths[i]);
        if(x.flags!=y.flags||x.blockShift!=y.blockShift||x.packingClass!=y.packingClass||x.mtime!=y.mtime)std::cout<<"Packing profile/metadata differs: "<<a.paths[i]<<" (decoded content matches source)\n";
    }
    std::cout<<"FINAL CONTENT CHECK PASS: every original file is accounted for and indexed assets match the source, with only the pinned runtime replacement permitted; manifests, CRCs and declared loose files validated. This is not a PS5 launch test.\n";
    bool bytesEqual=a.names==b.names;std::set<std::string> all=a.names;all.insert(b.names.begin(),b.names.end());phase("Final archive byte comparison");uint64_t hashTotal=0,hashDone=0;for(auto&name:all){if(a.names.count(name)&&b.names.count(name)){hashTotal+=fs::file_size(native/fs::u8path(name));hashTotal+=fs::file_size(reference/fs::u8path(name));}}if(progressHook)progressHook(0,hashTotal);
    for(auto&name:all){check(!cancelled,"cancelled");if(!a.names.count(name)||!b.names.count(name)){bytesEqual=false;std::cout<<"Archive filename difference: "<<name<<"\n";continue;}
        auto left=native/fs::u8path(name),right=reference/fs::u8path(name);
        auto streamedDigest=[&](const fs::path&p){Sha hash;std::ifstream in(p,std::ios::binary);check(bool(in),"cannot open final comparison file");char buffer[65536];while(in){ensureActive();in.read(buffer,sizeof buffer);auto n=in.gcount();if(n){hash.add(buffer,n);hashDone+=n;if(progressHook)progressHook(hashDone,hashTotal);}}check(in.eof(),"final hash read failed");return hash.finish();};
        auto lhash=streamedDigest(left),rhash=streamedDigest(right);
        if(fs::file_size(left)!=fs::file_size(right)||lhash!=rhash){bytesEqual=false;std::cout<<"Archive byte difference: "<<name<<"\n";}}
    std::cout<<(bytesEqual?"PASS: exact archive bytes match.\n":"Archive bytes differ. Game content verified; layout, configuration or runtime bytes differ.\n");
    check(!exact||bytesEqual,"exact archive-byte equality was required but differs");return bytesEqual;
}
