#include "Spray.h"
#include <cstring>

namespace XRay::Damage {
const JetStyle kJetStyles[kJetKinds]={
    {120,30,90,.55f,.03f,.05f,.14f,.35f,.7f},
    {300,60,200,.35f,.07f,.06f,.2f,.4f,.85f},
    {200,40,130,1.6f,.03f,.07f,.22f,.45f,.9f},
    {400,6,30,3,.05f,.025f,.06f,.35f,.8f},
};
void Spray::Clear(){for(auto& jet:jets_)jet=Jet{};next_=0;}
void Spray::Add(const Jet& jet){jets_[next_]=jet;next_=(next_+1)%kJets;}
void Spray::Emit(const Transform& pose,float born,uint64_t sequence,Vec3 entry,Vec3 exit,float exitTime,const Rupture* ruptures,unsigned count,
    float severity,float blood) {
    for(auto& jet:jets_)if(jet.sequence==sequence)jet.live=false;
    Vec3 direction=exit-entry;
    if(!Normalize(direction)||!pose.Valid()||!(blood>0))return;
    severity=fmaxf(severity,1);
    float amount=blood*severity,speed=sqrtf(severity);
    auto emit=[&](JetKind kind,Vec3 at,Vec3 heading,float time,float more,float faster) {
        Vec3 world=pose.Rotate(heading);
        if(Normalize(world))Add({pose.Apply(at),world,born+time,pose.scale,sequence,kind,true,amount*more,speed*faster});
    };
    emit(EntryJet,entry,direction*-1,0,1,1);
    emit(ExitJet,exit,direction,exitTime,1,1);
    emit(MistJet,exit,direction,exitTime,1,1);
    unsigned pulses=unsigned((severity-1)/kPulseEvery+.5f);
    for(unsigned k=1;k<=pulses;++k){float fade=powf(kPulseFade,float(k));emit(ExitJet,exit,direction,exitTime+kPulseGap*float(k),fade,fade);}
    for(unsigned k=0;k<count;++k)emit(RuptureJet,ruptures[k].centre,ruptures[k].direction,ruptures[k].time,1,1);
}
bool Spray::Live(unsigned jet,float clock) const {
    const auto& j=jets_[jet];const auto& style=kJetStyles[j.kind];
    float age=clock-j.born;
    return j.live&&std::isfinite(age)&&age>=0&&age<=style.emission+style.lifeHi;
}
bool Spray::Any(float clock) const {
    for(unsigned jet=0;jet<kJets;++jet)if(Live(jet,clock))return true;
    return false;
}
float Spray::DropletCount(unsigned jet) const {
    const auto& j=jets_[jet];
    return fminf(float(kJetStyles[j.kind].count)*j.amount,float(kDroplets));
}
void Spray::Tally(uint64_t sequence,unsigned* jets,unsigned* droplets) const {
    for(unsigned k=0;k<kJetKinds;++k)jets[k]=droplets[k]=0;
    for(unsigned jet=0;jet<kJets;++jet) {
        const auto& j=jets_[jet];
        if(!j.live||j.sequence!=sequence)continue;
        ++jets[j.kind];droplets[j.kind]+=unsigned(DropletCount(jet)+.5f);
    }
}
unsigned Spray::Constants(unsigned jet,float clock,Vec3 origin,float* out) const {
    const auto& j=jets_[jet];const auto& style=kJetStyles[j.kind];
    Vec3 at=j.origin-origin;
    //more blood comes as more drops, only a little bigger
    float speed=j.scale*j.speed,size=j.scale*Clamp(1+.1f*(j.amount-1),.6f,1.4f);
    const float values[kSprayConstants*4]={at.x,at.y,at.z,clock-j.born,j.direction.x,j.direction.y,j.direction.z,kSprayGravity,
        style.speedLo*speed,style.speedHi*speed,style.spread,style.emission,style.sizeLo*size,style.sizeHi*size,style.lifeLo,style.lifeHi,
        float((j.sequence*7+j.kind+uint64_t(j.born*1000))%997),kSprayStretch,0,0};
    memcpy(out,values,sizeof values);
    return unsigned(DropletCount(jet)+.5f);
}
void Droplets(PieceVertex* out) {
    const float t=1.618034f;
    Vec3 corners[12]={{-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},{0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},{t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1}};
    for(auto& c:corners)Normalize(c);
    const unsigned faces[kDropletTriangles][3]={{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
    for(unsigned droplet=0;droplet<kDroplets;++droplet)for(unsigned f=0;f<kDropletTriangles;++f)for(unsigned k=0;k<3;++k) {
        Vec3 p=corners[faces[f][k]];
        *out++={p,p,1,float(droplet)};
    }
}
}
