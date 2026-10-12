#include "Graphics.h"
#include "FitReference.h"

namespace XRay::Fit {
static Vec3 Row(const float* p){return {p[0],p[1],p[2]};}
const float* Centers(){return kCenters[0];}
float GradientBound(const HeadFit& fit) {
    float radial=0,affine=0;
    for(unsigned i=0;i<kFitAnchors;++i)radial+=Length(Row(fit.coefficients[i]));
    for(unsigned i=kFitAnchors+1;i<kFitCoefficients;++i)
        for(unsigned j=0;j<3;++j)affine+=fit.coefficients[i][j]*fit.coefficients[i][j];
    return radial+sqrtf(affine);
}

void Update(const MaskVertex* vertices,HeadFit& out) {
    out=HeadFit{};
    Vec3 delta[kFitAnchors];
    for(unsigned i=0;i<kFitAnchors;++i) {
        Vec3 point{};const auto& landmark=kLandmarks[i];
        for(unsigned j=0;j<landmark.count;++j)point=point+vertices[landmark.vertices[j]].local;
        point=point*(1.0f/landmark.count);
        if(!Finite(point)){out.strength=0;return;}
        if(i==kChinLandmark)out.chin=point.x;
        auto d=point-Row(kCenters[i]);
        constexpr float maxOffset=3;
        float length=Length(d);
        delta[i]=length>maxOffset?d*(maxOffset/length):d;
    }
    for(unsigned r=0;r<kFitCoefficients;++r) {
        Vec3 value{};
        for(unsigned i=0;i<kFitAnchors;++i)value=value+delta[i]*kSolve[r][i];
        auto& row=out.coefficients[r];row[0]=value.x;row[1]=value.y;row[2]=value.z;
    }
    //each radial gradient has length <=1, which keeps every deformation jacobian nonsingular
    float bound=GradientBound(out);
    constexpr float maxGradient=.75f;
    if(bound>maxGradient) {
        out.strength=maxGradient/bound;
        for(auto& row:out.coefficients)for(unsigned j=0;j<3;++j)row[j]*=out.strength;
    }
}

static Vec3 Evaluate(const HeadFit& fit,Vec3 position,Vec3& jx,Vec3& jy,Vec3& jz) {
    auto a=Row(fit.coefficients[kFitAnchors+1]),b=Row(fit.coefficients[kFitAnchors+2]),c=Row(fit.coefficients[kFitAnchors+3]);
    Vec3 moved=position+Row(fit.coefficients[kFitAnchors])+a*position.x+b*position.y+c*position.z;
    jx=Vec3{1,0,0}+a;jy=Vec3{0,1,0}+b;jz=Vec3{0,0,1}+c;
    for(unsigned i=0;i<kFitAnchors;++i) {
        Vec3 diff=position-Row(kCenters[i]),weight=Row(fit.coefficients[i]);
        float square=diff.x*diff.x+diff.y*diff.y+diff.z*diff.z;
        float inverse=1/sqrtf(square+1e-8f);
        moved=moved+weight*((square+1e-8f)*inverse-.0001f);
        jx=jx+weight*(diff.x*inverse);jy=jy+weight*(diff.y*inverse);jz=jz+weight*(diff.z*inverse);
    }
    return moved;
}
Vec3 Warp(const HeadFit& fit,Vec3 position,Vec3* normal) {
    Vec3 jx,jy,jz,moved=Evaluate(fit,position,jx,jy,jz);
    if(normal) {
        Vec3 n=Cross(jy,jz)*normal->x+Cross(jz,jx)*normal->y+Cross(jx,jy)*normal->z;
        *normal=n*(1/Length(n));
    }
    return moved;
}
Vec3 Unwarp(const HeadFit& fit,Vec3 target) {
    //newton on the bounded-gradient warp, columns jx/jy/jz are the jacobian
    Vec3 p=target;
    for(unsigned i=0;i<12;++i) {
        Vec3 jx,jy,jz,r=Evaluate(fit,p,jx,jy,jz)-target;
        if(Dot(r,r)<1e-12f)break;
        Vec3 a=Cross(jy,jz),b=Cross(jz,jx),c=Cross(jx,jy);
        float det=Dot(jx,a);
        if(!std::isfinite(det)||fabsf(det)<1e-6f)break;
        p=p-Vec3{Dot(a,r),Dot(b,r),Dot(c,r)}*(1/det);
    }
    return p;
}
}
namespace XRay {
Vec3 Posing::Place(Vec3 donor,float bind,float bones,Vec3* normal) const {
    Vec3 p=Fit::Warp(fit,donor,normal)*scale+offset;
    const auto& a=influences[FirstInfluence(bones)%kInfluences];const auto& b=influences[SecondInfluence(bones)%kInfluences];
    if(normal){Vec3 n=a.Rotate(*normal)*(a.scale*bind)+b.Rotate(*normal)*(b.scale*(1-bind));if(Normalize(n))*normal=n;}
    return a.Apply(p)*bind+b.Apply(p)*(1-bind);
}
}
