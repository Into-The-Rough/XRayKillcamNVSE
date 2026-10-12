#include "../src/Headshots.cpp"
#include "../src/ProjectileAmmo.cpp"
#include <cstdlib>
#include <cstring>
#include <thread>
void TestCinematic();
void TestShotTrace();

namespace {
unsigned checks=0,calls=0,shown=0,hidden=0;
uint32_t selected=0,selectedRegion=0,capturedRegion=0;
enum class Action {None,Clear,Mutate,Reenter};
Action action=Action::None;
bool projectileResult=true,clearInPredicate=false;
XRay::Engine::Form ammoForm{};
XRay::Engine::Form *expectedTarget=nullptr;
XRay::Engine::HitData *expectedData=nullptr;
XRay::Headshots::HitMe invoke=nullptr;
void Check(bool passed,const char* message) {
    ++checks;if(!passed){printf("FAIL: %s\n",message);std::exit(1);}
}
//the native hit moves these to their after values, as damage would
uint32_t lifeState=0,lifeAfter=0;float condition=100,conditionAfter=100;bool targetPresent=true;
uint32_t LifeStateStub(const XRay::Engine::Form*){return lifeState;}
int32_t LimbStub(XRay::Engine::Form*,int32_t location){return location==14?-1:25+location%7;}
float ValueStub(XRay::Engine::Form*,uint32_t){return condition;}
bool __fastcall IsProjectile(XRay::Engine::Form*,void*) {
    if(clearInPredicate)XRay::Headshots::Clear();
    return projectileResult;
}
void __fastcall PriorHook(XRay::Engine::Form* target,void*,XRay::Engine::HitData* data,char flag) {
    Check(target==expectedTarget&&data==expectedData&&flag==7,"prior hook receives ecx and both stack arguments unchanged");
    ++calls;lifeState=lifeAfter;condition=conditionAfter;
    auto current=action;action=Action::None;
    if(current==Action::Clear)XRay::Headshots::Clear();
    else if(current==Action::Mutate){data->location=0;data->impact={9,9,9};target->id=0xBADC0DE;ammoForm.id=0x8ECFF;}
    else if(current==Action::Reenter)invoke(target,data,7);
}
}
namespace XRay {
Settings settings;
void Log(const char*,...){}
uint64_t ShowAutomatic(const Headshots::Hit& hit,uint64_t now){selected=hit.target;selectedRegion=hit.region;++shown;return now+5000;}
void HideAutomaticHead(){selected=0;++hidden;}
//no killcam runs in the probe, so the reveal ends when its own time does
uint64_t HoldForKillcam(uint64_t,uint64_t until){return until;}
bool groinStub=false;unsigned groinCalls=0;
bool GroinHit(uint32_t,const Impact::Sample&){++groinCalls;return groinStub;}
void Clear(){Headshots::Clear();HideAutomaticHead();}
}
namespace XRay::Engine {
bool regionStub=true;
bool HasRegion(Form*,uint32_t){return regionStub;}
Impact::Sample CaptureImpact(Form*,Form* projectile,Vec3 point,bool capturePose,uint32_t region) {
    capturedRegion=region;
    Impact::Sample sample;sample.point=point;sample.direction={0,1,0};sample.pose=Transform::Identity();
    sample.ammo=ProjectileAmmoID(projectile);sample.bullet=Ammunition::Resolve(sample.ammo);
    sample.valid=true;sample.posed=capturePose;return sample;
}
}
int main() {
    using namespace XRay;
    using namespace XRay::Headshots;
    //a real x86 caller forwards two stack arguments, calls its owned e8 site, and ret 8
    auto code=static_cast<uint8_t*>(VirtualAlloc(nullptr,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(code!=nullptr,"allocate isolated call fixture");
    uint8_t caller[]={0xFF,0x74,0x24,0x08,0xFF,0x74,0x24,0x08,0xE8,0,0,0,0,0xC2,0x08,0x00};
    int32_t relative=static_cast<int32_t>(reinterpret_cast<uintptr_t>(PriorHook)-reinterpret_cast<uintptr_t>(code+13));
    memcpy(caller+9,&relative,4);memcpy(code,caller,sizeof caller);
    Check(s_hitCall.ReplaceCall(reinterpret_cast<uintptr_t>(code+8),HitHook),"install e8 hook over a preexisting plugin target");
    Check(s_hitCall.Original()==reinterpret_cast<uintptr_t>(PriorHook),"chain actual overwritten target");
    invoke=reinterpret_cast<HitMe>(code);s_lifeState=LifeStateStub;s_limbValue=LimbStub;s_actorValue=ValueStub;s_mainThread=GetCurrentThreadId();
    s_lookup=[](uint32_t)->Engine::Form*{return nullptr;};
    //the routing and threading checks below open on every hit, kills and cripples follow at the end
    settings.everyHit=true;

    void* vtable[0x228/4]{};vtable[0x224/4]=reinterpret_cast<void*>(IsProjectile);
    Engine::Form player{},target{};Engine::JIPProjectile projectileStorage{};
    auto& projectile=*reinterpret_cast<Engine::Form*>(&projectileStorage);
    ammoForm.type=Engine::kAmmo;ammoForm.id=0x8ED03;projectileStorage.ammo=&ammoForm;
    Check(!Engine::ProjectileAmmoID(&projectile),"JIP extension never read when provider is unavailable");
    Engine::s_jipAmmo=true;
    Check(!Engine::ProjectileAmmoID(&projectile),"zero projectile count gates the JIP ammo union");
    projectileStorage.numProjectiles=1;
    Check(Engine::ProjectileAmmoID(&projectile)==ammoForm.id,"JIP launch ammo identity copied from projectile");
    ammoForm.type=0;Check(!Engine::ProjectileAmmoID(&projectile),"non-ammo form rejected");ammoForm.type=Engine::kAmmo;
    projectileStorage.ammo=nullptr;Check(!Engine::ProjectileAmmoID(&projectile),"missing launch ammo rejected");projectileStorage.ammo=&ammoForm;
    player.id=Engine::kPlayerRefID;target.type=Engine::kCharacter;target.id=0x123456;
    projectile.vtable=vtable;
    //the native hitdata contract is a 0x64-byte record, health/critical/death fields vary below
    alignas(4) uint8_t storage[0x64]{};
    auto data=reinterpret_cast<Engine::HitData*>(storage);
    *data={&player,&target,&projectile,0,1};expectedTarget=&target;expectedData=data;
    data->impact={2,3,4};
    invoke(&target,data,7);
    auto hit=Take();Check(hit.target==target.id&&hit.location==1&&hit.count==1,"head hit accepted with zero damage and no fatal flag");
    Check(hit.impact.point.x==2&&hit.impact.point.y==3&&hit.impact.point.z==4&&hit.impact.posed,"native impact point and main-thread pose copied");
    Advance(hit,1000);Check(selected==target.id&&s_until==6000,"automatic reveal starts for five seconds");
    Advance({},5999);Check(selected==target.id,"reveal remains before deadline");
    invoke(&target,data,7);Advance(Take(),5999);
    Advance({},6000);Check(selected==target.id&&s_until==10999&&s_total==2,"same actor retriggers without cooldown");
    Advance({},10999);Check(selected==0&&!s_active,"reveal expires exactly at refreshed deadline");
    for(int location=-1;location<=15;++location) {
        data->location=location;invoke(&target,data,7);hit=Take();
        bool leg=location==7||location==10,arm=location==3||location==5;
        Check((hit.target!=0)==(location==0||location==1||location==2||location==13||leg||arm),"native torso, head1, head2, brain, arm and leg classifications only");
        Check(!hit.target||hit.region==(location==0?TorsoRegion:leg?LegsRegion:arm?ArmsRegion:HeadRegion),"torso, arm, leg and head hits carry their region");
    }
    //a torso hit opens the torso on every hit, like a head hit
    Headshots::Clear();data->location=0;
    invoke(&target,data,7);hit=Take();
    Check(hit.target==target.id&&hit.region==TorsoRegion&&hit.location==0&&hit.count==1&&capturedRegion==TorsoRegion,"torso hit triggers at once with the torso frame pose");
    Advance(hit,50000);Check(selected==target.id&&selectedRegion==TorsoRegion,"torso hit reaches the runtime as the torso region");
    data->location=1;invoke(&target,data,7);data->location=0;invoke(&target,data,7);hit=Take();
    Check(hit.region==TorsoRegion&&hit.count==1,"the latest hit decides the region and restarts the count");
    invoke(&target,data,7);invoke(&target,data,7);Check(Take().count==2,"torso hits on one target accumulate");
    settings.regions[TorsoRegion]=false;invoke(&target,data,7);Check(!Take().target,"config disables torso hits");settings.regions[TorsoRegion]=true;
    data->location=1;invoke(&target,data,7);data->location=13;invoke(&target,data,7);data->location=2;invoke(&target,data,7);
    Check(Take().count==3,"head, brain and second head parts on one target accumulate");
    //a leg hit opens the legs on every hit, with the side kept apart
    Headshots::Clear();data->location=7;
    invoke(&target,data,7);hit=Take();
    Check(hit.target==target.id&&hit.region==LegsRegion&&hit.location==7&&hit.count==1&&capturedRegion==LegsRegion,"left leg hit triggers at once with the pelvis frame pose");
    Advance(hit,60000);Check(selected==target.id&&selectedRegion==LegsRegion,"leg hit reaches the runtime as the leg region");
    invoke(&target,data,7);data->location=10;invoke(&target,data,7);hit=Take();
    Check(hit.region==LegsRegion&&hit.location==10&&hit.count==1,"a hit on the other leg restarts the count and carries its side");
    invoke(&target,data,7);data->location=0;invoke(&target,data,7);hit=Take();
    Check(hit.region==TorsoRegion&&hit.count==1,"a torso hit after a leg hit takes over");
    data->location=10;settings.regions[LegsRegion]=false;invoke(&target,data,7);Check(!Take().target,"config disables leg hits");settings.regions[LegsRegion]=true;
    //an arm hit opens the arms with the side kept apart, like the legs
    Headshots::Clear();data->location=5;
    invoke(&target,data,7);hit=Take();
    Check(hit.target==target.id&&hit.region==ArmsRegion&&hit.location==5&&hit.count==1&&capturedRegion==ArmsRegion,"right arm hit triggers at once with the chest frame pose");
    Advance(hit,70000);Check(selected==target.id&&selectedRegion==ArmsRegion,"arm hit reaches the runtime as the arm region");
    Check(Engine::LimbSide(5)==1&&Engine::LimbSide(3)==0&&Engine::LimbSide(10)==1&&Engine::LimbSide(7)==0,"right limbs carry side 1");
    invoke(&target,data,7);data->location=3;invoke(&target,data,7);hit=Take();
    Check(hit.region==ArmsRegion&&hit.location==3&&hit.count==1,"a hit on the other arm restarts the count");
    //a creature skeleton without the region's bones shows nothing for that hit
    Headshots::Clear();HideAutomaticHead();data->location=0;invoke(&target,data,7);hit=Take();Engine::regionStub=false;
    Advance(hit,80000);Check(selected==0&&!s_active,"a region missing from the target's skeleton shows nothing");Engine::regionStub=true;data->location=3;
    settings.regions[ArmsRegion]=false;invoke(&target,data,7);Check(!Take().target,"config disables arm hits");settings.regions[ArmsRegion]=true;
    Headshots::Clear();
    data->location=1;
    uint32_t fatalFlags=0x70;memcpy(storage+0x58,&fatalFlags,4);
    invoke(&target,data,7);Check(Take().target==target.id,"fatal/dismember/explode flags do not suppress trigger");
    player.id=0x99;invoke(&target,data,7);Check(!Take().target,"npc shooter rejected");player.id=Engine::kPlayerRefID;
    target.type=0x3A;invoke(&target,data,7);Check(!Take().target,"creature rejected");target.type=Engine::kCharacter;
    target.flags=Engine::kDeleted;invoke(&target,data,7);Check(!Take().target,"deleted victim rejected");target.flags=0;
    settings.automatic=false;invoke(&target,data,7);Check(!Take().target,"config disables automatic only");settings.automatic=true;
    projectileResult=false;invoke(&target,data,7);Check(!Take().target,"native nonprojectile predicate rejects melee/explosion source");projectileResult=true;
    data->projectile=nullptr;invoke(&target,data,7);Check(!Take().target,"missing projectile rejected");data->projectile=&projectile;
    data->target=nullptr;invoke(&target,data,7);Check(!Take().target,"mismatched hit victim rejected");data->target=&target;
    data->source=nullptr;invoke(&target,data,7);Check(!Take().target,"missing shooter rejected");data->source=&player;
    expectedData=nullptr;invoke(&target,nullptr,7);Check(!Take().target,"null hit forwarded safely");expectedData=data;

    action=Action::Mutate;uint32_t id=target.id;invoke(&target,data,7);hit=Take();
    Check(hit.target==id&&hit.location==1,"snapshot survives original modifying its hit buffer and victim");
    Check(hit.impact.point.x==2&&hit.impact.point.y==3&&hit.impact.point.z==4,"original hit mutation cannot move queued impact");
    Check(hit.impact.ammo==0x8ED03&&hit.impact.bullet.radius==Ammunition::Resolve(0x8ED03).radius,"ammo id and dimensions survive mutation in chained hit callback");
    target.id=id;data->location=1;
    action=Action::Clear;invoke(&target,data,7);Check(!Take().target,"load/off inside chained callback discards hit");
    clearInPredicate=true;invoke(&target,data,7);Check(!Take().target,"generation rejects hit after predicate reentry");clearInPredicate=false;
    action=Action::Reenter;invoke(&target,data,7);Check(Take().count==2,"nested hit chains once and both hits are counted");
    invoke(&target,data,7);Headshots::Clear();Check(!Take().target,"load/off discards pending hits");
    Advance({id,1,1},30000);Headshots::Clear();selected=0x654321;Advance({},40000);
    Check(selected==0x654321,"manual selection cancels automatic timeout");

    Headshots::Clear();
    constexpr unsigned iterations=4000;
    std::thread producer([&] {
        for(unsigned i=0;i<iterations;++i)invoke(&target,data,7);
    });
    uint32_t total=0;
    for(unsigned i=0;i<iterations;++i)total+=Take().count;
    producer.join();total+=Take().count;
    Check(total==iterations,"cross-thread consume preserves count with repeated identical target ids");
    std::thread worker([&]{invoke(&target,data,7);});worker.join();
    Check(!Take().impact.posed,"off-main impact does not traverse scene graph");
    Headshots::Clear();
    for(unsigned i=0;i<1000;++i)invoke(&target,data,7);
    Check(Take().count==1000,"burst hits coalesce without losing trigger count");

    //by default only kills and cripples open the X-ray, judged around the native hit
    s_lookup=[](uint32_t)->Engine::Form*{return targetPresent?reinterpret_cast<Engine::Form*>(expectedTarget):nullptr;};
    settings.everyHit=false;Headshots::Clear();data->location=1;
    lifeState=lifeAfter=0;condition=100;conditionAfter=60;
    invoke(&target,data,7);Check(!Take().target,"a hit that neither kills nor cripples opens nothing");
    condition=100;conditionAfter=100;lifeAfter=2;invoke(&target,data,7);hit=Take();
    Check(hit.target==target.id&&hit.cause==Kill&&hit.region==HeadRegion&&hit.location==1&&capturedRegion==HeadRegion,"a killing headshot opens the head");
    invoke(&target,data,7);Check(!Take().target,"a hit on a corpse opens nothing");
    lifeState=lifeAfter=0;data->location=7;condition=40;conditionAfter=0;
    invoke(&target,data,7);hit=Take();Check(hit.cause==Cripple&&hit.region==LegsRegion&&hit.location==7,"a crippling leg hit opens the leg");
    conditionAfter=-30;invoke(&target,data,7);Check(!Take().target,"a hit on a limb already crippled opens nothing");
    data->location=3;condition=40;conditionAfter=0;lifeAfter=1;invoke(&target,data,7);hit=Take();
    Check(hit.cause==Kill&&hit.region==ArmsRegion,"a hit that cripples and kills counts as the kill");
    lifeState=lifeAfter=0;condition=100;conditionAfter=100;
    settings.kills=false;lifeAfter=2;invoke(&target,data,7);Check(!Take().target,"Kills=0 ignores kills");settings.kills=true;lifeState=lifeAfter=0;
    settings.cripples=false;condition=40;conditionAfter=0;invoke(&target,data,7);Check(!Take().target,"Cripples=0 ignores cripples");settings.cripples=true;
    condition=conditionAfter=100;
    //a critical is decided while the hit data is filled, so its flag is read before the native hit on any thread
    uint32_t flags=0;auto setFlags=[&](uint32_t f){flags=f;memcpy(storage+0x58,&flags,4);};
    data->location=10;setFlags(Engine::kHitCritical);invoke(&target,data,7);hit=Take();
    Check(hit.cause==Critical&&hit.region==LegsRegion&&hit.location==10,"a critical that neither kills nor cripples opens the part it hit");
    lifeAfter=2;invoke(&target,data,7);Check(Take().cause==Kill,"a critical that kills counts as the kill");lifeState=lifeAfter=0;
    condition=40;conditionAfter=0;invoke(&target,data,7);Check(Take().cause==Cripple,"a critical that cripples counts as the cripple");condition=conditionAfter=100;
    settings.criticals=false;invoke(&target,data,7);Check(!Take().target,"Criticals=0 ignores criticals");settings.criticals=true;
    std::thread offCritical([&]{invoke(&target,data,7);});offCritical.join();
    hit=Take();Check(hit.cause==Critical&&!s_deferred.hit.target,"an off-main critical opens at once");
    setFlags(0x70);invoke(&target,data,7);Check(!Take().target,"other hit flags are not criticals");
    //a torso hit through the testes or penis opens on its own, after a kill and before a cripple or critical
    setFlags(0);data->location=0;groinStub=true;groinCalls=0;
    invoke(&target,data,7);hit=Take();Check(hit.cause==Groin&&hit.groin&&hit.region==TorsoRegion&&groinCalls==1,"a groin shot opens the torso");
    lifeAfter=2;invoke(&target,data,7);hit=Take();Check(hit.cause==Kill&&hit.groin&&groinCalls==2,"a groin shot that kills is the kill, still shown through the groin");lifeState=lifeAfter=0;
    condition=40;conditionAfter=0;setFlags(Engine::kHitCritical);invoke(&target,data,7);
    Check(Take().cause==Groin,"a groin shot that cripples or crits is the groin shot");condition=conditionAfter=100;setFlags(0);
    settings.groin=false;invoke(&target,data,7);Check(!Take().target,"Groin=0 ignores groin shots");
    setFlags(Engine::kHitCritical);invoke(&target,data,7);hit=Take();
    Check(hit.cause==Critical&&hit.groin,"with Groin=0 a critical through the groin is still shown through the groin");settings.groin=true;
    groinStub=false;invoke(&target,data,7);hit=Take();Check(hit.cause==Critical&&!hit.groin,"a torso critical elsewhere shows the whole torso");
    groinStub=true;setFlags(0);
    data->location=7;unsigned before=groinCalls;invoke(&target,data,7);Check(!Take().target&&groinCalls==before,"only torso hits are tested");
    data->location=0;std::thread offGroin([&]{invoke(&target,data,7);});offGroin.join();
    Check(groinCalls==before&&!Take().target,"and only on the main thread, where the pose is the hit's");
    groinStub=false;Headshots::Clear();
    //only a standing target counts, a companion knocked out or anyone down opens nothing however it is hit
    setFlags(Engine::kHitCritical);
    for(uint32_t down:{1u,2u,3u,5u,6u}) {
        lifeState=lifeAfter=down;condition=40;conditionAfter=0;invoke(&target,data,7);
        Check(!Take().target,"a target already dead or down opens nothing, critical or crippled");
    }
    settings.everyHit=true;lifeState=lifeAfter=6;invoke(&target,data,7);Check(!Take().target,"not even on every hit");settings.everyHit=false;
    std::thread offDown([&]{invoke(&target,data,7);});offDown.join();Check(!Take().target&&!s_deferred.hit.target,"nor off the main thread");
    setFlags(0);lifeState=0;lifeAfter=6;condition=conditionAfter=100;invoke(&target,data,7);
    Check(Take().cause==Kill,"the hit that knocks an essential down is their kill");lifeState=lifeAfter=0;
    Advance(Take(),80000);
    data->location=0;lifeAfter=2;invoke(&target,data,7);Advance(Take(),80001);
    Check(selected==target.id&&selectedRegion==TorsoRegion,"a kill reaches the runtime");lifeState=lifeAfter=0;
    //off the main thread the engine applies the hit later, so only the kill is read, from the main thread
    Headshots::Clear();
    std::thread late([&]{invoke(&target,data,7);});late.join();
    Check(!Take().target&&s_deferred.hit.target==target.id,"an off-main hit waits for its kill");
    Judge(s_deferred.hit.time+100);Check(!Take().target&&s_deferred.hit.target,"still alive inside the window keeps waiting");
    lifeState=2;Judge(s_deferred.hit.time+200);hit=Take();
    Check(hit.target==target.id&&hit.cause==Kill&&!s_deferred.hit.target,"the target reading dead turns it into a kill");lifeState=0;
    std::thread lapse([&]{invoke(&target,data,7);});lapse.join();
    Judge(s_deferred.hit.time+kDeferredWindow);Check(!Take().target&&!s_deferred.hit.target,"an off-main hit that never kills lapses");
    std::thread gone([&]{invoke(&target,data,7);});gone.join();
    targetPresent=false;lifeState=2;Judge(s_deferred.hit.time+1);Check(!Take().target&&s_deferred.hit.target,"an unresolved target is not read");
    Headshots::Clear();Check(!s_deferred.hit.target,"load clears a waiting hit");targetPresent=true;lifeState=0;
    VirtualFree(code,0,MEM_RELEASE);
    printf("PASS: %u checks; %u chained hit calls; %u reveals\n",checks,calls,shown);
    TestCinematic();
    TestShotTrace();
    return 0;
}
