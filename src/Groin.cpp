#include "Groin.h"
#include "FaceFit.h"
#include <cfloat>

namespace XRay::Groin {
bool Near(const Model& torso,Vec3 point,Vec3 direction,float margin) {
    if(!Normalize(direction))return false;
    for(const auto& mesh:torso.meshes) {
        if(!OpensSkin(mesh.material)||mesh.vertices.empty())continue;
        Vec3 lo{FLT_MAX,FLT_MAX,FLT_MAX},hi{-FLT_MAX,-FLT_MAX,-FLT_MAX};
        for(const auto& v:mesh.vertices){lo={fminf(lo.x,v.position.x),fminf(lo.y,v.position.y),fminf(lo.z,v.position.z)};hi={fmaxf(hi.x,v.position.x),fmaxf(hi.y,v.position.y),fmaxf(hi.z,v.position.z)};}
        Vec3 centre=(lo+hi)*.5f,offset=centre-point;
        float reach=Length(hi-lo)*.5f+margin;
        if(Length(offset-direction*Dot(offset,direction))<=reach)return true;
    }
    return false;
}
static bool Hits(Vec3 origin,Vec3 direction,float length,Vec3 a,Vec3 b,Vec3 c) {
    Vec3 e1=b-a,e2=c-a,p=Cross(direction,e2);float det=Dot(e1,p);
    if(fabsf(det)<1e-9f)return false;
    float inverse=1/det;Vec3 s=origin-a;float u=Dot(s,p)*inverse;if(u<0||u>1)return false;
    Vec3 q=Cross(s,e1);float v=Dot(direction,q)*inverse;if(v<0||u+v>1)return false;
    float t=Dot(e2,q)*inverse;return t>=0&&t<=length;
}
bool Crossed(const Model& torso,const HeadFrame& frame,const Impact::Path& path,float radius) {
    if(!path.valid)return false;
    Vec3 direction=path.exit-path.entry;float length=Length(direction);
    if(!Normalize(direction)||length<.01f)return false;
    Posing posing{HeadFit{},1,{},frame.influences};
    std::vector<Vec3> placed;
    for(const auto& mesh:torso.meshes) {
        if(!OpensSkin(mesh.material)||mesh.vertices.empty())continue;
        placed.resize(mesh.vertices.size());
        for(size_t i=0;i<placed.size();++i) {
            const auto& v=mesh.vertices[i];placed[i]=posing.Place(v.position,v.bind,v.bones);
            //a graze within the bullet's radius counts as well as a crossing
            Vec3 offset=placed[i]-path.entry;float along=Clamp(Dot(offset,direction),0,length);
            if(Length(offset-direction*along)<=radius)return true;
        }
        for(size_t i=0;i+2<mesh.indices.size();i+=3)
            if(Hits(path.entry,direction,length,placed[mesh.indices[i]],placed[mesh.indices[i+1]],placed[mesh.indices[i+2]]))return true;
    }
    return false;
}
}
