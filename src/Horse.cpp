#include "Engine.h"
#include <cfloat>

namespace XRay::Engine {
namespace {
//animal bases already read, the oldest written over once full
constexpr unsigned kBases=128;
struct Base {uint32_t id;bool horse;};
Base s_bases[kBases];unsigned s_count=0,s_next=0;
SRWLOCK s_lock=SRWLOCK_INIT;
//the trunk from the pelvis to the base of the neck, and the neck from there to the head
const char* s_trunk[5]{};const char* s_neck[4]{};
const Base* Find(uint32_t id) {
    for(unsigned i=0;i<s_count;++i)if(s_bases[i].id==id)return &s_bases[i];
    return nullptr;
}
//the nearest any link of the chain passes to the point, FLT_MAX when a bone is missing
float Reach(Object* root,const char* const* names,unsigned count,Vec3 point) {
    float best=FLT_MAX;const Object* last=nullptr;
    for(unsigned i=0;i<count;++i) {
        auto bone=FindBone(root,names[i]);
        if(!bone||!bone->world.Valid())return FLT_MAX;
        if(last) {
            Vec3 a=last->world.t,ab=bone->world.t-a;
            float along=Dot(ab,ab)>1e-6f?Clamp(Dot(point-a,ab)/Dot(ab,ab),0,1):0;
            best=fminf(best,Length(point-(a+ab*along)));
        }
        last=bone;
    }
    return best;
}
}
bool HorseBase(uint32_t baseID) {
    AcquireSRWLockShared(&s_lock);auto known=Find(baseID);bool horse=known&&known->horse;ReleaseSRWLockShared(&s_lock);
    return horse;
}
void ClassifyHorse(Form* actor) {
    if(!actor||actor->type!=kCreature||(actor->flags&kDeleted))return;
    auto base=reinterpret_cast<const Form*(__thiscall*)(const Form*)>(0x4181E0)(actor);
    if(!base||base->type!=kCreatureBase||reinterpret_cast<const uint8_t*>(base)[0x12C]!=kAnimalType)return;
    AcquireSRWLockShared(&s_lock);bool known=Find(base->id)!=nullptr;ReleaseSRWLockShared(&s_lock);
    if(known)return;
    //an unloaded body is read again at its next hit
    auto root=reinterpret_cast<Object*(__thiscall*)(Form*)>(actor->vtable[0x1D0/4])(actor);
    if(!root)return;
    bool horse=CarriesAnatomy(root,HorseAnatomy);
    AcquireSRWLockExclusive(&s_lock);
    if(!Find(base->id)) {
        s_bases[s_next]={base->id,horse};s_next=(s_next+1)%kBases;
        if(s_count<kBases)++s_count;
    }
    ReleaseSRWLockExclusive(&s_lock);
}
uint32_t HorseRegion(Form* actor,uint32_t region,Vec3 point) {
    if(region!=TorsoRegion||KindOf(actor)!=Kind::Horse)return region;
    auto root=reinterpret_cast<Object*(__thiscall*)(Form*)>(actor->vtable[0x1D0/4])(actor);
    if(!root||(root->flags&kHidden))return region;
    return Reach(root,s_neck,4,point)<Reach(root,s_trunk,5,point)?HeadRegion:region;
}
bool InitHorse() {
    auto intern=reinterpret_cast<const char*(__cdecl*)(const char*)>(0xA5B690);
    const char* trunk[]={"Bip01 Pelvis","Bip01 Spine","Bip01 Spine1","Bip01 Spine2","Bip01 Neck"};
    const char* neck[]={"Bip01 Neck","Bip01 Neck1","Bip01 Neck2","Bip01 Head"};
    for(unsigned i=0;i<5;++i)if(!(s_trunk[i]=intern(trunk[i])))return false;
    for(unsigned i=0;i<4;++i)if(!(s_neck[i]=intern(neck[i])))return false;
    return true;
}
}
