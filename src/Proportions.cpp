#include "Proportions.h"
#include "Graphics.h"
#include "HumanRest.h"

namespace XRay {
namespace {
//spans outside this range are a broken skeleton, not a creature
constexpr float kSmallest=.5f,kLargest=3;
float Ratio(float measured,float human){return measured>0&&human>0?Clamp(measured/human,kSmallest,kLargest):0;}
}
namespace Creature {
Proportions Measure(uint32_t region,const Spans& spans) {
    Proportions out;
    if(region>=kRegions)return out;
    //the region's chain sizes its trunk, the crown its head, a limb's frame carries no anatomy
    float whole=Ratio(spans.chain,kHumanChain[region]);
    if(!whole)whole=Ratio(spans.crown,kHumanCrown);
    if(!whole)whole=1;
    for(unsigned k=0;k<kInfluences;++k) {
        float own=Ratio(spans.reach[k],kHumanReach[region][k]);
        out.scale[k]=own?own:whole;
    }
    float wide=0;unsigned measured=0;
    for(unsigned w=0;w<2;++w)if(float r=Ratio(spans.widths[w],kHumanWidths[region][w])){wide+=r;++measured;}
    if(measured) {
        float g=Clamp(wide/float(measured)/whole,kSmallest,kLargest);
        out.girth={1,g,g};
    }
    return out;
}
Transform Retarget(uint32_t region,unsigned k,const Proportions& proportions) {
    Transform t=kHumanRest[region<kRegions?region:0][k%kInfluences];
    float s=proportions.scale[k%kInfluences];
    t.t=t.t*s;t.scale*=s/proportions.scale[0];
    return t;
}
HeadFit Fit(const Proportions& proportions) {
    HeadFit fit{};Vec3 axes=proportions.girth*proportions.scale[0];
    fit.coefficients[kFitAnchors+1][0]=axes.x-1;fit.coefficients[kFitAnchors+2][1]=axes.y-1;fit.coefficients[kFitAnchors+3][2]=axes.z-1;
    return fit;
}
}
float BodySize(const HeadFrame& frame) {
    const auto& c=frame.fit.coefficients;
    float size=fmaxf(fmaxf(1+c[kFitAnchors+1][0],1+c[kFitAnchors+2][1]),fmaxf(1+c[kFitAnchors+3][2],1));
    for(const auto& t:frame.influences)if(t.Valid())size=fmaxf(size,t.scale);
    return size;
}
}
