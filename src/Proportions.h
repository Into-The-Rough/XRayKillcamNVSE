#pragma once
#include "FaceFit.h"
#include "Model.h"
#include <cstdint>

namespace XRay {
struct HeadFrame;
//a creature's spans in its frame bone's units, 0 where unmeasured: each influence's own length, its region's chain and
//widths, and its head's crown
struct Spans {float reach[kInfluences]{},chain=0,widths[2]{},crown=0;};
//how much larger than the human anatomy each influence's part is, and the region's girth across its frame's x axis
struct Proportions {float scale[kInfluences]{1,1,1,1,1,1,1};Vec3 girth{1,1,1};};
namespace Creature {
Proportions Measure(uint32_t region,const Spans& spans);
//the human rest relation of influence k carried to a creature's proportions, the frame's own scale stays in the fit
Transform Retarget(uint32_t region,unsigned k,const Proportions& proportions);
//the frame's scale and the region's girth, applied to the anatomy before its influences
HeadFit Fit(const Proportions& proportions);
}
//how much larger than a human's a capture's body is, for bounds measured on human bodies
float BodySize(const HeadFrame& frame);
}
