#include "../src/Damage.h"
#include "RecordedShots.h"
#include "../src/ShotAudit.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {
using namespace XRay;
unsigned checks=0;
void Check(bool ok,const char* name){++checks;if(!ok){printf("FAIL: %s\n",name);exit(1);}}
void Hr(HRESULT hr){Check(SUCCEEDED(hr),"fracture probe D3D call");}
//closed box surface, inverted boxes bound a cavity so outer+inner make a hollow solid wall
void AddBox(Mesh& mesh,Vec3 lo,Vec3 hi,bool inward) {
    Vec3 e=hi-lo,x={e.x,0,0},y={0,e.y,0},z={0,0,e.z};
    //corner and two edges per face, ordered so cross(u,v) points out of the box
    const Vec3 faces[6][3]={{lo,y,x},{{lo.x,lo.y,hi.z},x,y},{lo,x,z},{{lo.x,hi.y,lo.z},z,x},{lo,z,y},{{hi.x,lo.y,lo.z},y,z}};
    constexpr unsigned n=24;
    for(const auto& face:faces) {
        Vec3 a=face[0],u=inward?face[2]:face[1],v=inward?face[1]:face[2];
        Vec3 normal=Cross(u,v);Normalize(normal);
        auto base=uint16_t(mesh.vertices.size());
        for(unsigned j=0;j<=n;++j)for(unsigned i=0;i<=n;++i)mesh.vertices.push_back({a+u*(float(i)/n)+v*(float(j)/n),normal,1});
        for(unsigned j=0;j<n;++j)for(unsigned i=0;i<n;++i) {
            uint16_t k=uint16_t(base+j*(n+1)+i);
            const uint16_t quad[]={k,uint16_t(k+1),uint16_t(k+n+2),k,uint16_t(k+n+2),uint16_t(k+n+1)};
            mesh.indices.insert(mesh.indices.end(),quad,quad+6);
        }
    }
}
Model Shell() {
    Model model;
    Mesh bone;bone.material=kBoneMaterial;bone.visibility=1;
    AddBox(bone,{-3.5f,-3.5f,-3.5f},{3.5f,3.5f,3.5f},false);AddBox(bone,{-3.1f,-3.1f,-3.1f},{3.1f,3.1f,3.1f},true);
    Mesh brain;brain.material=kBrainMaterial;brain.visibility=1;AddBox(brain,{-2.5f,-2.5f,-2.5f},{2.5f,2.5f,2.5f},false);
    model.meshes.push_back(std::move(bone));model.meshes.push_back(std::move(brain));return model;
}
int Nearest(const Damage::Seed* seeds,unsigned count,Vec3 p,float* distance=nullptr) {
    int best=-1;float q=1e30f;
    for(unsigned i=0;i<count;++i){Vec3 d=p-seeds[i].position;float s=Dot(d,d);if(s<q){q=s;best=int(i);}}
    if(distance)*distance=sqrtf(q);
    return best;
}
bool Broken(const Damage::Seed* seeds,unsigned count,Vec3 p) {
    float distance;int i=Nearest(seeds,count,p,&distance);
    return i>=0&&seeds[i].state!=Damage::Anchor&&distance<seeds[i].reach;
}
//independent ray oracle over raw triangles, for which anatomy a straight path really crosses
bool Crosses(const Model& model,unsigned material,Vec3 origin,Vec3 direction,float length) {
    for(const auto& mesh:model.meshes) {
        if(!(mesh.visibility&1)||(mesh.material!=material&&!(material==kBoneMaterial&&mesh.material==kTeethMaterial)))continue;
        for(size_t i=0;i<mesh.indices.size();i+=3) {
            Vec3 a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
            Vec3 e1=b-a,e2=c-a,p=Cross(direction,e2);float det=Dot(e1,p);if(fabsf(det)<1e-12f)continue;
            Vec3 s=origin-a;float u=Dot(s,p)/det;if(u<0||u>1)continue;Vec3 q=Cross(s,e1);float v=Dot(direction,q)/det;
            if(v<0||u+v>1)continue;float t=Dot(e2,q)/det;if(t>=-.6f&&t<=length+.6f)return true;
        }
    }
    return false;
}
Impact::Frame FrameFor(const Impact::Path& path,float age,uint64_t sequence,float sceneTime=-1,uint32_t ammo=0x6B53D) {
    Impact::Frame frame;frame.path=path;frame.path.valid=true;frame.age=age;frame.bullet=Ammunition::Resolve(ammo);
    frame.sequence=sequence;frame.sceneTime=sceneTime;return frame;
}
double Area(Vec3 a,Vec3 b,Vec3 c){return Length(Cross(b-a,c-a))*.5;}
void CheckCarving(const Model& model,const Damage::Seed* seeds,unsigned count,unsigned set,const char* label) {
    Damage::Surface surface;surface.Reserve(model);
    auto carver=std::make_unique<Damage::Carver>();
    std::vector<uint8_t> selected(count,1);
    unsigned cells=carver->Carve(model,surface,set,seeds,count,selected.data());
    double carved=0;unsigned outside=0,vertices=0;
    for(unsigned cell=0;cell<cells;++cell) {
        unsigned seed=carver->SeedOf(cell);
        for(unsigned part=0;part<Damage::Carver::kParts;++part) {
            const auto& v=carver->PartOf(cell,part).vertices;
            for(size_t i=0;i+2<v.size();i+=3)carved+=Area(v[i].position,v[i+1].position,v[i+2].position);
            for(const auto& vertex:v) {
                ++vertices;float d;int nearest=Nearest(seeds,count,vertex.position,&d);
                Vec3 own=vertex.position-seeds[seed].position;
                bool inside=nearest==int(seed)||Dot(own,own)-d*d<2e-3f;
                //the reach sphere is clipped as a tangent polyhedron whose overshoot stays far below a pixel
                outside+=!inside||Length(own)>seeds[seed].reach+.04f;
                Check(Finite(vertex.position)&&fabsf(Length(vertex.normal)-1)<.002f,"carved vertices and normals stay finite and unit");
            }
        }
    }
    //sample every donor triangle densely and integrate the broken area the shader predicate removes
    double expected=0;
    for(size_t n=0;n<model.meshes.size();++n) {
        if(surface.SetOf(unsigned(n))!=set)continue;
        const auto& mesh=model.meshes[n];
        for(size_t i=0;i+2<mesh.indices.size();i+=3) {
            Vec3 a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
            constexpr unsigned k=10;unsigned hit=0,total=0;
            for(unsigned x=0;x<k;++x)for(unsigned y=0;x+y<k;++y) {
                float u=(x+1/3.0f)/k,v=(y+1/3.0f)/k;
                hit+=Broken(seeds,count,a+(b-a)*u+(c-a)*v);++total;
            }
            if(hit)expected+=Area(a,b,c)*hit/total;
        }
    }
    printf("%s carving: cells=%u vertices=%u carved=%.4f sampled=%.4f outsideCell=%u\n",label,cells,vertices,carved,expected,outside);

    Check(!outside,"every carved vertex lies in its own nearest-seed cell within reach");
    Check(expected<.0001?carved<.0001:fabs(carved-expected)<expected*.03+.002,"carved cells cover exactly the area the per-pixel predicate removes");
}
}
void TestFracture(const XRay::Model& model,const XRay::HeadFrame& head) {
    using namespace XRay;
    Model shell=Shell();
    auto frameHead=std::make_unique<HeadFrame>();frameHead->head=Transform::Identity();
    Impact::Path path{{0,-4,0},{0,4,0},true};
    auto scene=std::make_unique<Damage::Scene>();scene->Reserve(shell);
    scene->Update(shell,*frameHead,{},FrameFor(path,0,1));
    auto report=scene->Report();
    Check(report.boneRuns==2&&report.brainRuns==1&&report.entry==1&&report.exit==1&&!report.inner,"hollow skull wall yields entry and exit breaks around one brain crossing");
    Check(!scene->ActiveSeeds(Damage::BoneSet)&&!scene->ActiveSeeds(Damage::BrainSet),"nothing breaks before the bullet reaches the first bone");
    float entryTime=scene->Clusters()[0].time,exitTime=scene->Clusters()[1].time;
    Check(entryTime>0&&exitTime>entryTime,"breaks are timed along the bullet");
    scene->Update(shell,*frameHead,{},FrameFor(path,(entryTime+exitTime)*.5f,1));
    unsigned midBone=scene->ActiveSeeds(Damage::BoneSet);
    Check(midBone>0&&midBone<scene->SeedCount(Damage::BoneSet),"entry breaks before the exit");
    scene->Update(shell,*frameHead,{},FrameFor(path,1.5f,1));
    Check(scene->ActiveSeeds(Damage::BoneSet)==scene->SeedCount(Damage::BoneSet)&&scene->ActiveSeeds(Damage::BrainSet)==scene->SeedCount(Damage::BrainSet),
        "settled wound keeps every break active");
    unsigned status=scene->Status();
    Check((status&Graphics::EntryBreak)&&(status&Graphics::ExitBreak)&&(status&Graphics::BrainTorn)&&(status&Graphics::HingedPlates)&&(status&Graphics::BoneFragments),
        "settled shell shows entry and exit breaks, plates, fragments and torn brain");
    const auto* bone=scene->Seeds(Damage::BoneSet);unsigned boneCount=scene->SeedCount(Damage::BoneSet);
    CheckCarving(shell,bone,boneCount,Damage::BoneSet,"shell bone");
    CheckCarving(shell,scene->Seeds(Damage::BrainSet),scene->SeedCount(Damage::BrainSet),Damage::BrainSet,"shell brain");
    Check(Broken(bone,boneCount,{0,-3.3f,0})&&Broken(bone,boneCount,{0,3.3f,0}),"bone directly on the shot line is removed at both walls");
    Check(!Broken(bone,boneCount,{3.3f,0,0})&&!Broken(bone,boneCount,{0,-3.3f,3.4f})&&!Broken(bone,boneCount,{-3.3f,0,0}),"distant skull wall stays intact");
    Check(Broken(scene->Seeds(Damage::BrainSet),scene->SeedCount(Damage::BrainSet),{0,-2.5f,0})&&Broken(scene->Seeds(Damage::BrainSet),scene->SeedCount(Damage::BrainSet),{0,2.5f,0}),
        "brain surface is torn where the bullet enters and leaves it");
    Check(!Broken(scene->Seeds(Damage::BrainSet),scene->SeedCount(Damage::BrainSet),{2.5f,0,2.4f}),"brain away from the channel stays intact");
    //plates turn rigidly about their hinge and swing toward the bullet's travel
    unsigned plates=scene->Plates();Check(plates>0,"retained bone breaks into hinged plates");
    for(unsigned k=0;k<plates;++k) {
        const float* r=scene->PlateRows()+k*12;
        Vec3 x={r[0],r[4],r[8]},y={r[1],r[5],r[9]},z={r[2],r[6],r[10]};
        Check(fabsf(Length(x)-1)<1e-3f&&fabsf(Length(y)-1)<1e-3f&&fabsf(Dot(x,y))<1e-3f&&fabsf(Dot(Cross(x,y),z)-1)<1e-3f,"plate motion is a proper rotation");
    }
    double moved=0;unsigned along=0;
    for(unsigned i=0;i<boneCount;++i) {
        if(bone[i].state!=Damage::Plate)continue;
        //a point on the plate's inner side, relative to its hinge line
        Vec3 axis=bone[i].axis,inner=bone[i].position+(bone[i].position-bone[i].hinge)*.2f-bone[i].hinge;float angle=bone[i].angle;
        Vec3 turned=inner*cosf(angle)+Cross(axis,inner)*sinf(angle)+axis*(Dot(axis,inner)*(1-cosf(angle)));
        float shift=Dot(turned-inner,Vec3{0,1,0});moved+=shift;along+=shift>0;
    }
    const auto& debris=scene->Fragments();
    bool live=false;for(unsigned b=0;b<Damage::kBursts;++b)live|=debris.Live(b,.2f);
    Check(live,"fragments fly after the shot");
    bool expired=true;for(unsigned b=0;b<Damage::kBursts;++b)expired&=!debris.Live(b,3);
    Check(expired,"fragments expire while the settled wound remains");
    float rows[Damage::kBurstPieces*12];unsigned pieces=debris.Transforms(0,3,{},rows);
    bool zero=true;for(unsigned i=0;i<pieces*12;++i)zero&=rows[i]==0;
    Check(pieces>0&&zero,"expired pieces collapse to zero transforms");
    pieces=debris.Transforms(0,entryTime+.001f,{},rows);
    unsigned atRest=0;
    for(unsigned i=0;i<pieces;++i) {
        const float* r=rows+i*12;
        atRest+=fabsf(r[0]-1)<1e-4f&&fabsf(r[5]-1)<1e-4f&&fabsf(r[10]-1)<1e-4f&&fabsf(r[3])<1e-4f&&fabsf(r[7])<1e-4f&&fabsf(r[11])<1e-4f;
    }
    Check(atRest>0,"released pieces start exactly where their anatomy was");
    auto twin=std::make_unique<Damage::Scene>();twin->Reserve(shell);twin->Update(shell,*frameHead,{},FrameFor(path,1.5f,1));
    bool same=twin->SeedCount(Damage::BoneSet)==boneCount;
    for(unsigned i=0;same&&i<boneCount;++i) {
        const auto& a=twin->Seeds(Damage::BoneSet)[i];const auto& b=bone[i];
        same=Length(a.position-b.position)==0&&a.reach==b.reach&&a.state==b.state&&a.cluster==b.cluster&&a.time==b.time;
    }
    Check(same,"same shot fractures identically");

    //burst accumulation on one head: earlier holes persist and stay holes
    scene->Clear();
    scene->Update(shell,*frameHead,{},FrameFor(path,.5f,10,100.5f));
    std::vector<Vec3> holes;
    for(unsigned i=0;i<scene->SeedCount(Damage::BoneSet);++i)if(scene->Seeds(Damage::BoneSet)[i].state==Damage::Loose)holes.push_back(scene->Seeds(Damage::BoneSet)[i].position);
    Impact::Path second{{1.1f,-4,.4f},{.6f,4,1.2f},true};
    scene->Update(shell,*frameHead,{},FrameFor(second,.3f,11,100.8f));
    Check(scene->Report().wounds==2,"second hit adds a wound instead of replacing the first");
    unsigned kept=0;for(auto p:holes)kept+=Broken(scene->Seeds(Damage::BoneSet),scene->SeedCount(Damage::BoneSet),p);
    Check(kept==holes.size(),"earlier entry and exit holes survive the next hit");
    CheckCarving(shell,scene->Seeds(Damage::BoneSet),scene->SeedCount(Damage::BoneSet),Damage::BoneSet,"overlapping wounds");
    for(unsigned shot=0;shot<4;++shot) {
        Impact::Path p{{-1.0f+shot*.7f,-4,-.8f},{-1.2f+shot*.6f,4,.9f},true};
        scene->Update(shell,*frameHead,{},FrameFor(p,.2f,20+shot,101.0f+shot*.1f));
        Check(scene->Report().wounds<=Damage::kWounds&&scene->SeedCount(Damage::BoneSet)<=Damage::kSceneBoneSeeds,"rapid fire stays inside wound and seed capacity");
    }
    Check(scene->Report().wounds==Damage::kWounds,"six hits in a row all stay on one head");
    //kept and brought back on the same head: the same breaks, settled, with nothing flying, spraying or sounding again
    std::vector<Damage::Kept> keep;scene->Keep(keep);
    Check(keep.size()==Damage::kWounds,"every planned wound is kept");
    auto back=std::make_unique<Damage::Scene>();back->Reserve(shell);
    uint32_t revision=back->WoundRevision();back->Restore(keep);
    Check(back->WoundRevision()!=revision,"bringing wounds back counts as a change to keep");
    auto later=FrameFor(path,.1f,70,300.0f);later.path.valid=false;
    back->Advance(*frameHead,{},later);
    bool sameSeeds=back->SeedCount(Damage::BoneSet)==scene->SeedCount(Damage::BoneSet)&&back->SeedCount(Damage::BrainSet)==scene->SeedCount(Damage::BrainSet);
    for(unsigned i=0;sameSeeds&&i<back->SeedCount(Damage::BoneSet);++i)sameSeeds=Length(back->Seeds(Damage::BoneSet)[i].position-scene->Seeds(Damage::BoneSet)[i].position)==0;
    Check(sameSeeds,"the kept wounds plan the same breaks when they come back");
    Check(back->ActiveSeeds(Damage::BoneSet)==back->SeedCount(Damage::BoneSet)&&back->ActiveClusters()==back->ClusterCount(),"every break is already settled");
    bool flying=false;for(unsigned b=0;b<Damage::kBursts;++b)flying|=back->Fragments().Live(b,300.05f);
    uint8_t heard[16];
    Check(!flying&&!back->Blood().Any(300.05f)&&!back->TakeCues(heard,16),"nothing flies, sprays or sounds again");
    std::vector<Damage::Kept> again;back->Keep(again);
    Check(again.size()==keep.size()&&again.back().sequence==keep.back().sequence,"brought-back wounds keep their shots");
    auto unmapped=FrameFor(path,.1f,40,101.6f);unmapped.path.valid=false;
    scene->Advance(*frameHead,{},unmapped);
    Check(scene->Wounded()&&scene->ActiveSeeds(Damage::BoneSet)>0,"an unmapped follow-up keeps earlier wounds on the same head");
    unmapped.sceneTime=-1;scene->Advance(*frameHead,{},unmapped);
    Check(!scene->Wounded(),"without a shared scene clock an unmapped shot cannot time earlier wounds");
    scene->Update(shell,*frameHead,{},FrameFor(path,.05f,50,200.05f));
    scene->ClearWounds();
    live=false;for(unsigned b=0;b<Damage::kBursts;++b)live|=scene->Fragments().Live(b,200.1f);
    Check(!scene->Wounded()&&live,"switching target clears wounds but keeps airborne fragments");
    scene->Clear();live=false;for(unsigned b=0;b<Damage::kBursts;++b)live|=scene->Fragments().Live(b,200.1f);
    Check(!live,"explicit clear removes fragments too");
    Impact::Path miss{{10,-4,0},{10,4,0},true};
    scene->Update(shell,*frameHead,{},FrameFor(miss,1,60));
    Check(!scene->Status()&&!scene->SeedCount(Damage::BoneSet)&&!scene->SeedCount(Damage::BrainSet),"a path that misses all anatomy invents no damage");

    //the game carves on a worker: the render thread only plans, then adopts the same result later
    {
        auto sync=std::make_unique<Damage::Scene>();sync->Reserve(model);
        auto async=std::make_unique<Damage::Scene>();async->SetAsync(true);async->Reserve(model);
        const auto& route=RecordedShots::session[13];
        sync->Update(model,head,{},FrameFor(route.path,.01f,route.sequence,50.01f));
        LARGE_INTEGER frequency,before,after;QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&before);async->Update(model,head,{},FrameFor(route.path,.01f,route.sequence,50.01f));QueryPerformanceCounter(&after);
        double submit=double(after.QuadPart-before.QuadPart)*1000/frequency.QuadPart;
        Check(async->Pending()&&!async->SeedCount(Damage::BoneSet)&&!async->ActiveSeeds(Damage::BoneSet),"asynchronous hit breaks nothing until its carving is adopted");
        unsigned frames=0;float age=.01f;
        while(async->Pending()&&frames<400){Sleep(5);age+=.005f;async->Update(model,head,{},FrameFor(route.path,age,route.sequence,50+age));++frames;}
        Check(!async->Pending(),"worker finishes and the render thread adopts its result");
        bool matched=async->SeedCount(Damage::BoneSet)==sync->SeedCount(Damage::BoneSet)&&async->Plates()==sync->Plates()
            &&async->PlatePart(0).count==sync->PlatePart(0).count&&async->Report().fragments==sync->Report().fragments;
        for(unsigned i=0;matched&&i<sync->SeedCount(Damage::BoneSet);++i)
            matched=Length(async->Seeds(Damage::BoneSet)[i].position-sync->Seeds(Damage::BoneSet)[i].position)==0&&async->Seeds(Damage::BoneSet)[i].state==sync->Seeds(Damage::BoneSet)[i].state;
        Check(matched,"worker carving matches the synchronous carving");
        printf("asynchronous carve: render-thread submit %.3f ms, adopted after %u frames\n",submit,frames);
        async->Update(model,head,{},FrameFor(RecordedShots::session[14].path,.01f,900,60.01f));
        async->ClearWounds();
        for(unsigned i=0;i<400&&async->Pending();++i){Sleep(5);async->Advance(head,{},FrameFor(route.path,.1f,901,60.1f));}
        Check(!async->Wounded()&&!async->SeedCount(Damage::BoneSet),"a job finishing after a target switch is discarded");
        async->Update(model,head,{},FrameFor(RecordedShots::session[15].path,.01f,902,70.01f));
        async->Reserve(model);
        Check(!async->Pending()&&!async->Wounded(),"reserve waits for the worker before rebuilding shared anatomy");
    }
    //real anatomy on the logged session routes
    scene->Reserve(model);
    LARGE_INTEGER frequency,before,after;QueryPerformanceFrequency(&frequency);
    double worst=0;
    for(const auto& shot:RecordedShots::session) {
        scene->Clear();
        QueryPerformanceCounter(&before);scene->Update(model,head,{},FrameFor(shot.path,1.5f,shot.sequence));QueryPerformanceCounter(&after);
        double ms=double(after.QuadPart-before.QuadPart)*1000/frequency.QuadPart;worst=ms>worst?ms:worst;
        Vec3 d=shot.path.exit-shot.path.entry;float length=Length(d);Normalize(d);
        bool hitsBone=Crosses(model,kBoneMaterial,shot.path.entry,d,length),brain=Crosses(model,kBrainMaterial,shot.path.entry,d,length);
        auto r=scene->Report();
        printf("session route %u: bone=%u brain=%u runs=%u/%u breaks=%u/%u/%u plates=%u fragments=%u brainPieces=%u vertices=%u setup=%.2f ms\n",
            shot.sequence,hitsBone,brain,r.boneRuns,r.brainRuns,r.entry,r.inner,r.exit,r.plates,r.fragments,r.brainPieces,r.vertices,ms);
        Check(bool(r.boneRuns)==hitsBone&&bool(r.brainRuns)==brain,"planner finds exactly the anatomy the straight path crosses");
        if(hitsBone)Check(r.entry+r.inner+r.exit>0&&r.fragments>0,"every bone crossing breaks and releases fragments");
        if(brain)Check(r.brainPieces>0,"every brain crossing tears out tissue");
        if(hitsBone)CheckCarving(model,scene->Seeds(Damage::BoneSet),scene->SeedCount(Damage::BoneSet),Damage::BoneSet,"real bone");
        if(brain)CheckCarving(model,scene->Seeds(Damage::BrainSet),scene->SeedCount(Damage::BrainSet),Damage::BrainSet,"real brain");
    }
    printf("worst real-anatomy setup: %.2f ms\n",worst);
    for(const auto& shot:RecordedShots::shots){scene->Clear();scene->Update(model,head,{},FrameFor(shot.path,.3f,shot.sequence));}
    for(const auto& shot:RecordedShots::recent){scene->Clear();scene->Update(model,head,{},FrameFor(shot.path,.3f,shot.sequence));}
    QueryPerformanceCounter(&before);
    for(unsigned i=0;i<1000;++i)scene->Update(model,head,{},FrameFor(RecordedShots::recent[0].path,.3f+i*.0001f,RecordedShots::recent[0].sequence));
    QueryPerformanceCounter(&after);
    printf("steady damage update: %.4f ms/frame\n",double(after.QuadPart-before.QuadPart)/frequency.QuadPart);
    printf("PASS: %u fracture planning, carving, timing, accumulation and lifetime checks\n",checks);
}
namespace {
std::vector<DWORD> Pixels(IDirect3DDevice9* d,IDirect3DSurface9* source,unsigned size) {
    IDirect3DSurface9* copy=nullptr;Hr(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,nullptr));
    Hr(d->GetRenderTargetData(source,copy));D3DLOCKED_RECT r;Hr(copy->LockRect(&r,nullptr,D3DLOCK_READONLY));
    std::vector<DWORD> pixels(size*size);
    for(unsigned y=0;y<size;++y)memcpy(pixels.data()+y*size,static_cast<char*>(r.pBits)+y*r.Pitch,size*4);
    Hr(copy->UnlockRect());copy->Release();return pixels;
}
float Luma(DWORD p){return .3f*((p>>16)&255)+.59f*((p>>8)&255)+.11f*(p&255);}
//camera on the shot line looking down +y at the synthetic skull, with a skin quad covering the frame
struct Bench {
    IDirect3DDevice9* d;IDirect3DSurface9 *rt=nullptr,*ds=nullptr;CameraFrame c{};std::unique_ptr<HeadFrame> head;static constexpr unsigned size=256;
    explicit Bench(IDirect3DDevice9* device,Vec3 eye,Vec3 target):d(device),head(std::make_unique<HeadFrame>()) {
        Hr(d->CreateRenderTarget(size,size,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt,nullptr));
        Hr(d->CreateDepthStencilSurface(size,size,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&ds,nullptr));
        Vec3 forward=target-eye;Normalize(forward);Vec3 right=Cross({1,0,0},forward);Normalize(right);Vec3 up=Cross(forward,right);
        const float view[16]={right.x,up.x,forward.x,0,right.y,up.y,forward.y,0,right.z,up.z,forward.z,0,-Dot(eye,right),-Dot(eye,up),-Dot(eye,forward),1};
        memcpy(c.view,view,sizeof view);
        const float projection[16]={2.2f,0,0,0,0,2.2f,0,0,0,0,1000.0f/999,1,0,0,-1000.0f/999,0};memcpy(c.projection,projection,sizeof projection);
        c.viewport={0,0,size,size,0,1};
        head->head=Transform::Identity();head->fit.chin=-50;
        //skin quad in front of the skull, its local coordinate on the entry so the opening reveal covers it at once
        Vec3 centre=eye+forward*4.0f;
        Vec3 corners[4]={centre-right*20-up*20,centre+right*20-up*20,centre+right*20+up*20,centre-right*20+up*20};
        for(unsigned i=0;i<4;++i)head->vertices[i]={corners[i],{0,-4,0}};
        const uint16_t quad[]={0,1,2,0,2,3};for(unsigned i=0;i<kHeadTriangles*3;++i)head->indices[i]=quad[i%6];
    }
    ~Bench(){Graphics::Release();rt->Release();ds->Release();}
    std::vector<DWORD> Render(const Model& model,const Impact::Frame* frame) {
        Hr(d->SetDepthStencilSurface(nullptr));Hr(d->SetRenderTarget(0,rt));Hr(d->SetDepthStencilSurface(ds));
        Hr(d->SetViewport(&c.viewport));Hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xFF102030,1,0));
        Hr(d->BeginScene());Check(Graphics::Draw(d,model,c,*head,1,{},frame),"fracture bench render");Hr(d->EndScene());
        return Pixels(d,rt,size);
    }
    bool Project(Vec3 p,int& x,int& y) {
        Vec3 screen;if(!Audit::Project(c,p,screen))return false;
        x=int(screen.x);y=int(screen.y);return x>=0&&y>=0&&x<int(size)&&y<int(size);
    }
};
}
bool BoneColour(DWORD p){float r=float((p>>16)&255),b=float(p&255);return Luma(p)>80&&b>r*.6f;}
void TestFractureGPU(IDirect3DDevice9* d,const XRay::Model& model,const XRay::HeadFrame& head) {
    using namespace XRay;
    (void)head;(void)model;
    Model shell=Shell();
    Impact::Path path{{0,-4,0},{0,4,0},true};
    auto frameHead=std::make_unique<HeadFrame>();frameHead->head=Transform::Identity();
    auto scene=std::make_unique<Damage::Scene>();scene->Reserve(shell);
    auto settledFrame=FrameFor(path,1.5f,1);
    scene->Update(shell,*frameHead,{},settledFrame);
    float entry=scene->Clusters()[0].time;
    auto beforeFrame=FrameFor(path,entry-.0005f,1),restFrame=FrameFor(path,entry+.0004f,1);
    {
        Bench bench(d,{.4f,-30,.3f},{0,0,0});
        auto settled=bench.Render(shell,&settledFrame);
        Graphics::Release();auto intact=bench.Render(shell,nullptr);
        int cx,cy;Check(bench.Project({0,-3.5f,0},cx,cy),"entry projects into the bench");
        Check(BoneColour(intact[cy*Bench::size+cx])&&!BoneColour(settled[cy*Bench::size+cx]),"settled entry opens where intact bone was");
        const auto* seeds=scene->Seeds(Damage::BoneSet);unsigned count=scene->SeedCount(Damage::BoneSet);
        unsigned hole=0,holeOpen=0,kept=0,keptBone=0;
        for(float x=-3.3f;x<=3.3f;x+=.15f)for(float z=-3.3f;z<=3.3f;z+=.15f) {
            Vec3 p{x,-3.5f,z};int px,py;if(!bench.Project(p,px,py))continue;
            float distance;int nearest=Nearest(seeds,count,p,&distance);
            //margin from every cell border keeps ragged edges, stains and cracks out of the sample
            float margin=1e30f;
            for(unsigned j=0;j<count;++j)if(int(j)!=nearest){Vec3 a=seeds[nearest].position,b=seeds[j].position;
                margin=fminf(margin,(Dot(p-b,p-b)-distance*distance)/(2*Length(b-a)));}
            if(margin<.35f||!BoneColour(intact[py*Bench::size+px]))continue;
            DWORD now=settled[py*Bench::size+px];
            if(seeds[nearest].state==Damage::Loose&&distance<seeds[nearest].reach){++hole;holeOpen+=!BoneColour(now);}
            else if(seeds[nearest].state==Damage::Anchor&&Length(p-Vec3{0,-3.5f,0})>3.0f){++kept;keptBone+=BoneColour(now);}
        }
        printf("GPU classification: loose cells %u/%u open, retained %u/%u bone\n",holeOpen,hole,keptBone,kept);
        Check(hole>20&&holeOpen*10>=hole*9,"bone the CPU predicate releases is gone on the GPU once fragments leave");
        Check(kept>20&&keptBone*10>=kept*9,"bone the CPU predicate keeps remains intact on the GPU");
        //at the instant of fracture the plates and loose pieces cover exactly the bone they replace
        Graphics::Release();auto before=bench.Render(shell,&beforeFrame);
        Graphics::Release();auto rest=bench.Render(shell,&restFrame);
        std::vector<uint8_t> dark(Bench::size*Bench::size);
        for(unsigned i=0;i<dark.size();++i)dark[i]=Luma(rest[i])<Luma(before[i])*.5f;
        unsigned lines=0,areas=0;
        for(unsigned y=1;y+1<Bench::size;++y)for(unsigned x=1;x+1<Bench::size;++x) {
            if(abs(int(x)-cx)<14&&abs(int(y)-cy)<14)continue;
            if(!dark[y*Bench::size+x])continue;
            ++lines;bool solid=true;
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)solid&=dark[(y+dy)*Bench::size+x+dx]!=0;
            areas+=solid;
        }
        printf("fracture instant: %u pixels darken along new cracks, %u inside open areas\n",lines,areas);
        Check(lines>50,"fracture lines appear the instant the bullet arrives");
        Check(areas<12,"no hole opens before the pieces actually move");
    }
    {
        //bone only: anything orange-red in the hole is exposed wall thickness, not brain
        Model bone;bone.meshes.push_back(shell.meshes[0]);
        Bench bench(d,{9,-26,6},{0,-3.5f,0});
        auto settled=bench.Render(bone,&settledFrame);
        Graphics::Release();auto intact=bench.Render(bone,nullptr);
        auto cut=[](const std::vector<DWORD>& pixels) {
            unsigned count=0;
            for(auto p:pixels){int r=(p>>16)&255,g=(p>>8)&255,b=p&255;count+=r>60&&g>r*.4f&&r>g*1.35f&&g>b*1.05f;}
            return count;
        };
        printf("oblique settled hole: %u cut-surface pixels (intact %u)\n",cut(settled),cut(intact));
        Check(cut(settled)>40&&cut(intact)<5,"broken walls show cut bone thickness from an angle");
    }
    printf("PASS: fracture GPU classification, seamless release and exposed thickness\n");
}
