#include "Engine.h"
#include <cstring>

namespace XRay::Engine {
bool PlayFromFolder(const char* folder,char (&last)[260],float volume,float speed) {
    //bswin32audio at 0x11f6d98, its folder pick at +0x28 is cdecl(path) and swaps the folder for one of its files (0x5e3630):
    //archives always, loose files by findfirstfile from the game folder when 0x11acdf9 is set
    auto audio=*reinterpret_cast<uint8_t**>(0x11F6D98);
    if(!audio||!folder)return false;
    auto pick=*reinterpret_cast<bool(__cdecl**)(char*)>(audio+0x28);
    char path[260];
    if(!pick||strcpy_s(path,folder)||!pick(path))return false;
    for(unsigned retry=0;retry<2&&last[0]&&!_stricmp(path,last);++retry) {
        char again[260];
        if(!strcpy_s(again,folder)&&pick(again))strcpy_s(path,again);
    }
    strcpy_s(last,path);
    SoundHandle handle;
    //0xad7480 takes four stack args (handle, path, flags, form) and ret 0x10, 0x101 is a 2d one-shot the global timescale pitches
    reinterpret_cast<void(__thiscall*)(uint8_t*,SoundHandle*,const char*,uint32_t,void*)>(0xAD7480)(audio,&handle,path,0x101,nullptr);
    if(handle.id==~0u)return false;
    //volume 0xad89e0 and speed 0xad8a90 queue audio commands, play 0xad8830 thiscall(handle, char)
    if(volume<1)reinterpret_cast<void(__thiscall*)(SoundHandle*,float)>(0xAD89E0)(&handle,Clamp(volume,0,1));
    reinterpret_cast<void(__thiscall*)(SoundHandle*,float)>(0xAD8A90)(&handle,speed);
    reinterpret_cast<void(__thiscall*)(SoundHandle*,char)>(0xAD8830)(&handle,0);
    return true;
}
}
