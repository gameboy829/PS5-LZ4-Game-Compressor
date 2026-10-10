// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
void portableLoosePermissions(const fs::path& src,const fs::path& dst){
#ifndef _WIN32
    auto mode=fs::perms::owner_read|fs::perms::owner_write|fs::perms::group_read|fs::perms::others_read;
    auto execute=fs::perms::owner_exec|fs::perms::group_exec|fs::perms::others_exec;
    auto extension=dst.extension().u8string();
    bool program=dst.filename()=="eboot.bin"||extension==".prx"||extension==".sprx"||extension==".elf"||extension==".self";
    if(program||(fs::status(src).permissions()&execute)!=fs::perms::none)mode|=execute;
    fs::permissions(dst,mode,fs::perm_options::replace);
#endif
}
