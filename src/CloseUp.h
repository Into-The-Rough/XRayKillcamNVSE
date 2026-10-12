#pragma once
#include "Math.h"
#include <cstdint>

namespace XRay::CloseUp {
//the wound the close-up looks at, in the X-ray frame's space, a new target or region restarts the shot
void Track(uint32_t target,uint32_t region,Vec3 entry,Vec3 exit);
void Clear();
bool Install();
//the renderer's camera position this frame, to report whether the close-up was the camera drawn
void Rendered(Vec3 origin);
//how high above the wound the camera sits, as a share of its distance
constexpr float kRise=.2f;
//a camera behind the entry along the shot, raised a little and turned about the vertical by angle radians,
//it looks along its first column with its second up, the third keeps the live camera's handedness
inline Transform Pose(Vec3 wound,Vec3 shot,float distance,float angle,float handedness) {
    Vec3 back=shot*-1.f;if(!Normalize(back))back={0,-1,0};
    Vec3 offset=back*distance+Vec3{0,0,distance*kRise};
    float c=cosf(angle),s=sinf(angle);
    Vec3 eye=wound+Vec3{offset.x*c-offset.y*s,offset.x*s+offset.y*c,offset.z};
    Vec3 look=wound-eye;Normalize(look);
    Vec3 up=Vec3{0,0,1}-look*look.z;if(!Normalize(up)){up=Vec3{0,1,0}-look*look.y;Normalize(up);}
    Vec3 side=Cross(look,up)*(handedness<0?-1.f:1.f);
    return {{look.x,up.x,side.x,look.y,up.y,side.y,look.z,up.z,side.z},eye,1};
}
//the camera keeps this far in front of a wall and no nearer the wound than the floor, and backs out again this fast a second
constexpr float kWallMargin=4,kNearest=8,kRegrow=30;
//the camera trails the wound by this time constant in seconds, so a frame of the bone's jitter barely moves it
constexpr float kFollow=.12f;
//the followed point seconds later, eased toward where the wound is now the same way at any frame rate
inline Vec3 Follow(Vec3 from,Vec3 to,float seconds){return from+(to-from)*(1-expf(-fmaxf(seconds,0)/kFollow));}
//turns off the shot's line tried once per shot when something stands behind the entry, nearest the line first
constexpr float kTurns[]={0,.7854f,-.7854f,1.5708f,-1.5708f,2.3562f,-2.3562f,3.1416f};
struct Framing {float turn=0,distance=-1;bool chosen=false;};
//room(point) is the free distance from the wound toward point, negative when it could not be tested: the first clear turn,
//else the roomiest, is kept for the shot, and the camera comes in at once to the room it has and backs out slowly
template<class Room> Transform Frame(Framing& framing,Vec3 wound,Vec3 shot,float distance,float angle,float handedness,float seconds,Room room) {
    const float slant=sqrtf(1+kRise*kRise),want=distance*slant+kWallMargin;
    //each test reaches the margin past the eye
    auto test=[&](float turn){Vec3 out=Pose(wound,shot,distance,angle+turn,handedness).t-wound;Normalize(out);return room(wound+out*want);};
    if(!framing.chosen) {
        float best=-1,turn=0;bool known=true;
        for(float candidate:kTurns) {
            float free=test(candidate);
            if(free<0){known=false;break;}
            if(free>=want-.01f){turn=candidate;break;}
            if(free>best+.5f){best=free;turn=candidate;}
        }
        if(known){framing.turn=turn;framing.chosen=true;}
    }
    float free=test(framing.turn);
    float allowed=free<0?(framing.distance<0?distance:framing.distance):fmaxf(fminf(distance,(free-kWallMargin)/slant),kNearest);
    if(framing.distance<0||allowed<framing.distance)framing.distance=allowed;
    else framing.distance=fminf(allowed,framing.distance+kRegrow*seconds);
    return Pose(wound,shot,framing.distance,angle+framing.turn,handedness);
}
}
