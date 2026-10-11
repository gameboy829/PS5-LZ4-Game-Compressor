// SPDX-License-Identifier: GPL-3.0-or-later
// Use saved data only: diagnostic collection must not wait on game I/O locks.
#ifndef STUDIO_HOST_TEST
#include <ps5/kernel.h>
#endif
#ifndef SHADOW_DEBUG_LOG
#define SHADOW_DEBUG_LOG "/data/shadowmount/debug.log"
#endif
static J diagnosticReport(){
    J report={{"ok",true},{"schema",1},{"appVersion",APP_VERSION},{"logs",J::object()}};
#ifndef STUDIO_HOST_TEST
    char firmware[32];snprintf(firmware,sizeof(firmware),"0x%08x",kernel_get_fw_version());report["firmware"]=firmware;
#endif
    {std::unique_lock<std::mutex> lock(stateLock,std::try_to_lock);if(lock.owns_lock())report["job"]=state;else report["jobUnavailable"]="State is busy";}
    {std::unique_lock<std::mutex> lock(control,std::try_to_lock);if(lock.owns_lock())report["setup"]=sharedSetup;else report["setupUnavailable"]="Operation is busy";}
    for(auto name:{"logs/current.log","launcher-install.log","startup-selftest.log","pipeline-threads.json","shadowmount/debug.log"}){
        auto path=std::string(name)=="shadowmount/debug.log"?fs::path(SHADOW_DEBUG_LOG):fs::path(AMPR_STATE_ROOT)/name;std::error_code ec;auto st=fs::symlink_status(path,ec);
        if(ec||!fs::is_regular_file(st)){report["logs"][name]={{"available",false}};continue;}
        auto bytes=fs::file_size(path,ec);if(ec)continue;
        const uint64_t limit=1024*1024;auto count=std::min<uint64_t>(bytes,limit);std::ifstream file(path,std::ios::binary);file.seekg(bytes-count);
        std::string data(count,'\0');file.read(data.data(),data.size());data.resize(file.gcount());
        // Replace invalid UTF-8 instead of letting a malformed log abort the report.
        report["logs"][name]={{"available",bool(file)||file.eof()},{"bytes",bytes},{"truncated",bytes>limit},{"text",J::parse(J(data).dump(-1,' ',false,J::error_handler_t::replace))}};
    }
    return report;
}
