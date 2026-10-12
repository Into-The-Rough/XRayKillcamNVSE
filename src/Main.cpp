#include "PluginAPI.h"
#include "Runtime.h"
#include "Commands.h"
#include "ShotAudit.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <share.h>

namespace XRay {
static FILE* s_log=nullptr;
static char s_modelPath[MAX_PATH];
static bool s_commandsReady=false;
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
static float ReadFloat(const char* path,const char* name,float value,float lo,float hi,const char* section="Preview") {
    char fallback[32],text[64];sprintf_s(fallback,"%.6g",value);
    GetPrivateProfileStringA(section,name,fallback,text,sizeof text,path);
    char* end=nullptr;float result=strtof(text,&end);
    return end==text||*end||!std::isfinite(result)?value:Clamp(result,lo,hi);
}
static void Message(NVSEMessagingInterface::Message* msg) {
    switch(msg->type) {
    case NVSEMessagingInterface::DeferredInit:
        Log(s_commandsReady&&InitRuntime(s_modelPath)?"ready, console: XRayHead, XRayView, XRayOff":"initialization failed");break;
    case NVSEMessagingInterface::MainGameLoop:Update();break;
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
    if(sprintf_s(path,"%sData\\config\\XRayKillcamNVSE.ini",runtime)<0)return false;
    if(GetPrivateProfileIntA("Debug","ShotAudit",0,path))Audit::Init(runtime);
    auto& a=settings.appearance;
    a.opacity=ReadFloat(path,"Opacity",1,0,1);a.scale=ReadFloat(path,"Scale",1,.5f,1.5f);
    a.up=ReadFloat(path,"Up",0,-8,8);a.forward=ReadFloat(path,"Forward",0,-8,8);a.side=ReadFloat(path,"Side",0,-8,8);
    a.depthTolerance=ReadFloat(path,"DepthTolerance",2,0,4);
    settings.automatic=GetPrivateProfileIntA("Automatic","Enabled",1,path)!=0;
    settings.revealSeconds=ReadFloat(path,"Duration",5,1,30,"Automatic");
    settings.kills=GetPrivateProfileIntA("Automatic","Kills",1,path)!=0;
    settings.cripples=GetPrivateProfileIntA("Automatic","Cripples",1,path)!=0;
    settings.criticals=GetPrivateProfileIntA("Automatic","Criticals",1,path)!=0;
    settings.groin=GetPrivateProfileIntA("Automatic","Groin",1,path)!=0;
    settings.everyHit=GetPrivateProfileIntA("Automatic","EveryHit",0,path)!=0;
    settings.regions[TorsoRegion]=GetPrivateProfileIntA("Automatic","Torso",1,path)!=0;
    settings.regions[LegsRegion]=GetPrivateProfileIntA("Automatic","Legs",1,path)!=0;
    settings.regions[ArmsRegion]=GetPrivateProfileIntA("Automatic","Arms",1,path)!=0;
    settings.bloodSpray=GetPrivateProfileIntA("Automatic","BloodSpray",1,path)!=0;
    settings.sounds=GetPrivateProfileIntA("Automatic","Sounds",1,path)!=0;
    settings.feralGhouls=GetPrivateProfileIntA("Automatic","FeralGhouls",1,path)!=0;
    settings.superMutants=GetPrivateProfileIntA("Automatic","SuperMutants",1,path)!=0;
    settings.soundVolume=ReadFloat(path,"SoundVolume",1,0,1,"Automatic");
    settings.killSeverity=ReadFloat(path,"KillSeverity",1.8f,1,3,"Automatic");
    settings.bloodAmount=ReadFloat(path,"BloodAmount",1.5f,0,4,"Automatic");
    settings.nativeCamera=GetPrivateProfileIntA("Cinematic","Enabled",1,path)!=0;
    settings.cameraSeconds=ReadFloat(path,"Duration",4,.5f,10,"Cinematic");
    settings.impactSpeed=ReadFloat(path,"ImpactSpeed",.3f,.05f,1,"Cinematic");
    settings.closeUp=GetPrivateProfileIntA("Cinematic","CloseUp",1,path)!=0;
    settings.closeUpDistance=ReadFloat(path,"CloseUpDistance",28,10,80,"Cinematic");
    settings.closeUpOrbit=ReadFloat(path,"CloseUpOrbit",20,0,90,"Cinematic");
    Log("head depth tolerance: %.3f game units",a.depthTolerance);
    if(sprintf_s(s_modelPath,"%sData\\meshes\\XRayKillcamNVSE\\head.xra",runtime)<0)return false;
    auto messages=static_cast<NVSEMessagingInterface*>(nvse->QueryInterface(2));
    if(!messages||!messages->RegisterListener(nvse->GetPluginHandle(),"NVSE",Message))return false;
    s_commandsReady=Commands::Register(nvse);
    if(!s_commandsReady)Log("preview disabled: console commands unavailable");
    return true;
}
}
extern "C" __declspec(dllexport) bool NVSEPlugin_Query(const NVSEInterface* nvse,PluginInfo* info) {
    info->infoVersion=1;info->name="XRayKillcamNVSE";info->version=10000;return !nvse->isEditor;
}
extern "C" __declspec(dllexport) bool NVSEPlugin_Load(const NVSEInterface* nvse) {return XRay::Load(nvse);}
