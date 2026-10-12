#include "RevealMask.h"

namespace XRay::RevealMask {
//boundary rings in the supported 1211-vertex face, the neck/seam boundary stays open
static constexpr uint16_t kMouth[]={343,344,699,764,703,700,701,931,933,980,930,389,390,392,393,975,928,929,932,720,702,697,698,759,346,347};
static constexpr uint16_t kRightEye[]={417,418,725,727,729,728,623,624,642,445,446,459,428,429};
static constexpr uint16_t kLeftEye[]={464,465,475,476,507,480,481,885,866,867,944,945,943,941};
static_assert(sizeof(kMouth)/sizeof(*kMouth)+sizeof(kRightEye)/sizeof(*kRightEye)+sizeof(kLeftEye)/sizeof(*kLeftEye)==kApertureTriangles);
template<unsigned N> static void CloseRing(const MaskVertex* source,const uint16_t (&ring)[N],MaskVertex*& out) {
    MaskVertex center{{},{},0};
    for(auto index:ring){center.world=center.world+source[index].world;center.local=center.local+source[index].local;center.reveal+=source[index].reveal;}
    center.world=center.world*(1.0f/N);center.local=center.local*(1.0f/N);center.reveal*=1.0f/N;
    for(unsigned i=0;i<N;++i){*out++=center;*out++=source[ring[i]];*out++=source[ring[(i+1)%N]];}
}
void BuildApertures(const HeadFrame& head,MaskVertex* triangles) {
    CloseRing(head.vertices,kMouth,triangles);
    CloseRing(head.vertices,kRightEye,triangles);
    CloseRing(head.vertices,kLeftEye,triangles);
}
}
