#pragma once
#include <cstdint>

namespace XRay::Ammunition {
struct Profile {
    float radius=.26f,length=1.7f,nose=.65f;
    bool known=false;
    float Bore() const {return radius*(.32f/.26f);}
    float Section() const {return radius*(.36f/.26f);}
    float Depth() const {return radius*(.33f/.26f);}
};
Profile Resolve(uint32_t ammo);
}
