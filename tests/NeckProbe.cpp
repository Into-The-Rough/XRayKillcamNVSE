#include "../src/Damage.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace XRay;
using namespace XRay::Damage;
static void Check(bool ok,const char* name){if(!ok){printf("FAIL: %s\n",name);exit(1);}}
Transform TestRotation(Vec3 axis,float degrees,Vec3 pivot) {
    Normalize(axis);float a=degrees*.01745329252f,c=cosf(a),s=sinf(a),k=1-c;
    Transform out={{c+axis.x*axis.x*k,axis.x*axis.y*k-axis.z*s,axis.x*axis.z*k+axis.y*s,
        axis.y*axis.x*k+axis.z*s,c+axis.y*axis.y*k,axis.y*axis.z*k-axis.x*s,
        axis.z*axis.x*k-axis.y*s,axis.z*axis.y*k+axis.x*s,c+axis.z*axis.z*k},{},1};
    out.t=pivot-out.Rotate(pivot);return out;
}
static float LineDistance(Vec3 p,Vec3 origin,Vec3 direction) {
    Vec3 d=p-origin;return Length(d-direction*Dot(d,direction));
}
struct Hit {float t;Vec3 point;float bind;unsigned mesh;};
//brute force: pose every bound mesh and intersect the head-space path
static std::vector<Hit> Oracle(const Model& model,const Surface& surface,const Posing& posing,Vec3 entry,Vec3 exit) {
    std::vector<Hit> hits;Vec3 d=exit-entry;float length=Length(d);Normalize(d);
    for(unsigned n=0;n<model.meshes.size();++n) {
        const auto& mesh=model.meshes[n];
        if(!mesh.bound||surface.SetOf(n)==Surface::kNone)continue;
        std::vector<Vec3> posed(mesh.vertices.size());
        for(size_t i=0;i<posed.size();++i)posed[i]=posing.Place(mesh.vertices[i].position,mesh.vertices[i].bind,mesh.vertices[i].bones);
        for(size_t i=0;i+2<mesh.indices.size();i+=3) {
            const auto &va=mesh.vertices[mesh.indices[i]],&vb=mesh.vertices[mesh.indices[i+1]],&vc=mesh.vertices[mesh.indices[i+2]];
            Vec3 a=posed[mesh.indices[i]],e1=posed[mesh.indices[i+1]]-a,e2=posed[mesh.indices[i+2]]-a,p=Cross(d,e2);
            float det=Dot(e1,p);if(fabsf(det)<1e-12f)continue;
            float inverse=1/det;Vec3 s=entry-a;float u=Dot(s,p)*inverse;if(u<0||u>1)continue;
            Vec3 q=Cross(s,e1);float v=Dot(d,q)*inverse;if(v<0||u+v>1)continue;
            float t=Dot(e2,q)*inverse;if(!(t>=-.6f&&t<=length+.6f))continue;
            hits.push_back({t,va.position+(vb.position-va.position)*u+(vc.position-va.position)*v,va.bind+(vb.bind-va.bind)*u+(vc.bind-va.bind)*v,n});
        }
    }
    std::sort(hits.begin(),hits.end(),[](const Hit& a,const Hit& b){return a.t<b.t;});
    return hits;
}
void TestNeck(const Model& model) {
    unsigned bound=0,lower=0;
    for(const auto& mesh:model.meshes) {
        if(!mesh.bound){for(const auto& v:mesh.vertices)Check(v.bind==1,"skull, teeth and brain stay rigid with the head");continue;}
        ++bound;float lo=1;for(const auto& v:mesh.vertices)lo=fminf(lo,v.bind);
        lower+=lo<.5f;
    }
    Check(bound==12&&lower>=6,"twelve neck batches, the lower neck mostly follows the neck bone");
    Surface surface;surface.Reserve(model);
    HeadFit fit;fit.coefficients[kFitAnchors][0]=.3f;fit.coefficients[kFitAnchors+2][1]=.04f;fit.coefficients[2][2]=.05f;
    Posing posing{fit,1,{}};
    for(float x=-6;x<=12;x+=1.5f)for(float b=0;b<=1;b+=.25f) {
        Vec3 n{0,1,0},m{0,1,0};Vec3 a=posing.Place({x,1,.5f},b,1,&n),w=Fit::Warp(fit,{x,1,.5f},&m);
        Check(Length(a-w)<1e-5f&&Length(n-m)<1e-5f,"a neck at rest leaves the face fit unchanged");
    }
    posing.influences[1]=TestRotation({0,0,1},30,{0,0,0});posing.scale=1.1f;posing.offset={.2f,-.1f,.05f};
    for(float x=-6;x<=6;x+=2) {
        Vec3 p{x,.5f,-.3f},w=Fit::Warp(fit,p)*1.1f+posing.offset;
        Check(Length(posing.Place(p,0,1)-posing.influences[1].Apply(w))<1e-4f,"neck-only anatomy follows the neck bone");
        Check(Length(posing.Place(p,.25f,1)-(w*.25f+posing.influences[1].Apply(w)*.75f))<1e-4f,"blended anatomy is the linear skin blend");
    }
    //oracle comparison under rest, nods, a turn and a tilt, with the prefilter active
    const Transform bends[]={Transform::Identity(),TestRotation({0,0,1},25,{0,0,0}),TestRotation({0,0,1},-40,{0,0,0}),
        TestRotation({1,0,0},35,{0,0,0}),TestRotation({0,1,0},20,{-2,0,0})};
    std::vector<Crossing> crossings(1024);std::vector<Vec3> scratch;
    unsigned paths=0,checked=0;float worstPoint=0,worstT=0;uint32_t random=12345;
    auto next=[&]{random^=random<<13;random^=random>>17;random^=random<<5;return float(random&0xFFFF)/65535.0f;};
    for(const auto& bend:bends) {
        Posing pose{fit,1,{}};pose.influences[1]=bend;
        for(unsigned k=0;k<40;++k) {
            const Mesh* mesh=nullptr;
            while(!mesh||!mesh->bound)mesh=&model.meshes[unsigned(next()*float(model.meshes.size()-1))];
            const auto& v=mesh->vertices[unsigned(next()*float(mesh->vertices.size()-1))];
            Vec3 target=pose.Place(v.position,v.bind,v.bones)+Vec3{next()-.5f,next()-.5f,next()-.5f}*.6f;
            Vec3 d{next()-.5f,next()-.5f,next()-.5f};if(!Normalize(d))continue;
            Vec3 entry=target-d*7,exit=target+d*7;
            unsigned n=surface.TraceBound(model,pose,entry,exit,14,-.6f,14.6f,crossings.data(),1024,scratch);
            std::sort(crossings.begin(),crossings.begin()+n,[](const Crossing& a,const Crossing& b){return a.t<b.t;});
            auto truth=Oracle(model,surface,pose,entry,exit);
            Check(n==truth.size(),"posed neck crossings match brute force with the prefilter");
            for(unsigned i=0;i<n;++i) {
                worstT=fmaxf(worstT,fabsf(crossings[i].t-truth[i].t));
                worstPoint=fmaxf(worstPoint,LineDistance(pose.Place(crossings[i].point,truth[i].bind,1),entry,d));
                ++checked;
            }
            ++paths;
        }
    }
    printf("neck tracing: paths=%u crossings=%u worstT=%.6f worstPoint=%.6f\n",paths,checked,worstT,worstPoint);
    //posing is exact at vertices and flat between them, so the smooth map of a crossing strays by the triangle curvature
    Check(checked>100&&worstT<1e-3f&&worstPoint<.05f,"donor crossing points pose back onto the shot");
    //a nod moves the lower neck away from where a rigid head would put it, planning still breaks along the real shot
    Posing bent{fit,1,{}};bent.influences[1]=TestRotation({0,0,1},-35,{0,0,0});
    const Mesh* c6=nullptr;Vec3 centre{};
    for(const auto& mesh:model.meshes)if(mesh.bound&&mesh.material==kBoneMaterial) {
        Vec3 sum{};for(const auto& v:mesh.vertices)sum=sum+v.position;sum=sum*(1.0f/float(mesh.vertices.size()));
        if(sum.x<-2.2f&&sum.x>-3.2f){c6=&mesh;centre=sum;}
    }
    Check(c6!=nullptr,"sixth cervical vertebra present");
    float bind=0;for(const auto& v:c6->vertices)bind+=v.bind;bind/=float(c6->vertices.size());
    Vec3 target=bent.Place(centre,bind,1),through{0,0,1};
    Check(Length(target-Fit::Warp(fit,centre))>1,"the nod displaces the vertebra by more than its thickness");
    Shot shot;shot.entry=target-through*6;shot.exit=target+through*6;
    shot.origin=Fit::Unwarp(fit,shot.entry);Vec3 donorExit=Fit::Unwarp(fit,shot.exit);
    shot.direction=donorExit-shot.origin;shot.length=Length(shot.direction);Normalize(shot.direction);
    shot.secondsPerUnit=.003f;shot.sequence=9;shot.posing=&bent;
    Seed seeds[kSets][kBoneSeeds];Cluster clusters[kClusters];Plan plan;
    for(unsigned set=0;set<kSets;++set){plan.seeds[set]=seeds[set];plan.capacity[set]=kSeedCapacity[set];}
    plan.clusters=clusters;plan.clusterCapacity=kClusters;
    auto runs=PlanWound(model,surface,shot,plan);
    Check(runs.bone>=1&&plan.clusterCount>=1,"bent neck shot breaks the vertebra it crosses");
    float worstCluster=0;
    for(unsigned c=0;c<plan.clusterCount;++c) {
        float nearest=1e9f,b=1;
        for(const auto& mesh:model.meshes)if(mesh.bound)for(const auto& v:mesh.vertices) {
            float q=Length(v.position-clusters[c].center);if(q<nearest){nearest=q;b=v.bind;}
        }
        worstCluster=fmaxf(worstCluster,LineDistance(bent.Place(clusters[c].center,b,1),shot.entry,through));
    }
    Check(worstCluster<.35f,"break centres sit on the shot after the bend");
    Shot rigid=shot;rigid.posing=nullptr;Plan none=plan;none.count[BoneSet]=none.count[BrainSet]=0;none.clusterCount=0;
    Check(PlanWound(model,surface,rigid,none).bone<runs.bone,"the vertebra is found only through the bend");
    printf("bent neck plan: runs=%u/%u clusters=%u worst centre offset=%.3f\n",runs.bone,runs.brain,plan.clusterCount,worstCluster);
    printf("PASS: neck binding, posed tracing and planning under bends\n");
}
