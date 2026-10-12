#pragma once
#include "Impact.h"
#include "ShotTrace.h"

namespace XRay::Headshots {
//what made a hit open the X-ray
enum Cause : uint8_t {NoCause,Kill,Cripple,Critical,Groin,EveryHit};
//groin: the shot crossed the testes or penis, whatever opened it, so the reveal shows only them
struct Hit {uint32_t target=0,count=0;int32_t location=0;uint32_t region=0;uint8_t cause=NoCause;bool groin=false;Impact::Sample impact;Trace::View launchView;uint64_t time=0;};
bool InitHooks();
void Update();
void Clear();
//the target of a hit already judged to open an X-ray and not yet shown, 0 for none
uint32_t PendingTarget();
}
