#include "Engine.h"
#include "HumanRest.h"
#include "Proportions.h"
#include <cfloat>
static_assert(sizeof XRay::kHumanRest/sizeof *XRay::kHumanRest==XRay::kRegions,"a human rest row for every region");

namespace XRay::Engine {
namespace {
//one body region below the neck: its frame bone first, the influences in the order its anatomy build uses,
//the bones a shape must use to count as its skin, and the bones whose weight opens the reveal for each side
struct Spec {
    const char* influences[kInfluences];
    //an outfit not bound to a bone moves that bone's anatomy with its parent
    unsigned parents[kInfluences];
    const char* required[kSkinBones];
    const char* reveal[2][kSkinBones];
};
const Spec kSpecs[]={
    {{"Bip01 Spine1","Bip01 Spine2","Bip01 Spine","Bip01 Pelvis","Bip01 Neck1","Bip01 L Clavicle","Bip01 R Clavicle"},{0,0,0,2,1,1,1},
        {"Bip01 Spine","Bip01 Spine1","Bip01 Spine2","Bip01 Pelvis"},
        {{"Bip01 Pelvis","Bip01 Spine","Bip01 Spine1","Bip01 Spine2","Bip01 L Clavicle","Bip01 R Clavicle"},
         {"Bip01 Pelvis","Bip01 Spine","Bip01 Spine1","Bip01 Spine2","Bip01 L Clavicle","Bip01 R Clavicle"}}},
    {{"Bip01 Pelvis","Bip01 L Thigh","Bip01 L Calf","Bip01 L Foot","Bip01 R Thigh","Bip01 R Calf","Bip01 R Foot"},{0,0,1,2,0,4,5},
        {"Bip01 L Thigh","Bip01 L Calf","Bip01 L Foot","Bip01 R Thigh","Bip01 R Calf","Bip01 R Foot"},
        {{"Bip01 L Thigh","Bip01 L Calf","Bip01 L Foot","Bip01 L Toe0"},{"Bip01 R Thigh","Bip01 R Calf","Bip01 R Foot","Bip01 R Toe0"}}},
    //finger weight stays out of the reveal, the anatomy has no finger bones to follow a grip
    {{"Bip01 Spine2","Bip01 L UpperArm","Bip01 L Forearm","Bip01 L Hand","Bip01 R UpperArm","Bip01 R Forearm","Bip01 R Hand"},{0,0,1,2,0,4,5},
        {"Bip01 L UpperArm","Bip01 L Forearm","Bip01 L Hand","Bip01 R UpperArm","Bip01 R Forearm","Bip01 R Hand"},
        {{"Bip01 L UpperArm","Bip01 LUpArmTwistBone","Bip01 L Forearm","Bip01 L ForeTwist","Bip01 L Hand"},
         {"Bip01 R UpperArm","Bip01 RUpArmTwistBone","Bip01 R Forearm","Bip01 R ForeTwist","Bip01 R Hand"}}},
};
constexpr unsigned kSpecCount=sizeof kSpecs/sizeof *kSpecs;
static_assert(kSpecCount==kRegions-1,"every region below the head has a spec");
//a creature's head is part of its body mesh: its head bone and neck, and the head, jaw and neck weight opening the reveal
const Spec kHeadSpec={{"Bip01 Head","Bip01 Neck1"},{0,0,0,0,0,0,0},{"Bip01 Head"},
    {{"Bip01 Head","Bip01 Head2","Bip01 Neck1"},{"Bip01 Head","Bip01 Head2","Bip01 Neck1"}}};
struct Names {
    const char* influences[kInfluences];const char* required[kSkinBones];const char* reveal[2][kSkinBones];
    const char* reach[kInfluences];const char* chain[kChainBones];const char* widths[2][2];
};
Names s_names[kSpecCount]{},s_headNames{};
const Spec& SpecOf(uint32_t region){return region==HeadRegion?kHeadSpec:kSpecs[region-1];}
Names& NamesOf(uint32_t region){return region==HeadRegion?s_headNames:s_names[region-1];}
//a creature's spans on its live skeleton, in its frame bone's units, and its head's crown from the skin capture
Spans MeasureSpans(Object* root,uint32_t region,Object* const* bones,const SkinBinds& binds) {
    Spans spans;const auto& names=NamesOf(region);
    float unit=bones[0]->world.scale;
    auto distance=[&](const Object* a,const Object* b){return a&&b&&a->world.Valid()&&b->world.Valid()?Length(b->world.t-a->world.t)/unit:0.f;};
    for(unsigned k=0;k<kInfluences;++k)if(names.reach[k]&&names.influences[k])spans.reach[k]=distance(bones[k],FindBone(root,names.reach[k]));
    //a missing link leaves the chain unmeasured rather than short
    const Object* last=nullptr;float chain=0;
    for(unsigned i=0;i<kChainBones&&names.chain[i];++i) {
        auto bone=FindBone(root,names.chain[i]);
        if(!bone||!bone->world.Valid()){chain=0;break;}
        if(last)chain+=distance(last,bone);
        last=bone;
    }
    spans.chain=chain;
    for(unsigned w=0;w<2;++w)if(names.widths[w][0])spans.widths[w]=distance(FindBone(root,names.widths[w][0]),FindBone(root,names.widths[w][1]));
    spans.crown=binds.crown;
    return spans;
}
uint64_t ResolveCreature(Form* form,Object*& root) {
    if(!form)return CaptureFailure(HeadFailure::MissingReference);
    if(!Supported(form))return CaptureFailure(HeadFailure::WrongReferenceType,form->type);
    root=reinterpret_cast<Object*(__thiscall*)(Form*)>(form->vtable[0x1D0/4])(form);
    if(!root)return CaptureFailure(HeadFailure::MissingRoot);
    if(root->flags&kHidden)return CaptureFailure(HeadFailure::HiddenRoot,root->flags);
    return 0;
}
}
const char* RegionFrameName(uint32_t region){return region<kRegions?NamesOf(region).influences[0]:nullptr;}
bool InitRegionNames() {
    auto intern=reinterpret_cast<const char*(__cdecl*)(const char*)>(0xA5B690);
    auto copy=[&](const char* name,const char*& out){if(!name){out=nullptr;return true;}return (out=intern(name))!=nullptr;};
    for(uint32_t region=0;region<kRegions;++region) {
        const auto& spec=SpecOf(region);auto& names=NamesOf(region);
        for(unsigned k=0;k<kInfluences;++k)if(!copy(spec.influences[k],names.influences[k]))return false;
        for(unsigned k=0;k<kSkinBones;++k)if(!copy(spec.required[k],names.required[k]))return false;
        for(unsigned side=0;side<2;++side)for(unsigned k=0;k<kSkinBones;++k)if(!copy(spec.reveal[side][k],names.reveal[side][k]))return false;
        for(unsigned k=0;k<kInfluences;++k)if(!copy(kReachBones[region][k],names.reach[k]))return false;
        for(unsigned k=0;k<kChainBones;++k)if(!copy(kChainBoneNames[region][k],names.chain[k]))return false;
        for(unsigned w=0;w<2;++w)for(unsigned e=0;e<2;++e)if(!copy(kWidthBones[region][w][e],names.widths[w][e]))return false;
    }
    return true;
}
uint64_t CaptureRegion(uint32_t refID,uint32_t region,unsigned side,HeadFrame& out) {
    if(region>=kRegions||side>1)return CaptureFailure(HeadFailure::MissingRegionBone);
    auto actor=Lookup(refID);auto kind=KindOf(actor);
    Object *root=nullptr,*head=nullptr;Geometry* face=nullptr;
    if(kind==Kind::Human) {
        //a human head has its own face capture, the regions below keep the face out of their skin
        if(region==HeadRegion)return CaptureFailure(HeadFailure::MissingRegionBone);
        if(auto failure=ResolveHuman(refID,root,head,face))return failure;
    }else if(auto failure=ResolveCreature(actor,root))return failure;
    const auto& spec=SpecOf(region);const auto& names=NamesOf(region);
    Object* bones[kInfluences];
    for(unsigned k=0;k<kInfluences;++k) {
        //an influence the region does not name rides on its parent
        if(!names.influences[k]){bones[k]=bones[spec.parents[k]];continue;}
        bones[k]=FindBone(root,names.influences[k]);
        if(!bones[k]||!bones[k]->world.Valid())return CaptureFailure(HeadFailure::MissingRegionBone,k);
    }
    out.head=bones[0]->world;out.fit=HeadFit{};out.face=false;out.region=region;out.neckError=-1;
    out.noGenitals=kind!=Kind::Human||IsFemale(actor);
    for(unsigned k=0;k<kInfluences;++k)out.posedBones[k]=Compose(Inverse(out.head),bones[k]->world);
    SkinRequest request;request.requireAll=false;request.crown=region==HeadRegion&&kind!=Kind::Human;
    for(unsigned k=0;k<kSkinBones&&names.required[k];++k)if(auto bone=FindBone(root,names.required[k]))request.required[request.requiredCount++]=bone;
    for(unsigned k=0;k<kSkinBones&&names.reveal[side][k];++k)if(auto bone=FindBone(root,names.reveal[side][k]))request.reveal[request.revealCount++]=bone;
    request.frame=bones[0];for(unsigned k=0;k<kInfluences;++k)request.influences[k]=names.influences[k]?bones[k]:nullptr;
    //about 12 KB of link tables, the capture runs only inside the world render hook
    static SkinBinds binds;CaptureSkin(root,face,request,out,&binds);
    if(!out.skinTriangles)return CaptureFailure(HeadFailure::MissingRegionSkin);
    out.influences=RestInfluences();
    //the anatomy is human: a creature places each bone's part where it sits on the human bone, whatever its own bind pose,
    //sized by its spans over the human ones
    bool human=kind==Kind::Human;Proportions proportions;
    if(!human) {
        proportions=Creature::Measure(region,MeasureSpans(root,region,bones,binds));
        out.fit=Creature::Fit(proportions);
    }
    for(unsigned k=1;k<kInfluences;++k) {
        if(!names.influences[k]||(human&&!binds.found[k])){out.influences[k]=out.influences[spec.parents[k]];continue;}
        //skinned world = bone.world * skinToBone * skin position, so rest frame space reaches each bone's posed space through skin space
        Transform bend=Compose(Inverse(out.head),Compose(bones[k]->world,human?binds.relation[k]:Creature::Retarget(region,k,proportions)));
        if(!bend.Valid())return CaptureFailure(HeadFailure::MissingRegionBind,k);
        out.influences[k]=bend;
    }
    return 0;
}
bool HasRegion(Form* form,uint32_t region) {
    if(region>=kRegions||!Supported(form))return false;
    auto root=reinterpret_cast<Object*(__thiscall*)(Form*)>(form->vtable[0x1D0/4])(form);
    if(!root)return false;
    const auto& names=NamesOf(region);
    for(unsigned k=0;k<kInfluences;++k)if(names.influences[k]&&!FindBone(root,names.influences[k]))return false;
    return true;
}
bool HitCarrier(Form* form,uint32_t region,Vec3 point,uint32_t& carrier,Transform& pose) {
    if(region<=HeadRegion||region>=kRegions||!Supported(form))return false;
    auto root=reinterpret_cast<Object*(__thiscall*)(Form*)>(form->vtable[0x1D0/4])(form);
    if(!root||(root->flags&kHidden))return false;
    const auto& spec=SpecOf(region);const auto& names=NamesOf(region);
    Object* bones[kInfluences];
    for(unsigned k=0;k<kInfluences;++k) {
        bones[k]=names.influences[k]?FindBone(root,names.influences[k]):nullptr;
        if(!bones[k]||!bones[k]->world.Valid())return false;
    }
    float best=FLT_MAX;
    for(unsigned k=0;k<kInfluences;++k) {
        //a bone with no child in the table is a point at its origin
        Vec3 a=bones[k]->world.t,b=a;
        for(unsigned c=1;c<kInfluences;++c)if(c!=k&&spec.parents[c]==k){b=bones[c]->world.t;break;}
        Vec3 ab=b-a;float along=Dot(ab,ab)>1e-6f?Clamp(Dot(point-a,ab)/Dot(ab,ab),0,1):0;
        float distance=Length(point-(a+ab*along));
        if(distance<best){best=distance;carrier=k;pose=bones[k]->world;}
    }
    return true;
}
}
