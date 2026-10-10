// SPDX-License-Identifier: GPL-3.0-or-later
// Native translation of the pinned ampr_pack.py configuration semantics.
#include "../third_party/config/toml.hpp"
#include "../third_party/config/json.hpp"
#include <regex>
#include <map>
#include <memory>
#include <iomanip>
#include <sstream>
#include "safety_patterns.h"
using Json=nlohmann::json;
fs::path activeProfile,activeRuntime;
bool productionPack=false;
std::function<void(const std::string&)> phaseHook;
std::function<void()> outputCreatedHook;
std::vector<std::string> activeVolumeNames;
void phase(const std::string& name){if(phaseHook)phaseHook(name);std::cout<<"Stage: "<<name<<"\n";}
uint64_t sizeValue(const Json& v){
    if(v.is_number_unsigned()||v.is_number_integer()){check(v.get<int64_t>()>=0,"negative size");return v.get<uint64_t>();}
    check(v.is_string(),"size must be an integer or string");auto s=v.get<std::string>();std::smatch m;
    check(std::regex_match(s,m,std::regex("([0-9]+)(B|KiB|MiB|GiB|TiB)?",std::regex::icase)),"invalid size: "+s);
    uint64_t n=std::stoull(m[1]);std::string unit=m[2];for(char&c:unit)c=char(tolower((unsigned char)c));int shift=unit=="kib"?10:unit=="mib"?20:unit=="gib"?30:unit=="tib"?40:0;
    check(n<=(UINT64_MAX>>shift),"size overflow");return n<<shift;
}
int shiftValue(const Json& v){auto n=sizeValue(v);check(n>=16384&&n<=1048576&&!(n&(n-1)),"block size must be power of two 16KiB..1MiB");int s=0;while(n>1){n>>=1;++s;}return s;}
bool globOne(const std::string&s,const std::string&p){
    // Python fnmatchcase: '*' includes '/' (including '**').
    std::string re="^";for(size_t i=0;i<p.size();++i){char c=p[i];if(c=='*')re+=".*";else if(c=='?')re+='.';else if(c=='['){auto end=p.find(']',i+1);check(end!=std::string::npos,"invalid glob bracket: "+p);auto part=p.substr(i+1,end-i-1);if(!part.empty()&&part[0]=='!')part[0]='^';re+='[';re+=part;re+=']';i=end;}else{if(std::string(".\\+(){}^$|").find(c)!=std::string::npos)re+='\\';re+=c;}}re+='$';return std::regex_match(s,std::regex(re));
}
bool globs(const std::string&s,const Json& patterns){for(auto&p:patterns)if(globOne(s,p.get<std::string>()))return true;return false;}
Json normalizedRule(const Json& value,const Json& c,const fs::path& directory){
    Json r={{"action",value.value("action",std::string("compress"))},{"include",value.value("include",Json::array({"**"}))},{"exclude",value.value("exclude",Json::array())},
    {"block_shift",shiftValue(value.value("block_size",Json(uint64_t(1)<<c["default_block_shift"].get<int>())))},{"group",value.value("group",std::string("default"))},
    {"mode",value.value("compression_mode",c["compression_mode"].get<std::string>())},{"level",value.value("compression_level",c["compression_level"].get<int>())},
    {"acceleration",value.value("acceleration",c["acceleration"].get<int>())},{"layout",value.value("layout",std::string("auto"))},
    {"streaming",value.value("streaming",false)},{"hot",value.value("hot",false)},{"force_pack",value.value("force_pack",false)}};
    for(auto key:{"min_savings_bytes","io_neutral_min_savings_bytes"})r[key]=sizeValue(value.value(key,c[key]));
    for(auto key:{"min_savings_ratio","io_neutral_min_savings_ratio"})r[key]=value.value(key,c[key].get<double>());
    for(auto key:{"include_from","exclude_from"})if(value.contains(key)){
        auto target=std::string(key)=="include_from"?"include":"exclude";if(target==std::string("include")&&!value.contains("include"))r[target]=Json::array();
        for(auto&name:value[key]){std::ifstream in(safe(directory,name.get<std::string>()));check(bool(in),"cannot open pattern file");std::string line;while(std::getline(in,line)){auto a=line.find_first_not_of(" \t\r"),b=line.find_last_not_of(" \t\r");if(a!=std::string::npos&&line[a]!='#')r[target].push_back(line.substr(a,b-a+1));}}
    }
    check(r["include"].is_array()&&!r["include"].empty()&&r["exclude"].is_array(),"invalid rule patterns");
    auto action=r["action"].get<std::string>(),mode=r["mode"].get<std::string>(),layout=r["layout"].get<std::string>();
    check(action=="loose"||action=="compress"||action=="store","invalid rule action");check(mode=="hc"||mode=="fast","invalid codec mode");check(layout=="auto"||layout=="mixed"||layout=="random"||layout=="streaming","invalid layout");
    check(r["level"].get<int>()>=1&&r["level"].get<int>()<=12&&r["acceleration"].get<int>()>=1,"invalid codec settings");for(auto key:{"min_savings_ratio","io_neutral_min_savings_ratio"})check(r[key].get<double>()>=0&&r[key].get<double>()<1,"invalid savings ratio");return r;
}
struct NativeConfig{Json canonical,runtime;bool preserve=true,selfContained=false;Json required=Json::array();};
NativeConfig loadNativeConfig(const fs::path& path){
    std::ifstream input(path);check(bool(input),"profile not found: "+path.u8string());std::string text((std::istreambuf_iterator<char>(input)),{});
    auto table=toml::parse(text);std::ostringstream encoded;encoded<<toml::json_formatter(table);auto raw=Json::parse(encoded.str()),p=raw.value("pack",Json::object());
    NativeConfig result;auto&c=result.canonical;c=Json::parse(profile_json);c["default_action"]="loose";c["compression_level"]=12;c["deduplicate"]=true;c["auto_loose_large_files"]=true;
    for(auto key:{"index_name","pack_pattern","default_action","compression_mode","compression_level","acceleration","deduplicate","deduplicate_scope","deduplicate_streaming","auto_loose_large_files","auto_loose_hot_files","auto_loose_sample_blocks","auto_loose_min_savings_ratio","auto_loose_max_raw_ratio","min_savings_ratio","io_neutral_min_savings_ratio"})if(p.contains(key))c[key]=p[key];
    for(auto key:{"io_page_size","payload_alignment","chunk_alignment","min_savings_bytes","io_neutral_min_savings_bytes","auto_loose_min_file_size","auto_loose_sample_bytes"})if(p.contains(key))c[key]=sizeValue(p[key]);
    c["default_block_shift"]=shiftValue(p.value("default_block_size",Json("64KiB")));if(!p.contains("payload_alignment"))c["payload_alignment"]=c["io_page_size"];
    check(c["index_name"]=="ampr_assets.index","this app requires index_name=ampr_assets.index");
    for(auto key:{"io_page_size","payload_alignment","chunk_alignment"}){auto n=c[key].get<uint64_t>();check(n&&!(n&(n-1))&&n<=1048576,"invalid alignment");}
    check(c["io_page_size"].get<uint64_t>()>=4096&&c["payload_alignment"].get<uint64_t>()>=64&&c["chunk_alignment"].get<uint64_t>()<=c["io_page_size"].get<uint64_t>(),"invalid page/alignment range");
    check(c["deduplicate_scope"]=="lane"||c["deduplicate_scope"]=="group","invalid deduplication scope");check(c["auto_loose_sample_blocks"].get<int>()>=1&&c["auto_loose_sample_blocks"].get<int>()<=4096&&c["auto_loose_sample_bytes"].get<uint64_t>()>0,"invalid sampling settings");
    for(auto key:{"auto_loose_min_savings_ratio","auto_loose_max_raw_ratio"})check(c[key].get<double>()>=0&&c[key].get<double>()<=1,"invalid sampling ratio");
    std::map<std::string,Json> groups;auto rawgroups=raw.value("groups",Json::object());rawgroups["default"]=rawgroups.value("default",Json::object());
    for(auto it=rawgroups.begin();it!=rawgroups.end();++it){auto v=it.value();Json g={{"name",it.key()},{"pack_count",v.value("pack_count",1)},{"assignment",v.value("assignment",std::string("balanced"))},{"stripe_large_files",v.value("stripe_large_files",false)},{"stripe_group_blocks",v.value("stripe_group_blocks",8)}};
        g["max_pack_size"]=sizeValue(v.value("max_pack_size",Json(0)));g["stripe_threshold"]=sizeValue(v.value("stripe_threshold",Json("256MiB")));g["io_page_size"]=sizeValue(v.value("io_page_size",Json(0)));
        check(g["pack_count"].get<int>()>=1&&g["pack_count"].get<int>()<=65535&&g["stripe_group_blocks"].get<int>()>=1,"invalid group settings");auto page=g["io_page_size"].get<uint64_t>();if(!page)page=c["io_page_size"];
        check(page>=4096&&page<=1048576&&!(page&(page-1))&&page%c["chunk_alignment"].get<uint64_t>()==0,"invalid group page size");auto limit=g["max_pack_size"].get<uint64_t>();check(!limit||limit>=std::max(page,c["payload_alignment"].get<uint64_t>())+page,"volume limit too small");
        check(g["assignment"]=="balanced"||g["assignment"]=="hash"||g["assignment"]=="round_robin","invalid lane assignment");groups[it.key()]=g;}
    check(groups.size()<=256,"too many groups");c["groups"]=Json::array();for(auto&g:groups)c["groups"].push_back(g.second);
    c["rules"]=Json::array();for(auto&v:raw.value("rule",Json::array())){auto r=normalizedRule(v,c,path.parent_path());check(groups.count(r["group"].get<std::string>()),"unknown rule group");c["rules"].push_back(r);}
    normalizedRule(Json{{"action",c["default_action"]}},c,path.parent_path());result.preserve=p.value("preserve_mtime",true);result.selfContained=p.value("self_contained",false);result.required=p.value("required_packed",Json::array());
    result.runtime=raw.value("runtime",Json());if(!result.runtime.is_null()){auto&r=result.runtime;check(r.size()==4,"runtime requires exactly four settings");for(auto key:{"decoded_cache_bytes","physical_cache_bytes"})r[key]=sizeValue(r.at(key));int w=r.at("workers"),reserve=r.at("latency_reserve_workers");check(w>=1&&w<=16&&reserve>=0&&reserve<w&&r["decoded_cache_bytes"].get<uint64_t>()%16384==0&&r["physical_cache_bytes"].get<uint64_t>()%16384==0,"invalid runtime settings");}
    return result;
}
std::string resolvedLayout(const Json&r){auto layout=r["layout"].get<std::string>();return layout!="auto"?layout:r["streaming"].get<bool>()?"streaming":r["hot"].get<bool>()?"random":"mixed";}
std::string renderedName(std::string pattern,const std::string& group,int lane,int volume,int id){
    std::regex token("\\{(group|lane|volume|id)(?::0?([0-9]+)d)?\\}");std::string result;std::smatch match;
    while(std::regex_search(pattern,match,token)){result+=match.prefix();auto key=match[1].str();if(key=="group")result+=group;else{int n=key=="lane"?lane:key=="volume"?volume:id;std::ostringstream s;if(match[2].matched){int width=std::stoi(match[2]);check(width<=16,"pack pattern width too large");s<<std::setfill('0')<<std::setw(width);}s<<n;result+=s.str();}pattern=match.suffix();}result+=pattern;check(result.find('{')==std::string::npos&&result.find('}')==std::string::npos&&result.size()<=1017,"unsupported pack pattern");return result;
}
