#include "Runtime.h"
#include "Headshots.h"

//other plugins ask which actor an X-ray killcam shows, or is about to from a hit already judged, to hold their own effects on it
extern "C" uint32_t __cdecl XRayKillcamShownActor() {
    if(auto target=XRay::ShownTarget())return target;
    return XRay::Headshots::PendingTarget();
}
