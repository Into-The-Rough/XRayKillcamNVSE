#include "../src/Cinematic.cpp"
#include <cstdio>
#include <cstdlib>

namespace {
using namespace XRay;
Engine::PlayerCamera player{};
Engine::VATS vats{};
Engine::Form target{};
bool menu=false,headAvailable=true,present=true,queueInCallback=false;
int32_t expectedPart=1;
unsigned modeWrites=0;uint32_t modeAtStart=0;
unsigned headCallback=0;
unsigned calls=0,checks=0;
void Check(bool pass,const char* name){++checks;if(!pass){printf("FAIL: %s\n",name);exit(1);}}
void __fastcall SetMode(Engine::PlayerCamera* self,void*,uint32_t mode){Check(self==&player,"mode setter ecx ABI");++modeWrites;player.mode=mode;}
Engine::PlayerCamera* Player(){return &player;}
Engine::VATS* VATS(){return &vats;}
Engine::Form* Lookup(uint32_t){return present?&target:nullptr;}
bool Menus(){return menu;}
void __fastcall Start(Engine::PlayerCamera* self,void*,Engine::Form* actor,float time,char cooldown,int32_t part) {
    Check(self==&player&&actor==&target&&time==settings.cameraSeconds&&cooldown==0&&part==expectedPart,"native camera ecx, float, char and body-part ABI");
    ++calls;modeAtStart=player.mode;
    if(queueInCallback){Cinematic::Clear();Cinematic::Queue(0x456,1);}
}
}
namespace XRay::Engine {
bool FramePose(Form*,uint32_t,Transform& out){
    out=Transform::Identity();
    if(headCallback==1)Cinematic::Clear();
    if(headCallback==2)Cinematic::Queue(0x456,1);
    if(headCallback==3)vats.mode=4;
    return headAvailable;
}
}
void TestCinematic() {
    using namespace XRay;
    Cinematic::s_player=Player;Cinematic::s_vats=VATS;Cinematic::s_lookup=Lookup;Cinematic::s_menus=Menus;
    Cinematic::s_start=reinterpret_cast<Cinematic::Start>(Start);Cinematic::s_setMode=reinterpret_cast<Cinematic::SetMode>(SetMode);
    player.mode=Engine::kKillCamFull;target.type=Engine::kCharacter;target.id=0x123;
    auto request=[] {Cinematic::Queue(0x123,1);Cinematic::Update();};
    request();Check(calls==1,"idle full-camera mode starts a sequence on the hit body part");
    Cinematic::Update();Check(calls==1,"request consumed once");
    vats.mode=4;request();Check(calls==1,"existing playback not interrupted");vats.mode=2;request();Check(calls==1,"vats selection not interrupted");vats.mode=0;
    player.timer=1;request();Check(calls==1,"existing killcam timer not replaced");player.timer=0;
    player.mode=0;request();Check(calls==1,"game's disabled kill cam respected");
    player.mode=1;request();Check(calls==2,"player view still asks the engine for its slow motion");player.mode=2;
    menu=true;request();Check(calls==2,"menu blocks camera request");menu=false;
    headAvailable=false;request();Check(calls==2,"missing body blocks camera request");headAvailable=true;
    present=false;request();Check(calls==2,"deleted lookup result safely rejected");present=true;
    target.flags=Engine::kDeleted;request();Check(calls==2,"deleted actor safely rejected");target.flags=0;
    target.type=0x3A;request();Check(calls==2,"creature not interpreted as human");target.type=Engine::kCharacter;
    expectedPart=7;Cinematic::Queue(0x123,7);Cinematic::Update();Check(calls==3,"limb hits frame the limb");
    Cinematic::Queue(0x123,14);Cinematic::Update();Check(calls==3,"a body part without anatomy asks for no camera");expectedPart=1;
    settings.nativeCamera=false;request();settings.nativeCamera=true;Cinematic::Update();Check(calls==3,"disabled request does not start later");
    Cinematic::Queue(0x123,1);Cinematic::Clear();Cinematic::Update();Check(calls==3,"load/manual/off cancels pending camera");
    headCallback=1;request();Check(calls==3,"clear during scene query cancels stale request");
    headCallback=2;request();Check(calls==3,"replacement during scene query cancels old request");
    headCallback=0;Cinematic::Clear();headCallback=3;request();Check(calls==3,"camera starting during scene query is not interrupted");
    headCallback=0;vats.mode=0;
    queueInCallback=true;request();queueInCallback=false;
    Check(calls==4,"native callback can reset and queue another request");
    Cinematic::Update();Check(calls==5,"outer callback does not overwrite reentrant queued request");
    Check(player.mode==2&&player.timer==0&&vats.mode==0&&!modeWrites,"plugin writes no native camera fields");
    //a cripple runs its killcam in full mode whatever the option says, and the option comes back after the native cleanup
    unsigned before=calls;player.mode=0;
    Cinematic::Queue(0x123,1,true);Cinematic::Update();
    Check(calls==before+1&&modeAtStart==2&&modeWrites==1,"a forced killcam starts in full mode through the native setter");
    player.timer=1;Cinematic::Update();Check(player.mode==2&&modeWrites==1,"full mode holds while the killcam runs");
    player.timer=0;vats.mode=4;Cinematic::Update();Check(player.mode==2,"and while vats still owns the camera");
    vats.mode=0;Cinematic::Update();Check(player.mode==0&&modeWrites==2,"the player's mode comes back once the native cleanup has run");
    Cinematic::Update();Check(modeWrites==2,"restored once");
    player.mode=0;Cinematic::Queue(0x123,1);Cinematic::Update();Check(calls==before+1,"a kill still follows the disabled option");
    player.mode=1;Cinematic::Queue(0x123,7,true);expectedPart=7;Cinematic::Update();expectedPart=1;
    Check(calls==before+2&&modeAtStart==2&&modeWrites==3,"player view is raised to full for a cripple");
    player.timer=1;Cinematic::Update();player.mode=0;player.timer=0;Cinematic::Update();
    Check(player.mode==0&&modeWrites==3,"a mode the player changed during the killcam is kept");
    player.mode=2;Cinematic::Queue(0x123,1,true);Cinematic::Update();Check(calls==before+3&&modeWrites==3,"full mode needs no change");
    player.mode=0;Cinematic::Queue(0x123,1,true);Cinematic::Update();Cinematic::Update();
    Check(player.mode==0&&modeWrites==5,"a killcam the engine never started restores on the next update");
    player.mode=2;
    printf("PASS: %u native-camera policy and ABI checks\n",checks);
}
