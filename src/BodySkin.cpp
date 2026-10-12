#include "BodySkin.h"
#include <cfloat>

namespace XRay::BodySkin {
//the body below the neck is in the frame too, so this only rejects corrupt positions
constexpr float kReach=1000;
Skip Add(const Shape& shape,std::vector<uint16_t>& slot,HeadFrame& out) {
    if(!shape.world||!shape.reveal||!shape.indices||shape.indexCount%3)return BadTriangles;
    uint32_t vertexBase=out.skinVertices,triangleBase=out.skinTriangles;
    auto reject=[&](Skip why){out.skinVertices=vertexBase;out.skinTriangles=triangleBase;return why;};
    slot.assign(shape.count,0xFFFF);
    for(uint32_t t=0;t+2<shape.indexCount;t+=3) {
        if(out.skinTriangles>=kSkinTriangles)return reject(OverCapacity);
        for(unsigned k=0;k<3;++k) {
            uint16_t index=shape.indices[t+k];
            if(index>=shape.count)return reject(BadTriangles);
            uint16_t& s=slot[index];
            if(s==0xFFFF) {
                if(out.skinVertices>=kSkinVertices)return reject(OverCapacity);
                Vec3 world=shape.world[index],local=out.head.Unapply(world);float reveal=shape.reveal[index];
                if(!Finite(world)||!Finite(local)||fabsf(local.x)>kReach||fabsf(local.y)>kReach||fabsf(local.z)>kReach
                    ||!std::isfinite(reveal)||reveal<0)return reject(BadVertex);
                s=uint16_t(out.skinVertices);out.skin[out.skinVertices++]={world,local,fminf(reveal,1)};
            }
            out.skinIndices[out.skinTriangles*3+k]=s;
        }
        ++out.skinTriangles;
    }
    return Kept;
}
const char* Describe(uint32_t skip) {
    static const char* const names[]={"none","data missing","bad bone","bad weight","bad triangles","over capacity","bad vertex"};
    return skip<sizeof names/sizeof *names?names[skip]:"unknown";
}
}
namespace XRay::BodySkin {
void OpenOver(const Model& model,HeadFrame& out,bool focused) {
    float nearest=focused?kFocusNear:kOpenNear,farthest=focused?kFocusFar:kOpenFar;
    bool found=false;
    for(const auto& mesh:model.meshes) {
        if(!OpensSkin(mesh.material)||mesh.vertices.empty()||mesh.first>=kInfluences)continue;
        //a focus replaces the region's weights, so the first mesh starts every vertex closed
        if(focused&&!found)for(uint32_t i=0;i<out.skinVertices;++i)out.skin[i].reveal=0;
        found=true;
        Vec3 lo{FLT_MAX,FLT_MAX,FLT_MAX},hi{-FLT_MAX,-FLT_MAX,-FLT_MAX};
        for(const auto& v:mesh.vertices){lo={fminf(lo.x,v.position.x),fminf(lo.y,v.position.y),fminf(lo.z,v.position.z)};hi={fmaxf(hi.x,v.position.x),fmaxf(hi.y,v.position.y),fmaxf(hi.z,v.position.z)};}
        //the box stays in rest space, each skin vertex comes back to it through the mesh's influence
        Transform toRest=Inverse(out.influences[mesh.first]);
        for(uint32_t i=0;i<out.skinVertices;++i) {
            Vec3 p=toRest.Apply(out.skin[i].local);
            Vec3 d{fmaxf(fmaxf(lo.x-p.x,p.x-hi.x),0),fmaxf(fmaxf(lo.y-p.y,p.y-hi.y),0),fmaxf(fmaxf(lo.z-p.z,p.z-hi.z),0)};
            float t=Clamp((farthest-Length(d))/(farthest-nearest),0,1);
            out.skin[i].reveal=fmaxf(out.skin[i].reveal,t*t*(3-2*t));
        }
    }
}
}
namespace XRay {
void SkinBinds::Resolve() {
    found[0]=true;relation[0]=Transform::Identity();
    //direct links to the frame first, then chains through influences already placed
    for(unsigned pass=0;pass<kInfluences;++pass) {
        bool changed=false;
        for(unsigned s=0;s<shapeCount;++s) {
            const auto& shape=shapes[s];
            for(unsigned k=1;k<kInfluences;++k) {
                if(found[k]||!(shape.present>>k&1))continue;
                for(unsigned j=0;j<(pass?kInfluences:1u);++j) {
                    if(!found[j]||!(shape.present>>j&1))continue;
                    relation[k]=Compose(shape.skinToBone[k],Compose(Inverse(shape.skinToBone[j]),relation[j]));
                    found[k]=changed=true;break;
                }
            }
        }
        if(pass&&!changed)break;
    }
}
}
