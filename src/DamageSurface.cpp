#include "DamageSurface.h"
#include <algorithm>

namespace XRay::Damage {
void Surface::Reserve(const Model& model) {
    bounds_.assign(model.meshes.size(),{});sets_.assign(model.meshes.size(),kNone);
    bindLo_.assign(model.meshes.size(),1);bindHi_.assign(model.meshes.size(),1);
    for(size_t n=0;n<model.meshes.size();++n) {
        const auto& mesh=model.meshes[n];
        if(!(mesh.visibility&1))continue;
        if(FractureKind(mesh.material)!=kNoFracture)sets_[n]=BoneSet;
        else if(SoftKind(mesh.material)!=kNoFracture)sets_[n]=BrainSet;
        else continue;
        auto& b=bounds_[n];
        for(const auto& v:mesh.vertices) {
            b.lo={fminf(b.lo.x,v.position.x),fminf(b.lo.y,v.position.y),fminf(b.lo.z,v.position.z)};
            b.hi={fmaxf(b.hi.x,v.position.x),fmaxf(b.hi.y,v.position.y),fmaxf(b.hi.z,v.position.z)};
            bindLo_[n]=fminf(bindLo_[n],v.bind);bindHi_[n]=fmaxf(bindHi_[n],v.bind);
        }
    }
    for(unsigned set=0;set<kSets;++set) {
        auto& facets=facets_[set];auto& grid=grids_[set];facets.clear();grid=Grid{};
        Bounds b;
        for(size_t n=0;n<model.meshes.size();++n) {
            if(sets_[n]!=set)continue;
            const auto& mesh=model.meshes[n];
            for(size_t i=0;i+2<mesh.indices.size();i+=3) {
                Vec3 a=mesh.vertices[mesh.indices[i]].position,c1=mesh.vertices[mesh.indices[i+1]].position,c2=mesh.vertices[mesh.indices[i+2]].position;
                Vec3 centre=(a+c1+c2)*(1.0f/3);
                float radius=sqrtf(fmaxf(Dot(a-centre,a-centre),fmaxf(Dot(c1-centre,c1-centre),Dot(c2-centre,c2-centre))));
                unsigned part=set==BoneSet?FractureKind(mesh.material):SoftKind(mesh.material);
                facets.push_back({centre,radius,uint32_t(i),uint16_t(n),uint8_t(part),0});
                grid.reach=fmaxf(grid.reach,radius);
                b.lo={fminf(b.lo.x,centre.x),fminf(b.lo.y,centre.y),fminf(b.lo.z,centre.z)};
                b.hi={fmaxf(b.hi.x,centre.x),fmaxf(b.hi.y,centre.y),fmaxf(b.hi.z,centre.z)};
            }
        }
        if(facets.empty())continue;
        grid.origin=b.lo;
        grid.size[0]=int((b.hi.x-b.lo.x)/grid.cell)+1;grid.size[1]=int((b.hi.y-b.lo.y)/grid.cell)+1;grid.size[2]=int((b.hi.z-b.lo.z)/grid.cell)+1;
        auto index=[&](Vec3 p) {
            int x=int((p.x-grid.origin.x)/grid.cell),y=int((p.y-grid.origin.y)/grid.cell),z=int((p.z-grid.origin.z)/grid.cell);
            x=x<0?0:x>=grid.size[0]?grid.size[0]-1:x;y=y<0?0:y>=grid.size[1]?grid.size[1]-1:y;z=z<0?0:z>=grid.size[2]?grid.size[2]-1:z;
            return uint32_t((z*grid.size[1]+y)*grid.size[0]+x);
        };
        size_t cells=size_t(grid.size[0])*grid.size[1]*grid.size[2];
        grid.starts.assign(cells+1,0);grid.facets.resize(facets.size());grid.reaches.assign(cells,0);
        for(const auto& f:facets){auto c=index(f.centre);++grid.starts[c+1];grid.reaches[c]=fmaxf(grid.reaches[c],f.radius);}
        for(size_t c=0;c<cells;++c)grid.starts[c+1]+=grid.starts[c];
        std::vector<uint32_t> cursor(grid.starts.begin(),grid.starts.end()-1);
        for(uint32_t i=0;i<facets.size();++i)grid.facets[cursor[index(facets[i].centre)]++]=i;
    }
}
static bool SlabHit(const Bounds& b,Vec3 o,Vec3 d,float lo,float hi) {
    const float origin[3]={o.x,o.y,o.z},dir[3]={d.x,d.y,d.z},min[3]={b.lo.x,b.lo.y,b.lo.z},max[3]={b.hi.x,b.hi.y,b.hi.z};
    for(unsigned i=0;i<3;++i) {
        if(fabsf(dir[i])<1e-9f){if(origin[i]<min[i]-.01f||origin[i]>max[i]+.01f)return false;continue;}
        float a=(min[i]-.01f-origin[i])/dir[i],c=(max[i]+.01f-origin[i])/dir[i];
        if(a>c)std::swap(a,c);
        lo=fmaxf(lo,a);hi=fminf(hi,c);if(lo>hi)return false;
    }
    return true;
}
unsigned Surface::Trace(const Model& model,Vec3 origin,Vec3 direction,float lo,float hi,Crossing* out,unsigned capacity) const {
    unsigned count=0;
    for(size_t n=0;n<model.meshes.size()&&n<sets_.size();++n) {
        const auto& mesh=model.meshes[n];
        if(sets_[n]==kNone||mesh.bound||!SlabHit(bounds_[n],origin,direction,lo,hi))continue;
        for(size_t i=0;i+2<mesh.indices.size();i+=3) {
            Vec3 a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
            Vec3 e1=b-a,e2=c-a,p=Cross(direction,e2);
            float det=Dot(e1,p);
            if(fabsf(det)<1e-12f)continue;
            float inverse=1/det;Vec3 s=origin-a;
            float u=Dot(s,p)*inverse;if(u<0||u>1)continue;
            Vec3 q=Cross(s,e1);float v=Dot(direction,q)*inverse;if(v<0||u+v>1)continue;
            float t=Dot(e2,q)*inverse;if(!(t>=lo&&t<=hi))continue;
            Vec3 normal=Cross(e1,e2);if(!Normalize(normal))continue;
            if(count<capacity)out[count++]={t,normal,unsigned(n),Dot(normal,direction)<0,origin+direction*t};
        }
    }
    std::sort(out,out+count,[](const Crossing& a,const Crossing& b){return a.t<b.t;});
    return count;
}
unsigned Surface::TraceBound(const Model& model,const Posing& posing,Vec3 entry,Vec3 exit,float length,float lo,float hi,
    Crossing* out,unsigned capacity,std::vector<Vec3>& posed) const {
    Vec3 direction=exit-entry;float span=Length(direction);
    if(!Normalize(direction)||!(length>0))return 0;
    //the same fraction of the path on both lines
    float scale=span/length,from=lo*scale,to=hi*scale;
    unsigned count=0;
    for(size_t n=0;n<model.meshes.size()&&n<sets_.size();++n) {
        const auto& mesh=model.meshes[n];
        if(sets_[n]==kNone||!mesh.bound)continue;
        //the blend is affine for each weight and the warp nearly so, the corners at both weight limits bound the posed mesh
        Bounds hull;const auto& d=bounds_[n];
        for(unsigned k=0;k<16;++k) {
            Vec3 corner={k&1?d.hi.x:d.lo.x,k&2?d.hi.y:d.lo.y,k&4?d.hi.z:d.lo.z};
            Vec3 p=posing.Place(corner,k&8?bindHi_[n]:bindLo_[n],mesh.vertices[0].bones);
            hull.lo={fminf(hull.lo.x,p.x-2),fminf(hull.lo.y,p.y-2),fminf(hull.lo.z,p.z-2)};
            hull.hi={fmaxf(hull.hi.x,p.x+2),fmaxf(hull.hi.y,p.y+2),fmaxf(hull.hi.z,p.z+2)};
        }
        if(!SlabHit(hull,entry,direction,from,to))continue;
        posed.resize(mesh.vertices.size());
        Bounds b;
        for(size_t i=0;i<mesh.vertices.size();++i) {
            Vec3 p=posing.Place(mesh.vertices[i].position,mesh.vertices[i].bind,mesh.vertices[i].bones);posed[i]=p;
            b.lo={fminf(b.lo.x,p.x),fminf(b.lo.y,p.y),fminf(b.lo.z,p.z)};b.hi={fmaxf(b.hi.x,p.x),fmaxf(b.hi.y,p.y),fmaxf(b.hi.z,p.z)};
        }
        if(!SlabHit(b,entry,direction,from,to))continue;
        for(size_t i=0;i+2<mesh.indices.size();i+=3) {
            uint16_t ia=mesh.indices[i],ib=mesh.indices[i+1],ic=mesh.indices[i+2];
            Vec3 a=posed[ia],e1=posed[ib]-a,e2=posed[ic]-a,p=Cross(direction,e2);
            float det=Dot(e1,p);
            if(fabsf(det)<1e-12f)continue;
            float inverse=1/det;Vec3 s=entry-a;
            float u=Dot(s,p)*inverse;if(u<0||u>1)continue;
            Vec3 q=Cross(s,e1);float v=Dot(direction,q)*inverse;if(v<0||u+v>1)continue;
            float t=Dot(e2,q)*inverse;if(!(t>=from&&t<=to))continue;
            Vec3 da=mesh.vertices[ia].position,db=mesh.vertices[ib].position,dc=mesh.vertices[ic].position;
            Vec3 normal=Cross(db-da,dc-da);if(!Normalize(normal))continue;
            if(count<capacity)out[count++]={t/scale,normal,unsigned(n),Dot(Cross(e1,e2),direction)<0,da+(db-da)*u+(dc-da)*v};
        }
    }
    return count;
}
}
