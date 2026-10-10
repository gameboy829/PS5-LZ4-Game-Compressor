// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Application-authored profile construction using AMPR's licensed configuration interface.
fs::path generateUniversalProfile(const fs::path& source,const fs::path& output,int level,bool fast,const fs::path& learnedDirectory={}) {
    check(level>=1&&level<=12,"Unsupported compression level");
    Json pack={
        {"index_name","ampr_assets.index"},{"pack_pattern","ampr_assets-{group}-lane{lane:02d}-vol{volume:02d}-{id:03d}.pak"},
        {"default_action","loose"},{"default_block_size","64KiB"},{"io_page_size","64KiB"},{"payload_alignment","64KiB"},{"chunk_alignment","64B"},
        {"workers",8},{"compression_mode",fast||level<=4?"fast":"hc"},{"compression_level",level},{"acceleration",1},
        {"deduplicate",true},{"deduplicate_scope","lane"},{"deduplicate_streaming",false},
        {"min_savings_bytes",64},{"min_savings_ratio",0.01},{"io_neutral_min_savings_bytes","8KiB"},{"io_neutral_min_savings_ratio",0.125},
        {"auto_loose_large_files",true},{"auto_loose_hot_files",false},{"auto_loose_min_file_size","64MiB"},
        {"auto_loose_sample_blocks",32},{"auto_loose_sample_bytes","16MiB"},{"auto_loose_min_savings_ratio",0.05},{"auto_loose_max_raw_ratio",0.9},
        {"preserve_mtime",true},{"validate_index_metadata",true}};
    std::cout<<"Profile: compression settings prepared\n";
    std::ostringstream text;
    auto table=[&](const std::string& heading,const Json& data){text<<"\n["<<heading<<"]\n";for(auto i=data.begin();i!=data.end();++i)text<<i.key()<<" = "<<i.value().dump()<<"\n";};
    table("pack",pack);
    table("runtime",Json{{"decoded_cache_bytes","256MiB"},{"physical_cache_bytes","64MiB"},{"workers",8},{"latency_reserve_workers",1}});
    table("groups.assets",Json{{"pack_count",4},{"assignment","balanced"},{"max_pack_size","16GiB"},{"stripe_large_files",false}});
    auto rule=[&](Json data){text<<"\n[[rule]]\n";for(auto i=data.begin();i!=data.end();++i)text<<i.key()<<" = "<<i.value().dump()<<"\n";};
    Json mixed={{"action","compress"},{"group","assets"},{"layout","mixed"},{"block_size","64KiB"}};
    Json hot={{"action","compress"},{"group","assets"},{"layout","random"},{"block_size","16KiB"},{"hot",true},{"min_savings_bytes",16},{"min_savings_ratio",0.0025},{"io_neutral_min_savings_bytes","2KiB"},{"io_neutral_min_savings_ratio",0.125}};
    std::set<std::string> suffixes;Json numbered=Json::array();bool nested=false,audio=false,video=false;
    const std::vector<std::string> audioWords={"sound","audio","sfx","music","voice","vo","dialog","dialogue","soundbank","wem"};
    const std::vector<std::string> videoWords={"video","movie","movies","cutscene","cutscenes","fmv","cinematic","cinematics"};
    std::cout<<"Profile: creating filename classifier\n";
    const std::regex numbering("[a-zA-Z_]+\\.[0-9]+|[a-zA-Z_]+[0-9]+\\.[a-zA-Z]+");
    std::cout<<"Profile: scanning filenames\n";
    for(auto& item:fs::recursive_directory_iterator(source)){
        ensureActive();check(!item.is_symlink(),"Source links are unsupported");if(!item.is_regular_file())continue;
        auto relative=item.path().lexically_relative(source).generic_u8string();auto slash=relative.find('/');
        if(slash!=std::string::npos){nested=true;auto top=relative.substr(0,slash);std::transform(top.begin(),top.end(),top.begin(),[](unsigned char c){return char(std::tolower(c));});for(auto& word:audioWords)audio|=top.find(word)!=std::string::npos;for(auto& word:videoWords)video|=top.find(word)!=std::string::npos;}
        else if(std::regex_match(relative,numbering))numbered.push_back(relative);
        auto suffix=item.path().extension().u8string();std::transform(suffix.begin(),suffix.end(),suffix.begin(),[](unsigned char c){return char(std::tolower(c));});suffixes.insert(suffix);
    }
    std::cout<<"Profile: writing file rules\n";
    if(nested){auto r=mixed;r["include"]=Json::array({"*/**"});rule(r);}
    std::cout<<"Profile: nested rule ready\n";
    if(!numbered.empty()){std::sort(numbered.begin(),numbered.end());auto r=mixed;r["include"]=numbered;rule(r);}
    std::cout<<"Profile: numbered rule ready\n";
    const std::set<std::string> assetTypes={".dds",".ktx",".ktx2",".astc",".basis",".tga",".gnf",".gnfp",".jxm",".vtf",".dat",".res",".uasset",".ubulk",".bin"};Json matching=Json::array();for(auto& suffix:suffixes)if(assetTypes.count(suffix))matching.push_back("**/*"+suffix);
    if(!matching.empty()){auto r=mixed;r["include"]=matching;rule(r);}
    std::cout<<"Profile: asset rules ready\n";
    if(audio){Json names=Json::array();for(const char* name:{"sound","audio","sfx","music","voice","vo","dialog","dialogue"})names.push_back(std::string("**/")+name+"/**");auto r=hot;r["include"]=names;rule(r);}
    if(video){Json names=Json::array();for(auto& name:videoWords)names.push_back("**/"+name+"/**");rule(Json{{"action","loose"},{"include",names}});}
    Json streams=Json::array();for(const char* name:{"movie","movies","soundbank","screenreaderwem","wem"}){streams.push_back(std::string("**/")+name);if(std::string(name)!="screenreaderwem")streams.push_back(std::string("**/")+name+"_*");if(std::string(name)=="screenreaderwem"||std::string(name)=="wem")streams.push_back(std::string("**/")+name+".*");}
    rule(Json{{"action","loose"},{"include",streams}});
    if(std::any_of(suffixes.begin(),suffixes.end(),[](const std::string& s){return s.find("soundbank")!=std::string::npos;})){auto r=hot;r["include"]=Json::array({"**/soundbank.*"});rule(r);}
    std::cout<<"Profile: streaming rules ready\n";
    std::set<std::string> learned;
    if(!learnedDirectory.empty()&&fs::is_directory(learnedDirectory))for(auto& file:fs::directory_iterator(learnedDirectory)){
        if(!file.is_regular_file()||file.path().extension()!=".toml")continue;
        auto parsed=toml::parse_file(file.path().u8string());std::ostringstream json;json<<toml::json_formatter(parsed);auto imported=Json::parse(json.str());
        for(auto& r:imported.value("rule",Json::array()))if(r.value("action",std::string())=="loose")for(auto& p:r.value("include",Json::array()))learned.insert(p.get<std::string>());
    }
    if(!learned.empty()){Json names=Json::array();for(auto& p:learned)names.push_back(p);rule(Json{{"action","loose"},{"include",names}});}
    std::cout<<"Profile: preparing safety rules\n";
    rule(Json{{"action","loose"},{"include",Json::parse(safety_json)}});
    std::cout<<"Profile: saving configuration\n";
    auto encoded=text.str();std::ofstream file(output,std::ios::binary);write(file,encoded.data(),encoded.size());return output;
}
