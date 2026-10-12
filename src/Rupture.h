#pragma once
#include "Debris.h"

namespace XRay::Damage {
//encapsulated tissue the shot passes within this reach of bursts whole, the temporary cavity of a standard round
constexpr float kRuptureReach=1.5f;
//the lumps share more than the organ's volume so a burst reads at killcam distance
constexpr float kRuptureSwell=1.6f;
constexpr unsigned kRuptures=4,kRupturePieces=12,kRupturePiecesMost=24;
//centre and direction in posed frame space, time after the shot when the bullet comes level with it
//force is the shot's severity: a harder shot throws more lumps, faster
struct Rupture {Vec3 centre{},direction{};float time=0;uint16_t mesh=0;float force=1;};
unsigned PlanRuptures(const Model& model,const Shot& shot,Rupture* out,unsigned capacity);
//the burst mesh as lumps sharing its volume, thrown from its centre and on along the shot, staged as pieces from first
//budgets holds the vertices left in each of the wound's bursts, a piece takes them from the burst its index falls in
unsigned Spill(const Model& model,const Posing& posing,const Rupture& rupture,uint64_t key,unsigned* budgets,
    std::vector<PieceVertex>& staging,unsigned first,PieceMotion* pieces,unsigned capacity);
}
