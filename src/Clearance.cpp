#include "Engine.h"

namespace XRay::Engine {
namespace {
constexpr uint32_t kLineOfSightLayer=37;
//the pick wrapper reads the exterior grid without a guard when there is no interior, so a cell has to be loaded
//tes +0x34 current interior, +0x08 grid, grid +0x0c size, zero while it is torn down
bool CellLoaded(const uint8_t* tes) {
    if(*reinterpret_cast<void* const*>(tes+0x34))return true;
    auto grid=*reinterpret_cast<const uint8_t* const*>(tes+0x08);
    return grid&&*reinterpret_cast<const uint32_t*>(grid+0x0C)!=0;
}
}
float Clearance(Form* skip,Vec3 from,Vec3 to) {
    auto tes=*reinterpret_cast<uint8_t**>(0x11DEA10);
    if(!tes||!CellLoaded(tes))return -1;
    //0x931ed0 thiscall(actor, out): the actor's filter, its group in the high word keeps the ray off its own body
    uint32_t group=0;
    if(skip)reinterpret_cast<uint32_t*(__thiscall*)(Form*,uint32_t*)>(0x931ED0)(skip,&group);
    Vec3 direction=to-from;float length=Length(direction);
    if(!Normalize(direction))return 0;
    float travelled=0;
    //an actor in the way, or skip's own ragdoll, is not a wall: the ray starts again just past it
    for(unsigned attempt=0;attempt<4;++attempt) {
        PickData pick;
        //0x4a3c20 ctor, 0x4a3da0 and 0x4a3eb0 set the ends in world units (thiscall, one point, ret 4)
        reinterpret_cast<void(__thiscall*)(PickData*)>(0x4A3C20)(&pick);
        for(auto& key:pick.shapeKeys)key=-1;
        Vec3 start=from+direction*travelled;
        reinterpret_cast<void(__thiscall*)(PickData*,const Vec3*)>(0x4A3DA0)(&pick,&start);
        reinterpret_cast<void(__thiscall*)(PickData*,const Vec3*)>(0x4A3EB0)(&pick,&to);
        pick.filter=(group&0xFFFF0000)|kLineOfSightLayer;
        //0x458420 thiscall(tes, pick) ret 4 casts in the current interior or exterior world, returns the hit object
        void* hit=reinterpret_cast<void*(__thiscall*)(uint8_t*,PickData*)>(0x458420)(tes,&pick);
        if(pick.skipped)return -1;
        if(!pick.collidable)return length;
        if(!std::isfinite(pick.fraction))return -1;
        float at=travelled+(length-travelled)*Clamp(pick.fraction,0,1);
        //0x56f930 cdecl walks up from a scene object to the reference it belongs to
        auto owner=hit?reinterpret_cast<Form*(__cdecl*)(void*)>(0x56F930)(hit):nullptr;
        if(!owner||(owner->type!=kCharacter&&owner->type!=kCreature))return at;
        travelled=at+.5f;
        if(travelled>=length)return length;
    }
    return length;
}
}
