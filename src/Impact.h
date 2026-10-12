#pragma once
#include "Math.h"
#include "Ammunition.h"
#include <windows.h>
#include <cstdint>

namespace XRay {
struct HeadFrame;
struct CameraFrame;
struct Vertex;
namespace Impact {
struct Sample {
    Vec3 point{},direction{};Transform pose{};bool valid=false,posed=false;uint32_t ammo=0;Ammunition::Profile bullet;
    Vec3 rayOrigin{};bool traced=false;float headingChange=0;
    //the influence the contact sits on and its world transform at the hit, the shot moves with that bone until it is mapped
    uint32_t carrier=0;Transform carrierPose{};
};
struct Path {Vec3 entry{},exit{};bool valid=false;Vec3 point{},direction{};bool traced=false;float correction=0;};
struct Frame {Path path;float age=0,duration=5;Ammunition::Profile bullet;uint64_t sequence=0;float realAge=0,sceneTime=-1;};
//neckBend is the frame's first influence, Neck1 for the head and Spine2 for the torso, the larger thigh or upper arm for the limbs
struct Report {Frame frame;unsigned stage=0;bool initial=false;float headTurn=-1,entryShift=-1,neckBend=0,neckError=-1;unsigned skinTriangles=0,skinShapes=0,skinSkipped=0,region=0;};
constexpr unsigned kBulletSides=12,kBulletVertices=kBulletSides*12;
constexpr float kTravelTime=.04f;
//the X-ray's own clock runs at the impact speed of the game's while the bullet crosses the part and kImpactLinger after,
//then eases back to the game's over kImpactEase of its own seconds
constexpr float kImpactLinger=.04f,kImpactEase=.2f;
float ClockRate(float age,float crossing,float speed);
//a path through one human body part is never longer, and its contact sits within this slack of the visible skin,
//both grow with the body's size
constexpr float kLongestPath=40,kContactSlack=8;
bool Map(const Sample& sample,const HeadFrame& head,Path& out);
float Distance(const Frame& frame);
float TimeAt(const Frame& frame,float distance);
float BulletEnd(const Frame& frame);
float Progress(const Frame& frame);
unsigned BuildBullet(const Frame& frame,Vertex* out,float size);
Vec3 Eye(const HeadFrame& head,const CameraFrame& camera);
Vec3 SectionNormal(const Frame& frame,const HeadFrame& head,const CameraFrame& camera);
class Playback {
    SRWLOCK lock_=SRWLOCK_INIT;
    Sample sample_;
    Path path_;
    uint32_t target_=0;
    uint64_t started_=0,expires_=0,sequence_=0;
    float lastSimulation_=0;
    //the scene clock all shots share and its reading when this shot began
    double clock_=0,clockStart_=0;float impactSpeed_=1;
    bool attempted_=false;
    Report report_{};unsigned reportStage_=0;bool reportReady_=false;
public:
    uint64_t Start(uint32_t target,const Sample& sample,uint64_t now,float simulation,float duration=5);
    void Clear();
    //how fast the X-ray's clock runs against the game's through each shot's impact, 1 for the game's own pace
    void SetImpactSpeed(float speed);
    //keeps the reveal open until at least then
    void Extend(uint64_t until);
    int Snapshot(uint32_t target,const HeadFrame& head,uint64_t now,float simulation,Frame& out,Sample* source=nullptr);
    bool TakeReport(Report& out);
};
}
}
