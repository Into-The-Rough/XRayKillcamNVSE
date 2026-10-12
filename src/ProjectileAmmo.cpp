#include "Engine.h"
#include "Runtime.h"
#include <cstring>

namespace XRay::Engine {
static bool s_jipAmmo=false;
void InitProjectileAmmo() {
    auto jip=GetModuleHandleA("jip_nvse.dll");
    auto version=jip?reinterpret_cast<double(__cdecl*)()>(GetProcAddress(jip,"GetJIPLNVersion")):nullptr;
    //jip 57.30 initprojectilesetammohook owns +0x60/+0x14a, vanilla has no stored ammo
    if(version&&fabs(version()-57.30)<.00001) {
        auto site=reinterpret_cast<const uint8_t*>(0x9BC241);
        if(*site==0xE9) {
            int32_t displacement;memcpy(&displacement,site+1,sizeof displacement);
            auto target=reinterpret_cast<LPCSTR>(reinterpret_cast<uintptr_t>(site+5)+displacement);
            HMODULE owner=nullptr;
            s_jipAmmo=GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,target,&owner)&&owner==jip;
        }
    }
    Log("projectile ammo: %s",s_jipAmmo?"JIP 57.30 launch snapshot; standard bullet profiles enabled":"unavailable; generic bullet profile");
}
uint32_t ProjectileAmmoID(Form* projectile) {
    if(!s_jipAmmo||!projectile)return 0;
    auto ammo=reinterpret_cast<JIPProjectile*>(projectile)->GetAmmo();
    return ammo&&ammo->type==kAmmo?ammo->id:0;
}
}
