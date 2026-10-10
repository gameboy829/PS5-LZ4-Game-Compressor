// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Functional exclusions: loaders, system data and already encoded media remain loose.
static const std::string safety_json=[](){
    nlohmann::json patterns=nlohmann::json::array();
    auto both=[&](const std::string& name){patterns.push_back(name);patterns.push_back("**/"+name);};
    both("eboot.bin");
    for(const auto& category:std::vector<std::string>{
        "elf self prx sprx bak dat utoc",
        "json ini cfg xml txt",
        "bk2 mp4 ivf usm bnk wem at9 pfs img",
        "png jpg jpeg webp gif bmp ico tga tif tiff exr hdr psd",
        "dds ktx ktx2 astc basis gnf gnfp jxm vtf",
        "zip 7z rar gz xz bz2 zst lz4",
        "mp3 ogg flac aac opus m4a avi mkv mov wmv flv webm m4v"}){
        std::istringstream words(category);std::string extension;while(words>>extension)both("*."+extension);
    }
    for(const char* directory:{"decrypted","sce_module","sce_sys","system","mods","save","fakelib","_DUPLEX_","trophy2","uds"})both(std::string(directory)+"/**");
    for(const char* file:{"ampr_emu.index","ampr_assets.index","ampr_assets.index.crc","ampr_assets.index.runtime"})both(file);
    patterns.push_back("ampr_assets-*.pak");patterns.push_back("**/*.pak");return patterns.dump();
}();
