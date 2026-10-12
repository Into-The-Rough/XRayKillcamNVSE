#include "Headshots.h"
#include "Engine.h"
#include "Hooks.h"
#include "Runtime.h"
#include <cstdio>
#include <cstring>

namespace XRay::Headshots {
using HitMe=void(__thiscall*)(Engine::Form*,Engine::HitData*,char);
using Notify=bool(__cdecl*)(const char*,uint32_t,const char*,const char*,float,uint8_t);
static auto s_notify=reinterpret_cast<Notify>(0x7052F0);
static auto s_lifeState=Engine::LifeState;
static auto s_limbValue=Engine::LimbActorValue;
static auto s_actorValue=Engine::ActorValue;
static auto s_lookup=Engine::Lookup;
static auto s_groin=GroinHit;
//what each player projectile hit on a character did, written to the log from the main thread
enum Verdict : uint8_t {Opened,Disabled,NoAnatomy,NoTrigger,JudgedLater,Down};
struct Note {uint32_t target;int32_t location;uint32_t lifeState;uint32_t region;uint8_t verdict,cause;bool groin;};
static const char* const kKinds[kRegions]={"headshot","torso hit","leg hit","arm hit"};
static const char* const kCauses[]={"","kill","cripple","critical","groin","every hit"};
//a hit off the main thread is applied later by a native task, so its kill is read from the main thread for a short while
constexpr uint64_t kDeferredWindow=500;
struct Deferred {Hit hit;uint64_t deadline=0;};
static Deferred s_deferred;
static Note s_notes[16];static unsigned s_noteCount=0,s_notesDropped=0;
static CallDetour s_hitCall;
static SRWLOCK s_lock=SRWLOCK_INIT;
static Hit s_pending;
static DWORD s_mainThread=0;
static uint32_t s_epoch=0,s_total=0;
static uint64_t s_until=0;
static bool s_installed=false,s_active=false;

static uint32_t Epoch() {
    AcquireSRWLockShared(&s_lock);uint32_t epoch=s_epoch;ReleaseSRWLockShared(&s_lock);
    return epoch;
}
static Hit Take() {
    AcquireSRWLockExclusive(&s_lock);Hit hit=s_pending;s_pending={};ReleaseSRWLockExclusive(&s_lock);
    return hit;
}
static void LogNotes() {
    Note notes[16];unsigned count,dropped;
    AcquireSRWLockExclusive(&s_lock);count=s_noteCount;dropped=s_notesDropped;memcpy(notes,s_notes,count*sizeof(Note));s_noteCount=s_notesDropped=0;ReleaseSRWLockExclusive(&s_lock);
    for(unsigned i=0;i<count;++i) {
        const auto& n=notes[i];const char* region=RegionName(n.region);
        switch(n.verdict) {
        case Opened:Log("player hit %08X bodyPart=%d lifeState=%u: %s X-ray, %s%s",n.target,n.location,n.lifeState,region,kCauses[n.cause],
            n.groin&&n.cause!=Groin?", through the groin":"");break;
        case Disabled:Log("player hit %08X bodyPart=%d lifeState=%u: ignored, %s X-rays disabled in config",n.target,n.location,n.lifeState,region);break;
        case NoAnatomy:Log("player hit %08X bodyPart=%d lifeState=%u: ignored, no anatomy for this body part",n.target,n.location,n.lifeState);break;
        case Down:Log("player hit %08X bodyPart=%d lifeState=%u: ignored, the target was already dead or down",n.target,n.location,n.lifeState);break;
        case NoTrigger:Log("player hit %08X bodyPart=%d lifeState=%u: no X-ray, the hit did not kill, cripple or crit",n.target,n.location,n.lifeState);break;
        default:Log("player hit %08X bodyPart=%d lifeState=%u: off the main thread, kill read for %.1fs",n.target,n.location,n.lifeState,kDeferredWindow*.001f);break;
        }
    }
    if(dropped)Log("%u more player hits this frame not listed",dropped);
}
static void Remember(uint32_t target,int32_t location,uint32_t lifeState,uint32_t region,uint8_t verdict,uint8_t cause,bool groin=false) {
    AcquireSRWLockExclusive(&s_lock);
    if(s_noteCount<16)s_notes[s_noteCount++]={target,location,lifeState,region,verdict,cause,groin};else ++s_notesDropped;
    ReleaseSRWLockExclusive(&s_lock);
}
static void Publish(const Hit& hit,uint32_t epoch) {
    AcquireSRWLockExclusive(&s_lock);
    auto& slot=s_pending;
    if(epoch==s_epoch) {
        //the latest hit decides the region, hits on another target, region or limb start a new count
        if(slot.target!=hit.target||slot.region!=hit.region||Engine::LimbSide(slot.location)!=Engine::LimbSide(hit.location))slot={};
        uint32_t count=slot.count+1;slot=hit;slot.count=count;
    }
    ReleaseSRWLockExclusive(&s_lock);
}
static void __fastcall HitHook(Engine::Form* target,void*,Engine::HitData* data,char flag) {
    auto original=reinterpret_cast<HitMe>(s_hitCall.Original());
    uint32_t epoch=Epoch();
    auto kind=Engine::KindOf(target);
    if(!settings.automatic||!target||!data||data->target!=target||kind==Engine::Kind::None||(kind==Engine::Kind::FeralGhoul&&!settings.feralGhouls)
        ||(kind==Engine::Kind::SuperMutant&&!settings.superMutants)
        ||!data->source||data->source->id!=Engine::kPlayerRefID||!data->projectile) {
        original(target,data,flag);return;
    }
    uint32_t id=target->id;
    int32_t location=data->location;
    auto projectile=data->projectile;
    Vec3 point=data->impact;
    bool critical=(data->flags&Engine::kHitCritical)!=0;
    uint32_t region=Engine::PartRegion(location);
    bool open=region<kRegions&&settings.regions[region];
    //actor::hitme uses this same native predicate for hitdata+0x08 at 0x89c88b
    bool isProjectile=reinterpret_cast<bool(__thiscall*)(Engine::Form*)>(projectile->vtable[0x224/4])(projectile);
    bool main=GetCurrentThreadId()==s_mainThread;
    if(!isProjectile||!id){original(target,data,flag);return;}
    if(!open) {
        Remember(id,location,s_lifeState(target),region,region<kRegions?Disabled:NoAnatomy,NoCause);
        original(target,data,flag);return;
    }
    //only a standing target counts: dying, dead, unconscious, essential down and the rest are past it
    uint32_t before=s_lifeState(target);
    if(before!=Engine::kAlive) {
        Remember(id,location,before,region,Down,NoCause);
        original(target,data,flag);return;
    }
    //the engine's own tests, read before and after the native hit: alive before and dead after is a kill,
    //the hit limb's condition above zero before and at or below zero after is a cripple (0x89bdd8)
    int32_t limb=main?s_limbValue(target,location):-1;
    float condition=limb>=0?s_actorValue(target,uint32_t(limb)):0;
    Hit hit;hit.target=id;hit.location=location;hit.region=region;hit.time=GetTickCount64();
    if(epoch==Epoch()) {
        Trace::Ray ray;
        Trace::Read(projectile->id,reinterpret_cast<uintptr_t>(projectile),hit.time,ray);
        //the pose is copied before native hit processing, which may start the ragdoll
        hit.impact=Engine::CaptureImpact(target,projectile,point,main,region);hit.launchView=ray.view;
    }
    original(target,data,flag);
    uint32_t after=s_lifeState(target);
    hit.groin=main&&region==TorsoRegion&&kind==Engine::Kind::Human&&s_groin(id,hit.impact);
    if(settings.everyHit)hit.cause=EveryHit;
    else if(!main&&!(settings.criticals&&critical)) {
        if(settings.kills) {
            AcquireSRWLockExclusive(&s_lock);
            if(epoch==s_epoch)s_deferred={hit,hit.time+kDeferredWindow};
            ReleaseSRWLockExclusive(&s_lock);
            Remember(id,location,after,region,JudgedLater,NoCause);
        }else Remember(id,location,after,region,NoTrigger,NoCause);
        return;
    }
    else if(main&&settings.kills&&Engine::IsDeadState(after))hit.cause=Kill;
    else if(settings.groin&&hit.groin)hit.cause=Groin;
    else if(main&&settings.cripples&&limb>=0&&condition>0&&s_actorValue(target,uint32_t(limb))<=0)hit.cause=Cripple;
    else if(settings.criticals&&critical)hit.cause=Critical;
    if(!hit.cause){Remember(id,location,after,region,NoTrigger,NoCause);return;}
    Remember(id,location,after,region,Opened,hit.cause,hit.groin);
    //a load or off during the native hit discards it
    Publish(hit,epoch);
}
//a deferred hit becomes a kill once its target reads dead on the main thread, or lapses
static void Judge(uint64_t now) {
    AcquireSRWLockExclusive(&s_lock);Deferred d=s_deferred;uint32_t epoch=s_epoch;ReleaseSRWLockExclusive(&s_lock);
    if(!d.hit.target)return;
    auto target=s_lookup(d.hit.target);
    bool dead=Engine::Supported(target)&&Engine::IsDeadState(s_lifeState(target));
    if(!dead&&now<d.deadline)return;
    AcquireSRWLockExclusive(&s_lock);
    if(s_deferred.hit.target==d.hit.target&&s_deferred.hit.time==d.hit.time)s_deferred={};
    ReleaseSRWLockExclusive(&s_lock);
    if(dead){d.hit.cause=Kill;Publish(d.hit,epoch);}
}
uint32_t PendingTarget(){AcquireSRWLockShared(&s_lock);uint32_t target=s_pending.target;ReleaseSRWLockShared(&s_lock);return target;}
void Clear() {
    AcquireSRWLockExclusive(&s_lock);++s_epoch;s_pending={};s_deferred={};s_noteCount=s_notesDropped=0;ReleaseSRWLockExclusive(&s_lock);
    s_active=false;s_until=0;s_total=0;
}
static void Advance(Hit hit,uint64_t now) {
    //a creature skeleton without a region's bones, a behemoth's upper spine, has no anatomy to show there
    if(hit.target&&!Engine::HasRegion(s_lookup(hit.target),hit.region)) {
        Log("automatic X-ray skipped: %08X has no %s bones",hit.target,RegionName(hit.region));hit.target=0;
    }
    if(hit.target) {
        s_total+=hit.count;s_active=true;
        s_until=ShowAutomatic(hit,now);
        const char* kind=kKinds[hit.region<kRegions?hit.region:HeadRegion];
        Log("automatic %s %u: target=%08X bodyPart=%d cause=%s hits=%u remaining=%.1fs",kind,s_total,hit.target,hit.location,
            kCauses[hit.cause<=EveryHit?hit.cause:NoCause],hit.count,float(s_until-now)*.001f);
        char text[80];sprintf_s(text,"X-Ray %s %u",kind,s_total);
        //queueuimessage copies text into its hud record, true brings this message forward
        s_notify(text,0,nullptr,nullptr,1.0f,1);
    }else if(s_active) {
        s_until=HoldForKillcam(now,s_until);
        if(now>=s_until){s_active=false;HideAutomaticHead();}
    }
}
void Update() {
    if(!s_installed)return;
    uint64_t now=GetTickCount64();
    LogNotes();
    Judge(now);
    Hit hit=Take();
    if(hit.target||s_active)Advance(hit,now);
}
static const char* Triggers() {
    static char text[64];text[0]=0;
    const char* names[]={settings.kills?"kills":nullptr,settings.cripples?"cripples":nullptr,settings.criticals?"criticals":nullptr,settings.groin?"groin shots":nullptr};
    for(auto n:names)if(n){if(text[0])strcat_s(text,", ");strcat_s(text,n);}
    return text[0]?text:"no trigger";
}
bool InitHooks() {
    if(s_installed)return true;
    if(!settings.automatic){Log("automatic headshots disabled in config");return true;}
    s_mainThread=GetCurrentThreadId();
    //combathit -> hitme: e8 23 00 00 00, ecx=victim, stack=hitdata/char, callee ret 8
    if(!s_hitCall.ReplaceCall(0x89A738,HitHook))return false;
    s_installed=true;
    Log("automatic X-rays enabled: %.1fs or as long as their killcam (%.1fs timer, impact at %.2f speed), full-opacity cutaway; %s; head on, torso %s, legs %s, arms %s; feral ghouls %s, super mutants %s; blood spray %s; gore sounds %s; hit hook chained to %08X",settings.revealSeconds,
        settings.cameraSeconds,settings.impactSpeed,settings.everyHit?"every hit":Triggers(),
        settings.regions[TorsoRegion]?"on":"off",settings.regions[LegsRegion]?"on":"off",settings.regions[ArmsRegion]?"on":"off",settings.feralGhouls?"on":"off",settings.superMutants?"on":"off",settings.bloodSpray?"on":"off",settings.sounds?"on":"off",
        static_cast<unsigned>(s_hitCall.Original()));
    return true;
}
}
