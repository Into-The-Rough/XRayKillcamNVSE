#pragma once
#include "Model.h"
#include "FaceFit.h"

namespace XRay::Damage {
enum Set : unsigned {BoneSet,BrainSet,kSets};
//brain and the organs share the soft set, they tear instead of fracturing
//t runs along the frame-space donor line, point is the donor position actually crossed
struct Crossing {float t;Vec3 normal;unsigned mesh;bool entering;Vec3 point;};
struct Bounds {Vec3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};};
struct Facet {Vec3 centre;float radius;uint32_t first;uint16_t mesh;uint8_t part,pad;};
//triangles bucketed by centroid so carving visits only cells near broken anatomy
struct Grid {Vec3 origin{};float cell=1,reach=0;int size[3]{};std::vector<uint32_t> starts,facets;std::vector<float> reaches;};
class Surface {
    std::vector<Bounds> bounds_;
    std::vector<float> bindLo_,bindHi_;
    std::vector<uint8_t> sets_;
    std::vector<Facet> facets_[kSets];
    Grid grids_[kSets];
public:
    static constexpr uint8_t kNone=0xFF;
    void Reserve(const Model& model);
    uint8_t SetOf(unsigned mesh) const {return mesh<sets_.size()?sets_[mesh]:kNone;}
    const Bounds& BoundsOf(unsigned mesh) const {return bounds_[mesh];}
    const Grid& GridOf(unsigned set) const {return grids_[set];}
    const Facet& FacetOf(unsigned set,uint32_t index) const {return facets_[set][index];}
    //anatomy rigid with the head, along a straight donor line
    unsigned Trace(const Model& model,Vec3 origin,Vec3 direction,float lo,float hi,Crossing* out,unsigned capacity) const;
    //anatomy that bends with the neck, posed onto the straight head-space path, t scaled to a donor line of this length
    unsigned TraceBound(const Model& model,const Posing& posing,Vec3 entry,Vec3 exit,float length,float lo,float hi,
        Crossing* out,unsigned capacity,std::vector<Vec3>& posed) const;
};
}
