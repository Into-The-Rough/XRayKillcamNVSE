#pragma once
#include "Graphics.h"

namespace XRay::Audit {
constexpr uint32_t kMagic=0x31435258;
struct Record {
    uint32_t magic=kMagic,version=6,size=0,target=0,support=0,stage=0;
    uint64_t session=0;
    HeadFrame head{};
    CameraFrame camera{};
    Appearance appearance{};
    Impact::Sample source{};
    Impact::Frame impact{};
};
bool Project(const CameraFrame& camera,Vec3 world,Vec3& screen);
bool CentreRay(const CameraFrame& camera,Vec3& origin,Vec3& direction);
bool Read(const char* path,Record& out);
void Init(const char* runtime);
void Select(uint32_t generation);
void Submit(uint32_t generation,uint32_t target,const CameraFrame& camera,const HeadFrame& head,
    const Appearance& appearance,const Impact::Sample& source,const Impact::Frame& impact,unsigned support);
void Drain();
}
