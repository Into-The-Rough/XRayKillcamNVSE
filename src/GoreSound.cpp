#include "GoreSound.h"
#include "Engine.h"
#include <windows.h>

namespace XRay::GoreSound {
//loose files are found from the game folder, so the path keeps its data prefix, archives trim it
static const char* const kFolders[Damage::kCueKinds]={"Data\\Sound\\fx\\xraykillcam\\hit\\","Data\\Sound\\fx\\xraykillcam\\bone\\",
    "Data\\Sound\\fx\\xraykillcam\\organ\\","Data\\Sound\\fx\\xraykillcam\\burst\\","Data\\Sound\\fx\\xraykillcam\\spray\\"};
//each moment's level against the others, the clips are only loudness-matched within their own kind
constexpr float kLevels[Damage::kCueKinds]={.8f,1,.85f,1,.7f};
//repeats of one clip vary by this much either way in speed
constexpr float kSpeedSpread=.06f;
//the native pick's no-repeat ring never advances, so each cue remembers its own last clip
static char s_last[Damage::kCueKinds][260];
const char* Name(uint8_t cue) {
    static const char* const names[Damage::kCueKinds]={"hit","bone","organ","burst","spray"};
    return cue<Damage::kCueKinds?names[cue]:"unknown";
}
bool Play(uint8_t cue,float volume) {
    if(cue>=Damage::kCueKinds)return false;
    static uint32_t s_seed=GetTickCount()|1;
    s_seed=s_seed*1664525u+1013904223u;
    float speed=1+kSpeedSpread*(float(s_seed>>8&0xFFFF)/32767.5f-1);
    return Engine::PlayFromFolder(kFolders[cue],s_last[cue],volume*kLevels[cue],speed);
}
}
