#pragma once
#include "Rupture.h"

namespace XRay::Damage {
//blood thrown from a wound as droplets the vertex shader flies: blowback at the entry, the exit jet and its mist, a burst organ
enum JetKind : uint8_t {EntryJet,ExitJet,RuptureJet,MistJet,kJetKinds};
constexpr unsigned kJets=32,kDroplets=1024,kDropletTriangles=20,kSprayConstants=5;
//world units a second squared, and how far a droplet stretches along its flight per unit of speed
constexpr float kSprayGravity=300,kSprayStretch=.01f;
//a pumping exit: one more spurt per this much severity, this far apart in scene seconds, each this much weaker
constexpr float kPulseEvery=.4f,kPulseGap=.08f,kPulseFade=.7f;
struct JetStyle {unsigned count;float speedLo,speedHi,spread,emission,sizeLo,sizeHi,lifeLo,lifeHi;};
extern const JetStyle kJetStyles[kJetKinds];
//amount scales the droplet count and a little of their size, speed how fast they leave
struct Jet {Vec3 origin{},direction{};float born=0,scale=1;uint64_t sequence=0;uint8_t kind=EntryJet;bool live=false;float amount=1,speed=1;};
class Spray {
    Jet jets_[kJets];unsigned next_=0;
    void Add(const Jet& jet);
public:
    void Clear();
    //one shot's jets fixed in world space by the frame's pose, replacing any the same shot left
    //blood multiplies every jet's droplets, a severity above 1 also sends them faster and pumps the exit again, once for each
    //kPulseEvery above 1
    void Emit(const Transform& pose,float born,uint64_t sequence,Vec3 entry,Vec3 exit,float exitTime,const Rupture* ruptures,unsigned count,
        float severity=1,float blood=1);
    bool Live(unsigned jet,float clock) const;
    bool Any(float clock) const;
    //camera-relative origin and age, direction and gravity, speeds, spread and emission, sizes and lives, seed and stretch
    unsigned Constants(unsigned jet,float clock,Vec3 origin,float* out) const;
    const Jet& JetAt(unsigned jet) const {return jets_[jet];}
    float DropletCount(unsigned jet) const;
    //the live jets one shot threw and their droplets, by kind
    void Tally(uint64_t sequence,unsigned* jets,unsigned* droplets) const;
};
//kDroplets icosahedra about the origin, each vertex carrying its droplet index as the piece
void Droplets(PieceVertex* out);
}
