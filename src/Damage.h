#pragma once
#include "Debris.h"
#include "Spray.h"
#include "Cues.h"
#include "Graphics.h"
#include <atomic>

namespace XRay::Damage {
//one wound plans at most kBoneSeeds bone and kBrainSeeds soft seeds in kClusters breaks, and a scene holds kWounds of them
//within its shader tables: bone seeds in two rows of kSeedRow, a ps_3_0 loop's limit
constexpr unsigned kWounds=6,kBoneSeeds=128,kBrainSeeds=48,kClusters=12,kPlates=64,kPlateVertices=98304;
constexpr unsigned kSeedRow=240,kSceneBoneSeeds=2*kSeedRow,kSceneBrainSeeds=160,kSceneClusters=32;
constexpr unsigned kSeedCapacity[kSets]={kBoneSeeds,kBrainSeeds};
constexpr unsigned kSceneCapacity[kSets]={kSceneBoneSeeds,kSceneBrainSeeds};
//a seed packs its break below 32 into the shader's w with its reach
static_assert(kSceneClusters<=32&&kSceneBrainSeeds<=255);
using Summary=Graphics::DamageReport;
//one immutable snapshot of every wound on a head, carved off the render thread when asynchronous
struct Work {
    uint32_t generation=0;
    Seed seeds[kSets][kSceneBoneSeeds];unsigned seedCount[kSets]{};
    Cluster clusters[kSceneClusters];unsigned clusterCount=0;
    uint64_t sequence[kWounds]{};float born[kWounds]{};bool release[kWounds]{};unsigned wounds=0;
    Posing posing;Transform pose{};
    std::vector<PieceVertex> plates;Range plateParts[kFractureKinds];unsigned plateSeeds[kPlates]{},plateCount=0;
    std::vector<PieceVertex> staging[kWounds][kParts];
    PieceMotion pieces[kWounds][kWoundPieces];unsigned pieceCount[kWounds]{};
    unsigned fragments[kWounds]{},brainPieces[kWounds]{},vertices[kWounds]{};
    Rupture ruptures[kWounds][kRuptures];unsigned ruptureCount[kWounds]{};
    float severity[kWounds]{};
};
//a planned wound kept for its actor after the X-ray closes, brought back settled with no debris, sound or spray
struct Kept {
    uint64_t sequence=0;Impact::Path path{};Ammunition::Profile bullet;Runs runs;float severity=1;
    std::vector<Seed> seeds[kSets];std::vector<Cluster> clusters;std::vector<Rupture> ruptures;
};
class Scene {
    struct Wound {
        uint64_t sequence=0;float born=0,age=0;Impact::Path path{};Ammunition::Profile bullet;bool released=false;
        Seed seeds[kSets][kBoneSeeds];unsigned seedCount[kSets]{};
        Cluster clusters[kClusters];unsigned clusterCount=0;Runs runs;
        Rupture ruptures[kRuptures];unsigned ruptureCount=0;
        Cue cues[kCues];unsigned cueCount=0;float severity=1;
    };
    Surface surface_;
    Carver carver_;
    Debris debris_;
    Spray spray_;
    //cues whose moment has come, waiting for the main thread to sound them
    uint8_t heard_[16]{};unsigned heardCount_=0;
    void Hear(float clock);
    Wound wounds_[kWounds];unsigned woundCount_=0;bool dirty_=false;
    Seed seeds_[kSets][kSceneBoneSeeds];unsigned seedCount_[kSets]{};
    Cluster clusters_[kSceneClusters];unsigned clusterCount_=0;
    float born_[kWounds]{};unsigned liveWounds_=0;
    std::vector<PieceVertex> plates_;
    Range plateParts_[kFractureKinds];unsigned plateSeeds_[kPlates]{},plateCount_=0;uint32_t plateRevision_=1;
    float seedConstants_[kSets][kSceneBoneSeeds*4]{};unsigned activeSeeds_[kSets]{};
    float clusterConstants_[kSceneClusters*4]{};unsigned activeClusters_=0;
    //counts every change to the planned wounds, so their keeper knows when to copy them again
    uint32_t woundRevision_=0;
    float plateRows_[kPlates*12]{};
    //burst meshes vanish once the bullet reaches them, their lumps fly as debris
    Rupture ruptures_[kWounds*kRuptures];uint8_t ruptureWound_[kWounds*kRuptures]{};unsigned ruptureCount_=0;uint32_t ruptured_[4]{};
    Vertex bullet_[Impact::kBulletVertices];unsigned bullets_=0;
    Transform bulletRelease_{};bool bulletReleased_=false;
    float clock_=0;
    Summary summary_;
    const Model* model_=nullptr;
    Work work_;
    std::atomic<int> job_{0};
    std::atomic<bool> quit_{false};
    uint32_t generation_=0;
    bool async_=false;
    void* thread_=nullptr;void* wake_=nullptr;void* done_=nullptr;
    static float Clock(const Impact::Frame& frame) {return frame.sceneTime>=0?frame.sceneTime:frame.age;}
    void Remove(unsigned wound);
    bool Snapshot(Work& work) const;
    void Submit(const HeadFrame& head,const Appearance& style);
    void Adopt();
    void Animate(float clock);
    void Wait();
    static unsigned long __stdcall Worker(void* scene);
public:
    ~Scene();
    void SetAsync(bool async){async_=async;}
    void Reserve(const Model& model);
    void Clear();
    void ClearWounds();
    //severity is the next new wound's, how much harder than an ordinary hit it breaks, tears, bursts and bleeds
    void Update(const Model& model,const HeadFrame& head,const Appearance& style,const Impact::Frame& frame,float severity=1,float blood=1);
    void Advance(const HeadFrame& head,const Appearance& style,const Impact::Frame& frame);
    bool Wounded() const {return woundCount_>0;}
    const Impact::Path* LatestPath() const {return woundCount_?&wounds_[woundCount_-1].path:nullptr;}
    Ammunition::Profile LatestBullet() const {return woundCount_?wounds_[woundCount_-1].bullet:Ammunition::Profile{};}
    unsigned ActiveSeeds(unsigned set) const {return activeSeeds_[set];}
    const float* SeedConstants(unsigned set) const {return seedConstants_[set];}
    unsigned ActiveClusters() const {return activeClusters_;}
    const float* ClusterConstants() const {return clusterConstants_;}
    unsigned Plates() const {return plateCount_;}
    const float* PlateRows() const {return plateRows_;}
    bool Ruptured(unsigned mesh) const {return mesh<128&&(ruptured_[mesh>>5]>>(mesh&31)&1);}
    Range PlatePart(unsigned part) const {return plateParts_[part];}
    const std::vector<PieceVertex>& PlateVertices() const {return plates_;}
    uint32_t PlateRevision() const {return plateRevision_;}
    const Debris& Fragments() const {return debris_;}
    const Spray& Blood() const {return spray_;}
    uint32_t WoundRevision() const {return woundRevision_;}
    //every wound now planned, oldest first
    void Keep(std::vector<Kept>& out) const;
    //replaces the wounds with kept ones, settled long ago
    void Restore(const std::vector<Kept>& kept);
    unsigned TakeCues(uint8_t* out,unsigned capacity);
    float SceneClock() const {return clock_;}
    const Vertex* Bullet() const {return bullet_;}
    unsigned Bullets() const {return bullets_;}
    const Summary& Report() const {return summary_;}
    unsigned Status() const;
    bool Pending() const {return dirty_||job_.load()!=0;}
    const Seed* Seeds(unsigned set) const {return seeds_[set];}
    unsigned SeedCount(unsigned set) const {return seedCount_[set];}
    const Cluster* Clusters() const {return clusters_;}
    unsigned ClusterCount() const {return clusterCount_;}
};
Posing PosingOf(const HeadFrame& head,const Appearance& style);
//carves one snapshot, touching only the work, the immutable model/surface and the carver it is given
void Shatter(const Model& model,const Surface& surface,Carver& carver,Work& work);
}
