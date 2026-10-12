#pragma once
#include "Math.h"
#include <cstdint>

namespace XRay::Trace {
struct View {Vec3 origin{},forward{};bool valid=false;};
struct Ray {Vec3 origin{},direction{};View view;};
bool InitHooks();
void Clear();
bool Read(uint32_t id,uintptr_t identity,uint64_t now,Ray& out);
}
