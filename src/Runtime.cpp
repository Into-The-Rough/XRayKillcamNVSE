#include "Runtime.h"
#include "Engine.h"
#include "Hooks.h"
#include "Headshots.h"
#include "Cinematic.h"
#include "ShotTrace.h"
#include "ShotAudit.h"
#include "BodySkin.h"
#include "Groin.h"
#include "CloseUp.h"
#include "GoreSound.h"
#include "Damage.h"
#include <atomic>
#include <memory>
#include <cstring>

namespace XRay {
Settings settings;
static Model s_models[kRegions];
//the torso without the external genitals, for women and creatures
static Model s_plainTorso;
static std::atomic<uint32_t> s_region{HeadRegion},s_side{0};
static CallDetour s_finish;
static std::atomic<uint32_t> s_target{0},s_generation{0};
static std::atomic<unsigned> s_mode{2};
static std::atomic<bool> s_automatic{false};
//a shot through the genitals reveals only the skin over them
static std::atomic<bool> s_groinFocus{false};
static std::atomic<bool> s_discardDamage{false};
//the wounds each actor's X-rays planned, per body part, for this session: render thread only, the oldest used dropped first
struct Remembered {uint32_t target=0,region=0;uint64_t used=0;std::vector<Damage::Kept> wounds;};
constexpr unsigned kRememberedParts=64;
static std::vector<Remembered> s_remembered;
//whose wounds each region's scene holds, and the scene's wound count when they were last copied
static uint32_t s_owner[kRegions]{},s_keptRevision[kRegions]{};
//an actor's part brought back with its earlier wounds, logged from the main loop: target, region and count
static std::atomic<uint64_t> s_restoredNote{0};
static void ForgetOwners(){for(auto& owner:s_owner)owner=0;}
static Remembered* Recall(uint32_t target,uint32_t region) {
    for(auto& r:s_remembered)if(r.target==target&&r.region==region)return &r;
    return nullptr;
}
static Remembered& Keep(uint32_t target,uint32_t region) {
    if(auto r=Recall(target,region))return *r;
    if(s_remembered.size()>=kRememberedParts) {
        size_t oldest=0;for(size_t i=1;i<s_remembered.size();++i)if(s_remembered[i].used<s_remembered[oldest].used)oldest=i;
        s_remembered.erase(s_remembered.begin()+ptrdiff_t(oldest));
    }
    s_remembered.push_back({target,region,0,{}});return s_remembered.back();
}

static SRWLOCK s_reportLock=SRWLOCK_INIT;
static Graphics::DamageReport s_report;
static Impact::Playback s_impact;
static std::atomic<int> s_impactStatus{0};
static std::atomic<uint64_t> s_damageEvent{0};
static uint64_t s_reportedDamage=0;
static int s_reportedImpact=0;
static std::atomic_flag s_rendering=ATOMIC_FLAG_INIT;
static std::atomic<int> s_status{0};
static std::atomic<HRESULT> s_hresult{S_OK};
static std::atomic<float> s_fitStrength{1};
static std::atomic<uint64_t> s_captureFailure{0};
static bool s_failed=false,s_installed=false,s_hasResources=false;
static Engine::Renderer* s_resetOwner=nullptr;
//the head frame carries the neck skin, so it lives on the heap rather than in the image
static std::unique_ptr<HeadFrame> s_frame;
static int s_reported=0;
//a cue whose folder had nothing to play, plus one, reported once from the main loop
static std::atomic<uint32_t> s_silentCue{0};static uint32_t s_reportedSilent=0;
//the cues sounded since the main loop last logged them, one bit per kind and a count
static std::atomic<uint32_t> s_soundedKinds{0},s_soundedCount{0};
static uint64_t s_reportedCapture=0;
//under the report lock: the last shot whose blood landed, and the shot whose blood still flies, in case the X-ray closes first
static Graphics::BloodReport s_bloodLanded,s_bloodFlying;static std::atomic<bool> s_bloodReady{false};static uint64_t s_bloodLogged=0;
static void LogBlood(const Graphics::BloodReport& b,bool cut) {
    char seen[256],camera[256];
    if(!b.counted)strcpy_s(seen,"blood pixels not measured, the device has no occlusion queries");
    else if(!b.measured)strcpy_s(seen,"no frame measured");
    else sprintf_s(seen,"at most %u blood pixels outside the window and %u inside (%u frames measured, overlaps counted)",b.outside,b.inside,b.measured);
    if(b.distance<0)strcpy_s(camera,"camera not measured");
    else sprintf_s(camera,"camera %.0f units from the entry, %.0fdeg off the bullet's line (0 looks along it), entry drops about %.1f px",b.distance,b.angle,b.dropPixels);
    Log("blood %u: jets entry/exit/burst/mist=%u/%u/%u/%u droplets=%u/%u/%u/%u, flew %u frames%s; %s; %s",static_cast<unsigned>(b.sequence),
        b.jets[0],b.jets[1],b.jets[2],b.jets[3],b.droplets[0],b.droplets[1],b.droplets[2],b.droplets[3],b.frames,cut?" until the X-ray closed":"",seen,camera);
}

uint32_t ShownTarget(){return s_automatic.load()?s_target.load():0;}
void HideAutomaticHead(){CloseUp::Clear();s_groinFocus=false;s_discardDamage=true;s_damageEvent=0;s_reportedDamage=0;s_target=0;s_automatic=false;s_impact.Clear();Audit::Select(++s_generation);s_status=0;s_reported=0;s_reportedCapture=0;s_impactStatus=0;s_reportedImpact=0;}
void Clear(){Trace::Clear();Headshots::Clear();Cinematic::Clear();HideAutomaticHead();s_remembered.clear();ForgetOwners();}
static void SelectTarget(uint32_t refID) {
    s_target=refID;Audit::Select(++s_generation);s_status=0;s_reported=0;s_reportedCapture=0;
}
uint64_t ShowAutomatic(const Headshots::Hit& hit,uint64_t now) {
    auto until=s_impact.Start(hit.target,hit.impact,now,Engine::SimulationTime(),settings.revealSeconds);s_impactStatus=0;s_reportedImpact=0;
    s_region=hit.region<kRegions?hit.region:HeadRegion;
    s_side=Engine::LimbSide(hit.location);s_groinFocus=hit.groin;
    float severity=hit.cause==Headshots::Kill?settings.killSeverity:1;
    Graphics::SetShotForce(severity,settings.bloodAmount);
    s_automatic=true;s_mode=2;SelectTarget(hit.target);
    //kills follow the game's kill cam option, cripples, criticals and groin shots get their killcam whatever that says
    Cinematic::Queue(hit.target,hit.location,hit.cause==Headshots::Cripple||hit.cause==Headshots::Critical||hit.cause==Headshots::Groin);
    //the close-up starts on the contact and moves to the mapped wound once the renderer has it
    if(hit.impact.posed) {
        Vec3 contact=hit.impact.pose.Unapply(hit.impact.point),along=hit.impact.pose.Unrotate(hit.impact.direction);
        if(Normalize(along))CloseUp::Track(hit.target,s_region.load(),contact,contact+along*8.f);
    }
    Log("impact: point=(%.2f,%.2f,%.2f) direction=(%.3f,%.3f,%.3f) valid=%u poseAtHit=%u",
        hit.impact.point.x,hit.impact.point.y,hit.impact.point.z,hit.impact.direction.x,hit.impact.direction.y,hit.impact.direction.z,
        hit.impact.valid?1u:0u,hit.impact.posed?1u:0u);
    const auto& bullet=hit.impact.bullet;
    Log("bullet: ammo=%08X profile=%s diameter=%.3f length=%.3f channel=%.3f severity=%.2f blood=%.2f",
        hit.impact.ammo,bullet.known?"standard":"generic",bullet.radius*2,bullet.length,bullet.Section()*2,severity,settings.bloodAmount);
    const auto& sample=hit.impact;
    Log("trajectory source=%s rayOrigin=(%.2f,%.2f,%.2f) headingChange=%.2fdeg",
        sample.traced?"observed hitscan":"projectile vector",sample.rayOrigin.x,sample.rayOrigin.y,sample.rayOrigin.z,sample.headingChange);
    const auto& view=hit.launchView;
    Vec3 aim=sample.point-view.origin;
    float forward=Dot(aim,view.forward),offset=Length(aim-view.forward*forward);
    Log("launch camera (diagnostic only): valid=%u eye=(%.3f,%.3f,%.3f) forward=(%.6f,%.6f,%.6f) contactDepth=%.3f offAxis=%.3f angle=%.3fdeg",
        view.valid?1:0,view.origin.x,view.origin.y,view.origin.z,view.forward.x,view.forward.y,view.forward.z,
        view.valid?forward:0,view.valid?offset:0,view.valid?atan2f(offset,forward)*57.2957795f:0);
    return until;
}
uint64_t HoldForKillcam(uint64_t now,uint64_t until) {
    uint32_t target=s_target.load();
    if(!s_automatic.load()||!Engine::KillcamShows(target)||now+kKillcamTail<=until)return until;
    s_impact.Extend(now+kKillcamTail);return now+kKillcamTail;
}
bool SelectHead(uint32_t refID) {
    if(!s_installed||!refID)return false;
    Headshots::Clear();Cinematic::Clear();s_impact.Clear();s_impactStatus=0;s_reportedImpact=0;s_automatic=false;s_groinFocus=false;s_discardDamage=true;s_region=HeadRegion;SelectTarget(refID);
    Log("selected %08X",refID);return true;
}
unsigned ChangeView() {
    unsigned mode=s_mode.load()==1?2:1;s_mode=mode;
    Log("anatomy view: %s",mode==1?"intact":"cutaway");return mode;
}
static bool __cdecl ResetNotify(bool before,void*) {
    if(before){Graphics::Release();s_hasResources=false;s_failed=false;ForgetOwners();Audit::Select(++s_generation);}
    return true;
}
static int __cdecl FinishWorld(void* camera,void* accumulator,uint32_t flags) {
    auto original=reinterpret_cast<int(__cdecl*)(void*,void*,uint32_t)>(s_finish.Original());
    if(s_rendering.test_and_set())return original(camera,accumulator,flags);
    struct Guard {~Guard(){s_rendering.clear();}} guard;
    if(!s_target.load()) {
        if(s_hasResources){Graphics::Release();s_hasResources=false;ForgetOwners();}
        return original(camera,accumulator,flags);
    }
    if(s_failed)return original(camera,accumulator,flags);
    CameraFrame snapshot{};auto renderer=Engine::GetRenderer();IDirect3DDevice9* device=nullptr;
    uint32_t generation=s_generation.load(),target=s_target.load();
    if(camera&&renderer&&!renderer->inactive&&renderer->device&&!Engine::Menus()) {
        if(!s_resetOwner) {
            reinterpret_cast<uint32_t(__thiscall*)(Engine::Renderer*,bool(__cdecl*)(bool,void*),void*)>(0x86BAE0)(renderer,ResetNotify,nullptr);
            s_resetOwner=renderer;
        }
        if(s_resetOwner==renderer) {
            reinterpret_cast<void(__thiscall*)(Engine::Renderer*,void*)>(0x4E9BB0)(renderer,camera);
            memcpy(snapshot.view,renderer->view,sizeof snapshot.view);memcpy(snapshot.projection,renderer->projection,sizeof snapshot.projection);
            snapshot.origin=*reinterpret_cast<Vec3*>(0x11F474C);
            device=renderer->device;device->AddRef();
            if(FAILED(device->GetViewport(&snapshot.viewport))){device->Release();device=nullptr;}
        }
    }
    //the guard spans the chained renderer, only copied values and a com reference survive it
    int result=original(camera,accumulator,flags);
    if(device) {
        auto live=Engine::GetRenderer();
        if(live==s_resetOwner&&!live->inactive&&live->device==device&&generation==s_generation.load()&&target==s_target.load()) {
            uint32_t region=s_region.load();
            auto failure=region==HeadRegion?Engine::CaptureHead(target,*s_frame):Engine::CaptureRegion(target,region,s_side.load(),*s_frame);
            const Model& anatomy=region==TorsoRegion&&s_frame->noGenitals&&!s_plainTorso.meshes.empty()?s_plainTorso:s_models[region];
            if(!failure)BodySkin::OpenOver(anatomy,*s_frame,region==TorsoRegion&&s_groinFocus.load());
            if(!failure) {
                auto appearance=settings.appearance;
                //the preview scale and offsets fit the head only
                if(region!=HeadRegion){appearance.scale=1;appearance.up=appearance.forward=appearance.side=0;}
                Impact::Frame impact{};Impact::Sample source{};
                bool automatic=s_automatic.load();
                if(automatic) {
                    appearance.opacity=1;
                    int status=s_impact.Snapshot(target,*s_frame,GetTickCount64(),Engine::SimulationTime(),impact,&source);
                    if(!status){device->Release();return result;}
                    s_impactStatus=status;
                    float fade=Clamp((impact.duration-impact.realAge)/.35f,0,1);
                    appearance.opacity*=fade;
                }
                if(generation!=s_generation.load()||target!=s_target.load()){device->Release();return result;}
                if(s_discardDamage.exchange(false)){Graphics::ClearDamage();ForgetOwners();}
                //another actor's part, or this one's again after its scene was cleared: bring back its wounds, the debris in the air flies on
                if(automatic&&s_owner[region]!=target) {
                    auto kept=Recall(target,region);
                    Graphics::RestoreWounds(region,kept?kept->wounds:std::vector<Damage::Kept>{});
                    if(kept&&!kept->wounds.empty())s_restoredNote=(uint64_t(target)<<32)|(region<<8)|uint32_t(kept->wounds.size());
                    s_owner[region]=target;
                }
                if(!automatic)s_owner[region]=0;
                if(Graphics::Draw(device,anatomy,snapshot,*s_frame,s_mode.load(),appearance,automatic?&impact:nullptr)){
                    s_fitStrength=s_frame->fit.strength;s_status=1;s_hasResources=true;
                    if(automatic&&Graphics::WoundRevision(region)!=s_keptRevision[region]) {
                        s_keptRevision[region]=Graphics::WoundRevision(region);
                        auto& kept=Keep(target,region);kept.used=GetTickCount64();Graphics::KeepWounds(region,kept.wounds);
                    }
                    if(automatic&&impact.path.valid){CloseUp::Track(target,region,impact.path.entry,impact.path.exit);CloseUp::Rendered(snapshot.origin);}
                    uint8_t cues[16];unsigned heard=Graphics::TakeCues(cues,16);
                    if(automatic&&settings.sounds)for(unsigned k=0;k<heard;++k) {
                        if(GoreSound::Play(cues[k],settings.soundVolume)){s_soundedKinds|=1u<<cues[k];++s_soundedCount;}
                        else s_silentCue=cues[k]+1u;
                    }
                    if(automatic) {
                        Graphics::BloodReport blood;bool landed=Graphics::TakeBlood(blood);auto flying=Graphics::BloodInFlight();
                        AcquireSRWLockExclusive(&s_reportLock);if(landed){s_bloodLanded=blood;s_bloodReady=true;}s_bloodFlying=flying;ReleaseSRWLockExclusive(&s_reportLock);
                    }
                    if(automatic&&impact.path.valid) {
                        auto report=Graphics::DamageSummary();
                        AcquireSRWLockExclusive(&s_reportLock);s_report=report;ReleaseSRWLockExclusive(&s_reportLock);
                        s_damageEvent=(impact.sequence<<32)|Graphics::DamageStatus();
                    }
                    if(automatic)Audit::Submit(generation,target,snapshot,*s_frame,appearance,source,impact,Graphics::DamageStatus());
                }
                else{s_hresult=Graphics::LastError();s_status=-2;s_failed=true;Graphics::Release();s_hasResources=false;ForgetOwners();}
            }else {s_captureFailure=failure;s_status=-1;}
        }
        device->Release();
    }
    return result;
}
//a frame of its own, so the hit test never disturbs the one being rendered
static std::unique_ptr<HeadFrame> s_groinFrame;
bool GroinHit(uint32_t target,const Impact::Sample& sample) {
    const Model& torso=s_models[TorsoRegion];
    if(!sample.valid||!sample.posed||torso.meshes.empty())return false;
    //the line through the contact must pass near the anatomy's rest box before anything is captured
    constexpr float kGroinMargin=8;
    if(!Groin::Near(torso,sample.pose.Unapply(sample.point),sample.pose.Unrotate(sample.direction),kGroinMargin))return false;
    //the genitals belong to men's anatomy, and the female test reads an npc's base data
    auto actor=Engine::Lookup(target);
    if(Engine::KindOf(actor)!=Engine::Kind::Human||Engine::IsFemale(actor))return false;
    if(!s_groinFrame)s_groinFrame=std::make_unique<HeadFrame>();
    if(Engine::CaptureRegion(target,TorsoRegion,0,*s_groinFrame))return false;
    Impact::Path path;
    return Impact::Map(sample,*s_groinFrame,path)&&Groin::Crossed(torso,*s_groinFrame,path,sample.bullet.radius);
}
bool InitRuntime(const char* modelPath) {
    if(s_installed)return true;
    s_impact.SetImpactSpeed(settings.impactSpeed);
    if(!s_models[HeadRegion].Load(modelPath)){Log("anatomy asset invalid or missing: %s",modelPath);return false;}
    //the body regions' assets sit next to head.xra, a missing one turns its region off
    auto beside=[&](const char* file,char (&path)[MAX_PATH]) {
        strcpy_s(path,modelPath);
        if(auto slash=strrchr(path,'\\'))strcpy_s(slash+1,path+MAX_PATH-(slash+1),file);
    };
    auto load=[&](uint32_t region,const char* file,bool& enabled) {
        char path[MAX_PATH];beside(file,path);
        if(!s_models[region].Load(path)){enabled=false;Log("%s invalid or missing: %s; its hits disabled",file,path);}
    };
    load(TorsoRegion,"torso.xra",settings.regions[TorsoRegion]);load(LegsRegion,"legs.xra",settings.regions[LegsRegion]);
    load(ArmsRegion,"arms.xra",settings.regions[ArmsRegion]);
    char plain[MAX_PATH];beside("torso-female.xra",plain);
    if(settings.regions[TorsoRegion]&&!s_plainTorso.Load(plain))Log("torso-female.xra invalid or missing: %s; women and creatures use the men's torso",plain);
    s_frame=std::make_unique<HeadFrame>();
    Graphics::SetAsyncDamage(true);Graphics::SetBloodSpray(settings.bloodSpray);
    if(settings.closeUp&&!CloseUp::Install())Log("close-up camera hook unavailable; killcams keep the game's camera");
    if(!Engine::InitNames()||!Engine::InitRegionNames())return false;
    Engine::InitProjectileAmmo();
    //0x8741db is e8/cdecl(camera, accumulator, flags), the caller pops 0x0c
    if(!s_finish.ReplaceCall(0x8741DB,FinishWorld))return false;
    s_installed=true;Log("world hook chained to %08X; %u head, %u torso (%u for women), %u leg and %u arm anatomy batches",static_cast<unsigned>(s_finish.Original()),
        static_cast<unsigned>(s_models[HeadRegion].meshes.size()),static_cast<unsigned>(s_models[TorsoRegion].meshes.size()),static_cast<unsigned>(s_plainTorso.meshes.size()),
        static_cast<unsigned>(s_models[LegsRegion].meshes.size()),static_cast<unsigned>(s_models[ArmsRegion].meshes.size()));
    if(!Headshots::InitHooks())Log("ERROR: automatic headshot hook unavailable; manual preview still works");
    else if(settings.automatic&&!Trace::InitHooks())Log("WARNING: shot ray capture unavailable; projectile-vector fallback active");
    return true;
}
//the bend each region reports: the frame's first influence, or the larger of both limb roots
static const char* const kBendNames[kRegions]={"neckBend","chestBend","thighBend","shoulderBend"};
void Update() {
    if(!s_installed)return;
    Audit::Drain();Headshots::Update();
    Impact::Report report;
    if(s_impact.TakeReport(report)) {
        const auto& frame=report.frame;const auto& p=frame.path;
        if(report.initial)Log("trajectory %u: mapped=%u traced=%u contactLocal=(%.2f,%.2f,%.2f) directionLocal=(%.3f,%.3f,%.3f) correction=%.3f entry=(%.2f,%.2f,%.2f) exit=(%.2f,%.2f,%.2f)",
            static_cast<unsigned>(frame.sequence),p.valid?1u:0u,p.traced?1u:0u,p.point.x,p.point.y,p.point.z,p.direction.x,p.direction.y,p.direction.z,
            p.correction,p.entry.x,p.entry.y,p.entry.z,p.exit.x,p.exit.y,p.exit.z);
        if(p.valid&&report.region!=HeadRegion)Log("trajectory pose %u: stage=%u age=%.3f realAge=%.3f %sTurn=%.2fdeg entryShift=%.3f world units (-1 means no captured pose) %s=%.2fdeg",
            static_cast<unsigned>(frame.sequence),report.stage,frame.age,frame.realAge,RegionName(report.region),report.headTurn,report.entryShift,
            kBendNames[report.region<kRegions?report.region:HeadRegion],report.neckBend);
        else if(p.valid)Log("trajectory pose %u: stage=%u age=%.3f realAge=%.3f headTurn=%.2fdeg entryShift=%.3f world units (-1 means no captured pose) neckBend=%.2fdeg neckSkinError=%.3f",
            static_cast<unsigned>(frame.sequence),report.stage,frame.age,frame.realAge,report.headTurn,report.entryShift,report.neckBend,report.neckError);
        if(report.initial) {
            auto note=Engine::LastSkinSkip();
            Log("%s skin %u: triangles=%u shapes=%u skipped=%u, first skipped shape: %s with %u vertices",report.region==HeadRegion?"neck":RegionName(report.region),
                static_cast<unsigned>(frame.sequence),report.skinTriangles,report.skinShapes,report.skinSkipped,BodySkin::Describe(note.reason),note.vertices);
        }
    }
    int impactStatus=s_impactStatus.load();
    if(impactStatus&&impactStatus!=s_reportedImpact) {
        s_reportedImpact=impactStatus;
        Log(impactStatus==1?"impact path mapped to head; animated penetration active":
            impactStatus==-1?"impact path missed supported skin; using full-head reveal":"impact data unavailable; using full-head reveal");
    }
    auto damage=s_damageEvent.load();
    if(damage&&damage!=s_reportedDamage) {
        s_reportedDamage=damage;
        AcquireSRWLockShared(&s_reportLock);auto r=s_report;ReleaseSRWLockShared(&s_reportLock);
        Log("destruction sequence %u: wounds=%u boneRuns=%u brainRuns=%u breaks entry/inner/exit=%u/%u/%u plates=%u boneFragments=%u brainPieces=%u ruptured=%u debrisVertices=%u (geometry, not visible pixels)",
            static_cast<uint32_t>(damage>>32),r.wounds,r.boneRuns,r.brainRuns,r.entry,r.inner,r.exit,r.plates,r.fragments,r.brainPieces,r.ruptures,r.vertices);
    }
    {
        Graphics::BloodReport blood;bool have=false,cut=false;
        AcquireSRWLockShared(&s_reportLock);
        if(s_bloodReady.exchange(false)){blood=s_bloodLanded;have=true;}
        else if(!s_automatic.load()&&s_bloodFlying.frames&&s_bloodFlying.sequence!=s_bloodLogged){blood=s_bloodFlying;have=cut=true;}
        ReleaseSRWLockShared(&s_reportLock);
        if(have&&blood.sequence!=s_bloodLogged){s_bloodLogged=blood.sequence;LogBlood(blood,cut);}
    }
    if(auto note=s_restoredNote.exchange(0))
        Log("%s %08X: %u earlier wounds brought back, settled",RegionName(uint32_t(note>>8&0xFF)),uint32_t(note>>32),uint32_t(note&0xFF));
    auto silent=s_silentCue.load();
    if(silent&&silent!=s_reportedSilent){
        s_reportedSilent=silent;
        Log("gore sound: nothing to play in Data\\Sound\\fx\\xraykillcam\\%s\\",GoreSound::Name(uint8_t(silent-1)));
    }
    if(auto kinds=s_soundedKinds.exchange(0)) {
        char names[64]="";
        for(uint8_t k=0;k<Damage::kCueKinds;++k)if(kinds>>k&1){if(names[0])strcat_s(names,", ");strcat_s(names,GoreSound::Name(k));}
        Log("gore sounds played: %u (%s)",s_soundedCount.exchange(0),names);
    }
    int status=s_status.load();
    auto failure=s_captureFailure.load();
    if(status!=s_reported||(status==-1&&static_cast<uint32_t>(failure)!=static_cast<uint32_t>(s_reportedCapture))) {
        s_reported=status;
        s_reportedCapture=failure;
        if(status==1)Log("rendering %s %08X, mode %u, fit strength %.0f%%",RegionName(s_region.load()),s_target.load(),s_mode.load(),s_fitStrength.load()*100);
        else if(status==-1)Log("%s %08X capture rejected: %s; value=%u (0x%08X)",RegionName(s_region.load()),s_target.load(),
            Engine::DescribeHeadFailure(static_cast<Engine::HeadFailure>(static_cast<uint32_t>(failure))),
            static_cast<uint32_t>(failure>>32),static_cast<uint32_t>(failure>>32));
        else if(status==-2)Log("render failed: HRESULT %08X; disabled until device reset",static_cast<unsigned>(s_hresult.load()));
    }
    Cinematic::Update();
}
}
