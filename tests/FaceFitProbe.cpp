#include "../src/Graphics.h"
#include "../src/FitReference.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace XRay;
static void Require(bool ok,const char* name){if(!ok){printf("FAIL: %s\n",name);exit(1);}}
static Vec3 Base(unsigned i){return {Fit::kCenters[i][0],Fit::kCenters[i][1],Fit::kCenters[i][2]};}
static void Assign(MaskVertex* vertices,unsigned i,Vec3 point) {
    const auto& g=Fit::kLandmarks[i];
    for(unsigned j=0;j<g.count;++j)vertices[g.vertices[j]].local=point;
}
static void Landmarks(const HeadFit& fit,const Vec3* points) {
    for(unsigned i=0;i<kFitAnchors;++i) {
        Vec3 d=points[i]-Base(i);float length=Length(d);
        if(length>3)d=d*(3/length);
        Require(Length(Fit::Warp(fit,Base(i))-(Base(i)+d*fit.strength))<.0002f,"landmarks interpolate their target displacement");
    }
}
static void Differential(const HeadFit& fit) {
    constexpr float e=.002f;
    Vec3 n{.2f,.3f,.9f};n=n*(1/Length(n));
    Vec3 t=Cross(n,{0,0,1});t=t*(1/Length(t));
    for(float x=-.8f;x<15;x+=1.1f)for(float y=-6;y<8;y+=1.3f)for(float z=-5;z<5;z+=1.2f) {
        Vec3 p{x,y,z},normal=n;
        auto moved=Fit::Warp(fit,p,&normal);
        auto a=(Fit::Warp(fit,p+Vec3{e,0,0})-Fit::Warp(fit,p-Vec3{e,0,0}))*(.5f/e);
        auto b=(Fit::Warp(fit,p+Vec3{0,e,0})-Fit::Warp(fit,p-Vec3{0,e,0}))*(.5f/e);
        auto c=(Fit::Warp(fit,p+Vec3{0,0,e})-Fit::Warp(fit,p-Vec3{0,0,e}))*(.5f/e);
        Require(Finite(moved)&&Finite(normal)&&Dot(a,Cross(b,c))>.01f,"positive finite deformation jacobian");
        auto tangent=(Fit::Warp(fit,p+t*e)-Fit::Warp(fit,p-t*e))*(.5f/e);
        Require(fabsf(Dot(normal,tangent))<.004f,"warped normals remain perpendicular to the surface");
    }
}
HeadFit TestFaceFit(const Model& model) {
    MaskVertex vertices[kHeadVertices]{};Vec3 points[kFitAnchors];HeadFit fit;
    for(unsigned i=0;i<kFitAnchors;++i){points[i]=Base(i);Assign(vertices,i,points[i]);}
    Fit::Update(vertices,fit);Landmarks(fit,points);
    for(const auto& m:model.meshes)for(const auto& v:m.vertices)
        Require(Length(Fit::Warp(fit,v.position)-v.position)<.0001f,"neutral head leaves anatomy unchanged");
    for(unsigned i=0;i<kFitAnchors;++i) {
        auto p=Base(i);points[i]={p.x*1.03f+.1f,p.y*.98f+.2f,p.z*1.04f-.05f};Assign(vertices,i,points[i]);
    }
    Fit::Update(vertices,fit);Require(fit.strength==1,"small affine fit is not damped");Landmarks(fit,points);
    for(const auto& m:model.meshes)for(const auto& v:m.vertices) {
        Vec3 wanted{v.position.x*1.03f+.1f,v.position.y*.98f+.2f,v.position.z*1.04f-.05f};
        Require(Length(Fit::Warp(fit,v.position)-wanted)<.0002f,"affine precision across the whole anatomy");
    }
    FILE* file=nullptr;fopen_s(&file,"tests/output/fit/faces.bin","rb");Require(file!=nullptr,"private male/female landmark fixtures");
    for(unsigned face=0;face<2;++face) {
        Require(fread(points,sizeof points,1,file)==1,"landmark fixture bytes");
        for(unsigned i=0;i<kFitAnchors;++i)Assign(vertices,i,points[i]);
        Fit::Update(vertices,fit);Landmarks(fit,points);Differential(fit);
        float before=0,after=0;
        for(unsigned i=0;i<kFitAnchors;++i) {
            before+=Dot(points[i]-Base(i),points[i]-Base(i));
            auto error=points[i]-Fit::Warp(fit,Base(i));after+=Dot(error,error);
        }
        printf("PASS: %s head landmarks RMS %.6f -> %.6f, fit strength %.3f\n",face?"female":"male",sqrtf(before/kFitAnchors),sqrtf(after/kFitAnchors),fit.strength);
        if(face)Require(after<before*.1f,"real female head fit improves landmark error");
    }
    fclose(file);
    HeadFit female=fit;
    for(unsigned i=0;i<kFitAnchors;++i){points[i]=Base(i)+Vec3{i%2?8.0f:-8.0f,4,-3};Assign(vertices,i,points[i]);}
    Fit::Update(vertices,fit);Require(fit.strength>0&&fit.strength<1,"extreme shape fit is damped");Landmarks(fit,points);Differential(fit);
    vertices[Fit::kLandmarks[0].vertices[0]].local.x=std::numeric_limits<float>::quiet_NaN();
    Fit::Update(vertices,fit);Require(fit.strength==0,"nonfinite landmark fails to identity");
    Require(Length(Fit::Warp(fit,{2,3,4})-Vec3{2,3,4})==0,"invalid fit leaves anatomy unchanged");
    printf("PASS: fitting identity, affine precision, landmark interpolation, normal Jacobian and deformation limits\n");
    return female;
}
