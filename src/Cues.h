#pragma once
#include "Rupture.h"

namespace XRay::Damage {
//the moments a wound's sounds mark, each folder of clips under sound\fx\xraykillcam
enum CueKind : uint8_t {HitCue,BoneCue,OrganCue,BurstCue,SprayCue,kCueKinds};
//time in scene seconds after the wound's birth
struct Cue {float time=0;uint8_t kind=HitCue;bool fired=false;};
//a burst of bone cues blurs into one, so a wound sounds its first and last break only
constexpr unsigned kCues=10,kBoneCues=2;
//in time order: the bullet entering, its first and last bone break, the first tissue it tears, each burst organ, its exit
unsigned PlanCues(const Seed* soft,unsigned softCount,const Cluster* clusters,unsigned clusterCount,
    const Rupture* ruptures,unsigned ruptureCount,float exitTime,Cue* out);
}
