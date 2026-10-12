#include "Engine.h"
#include "BodySkin.h"
#include <vector>

namespace XRay::Engine {
namespace {
struct Scratch {std::vector<Vec3> posed,world;std::vector<float> reveal;std::vector<uint16_t> slot,triangles;};
static SkinSkipNote s_note;
struct Collector {
    const Geometry* face;const SkinRequest& request;HeadFrame& out;SkinBinds* binds;Scratch& scratch;unsigned visited=0;
    void Walk(Object* object,unsigned depth);
    bool Triangles(const Skin* skin,const ShapeData* data);
    void Shape(Geometry* geometry);
};
//the game draws only enabled partitions, so a dismembered piece or an unused cap never counts as skin
bool Collector::Triangles(const Skin* skin,const ShapeData* data) {
    auto& triangles=scratch.triangles;triangles.clear();
    auto rtti=reinterpret_cast<uintptr_t(__thiscall*)(const Skin*)>(skin->vtable[0x08/4])(skin);
    auto dismember=rtti==kDismemberRTTI?reinterpret_cast<const DismemberSkin*>(skin):nullptr;
    bool every=true;
    if(dismember) {
        if(!dismember->renderable)return true;
        if(dismember->count&&!dismember->entries)return false;
        for(unsigned i=0;i<dismember->count;++i)every&=dismember->entries[i].enabled!=0;
    }
    if(every) {
        if(!data->indices||data->indexCount!=uint32_t(data->triangles)*3)return false;
        triangles.assign(data->indices,data->indices+data->indexCount);
        return true;
    }
    auto set=skin->partition;
    if(!set||set->count!=dismember->count||!set->partitions)return false;
    for(unsigned p=0;p<set->count;++p) {
        if(!dismember->entries[p].enabled)continue;
        const auto& part=set->partitions[p];
        if(!part.vertexMap||!part.indices)return false;
        auto add=[&](uint16_t a,uint16_t b,uint16_t c) {
            if(a>=part.vertices||b>=part.vertices||c>=part.vertices)return false;
            triangles.push_back(part.vertexMap[a]);triangles.push_back(part.vertexMap[b]);triangles.push_back(part.vertexMap[c]);
            return true;
        };
        if(part.strips) {
            if(!part.stripLengths)return false;
            const uint16_t* strip=part.indices;
            for(unsigned s=0;s<part.strips;strip+=part.stripLengths[s++])
                for(unsigned k=0;k+2<part.stripLengths[s];++k) {
                    uint16_t a=strip[k],b=strip[k+1],c=strip[k+2];
                    if(a==b||b==c||a==c)continue;
                    if(!(k&1?add(a,c,b):add(a,b,c)))return false;
                }
        }
        else for(unsigned t=0;t<part.triangles;++t)if(!add(part.indices[t*3],part.indices[t*3+1],part.indices[t*3+2]))return false;
    }
    return true;
}
void Collector::Shape(Geometry* geometry) {
    if(geometry==face||!geometry->skin||!geometry->data)return;
    //blended shapes never write scene depth, so the mask could not test them against the visible surface
    if(geometry->alpha&&(geometry->alpha->flags&1))return;
    auto skin=geometry->skin;auto sd=skin->data;auto data=geometry->data;
    if(!sd||!sd->bones||!skin->bones||!sd->count||sd->count>255)return;
    auto indexOf=[&](const Object* bone){for(unsigned i=0;i<sd->count;++i)if(bone&&skin->bones[i]==bone)return i;return ~0u;};
    unsigned matched=0;
    for(unsigned r=0;r<request.requiredCount;++r)matched+=indexOf(request.required[r])!=~0u;
    if(request.requireAll?matched<request.requiredCount:!matched)return;
    auto skip=[&](BodySkin::Skip why){++out.skinSkipped;if(!s_note.reason)s_note={why,data->count};};
    if(!reinterpret_cast<ShapeData*(__thiscall*)(ShapeData*)>(data->vtable[0x98/4])(data)||!data->count||!data->positions
        ||!skin->root||!skin->root->world.Valid()||!sd->rootToSkin.Valid())return skip(BodySkin::DataMissing);
    unsigned count=data->count;
    auto& reveal=scratch.reveal;auto& triangles=scratch.triangles;auto& posed=scratch.posed;auto& world=scratch.world;
    reveal.assign(count,0);
    for(unsigned i=0;i<sd->count;++i) {
        const auto& bone=sd->bones[i];
        if(!skin->bones[i]||!skin->bones[i]->world.Valid()||!bone.skinToBone.Valid()||(bone.count&&!bone.weights))return skip(BodySkin::BadBone);
        bool counted=false;for(unsigned r=0;r<request.revealCount;++r)counted|=skin->bones[i]==request.reveal[r];
        for(unsigned j=0;j<bone.count;++j) {
            const auto& w=bone.weights[j];
            if(w.vertex>=count||!std::isfinite(w.weight)||w.weight<0||w.weight>1)return skip(BodySkin::BadWeight);
            if(counted)reveal[w.vertex]+=w.weight;
        }
    }
    if(binds&&request.crown) {
        //a creature's own head is the skin bound across its neck, worn pieces bind the head alone
        unsigned frame=indexOf(request.influences[0]),neck=indexOf(request.influences[1]);
        if(frame!=~0u&&neck!=~0u) {
            const auto& bone=sd->bones[frame];
            for(unsigned j=0;j<bone.count;++j)
                if(bone.weights[j].weight>=.5f)binds->crown=fmaxf(binds->crown,bone.skinToBone.Apply(data->positions[bone.weights[j].vertex]).x);
        }
    }
    if(binds&&binds->shapeCount<kBindShapes) {
        auto& shape=binds->shapes[binds->shapeCount];shape.present=0;
        for(unsigned k=0;k<kInfluences;++k) {
            unsigned index=indexOf(request.influences[k]);
            if(index!=~0u){shape.present|=1u<<k;shape.skinToBone[k]=sd->bones[index].skinToBone;}
        }
        //a shape bound to fewer than two influences links nothing
        if(shape.present&(shape.present-1))++binds->shapeCount;
    }
    if(!Triangles(skin,data))return skip(BodySkin::BadTriangles);
    if(triangles.empty())return;
    posed.resize(count);world.resize(count);
    //same native deform as the face, the partitions only change what the game draws
    reinterpret_cast<void(__thiscall*)(Skin*,const Vec3*,const Vec3*,uint32_t,Vec3*,Vec3*,Vec3*,Vec3*,uint32_t)>(0xA855F0)
        (skin,data->positions,nullptr,count,posed.data(),nullptr,nullptr,nullptr,sizeof(Vec3));
    for(unsigned i=0;i<count;++i)world[i]=skin->root->world.Apply(sd->rootToSkin.Unapply(posed[i]));
    auto why=BodySkin::Add({world.data(),reveal.data(),count,triangles.data(),uint32_t(triangles.size())},scratch.slot,out);
    if(why)skip(why);else ++out.skinShapes;
}
void Collector::Walk(Object* object,unsigned depth) {
    if(!object||depth>32||++visited>4096||(object->flags&kHidden))return;
    if(auto node=reinterpret_cast<Node*(__thiscall*)(Object*)>(object->vtable[0x0C/4])(object)) {
        if(!node->children)return;
        for(unsigned i=0;i<node->count;++i)Walk(node->children[i],depth+1);
    }
    else if(auto geometry=reinterpret_cast<Geometry*(__thiscall*)(Object*)>(object->vtable[0x18/4])(object))Shape(geometry);
}
}
void CaptureSkin(Object* root,const Geometry* face,const SkinRequest& request,HeadFrame& out,SkinBinds* binds) {
    out.skinVertices=out.skinTriangles=out.skinShapes=out.skinSkipped=0;s_note={};
    if(binds)*binds={};
    if(!root||!request.requiredCount)return;
    //capture runs only inside the world render hook
    static Scratch s_scratch;
    Collector{face,request,out,binds,s_scratch}.Walk(root,0);
    if(binds)binds->Resolve();
}
SkinSkipNote LastSkinSkip(){return s_note;}
}
