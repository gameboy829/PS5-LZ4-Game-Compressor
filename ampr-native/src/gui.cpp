// SPDX-License-Identifier: GPL-3.0-or-later
#define wmain engine_main
#include "main.cpp"
#include "app_version.h"
#undef wmain
#include "comparison.h"
#include "live_comparison.h"
#include "universal_profile.h"
#include "index_builder.h"
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <thread>
#include <sstream>
#include <chrono>
#include <atomic>
#include <memory>
#include <mutex>

static HWND window,sourceEdit,destEdit,indexEdit,levelBox,fastBox,logBox,statusLabel,progressBar,startButton,cancelButton;
static HWND referenceEdit,profileEdit,autoCompareBox,exactBox,compareButton;
static HWND liveButton,verifyBox;
static bool lastUnverified=false;
static std::mutex stageMutex;static std::string stageName="Preparing";static std::chrono::steady_clock::time_point stageStarted;
void guiPhase(const std::string&name);
static std::ofstream diagnosticLog;static std::mutex diagnosticMutex;static fs::path diagnosticPath;
static bool comparisonOnly=false;
static HFONT font;
static bool running=false,smoke=false;
static int smokeExit=1;
static std::chrono::steady_clock::time_point started;
static std::atomic<uint64_t> doneBytes{0},totalBytes{0};
void guiPhase(const std::string&name){std::lock_guard<std::mutex>guard(stageMutex);stageName=name;stageStarted=std::chrono::steady_clock::now();doneBytes=0;totalBytes=0;}
constexpr UINT LOG_MESSAGE=WM_APP+1, FINISH_MESSAGE=WM_APP+2;
std::wstring wide(const std::string& s) { int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0); std::wstring w(n,0); MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n); return w; }
std::wstring textOf(HWND control) { int n=GetWindowTextLengthW(control); std::wstring s(n+1,0); GetWindowTextW(control,s.data(),n+1); s.resize(n); return s; }
void logLine(std::string s) {{std::lock_guard<std::mutex> guard(diagnosticMutex);if(diagnosticLog){diagnosticLog<<s;diagnosticLog.flush();}}std::string normalized;for(char c:s){if(c=='\n')normalized+='\r';normalized+=c;} auto w=new std::wstring(wide(normalized)); if(!PostMessageW(window,LOG_MESSAGE,0,(LPARAM)w)) delete w; }
class LogBuffer:public std::streambuf {
    std::string pending;
    int overflow(int c) override { if(c!=EOF) { pending+=char(c); if(c=='\n') {logLine(pending);pending.clear();} } return c; }
    int sync() override { if(!pending.empty()) {logLine(pending);pending.clear();} return 0; }
};
HWND control(const wchar_t* cls,const wchar_t* title,DWORD style,int x,int y,int w,int h,int id=0) {
    HWND c=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,title,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(c,WM_SETFONT,(WPARAM)font,TRUE); return c;
}
std::wstring browse(bool folder,bool tomlFile=false) {
    IFileOpenDialog* dialog=nullptr; std::wstring result;
    if(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)))) {
        DWORD options; dialog->GetOptions(&options); dialog->SetOptions(options|FOS_FORCEFILESYSTEM|(folder?FOS_PICKFOLDERS:0));
        if(!folder) { COMDLG_FILTERSPEC filter{tomlFile?L"Python packing profile":L"AMPR source index",tomlFile?L"*.toml":L"*.index"}; dialog->SetFileTypes(1,&filter); }
        if(SUCCEEDED(dialog->Show(window))) { IShellItem* item=nullptr; if(SUCCEEDED(dialog->GetResult(&item))) { PWSTR path=nullptr; if(SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))) { result=path; CoTaskMemFree(path); } item->Release(); } }
        dialog->Release();
    } return result;
}
fs::path selectedOutputFolder(const fs::path& selected,const fs::path& source) {
    std::error_code error;
    if(fs::is_regular_file(selected/L"ampr_assets.index",error))return selected;
    auto name=source.filename().wstring();if(name.empty())name=L"assets";
    return selected/(name+L"-ampr");
}
void pathError(const std::wstring& reason,const fs::path& path) {
    auto message=reason+L"\n\n"+path.wstring();
    MessageBoxW(window,message.c_str(),L"Check folder",MB_OK|MB_ICONINFORMATION);
}
void start(bool onlyCompare=false,bool live=false) {
    if(running)return;
    fs::path source=textOf(sourceEdit),dest=textOf(destEdit),index=textOf(indexEdit);
    fs::path selectedProfile=textOf(profileEdit);
    fs::path reference=textOf(referenceEdit);bool doCompare=live||onlyCompare||SendMessageW(autoCompareBox,BM_GETCHECK,0,0)==BST_CHECKED;
    bool exact=SendMessageW(exactBox,BM_GETCHECK,0,0)==BST_CHECKED;bool verifyOutput=SendMessageW(verifyBox,BM_GETCHECK,0,0)==BST_CHECKED;lastUnverified=!onlyCompare&&!verifyOutput&&!doCompare;
    if(source.empty()){pathError(L"Original game: select the extracted original game folder.",source);return;}
    if(dest.empty()){pathError(onlyCompare?L"Native / PS5: select the completed compressed folder.":L"Native / PS5: enter a new output folder.",dest);return;}
    std::error_code sourceError,destError;bool sourceOk=fs::is_directory(source,sourceError),destExists=fs::exists(dest,destError);
    if(!sourceOk||sourceError){pathError(L"Original game folder cannot be opened.",source);return;}
    if(destError){pathError(L"Native / PS5 folder cannot be accessed.",dest);return;}
    if(!onlyCompare&&destExists){pathError(L"Packing needs a new output folder. To compare this existing output, use Compare folders.",dest);return;}
    std::error_code manifestError;
    if(onlyCompare&&!fs::is_regular_file(dest/L"ampr_assets.index",manifestError)){pathError(L"Native / PS5: this folder does not contain ampr_assets.index. Select the completed output folder itself.",dest);return;}
    manifestError.clear();
    if(doCompare&&!fs::is_regular_file(reference/L"ampr_assets.index",manifestError)){pathError(L"Python reference: select the completed Python output folder containing ampr_assets.index.",reference);return;}
    if(!onlyCompare&&!selectedProfile.empty()&&!fs::is_regular_file(selectedProfile)){MessageBoxW(window,L"The TOML profile does not exist.",L"Packing profile",MB_OK|MB_ICONERROR);return;}
    comparisonOnly=onlyCompare;
    running=true;cancelled=0;doneBytes=0;totalBytes=0;started=std::chrono::steady_clock::now();guiPhase("Preparing"); SetWindowTextW(logBox,L"");SetTimer(window,1,500,nullptr);
    try{wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto directory=fs::path(exe).parent_path().parent_path()/L"logs";fs::create_directories(directory);diagnosticPath=directory/(L"ampr-run-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".log");diagnosticLog.close();diagnosticLog.clear();diagnosticLog.open(diagnosticPath,std::ios::binary);logLine("Diagnostic log: "+diagnosticPath.u8string()+"\nSource: "+source.u8string()+"\nNative output: "+dest.u8string()+"\nPython reference: "+reference.u8string()+"\nMode: "+(live?"pack and compare during packing":onlyCompare?"compare existing":"pack")+"\n");}catch(const std::exception&e){logLine(std::string("Could not save diagnostic log: ")+e.what()+"\n");}
    for(auto c:{sourceEdit,destEdit,indexEdit,levelBox,fastBox,startButton,referenceEdit,profileEdit,autoCompareBox,exactBox,compareButton,liveButton,verifyBox})EnableWindow(c,FALSE);
    for(int id:{101,102,103,107,110})EnableWindow(GetDlgItem(window,id),FALSE);
    EnableWindow(cancelButton,TRUE); SetWindowTextW(statusLabel,L"Preparing source index..."); SendMessageW(progressBar,PBM_SETMARQUEE,TRUE,35);
    bool fast=SendMessageW(fastBox,BM_GETCHECK,0,0)==BST_CHECKED; int level=int(SendMessageW(levelBox,CB_GETCURSEL,0,0))+1;
    std::thread([source,dest,index,fast,level,reference,doCompare,exact,onlyCompare,live,selectedProfile,verifyOutput]()mutable {
        LogBuffer buffer; auto old=std::cout.rdbuf(&buffer); auto olderr=std::cerr.rdbuf(&buffer); int result=1; fs::path temp,profileTemp;
        try {
            phaseHook=guiPhase;progressHook=[](uint64_t done,uint64_t total){doneBytes=done;totalBytes=total;};
            if(!onlyCompare){if(index.empty()) { auto existing=source/L"ampr_emu.index"; if(fs::is_regular_file(existing)&&!fs::is_regular_file(source/L"fakelib/libSceAmpr.sprx")) {index=existing;logLine("Using existing source index to preserve file IDs.\n");}
                else {temp=fs::temp_directory_path()/(L"ampr-gui-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".index");makeIndex(source,temp);index=temp;} }
            progressHook=[](uint64_t done,uint64_t total){doneBytes=done;totalBytes=total;};
            std::unique_ptr<LiveReference> referenceReader;std::vector<Entry> liveEntries;
            if(live){logLine("Checking Python reference metadata before packing...\n");referenceReader=std::make_unique<LiveReference>(reference);referenceReader->approveRuntime(source);liveEntries=sourceindex(index);check(referenceReader->count()==liveEntries.size(),"Live comparison source/reference file counts differ");
                for(size_t i=0;i<liveEntries.size();i++){auto refId=referenceReader->idForPath(liveEntries[i].path);check(referenceReader->isReplacement(refId)||referenceReader->file(refId).logicalSize==liveEntries[i].size,"Live comparison size mismatch: "+liveEntries[i].path);}
                fileCompleteHook=[&](size_t id,const AmprPackFileRecord& f,const std::vector<AmprPackChunkRecord>& chunks,const std::vector<uint32_t>& crcs){referenceReader->compareFile(id,liveEntries.at(id).path,f,chunks,crcs,dest);};}
            auto profile=selectedProfile;
            if(profile.empty()){profileTemp=fs::temp_directory_path()/(L"ampr-profile-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".toml");phase("Generating compression profile");wchar_t appPath[32768]{};GetModuleFileNameW(nullptr,appPath,32768);profile=generateUniversalProfile(source,profileTemp,level,fast,fs::path(appPath).parent_path().parent_path()/L"resources/toml_profiles");}
            wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);auto runtime=fs::path(exe).parent_path().parent_path()/L"resources/fakelib/libSceAmpr.sprx";
            logLine("Native production profile: "+profile.u8string()+"\nImported profiles retain their compression settings.\n");productionPack=true;packProfile(source,index,dest,profile,runtime);if(verifyOutput){logLine("Packing finished. Verifying against source...\n");totalBytes=0;verify(dest/L"ampr_assets.index",source);}else{logLine("Final verification skipped.\n");if(!doCompare){std::ofstream marker(dest/L"UNVERIFIED.txt");marker<<"Verification skipped. Keep the original until independently checked and launch-tested.\n";check(bool(marker),"Cannot write unverified-output notice");}}}
            if(doCompare)compareOutputs(dest,reference,source,exact);result=0;
        }catch(const std::exception&e){logLine(std::string("ERROR: ")+e.what()+"\n");}
        fileCompleteHook=nullptr;progressHook=nullptr;phaseHook=nullptr;productionPack=false;buffer.pubsync();std::cout.rdbuf(old);std::cerr.rdbuf(olderr);
        if(!profileTemp.empty()){std::error_code ec;fs::remove(profileTemp,ec);}
        if(!temp.empty()){std::error_code ec;fs::remove(temp,ec);}
        PostMessageW(window,FINISH_MESSAGE,result,0);
    }).detach();
}
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        window=hwnd;font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
        control(L"STATIC",AMPR_APP_TITLE,0,22,18,750,28);
        control(L"STATIC",L"Compare completed outputs, or choose a new output folder to pack.",0,22,49,750,22);
        control(L"STATIC",L"Original game",0,22,89,120,20);sourceEdit=control(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,145,84,530,27);control(L"BUTTON",L"Browse...",WS_TABSTOP,685,84,92,27,101);
        control(L"STATIC",L"Native / PS5",0,22,130,120,20);destEdit=control(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,145,125,530,27);control(L"BUTTON",L"Browse...",WS_TABSTOP,685,125,92,27,102);
        control(L"STATIC",L"Source index",0,22,171,120,20);indexEdit=control(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,145,166,530,27);control(L"BUTTON",L"Browse...",WS_TABSTOP,685,166,92,27,103);
        control(L"STATIC",L"Optional: leave blank to use the folder's index or create one automatically.",0,145,199,620,22);
        control(L"STATIC",L"Compression level",0,22,241,140,20);levelBox=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,168,236,90,250);
        for(int i=1;i<=12;i++){auto s=std::to_wstring(i);SendMessageW(levelBox,CB_ADDSTRING,0,(LPARAM)s.c_str());}SendMessageW(levelBox,CB_SETCURSEL,6,0);
        fastBox=control(L"BUTTON",L"Fast mode",BS_AUTOCHECKBOX|WS_TABSTOP,281,239,140,24,104);
        control(L"STATIC",L"Python reference",0,22,285,120,20);referenceEdit=control(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,145,280,530,27);control(L"BUTTON",L"Browse...",WS_TABSTOP,685,280,92,27,107);
        autoCompareBox=control(L"BUTTON",L"Compare after packing",BS_AUTOCHECKBOX|WS_TABSTOP,22,318,235,24);
        exactBox=control(L"BUTTON",L"Require exact archive bytes",BS_AUTOCHECKBOX|WS_TABSTOP,280,318,270,24);
        verifyBox=control(L"BUTTON",L"Verify after packing",BS_AUTOCHECKBOX|WS_TABSTOP,555,318,225,24);SendMessageW(verifyBox,BM_SETCHECK,BST_CHECKED,0);
        startButton=control(L"BUTTON",L"Start packing",BS_DEFPUSHBUTTON|WS_TABSTOP,22,360,150,34,105);
        liveButton=control(L"BUTTON",L"Pack and compare",WS_TABSTOP,184,360,150,34,109);
        compareButton=control(L"BUTTON",L"Compare folders",WS_TABSTOP,346,360,150,34,108);
        cancelButton=control(L"BUTTON",L"Cancel",WS_TABSTOP,508,360,95,34,106);EnableWindow(cancelButton,FALSE);
        progressBar=control(PROGRESS_CLASSW,L"",PBS_MARQUEE,620,364,157,26);
        statusLabel=control(L"STATIC",L"Ready. Verification is enabled. Original folders stay untouched.",0,22,409,755,24);
        logBox=control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,22,445,755,195);
        SendMessageW(logBox,EM_SETLIMITTEXT,8*1024*1024,0);
        control(L"STATIC",L"TOML profile",0,22,664,120,20);profileEdit=control(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,145,659,530,27);control(L"BUTTON",L"Browse...",WS_TABSTOP,685,659,92,27,110);
        control(L"STATIC",L"Comparison checks every original file, packed data, loose files and archive bytes.",0,22,700,755,32);
        return 0; }
    case WM_COMMAND: switch(LOWORD(wp)) {
        case 101:{auto p=browse(true);if(!p.empty()){SetWindowTextW(sourceEdit,p.c_str());if(textOf(destEdit).empty())SetWindowTextW(destEdit,(p+L"-ampr").c_str());}break;}
        case 102:{auto p=browse(true);if(!p.empty()){auto out=selectedOutputFolder(p,fs::path(textOf(sourceEdit)));SetWindowTextW(destEdit,out.c_str());}break;}
        case 103:{auto p=browse(false);if(!p.empty())SetWindowTextW(indexEdit,p.c_str());break;}
        case 104:EnableWindow(levelBox,SendMessageW(fastBox,BM_GETCHECK,0,0)!=BST_CHECKED);break;
        case 105:start();break;
        case 107:{auto p=browse(true);if(!p.empty())SetWindowTextW(referenceEdit,p.c_str());break;}
        case 108:start(true);break;
        case 109:start(false,true);break;
        case 110:{auto p=browse(false,true);if(!p.empty())SetWindowTextW(profileEdit,p.c_str());break;}
        case 106:cancelled=1;EnableWindow(cancelButton,FALSE);SetWindowTextW(statusLabel,L"Cancelling after the current block...");break;
    }return 0;
    case LOG_MESSAGE:{if(GetWindowTextLengthW(logBox)>6*1024*1024){SendMessageW(logBox,EM_SETSEL,0,1024*1024);SendMessageW(logBox,EM_REPLACESEL,FALSE,(LPARAM)L"[Earlier lines retained in the diagnostic log file.]\r\n");}auto p=(std::wstring*)lp;SendMessageW(logBox,EM_SETSEL,-1,-1);SendMessageW(logBox,EM_REPLACESEL,FALSE,(LPARAM)p->c_str());delete p;return 0;}
    case WM_TIMER:if(running&&!cancelled){auto total=totalBytes.load(),done=doneBytes.load();std::string stage;std::chrono::steady_clock::time_point began;{std::lock_guard<std::mutex>guard(stageMutex);stage=stageName;began=stageStarted;}
        auto elapsed=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-started).count();auto stageElapsed=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-began).count();auto label=wide(stage)+L" | Elapsed "+std::to_wstring(elapsed)+L"s";
        if(total){done=std::min(done,total);SendMessageW(progressBar,PBM_SETMARQUEE,FALSE,0);auto percent=int(double(done)/total*100);SendMessageW(progressBar,PBM_SETPOS,percent,0);label=wide(stage)+L": "+std::to_wstring(percent)+L"% | Elapsed "+std::to_wstring(elapsed)+L"s";if(done&&done<total&&stageElapsed>1){auto eta=int64_t(double(total-done)/done*stageElapsed);label+=L" | Stage remaining ~"+std::to_wstring(eta)+L"s";}if(done==total)label+=L" | Finishing stage...";
        }else SendMessageW(progressBar,PBM_SETMARQUEE,TRUE,35);SetWindowTextW(statusLabel,label.c_str());}return 0;
    case FINISH_MESSAGE: {
        running=false;KillTimer(hwnd,1);SendMessageW(progressBar,PBM_SETMARQUEE,FALSE,0);SendMessageW(progressBar,PBM_SETPOS,wp?0:100,0);
        for(auto c:{sourceEdit,destEdit,indexEdit,levelBox,fastBox,startButton,referenceEdit,profileEdit,autoCompareBox,exactBox,compareButton,liveButton,verifyBox})EnableWindow(c,TRUE);
        for(int id:{101,102,103,107,110})EnableWindow(GetDlgItem(window,id),TRUE);
        EnableWindow(levelBox,SendMessageW(fastBox,BM_GETCHECK,0,0)!=BST_CHECKED);EnableWindow(cancelButton,FALSE);
        auto seconds=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-started).count();
        auto status=wp?(cancelled?L"Cancelled. Partial output was retained.":L"Failed. See the log for details."):(comparisonOnly?L"Comparison passed. See the log for exact byte results.":lastUnverified?L"Complete: UNVERIFIED. Verification skipped; keep the original.":L"Complete. See the log for verification/comparison results.");
        SetWindowTextW(statusLabel,status);logLine("Elapsed: "+std::to_string(seconds)+" seconds\n");
        if(smoke){smokeExit=int(wp);PostMessageW(hwnd,WM_CLOSE,0,0);} return 0; }
    case WM_CLOSE: if(running){cancelled=1;SetWindowTextW(statusLabel,L"Cancelling... Close the window when the operation stops.");return 0;}DestroyWindow(hwnd);return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX controls{sizeof controls,ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);
    WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.lpszClassName=L"AmprNativeGUI";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&cls);
    HWND hwnd=CreateWindowExW(0,cls.lpszClassName,AMPR_APP_TITLE,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,815,800,nullptr,nullptr,instance,nullptr);
    if(_wgetenv(L"AMPR_TEST_SKIP_VERIFY"))SendMessageW(verifyBox,BM_SETCHECK,BST_UNCHECKED,0);
    if(auto testProfile=_wgetenv(L"AMPR_TEST_PROFILE"))SetWindowTextW(profileEdit,testProfile);
    int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc==5&&std::wstring(args[1])==L"--test-output-picker"){auto actual=selectedOutputFolder(args[2],args[3]);int result=actual==fs::path(args[4])?0:1;LocalFree(args);DestroyWindow(hwnd);CoUninitialize();return result;}
    if(argc==5&&std::wstring(args[1])==L"--open-folders"){SetWindowTextW(sourceEdit,args[2]);SetWindowTextW(destEdit,args[3]);SetWindowTextW(referenceEdit,args[4]);ShowWindow(hwnd,show);UpdateWindow(hwnd);}
    else if(argc==5&&std::wstring(args[1])==L"--self-test"){smoke=true;SetWindowTextW(sourceEdit,args[2]);SetWindowTextW(indexEdit,args[3]);SetWindowTextW(destEdit,args[4]);ShowWindow(hwnd,SW_HIDE);PostMessageW(hwnd,WM_COMMAND,105,0);}
    else if(argc==6&&(std::wstring(args[1])==L"--test-compare"||std::wstring(args[1])==L"--test-pack-compare"||std::wstring(args[1])==L"--test-live")){smoke=true;SetWindowTextW(sourceEdit,args[2]);SetWindowTextW(destEdit,args[3]);SetWindowTextW(referenceEdit,args[4]);SendMessageW(exactBox,BM_SETCHECK,std::wstring(args[5])==L"exact"?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(autoCompareBox,BM_SETCHECK,BST_CHECKED,0);if(std::wstring(args[1])!=L"--test-compare"){auto sourceIndex=fs::path(args[2])/L"ampr_emu.index";SetWindowTextW(indexEdit,(fs::is_regular_file(sourceIndex)?sourceIndex:(fs::path(args[4])/L"ampr_emu.index")).c_str());}ShowWindow(hwnd,SW_HIDE);PostMessageW(hwnd,WM_COMMAND,std::wstring(args[1])==L"--test-compare"?108:std::wstring(args[1])==L"--test-live"?109:105,0);}
    else {ShowWindow(hwnd,show);UpdateWindow(hwnd);}LocalFree(args);
    MSG message;while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(hwnd,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    CoUninitialize();return smoke?smokeExit:0;
}



