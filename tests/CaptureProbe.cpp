#include <algorithm>
#include "../src/ShotAudit.h"
#include "../src/Damage.h"
#include "../src/BodySkin.h"
#include "ProbeRender.h"
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace XRay {
void Log(const char* format,...) {
    va_list args;va_start(args,format);vprintf(format,args);va_end(args);putchar('\n');
}
}
namespace {
using namespace XRay;
void Check(bool ok,const char* text){if(!ok){printf("FAIL: %s\n",text);exit(1);}}
void Hr(HRESULT hr){Check(SUCCEEDED(hr),"capture replay D3D call");}
CameraFrame Camera() {
    CameraFrame c{};c.viewport={0,0,640,480,0,1};
    const float view[]={0,1,0,0,0,0,-1,0,1,0,0,0,0,-7,50,1};
    memcpy(c.view,view,sizeof view);c.projection[0]=.75f;c.projection[5]=1;
    c.projection[10]=1000.0f/999;c.projection[11]=1;c.projection[14]=-1000.0f/999;return c;
}
void Write(const Audit::Record& r,const char* path,size_t size=sizeof(Audit::Record)) {
    FILE* file=nullptr;fopen_s(&file,path,"wb");Check(file!=nullptr,"capture fixture output");
    Check(fwrite(&r,1,size,file)==size&&fclose(file)==0,"capture fixture written");
}
}
namespace XRay::Probe {
using namespace XRay;
//the game draws the face and body before the x-ray, so only skin the camera sees opens the reveal
void DrawScene(IDirect3DDevice9* d,const CameraFrame& c,const std::vector<Vec3>& triangles) {
    struct Point {Vec3 p;DWORD color;};std::vector<Point> points;points.reserve(triangles.size());
    for(size_t i=0;i+2<triangles.size();i+=3) {
        Vec3 n=Cross(triangles[i+1]-triangles[i],triangles[i+2]-triangles[i]);if(!Normalize(n))continue;
        DWORD shade=DWORD(70+110*fabsf(Dot(n,{.4f,-.5f,.77f})));DWORD color=0xFF000000|(shade<<16)|((shade*3/4)<<8)|(shade*3/5);
        for(unsigned k=0;k<3;++k)points.push_back({triangles[i+k]-c.origin,color});
    }
    D3DMATRIX identity{},view,projection;identity._11=identity._22=identity._33=identity._44=1;
    memcpy(&view,c.view,sizeof view);memcpy(&projection,c.projection,sizeof projection);
    Hr(d->SetTransform(D3DTS_WORLD,&identity));Hr(d->SetTransform(D3DTS_VIEW,&view));Hr(d->SetTransform(D3DTS_PROJECTION,&projection));
    Hr(d->SetVertexShader(nullptr));Hr(d->SetPixelShader(nullptr));Hr(d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE));
    Hr(d->SetRenderState(D3DRS_LIGHTING,FALSE));Hr(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    Hr(d->SetRenderState(D3DRS_ZENABLE,TRUE));Hr(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
    Hr(d->SetRenderState(D3DRS_ZFUNC,c.Reversed()?D3DCMP_GREATEREQUAL:D3DCMP_LESSEQUAL));
    Hr(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));Hr(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
    if(!points.empty())Hr(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(points.size()/3),points.data(),sizeof(Point)));
}
std::vector<DWORD> RenderFrames(IDirect3DDevice9* d,const Model& model,Audit::Record& r,const float* ages,unsigned count,const std::vector<Vec3>* scene) {
    Graphics::Release();IDirect3DSurface9 *rt=nullptr,*depth=nullptr,*copy=nullptr;
    unsigned width=r.camera.viewport.X+r.camera.viewport.Width,height=r.camera.viewport.Y+r.camera.viewport.Height;
    Hr(d->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt,nullptr));
    Hr(d->CreateDepthStencilSurface(width,height,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    Hr(d->CreateOffscreenPlainSurface(width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,nullptr));
    float start=r.impact.sceneTime-r.impact.age;
    for(unsigned n=0;n<count;++n) {
        r.impact.age=ages[n];r.impact.sceneTime=start+ages[n];
        Hr(d->SetDepthStencilSurface(nullptr));Hr(d->SetRenderTarget(0,rt));Hr(d->SetDepthStencilSurface(depth));
        Hr(d->SetViewport(&r.camera.viewport));Hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xFF302C27,r.camera.Reversed()?0.0f:1.0f,0));
        Hr(d->BeginScene());if(scene)DrawScene(d,r.camera,*scene);
        bool drawn=Graphics::Draw(d,model,r.camera,r.head,2,r.appearance,&r.impact);if(!drawn)printf("draw HRESULT %08X\n",unsigned(Graphics::LastError()));
        Check(drawn,"consecutive frame rendered");Hr(d->EndScene());
        //reading the frame back waits for the GPU, as the game's present does
        Hr(d->GetRenderTargetData(rt,copy));
    }
    D3DLOCKED_RECT lock;Hr(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<DWORD> pixels(width*height);
    for(unsigned y=0;y<height;++y)memcpy(pixels.data()+y*width,static_cast<char*>(lock.pBits)+y*lock.Pitch,width*4);
    Hr(copy->UnlockRect());copy->Release();Graphics::Release();rt->Release();depth->Release();return pixels;
}
std::vector<DWORD> Render(IDirect3DDevice9* d,const Model& model,const Audit::Record& r,Graphics::DamageReport* report,const std::vector<Vec3>* scene) {
    Graphics::Release();IDirect3DSurface9 *rt=nullptr,*depth=nullptr,*copy=nullptr;
    unsigned width=r.camera.viewport.X+r.camera.viewport.Width,height=r.camera.viewport.Y+r.camera.viewport.Height;
    Hr(d->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt,nullptr));
    Hr(d->CreateDepthStencilSurface(width,height,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    Hr(d->SetDepthStencilSurface(nullptr));Hr(d->SetRenderTarget(0,rt));Hr(d->SetDepthStencilSurface(depth));
    Hr(d->SetViewport(&r.camera.viewport));Hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xFF302C27,r.camera.Reversed()?0.0f:1.0f,0));
    Hr(d->BeginScene());if(scene)DrawScene(d,r.camera,*scene);
    bool drawn=Graphics::Draw(d,model,r.camera,r.head,2,r.appearance,&r.impact);if(!drawn)printf("draw HRESULT %08X\n",unsigned(Graphics::LastError()));
    Check(drawn,"captured geometry rendered");Hr(d->EndScene());
    if(report)*report=Graphics::DamageSummary();
    Hr(d->CreateOffscreenPlainSurface(width,height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,nullptr));
    Hr(d->GetRenderTargetData(rt,copy));D3DLOCKED_RECT lock;Hr(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<DWORD> pixels(width*height);
    for(unsigned y=0;y<height;++y)memcpy(pixels.data()+y*width,static_cast<char*>(lock.pBits)+y*lock.Pitch,width*4);
    Hr(copy->UnlockRect());copy->Release();Graphics::Release();rt->Release();depth->Release();return pixels;
}
}
namespace {
using XRay::Probe::Render;
}
void TestAudit(const XRay::HeadFrame& head) {
    using namespace XRay;
    auto camera=Camera();Vec3 origin,direction,pixel;
    Check(Audit::CentreRay(camera,origin,direction)&&Length(origin-Vec3{7,50,0})<.0001f&&Length(direction-Vec3{0,-1,0})<.0001f,
        "camera centre ray in independently specified front-facing frame");
    Check(Audit::Project(camera,{7,0,0},pixel)&&fabsf(pixel.x-320)<.001f&&fabsf(pixel.y-240)<.001f,"head centre maps to viewport centre");
    Check(Audit::Project(camera,{10,0,2},pixel)&&fabsf(pixel.x-329.6f)<.001f&&fabsf(pixel.y-225.6f)<.001f,"independent perspective screen coordinates");
    Check(!Audit::Project(camera,{7,60,0},pixel),"points behind camera rejected");
    camera.origin={25000,-32000,7300};camera.viewport={23,19,640,480,0,1};camera.projection[8]=.13f;camera.projection[9]=-.21f;
    Check(Audit::CentreRay(camera,origin,direction)&&Audit::Project(camera,origin+direction*40,pixel)
        &&fabsf(pixel.x-343)<.02f&&fabsf(pixel.y-259)<.02f,"world origin and asymmetric projection preserve centre ray");
    camera.projection[10]=-1.0f/999;camera.projection[14]=1000.0f/999;
    Check(Audit::CentreRay(camera,origin,direction)&&Audit::Project(camera,origin+direction*40,pixel)&&fabsf(pixel.x-343)<.02f,"reversed depth preserves projected aim");
    camera.projection[15]=1;Check(!Audit::CentreRay(camera,origin,direction),"unsupported camera form has no invented aim ray");
    Audit::Record r;r.size=sizeof r;r.head=head;r.camera=Camera();r.target=0x123;
    r.head.influences[1].t={.4f,-.2f,0};r.head.influences[1].r[1]=.05f;r.head.influences[5].t={1,2,3};r.head.neckError=.015f;
    r.head.vertices[5].reveal=.3f;r.head.skinVertices=3;r.head.skinTriangles=1;r.head.skinShapes=1;r.head.skinSkipped=2;
    for(unsigned i=0;i<3;++i){r.head.skin[i]={{float(i),-3,1},{float(i)-4,-3,1},.25f*float(i)};r.head.skinIndices[i]=uint16_t(2-i);}
    r.source.valid=true;r.source.point={10,5,1};r.source.direction={0,-1,0};
    Check(Impact::Map(r.source,head,r.impact.path),"audit positive control maps");
    r.impact.age=.02f;r.impact.sceneTime=100.02f;r.impact.sequence=7;
    Audit::Init("tests\\output\\");Audit::Select(10);
    Audit::Submit(10,r.target,r.camera,r.head,r.appearance,r.source,r.impact,63);Audit::Drain();
    Audit::Record copy;
    Check(Audit::Read("tests/output/XRayKillcamCaptures/shot-07-0.xrc",copy)&&copy.target==r.target&&copy.support==63
        &&!memcmp(&copy.head,&r.head,sizeof r.head)&&!memcmp(&copy.source,&r.source,sizeof r.source),"live mailbox preserves exact skin, pose and impact values");
    Write(copy,"tests/output/audit-replay.xrc");
    copy.target=0x999;Audit::Submit(10,copy.target,r.camera,r.head,r.appearance,r.source,r.impact,0);Audit::Drain();
    Check(Audit::Read("tests/output/XRayKillcamCaptures/shot-07-0.xrc",copy)&&copy.target==r.target,"same stage is not written every frame");
    Audit::Select(11);Audit::Submit(10,0x999,r.camera,r.head,r.appearance,r.source,r.impact,0);Audit::Drain();
    Check(Audit::Read("tests/output/XRayKillcamCaptures/shot-07-0.xrc",copy)&&copy.target==r.target,"stale generation cannot publish after clear");
    r.impact.age=.15f;r.impact.sceneTime=100.15f;
    Audit::Submit(11,r.target,r.camera,r.head,r.appearance,r.source,r.impact,63);Audit::Drain();
    Check(Audit::Read("tests/output/XRayKillcamCaptures/shot-07-1.xrc",copy)&&copy.stage==1,"post-impact frame captured separately");
    const char* invalid="tests/output/audit-invalid.xrc";
    Write(copy,invalid,sizeof copy-1);Check(!Audit::Read(invalid,r),"truncated capture rejected");
    ++copy.version;Write(copy,invalid);Check(!Audit::Read(invalid,r),"foreign capture version rejected");--copy.version;
    copy.head.indices[0]=kHeadVertices;Write(copy,invalid);Check(!Audit::Read(invalid,r),"invalid captured skin index rejected");copy.head.indices[0]=head.indices[0];
    copy.camera.view[0]=std::numeric_limits<float>::quiet_NaN();Write(copy,invalid);Check(!Audit::Read(invalid,r),"nonfinite captured camera rejected");
    Check(Audit::Read("tests/output/XRayKillcamCaptures/shot-07-1.xrc",copy),"reread capture");
    copy.head.skinIndices[1]=3;Write(copy,invalid);Check(!Audit::Read(invalid,r),"neck skin index outside its vertices rejected");copy.head.skinIndices[1]=1;
    copy.head.skinTriangles=kSkinTriangles+1;Write(copy,invalid);Check(!Audit::Read(invalid,r),"neck skin over capacity rejected");copy.head.skinTriangles=1;
    copy.head.skin[2].reveal=1.5f;Write(copy,invalid);Check(!Audit::Read(invalid,r),"neck skin weight outside 0..1 rejected");copy.head.skin[2].reveal=.5f;
    copy.head.vertices[9].reveal=-1;Write(copy,invalid);Check(!Audit::Read(invalid,r),"face weight outside 0..1 rejected");
    Audit::Record legacy;const Transform rest=Transform::Identity();
    Check(Audit::Read("tests/output/captures-head/shot-04-0.xrc",legacy)&&legacy.version==6&&legacy.size==sizeof legacy
        &&!memcmp(&legacy.head.influences[1],&rest,sizeof rest)&&legacy.head.neckError==-1&&legacy.impact.path.valid,"version 1 captures replay with the neck at rest");
    Check(Audit::Read("tests/output/captures-neck/shot-04-0.xrc",legacy)&&legacy.version==6&&legacy.size==sizeof legacy
        &&fabsf(legacy.head.neckError-.004f)<.001f&&RotationAngle(legacy.head.influences[1])>.5f&&legacy.impact.path.valid&&!legacy.head.skinTriangles
        &&legacy.head.vertices[100].reveal==1,"version 2 captures keep their neck bend and replay with a full face reveal");
    Check(Audit::Read("tests/output/captures-neck-skin/shot-20-0.xrc",legacy)&&legacy.version==6&&legacy.head.region==HeadRegion&&legacy.head.face
        &&legacy.head.skinTriangles==54&&legacy.head.skinShapes==1&&legacy.head.vertices[100].reveal<=1&&RotationAngle(legacy.head.influences[1])>0
        &&legacy.impact.path.valid,"version 3 captures keep their neck skin, weights and bend");
    Check(Audit::Read("tests/output/captures-legs/shot-10-0.xrc",legacy)&&legacy.version==6&&legacy.head.region==LegsRegion&&!legacy.head.face
        &&legacy.head.skinTriangles>0&&legacy.source.posed&&!legacy.source.carrier&&!memcmp(&legacy.head.posedBones[3],&rest,sizeof rest),
        "version 4 captures read with their frame, skin and sample, the bones at rest and no carrier");
    {
        //a version 5 capture, laid out as the plugin wrote it before the larger skin and the anatomy set
        struct HeadV5 {
            Transform head;HeadFit fit;MaskVertex vertices[kHeadVertices];uint16_t indices[kHeadTriangles*3];Influences influences;float neckError;uint32_t region;
            bool face,noGenitals;uint32_t skinVertices,skinTriangles,skinShapes,skinSkipped;MaskVertex skin[8192];uint16_t skinIndices[16384*3];Influences posedBones;
        };
        struct RecordV5 {uint32_t magic,version,size,target,support,stage;uint64_t session;HeadV5 head;CameraFrame camera;Appearance appearance;Impact::Sample source;Impact::Frame impact;};
        Check(Audit::Read("tests/output/XRayKillcamCaptures/shot-07-1.xrc",copy),"reread capture for version 5");
        auto old=std::make_unique<RecordV5>();memset(old.get(),0,sizeof *old);
        old->magic=copy.magic;old->version=5;old->size=sizeof *old;old->target=copy.target;old->support=copy.support;old->stage=copy.stage;old->session=copy.session;
        auto& h=old->head;const auto& c=copy.head;
        h.head=c.head;h.fit=c.fit;memcpy(h.vertices,c.vertices,sizeof h.vertices);memcpy(h.indices,c.indices,sizeof h.indices);h.influences=c.influences;
        h.neckError=c.neckError;h.region=c.region;h.face=c.face;h.noGenitals=true;h.skinVertices=c.skinVertices;h.skinTriangles=c.skinTriangles;
        h.skinShapes=c.skinShapes;h.skinSkipped=c.skinSkipped;memcpy(h.skin,c.skin,c.skinVertices*sizeof(MaskVertex));
        memcpy(h.skinIndices,c.skinIndices,c.skinTriangles*3*sizeof(uint16_t));h.posedBones=c.posedBones;h.posedBones[1].t={4,5,6};
        old->camera=copy.camera;old->appearance=copy.appearance;old->source=copy.source;old->impact=copy.impact;
        FILE* file=nullptr;fopen_s(&file,"tests/output/audit-v5.xrc","wb");Check(file&&fwrite(old.get(),sizeof *old,1,file)==1&&fclose(file)==0,"version 5 capture written");
        Check(Audit::Read("tests/output/audit-v5.xrc",legacy)&&legacy.version==6&&legacy.head.anatomy==HumanAnatomy&&legacy.head.noGenitals
            &&legacy.head.skinTriangles==c.skinTriangles&&!memcmp(legacy.head.skinIndices,c.skinIndices,c.skinTriangles*3*sizeof(uint16_t))
            &&legacy.head.posedBones[1].t.z==6&&!memcmp(&legacy.source,&copy.source,sizeof copy.source),
            "version 5 captures read with their skin, genitals flag and posed bones as human anatomy");
        copy.head.anatomy=kAnatomies;Write(copy,invalid);Check(!Audit::Read(invalid,r),"an unknown anatomy set is rejected");
    }
    Check(Audit::Read("tests/output/XRayKillcamCaptures/shot-07-1.xrc",copy),"reread capture for the carrier");
    Transform moved=Transform::Identity();moved.t={1,2,3};
    copy.source.carrier=2;copy.source.carrierPose=moved;copy.head.posedBones[2]=moved;Write(copy,"tests/output/audit-carrier.xrc");
    Check(Audit::Read("tests/output/audit-carrier.xrc",legacy)&&legacy.source.carrier==2&&!memcmp(&legacy.source.carrierPose,&moved,sizeof moved)
        &&!memcmp(&legacy.head.posedBones[2],&moved,sizeof moved),"the carrier bone and posed bones survive a round trip");
    copy.source.carrier=kInfluences;Write(copy,invalid);Check(!Audit::Read(invalid,r),"a carrier outside the influences is rejected");
    copy.source.carrier=2;copy.source.carrierPose.scale=0;Write(copy,invalid);Check(!Audit::Read(invalid,r),"an invalid carrier pose is rejected");
    printf("PASS: shot projection, bounded snapshot mailbox, epoch rejection, version 1 and 2 captures, neck skin round trip and private capture validation\n");
}
static void SavePixels(const std::vector<DWORD>& pixels,unsigned width,unsigned height,const char* name) {
    FILE* file=nullptr;fopen_s(&file,name,"wb");Check(file!=nullptr,"replayed shot output");
    fprintf(file,"P6\n%u %u\n255\n",width,height);
    for(auto p:pixels){unsigned char rgb[]={static_cast<unsigned char>(p>>16),static_cast<unsigned char>(p>>8),static_cast<unsigned char>(p)};fwrite(rgb,1,3,file);}
    fclose(file);
}
//moves the captured camera along its own line of sight until the head spans most of a square frame
static XRay::CameraFrame Closer(const XRay::Audit::Record& r,float distance,unsigned size) {
    using namespace XRay;
    CameraFrame c=r.camera;Vec3 origin,forward;Check(Audit::CentreRay(r.camera,origin,forward),"captured perspective camera");
    Vec3 centre=r.impact.path.valid?r.head.head.Apply(r.impact.path.entry):r.head.head.Apply(Fit::Warp(r.head.fit,{8,0,0}));
    Vec3 axis=centre-origin;Normalize(axis);Vec3 eye=centre-axis*distance;
    Vec3 up={c.view[1],c.view[5],c.view[9]};Vec3 right=Cross(up,axis);Normalize(right);up=Cross(axis,right);
    Vec3 relative=eye-c.origin;
    const float view[16]={right.x,up.x,axis.x,0,right.y,up.y,axis.y,0,right.z,up.z,axis.z,0,-Dot(relative,right),-Dot(relative,up),-Dot(relative,axis),1};
    memcpy(c.view,view,sizeof view);
    float focal=1/tanf(.25f);
    float projection[16]={focal,0,0,0,0,focal,0,0,0,0,1000.0f/999,1,0,0,-1000.0f/999,0};
    if(r.camera.Reversed()){projection[10]=-1.0f/999;projection[14]=1000.0f/999;}
    memcpy(c.projection,projection,sizeof projection);
    c.viewport={0,0,size,size,0,1};return c;
}
//captures from before the neck skin get the reference face weights and the vanilla torso posed by their neck bend
static void VanillaNeck(XRay::Audit::Record& r,std::vector<XRay::Vec3>& scene) {
    using namespace XRay;
    FILE* file=nullptr;fopen_s(&file,"tests/output/reveal/headhuman.reveal.bin","rb");Check(file!=nullptr,"private face weight fixture");
    for(auto& v:r.head.vertices)Check(fread(&v.reveal,4,1,file)==1,"face weight");fclose(file);
    fopen_s(&file,"tests/output/reveal/upperbody.bin","rb");Check(file!=nullptr,"private torso fixture");
    uint32_t counts[2];Check(fread(counts,sizeof counts,1,file)==1,"torso counts");
    std::vector<float> vertices(counts[0]*4);std::vector<uint16_t> indices(counts[1]*3);
    Check(fread(vertices.data(),4,vertices.size(),file)==vertices.size()&&fread(indices.data(),2,indices.size(),file)==indices.size(),"torso data");fclose(file);
    auto& h=r.head;h.skinVertices=h.skinTriangles=0;
    std::vector<Vec3> world(counts[0]);std::vector<float> reveal(counts[0]);std::vector<uint16_t> slot;
    for(uint32_t i=0;i<counts[0];++i){world[i]=h.head.Apply(h.influences[1].Apply({vertices[i*4],vertices[i*4+1],vertices[i*4+2]}));reveal[i]=vertices[i*4+3];}
    for(auto i:indices)scene.push_back(world[i]);
    Check(BodySkin::Add({world.data(),reveal.data(),counts[0],indices.data(),uint32_t(indices.size())},slot,h)==BodySkin::Kept,"vanilla torso assembled as captured skin");
    h.skinShapes=1;
    printf("vanilla neck: torso %u vertices %u triangles posed by the neck bend %.1fdeg\n",h.skinVertices,h.skinTriangles,RotationAngle(h.influences[1])*57.2957795f);
}
void ReplayCapture(IDirect3DDevice9* d,const XRay::Model& model,const char* path,const float* ages,unsigned count,bool close,bool bare,bool groin,float severity) {
    using namespace XRay;
    Audit::Record r;Check(Audit::Read(path,r),"read shot capture");
    Graphics::SetShotForce(severity,1.5f);
    std::vector<Vec3> scene;
    //each body region replays with its own anatomy, the head with the asset on the command line
    static Model regions[kRegions];
    static const char* const files[kRegions]={nullptr,"package\\meshes\\XRayKillcamNVSE\\torso.xra","package\\meshes\\XRayKillcamNVSE\\legs.xra","package\\meshes\\XRayKillcamNVSE\\arms.xra"};
    uint32_t region=r.head.region<kRegions?r.head.region:HeadRegion;
    if(region!=HeadRegion&&regions[region].meshes.empty())Check(regions[region].Load(files[region]),"region anatomy for its captures");
    static Model female;
    //women and creatures show the torso without the external genitals
    bool women=region==TorsoRegion&&r.head.noGenitals;
    if(women&&female.meshes.empty())Check(female.Load("package\\meshes\\XRayKillcamNVSE\\torso-female.xra"),"torso without genitals for their captures");
    const Model& anatomy=region==HeadRegion?model:women?female:regions[region];
    //a groin replay reveals what the runtime shows for a shot through the genitals
    BodySkin::OpenOver(anatomy,r.head,groin&&region==TorsoRegion);
    if(!r.head.skinTriangles&&r.head.face)VanillaNeck(r,scene);
    if(r.head.face)for(uint32_t i=0;i<kHeadTriangles*3;++i)scene.push_back(r.head.vertices[r.head.indices[i]].world);
    for(uint32_t i=0;i<r.head.skinTriangles*3;++i)scene.push_back(r.head.skin[r.head.skinIndices[i]].world);
    Impact::Path remapped;bool mapped=Impact::Map(r.source,r.head,remapped);
    printf("captured %s session=%llu sequence=%llu target=%08X stage=%u support=%u map=%u replay=%u age=%.4f\n",RegionName(region),
        r.session,r.impact.sequence,r.target,r.stage,r.support,r.impact.path.valid?1:0,mapped?1:0,r.impact.age);
    if(mapped&&!r.impact.path.valid){r.impact.path=remapped;printf("using remapped path from the current mapper\n");}
    if(!mapped&&r.source.valid) {
        //where the shot line meets the captured skin, in the frame the mapper used, to see which of its limits it failed
        const Transform& pose=r.source.posed?r.source.pose:r.head.head;
        Vec3 point=pose.Unapply(r.source.point),direction=pose.Unrotate(r.source.direction);Normalize(direction);
        if(r.source.traced){Vec3 origin=pose.Unapply(r.source.rayOrigin);float along=Dot(point-origin,direction);if(along>0)point=origin+direction*along;}
        std::vector<float> hits;float nearest=1e9f;
        for(uint32_t i=0;i<r.head.skinTriangles*3;i+=3) {
            Vec3 a=r.head.skin[r.head.skinIndices[i]].local,e1=r.head.skin[r.head.skinIndices[i+1]].local-a,e2=r.head.skin[r.head.skinIndices[i+2]].local-a;
            nearest=fminf(nearest,Length(a-point));
            Vec3 q=Cross(direction,e2);float det=Dot(e1,q);if(fabsf(det)<1e-9f)continue;
            Vec3 o=point-a;float u=Dot(o,q)/det;if(u<0||u>1)continue;Vec3 w=Cross(o,e1);float v=Dot(direction,w)/det;if(v<0||u+v>1)continue;
            hits.push_back(Dot(e2,w)/det);
        }
        std::sort(hits.begin(),hits.end());
        printf("unmapped: posed=%u point=(%.2f,%.2f,%.2f) direction=(%.3f,%.3f,%.3f) nearest skin vertex %.2f, %zu crossings:",r.source.posed?1u:0u,
            point.x,point.y,point.z,direction.x,direction.y,direction.z,nearest,hits.size());
        for(float h:hits)printf(" %.2f",h);
        printf("\n");
    }
    if(r.impact.path.valid) {
        //how close the shot passes to each posed anatomy batch, measured on its vertices
        auto posing=Damage::PosingOf(r.head,r.appearance);Vec3 a=r.impact.path.entry,b=r.impact.path.exit,ab=b-a;float length=Length(ab);Normalize(ab);
        printf("path entry=(%.2f,%.2f,%.2f) exit=(%.2f,%.2f,%.2f) length=%.2f\n",a.x,a.y,a.z,b.x,b.y,b.z,length);
        for(size_t n=0;n<anatomy.meshes.size();++n) {
            const auto& m=anatomy.meshes[n];float nearest=1e9f;
            for(const auto& v:m.vertices){Vec3 p=posing.Place(v.position,v.bind,v.bones)-a;float t=Clamp(Dot(p,ab),0,length);nearest=fminf(nearest,Length(p-ab*t));}
            if(nearest<1.5f)printf("  batch %zu material %u bound %u nearest %.2f\n",n,m.material,m.bound?1u:0u,nearest);
        }
    }
    float start=r.impact.sceneTime>=0?r.impact.sceneTime-r.impact.age:-1;
    if(close)r.camera=Closer(r,16,1024);
    unsigned width=r.camera.viewport.X+r.camera.viewport.Width,height=r.camera.viewport.Y+r.camera.viewport.Height;
    for(unsigned n=0;n<(count?count:1);++n) {
        if(count){r.impact.age=ages[n];r.impact.sceneTime=start>=0?start+ages[n]:-1;}
        Graphics::DamageReport report;auto pixels=Render(d,anatomy,r,&report,bare?nullptr:&scene);
        char name[MAX_PATH];sprintf_s(name,"tests/output/captured-shot-%u.ppm",n);
        SavePixels(pixels,width,height,count?name:"tests/output/captured-shot.ppm");
        printf("saved %s age=%.3f wounds=%u boneRuns=%u brainRuns=%u breaks=%u/%u/%u plates=%u fragments=%u brain=%u ruptured=%u vertices=%u\n",count?name:"tests/output/captured-shot.ppm",
            r.impact.age,report.wounds,report.boneRuns,report.brainRuns,report.entry,report.inner,report.exit,report.plates,report.fragments,report.brainPieces,report.ruptures,report.vertices);
    }
}
void TestAuditRender(IDirect3DDevice9* d,const XRay::Model& model) {
    using namespace XRay;
    Audit::Record r;Check(Audit::Read("tests/output/audit-replay.xrc",r),"render capture fixture");
    auto pixels=Render(d,model,r);Write(r,"tests/output/audit-roundtrip.xrc");
    Audit::Record copy;Check(Audit::Read("tests/output/audit-roundtrip.xrc",copy),"read render roundtrip");
    Check(pixels==Render(d,model,copy),"captured frame survives binary roundtrip with identical shader output");
    unsigned visible=0;for(auto p:pixels)visible+=p!=0xFF302C27;
    Check(visible>500,"replay actually contains visible anatomy");
    printf("PASS: exact captured frame roundtrip, %u anatomy pixels\n",visible);
}
