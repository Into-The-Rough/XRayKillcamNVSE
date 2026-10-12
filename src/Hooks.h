#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace XRay {
class CallDetour {
    uintptr_t original_=0;
public:
    template<class T> bool ReplaceCall(uintptr_t site,T hook) {
        constexpr uint8_t call=0xE8;
        if(original_||*reinterpret_cast<uint8_t*>(site)!=call)return false;
        int32_t displacement;memcpy(&displacement,reinterpret_cast<void*>(site+1),4);
        uintptr_t original=site+5+displacement;
        if(original==reinterpret_cast<uintptr_t>(hook))return false;
        uint8_t bytes[5]={call};displacement=static_cast<int32_t>(reinterpret_cast<uintptr_t>(hook)-site-5);
        memcpy(bytes+1,&displacement,4);
        DWORD old;
        if(!VirtualProtect(reinterpret_cast<void*>(site),5,PAGE_EXECUTE_READWRITE,&old))return false;
        original_=original;memcpy(reinterpret_cast<void*>(site),bytes,5);
        DWORD unused;VirtualProtect(reinterpret_cast<void*>(site),5,old,&unused);
        FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(site),5);return true;
    }
    uintptr_t Original()const{return original_;}
};
}
