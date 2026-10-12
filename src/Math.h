#pragma once
#include <cmath>

namespace XRay {
struct Vec3 { float x,y,z; };
inline Vec3 operator+(Vec3 a,Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 operator-(Vec3 a,Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 operator*(Vec3 a,float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float Dot(Vec3 a,Vec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 Cross(Vec3 a,Vec3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float Length(Vec3 a) {return sqrtf(Dot(a,a));}
inline bool Finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
inline bool Normalize(Vec3& v) {float length=Length(v);if(!std::isfinite(length)||length<.0001f)return false;v=v*(1/length);return true;}
inline float Clamp(float v,float lo,float hi) { return v<lo?lo:v>hi?hi:v; }
struct Transform {
    float r[9]; Vec3 t; float scale;
    static Transform Identity() { return {{1,0,0,0,1,0,0,0,1},{0,0,0},1}; }
    Vec3 Rotate(Vec3 p) const {
        return {r[0]*p.x+r[1]*p.y+r[2]*p.z,r[3]*p.x+r[4]*p.y+r[5]*p.z,r[6]*p.x+r[7]*p.y+r[8]*p.z};
    }
    Vec3 Apply(Vec3 p) const { return Rotate(p)*scale+t; }
    Vec3 Unrotate(Vec3 p) const {return {r[0]*p.x+r[3]*p.y+r[6]*p.z,r[1]*p.x+r[4]*p.y+r[7]*p.z,r[2]*p.x+r[5]*p.y+r[8]*p.z};}
    Vec3 Unapply(Vec3 p) const {
        p=(p-t)*(1/scale);
        return {r[0]*p.x+r[3]*p.y+r[6]*p.z,r[1]*p.x+r[4]*p.y+r[7]*p.z,r[2]*p.x+r[5]*p.y+r[8]*p.z};
    }
    bool Valid() const {
        for (float v:r) if (!std::isfinite(v)) return false;
        return Finite(t)&&std::isfinite(scale)&&scale>0.001f&&scale<100;
    }
};
static_assert(sizeof(Transform)==0x34);
//a after b
inline Transform Compose(const Transform& a,const Transform& b) {
    Transform out;
    for (unsigned i=0;i<3;++i) for (unsigned j=0;j<3;++j) out.r[i*3+j]=a.r[i*3]*b.r[j]+a.r[i*3+1]*b.r[3+j]+a.r[i*3+2]*b.r[6+j];
    out.t=a.Rotate(b.t)*a.scale+a.t;out.scale=a.scale*b.scale;
    return out;
}
inline Transform Inverse(const Transform& a) {
    Transform out={{a.r[0],a.r[3],a.r[6],a.r[1],a.r[4],a.r[7],a.r[2],a.r[5],a.r[8]},{},1/a.scale};
    out.t=out.Rotate(a.t)*-out.scale;
    return out;
}
inline float RotationAngle(const Transform& a) {
    return acosf(Clamp((a.r[0]+a.r[4]+a.r[8]-1)*.5f,-1,1));
}
}
