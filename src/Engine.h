#pragma once
#include "Graphics.h"
#include "BodySkin.h"
#include "ShotTrace.h"
#include <cstddef>

namespace XRay::Engine {
constexpr uint8_t kCharacter=0x3B,kCreature=0x3C;
constexpr uint8_t kAmmo=0x29;
constexpr uint32_t kDeleted=0x20,kHidden=1;
constexpr uint32_t kPlayerRefID=0x14;
struct Form {void** vtable;uint8_t type,pad05[3];uint32_t flags,id;};
//getiscreaturetype 0x59f300: the reference's base form (0x4181e0) is a tescreature (form type 0x2b) whose type byte at +0x12c
//(0x59f3a0) matches, the record's DATA type: 4 for super mutants, nightkin and behemoths, 5 for feral ghouls
constexpr uint8_t kCreatureBase=0x2B,kAnimalType=0,kSuperMutantType=4,kFeralGhoulType=5;
enum class Kind {None,Human,FeralGhoul,SuperMutant,Horse};
//an animal base whose skeleton carries every bone the horse anatomy rides, as ClassifyHorse last found it, any thread
bool HorseBase(uint32_t baseID);
//what anatomy an actor reference can show: humans and ghouls on the human skeleton, creatures on their own
inline Kind KindOf(const Form* actor) {
    if(!actor||(actor->flags&kDeleted))return Kind::None;
    if(actor->type==kCharacter)return Kind::Human;
    if(actor->type!=kCreature)return Kind::None;
    auto base=reinterpret_cast<const Form*(__thiscall*)(const Form*)>(0x4181E0)(actor);
    if(!base||base->type!=kCreatureBase)return Kind::None;
    auto type=reinterpret_cast<const uint8_t*>(base)[0x12C];
    if(type==kAnimalType)return HorseBase(base->id)?Kind::Horse:Kind::None;
    return type==kFeralGhoulType?Kind::FeralGhoul:type==kSuperMutantType?Kind::SuperMutant:Kind::None;
}
inline bool Supported(const Form* actor){return KindOf(actor)!=Kind::None;}
inline uint8_t AnatomyOf(Kind kind){return kind==Kind::Horse?HorseAnatomy:HumanAnatomy;}
//reads an animal's skeleton once its 3d is loaded and keeps whether it can show the horse anatomy, main thread only
void ClassifyHorse(Form* actor);
//the horse's body part data counts its neck as torso, a torso hit nearer the neck than the trunk opens the head and neck instead
uint32_t HorseRegion(Form* actor,uint32_t region,Vec3 point);
bool InitHorse();
struct JIPProjectile {
    uint8_t pad00[0x60];Form* ammo;uint8_t pad64[0x14A-0x64];uint8_t numProjectiles;
    Form* GetAmmo() const {return numProjectiles?ammo:nullptr;}
};
static_assert(offsetof(JIPProjectile,ammo)==0x60&&offsetof(JIPProjectile,numProjectiles)==0x14A);
struct HitData {Form* source;Form* target;Form* projectile;uint32_t weaponAV;int32_t location;uint8_t pad14[0x24];Vec3 impact;uint8_t pad44[0x14];uint32_t flags;uint8_t pad5C[8];};
static_assert(offsetof(HitData,source)==0x00&&offsetof(HitData,target)==0x04&&offsetof(HitData,projectile)==0x08&&offsetof(HitData,location)==0x10
    &&offsetof(HitData,impact)==0x38&&offsetof(HitData,flags)==0x58&&sizeof(HitData)==0x64);
//the critical roll 0x9b7060 sets it through 0x407e00 while the hit data is filled, before actor::hitme
constexpr uint32_t kHitCritical=4;
struct PlayerCamera {uint8_t pad00[0x7C0];uint32_t mode;uint8_t pad7C4[0xE18-0x7C4];float timer;};
struct VATS {uint8_t pad00[8];uint32_t mode;};
static_assert(offsetof(PlayerCamera,mode)==0x7C0&&offsetof(PlayerCamera,timer)==0xE18&&offsetof(VATS,mode)==0x08);
//0x9ca2c0 ends with vats mode 4, the killcam playing
constexpr uint32_t kVATSNone=0,kVATSPlayback=4,kKillCamPlayerView=1,kKillCamFull=2;
inline PlayerCamera* GetPlayer(){return *reinterpret_cast<PlayerCamera**>(0x11DEA3C);}
inline VATS* GetVATS(){return reinterpret_cast<VATS*>(0x11F2250);}
//0x453550 accumulates the scaled scene delta supplied by 0x86fd08
inline float SimulationTime(){return *reinterpret_cast<const float*>(0x11C3C08);}
inline Form* Lookup(uint32_t id){return reinterpret_cast<Form*(__cdecl*)(uint32_t)>(0x4839C0)(id);}
enum class BodyPart:int32_t {Torso=0,Head1=1,Head2=2,LeftArm=3,RightArm=5,LeftLeg=7,RightLeg=10,Brain=13};
//the region a native body part opens, kRegions for a part without anatomy
inline uint32_t PartRegion(int32_t location) {
    switch(static_cast<BodyPart>(location)) {
    case BodyPart::Head1:case BodyPart::Head2:case BodyPart::Brain:return HeadRegion;
    case BodyPart::Torso:return TorsoRegion;
    case BodyPart::LeftLeg:case BodyPart::RightLeg:return LegsRegion;
    case BodyPart::LeftArm:case BodyPart::RightArm:return ArmsRegion;
    default:return kRegions;
    }
}
//0 for the left limb or anything central, 1 for the right limb
inline unsigned LimbSide(int32_t location) {
    return location==static_cast<int32_t>(BodyPart::RightLeg)||location==static_cast<int32_t>(BodyPart::RightArm)?1:0;
}
//niavobject: parent +0x18 (niavobject::update 0xa59c76), local transform +0x34, world +0x68
struct Object {
    void** vtable;uint32_t references;const char* name;uint8_t pad0C[0x0C];Object* parent;uint8_t pad1C[0x14];
    uint32_t flags;Transform local;Transform world;
};
static_assert(offsetof(Object,parent)==0x18&&offsetof(Object,flags)==0x30&&offsetof(Object,local)==0x34&&offsetof(Object,world)==0x68);
struct SceneGraph {uint8_t pad00[0xAC];Object* camera;};
inline SceneGraph* GetSceneGraph(){return *reinterpret_cast<SceneGraph**>(0x11DEB7C);}
//vats::update 0x9c72b6 copies the playing entry's target here, 0x9ca2c0 queues it at entry+0x0c
inline Form* VATSTarget(){return *reinterpret_cast<Form**>(0x11F21CC);}
//a killcam is playing and shows this actor
inline bool KillcamShows(uint32_t id) {
    auto player=GetPlayer();auto target=VATSTarget();
    return id&&player&&player->timer>0&&GetVATS()->mode==kVATSPlayback&&target&&target->id==id;
}
//niupdatedata 0x43d410 thiscall(time, char, char) ret 0x0c fills 9 bytes, niavobject::update 0xa59c60 thiscall(data) ret 4
inline void UpdateObject(Object* object) {
    alignas(8) uint8_t data[16]{};
    reinterpret_cast<void(__thiscall*)(void*,float,char,char)>(0x43D410)(data,0,0,0);
    reinterpret_cast<void(__thiscall*)(Object*,void*)>(0xA59C60)(object,data);
}
static_assert(offsetof(SceneGraph,camera)==0xAC);
//bhkpickdata 0xb0 bytes, 16-byte aligned: filter +0x24 (layer bits 0-6, collision group in the high word), hit fraction +0x40,
//shape keys +0x50, hit collidable +0x80, set +0xac when the per-frame pick budget skipped the cast
struct alignas(16) PickData {uint8_t pad00[0x24];uint32_t filter;uint8_t pad28[0x18];float fraction;uint8_t pad44[0x0C];int32_t shapeKeys[8];
    uint8_t pad70[0x10];void* collidable;uint8_t pad84[0x28];uint8_t skipped,padAD[3];};
static_assert(offsetof(PickData,filter)==0x24&&offsetof(PickData,fraction)==0x40&&offsetof(PickData,shapeKeys)==0x50
    &&offsetof(PickData,collidable)==0x80&&offsetof(PickData,skipped)==0xAC&&sizeof(PickData)==0xB0);
//free distance from one point toward another before something solid, passing through actors and skip's own body,
//a negative when the engine could not test it
float Clearance(Form* skip,Vec3 from,Vec3 to);
//bssoundhandle: sound id (~0 when unresolved), a byte isplaying 0xad8930 reads first so it stays zero, local state
struct SoundHandle {uint32_t id=~0u;uint8_t success=0,pad05[3]{};uint32_t state=0;};
static_assert(sizeof(SoundHandle)==12);
//a random wav or ogg from a folder (Data\Sound\fx\...\) other than last, which it updates, unpositioned,
//at a volume 0..1 and a speed the killcam slows further
bool PlayFromFolder(const char* folder,char (&last)[260],float volume,float speed);
struct Weight {uint16_t vertex,pad;float weight;};
struct Bone {Transform skinToBone;float bound[4];Weight* weights;uint16_t count,pad;};
struct SkinData {void** vtable;uint32_t references;void* partition;Transform rootToSkin;Bone* bones;uint32_t count;};
struct PartitionSet;
struct Skin {void** vtable;uint32_t references;SkinData* data;PartitionSet* partition;Object* root;Object** bones;};
struct ShapeData {
    void** vtable;uint32_t references;uint16_t count;uint8_t pad0A[0x16];Vec3* positions;
    uint8_t pad24[0x1C];uint16_t triangles,active;uint32_t indexCount;uint16_t* indices;
};
struct AlphaProperty {uint8_t pad00[0x18];uint16_t flags;};
struct Geometry {Object object;AlphaProperty* alpha;uint8_t padA0[0x18];ShapeData* data;Skin* skin;};
struct Node {Object object;void* arrayVtable;Object** children;uint16_t capacity,count;};
struct Partition {
    void** vtable;uint16_t* bones;float* weights;uint16_t* vertexMap;uint8_t* palette;uint16_t* indices;uint16_t* stripLengths;
    uint16_t vertices,triangles,boneCount,strips,bonesPerVertex,pad26;void* buffer;
};
struct PartitionSet {void** vtable;uint32_t references;uint32_t count;Partition* partitions;};
struct DismemberEntry {uint8_t enabled,pad01;uint16_t part;};
struct DismemberSkin {Skin skin;uint8_t pad18[0x1C];uint32_t count;DismemberEntry* entries;uint8_t renderable;};
//GetRTTI result for BSDismemberSkinInstance, compared the same way by 0xb63f10
constexpr uintptr_t kDismemberRTTI=0x11F49D8;
struct Renderer {
    uint8_t pad00[0x288];IDirect3DDevice9* device;uint8_t pad28C[0x470];uint8_t inactive;
    uint8_t pad6FD[0x283];float view[16],projection[16];
};
static_assert(offsetof(Form,type)==0x04&&offsetof(Form,flags)==0x08&&offsetof(Form,id)==0x0C);
static_assert(offsetof(Object,name)==0x08&&offsetof(Object,flags)==0x30&&offsetof(Object,world)==0x68&&sizeof(Object)==0x9C);
static_assert(sizeof(Weight)==0x08&&offsetof(Weight,weight)==0x04);
static_assert(sizeof(Bone)==0x4C&&offsetof(Bone,weights)==0x44&&offsetof(Bone,count)==0x48);
static_assert(offsetof(SkinData,rootToSkin)==0x0C&&offsetof(SkinData,bones)==0x40&&offsetof(SkinData,count)==0x44);
static_assert(offsetof(Skin,data)==0x08&&offsetof(Skin,root)==0x10&&offsetof(Skin,bones)==0x14);
static_assert(offsetof(ShapeData,count)==0x08&&offsetof(ShapeData,positions)==0x20&&offsetof(ShapeData,triangles)==0x40
    &&offsetof(ShapeData,indexCount)==0x44&&offsetof(ShapeData,indices)==0x48);
static_assert(offsetof(Geometry,alpha)==0x9C&&offsetof(Geometry,data)==0xB8&&offsetof(Geometry,skin)==0xBC&&offsetof(AlphaProperty,flags)==0x18);
static_assert(offsetof(Node,children)==0xA0&&offsetof(Node,count)==0xA6);
static_assert(offsetof(Partition,vertexMap)==0x0C&&offsetof(Partition,indices)==0x14&&offsetof(Partition,stripLengths)==0x18
    &&offsetof(Partition,vertices)==0x1C&&offsetof(Partition,triangles)==0x1E&&offsetof(Partition,strips)==0x22&&sizeof(Partition)==0x2C);
static_assert(offsetof(PartitionSet,count)==0x08&&offsetof(PartitionSet,partitions)==0x0C&&sizeof(DismemberEntry)==4);
static_assert(offsetof(DismemberSkin,count)==0x34&&offsetof(DismemberSkin,entries)==0x38&&offsetof(DismemberSkin,renderable)==0x3C);
static_assert(offsetof(Renderer,device)==0x288&&offsetof(Renderer,inactive)==0x6FC&&offsetof(Renderer,view)==0x980&&offsetof(Renderer,projection)==0x9C0);
inline Renderer* GetRenderer(){return *reinterpret_cast<Renderer**>(0x11F4748);}
inline bool Menus(){return reinterpret_cast<bool(__cdecl*)()>(0x702360)()||reinterpret_cast<bool(__cdecl*)()>(0x703D50)();}
bool InitNames();
Trace::View CaptureShotView();
void InitProjectileAmmo();
uint32_t ProjectileAmmoID(Form* projectile);
//the world transform of the region's frame bone, Bip01 Head, Spine1, Pelvis or Spine2
bool FramePose(Form* form,uint32_t region,Transform& out);
const char* RegionFrameName(uint32_t anatomy,uint32_t region);
//whether a skeleton has every bone the anatomy set rides, main thread only
bool CarriesAnatomy(Object* root,uint32_t anatomy);
//the region influence whose bone, to its first child bone, passes nearest a world point, and that bone's world transform
bool HitCarrier(Form* form,uint32_t region,Vec3 point,uint32_t& carrier,Transform& pose);
//whether the actor's skeleton has every bone the region's anatomy rides, main thread only
bool HasRegion(Form* form,uint32_t region);
Impact::Sample CaptureImpact(Form* target,Form* projectile,Vec3 point,bool capturePose,uint32_t region);
//0x4f8960 returns this dword, 0 alive, 1 dying, 2 dead, 6 essential down
inline uint32_t LifeState(const Form* actor){return *reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(actor)+0x108);}
//life states at actor +0x108: 0 alive, 1 dying, 2 dead, 3 unconscious, 6 essential down
constexpr uint32_t kAlive=0;
//actor::isdead(false) 0x8844f0: dying, dead and essential down
inline bool IsDeadState(uint32_t state){return state==1||state==2||state==6;}
//the condition actor value actor::hitme damages for a body part (0x89bc86 to 0x89bd07), -1 when the part has none:
//base form vtable +0x180 is the body part data, 0x5e50f0 its part at +0x34 + 4 * index, 0x5e5190 the part's value at +0x63
inline int32_t LimbActorValue(Form* actor,int32_t location) {
    if(location<0||location>=15)return -1;
    auto base=reinterpret_cast<Form*(__thiscall*)(Form*)>(0x4181E0)(actor);
    if(!base)return -1;
    auto data=reinterpret_cast<uint8_t*(__thiscall*)(Form*)>(base->vtable[0x180/4])(base);
    auto part=data?reinterpret_cast<uint8_t* const*>(data+0x34)[location]:nullptr;
    if(!part)return -1;
    int32_t value=*reinterpret_cast<const int8_t*>(part+0x63);
    return value>=0&&value<0x4D?value:-1;
}
//getissex 0x59f3c0: the reference's base form, then tesnpc 0x5f0cc0, actor base data flag 1, which returns 1 for female
inline bool IsFemale(Form* actor) {
    auto base=reinterpret_cast<Form*(__thiscall*)(Form*)>(0x4181E0)(actor);
    return base&&reinterpret_cast<int32_t(__thiscall*)(Form*)>(0x5F0CC0)(base)==1;
}
//actorvalueowner at actor+0xa4, getactorvalue is its vtable +0x0c (0x89bd14)
inline float ActorValue(Form* actor,uint32_t value) {
    auto owner=reinterpret_cast<uint8_t*>(actor)+0xA4;
    return reinterpret_cast<float(__thiscall*)(void*,uint32_t)>((*reinterpret_cast<void***>(owner))[0x0C/4])(owner,value);
}
enum class HeadFailure:uint32_t {
    None,MissingReference,WrongReferenceType,DeletedReference,Child,MissingRoot,HiddenRoot,
    MissingHead,MissingFace,HiddenHead,HiddenFace,HeadTransform,NotGeometry,MissingGeometryData,
    MissingSkin,NotTriangleShape,VertexCount,TriangleCount,IndexCount,MissingPositions,MissingIndices,
    MissingSkinData,BoneCount,MissingBoneData,MissingBoneArray,MissingSkinRoot,RootTransform,SkinTransform,
    MissingBone,BoneTransform,BindTransform,WeightCount,MissingWeights,WeightIndex,WeightValue,
    MissingHeadInfluence,VertexValue,TriangleIndex,PosedVertex,MissingRegionBone,MissingRegionBind,MissingRegionSkin
};
//the failure in the low word, the offending value in the high word
inline uint64_t CaptureFailure(HeadFailure failure,uint32_t value=0){return (uint64_t(value)<<32)|static_cast<uint32_t>(failure);}
uint64_t ResolveHuman(uint32_t refID,Object*& root,Object*& head,Geometry*& geometry);
uint64_t CaptureHead(uint32_t refID,HeadFrame& out);
//a region below the head, side 0 left and 1 right picks which limb's skin the reveal opens
uint64_t CaptureRegion(uint32_t refID,uint32_t region,unsigned side,HeadFrame& out);
bool InitRegionNames();
//a bone by its interned name, Bip01 Neck1 falling back to the Bip01 Neck creature skeletons use
Object* FindBone(Object* root,const char* interned);
//which skinned shapes a capture keeps, whose weights open the reveal and which rest relations it records
//bones a skin request can name for the shapes it keeps and for the reveal
constexpr unsigned kSkinBones=8;
struct SkinRequest {
    const Object* required[kSkinBones]{};unsigned requiredCount=0;bool requireAll=true;
    const Object* reveal[kSkinBones]{};unsigned revealCount=0;
    const Object* frame=nullptr;const Object* influences[kInfluences]{};
    //measure the frame bone's crown in the shapes bound across its first influence
    bool crown=false;
};
void CaptureSkin(Object* root,const Geometry* face,const SkinRequest& request,HeadFrame& out,SkinBinds* binds=nullptr);
//the first shape the last capture skipped, its reason from BodySkin::Skip and its vertex count
struct SkinSkipNote {uint32_t reason=0,vertices=0;};
SkinSkipNote LastSkinSkip();
const char* DescribeHeadFailure(HeadFailure failure);
}
