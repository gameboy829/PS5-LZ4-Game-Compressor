// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../ampr-native/third_party/config/json.hpp"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cassert>
#include <unistd.h>
using J=nlohmann::json;namespace fs=std::filesystem;
#define STUDIO_HOST_TEST
#define AMPR_STATE_ROOT "/tmp/lz4-report-fixture"
#define SHADOW_DEBUG_LOG "/tmp/lz4-report-fixture/shadow-debug.log"
static const char* APP_VERSION="test";static std::mutex stateLock,control;static J state={{"active",false}},sharedSetup=J::object();
#include "../standalone/report.inc"
int main(){fs::create_directories(AMPR_STATE_ROOT);fs::remove(SHADOW_DEBUG_LOG);auto missing=diagnosticReport();assert(missing["ok"]==true);assert(missing["logs"]["shadowmount/debug.log"]["available"]==false);
{std::ofstream f(SHADOW_DEBUG_LOG);f<<std::string(1024*1024+15,'x')<<"CRASH";}
auto log=diagnosticReport()["logs"]["shadowmount/debug.log"];assert(log["available"]==true);assert(log["truncated"]==true);auto text=log["text"].get<std::string>();assert(text.size()==1024*1024);assert(text.substr(text.size()-5)=="CRASH");fs::remove(SHADOW_DEBUG_LOG);fs::create_symlink("/etc/passwd",SHADOW_DEBUG_LOG);assert(diagnosticReport()["logs"]["shadowmount/debug.log"]["available"]==false);fs::remove(SHADOW_DEBUG_LOG);puts("PASS: ShadowMount log inclusion, bounded tail, missing log and symlink rejection");}
