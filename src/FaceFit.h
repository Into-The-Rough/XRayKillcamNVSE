#pragma once
#include "Math.h"
#include "Model.h"

namespace XRay {
constexpr unsigned kFitAnchors=9,kFitCoefficients=kFitAnchors+4;
struct MaskVertex;
struct HeadFit {
    float coefficients[kFitCoefficients][4]{};
    float chin=-.574f,strength=1;
};
namespace Fit {
void Update(const MaskVertex* vertices,HeadFit& out);
const float* Centers();
Vec3 Warp(const HeadFit& fit,Vec3 position,Vec3* normal=nullptr);
Vec3 Unwarp(const HeadFit& fit,Vec3 target);
float GradientBound(const HeadFit& fit);
}
//donor anatomy to posed frame space: the face fit, preview scale and offset, then the blend of its two influences
struct Posing {
    HeadFit fit;float scale=1;Vec3 offset{};
    //rest frame space to posed frame space for skin bound only to each influence, the first is the frame bone itself
    Influences influences=RestInfluences();
    Vec3 Place(Vec3 donor,float bind,float bones,Vec3* normal=nullptr) const;
};
}
