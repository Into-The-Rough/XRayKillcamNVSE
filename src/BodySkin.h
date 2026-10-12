#pragma once
#include "Graphics.h"
#include "Model.h"

namespace XRay {
//rest relations are skeleton facts, so any shape bound to two influences links them and the links chain across shapes
constexpr unsigned kBindShapes=32;
struct SkinBinds {
    //relation[k] carries rest frame-bone space into rest influence-k space
    bool found[kInfluences]{};Transform relation[kInfluences];
    struct Shape {uint32_t present;Transform skinToBone[kInfluences];};
    Shape shapes[kBindShapes];unsigned shapeCount=0;
    //the highest frame-weighted rest vertex along the frame bone's x axis, 0 unless the request measures it
    float crown=0;
    void Resolve();
};
}
namespace XRay::BodySkin {
enum Skip : uint32_t {Kept,DataMissing,BadBone,BadWeight,BadTriangles,OverCapacity,BadVertex};
//one shape the game has already skinned, positions in world space, reveal is the head plus neck weight
struct Shape {const Vec3* world;const float* reveal;uint32_t count;const uint16_t* indices;uint32_t indexCount;};
//appends the whole shape to the frame's skin or leaves the frame unchanged and says why
Skip Add(const Shape& shape,std::vector<uint16_t>& slot,HeadFrame& out);
const char* Describe(uint32_t skip);
//opens the captured skin within reach of the anatomy that opens skin, each mesh posed by its first influence
constexpr float kOpenNear=1.5f,kOpenFar=4.f;
//focused on that anatomy, the skin over it is all the reveal shows, a wider window than the opening
constexpr float kFocusNear=2.f,kFocusFar=5.f;
void OpenOver(const Model& model,HeadFrame& out,bool focused=false);
}
