#include "Debris.h"
#include <cstring>

namespace XRay::Damage {
static float Ease(float t){t=Clamp(t,0,1);return t*t*(3-2*t);}
void Debris::Clear() {
    for(auto& burst:bursts_){burst.count=0;for(auto& part:burst.parts)part={};}
    next_=0;++revision_;
}
unsigned Debris::Commit(const Transform& pose,float born,const PieceMotion* pieces,unsigned count,const std::vector<PieceVertex>* parts) {
    if(vertices_.empty()||!count)return ~0u;
    unsigned first=next_;
    for(unsigned from=0;from<count&&from<kWoundPieces;from+=kBurstPieces) {
        unsigned index=next_;next_=(next_+1)%kBursts;
        auto& burst=bursts_[index];burst.count=count-from<kBurstPieces?count-from:kBurstPieces;
        memcpy(burst.pieces,pieces+from,burst.count*sizeof(PieceMotion));
        burst.pose=pose;burst.born=born;
        unsigned used=0,base=index*kBurstVertices;
        //every triangle belongs to one piece, so filtering by piece keeps triangles whole
        for(unsigned part=0;part<kParts;++part) {
            unsigned start=used;
            for(const auto& v:parts[part]) {
                unsigned piece=unsigned(v.piece+.5f);
                if(piece<from||piece>=from+burst.count)continue;
                if(used==kBurstVertices)break;
                auto& out=vertices_[base+used++];out=v;out.piece=float(piece-from);
            }
            used=start+(used-start)/3*3;
            burst.parts[part]={base+start,used-start};
        }
    }
    ++revision_;return first;
}
bool Debris::Live(unsigned burst,float clock) const {
    const auto& b=bursts_[burst];
    for(unsigned i=0;i<b.count;++i)if(clock-b.born-b.pieces[i].time<=b.pieces[i].life)return true;
    return false;
}
unsigned Debris::LiveVertices(float clock) const {
    unsigned total=0;
    for(unsigned burst=0;burst<kBursts;++burst)if(Live(burst,clock))for(const auto& part:bursts_[burst].parts)total+=part.count;
    return total;
}
unsigned Debris::Transforms(unsigned burst,float clock,Vec3 origin,float* rows) const {
    const auto& b=bursts_[burst];const auto& pose=b.pose;
    for(unsigned i=0;i<b.count;++i) {
        const auto& p=b.pieces[i];float* row=rows+i*12;
        float age=clock-b.born-p.time;
        if(!(age>=0&&age<=p.life)||!std::isfinite(clock)){memset(row,0,12*sizeof(float));continue;}
        float flight=fmaxf(0,age-.006f),shrink=1-Ease((age-p.life*.7f)/(p.life*.3f));
        float c=cosf(flight*p.spin),s=sinf(flight*p.spin);
        auto rotate=[&](Vec3 v){return v*c+Cross(p.axis,v)*s+p.axis*(Dot(p.axis,v)*(1-c));};
        //world(x)=pose(center+R(x-center)*shrink)+shift, split into a matrix and a camera-relative offset
        Vec3 columns[3]={pose.Rotate(rotate({1,0,0}))*(pose.scale*shrink),pose.Rotate(rotate({0,1,0}))*(pose.scale*shrink),
            pose.Rotate(rotate({0,0,1}))*(pose.scale*shrink)};
        Vec3 shift=pose.Rotate(p.velocity)*(pose.scale*flight)+Vec3{0,0,-80*flight*flight};
        Vec3 offset=pose.Rotate(p.center-rotate(p.center)*shrink)*pose.scale+(pose.t-origin)+shift;
        const float values[12]={columns[0].x,columns[1].x,columns[2].x,offset.x,columns[0].y,columns[1].y,columns[2].y,offset.y,
            columns[0].z,columns[1].z,columns[2].z,offset.z};
        memcpy(row,values,sizeof values);
    }
    return b.count;
}
}
