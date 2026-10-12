#pragma once
#include "Graphics.h"

namespace XRay::RevealMask {
constexpr unsigned kApertureTriangles=54;
void BuildApertures(const HeadFrame& head,MaskVertex* triangles);
}
