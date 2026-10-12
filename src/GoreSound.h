#pragma once
#include "Cues.h"

namespace XRay::GoreSound {
//sounds one cue from its folder, a random clip at the cue's level and a slightly varied speed, false when the folder has none
bool Play(uint8_t cue,float volume);
const char* Name(uint8_t cue);
}
