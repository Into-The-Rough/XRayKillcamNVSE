#include "Engine.h"
#include "ShotTrace.h"
#include <cstring>

namespace XRay::Engine {
static const char *s_head=nullptr,*s_face=nullptr,*s_neck=nullptr,*s_spine=nullptr,*s_creatureNeck=nullptr;
Trace::View CaptureShotView() {
    Trace::View out;
    //jip scenegraph::camera, 0x45bba0 reads world column 0, no nvse snapshot primitive
    auto graph=GetSceneGraph();
    if(!graph||!graph->camera)return out;
    const auto pose=graph->camera->world;
    if(!pose.Valid())return out;
    out.origin=pose.t;out.forward=pose.Rotate({1,0,0});out.valid=Normalize(out.forward);return out;
}
bool InitNames() {
    auto intern=reinterpret_cast<const char*(__cdecl*)(const char*)>(0xA5B690);
    //createfacegenhead renames part 0 at 0x654b8a, using the table read by 0x605d40
    s_head=intern("Bip01 Head");s_face=intern("FaceGenFace");s_neck=intern("Bip01 Neck1");s_spine=intern("Bip01 Spine2");
    s_creatureNeck=intern("Bip01 Neck");
    return s_head&&s_face&&s_neck&&s_spine&&s_creatureNeck;
}
static Object* Find(Object* root,const char* name) {
    return reinterpret_cast<Object*(__thiscall*)(Object*,const char**)>(root->vtable[0x9C/4])(root,&name);
}
Object* FindBone(Object* root,const char* interned) {
    auto bone=Find(root,interned);
    return bone||interned!=s_neck?bone:Find(root,s_creatureNeck);
}
bool FramePose(Form* form,uint32_t region,Transform& out) {
    if(!Supported(form))return false;
    const char* name=region==HeadRegion?s_head:RegionFrameName(AnatomyOf(KindOf(form)),region);
    if(!name)return false;
    auto root=reinterpret_cast<Object*(__thiscall*)(Form*)>(form->vtable[0x1D0/4])(form);
    if(!root||(root->flags&kHidden))return false;
    auto bone=FindBone(root,name);
    if(!bone||(bone->flags&kHidden)||!bone->world.Valid())return false;
    out=bone->world;return true;
}
Impact::Sample CaptureImpact(Form* target,Form* projectile,Vec3 point,bool capturePose,uint32_t region) {
    Impact::Sample sample;sample.point=point;
    //native getter copies projectile+0x104, ret 4, hitdata+0x44 is blended with the contact normal
    reinterpret_cast<Vec3*(__thiscall*)(Form*,Vec3*)>(0x9B7010)(projectile,&sample.direction);
    Vec3 stored=sample.direction;Trace::Ray ray;
    if(Trace::Read(projectile->id,reinterpret_cast<uintptr_t>(projectile),GetTickCount64(),ray)) {
        sample.direction=ray.direction;sample.rayOrigin=ray.origin;sample.traced=true;
        if(Normalize(stored))sample.headingChange=acosf(Clamp(Dot(stored,ray.direction),-1,1))*57.2957795f;
    }
    sample.valid=Finite(point)&&Normalize(sample.direction);
    sample.ammo=ProjectileAmmoID(projectile);sample.bullet=Ammunition::Resolve(sample.ammo);
    if(sample.valid&&capturePose)sample.posed=FramePose(target,region,sample.pose);
    if(sample.posed&&region!=HeadRegion)HitCarrier(target,region,point,sample.carrier,sample.carrierPose);
    return sample;
}
static uint64_t Reject(HeadFailure failure,uint32_t value=0){return CaptureFailure(failure,value);}
const char* DescribeHeadFailure(HeadFailure failure) {
    static const char* const messages[]={
        "none","reference missing","wrong reference type","reference deleted","child race/model",
        "3D root missing","3D root hidden","Bip01 Head missing","FaceGenFace missing",
        "head bone hidden","face geometry hidden","invalid head transform","face is not geometry",
        "geometry data missing","skin instance missing","data is not a triangle shape",
        "vertex count must be 1211","triangle count must be 2294","index count must be 6882",
        "CPU positions missing","CPU indices missing","skin data missing","bone count outside 1..32",
        "bone data missing","bone pointer array missing","skin root missing","invalid skin root transform",
        "invalid root-to-skin transform","bone pointer missing (bone index)","invalid bone transform (bone index)",
        "invalid bind transform (bone index)","bone weight count exceeds vertex count","weights missing (bone index)",
        "weight vertex index out of bounds","invalid weight (vertex index)","skin does not use Bip01 Head",
        "nonfinite source position (vertex index)","triangle vertex index out of bounds","invalid posed position (vertex index)",
        "region bone missing or invalid (influence index)","invalid rest relation between a region bone and its frame bone (influence index)","no body or armour skin bound to the region's bones"
    };
    static_assert(sizeof messages/sizeof *messages==static_cast<unsigned>(HeadFailure::MissingRegionSkin)+1);
    auto index=static_cast<unsigned>(failure);
    return index<sizeof messages/sizeof *messages?messages[index]:"unknown capture failure";
}
//an adult human or ghoul whose FaceGen face has the supported topology
uint64_t ResolveHuman(uint32_t refID,Object*& root,Object*& head,Geometry*& geometry) {
    auto form=Lookup(refID);
    if(!form)return Reject(HeadFailure::MissingReference);
    if(form->type!=kCharacter)return Reject(HeadFailure::WrongReferenceType,form->type);
    if(form->flags&kDeleted)return Reject(HeadFailure::DeletedReference,form->flags);
    //true skips the height heuristic at 0x8d4196, short adult actors are not child races
    if(reinterpret_cast<bool(__thiscall*)(Form*,bool)>(form->vtable[0x1A0/4])(form,true))return Reject(HeadFailure::Child);
    root=reinterpret_cast<Object*(__thiscall*)(Form*)>(form->vtable[0x1D0/4])(form);
    if(!root)return Reject(HeadFailure::MissingRoot);
    if(root->flags&kHidden)return Reject(HeadFailure::HiddenRoot,root->flags);
    head=Find(root,s_head);auto face=Find(root,s_face);
    if(!head)return Reject(HeadFailure::MissingHead);
    if(!face)return Reject(HeadFailure::MissingFace);
    if(head->flags&kHidden)return Reject(HeadFailure::HiddenHead,head->flags);
    if(face->flags&kHidden)return Reject(HeadFailure::HiddenFace,face->flags);
    if(!head->world.Valid())return Reject(HeadFailure::HeadTransform);
    geometry=reinterpret_cast<Geometry*(__thiscall*)(Object*)>(face->vtable[0x18/4])(face);
    if(!geometry)return Reject(HeadFailure::NotGeometry);
    if(!geometry->data)return Reject(HeadFailure::MissingGeometryData);
    if(!geometry->skin)return Reject(HeadFailure::MissingSkin);
    auto data=geometry->data;
    if(!reinterpret_cast<ShapeData*(__thiscall*)(ShapeData*)>(data->vtable[0x98/4])(data))return Reject(HeadFailure::NotTriangleShape);
    if(data->count!=kHeadVertices)return Reject(HeadFailure::VertexCount,data->count);
    if(data->triangles!=kHeadTriangles)return Reject(HeadFailure::TriangleCount,data->triangles);
    if(data->indexCount!=kHeadTriangles*3)return Reject(HeadFailure::IndexCount,data->indexCount);
    return 0;
}
uint64_t CaptureHead(uint32_t refID,HeadFrame& out) {
    //a creature's head is part of its body mesh, captured like a region by its bone weights
    auto kind=KindOf(Lookup(refID));
    if(kind!=Kind::Human&&kind!=Kind::None)return CaptureRegion(refID,HeadRegion,0,out);
    Object *root=nullptr,*head=nullptr;Geometry* geometry=nullptr;
    if(auto failure=ResolveHuman(refID,root,head,geometry))return failure;
    auto data=geometry->data;
    if(!data->positions)return Reject(HeadFailure::MissingPositions);
    if(!data->indices)return Reject(HeadFailure::MissingIndices);
    auto skin=geometry->skin;auto sd=skin->data;
    if(!sd)return Reject(HeadFailure::MissingSkinData);
    if(!sd->count||sd->count>32)return Reject(HeadFailure::BoneCount,sd->count);
    if(!sd->bones)return Reject(HeadFailure::MissingBoneData);
    if(!skin->bones)return Reject(HeadFailure::MissingBoneArray);
    if(!skin->root)return Reject(HeadFailure::MissingSkinRoot);
    if(!skin->root->world.Valid())return Reject(HeadFailure::RootTransform);
    if(!sd->rootToSkin.Valid())return Reject(HeadFailure::SkinTransform);
    unsigned headBone=~0u,neckBone=~0u;
    auto neck=Find(root,s_neck);
    for(unsigned i=0;i<sd->count;++i) {
        const auto& bone=sd->bones[i];
        if(!skin->bones[i])return Reject(HeadFailure::MissingBone,i);
        if(!skin->bones[i]->world.Valid())return Reject(HeadFailure::BoneTransform,i);
        if(!bone.skinToBone.Valid())return Reject(HeadFailure::BindTransform,i);
        if(bone.count>kHeadVertices)return Reject(HeadFailure::WeightCount,bone.count);
        if(bone.count&&!bone.weights)return Reject(HeadFailure::MissingWeights,i);
        if(skin->bones[i]==head)headBone=i;
        if(neck&&skin->bones[i]==neck)neckBone=i;
        for(unsigned j=0;j<bone.count;++j) {
            if(bone.weights[j].vertex>=kHeadVertices)return Reject(HeadFailure::WeightIndex,bone.weights[j].vertex);
            if(!std::isfinite(bone.weights[j].weight)||bone.weights[j].weight<0||bone.weights[j].weight>1)
                return Reject(HeadFailure::WeightValue,bone.weights[j].vertex);
        }
    }
    if(headBone==~0u)return Reject(HeadFailure::MissingHeadInfluence);
    for(unsigned i=0;i<kHeadVertices;++i)if(!Finite(data->positions[i]))return Reject(HeadFailure::VertexValue,i);
    for(unsigned i=0;i<kHeadTriangles*3;++i)if(data->indices[i]>=kHeadVertices)return Reject(HeadFailure::TriangleIndex,data->indices[i]);
    Vec3 posed[kHeadVertices];
    //native deform has eight stack arguments (ret 0x20), nvse exposes no equivalent method
    reinterpret_cast<void(__thiscall*)(Skin*,const Vec3*,const Vec3*,uint32_t,Vec3*,Vec3*,Vec3*,Vec3*,uint32_t)>(0xA855F0)
        (skin,data->positions,nullptr,kHeadVertices,posed,nullptr,nullptr,nullptr,sizeof(Vec3));
    out.head=head->world;
    for(unsigned i=0;i<kHeadVertices;++i) {
        //deform emits rootToSkin * inverse(root.world) * world positions
        Vec3 world=skin->root->world.Apply(sd->rootToSkin.Unapply(posed[i]));
        Vec3 local=out.head.Unapply(world);
        if(!Finite(world)||!Finite(local)||fabsf(local.x)>60||fabsf(local.y)>60||fabsf(local.z)>60)return Reject(HeadFailure::PosedVertex,i);
        out.vertices[i]={world,local};
    }
    memcpy(out.indices,data->indices,sizeof out.indices);
    Fit::Update(out.vertices,out.fit);
    float headWeight[kHeadVertices]{},neckWeight[kHeadVertices]{};
    for(unsigned j=0;j<sd->bones[headBone].count;++j)headWeight[sd->bones[headBone].weights[j].vertex]=sd->bones[headBone].weights[j].weight;
    if(neckBone!=~0u)for(unsigned j=0;j<sd->bones[neckBone].count;++j)neckWeight[sd->bones[neckBone].weights[j].vertex]=sd->bones[neckBone].weights[j].weight;
    for(unsigned i=0;i<kHeadVertices;++i)out.vertices[i].reveal=fminf(headWeight[i]+neckWeight[i],1);
    out.influences=RestInfluences();out.posedBones=RestInfluences();out.neckError=-1;out.region=HeadRegion;out.face=true;out.anatomy=HumanAnatomy;
    auto actor=Lookup(refID);out.noGenitals=actor&&IsFemale(actor);
    if(neckBone!=~0u) {
        //skinned world = bone.world * skinToBone * skin position, so rest head space reaches posed neck space through skin space
        const auto& headBind=sd->bones[headBone].skinToBone;
        Transform bend=Compose(Inverse(out.head),Compose(neck->world,Compose(sd->bones[neckBone].skinToBone,Inverse(headBind))));
        if(bend.Valid()) {
            out.influences[1]=bend;
            float error=0;
            for(unsigned i=0;i<kHeadVertices;++i) {
                if(neckWeight[i]<.2f||headWeight[i]+neckWeight[i]<.999f)continue;
                Vec3 rest=headBind.Apply(data->positions[i]);
                Vec3 predicted=rest*headWeight[i]+bend.Apply(rest)*neckWeight[i];
                error=fmaxf(error,Length(predicted-out.vertices[i].local));
            }
            out.neckError=error;
        }
    }
    //body and armour skin bound across the neck joint closes the face's open collar
    SkinRequest request;request.required[0]=neck;request.required[1]=Find(root,s_spine);request.requiredCount=neck&&request.required[1]?2:0;
    request.reveal[0]=head;request.reveal[1]=neck;request.revealCount=2;
    CaptureSkin(root,geometry,request,out);
    return 0;
}
}
