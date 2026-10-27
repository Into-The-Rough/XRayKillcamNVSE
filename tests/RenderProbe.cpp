#include "../src/Graphics.h"
#include "../src/Damage.h"
#include "RecordedShots.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace XRay;
HeadFit TestFaceFit(const Model& model);
void TestImpact();
void TestFracture(const Model& model,const HeadFrame& head);
void TestNeck(const Model& model);
Transform TestRotation(Vec3 axis,float degrees,Vec3 pivot);
void TestFractureGPU(IDirect3DDevice9* d,const Model& model,const HeadFrame& head);
void TestGameplay(IDirect3DDevice9* d,const Model& model,const HeadFrame& head);
void TestAudit(const HeadFrame& head);
void TestSkin();
void TestSkinBinds();
void TestOpenOver();
void TestTorso(IDirect3DDevice9* d,const Model& torso);
void TestRegionPlanning(const Model& anatomy,unsigned which);
void TestCarrier();
void TestCloseUpPose();
void TestCloseUpWalls();
void TestCloseUpFollow();
void TestGroin(const Model& torso);
void TestRupture(const Model& torso);
void TestKillShot(const Model& torso);
void TestGroinRender(IDirect3DDevice9* d,const Model& torso);
void TestKillRender(IDirect3DDevice9* d,const Model& torso);
void TestBloodSeen(IDirect3DDevice9* d,const Model& torso);
void TestRestoreRender(IDirect3DDevice9* d,const Model& torso);
void TestRegionRender(IDirect3DDevice9* d,const Model& anatomy,unsigned which);
void TestAuditRender(IDirect3DDevice9* d,const Model& model);
void ReplayCapture(IDirect3DDevice9* d,const Model& model,const char* path,const float* ages,unsigned count,bool close,bool bare,bool groin,float severity);
static void Require(bool ok,const char* name){if(!ok){printf("FAIL: %s (render HRESULT %08X)\n",name,unsigned(Graphics::LastError()));exit(1);}}
static void Hr(HRESULT hr){if(FAILED(hr)){printf("FAIL: D3D call HRESULT %08X\n",unsigned(hr));exit(1);}}
static void SlopedQuad(IDirect3DDevice9* d,float left,float right,float zl,float zr,DWORD color,unsigned height=128) {
    struct V {float x,y,z,w;DWORD color;};
    V v[]={{left-.5f,-.5f,zl,1,color},{right-.5f,-.5f,zr,1,color},{right-.5f,float(height)-.5f,zr,1,color},
        {left-.5f,-.5f,zl,1,color},{right-.5f,float(height)-.5f,zr,1,color},{left-.5f,float(height)-.5f,zl,1,color}};
    Hr(d->SetVertexShader(nullptr));Hr(d->SetPixelShader(nullptr));Hr(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
    Hr(d->SetTexture(0,nullptr));Hr(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
    Hr(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));Hr(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    Hr(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,v,sizeof(V)));
}
static void Quad(IDirect3DDevice9* d,float left,float right,float z,DWORD color,unsigned height=128) {SlopedQuad(d,left,right,z,z,color,height);}
static std::vector<DWORD> Pixels(IDirect3DDevice9* d,IDirect3DSurface9* source,unsigned size) {
    IDirect3DSurface9* copy=nullptr;Hr(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,nullptr));
    Hr(d->GetRenderTargetData(source,copy));D3DLOCKED_RECT r;Hr(copy->LockRect(&r,nullptr,D3DLOCK_READONLY));
    std::vector<DWORD> pixels(size*size);
    for(unsigned y=0;y<size;++y)memcpy(pixels.data()+y*size,static_cast<char*>(r.pBits)+y*r.Pitch,size*4);
    Hr(copy->UnlockRect());copy->Release();return pixels;
}
static CameraFrame Camera(unsigned size,bool reversed) {
    CameraFrame c{};
    for(unsigned i=0;i<4;++i)c.view[i*5]=c.projection[i*5]=1;
    if(reversed){c.projection[10]=-1;c.projection[14]=1;}
    c.viewport={0,0,size,size,0,1};return c;
}
static void Mask(HeadFrame& f) {
    f.head=Transform::Identity();f.fit=HeadFit{};
    f.vertices[0]={{-1,-1,.5f},{10,0,0}};f.vertices[1]={{1,-1,.5f},{10,0,0}};
    f.vertices[2]={{1,1,.5f},{10,0,0}};f.vertices[3]={{-1,1,.5f},{10,0,0}};
    const uint16_t ii[]={0,1,2,0,2,3};
    for(unsigned i=0;i<kHeadTriangles*3;++i)f.indices[i]=ii[i%6];
}
static Model Synthetic() {
    Model m;Mesh mesh{};mesh.material=0;mesh.visibility=3;
    mesh.vertices={{{-.8f,-.8f,.7f},{0,0,-1},1},{{.8f,-.8f,.7f},{0,0,-1},1},{{.8f,.8f,.7f},{0,0,-1},1},{{-.8f,.8f,.7f},{0,0,-1},1}};
    mesh.indices={0,1,2,0,2,3};m.meshes.push_back(std::move(mesh));return m;
}
static void Contract(IDirect3DDevice9* d,D3DMULTISAMPLE_TYPE samples,bool reversed,bool shot=false) {
    Graphics::Release();IDirect3DSurface9 *rt=nullptr,*ds=nullptr,*resolved=nullptr;
    Hr(d->CreateRenderTarget(128,128,D3DFMT_A8R8G8B8,samples,0,FALSE,&rt,nullptr));
    Hr(d->CreateDepthStencilSurface(128,128,D3DFMT_D24S8,samples,0,FALSE,&ds,nullptr));
    Hr(d->SetDepthStencilSurface(nullptr));Hr(d->SetRenderTarget(0,rt));Hr(d->SetDepthStencilSurface(ds));
    CameraFrame c=Camera(128,reversed);Hr(d->SetViewport(&c.viewport));
    Hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,reversed?0.0f:1.0f,0));Hr(d->BeginScene());
    Hr(d->SetRenderState(D3DRS_ZENABLE,TRUE));Hr(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
    Hr(d->SetRenderState(D3DRS_ZFUNC,reversed?D3DCMP_GREATEREQUAL:D3DCMP_LESSEQUAL));
    Hr(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    Quad(d,0,128,.5f,0xFF303030);Quad(d,0,64,reversed?.75f:.25f,0xFF2040C0);
    Hr(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW));
    Hr(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_REVSUBTRACT));
    Hr(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,TRUE));
    IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
    Hr(d->CreateVertexBuffer(256,0,0,D3DPOOL_DEFAULT,&vb,nullptr));Hr(d->CreateIndexBuffer(32,0,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr));
    Hr(d->SetStreamSource(0,vb,0,20));Hr(d->SetIndices(ib));
    float savedConstants[152];for(unsigned i=0;i<152;++i)savedConstants[i]=float(i+1);
    Hr(d->SetPixelShaderConstantF(2,savedConstants,38));
    //fitting, piece transforms, the influence rows and the seed table must all come back as the game left them
    std::vector<float> savedFit(241*4);for(size_t i=0;i<savedFit.size();++i)savedFit[i]=float(i)*.1f;
    Hr(d->SetVertexShaderConstantF(15,savedFit.data(),241));
    const int savedLoops[12]={7,1,2,0,9,3,4,0,11,5,6,0};Hr(d->SetPixelShaderConstantI(0,savedLoops,3));
    IDirect3DTexture9* gameTexture=nullptr;Hr(d->CreateTexture(4,4,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&gameTexture,nullptr));
    Hr(d->SetTexture(3,gameTexture));Hr(d->SetSamplerState(3,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));
    HeadFrame f{};Mask(f);auto m=Synthetic();
    Appearance style;style.depthTolerance=.01f;
    Impact::Frame impact{{{-.6f,0,.7f},{.6f,0,.7f},true},.015f,5};
    Require(Graphics::Draw(d,m,c,f,2,style,shot?&impact:nullptr),"render pass");
    DWORD state=0;Hr(d->GetRenderState(D3DRS_CULLMODE,&state));Require(state==D3DCULL_CW,"cull restored");
    Hr(d->GetRenderState(D3DRS_ZWRITEENABLE,&state));Require(state==TRUE,"zwrite restored");
    Hr(d->GetRenderState(D3DRS_BLENDOP,&state));Require(state==D3DBLENDOP_REVSUBTRACT,"blend operation restored");
    Hr(d->GetSamplerState(0,D3DSAMP_SRGBTEXTURE,&state));Require(state==TRUE,"sampler restored");
    float constants[152];Hr(d->GetPixelShaderConstantF(2,constants,38));
    Require(!memcmp(constants,savedConstants,sizeof constants),"mask pixel constants restored");
    std::vector<float> fitConstants(241*4);Hr(d->GetVertexShaderConstantF(15,fitConstants.data(),241));
    Require(fitConstants==savedFit,"fitting and piece transform vertex constants restored");
    int loops[12];Hr(d->GetPixelShaderConstantI(0,loops,3));Require(!memcmp(loops,savedLoops,sizeof loops),"seed loop counts restored");
    IDirect3DBaseTexture9* stage=nullptr;Hr(d->GetTexture(3,&stage));Require(stage==gameTexture,"seed table stage restored");if(stage)stage->Release();
    Hr(d->GetSamplerState(3,D3DSAMP_MAGFILTER,&state));Require(state==D3DTEXF_LINEAR,"seed table sampler restored");
    Hr(d->SetTexture(3,nullptr));gameTexture->Release();
    IDirect3DSurface9* check=nullptr;Hr(d->GetRenderTarget(0,&check));Require(check==rt,"target restored");check->Release();
    Hr(d->GetDepthStencilSurface(&check));Require(check==ds,"depth restored");check->Release();
    D3DVIEWPORT9 viewport{};Hr(d->GetViewport(&viewport));Require(!memcmp(&viewport,&c.viewport,sizeof viewport),"viewport restored");
    IDirect3DVertexBuffer9* vcheck=nullptr;UINT offset,stride;Hr(d->GetStreamSource(0,&vcheck,&offset,&stride));
    Require(vcheck==vb&&stride==20,"vertex stream restored");vcheck->Release();
    IDirect3DIndexBuffer9* icheck=nullptr;Hr(d->GetIndices(&icheck));Require(icheck==ib,"indices restored");icheck->Release();
    Quad(d,64,128,reversed?.4f:.6f,0xFF00FF00);
    Hr(d->EndScene());
    IDirect3DSurface9* source=rt;
    if(samples!=D3DMULTISAMPLE_NONE){Hr(d->CreateRenderTarget(128,128,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));Hr(d->StretchRect(rt,nullptr,resolved,nullptr,D3DTEXF_NONE));source=resolved;}
    auto pixels=Pixels(d,source,128);
    Require(pixels[64*128+32]==0xFF2040C0,"foreground occluder retained");
    Require(pixels[64*128+96]!=0xFF303030&&pixels[64*128+96]!=0xFF00FF00,"anatomy revealed and world depth preserved");
    Require(pixels[4*128+96]!=0xFF303030,"gaps inside reveal replace the original skin");
    printf("PASS: mask, occlusion, depth and state restoration (MSAA=%u reversed=%u impact=%u)\n",unsigned(samples),unsigned(reversed),unsigned(shot));
    Graphics::Release();Hr(d->SetStreamSource(0,nullptr,0,0));Hr(d->SetIndices(nullptr));vb->Release();ib->Release();
    if(resolved)resolved->Release();rt->Release();ds->Release();
}
static void Closeups(IDirect3DDevice9* d,D3DMULTISAMPLE_TYPE samples,bool reversed,bool subrange) {
    IDirect3DSurface9 *rt=nullptr,*ds=nullptr,*resolved=nullptr;
    Hr(d->CreateRenderTarget(128,128,D3DFMT_A8R8G8B8,samples,0,FALSE,&rt,nullptr));
    Hr(d->CreateDepthStencilSurface(128,128,D3DFMT_D24S8,samples,0,FALSE,&ds,nullptr));
    if(samples!=D3DMULTISAMPLE_NONE)Hr(d->CreateRenderTarget(128,128,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
    Hr(d->SetDepthStencilSurface(nullptr));Hr(d->SetRenderTarget(0,rt));Hr(d->SetDepthStencilSurface(ds));
    auto c=Camera(128,reversed);constexpr float nearPlane=5,farPlane=10000;
    c.projection[10]=reversed?nearPlane/(nearPlane-farPlane):farPlane/(farPlane-nearPlane);
    c.projection[14]=reversed?nearPlane*farPlane/(farPlane-nearPlane):-nearPlane*farPlane/(farPlane-nearPlane);
    c.projection[11]=1;c.projection[15]=0;
    if(subrange){c.viewport.MinZ=.2f;c.viewport.MaxZ=.8f;}
    auto project=[&](float z){return c.viewport.MinZ+(c.projection[10]+c.projection[14]/z)*(c.viewport.MaxZ-c.viewport.MinZ);};
    for(float distance:{6.0f,12.0f,40.0f,120.0f,500.0f,2500.0f}) {
        Graphics::Release();auto m=Synthetic();
        for(auto& v:m.meshes[0].vertices){v.position.x*=distance+20;v.position.y*=distance+20;v.position.z=distance+20;}
        const float right=distance+10;
        auto surface=[&](float x){return 1.0f/((1-x/128)/distance+(x/128)/right);};
        for(float jitter:{.06f,.02f,-.04f,.04f,-.02f,.05f}) {
            Hr(d->SetViewport(&c.viewport));
            Hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,reversed?0.0f:1.0f,0));Hr(d->BeginScene());
            Hr(d->SetRenderState(D3DRS_ZENABLE,TRUE));Hr(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
            Hr(d->SetRenderState(D3DRS_ZFUNC,reversed?D3DCMP_GREATEREQUAL:D3DCMP_LESSEQUAL));
            Hr(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            SlopedQuad(d,0,128,project(distance),project(right),0xFF303030);
            SlopedQuad(d,76,128,project(surface(76)-1.25f),project(right-1.25f),0xFF101010);
            SlopedQuad(d,64,76,project(surface(64)-3),project(surface(76)-3),0xFF2040C0);
            HeadFrame f{};Mask(f);
            float leftZ=surface(16)+jitter,rightZ=surface(112)+jitter;
            f.vertices[0].world={-.75f*leftZ,-.75f*leftZ,leftZ};
            f.vertices[1].world={.75f*rightZ,-.75f*rightZ,rightZ};
            f.vertices[2].world={.75f*rightZ,.75f*rightZ,rightZ};
            f.vertices[3].world={-.75f*leftZ,.75f*leftZ,leftZ};
            Require(Graphics::Draw(d,m,c,f,2,{}),"perspective mask pass");
            SlopedQuad(d,0,128,project(distance+.5f),project(right+.5f),0xFF00FF00);
            Hr(d->EndScene());
            if(resolved)Hr(d->StretchRect(rt,nullptr,resolved,nullptr,D3DTEXF_NONE));
            auto pixels=Pixels(d,resolved?resolved:rt,128);
            for(unsigned y=24;y<104;++y) {
                for(unsigned x=24;x<60;++x)Require(pixels[y*128+x]!=0xFF303030&&pixels[y*128+x]!=0xFF00FF00,"stable face coverage with depth mismatch");
                for(unsigned x=82;x<104;++x)Require(pixels[y*128+x]!=0xFF101010&&pixels[y*128+x]!=0xFF00FF00,"near-surface hair allowance");
                for(unsigned x=66;x<74;++x)Require(pixels[y*128+x]==0xFF2040C0,"foreground beyond tolerance retained");
                Require(pixels[y*128+13]==0xFF303030&&pixels[y*128+114]==0xFF101010,"mask silhouette unchanged");
            }
        }
    }
    printf("PASS: 36 sloped perspective poses, 6..2500 units, face mismatch, hair allowance, foreground and silhouette (MSAA=%u reversed=%u subrange=%u)\n",
        unsigned(samples),unsigned(reversed),unsigned(subrange));
    Graphics::Release();if(resolved)resolved->Release();rt->Release();ds->Release();
}
static void RevealContract(IDirect3DDevice9* d) {
    Graphics::Release();IDirect3DSurface9 *rt=nullptr,*ds=nullptr;
    Hr(d->CreateRenderTarget(128,128,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt,nullptr));
    Hr(d->CreateDepthStencilSurface(128,128,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&ds,nullptr));
    Hr(d->SetDepthStencilSurface(nullptr));Hr(d->SetRenderTarget(0,rt));Hr(d->SetDepthStencilSurface(ds));
    auto c=Camera(128,false);HeadFrame f{};Mask(f);
    for(unsigned i=0;i<4;++i){f.vertices[i].world.x*=.75f;f.vertices[i].world.y*=.75f;}
    for(unsigned i=0;i<4;++i){f.vertices[i+4]=f.vertices[i];f.vertices[i+4].world.z=.55f;f.vertices[i+4].local={.05f,0,0};}
    const uint16_t rear[]={4,5,6,4,6,7};
    for(unsigned i=6;i<kHeadTriangles*3;++i)f.indices[i]=rear[i%6];
    auto m=Synthetic();for(auto& v:m.meshes[0].vertices){v.position.x*=.25f;v.position.y*=.25f;}
    DWORD gap=0,bone=0;
    for(unsigned pass=0;pass<3;++pass) {
        DWORD background=pass==0?0xFF2040C0:0xFFC04020;
        Hr(d->SetViewport(&c.viewport));Hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,background,1,0));Hr(d->BeginScene());
        Appearance style;style.opacity=pass==2?0.0f:1.0f;
        Require(Graphics::Draw(d,m,c,f,2,style),"continuous reveal render");Hr(d->EndScene());
        auto pixels=Pixels(d,rt,128);
        Require(pixels[64*128+13]==background&&pixels[64*128+114]==background,"reveal never expands the silhouette");
        if(pass==0){gap=pixels[64*128+96];bone=pixels[64*128+64];}
        else if(pass==1)Require(pixels[64*128+96]==gap&&pixels[64*128+64]==bone,"skin color cannot leak through interior gaps or anatomy");
        if(pass<2)Require(pixels[64*128+16]!=background&&pixels[64*128+16]!=gap,"soft tissue transition inside the boundary");
        else for(auto p:pixels)Require(p==background,"zero opacity restores the complete original image");
    }
    Hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xFF2040C0,1,0));
    c.viewport={24,16,80,96,0,1};Mask(f);Hr(d->SetViewport(&c.viewport));Hr(d->BeginScene());
    Require(Graphics::Draw(d,m,c,f,2,{}),"offset viewport reveal");Hr(d->EndScene());
    auto offsetPixels=Pixels(d,rt,128);
    for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x)
        if(x<24||x>=104||y<16||y>=112)Require(offsetPixels[y*128+x]==0xFF2040C0,"offset viewport bounds preserved");
    Require(offsetPixels[64*128+30]!=0xFF2040C0,"offset viewport contains reveal");
    auto beyond=c;beyond.viewport={100,16,80,96,0,1};Model empty;Hr(d->BeginScene());
    Require(!Graphics::Draw(d,m,beyond,f,2,{})&&Graphics::Skipped(),"a viewport past the target skips the frame without failing");
    Hr(d->SetDepthStencilSurface(nullptr));
    Require(!Graphics::Draw(d,m,c,f,2,{})&&Graphics::Skipped(),"a frame without a depth buffer skips without failing");
    Hr(d->SetDepthStencilSurface(ds));
    Require(!Graphics::Draw(d,empty,c,f,2,{})&&!Graphics::Skipped(),"an unusable model fails rather than skips");
    Require(Graphics::Draw(d,m,c,f,2,{}),"the next fitting frame draws after skipped ones");Hr(d->EndScene());
    printf("PASS: overlapping mask coverage, continuous cavity, skin-independent interior, bounded rim, offset viewport, skipped frames and zero opacity\n");
    Graphics::Release();rt->Release();ds->Release();
}
//the face quad fades from no head or neck weight on the left to full weight on the right
static void NeckRevealContract(IDirect3DDevice9* d) {
    Graphics::Release();IDirect3DSurface9 *rt=nullptr,*ds=nullptr;
    Hr(d->CreateRenderTarget(128,128,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt,nullptr));
    Hr(d->CreateDepthStencilSurface(128,128,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&ds,nullptr));
    Hr(d->SetDepthStencilSurface(nullptr));Hr(d->SetRenderTarget(0,rt));Hr(d->SetDepthStencilSurface(ds));
    auto c=Camera(128,false);HeadFrame f{};Mask(f);
    f.vertices[0].reveal=f.vertices[3].reveal=0;
    auto m=Synthetic();for(auto& v:m.meshes[0].vertices){v.position.x*=.1f;v.position.y*=.1f;}
    auto render=[&](const char* name) {
        Hr(d->SetViewport(&c.viewport));Hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xFF2040C0,1,0));Hr(d->BeginScene());
        Require(Graphics::Draw(d,m,c,f,2,{}),name);Hr(d->EndScene());return Pixels(d,rt,128);
    };
    auto pixels=render("neck weight reveal render");
    for(unsigned y=8;y<120;++y) {
        Require(pixels[y*128+1]==0xFF2040C0,"no reveal where the skin has no head or neck weight");
        Require(pixels[y*128+100]!=0xFF2040C0,"full reveal on skin moved by the head and neck");
    }
    auto blue=[&](unsigned x){return int(pixels[64*128+x]&255);};
    Require(blue(4)>blue(16)&&blue(16)>blue(40),"the reveal fades in with the neck weight");
    //the right half of the face is gone, neck skin with full weight covers it and with no weight leaves it
    f.vertices[0].reveal=f.vertices[3].reveal=1;
    for(unsigned i:{1u,2u})f.vertices[i].world.x=0;
    const Vec3 corners[]={{0,-1,.5f},{1,-1,.5f},{1,1,.5f},{0,1,.5f}};const uint16_t quad[]={0,1,2,0,2,3};
    for(unsigned i=0;i<4;++i)f.skin[i]={corners[i],{10,0,0},1};
    f.skinVertices=4;f.skinTriangles=2;memcpy(f.skinIndices,quad,sizeof quad);
    pixels=render("neck skin reveal render");
    for(unsigned y=8;y<120;++y)for(unsigned x:{20u,100u})Require(pixels[y*128+x]!=0xFF2040C0,"face and neck skin both open the reveal");
    for(auto& v:f.skin)v.reveal=0;
    pixels=render("unweighted skin render");
    for(unsigned y=8;y<120;++y){Require(pixels[y*128+20]!=0xFF2040C0,"face reveal unchanged");Require(pixels[y*128+100]==0xFF2040C0,"torso skin stays closed");}
    printf("PASS: reveal follows head and neck skin weights on the face and the captured neck skin\n");
    Graphics::Release();rt->Release();ds->Release();
}
static void PreviewHead(HeadFrame& head) {
    FILE* file=nullptr;fopen_s(&file,"tests/output/reveal/headhuman.bin","rb");Require(file!=nullptr,"private head preview fixture");
    head.head=Transform::Identity();
    for(auto& v:head.vertices){Require(fread(&v.local,sizeof(Vec3),1,file)==1,"private head vertex");v.world=v.local;}
    Require(fread(head.indices,sizeof head.indices,1,file)==1&&fgetc(file)==EOF,"private head topology");fclose(file);
}
static void ApertureContract(IDirect3DDevice9* d) {
    Graphics::Release();IDirect3DSurface9 *rt=nullptr,*ds=nullptr;
    constexpr unsigned size=768;
    Hr(d->CreateRenderTarget(size,size,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt,nullptr));
    Hr(d->CreateDepthStencilSurface(size,size,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&ds,nullptr));
    Hr(d->SetDepthStencilSurface(nullptr));Hr(d->SetRenderTarget(0,rt));Hr(d->SetDepthStencilSurface(ds));
    auto c=Camera(size,false);const float view[16]={0,1,0,0,0,0,-1,0,1,0,0,0,0,-7,30,1};memcpy(c.view,view,sizeof view);
    c.projection[0]=c.projection[5]=1.0f/12;c.projection[10]=1.0f/60;
    Hr(d->SetViewport(&c.viewport));Hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xFF2040C0,.4f,0));
    HeadFrame head{};PreviewHead(head);auto m=Synthetic();
    Hr(d->BeginScene());Require(Graphics::Draw(d,m,c,head,2,{}),"aperture mask render");Hr(d->EndScene());
    auto pixels=Pixels(d,rt,size);Vec3 centers[3];FILE* file=nullptr;fopen_s(&file,"tests/output/reveal/apertures.bin","rb");
    Require(file&&fread(centers,sizeof centers,1,file)==1,"private aperture centres");fclose(file);
    for(auto center:centers) {
        int x=int((center.z/12+1)*size*.5f),y=int((1-(center.x-7)/12)*size*.5f);
        for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)
            Require(pixels[(y+dy)*size+x+dx]!=0xFF2040C0,"mouth and eye apertures remain covered when rear head fails world depth");
    }
    printf("PASS: mouth and both eye openings covered against foreground head depth\n");
    Graphics::Release();rt->Release();ds->Release();
}
static std::vector<DWORD> SavePreview(IDirect3DDevice9* d,const Model& m,unsigned mode,const char* path,const HeadFit* fit=nullptr,bool front=false,const Impact::Frame* impact=nullptr,float yaw=0,const Transform* neck=nullptr) {
    Graphics::Release();IDirect3DSurface9 *rt=nullptr,*ds=nullptr;
    constexpr unsigned size=768;
    Hr(d->CreateRenderTarget(size,size,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt,nullptr));
    Hr(d->CreateDepthStencilSurface(size,size,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&ds,nullptr));
    Hr(d->SetDepthStencilSurface(nullptr));Hr(d->SetRenderTarget(0,rt));Hr(d->SetDepthStencilSurface(ds));
    auto c=Camera(size,false);const float v[16]={0,1,0,0,1,0,0,0,0,0,-1,0,0,-7,30,1};memcpy(c.view,v,sizeof v);
    if(front){const float faceView[16]={0,1,0,0,0,0,-1,0,1,0,0,0,0,-7,30,1};memcpy(c.view,faceView,sizeof faceView);}
    if(yaw!=0) {
        Vec3 eye={7,30*sinf(yaw),30*cosf(yaw)},forward={0,-sinf(yaw),-cosf(yaw)};
        Vec3 right=Cross({1,0,0},forward),up={1,0,0};
        const float view[]={right.x,up.x,forward.x,0,right.y,up.y,forward.y,0,right.z,up.z,forward.z,0,-Dot(eye,right),-Dot(eye,up),-Dot(eye,forward),1};
        memcpy(c.view,view,sizeof view);
    }
    c.projection[0]=c.projection[5]=1.0f/12;c.projection[10]=1.0f/60;
    Hr(d->SetViewport(&c.viewport));Hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xFF20252A,1,0));
    HeadFrame f{};PreviewHead(f);
    if(fit)f.fit=*fit;
    if(neck)f.influences[1]=*neck;
    Hr(d->BeginScene());Require(Graphics::Draw(d,m,c,f,mode,{},impact),"asset preview");Hr(d->EndScene());
    auto pixels=Pixels(d,rt,size);FILE* file=nullptr;fopen_s(&file,path,"wb");Require(file!=nullptr,"preview file");
    fprintf(file,"P6\n%u %u\n255\n",size,size);
    unsigned changed=0;
    for(auto p:pixels){unsigned char rgb[]={static_cast<unsigned char>(p>>16),static_cast<unsigned char>(p>>8),static_cast<unsigned char>(p)};fwrite(rgb,1,3,file);changed+=p!=0xFF20252A;}
    fclose(file);Require(changed>(impact?50u:10000u),"anatomy produces visible pixels");
    printf("PASS: actual anatomy mode %u rendered (%u pixels)\n",mode,changed);
    Graphics::Release();rt->Release();ds->Release();
    return pixels;
}
static void ImpactPreviews(IDirect3DDevice9* d,const Model& model) {
    HeadFrame head{};PreviewHead(head);
    Impact::Sample sample;sample.point={10,5,1};sample.direction={0,-1,0};sample.valid=true;
    Impact::Path path;Require(Impact::Map(sample,head,path),"real head face/occiput impact mapping");
    printf("PASS: real head path (%.2f,%.2f,%.2f) -> (%.2f,%.2f,%.2f)\n",path.entry.x,path.entry.y,path.entry.z,path.exit.x,path.exit.y,path.exit.z);
    const float ages[]={0,.003f,.01f,.023f,.04f,.065f};
    std::vector<DWORD> previous;unsigned firstChanged=0,lastChanged=0;
    for(unsigned n=0;n<6;++n) {
        Impact::Frame frame{path,ages[n],5};char name[100];sprintf_s(name,"tests/output/impact-%u.ppm",n);
        auto pixels=SavePreview(d,model,2,name,nullptr,false,&frame);
        unsigned different=0,changed=0;
        for(size_t i=0;i<pixels.size();++i){changed+=pixels[i]!=0xFF20252A;if(!previous.empty())different+=pixels[i]!=previous[i];}
        if(n==0)firstChanged=changed;
        if(n>0)Require(different>20,"impact animation changes visible image over time");
        lastChanged=changed;previous=std::move(pixels);
    }
    Require(firstChanged<lastChanged/4,"reveal expands from localized entry to full head");
    Impact::Frame front{path,.015f,5};SavePreview(d,model,2,"tests/output/impact-front.ppm",nullptr,true,&front);
    printf("PASS: animated actual anatomy, expanding entry reveal, penetration and exit\n");
    const uint32_t ammo[]={0x7EA27,0x8ED03,0x8ECFF};unsigned gold[3]{};
    for(unsigned n=0;n<3;++n) {
        Impact::Frame frame{path,.023f,5,Ammunition::Resolve(ammo[n])};char name[100];
        sprintf_s(name,"tests/output/impact-ammo-%08X.ppm",ammo[n]);
        //anatomy the route never touches, so fragments cannot hide one calibre's bullet more than another's
        auto pixels=SavePreview(d,Synthetic(),2,name,nullptr,false,&frame);
        for(auto p:pixels) {
            unsigned r=(p>>16)&255,g=(p>>8)&255,b=p&255;
            gold[n]+=r>140&&g>75&&r>g*1.2f&&g>b*1.6f;
        }
        printf("ammo %08X: gold=%u diameter=%.3f channel=%.3f\n",ammo[n],gold[n],frame.bullet.radius*2,frame.bullet.Section()*2);
    }
    Require(gold[0]>20&&gold[1]>gold[0]&&gold[2]>gold[1]*2,"actual shader output distinguishes 22LR, 9mm and 50 MG dimensions");
}
static void ImpactViews(IDirect3DDevice9* d,const Model& model) {
    Graphics::Release();IDirect3DSurface9 *rt=nullptr,*ds=nullptr;
    constexpr unsigned size=1024;
    Hr(d->CreateRenderTarget(size,size,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&rt,nullptr));
    Hr(d->CreateDepthStencilSurface(size,size,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&ds,nullptr));
    Hr(d->SetDepthStencilSurface(nullptr));Hr(d->SetRenderTarget(0,rt));Hr(d->SetDepthStencilSurface(ds));
    HeadFrame head{};PreviewHead(head);
    Impact::Sample sample;sample.point={10,5,1};sample.direction={0,-1,0};sample.valid=true;
    Impact::Path path;Require(Impact::Map(sample,head,path),"perspective impact fixture");
    std::vector<DWORD> neutral[8];
    const float stages[]={.003f,.01f,.02f,.03f,.048f,1.2f};
    for(unsigned placement=0;placement<2;++placement)for(unsigned n=0;n<8;++n) {
        unsigned peakGold=0;
        for(unsigned stage=0;stage<6;++stage) {
            Impact::Frame impact{path,stages[stage],5};
            head.head=Transform::Identity();
            if(placement) {
                const float rotate[]={0,1,0,0,0,1,1,0,0};memcpy(head.head.r,rotate,sizeof rotate);
                head.head.t={-67820,3450,8500};head.head.scale=1.25f;
            }
            for(auto& vertex:head.vertices)vertex.world=head.head.Apply(vertex.local);
            float angle=float(n)*.7853981634f;
            Vec3 eye=head.head.Apply({7,50*sinf(angle),50*cosf(angle)}),forward=head.head.Apply({7,0,0})-eye;Normalize(forward);
            Vec3 right=Cross(head.head.Rotate({1,0,0}),forward);Normalize(right);Vec3 up=Cross(forward,right);
            auto c=Camera(size,false);
            if(placement)c.origin=eye;
            Vec3 relative=eye-c.origin;
            const float view[]={right.x,up.x,forward.x,0,right.y,up.y,forward.y,0,right.z,up.z,forward.z,0,
                -Dot(relative,right),-Dot(relative,up),-Dot(relative,forward),1};memcpy(c.view,view,sizeof view);
            c.projection[10]=1000.0f/999;c.projection[11]=1;c.projection[14]=-1000.0f/999;c.projection[15]=0;
            Hr(d->SetViewport(&c.viewport));Hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xFF20252A,1,0));
            Hr(d->BeginScene());Require(Graphics::Draw(d,model,c,head,2,{},&impact),"perspective impact view");Hr(d->EndScene());
            auto pixels=Pixels(d,rt,size);unsigned brain=0,gold=0;
            char name[100];sprintf_s(name,"tests/output/impact-view-%u-%u-%u.ppm",placement,n,stage);FILE* file=nullptr;fopen_s(&file,name,"wb");
            Require(file!=nullptr,"perspective preview file");fprintf(file,"P6\n%u %u\n255\n",size,size);
            for(auto p:pixels) {
                unsigned r=(p>>16)&255,g=(p>>8)&255,b=p&255;
                brain+=r>g*1.3f&&g>25&&r>b*1.2f;
                gold+=r>140&&g>75&&r>g*1.2f&&g>b*1.6f;
                unsigned char rgb[]={static_cast<unsigned char>(r),static_cast<unsigned char>(g),static_cast<unsigned char>(b)};fwrite(rgb,1,3,file);
            }
            fclose(file);printf("impact view %u/%u stage=%u: tissue=%u gold=%u\n",placement,n,stage,brain,gold);
            if(stage==2)Require(brain>1600,"brain remains exposed from both sides/front/back at gameplay head size");
            peakGold=gold>peakGold?gold:peakGold;
            if(!placement&&stage==5)neutral[n]=pixels;
            else if(placement&&stage==5) {
                unsigned different=0;
                for(size_t i=0;i<pixels.size();++i) {
                    unsigned a=pixels[i],b=neutral[n][i];
                    different+=abs(int((a>>16)&255)-int((b>>16)&255))>5||abs(int((a>>8)&255)-int((b>>8)&255))>5||abs(int(a&255)-int(b&255))>5;
                }
                printf("distant rotated/scaled head: %u pixels differ by more than 5/255\n",different);
                Require(different<320,"camera-relative origin and posed/scaled head preserve wounds after world-space debris expires");
            }
        }
        Require(peakGold>=3,"projectile is visible during traversal from every viewing angle");
    }
    Graphics::Release();rt->Release();ds->Release();
    printf("PASS: sixteen perspective impact views at five traversal times and after debris expiry, including distant rotated/scaled head\n");
}
static void WoundContract(IDirect3DDevice9* d,const Model& model) {
    Model brain;
    for(const auto& mesh:model.meshes)if(mesh.material==kBrainMaterial)brain.meshes.push_back(mesh);
    Require(!brain.meshes.empty(),"brain fixture");
    HeadFrame head{};PreviewHead(head);Impact::Sample sample;sample.valid=true;sample.point={10,5,1};sample.direction={0,-1,0};
    Impact::Path path;Require(Impact::Map(sample,head,path),"wound fixture route");
    Impact::Frame wounded{path,1.2f,5,Ammunition::Resolve(0x8ED03),500};
    Impact::Frame quiet{{{-10,5,1},{-10,-5,1},true},1.2f,5,Ammunition::Resolve(0x8ED03),501};
    auto baseline=SavePreview(d,brain,2,"tests/output/wound-uninjured.ppm",nullptr,false,&quiet);
    auto after=SavePreview(d,brain,2,"tests/output/wound-surface.ppm",nullptr,false,&wounded);
    auto torn=[](DWORD p){unsigned r=(p>>16)&255,g=(p>>8)&255;return r>40&&r>g*2.6f;};
    unsigned changed=0,tornAfter=0,tornBefore=0,distant=0,distantSame=0;
    for(size_t i=0;i<after.size();++i){changed+=after[i]!=baseline[i];tornAfter+=torn(after[i]);tornBefore+=torn(baseline[i]);}
    //the cerebellum lies well outside the channel's reach and must not change
    for(unsigned y=387;y<=407;++y)for(unsigned x=305;x<=325;++x){++distant;distantSame+=after[y*768+x]==baseline[y*768+x];}
    Require(changed>200&&tornAfter>tornBefore+100,"the track tears visible holes where it enters and leaves the brain");
    Require(distantSame==distant,"brain away from the track is untouched");
    printf("PASS: %u brain pixels change, %u torn-tissue pixels (uninjured %u), distant tissue unchanged\n",changed,tornAfter,tornBefore);
}
static void DestructionPreviews(IDirect3DDevice9* d,const Model& model) {
    HeadFrame head{};PreviewHead(head);Impact::Sample sample;sample.valid=true;sample.point={10,5,1};sample.direction={0,-1,0};
    Impact::Path path;Require(Impact::Map(sample,head,path),"destruction preview path");
    const float ages[]={0,.003f,.006f,.01f,.014f,.018f,.023f,.032f,.04f,.045f,.048f,.055f,.075f,.10f,.15f,.2f};
    for(unsigned n=0;n<sizeof ages/sizeof *ages;++n) {
        Impact::Frame impact{path,ages[n],5,Ammunition::Resolve(0x8ED03)};
        char name[100];sprintf_s(name,"tests/output/destruction-%02u.ppm",n);
        SavePreview(d,model,2,name,nullptr,false,&impact);
    }
    for(unsigned n=0;n<8;++n) {
        Impact::Frame impact{path,.048f,5,Ammunition::Resolve(0x8ED03)};
        char name[100];sprintf_s(name,"tests/output/destruction-angle-%u.ppm",n);
        SavePreview(d,model,2,name,nullptr,false,&impact,float(n)*.7853981634f);
    }
    printf("PASS: entry/crack/exit sequence and eight-angle destruction previews rendered\n");
    for(const auto& recorded:RecordedShots::shots) {
        Impact::Frame impact{recorded.path,.023f,5,Ammunition::Resolve(0x6B53D),recorded.sequence};
        char name[100];sprintf_s(name,"tests/output/recorded-route-%u.ppm",recorded.sequence);
        SavePreview(d,model,2,name,nullptr,false,&impact,.7853981634f);
        impact.age=.048f;sprintf_s(name,"tests/output/recorded-exit-%u.ppm",recorded.sequence);
        SavePreview(d,model,2,name,nullptr,false,&impact,.7853981634f);
    }
}
static void ExteriorContract(IDirect3DDevice9* d,const Model& model,D3DMULTISAMPLE_TYPE samples,bool reversed,bool settled=false) {
    Graphics::Release();IDirect3DSurface9 *rt=nullptr,*ds=nullptr,*resolved=nullptr;
    constexpr unsigned size=512;
    Hr(d->CreateRenderTarget(size,size,D3DFMT_A8R8G8B8,samples,0,FALSE,&rt,nullptr));
    Hr(d->CreateDepthStencilSurface(size,size,D3DFMT_D24S8,samples,0,FALSE,&ds,nullptr));
    if(samples!=D3DMULTISAMPLE_NONE)Hr(d->CreateRenderTarget(size,size,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
    Hr(d->SetDepthStencilSurface(nullptr));Hr(d->SetRenderTarget(0,rt));Hr(d->SetDepthStencilSurface(ds));
    auto camera=Camera(size,reversed);const float view[]={0,1,0,0,1,0,0,0,0,0,-1,0,0,-7,30,1};memcpy(camera.view,view,sizeof view);
    camera.projection[0]=camera.projection[5]=1.0f/12;camera.projection[10]=(reversed?-1.0f:1.0f)/60;
    HeadFrame head{};PreviewHead(head);Impact::Sample sample;sample.valid=true;sample.point={10,5,1};sample.direction={0,-1,0};
    Impact::Path path;Require(Impact::Map(sample,head,path),"exterior path");
    Impact::Frame impact{path,settled?1.2f:.048f,5,Ammunition::Resolve(0x8ED03)};
    std::vector<DWORD> baseline,visible;size_t outsidePixel=0;unsigned outside=0,gold=0;
    for(unsigned pass=0;pass<3;++pass) {
        Hr(d->SetViewport(&camera.viewport));Hr(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xFF20252A,reversed?0.0f:1.0f,0));
        Hr(d->BeginScene());
        if(pass==2) {
            Hr(d->SetRenderState(D3DRS_ZENABLE,TRUE));Hr(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
            Hr(d->SetRenderState(D3DRS_ZFUNC,reversed?D3DCMP_GREATEREQUAL:D3DCMP_LESSEQUAL));Hr(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            Quad(d,0,size/2,reversed?.9f:.1f,0xFF2040C0,size);
        }
        Require(Graphics::Draw(d,model,camera,head,2,{},pass?&impact:nullptr),"exterior rendering");
        if(pass)Require((Graphics::DamageStatus()&(Graphics::EntryBreak|Graphics::ExitBreak))==(Graphics::EntryBreak|Graphics::ExitBreak),"entry and exit breaks planned during exterior rendering");
        Hr(d->EndScene());
        if(resolved)Hr(d->StretchRect(rt,nullptr,resolved,nullptr,D3DTEXF_NONE));
        auto pixels=Pixels(d,resolved?resolved:rt,size);
        if(!pass)baseline=pixels;
        if(pass==1) {
            visible=pixels;
            for(size_t i=0;i<pixels.size();++i)if(baseline[i]==0xFF20252A&&pixels[i]!=baseline[i]) {
                ++outside;outsidePixel=i;unsigned p=pixels[i],r=(p>>16)&255,g=(p>>8)&255,b=p&255;
                gold+=r>140&&g>75&&r>g*1.2f&&g>b*1.6f;
            }
            printf("exterior: pixels=%u gold=%u MSAA=%u reversed=%u settled=%u\n",outside,gold,unsigned(samples),unsigned(reversed),unsigned(settled));
            Require(outside>100&&(settled||gold>5),settled?"everted exit plates remain visible beyond the skin after all debris expires":"bullet and fragments emerge beyond the head silhouette");
            Hr(d->BeginScene());Hr(d->SetRenderState(D3DRS_ZENABLE,TRUE));Hr(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
            Hr(d->SetRenderState(D3DRS_ZFUNC,reversed?D3DCMP_GREATEREQUAL:D3DCMP_LESSEQUAL));Hr(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            Quad(d,0,size,reversed?.1f:.9f,0xFF00FF00,size);Hr(d->EndScene());
            if(resolved)Hr(d->StretchRect(rt,nullptr,resolved,nullptr,D3DTEXF_NONE));
            auto after=Pixels(d,resolved?resolved:rt,size);
            Require(after[outsidePixel]==0xFF00FF00,"exterior effect does not write its depth into the game buffer");
        }
        if(pass==2) {
            unsigned checked=0;
            for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size/2-1;++x) {
                size_t i=y*size+x;
                if(baseline[i]==0xFF20252A&&visible[i]!=baseline[i]){++checked;Require(pixels[i]==0xFF2040C0,"foreground occludes exterior fragments and bullet");}
            }
            Require(checked>100,"foreground test crosses the actual exterior effect");
        }
    }
    Graphics::Release();if(resolved)resolved->Release();rt->Release();ds->Release();
    printf("PASS: exterior visibility, foreground occlusion and untouched world depth\n");
}
int main(int argc,char** argv) {
    bool capture=argc>=4&&!strcmp(argv[2],"--capture");
    Require(argc==2||argc==3||capture,"asset argument");Model model;Require(model.Load(argv[1]),"valid anatomy load");
    Model invalid;Require(!invalid.Load("missing.xra"),"missing model rejected");
    printf("PASS: loaded %u anatomy records\n",unsigned(model.meshes.size()));
    FILE* asset=nullptr;fopen_s(&asset,argv[1],"rb");Require(asset!=nullptr,"fixture read");
    fseek(asset,0,SEEK_END);long size=ftell(asset);rewind(asset);std::vector<unsigned char> data(size);
    Require(fread(data.data(),1,data.size(),asset)==data.size(),"fixture bytes");fclose(asset);
    auto reject=[&](const std::vector<unsigned char>& bytes,const char* name){
        FILE* bad=nullptr;fopen_s(&bad,"tests/output/invalid.xra","wb");Require(bad!=nullptr,"invalid fixture");
        fwrite(bytes.data(),1,bytes.size(),bad);fclose(bad);Require(!invalid.Load("tests/output/invalid.xra")&&invalid.meshes.empty(),name);
    };
    auto changed=data;changed[0]='?';reject(changed,"magic rejected");
    changed=data;changed.resize(changed.size()-1);reject(changed,"truncation rejected");
    changed=data;changed.push_back(0);reject(changed,"trailing bytes rejected");
    changed=data;uint32_t badMaterial=kMaterialCount;memcpy(changed.data()+16,&badMaterial,4);reject(changed,"invalid material rejected");
    changed=data;memset(changed.data()+24,255,4);reject(changed,"unbounded vertex count rejected");
    changed=data;uint32_t nan=0x7FC00000;memcpy(changed.data()+32,&nan,4);reject(changed,"nan position rejected");
    changed=data;uint32_t vertices=0;memcpy(&vertices,changed.data()+24,4);
    changed[32+vertices*sizeof(Vertex)]=255;changed[33+vertices*sizeof(Vertex)]=255;reject(changed,"out of range index rejected");
    printf("PASS: missing, corrupt, truncated, oversized, nonfinite and invalid-index assets rejected\n");
    HeadFrame reference{};PreviewHead(reference);
    HeadFit femaleFit;
    if(!capture) {
        femaleFit=TestFaceFit(model);
        TestImpact();
        TestFracture(model,reference);
        TestNeck(model);
        TestSkin();TestSkinBinds();TestOpenOver();TestCloseUpPose();TestCloseUpWalls();TestCloseUpFollow();
		Model torso;
		Require(torso.Load("mod\\meshes\\XRayKillcamNVSE\\torso.xra"), "valid torso anatomy load");
		TestRegionPlanning(torso,0);TestGroin(torso);TestRupture(torso);TestKillShot(torso);
		Model legs;
		Require(legs.Load("mod\\meshes\\XRayKillcamNVSE\\legs.xra"), "valid leg anatomy load");
		TestRegionPlanning(legs,1);TestRegionPlanning(legs,2);TestCarrier();
		Model arms;
		Require(arms.Load("mod\\meshes\\XRayKillcamNVSE\\arms.xra"), "valid arm anatomy load");
		TestRegionPlanning(arms,3);TestRegionPlanning(arms,4);
		Model plain;
		Require(plain.Load("mod\\meshes\\XRayKillcamNVSE\\torso-female.xra"), "torso without genitals for feral ghouls");
		TestRegionPlanning(model,5);TestRegionPlanning(plain,6);TestRegionPlanning(legs,7);TestRegionPlanning(arms,8);
        TestRegionPlanning(model,9);TestRegionPlanning(plain,10);TestRegionPlanning(legs,11);TestRegionPlanning(arms,12);
        const char* horseFiles[]={"horse-head","horse-torso","horse-legs","horse-arms"};
        for(unsigned region=0;region<kRegions;++region) {
			Model horse;
			char path[MAX_PATH];
			sprintf_s(path, "mod\\meshes\\XRayKillcamNVSE\\%s.xra", horseFiles[region]);
			Require(horse.Load(path),"valid horse anatomy load");TestRegionPlanning(horse,13+region);
        }
        TestAudit(reference);
    }
    if(argc==3&&!strcmp(argv[2],"--geometry"))return 0;
    WNDCLASSA wc{};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(nullptr);wc.lpszClassName="XRayRenderProbe";RegisterClassA(&wc);
    HWND window=CreateWindowA(wc.lpszClassName,"XRay render probe",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,wc.hInstance,nullptr);
    Require(window!=nullptr,"standalone test window");
    IDirect3D9* api=Direct3DCreate9(D3D_SDK_VERSION);Require(api!=nullptr,"D3D9 available");
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=128;pp.BackBufferHeight=128;
    IDirect3DDevice9* d=nullptr;Hr(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&d));
    if(capture) {
        float ages[16];unsigned count=0;bool close=false,bare=false,groin=false;float severity=1;
        for(int i=4;i<argc;++i) {
            if(!strcmp(argv[i],"--close"))close=true;
            else if(!strcmp(argv[i],"--bare"))bare=true;
            else if(!strcmp(argv[i],"--groin"))groin=true;
            else if(!strcmp(argv[i],"--kill"))severity=1.8f;
            else if(count<16)ages[count++]=float(atof(argv[i]));
        }
        ReplayCapture(d,model,argv[3],ages,count,close,bare,groin,severity);d->Release();api->Release();DestroyWindow(window);return 0;
    }
    TestAuditRender(d,model);
	Model torso;
	Require(torso.Load("mod\\meshes\\XRayKillcamNVSE\\torso.xra"), "valid torso anatomy load");
	TestTorso(d,torso);TestGroinRender(d,torso);TestKillRender(d,torso);TestBloodSeen(d,torso);TestRestoreRender(d,torso);
	Model legs;
	Require(legs.Load("mod\\meshes\\XRayKillcamNVSE\\legs.xra"), "valid leg anatomy load");
	TestRegionRender(d,legs,1);TestRegionRender(d,legs,2);
	Model arms;
	Require(arms.Load("mod\\meshes\\XRayKillcamNVSE\\arms.xra"), "valid arm anatomy load");
	TestRegionRender(d,arms,3);TestRegionRender(d,arms,4);
	Model plain;
	Require(plain.Load("mod\\meshes\\XRayKillcamNVSE\\torso-female.xra"), "torso without genitals for feral ghouls");
	TestRegionRender(d,model,5);TestRegionRender(d,plain,6);TestRegionRender(d,legs,7);TestRegionRender(d,arms,8);
    TestRegionRender(d,model,9);TestRegionRender(d,plain,10);TestRegionRender(d,legs,11);TestRegionRender(d,arms,12);
    const char* horseFiles[]={"horse-head","horse-torso","horse-legs","horse-arms"};
    for(unsigned region=0;region<kRegions;++region) {
		Model horse;
		char path[MAX_PATH];
		sprintf_s(path, "mod\\meshes\\XRayKillcamNVSE\\%s.xra", horseFiles[region]);
		Require(horse.Load(path),"valid horse anatomy load");TestRegionRender(d,horse,13+region);
    }
    TestFractureGPU(d,model,reference);
    TestGameplay(d,model,reference);
    if(argc==3){d->Release();api->Release();DestroyWindow(window);return 0;}
    Contract(d,D3DMULTISAMPLE_NONE,false);Contract(d,D3DMULTISAMPLE_NONE,true);
    Contract(d,D3DMULTISAMPLE_NONE,false,true);Contract(d,D3DMULTISAMPLE_NONE,true,true);
    RevealContract(d);
    NeckRevealContract(d);
    Closeups(d,D3DMULTISAMPLE_NONE,false,false);Closeups(d,D3DMULTISAMPLE_NONE,true,false);
    Closeups(d,D3DMULTISAMPLE_NONE,false,true);Closeups(d,D3DMULTISAMPLE_NONE,true,true);
    DWORD quality=0;
    if(SUCCEEDED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_4_SAMPLES,&quality))) {
        Contract(d,D3DMULTISAMPLE_4_SAMPLES,false);
        Contract(d,D3DMULTISAMPLE_4_SAMPLES,false,true);Contract(d,D3DMULTISAMPLE_4_SAMPLES,true,true);
        Closeups(d,D3DMULTISAMPLE_4_SAMPLES,false,false);Closeups(d,D3DMULTISAMPLE_4_SAMPLES,true,false);
    }
    SavePreview(d,model,1,"tests/output/intact.ppm");SavePreview(d,model,2,"tests/output/cutaway.ppm");
    SavePreview(d,model,2,"tests/output/front.ppm",nullptr,true);
    ApertureContract(d);
    ImpactPreviews(d,model);
    WoundContract(d,model);
    DestructionPreviews(d,model);
    ImpactViews(d,model);
    ExteriorContract(d,model,D3DMULTISAMPLE_NONE,false);ExteriorContract(d,model,D3DMULTISAMPLE_NONE,true);
    ExteriorContract(d,model,D3DMULTISAMPLE_NONE,false,true);ExteriorContract(d,model,D3DMULTISAMPLE_NONE,true,true);
    if(quality){ExteriorContract(d,model,D3DMULTISAMPLE_4_SAMPLES,false);ExteriorContract(d,model,D3DMULTISAMPLE_4_SAMPLES,true);}
    if(quality){ExteriorContract(d,model,D3DMULTISAMPLE_4_SAMPLES,false,true);ExteriorContract(d,model,D3DMULTISAMPLE_4_SAMPLES,true,true);}
    auto gpu=SavePreview(d,model,2,"tests/output/female-gpu.ppm",&femaleFit);
    auto warped=model;
    for(auto& m:warped.meshes)for(auto& v:m.vertices)v.position=Fit::Warp(femaleFit,v.position,&v.normal);
    HeadFit identity;identity.chin=femaleFit.chin;
    auto cpu=SavePreview(d,warped,2,"tests/output/female-cpu.ppm",&identity);
    unsigned different=0;
    for(size_t i=0;i<gpu.size();++i) {
        bool mismatch=false;
        for(unsigned channel=0;channel<3;++channel)mismatch|=abs(int((gpu[i]>>(channel*8))&255)-int((cpu[i]>>(channel*8))&255))>2;
        different+=mismatch;
    }
    printf("GPU/CPU fitting comparison: %u pixels differ by more than 2/255\n",different);
    Require(different<gpu.size()/1000,"GPU fit matches CPU positions and normals");
    Transform bend=TestRotation({0,0,1},-30,{0,0,0});
    gpu=SavePreview(d,model,2,"tests/output/neck-gpu.ppm",&femaleFit,false,nullptr,.9f,&bend);
    auto bent=model;Posing posing{femaleFit,1,{}};posing.influences[1]=bend;
    for(auto& m:bent.meshes)for(auto& v:m.vertices){v.position=posing.Place(v.position,v.bind,v.bones,&v.normal);v.bind=1;v.bones=0;}
    cpu=SavePreview(d,bent,2,"tests/output/neck-cpu.ppm",&identity,false,nullptr,.9f);
    different=0;
    for(size_t i=0;i<gpu.size();++i) {
        bool mismatch=false;
        for(unsigned channel=0;channel<3;++channel)mismatch|=abs(int((gpu[i]>>(channel*8))&255)-int((cpu[i]>>(channel*8))&255))>2;
        different+=mismatch;
    }
    printf("GPU/CPU neck blend comparison: %u pixels differ by more than 2/255\n",different);
    Require(different<gpu.size()/1000,"GPU neck blend matches CPU positions and normals");
    Hr(d->SetDepthStencilSurface(nullptr));IDirect3DSurface9* back=nullptr;Hr(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));Hr(d->SetRenderTarget(0,back));back->Release();
    Hr(d->Reset(&pp));Contract(d,D3DMULTISAMPLE_NONE,false);printf("PASS: device reset and recreation\n");
    Graphics::Release();d->Release();api->Release();DestroyWindow(window);return 0;
}
