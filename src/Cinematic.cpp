#include "Cinematic.h"
#include "Engine.h"
#include "Runtime.h"

namespace XRay::Cinematic {
static uint32_t s_target=0;
static uint32_t s_epoch=0;
static int32_t s_bodyPart=0;
static bool s_force=false;
//the player's own kill cam mode while a forced killcam runs in full
static uint32_t s_restoreMode=0;
static bool s_forced=false;
using Start=void(__thiscall*)(Engine::PlayerCamera*,Engine::Form*,float,char,int32_t);
static auto s_start=reinterpret_cast<Start>(0x93E530);
//playercharacter kill cam mode setter behind the options menu, ret 4, a change during an active killcam ends it with the old mode
using SetMode=void(__thiscall*)(Engine::PlayerCamera*,uint32_t);
static auto s_setMode=reinterpret_cast<SetMode>(0x7D12D0);
static auto s_player=Engine::GetPlayer;
static auto s_vats=Engine::GetVATS;
static auto s_lookup=Engine::Lookup;
static auto s_menus=Engine::Menus;
void Queue(uint32_t target,int32_t bodyPart,bool force){++s_epoch;s_target=target;s_bodyPart=bodyPart;s_force=force;}
void Clear(){++s_epoch;s_target=0;}
//the end of a killcam passes the mode field to its cleanup (0x93eb2a), so a forced full mode holds until that has run
static void Restore(Engine::PlayerCamera* player) {
    if(!s_forced||!player||player->timer>0||s_vats()->mode!=Engine::kVATSNone)return;
    s_forced=false;
    //a mode the player changed meanwhile is theirs to keep
    if(player->mode!=Engine::kKillCamFull)return;
    s_setMode(player,s_restoreMode);
    Log("game Kill Cam mode %u restored",s_restoreMode);
}
void Update() {
    uint32_t id=s_target,epoch=s_epoch;int32_t part=s_bodyPart;bool force=s_force;s_target=0;
    Restore(s_player());
    if(!id||!settings.nativeCamera||s_menus())return;
    auto player=s_player();
    if(!player||s_vats()->mode!=Engine::kVATSNone||player->timer>0) {
        Log("native camera skipped: another camera/VATS sequence is active");return;
    }
    //full moves the camera and slows time, player view only slows time
    if(!force&&player->mode!=Engine::kKillCamFull&&player->mode!=Engine::kKillCamPlayerView) {
        Log("native camera skipped: game Kill Cam is disabled (mode=%u)",player->mode);return;
    }
    uint32_t region=Engine::PartRegion(part);
    if(region>=kRegions)return;
    auto target=s_lookup(id);Transform pose;
    if(!Engine::Supported(target)||!Engine::FramePose(target,region,pose)) {
        Log("native camera skipped: %s unavailable for %08X",RegionName(region),id);return;
    }
    if(epoch!=s_epoch||s_vats()->mode!=Engine::kVATSNone||player->timer>0)return;
    if(force&&player->mode!=Engine::kKillCamFull) {
        s_restoreMode=player->mode;s_forced=true;
        s_setMode(player,Engine::kKillCamFull);
    }
    Log("native killcam requested: target=%08X bodyPart=%d %s duration=%.2f%s",id,part,player->mode==Engine::kKillCamFull?"camera and slow motion":"slow motion only",
        settings.cameraSeconds,s_forced?", full mode held for this killcam":"");
    //ret 0x10, false bypasses cooldown, last arg becomes vats hitdata+0x10
    s_start(player,target,settings.cameraSeconds,0,part);
    //native vats owns playback and cleanup, no borrowed pointer is consumed after callbacks
}
}
