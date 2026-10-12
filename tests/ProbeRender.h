#pragma once
#include "../src/ShotAudit.h"
#include <vector>

namespace XRay::Probe {
//draws plain shaded world triangles into colour and depth, standing in for the game's own scene
void DrawScene(IDirect3DDevice9* d,const CameraFrame& c,const std::vector<Vec3>& triangles);
//one frame of a record through the plugin's renderer, optionally over a primed scene
std::vector<DWORD> Render(IDirect3DDevice9* d,const Model& model,const Audit::Record& r,Graphics::DamageReport* report=nullptr,const std::vector<Vec3>* scene=nullptr);
//consecutive frames of one record at each age without releasing the renderer between them, as the game draws, the last frame's pixels
std::vector<DWORD> RenderFrames(IDirect3DDevice9* d,const Model& model,Audit::Record& r,const float* ages,unsigned count,const std::vector<Vec3>* scene=nullptr);
}
