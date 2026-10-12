#pragma once
#include "DamageSurface.h"
#include "Ammunition.h"

namespace XRay::Damage {
enum State : uint8_t {Anchor,Plate,Loose};
enum Kind : uint8_t {EntryKind,InnerKind,ExitKind,BrainKind};
struct Seed {
    Vec3 position{};float reach=0,time=0;
    uint8_t state=Anchor,cluster=0,wound=0,kind=EntryKind,source=0;
    Vec3 hinge{},axis{},motion{};float angle=0,life=0,spin=0;
    bool hinged=false;
};
struct Cluster {Vec3 center{},normal{};float time=0,ring=0,crack=0;uint8_t kind=EntryKind,wound=0;};
struct Shot {
    Vec3 origin{},direction{};float length=0,secondsPerUnit=0;Ammunition::Profile bullet;uint64_t sequence=0;uint8_t wound=0;
    //the head-space path and posing, for anatomy that bends with the neck
    Vec3 entry{},exit{};const Posing* posing=nullptr;
    //how much harder than an ordinary hit it breaks, tears and throws, a killing shot's above 1
    float severity=1;
};
struct Plan {
    Seed* seeds[kSets]{};unsigned count[kSets]{},capacity[kSets]{};
    Cluster* clusters=nullptr;unsigned clusterCount=0,clusterCapacity=0;
};
struct Random {
    uint32_t state;
    Random(uint64_t seed,uint32_t salt) {
        state=uint32_t((seed*0x9E3779B97F4A7C15ull)>>32)^(salt*0x85EBCA6Bu)^0x2545F491u;
        if(!state)state=1;
    }
    float Next(){state^=state<<13;state^=state>>17;state^=state<<5;return float(state&0xFFFFFF)/16777216.0f;}
    float Range(float a,float b){return a+(b-a)*Next();}
};
//small calibres still break a visible hole, larger rounds grow it
inline float WoundScale(const Ammunition::Profile& bullet){return Clamp(.55f+bullet.radius*2.5f,1,1.6f);}
struct Runs {unsigned bone=0,brain=0;};
Runs PlanWound(const Model& model,const Surface& surface,const Shot& shot,Plan& plan);
void PruneAnchors(Seed* seeds,unsigned& count,const Cluster* clusters);
float Spacing(const Cluster& cluster);

struct PieceVertex {Vec3 position,normal;float ao,piece,bind=1,bones=0;};
constexpr unsigned kChunkVertices=240;
//a lumpy closed chunk of soft tissue around the origin, deterministic per key
void Chunk(float radius,uint32_t key,Vertex* out);
static_assert(sizeof(PieceVertex)==40);
class Carver {
public:
    //bone cells split by fracture kind, soft cells by soft kind
    static constexpr unsigned kCells=128,kParts=kFractureKinds>kSoftKinds?kFractureKinds:kSoftKinds;
    struct Part {std::vector<Vertex> vertices;Vec3 lo{},hi{};float area=0;};
    void Reserve(unsigned vertices);
    //clips every donor triangle of the set to the selected seeds' nearest-seed cells
    unsigned Carve(const Model& model,const Surface& surface,unsigned set,const Seed* seeds,unsigned count,const uint8_t* selected);
    unsigned Cells() const {return cells_;}
    unsigned SeedOf(unsigned cell) const {return seed_[cell];}
    const Part& PartOf(unsigned cell,unsigned part) const {return parts_[cell][part];}
private:
    Part parts_[kCells][kParts];
    unsigned seed_[kCells]{},cells_=0;
};
}
