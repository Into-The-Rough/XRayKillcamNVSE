#pragma once
#include "Graphics.h"
#include "Headshots.h"

namespace XRay {
//kills, cripples, criticals and groin shots open the X-ray by default, every hit only when asked
//the close-up holds the killcam's camera this far from the wound and turns it this many degrees a second
struct Settings {Appearance appearance;bool automatic=true,nativeCamera=true,kills=true,cripples=true,criticals=true,groin=true,everyHit=false;bool regions[kRegions]={true,true,true,true};
    //the killcam's timer, and how fast the X-ray's clock runs against the game's while the bullet crosses the part
    float revealSeconds=5,cameraSeconds=4,impactSpeed=.3f;bool closeUp=true;float closeUpDistance=28,closeUpOrbit=20;bool bloodSpray=true;
    bool sounds=true;float soundVolume=1;bool feralGhouls=true,superMutants=true;
    //how much harder a killing shot breaks, tears and bleeds, and how much blood every wound throws
    float killSeverity=1.8f,bloodAmount=1.5f;};
extern Settings settings;
void Log(const char* format,...);
bool InitRuntime(const char* modelPath);
void Update();
void Clear();
bool SelectHead(uint32_t refID);
unsigned ChangeView();
uint64_t ShowAutomatic(const Headshots::Hit& hit,uint64_t now);
//whether a man's torso hit crosses his testes or penis, read on the main thread just after the native hit
bool GroinHit(uint32_t target,const Impact::Sample& sample);
void HideAutomaticHead();
//the actor an automatic X-ray shows, 0 for none
uint32_t ShownTarget();
//the reveal's end, pushed on while a killcam shows its target, which then fades out kKillcamTail milliseconds after it ends
constexpr uint64_t kKillcamTail=500;
uint64_t HoldForKillcam(uint64_t now,uint64_t until);
}
