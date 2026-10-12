#include "../src/Graphics.h"
#include "RecordedShots.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
using namespace XRay;
void Check(bool ok,const char* name){if(!ok){printf("FAIL: %s\n",name);exit(1);}}
void Hr(HRESULT hr){Check(SUCCEEDED(hr),"gameplay D3D call");}
constexpr unsigned size=240;
std::vector<DWORD> Read(IDirect3DDevice9* d,IDirect3DSurface9* rt) {
    IDirect3DSurface9* copy=nullptr;Hr(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,nullptr));
    Hr(d->GetRenderTargetData(rt,copy));D3DLOCKED_RECT lock;Hr(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::vector<DWORD> pixels(size*size);
    for(unsigned y=0;y<size;++y)memcpy(pixels.data()+y*size,static_cast<char*>(lock.pBits)+y*lock.Pitch,size*4);
    Hr(copy->UnlockRect());copy->Release();return pixels;
}
void Save(const std::vector<DWORD>& pixels,const char* name) {
    FILE* file=nullptr;fopen_s(&file,name,"wb");Check(file!=nullptr,"gameplay output");
    fprintf(file,"P6\n%u %u\n255\n",size,size);
    for(auto p:pixels){unsigned char rgb[]={static_cast<unsigned char>(p>>16),static_cast<unsigned char>(p>>8),static_cast<unsigned char>(p)};fwrite(rgb,1,3,file);}
    fclose(file);
}
}
void TestGameplay(IDirect3DDevice9* d,const XRay::Model& model,const XRay::HeadFrame& head) {
    using namespace XRay;
    Graphics::Release();IDirect3DSurface9 *rt=nullptr,*depth=nullptr;
    Hr(d->CreateRenderTarget(size,size,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt,nullptr));
    Hr(d->CreateDepthStencilSurface(size,size,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    Hr(d->SetDepthStencilSurface(nullptr));Hr(d->SetRenderTarget(0,rt));Hr(d->SetDepthStencilSurface(depth));
    std::vector<RecordedShots::Shot> shots(std::begin(RecordedShots::recent),std::end(RecordedShots::recent));
    Impact::Sample crown;crown.valid=true;crown.point={10,5,1};crown.direction={0,-1,0};Impact::Path path;
    Check(Impact::Map(crown,head,path),"gameplay crown route");shots.push_back({100,path});
    for(auto shot:shots)for(unsigned height:{80u,120u})for(unsigned angle=0;angle<3;++angle) {
        Vec3 direction=shot.path.exit-shot.path.entry;Normalize(direction);
        Vec3 side=Cross(direction,{1,0,0});Normalize(side);
        Vec3 view=direction*cosf(angle*.55f)+side*sinf(angle*.55f);Normalize(view);
        Vec3 eye=Vec3{7,0,0}-view*(17*size/(1.4f*height)),right=Cross({1,0,0},view);Normalize(right);
        Vec3 up=Cross(view,right);CameraFrame camera{};camera.viewport={0,0,size,size,0,1};
        const float matrix[]={right.x,up.x,view.x,0,right.y,up.y,view.y,0,right.z,up.z,view.z,0,-Dot(eye,right),-Dot(eye,up),-Dot(eye,view),1};
        memcpy(camera.view,matrix,sizeof matrix);camera.projection[0]=camera.projection[5]=1/.7f;
        camera.projection[10]=1000.0f/999;camera.projection[11]=1;camera.projection[14]=-1000.0f/999;
        std::vector<DWORD> settled,burst;
        const float ages[]={.06f,.12f,.25f,.45f,.70f,1.2f,3};
        unsigned movement=0;
        auto render=[&](const Impact::Frame* impact) {
            Hr(d->SetViewport(&camera.viewport));Hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xFF302C27,1,0));
            Hr(d->BeginScene());Check(Graphics::Draw(d,model,camera,head,2,{},impact),"gameplay rendering");Hr(d->EndScene());
            return Read(d,rt);
        };
        auto draw=[&](float age) {
            Impact::Frame impact{shot.path,age,5,Ammunition::Resolve(0x6B53D),shot.sequence+900};
            return render(&impact);
        };
        for(unsigned stage=0;stage<7;++stage) {
            auto pixels=draw(ages[stage]);
            if(stage==6)Check(pixels==settled,"residual wound persists after the burst expires");
            else if(stage==5)settled=pixels;
            if(stage==2)burst=pixels;
            if(stage==3)for(unsigned i=0;i<pixels.size();++i)movement+=pixels[i]!=burst[i];
            char name[100];sprintf_s(name,"tests/output/gameplay-%u-%u-%u-%u.ppm",shot.sequence,height,angle,stage);Save(pixels,name);
        }
        if(shot.sequence==100)Check(movement>30,"cranial burst has visible movement at 80/120-pixel head scale after the bullet is gone");
        printf("gameplay path %u height=%u angle=%u: moving pixels=%u\n",shot.sequence,height,angle,movement);
        if(height==80&&angle==0&&(shot.sequence==100||shot.sequence==27))for(unsigned n=0;n<=36;++n) {
            auto pixels=draw(n/30.0f);char name[100];sprintf_s(name,"tests/output/burst-%u-%02u.ppm",shot.sequence,n);Save(pixels,name);
        }
        if(shot.sequence==100&&height==80&&angle==0) {
            Graphics::Release();
            Impact::Frame absent{};absent.sequence=10001;absent.sceneTime=100.2f;
            auto clean=render(&absent);
            Impact::Frame hit{shot.path,.15f,5,Ammunition::Resolve(0x6B53D),10002,0,100.15f};render(&hit);
            auto retained=render(&absent);unsigned changed=0;
            for(unsigned i=0;i<clean.size();++i)changed+=clean[i]!=retained[i];
            Check(changed>30,"unmapped subsequent hit retains visible debris through both render passes");
            Save(retained,"tests/output/unmapped-followup.ppm");
            absent.sceneTime=102;auto followup=render(&absent);
            Impact::Frame late=hit;late.age=1.85f;late.sceneTime=102;
            Check(followup!=clean&&followup==render(&late),"unmapped follow-up keeps exactly the earlier settled wound once its debris expires");
            hit.sceneTime=103.15f;render(&hit);render(nullptr);absent.sceneTime=103.2f;
            Check(render(&absent)==clean,"manual preview clears old bursts instead of leaking them into the next selection");
            hit.sceneTime=104.15f;render(&hit);Graphics::ClearDamage();absent.sceneTime=104.2f;
            Check(render(&absent)==clean,"explicit reset clears bursts even if no inactive frame was rendered");
            printf("PASS: unmapped follow-up retains %u moving pixels and the earlier wound, then manual and explicit reset clear it\n",changed);
        }
    }
    Graphics::Release();rt->Release();depth->Release();
    printf("PASS: 23 paths, 80/120-pixel head framing, shooter and two oblique views, flying debris and residual wounds\n");
}
