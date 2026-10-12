#include "Commands.h"
#include "Runtime.h"
#include "Engine.h"
#include <cstdarg>

namespace XRay::Commands {
static void Print(const char* format,...) {
    auto console=reinterpret_cast<void*(__cdecl*)(bool)>(0x71B160)(false);
    if(!console)return;
    va_list args;va_start(args,format);
    //consolemanager::print takes fmt + va_list, thiscall with ret 8
    reinterpret_cast<void(__thiscall*)(void*,const char*,va_list)>(0x71D0A0)(console,format,args);
    va_end(args);
}
static bool Head(void*,void*,void* object,void*,void*,void*,double* result,uint32_t*) {
    *result=0;
    auto form=static_cast<Engine::Form*>(object);
    if(!Engine::Supported(form)) {
        Print("XRayHead: select a human, ghoul, feral ghoul or super mutant in the console first.");return true;
    }
    uint32_t id=form->id;
    if(SelectHead(id)){*result=1;Print("X-ray head selected: %08X. Close the console to see it.",id);}
    else Print("X-ray preview is unavailable; check XRayKillcamNVSE.log.");
    return true;
}
static bool View(void*,void*,void*,void*,void*,void*,double* result,uint32_t*) {
    auto mode=ChangeView();*result=mode;
    Print("X-ray view: %s",mode==1?"intact skull":"cutaway");return true;
}
static bool Off(void*,void*,void*,void*,void*,void*,double* result,uint32_t*) {
    Clear();*result=1;Log("preview off");Print("X-ray preview off.");return true;
}
static CommandInfo s_commands[]={
    {"XRayHead","",0,"Preview anatomy on the selected human actor",1,0,nullptr,Head,nullptr,nullptr,0},
    {"XRayView","",0,"Switch intact/cutaway preview",0,0,nullptr,View,nullptr,nullptr,0},
    {"XRayOff","",0,"Disable anatomy preview",0,0,nullptr,Off,nullptr,nullptr,0}
};
bool Register(const NVSEInterface* nvse) {
    constexpr uint32_t base=0x40E6;
    auto table=static_cast<NVSECommandTableInterface*>(nvse->QueryInterface(3));
    if(!table)return false;
    for(unsigned i=0;i<3;++i) {
        auto existing=table->GetByOpcode(base+i);
        if((existing&&existing->longName&&*existing->longName)||table->GetByName(s_commands[i].longName)) {
            Log("preview command registration conflict at %04X",base+i);return false;
        }
    }
    nvse->SetOpcodeBase(base);
    for(auto& command:s_commands)if(!nvse->RegisterCommand(&command))return false;
    return true;
}
}
