#pragma once
#include "Fracture.h"

namespace XRay::Damage {
constexpr unsigned kBursts=8,kBurstPieces=64,kBurstVertices=49152;
//one wound's pieces fill this many bursts in turn, each burst one draw of at most kBurstPieces transforms
constexpr unsigned kWoundBursts=2,kWoundPieces=kBurstPieces*kWoundBursts;
//fracture kinds first, in the model's order, then torn soft tissue by soft kind
enum Part : unsigned {BonePart,TeethPart,CartilagePart,BrainPart};
constexpr unsigned kParts=kFractureKinds+kSoftKinds;
static_assert(CartilagePart+1==kFractureKinds&&BrainPart==kFractureKinds);
struct Range {unsigned first=0,count=0;};
struct PieceMotion {Vec3 center,velocity,axis;float time,life,spin;};
class Debris {
    struct Burst {
        PieceMotion pieces[kBurstPieces];unsigned count=0;
        Range parts[kParts];Transform pose{};float born=0;
    } bursts_[kBursts];
    std::vector<PieceVertex> vertices_;
    unsigned next_=0;uint32_t revision_=1;
public:
    void Reserve(){vertices_.assign(size_t(kBursts)*kBurstVertices,{});}
    void Clear();
    //copies one shot's carved pieces, already in head space, with piece indices in their vertices, kBurstPieces to a burst,
    //returns the first burst used
    unsigned Commit(const Transform& pose,float born,const PieceMotion* pieces,unsigned count,const std::vector<PieceVertex>* parts);
    bool Live(unsigned burst,float clock) const;
    //rows of piece-to-camera-relative-world transforms, expired pieces collapse to zero
    unsigned Transforms(unsigned burst,float clock,Vec3 origin,float* rows) const;
    Range PartOf(unsigned burst,unsigned part) const {return bursts_[burst].parts[part];}
    //the vertices a burst holds, its parts laid end to end from its base
    Range Used(unsigned burst) const {unsigned n=0;for(const auto& part:bursts_[burst].parts)n+=part.count;return {burst*kBurstVertices,n};}
    unsigned Pieces(unsigned burst) const {return bursts_[burst].count;}
    const std::vector<PieceVertex>& Vertices() const {return vertices_;}
    uint32_t Revision() const {return revision_;}
    unsigned LiveVertices(float clock) const;
};
}
