#include "ShotTrace.h"
#include "Engine.h"
#include "Hooks.h"
#include "Runtime.h"

namespace XRay::Trace {
static CallDetour s_launch,s_origin,s_displacement;
static SRWLOCK s_lock=SRWLOCK_INIT;
constexpr unsigned kCapacity=64;
constexpr uint64_t kLifetime=2000;
struct Record {Ray ray;uint32_t id=0;uintptr_t identity=0;uint64_t born=0;};
struct Observation {Record record;uint32_t epoch=0;uintptr_t pick=0;bool origin=false,complete=false;};
static Record s_records[kCapacity];
static uint32_t s_epoch=0;
static thread_local Observation* s_current=nullptr;
static bool s_attempted=false;
static DWORD s_mainThread=0;

void Clear() {
    AcquireSRWLockExclusive(&s_lock);++s_epoch;
    for(auto& record:s_records)record={};
    ReleaseSRWLockExclusive(&s_lock);
}
bool Read(uint32_t id,uintptr_t identity,uint64_t now,Ray& out) {
    AcquireSRWLockShared(&s_lock);const auto& record=s_records[id%kCapacity];
    bool valid=id&&record.id==id&&record.identity==identity&&now>=record.born&&now-record.born<=kLifetime;
    if(valid)out=record.ray;
    ReleaseSRWLockShared(&s_lock);return valid;
}
static int __fastcall OriginHook(void* pick,void*,const Vec3* origin) {
    if(s_current&&!s_current->origin&&origin&&Finite(*origin)) {
        s_current->record.ray.origin=*origin;s_current->pick=reinterpret_cast<uintptr_t>(pick);s_current->origin=true;
    }
    return reinterpret_cast<int(__thiscall*)(void*,const Vec3*)>(s_origin.Original())(pick,origin);
}
static int __fastcall DisplacementHook(void* pick,void*,const Vec3* displacement) {
    auto current=s_current;
    if(current&&current->origin&&!current->complete&&current->pick==reinterpret_cast<uintptr_t>(pick)&&displacement) {
        Vec3 direction=*displacement;
        if(Normalize(direction)) {
            current->record.ray.direction=direction;current->record.born=GetTickCount64();current->complete=true;
            AcquireSRWLockExclusive(&s_lock);
            if(current->record.id&&current->epoch==s_epoch)s_records[current->record.id%kCapacity]=current->record;
            ReleaseSRWLockExclusive(&s_lock);
        }
    }
    return reinterpret_cast<int(__thiscall*)(void*,const Vec3*)>(s_displacement.Original())(pick,displacement);
}
static int __fastcall LaunchHook(Engine::Form* projectile,void*) {
    Observation observation;
    observation.record.id=projectile?projectile->id:0;observation.record.identity=reinterpret_cast<uintptr_t>(projectile);
    if(GetCurrentThreadId()==s_mainThread)observation.record.ray.view=Engine::CaptureShotView();
    AcquireSRWLockExclusive(&s_lock);observation.epoch=s_epoch;
    auto& previous=s_records[observation.record.id%kCapacity];
    if(previous.id==observation.record.id&&previous.identity==observation.record.identity)previous={};
    ReleaseSRWLockExclusive(&s_lock);
    struct Scope {Observation* previous;~Scope(){s_current=previous;}} scope{s_current};
    s_current=&observation;
    //only copied identity and stack-owned observation survive the chained launch
    return reinterpret_cast<int(__thiscall*)(Engine::Form*)>(s_launch.Original())(projectile);
}
bool InitHooks() {
    if(s_attempted)return s_launch.Original()!=0;
    s_attempted=true;
    s_mainThread=GetCurrentThreadId();
    //ray setters are thiscall(pick, vec3*) with ret 4, launch is thiscall(projectile) with a plain ret
    //install the scope last: stranded setters only chain, with no active observation
    if(!s_origin.ReplaceCall(0x9C0AF4,OriginHook)||!s_displacement.ReplaceCall(0x9C0B06,DisplacementHook)
        ||!s_launch.ReplaceCall(0x9BDE64,LaunchHook))return false;
    Log("shot ray observers chained: launch=%08X origin=%08X displacement=%08X",static_cast<unsigned>(s_launch.Original()),
        static_cast<unsigned>(s_origin.Original()),static_cast<unsigned>(s_displacement.Original()));
    return true;
}
}
