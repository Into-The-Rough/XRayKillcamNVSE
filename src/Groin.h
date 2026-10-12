#pragma once
#include "Graphics.h"
#include "Impact.h"
#include "Model.h"

namespace XRay::Groin {
//anatomy that opens skin, the testes and penis, is what a groin shot has to cross
//before any capture: whether a frame-space line passes within a margin of that anatomy's rest box
bool Near(const Model& torso,Vec3 point,Vec3 direction,float margin);
//after mapping: whether the path, widened by the bullet, crosses that anatomy where its influences hold it now
bool Crossed(const Model& torso,const HeadFrame& frame,const Impact::Path& path,float radius);
}
