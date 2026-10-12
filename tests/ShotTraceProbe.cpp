#include "../src/ShotTrace.cpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>

namespace XRay::Engine {
Trace::View CaptureShotView(){return {{100,200,300},{0,1,0},true};}
}
namespace {
using namespace XRay;
std::atomic<unsigned> checks{0},origins{0},directions{0},launches{0};
using Launch=int(__thiscall*)(Engine::Form*);
using Setter=int(__thiscall*)(void*,const Vec3*);
Launch launch;
Setter origin,setDisplacement;
enum class Action {Normal,Nested,ClearOrigin,ClearDirection,WrongPick,Invalid,Mutate};
thread_local Action action=Action::Normal;
void Check(bool ok,const char* message){++checks;if(!ok){printf("FAIL: %s\n",message);exit(1);}}
int __fastcall OriginalOrigin(void* pick,void*,const Vec3* value) {
    ++origins;Check(pick&&value&&value->y==2,"origin bridge forwards ecx and vector unchanged");
    if(action==Action::ClearOrigin)Trace::Clear();
    return 71;
}
int __fastcall OriginalDisplacement(void* pick,void*,const Vec3* value) {
    ++directions;Check(pick&&value&&value->x==0,"displacement chains its own prior target");
    if(action==Action::ClearDirection)Trace::Clear();
    return 93;
}
int __fastcall OriginalLaunch(Engine::Form* projectile,void*) {
    ++launches;
    auto requested=action;
    Vec3 start{float(projectile->id),2,3},delta{0,20,0};
    if(action==Action::Invalid)delta.y=std::numeric_limits<float>::quiet_NaN();
    Check(origin(projectile,&start)==71,"origin preserves prior return value");
    if(action==Action::Nested) {
        Engine::Form nested{};nested.id=0x223;
        action=Action::Normal;Check(launch(&nested)==901,"nested launch preserves return value");action=requested;
        Trace::Ray ray;Check(Trace::Read(nested.id,reinterpret_cast<uintptr_t>(&nested),GetTickCount64(),ray)&&ray.origin.x==float(nested.id),"nested ray owns its own projectile identity");
    }
    void* pick=action==Action::WrongPick?&start:static_cast<void*>(projectile);
    Check(setDisplacement(pick,&delta)==93,"displacement preserves prior return value");
    start={999,999,999};delta={999,999,999};
    if(action==Action::Mutate)projectile->id=0xBAD;
    return 901;
}
uint8_t* Caller(uint8_t* code,uintptr_t target,bool argument) {
    unsigned offset=0;
    if(argument){const uint8_t push[]={0xFF,0x74,0x24,0x04};memcpy(code,push,4);offset=4;}
    code[offset]=0xE8;int32_t rel=static_cast<int32_t>(target-reinterpret_cast<uintptr_t>(code+offset+5));memcpy(code+offset+1,&rel,4);
    code[offset+5]=argument?0xC2:0xC3;
    if(argument){code[offset+6]=4;code[offset+7]=0;}
    return code+offset;
}
}
void TestShotTrace() {
    using namespace XRay;
    Trace::s_mainThread=GetCurrentThreadId();
    auto code=static_cast<uint8_t*>(VirtualAlloc(nullptr,96,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(code!=nullptr,"allocate isolated observer bridge");
    auto a=Caller(code,reinterpret_cast<uintptr_t>(OriginalLaunch),false);
    auto b=Caller(code+32,reinterpret_cast<uintptr_t>(OriginalOrigin),true);
    auto c=Caller(code+64,reinterpret_cast<uintptr_t>(OriginalDisplacement),true);
    Check(Trace::s_launch.ReplaceCall(reinterpret_cast<uintptr_t>(a),Trace::LaunchHook),"chain launch e8");
    Check(Trace::s_origin.ReplaceCall(reinterpret_cast<uintptr_t>(b),Trace::OriginHook),"chain origin e8");
    Check(Trace::s_displacement.ReplaceCall(reinterpret_cast<uintptr_t>(c),Trace::DisplacementHook),"chain displacement e8");
    launch=reinterpret_cast<Launch>(code);origin=reinterpret_cast<Setter>(code+32);setDisplacement=reinterpret_cast<Setter>(code+64);
    Engine::Form projectile{};projectile.id=0x123;Trace::Ray ray;
    for(auto mode:{Action::Normal,Action::Nested,Action::Mutate,Action::ClearOrigin,Action::ClearDirection,Action::WrongPick,Action::Invalid}) {
        Trace::Clear();action=mode;projectile.id=0x123;auto identity=reinterpret_cast<uintptr_t>(&projectile);
        Check(launch(&projectile)==901,"launch preserves chained return");
        bool expected=mode==Action::Normal||mode==Action::Nested||mode==Action::Mutate;
        Check(Trace::Read(0x123,identity,GetTickCount64(),ray)==expected,"only complete uncancelled matching-pick rays are published");
        if(expected) {
            Check(ray.view.valid&&ray.view.origin.x==100&&ray.view.forward.y==1,"launch camera snapshot belongs to the copied ray");
            Check(ray.origin.x==float(0x123)&&ray.origin.y==2&&ray.direction.y==1,"copied ray survives original buffer and projectile mutation");
            Check(!Trace::Read(0x124,identity,GetTickCount64(),ray),"different projectile cannot borrow ray");
            Check(!Trace::Read(0x123,identity+4,GetTickCount64(),ray),"recycled id with another identity rejected");
            Check(!Trace::Read(0x123,identity,GetTickCount64()+3000,ray),"old trace expires");
        }
    }
    Trace::Clear();action=Action::Normal;projectile.id=0x123;
    launch(&projectile);action=Action::Invalid;launch(&projectile);
    Check(!Trace::Read(projectile.id,reinterpret_cast<uintptr_t>(&projectile),GetTickCount64(),ray),"failed recast cannot retain an old ray for the same projectile");action=Action::Normal;
    Vec3 point{0,2,0},delta{0,20,0};origin(&projectile,&point);setDisplacement(&projectile,&delta);
    Check(!Trace::Read(projectile.id,reinterpret_cast<uintptr_t>(&projectile),GetTickCount64(),ray),"setters outside launch context cannot create a ray");
    auto worker=[](uint32_t id) {
        Engine::Form p{};p.id=id;
        for(unsigned i=0;i<1000;++i) {
            Check(launch(&p)==901,"parallel launch bridge");Trace::Ray r;
            Check(Trace::Read(id,reinterpret_cast<uintptr_t>(&p),GetTickCount64(),r)&&r.origin.x==float(id),"thread-local scopes and cache keep concurrent projectile rays separate");
            Check(!r.view.valid,"worker launches never walk the main camera");
        }
    };
    std::thread one(worker,0x310),two(worker,0x320);one.join();two.join();
    Trace::Clear();VirtualFree(code,0,MEM_RELEASE);
    printf("PASS: %u shot-ray checks; %u launches, %u origin and %u displacement chains\n",checks.load(),launches.load(),origins.load(),directions.load());
}
