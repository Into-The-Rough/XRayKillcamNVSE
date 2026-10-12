#include "Cues.h"
#include <algorithm>

namespace XRay::Damage {
unsigned PlanCues(const Seed* soft,unsigned softCount,const Cluster* clusters,unsigned clusterCount,
    const Rupture* ruptures,unsigned ruptureCount,float exitTime,Cue* out) {
    unsigned count=0;
    auto add=[&](float time,CueKind kind){if(count<kCues&&std::isfinite(time))out[count++]={fmaxf(time,0),uint8_t(kind),false};};
    add(0,HitCue);
    if(clusterCount) {
        unsigned first=0,last=0;
        for(unsigned c=1;c<clusterCount;++c){if(clusters[c].time<clusters[first].time)first=c;if(clusters[c].time>clusters[last].time)last=c;}
        add(clusters[first].time,BoneCue);
        if(kBoneCues>1&&last!=first)add(clusters[last].time,BoneCue);
    }
    if(softCount) {
        float first=soft[0].time;
        for(unsigned s=1;s<softCount;++s)first=fminf(first,soft[s].time);
        add(first,OrganCue);
    }
    for(unsigned r=0;r<ruptureCount;++r)add(ruptures[r].time,BurstCue);
    add(exitTime,SprayCue);
    std::stable_sort(out,out+count,[](const Cue& a,const Cue& b){return a.time<b.time;});
    return count;
}
}
