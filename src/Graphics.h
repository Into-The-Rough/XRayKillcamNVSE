#pragma once
#include "Model.h"
#include "FaceFit.h"
#include "Impact.h"
#include <d3d9.h>

namespace XRay::Damage {struct Kept;}
namespace XRay {
constexpr unsigned kHeadVertices=1211,kHeadTriangles=2294,kSkinVertices=16384,kSkinTriangles=32768;
//reveal is the skin weight of Bip01 Head plus Bip01 Neck1
struct MaskVertex { Vec3 world,local; float reveal=1; };
enum Region : uint32_t {HeadRegion,TorsoRegion,LegsRegion,ArmsRegion,kRegions};
inline const char* RegionName(uint32_t region){static const char* const names[]={"head","torso","leg","arm"};return region<kRegions?names[region]:"head";}
//which anatomy set a body carries: the human one, shown on people and on creatures sized to their skeletons, or the horse's own
enum Anatomy : uint8_t {HumanAnatomy,HorseAnatomy,kAnatomies};
//limb regions carry both sides, the right side's root bone at influence 4
constexpr bool kSided[kRegions]={false,false,true,true};
//one captured body region, the head with its face or a region below it, in the space of its frame bone
struct HeadFrame {
    //the frame bone: Bip01 Head, Spine1 for the torso, Pelvis for the legs, Spine2 for the arms
    Transform head;
    HeadFit fit;
    MaskVertex vertices[kHeadVertices];
    uint16_t indices[kHeadTriangles*3];
    //rest frame space to posed frame space for skin bound only to each influence, the head's second is Bip01 Neck1
    Influences influences=RestInfluences();
    //largest error rebuilding the face's head and neck skinned vertices from the neck influence, -1 when unchecked
    float neckError=-1;
    uint32_t region=HeadRegion;
    //only a human head has its face, every other capture has only the skin
    bool face=true;
    //the torso anatomy without the external genitals, for women and creatures, and the anatomy set, both in the padding before the skin counts
    bool noGenitals=false;uint8_t anatomy=HumanAnatomy;
    //body and armour shapes bound across the neck joint, posed by the game like the face, they close the head's open collar
    uint32_t skinVertices=0,skinTriangles=0,skinShapes=0,skinSkipped=0;
    MaskVertex skin[kSkinVertices];
    uint16_t skinIndices[kSkinTriangles*3];
    //each influence bone's posed transform in the frame's space, the identity for the head, kept last in captures
    Influences posedBones=RestInfluences();
};
struct CameraFrame {
    float view[16],projection[16];
    Vec3 origin;
    D3DVIEWPORT9 viewport;
    bool Reversed() const {return projection[10]*projection[15]<projection[14]*projection[11];}
};
struct Appearance { float opacity=1,scale=1,up=0,forward=0,side=0,depthTolerance=2; };
namespace Graphics {
enum DamageSupport : unsigned {EntryBreak=1,ExitBreak=2,InnerBreak=4,HingedPlates=8,BoneFragments=16,BrainTorn=32};
struct DamageReport {
    uint64_t sequence=0;unsigned wounds=0,boneRuns=0,brainRuns=0,entry=0,inner=0,exit=0;
    unsigned plates=0,fragments=0,brainPieces=0,vertices=0,ruptures=0;
};
//one shot's blood on screen: its jets and droplets by kind (entry, exit, burst, mist) and the frames they flew, and the measured
//frame showing the most: its blood pixels outside and inside the window, overlaps counted, and the camera then
struct BloodReport {
    uint64_t sequence=0;unsigned jets[4]{},droplets[4]{};
    unsigned frames=0,measured=0,outside=0,inside=0;
    //distance to the entry, degrees between the view and the bullet's line (0 looks along it from behind), entry drop pixels
    float distance=-1,angle=0,dropPixels=0;
    bool counted=false;
};
bool Draw(IDirect3DDevice9* device,const Model& model,const CameraFrame& camera,const HeadFrame& head,unsigned mode,const Appearance& style,const Impact::Frame* impact=nullptr);
void Release();
void ClearDamage();
void ClearWounds();
//the game carves wounds on a worker thread, standalone replays stay synchronous
void SetAsyncDamage(bool async);
//blood droplets thrown from each wound
void SetBloodSpray(bool enabled);
//how hard the next new wound hits (1 an ordinary hit, more for a killing shot) and how much blood it throws
void SetShotForce(float severity,float blood);
HRESULT LastError();
//the last draw found targets it cannot draw into this frame, nothing failed
bool Skipped();
unsigned DamageStatus();
DamageReport DamageSummary();
//the sound cues the drawn region's wounds reached since the last call, oldest first
unsigned TakeCues(uint8_t* out,unsigned capacity);
//a shot whose blood has landed, or that a newer shot replaced, once each
bool TakeBlood(BloodReport& out);
//the shot whose blood is still flying
BloodReport BloodInFlight();
//a count that changes whenever a region's planned wounds do, and a copy of them
uint32_t WoundRevision(unsigned region);
void KeepWounds(unsigned region,std::vector<Damage::Kept>& out);
//lays kept wounds into a region's scene, settled, at its next draw
void RestoreWounds(unsigned region,const std::vector<Damage::Kept>& kept);
}
}
