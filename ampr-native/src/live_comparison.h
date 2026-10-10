// SPDX-License-Identifier: GPL-3.0-or-later
// Metadata is resident; asset data is decoded one bounded block at a time.
class LiveReference {
    fs::path root;Bytes data,strings,crcs;AmprPackIndexHeader header{};
    std::vector<fs::path> volumes;std::vector<AmprPackDataRecord> packs;bool runtimeReplaced=false;
    std::map<std::string,size_t> pathIds;
public:
    LiveReference(const fs::path& directory):root(directory) {
        data=read(root/"ampr_assets.index");header=at<AmprPackIndexHeader>(data,0);auto h=header;auto saved=h.headerCrc32;h.headerCrc32=0;
        check(!memcmp(h.magic,"AMPRPAK4",8)&&h.version==4&&h.headerSize==128&&h.endianMarker==0x01020304&&!h.flags&&!h.reserved,"Python reference has an invalid manifest header");
        check(crc(&h,128)==saved&&crc(data.data()+128,data.size()-128)==h.payloadCrc32,"Python reference manifest CRC failed");
        check(h.fileRecordSize==48&&h.chunkRecordSize==12&&h.packRecordSize==32&&h.fileCount<=(data.size()-128)/48&&h.chunkCount<=(data.size()-128)/12&&h.packCount<=(data.size()-128)/32,"Python reference record counts/geometry invalid");
        check(h.filesOffset==128&&h.chunksOffset==128+h.fileCount*48&&h.packsOffset==h.chunksOffset+h.chunkCount*12&&h.stringsOffset==h.packsOffset+h.packCount*32&&h.stringsOffset<=data.size()&&h.stringsSize==data.size()-h.stringsOffset,"Python reference manifest bounds invalid");
        strings=Bytes(data.begin()+h.stringsOffset,data.end());crcs=read(root/"ampr_assets.index.crc");auto ch=at<CrcHeader>(crcs,0);auto cs=ch.header;ch.header=0;
        check(!memcmp(ch.magic,"AMPRCRC1",8)&&ch.version==1&&ch.size==48&&ch.count==h.chunkCount&&!memcmp(ch.id,h.buildId,16)&&crcs.size()==48+h.chunkCount*4&&crc(&ch,48)==cs&&crc(crcs.data()+48,crcs.size()-48)==ch.payload,"Python reference CRC sidecar invalid");
        auto entries=sourceindex(root/"ampr_emu.index");check(entries.size()==h.fileCount,"Python reference index count mismatch");
        for(size_t i=0;i<entries.size();i++){check(pathIds.emplace(path(i),i).second,"duplicate Python reference path");auto f=file(i);check(path(i)==entries[i].path&&f.logicalSize==entries[i].size&&f.pathHash==pathhash(path(i)),"Python reference source-index identity mismatch");}
        for(uint32_t i=0;i<h.packCount;i++){auto p=at<AmprPackDataRecord>(data,h.packsOffset+i*32);auto name=stringat(strings,p.nameOffset,p.nameLength);auto location=safe(root,name);std::ifstream in(location,std::ios::binary);AmprPackDataHeader d{};in.read((char*)&d,64);auto dc=d.headerCrc32;d.headerCrc32=0;
            check(bool(in)&&!memcmp(d.magic,"AMPRDAT3",8)&&d.version==3&&d.headerSize==64&&d.packId==i&&d.flags==p.flags&&!d.reserved&&!memcmp(d.buildId,h.buildId,16)&&crc(&d,64)==dc,"Python reference volume header invalid: "+name);
            check(fs::file_size(location)==p.fileSize&&d.payloadOffset>=64&&d.payloadOffset<=p.fileSize&&d.payloadBytes==p.payloadBytes&&p.payloadBytes==p.fileSize-d.payloadOffset,"Python reference volume size invalid: "+name);packs.push_back(p);volumes.push_back(location);}
    }
    size_t idForPath(const std::string& name)const{auto found=pathIds.find(name);check(found!=pathIds.end(),"Python reference missing asset: "+name);return found->second;}
    size_t count()const{return size_t(header.fileCount);}
    void approveRuntime(const fs::path& source){runtimeReplaced=approvedRuntimeDifference(root,source);if(runtimeReplaced){bool found=false;for(size_t i=0;i<count();i++)if(path(i)==runtimeAsset){check(file(i).flags==0,"replacement runtime must be loose");found=true;}check(found,"replacement runtime is absent from reference manifest");}}
    bool isReplacement(size_t id)const{return runtimeReplaced&&path(id)==runtimeAsset;}
    AmprPackFileRecord file(size_t id)const{check(id<header.fileCount,"Python reference missing file ID "+std::to_string(id+1));return at<AmprPackFileRecord>(data,header.filesOffset+id*48);}
    std::string path(size_t id)const{auto f=file(id);return stringat(strings,f.pathOffset,f.pathLength);}
    Bytes digest(size_t id){auto f=file(id);auto name=path(id);if(!(f.flags&1)){auto p=safe(root,name.substr(6));check(fs::file_size(p)==f.logicalSize,"Python loose file size mismatch: "+name);return fileDigest(p);}
        check(f.blockShift>=14&&f.blockShift<=20&&f.firstChunk<=header.chunkCount&&f.chunkCount<=header.chunkCount-f.firstChunk,"Python file chunk bounds invalid: "+name);
        check(f.chunkCount==f.logicalSize/(1ull<<f.blockShift)+(f.logicalSize%(1ull<<f.blockShift)!=0),"Python file block geometry invalid: "+name);
        Sha sha;uint64_t left=f.logicalSize;
        for(uint32_t i=0;i<f.chunkCount;i++){check(!cancelled,"cancelled");auto id=f.firstChunk+i;auto c=at<AmprPackChunkRecord>(data,header.chunksOffset+uint64_t(id)*12);auto pid=ampr_pack_chunk_pack_id(c);auto off=ampr_pack_chunk_offset(c);auto n=ampr_pack_chunk_stored_size(c);auto codec=ampr_pack_chunk_codec(c);auto rawsize=std::min<uint64_t>(left,1ull<<f.blockShift);
            check(ampr_pack_chunk_descriptor_valid(c)&&codec<=1&&pid<packs.size()&&n<=rawsize,"Python chunk descriptor invalid: "+name);auto&p=packs[pid];check(off>=p.fileSize-p.payloadBytes&&off<=p.fileSize&&n<=p.fileSize-off,"Python chunk outside volume: "+name);
            Bytes stored(n),raw(rawsize);std::ifstream in(volumes[pid],std::ios::binary);in.seekg(off);in.read((char*)stored.data(),n);check(bool(in),"Python chunk read failed: "+name);
            if(codec)check(LZ4_decompress_safe((char*)stored.data(),(char*)raw.data(),n,int(rawsize))==rawsize,"Python LZ4 decode failed: "+name);else{check(n==rawsize,"Python RAW size mismatch: "+name);raw=stored;}
            check(crc(raw.data(),raw.size())==at<uint32_t>(crcs,48+uint64_t(id)*4),"Python decoded CRC mismatch: "+name+" block "+std::to_string(i));sha.add(raw.data(),raw.size());left-=rawsize;}
        return sha.finish();
    }
    void compareFile(size_t id,const std::string& name,const AmprPackFileRecord& f,const std::vector<AmprPackChunkRecord>& chunks,const std::vector<uint32_t>& checksums,const fs::path& output){
        auto nativeId=id;id=idForPath(name);(void)nativeId;
        if(isReplacement(id)){std::cout<<"Live runtime difference reported at ID "<<id+1<<": pinned Python replacement already verified; installed runtime will be hash-checked during the final check.\n";return;}
        check(file(id).logicalSize==f.logicalSize,"Live comparison size mismatch: "+name);
        Bytes native;if(!(f.flags&1))native=fileDigest(safe(output,name.substr(6)));else{
            Sha hash;uint64_t left=f.logicalSize;
            for(uint32_t i=0;i<f.chunkCount;i++){check(!cancelled,"cancelled");auto id=f.firstChunk+i;auto&c=chunks.at(id);auto n=ampr_pack_chunk_stored_size(c);auto size=std::min<uint64_t>(left,1ull<<f.blockShift);Bytes stored(n),raw(size);auto pid=ampr_pack_chunk_pack_id(c);auto name=activeVolumeNames.empty()?std::string("ampr_assets-000.pak"):activeVolumeNames.at(pid);std::ifstream in(output/fs::u8path(name),std::ios::binary);check(bool(in),"cannot reopen native volume for live comparison");in.seekg(ampr_pack_chunk_offset(c));in.read((char*)stored.data(),n);check(bool(in),"native live comparison read failed: "+name);
                if(ampr_pack_chunk_codec(c))check(LZ4_decompress_safe((char*)stored.data(),(char*)raw.data(),n,int(size))==size,"native live decode failed: "+name);else raw=stored;
                check(crc(raw.data(),raw.size())==checksums.at(id),"native live decoded CRC mismatch: "+name);hash.add(raw.data(),raw.size());left-=size;}native=hash.finish();}
        auto reference=digest(id);check(native==reference,"LIVE CONTENT MISMATCH: file ID "+std::to_string(id+1)+" "+name+"; decoded SHA-256 differs between native and Python output");
        std::cout<<"Live comparison PASS "<<id+1<<"/"<<count()<<": "<<name<<" (decoded SHA-256)\n";
    }
};
