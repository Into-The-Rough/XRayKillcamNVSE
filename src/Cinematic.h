#pragma once
#include <cstdint>

namespace XRay::Cinematic {
//force runs the killcam in full mode whatever the game's kill cam option says
void Queue(uint32_t target,int32_t bodyPart,bool force=false);
void Update();
void Clear();
}
