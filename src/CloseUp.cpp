#include "CloseUp.h"
#include "Engine.h"
#include "Hooks.h"
#include "Runtime.h"
#include <windows.h>

namespace XRay::CloseUp {
static CallDetour s_publish,s_effects;
static uint32_t s_target=0,s_region=0;
static Vec3 s_entry{},s_exit{};
static bool s_tracking=false,s_live=false,s_reported=false,s_described=false;
static double s_start=0;
static Vec3 s_eye{};
//the eye the close-up last placed this killcam, in world space, and whether it has placed one yet
static Transform s_pose=Transform::Identity();static bool s_posed=false;
//the shot's heading from the killcam's first frame and the point the camera follows
static Vec3 s_heading{},s_follow{};
//how far behind the wound's middle the eye must stay to be outside the shot's entry
static float s_reach=0;
static Framing s_framing;
//what one killcam's close-up did, logged when it ends: frames placed and skipped by reason, restarts, wall pulls, and the
//largest frame-to-frame eye move and turn
struct Watch {
    unsigned frames=0,skipped[4]{},restarts=0,pulls=0,jumpFrame=0;
    float jump=0,jumpStep=0,turn=0,nearest=1e9f,distance=-1;Vec3 eye{},look{};
};
static Watch s_watch;
static void Report() {
    auto& w=s_watch;
    if(w.frames)Log("close-up %08X ended: %u frames placed, skipped %u target/%u pose/%u camera/%u place, %u restarts, %u wall pulls (nearest %.1f), "
        "largest eye move %.2f units over %.1f ms at frame %u, largest turn %.2fdeg",s_target,w.frames,w.skipped[0],w.skipped[1],w.skipped[2],w.skipped[3],
        w.restarts,w.pulls,w.nearest,w.jump,w.jumpStep*1000,w.jumpFrame,w.turn);
    w=Watch{};
}
static double s_last=0;
//the performance counter in seconds, finer than the 15.6 ms tick
static double Now() {
    static const double period=[]{LARGE_INTEGER f;return QueryPerformanceFrequency(&f)&&f.QuadPart>0?1.0/double(f.QuadPart):0;}();
    LARGE_INTEGER t;
    return period>0&&QueryPerformanceCounter(&t)?double(t.QuadPart)*period:double(GetTickCount64())*.001;
}
void Track(uint32_t target,uint32_t region,Vec3 entry,Vec3 exit) {
    if(!s_tracking||target!=s_target||region!=s_region){Report();s_live=false;s_reported=false;}
    s_target=target;s_region=region;s_entry=entry;s_exit=exit;s_tracking=true;
}
void Clear(){Report();s_tracking=false;s_live=false;s_target=0;}
//the engine keeps its own local pose for its next frame, only the world pose is ours
static bool Place(Engine::Object* camera,Transform want) {
    want.scale=camera->world.scale;
    auto parent=camera->parent;
    Transform local=parent&&parent->world.Valid()?Compose(Inverse(parent->world),want):want;
    if(!local.Valid())return false;
    Transform kept=camera->local;
    camera->local=local;Engine::UpdateObject(camera);
    camera->local=kept;
    return true;
}
static void Apply() {
    if(!s_tracking||!settings.closeUp)return;
    auto vats=Engine::GetVATS();auto player=Engine::GetPlayer();
    if(vats->mode!=Engine::kVATSPlayback||!player||player->timer<=0){s_live=false;return;}
    auto target=Engine::VATSTarget();
    if(!target||target->id!=s_target||!Engine::Supported(target)){if(s_live)++s_watch.skipped[0];return;}
    Transform frame;
    if(!Engine::FramePose(target,s_region,frame)){if(s_live)++s_watch.skipped[1];return;}
    auto graph=Engine::GetSceneGraph();Engine::Object* camera=graph?graph->camera:nullptr;
    if(!camera||!camera->world.Valid()||!camera->local.Valid()){if(s_live)++s_watch.skipped[2];return;}
    const auto& r=camera->world.r;
    float handedness=Dot(Cross({r[0],r[3],r[6]},{r[1],r[4],r[7]}),{r[2],r[5],r[8]});
    double now=Now();
    Vec3 wound=frame.Apply((s_entry+s_exit)*.5f),shot=frame.Rotate(s_exit-s_entry);
    //the bone's own rotation never turns the view, and its frame-to-frame jitter is eased out of the camera's position
    if(!s_live) {
        //a killcam that comes back for the same target and region within one X-ray restarts the orbit from its start
        if(s_watch.frames){++s_watch.restarts;Log("close-up %08X restarted after %u frames: VATS mode %u, killcam timer %.2f",s_target,s_watch.frames,vats->mode,player->timer);}
        s_live=true;s_posed=s_reported=s_described=false;s_start=s_last=now;s_framing={};s_heading=shot;s_follow=wound;
        s_reach=Length(shot)*frame.scale*.5f+kNearest;
    }
    float seconds=float(now-s_start),step=float(now-s_last);s_last=now;
    s_follow=Follow(s_follow,wound,step);
    Transform want=Frame(s_framing,s_follow,s_heading,fmaxf(settings.closeUpDistance,s_reach),settings.closeUpOrbit*seconds*.01745329f,handedness,step,
        [&](Vec3 eye){return Engine::Clearance(target,s_follow,eye);});
    auto parent=camera->parent;
    //once a shot, when its turn is chosen, whether or not the renderer later maps the wound
    if(!s_described&&s_framing.chosen){s_described=true;Log("close-up camera %08X: camera=%p parent=%p native=(%.1f,%.1f,%.1f) eye=(%.1f,%.1f,%.1f) wound=(%.1f,%.1f,%.1f) turn=%.0fdeg distance=%.1f",s_target,
        static_cast<void*>(camera),static_cast<void*>(parent),camera->world.t.x,camera->world.t.y,camera->world.t.z,want.t.x,want.t.y,want.t.z,wound.x,wound.y,wound.z,
        s_framing.turn*57.29578f,s_framing.distance);}
    if(!Place(camera,want)){++s_watch.skipped[3];return;}
    auto& w=s_watch;Vec3 look{want.r[0],want.r[3],want.r[6]};
    if(w.frames) {
        float move=Length(want.t-w.eye),turn=acosf(Clamp(Dot(look,w.look),-1,1))*57.29578f;
        if(move>w.jump){w.jump=move;w.jumpStep=step;w.jumpFrame=w.frames;}
        w.turn=fmaxf(w.turn,turn);
        if(s_framing.distance<w.distance-.5f)++w.pulls;
    }
    w.eye=want.t;w.look=look;w.distance=s_framing.distance;w.nearest=fminf(w.nearest,s_framing.distance);++w.frames;
    s_eye=want.t;s_pose=want;s_posed=true;
}
//the frame's image space effects run before the scene publication: the killcam's depth radial blur projects its target
//through the scene camera (0x52a330 to 0xb8b760), so the close-up's last eye stands in from the start of the frame
static void Early() {
    if(!s_live||!s_posed||!s_tracking||!settings.closeUp)return;
    auto vats=Engine::GetVATS();auto player=Engine::GetPlayer();
    if(vats->mode!=Engine::kVATSPlayback||!player||player->timer<=0)return;
    auto target=Engine::VATSTarget();
    if(!target||target->id!=s_target)return;
    auto graph=Engine::GetSceneGraph();Engine::Object* camera=graph?graph->camera:nullptr;
    if(camera&&camera->world.Valid()&&camera->local.Valid())Place(camera,s_pose);
}
void Rendered(Vec3 origin) {
    if(!s_live||s_reported)return;
    s_reported=true;
    float off=Length(origin-s_eye);
    if(off<1)Log("close-up camera %08X drawn from its own eye",s_target);
    else Log("close-up camera %08X not the camera drawn: the renderer's camera is %.1f units from the close-up eye",s_target,off);
}
//the scene publication, ecx only and a plain ret, the only call of 0x86fc60, runs each frame before the world renders
static void __fastcall Published(void* self,void*) {
    reinterpret_cast<void(__thiscall*)(void*)>(s_publish.Original())(self);
    Apply();
}
//the frame's image space modifier pass, ecx and one byte argument with ret 4, called once a frame after the player update
static int __fastcall Effects(void* self,void*,char advance) {
    Early();
    return reinterpret_cast<int(__thiscall*)(void*,char)>(s_effects.Original())(self,advance);
}
bool Install() {
    if(!s_publish.ReplaceCall(0x86EC9B,Published))return false;
    Log("close-up camera hook chained to %08X",static_cast<unsigned>(s_publish.Original()));
    if(s_effects.ReplaceCall(0x86E799,Effects))Log("close-up effects hook chained to %08X",static_cast<unsigned>(s_effects.Original()));
    else Log("close-up effects hook unavailable; the killcam's radial blur may measure from the game's own camera");
    return true;
}
}
