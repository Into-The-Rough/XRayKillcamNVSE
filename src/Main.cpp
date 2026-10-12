#include "PluginAPI.h"
#include "Runtime.h"
#include "ShotAudit.h"
#include "Config.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <share.h>

namespace XRay {
static FILE* s_log=nullptr;
static char s_modelPath[MAX_PATH],s_iniPath[MAX_PATH];
void Log(const char* format,...) {
    if(!s_log)return;
    va_list args;va_start(args,format);vfprintf(s_log,format,args);va_end(args);
    fputc('\n',s_log);fflush(s_log);
}
//a launch that never shoots keeps the log of the last session that did
static void KeepShotLog(const char* path,const char* runtime) {
    FILE* old=nullptr;
    if(fopen_s(&old,path,"rb")||!old)return;
    char line[512];bool shots=false;
    while(!shots&&fgets(line,sizeof line,old))shots=!strncmp(line,"trajectory ",11);
    fclose(old);
    char kept[MAX_PATH];
    if(shots&&sprintf_s(kept,"%sXRayKillcamNVSE.previous.log",runtime)>0)MoveFileExA(path,kept,MOVEFILE_REPLACE_EXISTING);
}
static void Message(NVSEMessagingInterface::Message* msg) {
    switch(msg->type) {
    case NVSEMessagingInterface::DeferredInit:
        Log(InitRuntime(s_modelPath)?"ready":"initialisation failed");break;
    case NVSEMessagingInterface::MainGameLoop:Update();break;
    case NVSEMessagingInterface::ReloadConfig:
        if(msg->data&&msg->dataLen&&!_stricmp(static_cast<const char*>(msg->data),"XRayKillcamNVSE")){Config::Load(s_iniPath);ApplySettings();}
        break;
    case NVSEMessagingInterface::ExitGame:case NVSEMessagingInterface::ExitGameConsole:
    case NVSEMessagingInterface::ExitToMainMenu:case NVSEMessagingInterface::PreLoadGame:
    case NVSEMessagingInterface::PostLoadGame:case NVSEMessagingInterface::NewGame:Clear();break;
    }
}
bool Load(const NVSEInterface* nvse) {
    const char* runtime=nvse->GetRuntimeDirectory();if(!runtime)return false;
    char path[MAX_PATH];
    if(sprintf_s(path,"%sXRayKillcamNVSE.log",runtime)<0)return false;
    KeepShotLog(path,runtime);
    s_log=_fsopen(path,"w",_SH_DENYWR);Log("XRayKillcamNVSE 1.0.0");
    if(sprintf_s(s_iniPath,"%sData\\config\\XRayKillcamNVSE.ini",runtime)<0)return false;
    if(GetPrivateProfileIntA("Debug","ShotAudit",0,s_iniPath))Audit::Init(runtime);
    Config::Load(s_iniPath);
    if(sprintf_s(s_modelPath,"%sData\\meshes\\XRayKillcamNVSE\\head.xra",runtime)<0)return false;
    auto messages=static_cast<NVSEMessagingInterface*>(nvse->QueryInterface(2));
    if(!messages||!messages->RegisterListener(nvse->GetPluginHandle(),"NVSE",Message))return false;
    return true;
}
}
extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface* nvse,PluginInfo* info) {
    info->infoVersion=1;info->name="XRayKillcamNVSE";info->version=10000;
    //the low violence exe has shifted addresses
    return !nvse->isEditor&&!nvse->isNogore;
}
extern "C" __declspec(dllexport) bool NVSEPlugin_Load(const NVSEInterface* nvse) {return XRay::Load(nvse);}
