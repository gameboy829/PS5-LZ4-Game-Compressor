// SPDX-License-Identifier: GPL-3.0-or-later
void makeIndex(const fs::path& root,const fs::path& path) {
    std::vector<Entry> entries;
    for(auto& item:fs::recursive_directory_iterator(root)) {
        ensureActive(); check(!item.is_symlink(),"symbolic links are unsupported during folder indexing");
        if(!item.is_regular_file()) continue;
        auto rel=item.path().lexically_relative(root).generic_u8string(); auto key=rel;
        for(char& c:key) if(c>='A'&&c<='Z') c+=32;
        if(key=="ampr_emu.index"||key=="ampr_emu.index.tmp"||key=="ampr_commands.bin"||key=="apr_emu.log") continue;
        check(key!="ampr_assets.index"&&key!="ampr_assets.index.crc"&&key!="ampr_assets.index.runtime","source contains existing pack metadata; select the original uncompressed folder");
        entries.push_back({"/app0/"+rel,fs::file_size(item.path()),diskMtime(item.path())});
    }
    auto fold=[](std::string s) {for(char& c:s) if(c>='A'&&c<='Z')c+=32;return s;};
    std::sort(entries.begin(),entries.end(),[&](auto&a,auto&b){return fold(a.path)<fold(b.path);});
    Bytes records,strings; std::set<std::string> seen;
    for(auto&e:entries) { check(seen.insert(fold(e.path)).second,"case-insensitive filename collision"); check(strings.size()+e.path.size()+1<=0xffffffffull,"index string limit exceeded");
        SourceEntry r{uint32_t(strings.size()),uint32_t(e.path.size()),e.size,e.mtime}; append(records,r); strings.insert(strings.end(),e.path.begin(),e.path.end()); strings.push_back(0); }
    uint64_t slotCount=entries.empty()?0:2; while(slotCount<entries.size()*2)slotCount*=2; check(slotCount<=0xffffffffull,"too many index slots");
    struct Slot {uint64_t hash;uint32_t id,flags;}; std::vector<Slot> slots(slotCount); std::set<uint64_t> hashes;
    for(uint32_t i=0;i<entries.size();i++) {auto h=indexhash(entries[i].path); check(hashes.insert(h).second,"source path hash collision"); auto pos=h&(slotCount-1); while(slots[pos].id)pos=(pos+1)&(slotCount-1); slots[pos]={h,i+1,0};}
    auto offset=align(sizeof(SourceHeader)+records.size()+strings.size(),16);
    SourceHeader h{}; memcpy(h.magic,"AMPRIDX3",8); h.version=3; h.entrySize=24; h.count=entries.size(); h.pathBytes=strings.size(); h.hashOffset=offset; h.slotSize=16;h.slotCount=uint32_t(slotCount);
    std::ofstream out(path,std::ios::binary); write(out,&h,sizeof h);write(out,records.data(),records.size());write(out,strings.data(),strings.size());zeros(out,offset-sizeof h-records.size()-strings.size()); for(auto&s:slots)write(out,&s,16);
    std::cout<<"Created source index outside source: "<<entries.size()<<" files\n";
}
