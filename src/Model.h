#pragma once
#include "Math.h"
#include <array>
#include <cstdint>
#include <vector>

namespace XRay {
constexpr unsigned kMaterialCount=12;
constexpr unsigned kBoneMaterial=0,kBrainMaterial=1,kBoneInteriorMaterial=2,kTeethMaterial=3,kCartilageMaterial=4,
    kHeartMaterial=5,kLungMaterial=6,kLiverMaterial=7,kGutMaterial=8,kKidneyMaterial=9,kTestisMaterial=10,kPenisMaterial=11;
//bones packs two influence indices as first*16+second, bind is the first one's skinning weight
struct Vertex { Vec3 position,normal; float ao,bind=1,bones=0; };
static_assert(sizeof(Vertex)==36);
//vs_3_0 holds the plate transforms and seven influence rows within its 256 constants
constexpr unsigned kInfluences=7;
using Influences=std::array<Transform,kInfluences>;
inline Influences RestInfluences(){Influences out;for(auto& t:out)t=Transform::Identity();return out;}
inline unsigned FirstInfluence(float bones){return unsigned(bones+.5f)>>4;}
inline unsigned SecondInfluence(float bones){return unsigned(bones+.5f)&15;}
//rigid tissue that fractures, each kind keeps its own colour through carving
constexpr unsigned kFractureKinds=3,kNoFracture=~0u;
inline unsigned FractureKind(unsigned material) {
    return material==kBoneMaterial?0:material==kTeethMaterial?1:material==kCartilageMaterial?2:kNoFracture;
}
constexpr unsigned kFractureMaterials[kFractureKinds]={kBoneMaterial,kTeethMaterial,kCartilageMaterial};
//soft tissue tears instead of fracturing, each kind keeps its own colour in the chunks it throws
constexpr unsigned kSoftKinds=8;
constexpr unsigned kSoftMaterials[kSoftKinds]={kBrainMaterial,kHeartMaterial,kLungMaterial,kLiverMaterial,kGutMaterial,kKidneyMaterial,kTestisMaterial,kPenisMaterial};
inline unsigned SoftKind(unsigned material) {
    for(unsigned kind=0;kind<kSoftKinds;++kind)if(kSoftMaterials[kind]==material)return kind;
    return kNoFracture;
}
//anatomy that hangs under skin its region's weights leave closed, so the skin over it opens by distance
inline bool OpensSkin(unsigned material){return material==kTestisMaterial||material==kPenisMaterial;}
//encapsulated tissue the bullet's cavity bursts whole instead of tearing a track through it, from this shot severity:
//the testes on any hit, the heart and kidneys on a killing shot, everything else never
inline float BurstSeverity(unsigned material) {
    return material==kTestisMaterial?1.f:material==kHeartMaterial||material==kKidneyMaterial?1.5f:1e9f;
}
inline bool Bursts(unsigned material){return BurstSeverity(material)<=1;}
struct Mesh {
    uint32_t material,visibility;
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    //anatomy that does not move rigidly with the frame bone
    bool bound=false;
    unsigned first=0,second=0;
};
struct Model { std::vector<Mesh> meshes; bool Load(const char* path); };
}
