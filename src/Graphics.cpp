#include "Graphics.h"
#include "RevealMask.h"
#include "Damage.h"
#include "Proportions.h"
#include <cstring>
#include "MaskVS.h"
#include "MaskPS.h"
#include "AnatomyVS.h"
#include "PlateVS.h"
#include "DebrisVS.h"
#include "SprayVS.h"
#include "AnatomyPS.h"
#include "AnatomyExteriorPS.h"
#include "CompositeVS.h"
#include "CompositePS.h"
#include "BulletVS.h"
#include "BulletPS.h"
#include "DamagePS.h"
#include "ExteriorPS.h"
#include "SprayThroughPS.h"

namespace XRay::Graphics {
static_assert(kFitAnchors==9&&kFitCoefficients==13);
template<class T> static void Drop(T*& p) {if(p){p->Release();p=nullptr;}}
struct GpuMesh {IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;};
//rows: bone seeds, soft seeds, breaks, then the bone seeds past the first row
constexpr unsigned kTableWidth=256,kTableRows=4;
constexpr float kBodyRevealLow=.35f,kBodyRevealHigh=.75f;
static_assert(Damage::kSeedRow<=kTableWidth&&Damage::kSceneBoneSeeds<=2*Damage::kSeedRow&&Damage::kSceneBrainSeeds<=kTableWidth&&Damage::kSceneClusters<=kTableWidth);
static_assert(Damage::kPlates==64&&Damage::kBurstPieces==64,"shader piece constants hold 64 rigid transforms");
static IDirect3DDevice9* s_device=nullptr;
static IDirect3DStateBlock9* s_state=nullptr;
static IDirect3DVertexDeclaration9 *s_maskDecl=nullptr,*s_anatomyDecl=nullptr,*s_quadDecl=nullptr,*s_pieceDecl=nullptr;
static IDirect3DVertexShader9 *s_maskVS=nullptr,*s_anatomyVS=nullptr,*s_compositeVS=nullptr,*s_bulletVS=nullptr,*s_plateVS=nullptr,*s_debrisVS=nullptr,*s_sprayVS=nullptr;
static IDirect3DPixelShader9 *s_maskPS=nullptr,*s_anatomyPS=nullptr,*s_compositePS=nullptr,*s_bulletPS=nullptr;
static IDirect3DPixelShader9 *s_damagePS=nullptr,*s_exteriorPS=nullptr;
static IDirect3DPixelShader9 *s_anatomyExteriorPS=nullptr,*s_sprayThroughPS=nullptr;
//blood pixels drawn outside the window and inside it, counted by occlusion queries when the device has them
static IDirect3DQuery9* s_bloodQueries[2]{};static bool s_bloodPending[2]{};
struct BloodSample {uint64_t sequence=0;unsigned samples=1;float distance=-1,angle=0,dropPixels=0;DWORD pixels[2]{};bool answered[2]{};};
static BloodSample s_bloodSample;
static BloodReport s_bloodShot,s_bloodDone;static bool s_bloodReady=false;
static_assert(Damage::kJetKinds==4,"the blood report counts four jet kinds");
static Damage::Scene s_scenes[kRegions];
static unsigned s_region=HeadRegion;
static const Model* s_models[kRegions]{};
static IDirect3DTexture9 *s_mask=nullptr,*s_anatomy=nullptr,*s_exterior=nullptr,*s_table=nullptr;
static IDirect3DSurface9 *s_maskSurface=nullptr,*s_multisample=nullptr,*s_anatomySurface=nullptr,*s_depth=nullptr,*s_exteriorSurface=nullptr;
static IDirect3DVertexBuffer9 *s_plates=nullptr,*s_debris=nullptr,*s_droplets=nullptr;
static bool s_bloodSpray=true;
static float s_severity=1,s_blood=1;
static uint32_t s_plateRevision=0,s_debrisRevision=0;static unsigned s_plateRegion=kRegions,s_debrisRegion=kRegions;
static GpuMesh s_meshes[kRegions][128];
static D3DSURFACE_DESC s_desc{};
static DWORD s_targetCount=0;
static HRESULT s_error=S_OK;
static bool Check(HRESULT hr) {if(FAILED(hr)){s_error=hr;return false;}return true;}
HRESULT LastError(){return s_error;}
unsigned DamageStatus(){return s_scenes[s_region].Status();}
DamageReport DamageSummary(){return s_scenes[s_region].Report();}
unsigned TakeCues(uint8_t* out,unsigned capacity){return s_scenes[s_region].TakeCues(out,capacity);}
bool TakeBlood(BloodReport& out){if(!s_bloodReady)return false;out=s_bloodDone;s_bloodReady=false;return true;}
BloodReport BloodInFlight(){return s_bloodShot;}
//a restore waits for the draw, after the region's anatomy is prepared, since preparing clears the scene
static std::vector<Damage::Kept> s_restore[kRegions];static bool s_restoring[kRegions]{};
uint32_t WoundRevision(unsigned region){return region<kRegions?s_scenes[region].WoundRevision():0;}
void KeepWounds(unsigned region,std::vector<Damage::Kept>& out){if(region<kRegions)s_scenes[region].Keep(out);}
void RestoreWounds(unsigned region,const std::vector<Damage::Kept>& kept){if(region<kRegions){s_restore[region]=kept;s_restoring[region]=true;}}
void ClearDamage(){for(auto& scene:s_scenes)scene.Clear();}
void ClearWounds(){for(auto& scene:s_scenes)scene.ClearWounds();}
void SetAsyncDamage(bool async){for(auto& scene:s_scenes)scene.SetAsync(async);}
void SetBloodSpray(bool enabled){s_bloodSpray=enabled;}
void SetShotForce(float severity,float blood){s_severity=severity;s_blood=blood;}

void Release() {
    Drop(s_state);Drop(s_maskDecl);Drop(s_anatomyDecl);Drop(s_quadDecl);Drop(s_pieceDecl);
    Drop(s_maskVS);Drop(s_anatomyVS);Drop(s_compositeVS);Drop(s_maskPS);Drop(s_anatomyPS);Drop(s_compositePS);
    Drop(s_bulletVS);Drop(s_bulletPS);Drop(s_plateVS);Drop(s_debrisVS);Drop(s_sprayVS);
    Drop(s_damagePS);Drop(s_exteriorPS);for(auto& scene:s_scenes)scene.Clear();
    Drop(s_anatomyExteriorPS);Drop(s_sprayThroughPS);
    for(auto& q:s_bloodQueries)Drop(q);s_bloodPending[0]=s_bloodPending[1]=false;s_bloodSample.answered[0]=s_bloodSample.answered[1]=false;
    Drop(s_maskSurface);Drop(s_multisample);Drop(s_anatomySurface);Drop(s_depth);Drop(s_mask);Drop(s_anatomy);Drop(s_exteriorSurface);Drop(s_exterior);
    Drop(s_table);Drop(s_plates);Drop(s_debris);Drop(s_droplets);s_plateRevision=s_debrisRevision=0;s_plateRegion=s_debrisRegion=kRegions;
    for(auto& region:s_meshes)for(auto& m:region){Drop(m.vb);Drop(m.ib);}
    for(auto& model:s_models)model=nullptr;
    s_device=nullptr;s_desc={};s_targetCount=0;
}

static bool Create(IDirect3DDevice9* d,const D3DSURFACE_DESC& desc) {
    Release();
    D3DCAPS9 caps{};
    if(!Check(d->GetDeviceCaps(&caps)))return false;
    if(caps.VertexShaderVersion<D3DVS_VERSION(3,0)||caps.PixelShaderVersion<D3DPS_VERSION(3,0)
        ||!(caps.PrimitiveMiscCaps&D3DPMISCCAPS_BLENDOP)||!(caps.Caps2&D3DCAPS2_DYNAMICTEXTURES))return Check(D3DERR_NOTAVAILABLE);
    s_targetCount=caps.NumSimultaneousRTs<4?caps.NumSimultaneousRTs:4;
    const D3DVERTEXELEMENT9 mask[]={
        {0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,0},
        {0,24,D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,1},D3DDECL_END()};
    const D3DVERTEXELEMENT9 anatomy[]={
        {0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
        {0,24,D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,0},{0,28,D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,1},
        {0,32,D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,2},D3DDECL_END()};
    const D3DVERTEXELEMENT9 piece[]={
        {0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
        {0,24,D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,0},{0,28,D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,1},
        {0,32,D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,2},{0,36,D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,3},D3DDECL_END()};
    const D3DVERTEXELEMENT9 quad[]={
        {0,0,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_POSITION,0},{0,8,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    if(!Check(d->CreateStateBlock(D3DSBT_ALL,&s_state))||!Check(d->CreateVertexDeclaration(mask,&s_maskDecl))
        ||!Check(d->CreateVertexDeclaration(anatomy,&s_anatomyDecl))||!Check(d->CreateVertexDeclaration(quad,&s_quadDecl))
        ||!Check(d->CreateVertexDeclaration(piece,&s_pieceDecl))
        ||!Check(d->CreateVertexShader((const DWORD*)g_MaskVS,&s_maskVS))||!Check(d->CreatePixelShader((const DWORD*)g_MaskPS,&s_maskPS))
        ||!Check(d->CreateVertexShader((const DWORD*)g_AnatomyVS,&s_anatomyVS))||!Check(d->CreatePixelShader((const DWORD*)g_AnatomyPS,&s_anatomyPS))
        ||!Check(d->CreateVertexShader((const DWORD*)g_PlateVS,&s_plateVS))||!Check(d->CreateVertexShader((const DWORD*)g_DebrisVS,&s_debrisVS))
        ||!Check(d->CreateVertexShader((const DWORD*)g_SprayVS,&s_sprayVS))
        ||!Check(d->CreateVertexShader((const DWORD*)g_CompositeVS,&s_compositeVS))||!Check(d->CreatePixelShader((const DWORD*)g_CompositePS,&s_compositePS))
        ||!Check(d->CreateVertexShader((const DWORD*)g_BulletVS,&s_bulletVS))||!Check(d->CreatePixelShader((const DWORD*)g_BulletPS,&s_bulletPS))
        ||!Check(d->CreatePixelShader((const DWORD*)g_DamagePS,&s_damagePS))||!Check(d->CreatePixelShader((const DWORD*)g_ExteriorPS,&s_exteriorPS))
        ||!Check(d->CreatePixelShader((const DWORD*)g_AnatomyExteriorPS,&s_anatomyExteriorPS))
        ||!Check(d->CreatePixelShader((const DWORD*)g_SprayThroughPS,&s_sprayThroughPS))) return false;
    for(auto& q:s_bloodQueries)if(FAILED(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&q)))q=nullptr;
    if(!Check(d->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&s_mask,nullptr))
        ||!Check(s_mask->GetSurfaceLevel(0,&s_maskSurface))
        ||!Check(d->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&s_anatomy,nullptr))
        ||!Check(s_anatomy->GetSurfaceLevel(0,&s_anatomySurface))
        ||!Check(d->CreateDepthStencilSurface(desc.Width,desc.Height,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&s_depth,nullptr)))return false;
    if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE && !Check(d->CreateRenderTarget(desc.Width,desc.Height,D3DFMT_A8R8G8B8,
        desc.MultiSampleType,desc.MultiSampleQuality,FALSE,&s_multisample,nullptr)))return false;
    if(!Check(d->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&s_exterior,nullptr))
        ||!Check(s_exterior->GetSurfaceLevel(0,&s_exteriorSurface)))return false;
    if(!Check(d->CreateTexture(kTableWidth,kTableRows,1,D3DUSAGE_DYNAMIC,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&s_table,nullptr))
        ||!Check(d->CreateVertexBuffer(Damage::kPlateVertices*sizeof(Damage::PieceVertex),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&s_plates,nullptr))
        ||!Check(d->CreateVertexBuffer(Damage::kBursts*Damage::kBurstVertices*sizeof(Damage::PieceVertex),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&s_debris,nullptr)))return false;
    //every droplet's shape is fixed, the vertex shader flies it from its jet's constants
    constexpr unsigned kDropletBytes=Damage::kDroplets*Damage::kDropletTriangles*3*sizeof(Damage::PieceVertex);void* droplets=nullptr;
    if(!Check(d->CreateVertexBuffer(kDropletBytes,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&s_droplets,nullptr))||!Check(s_droplets->Lock(0,kDropletBytes,&droplets,0)))return false;
    Damage::Droplets(static_cast<Damage::PieceVertex*>(droplets));
    if(!Check(s_droplets->Unlock()))return false;
    s_desc=desc;s_device=d;return true;
}
//each region's anatomy is uploaded once per device, the first time it is drawn
static bool Prepare(IDirect3DDevice9* d,const Model& model,unsigned region) {
    if(s_models[region]==&model)return true;
    for(auto& g:s_meshes[region]){Drop(g.vb);Drop(g.ib);}
    for(size_t i=0;i<model.meshes.size();++i) {
        const auto& m=model.meshes[i];auto& g=s_meshes[region][i];void* p=nullptr;
        UINT vs=static_cast<UINT>(m.vertices.size()*sizeof(Vertex)),is=static_cast<UINT>(m.indices.size()*sizeof(uint16_t));
        if(!Check(d->CreateVertexBuffer(vs,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&g.vb,nullptr))
            ||!Check(d->CreateIndexBuffer(is,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&g.ib,nullptr))
            ||!Check(g.vb->Lock(0,vs,&p,0))) return false;
        memcpy(p,m.vertices.data(),vs);if(!Check(g.vb->Unlock())||!Check(g.ib->Lock(0,is,&p,0)))return false;
        memcpy(p,m.indices.data(),is);if(!Check(g.ib->Unlock()))return false;
    }
    s_scenes[region].Reserve(model);s_models[region]=&model;
    return true;
}

struct SavedTargets {
    IDirect3DDevice9* device;
    IDirect3DSurface9* targets[4]{};
    IDirect3DSurface9* depth=nullptr;
    unsigned count=0;bool captured=false;
    explicit SavedTargets(IDirect3DDevice9* d):device(d){}
    bool Capture() {
        count=s_targetCount;
        if(!Check(s_state->Capture()))return false;
        for(unsigned i=0;i<count;++i) {
            HRESULT hr=device->GetRenderTarget(i,&targets[i]);
            if(FAILED(hr) && (i==0||hr!=D3DERR_NOTFOUND)){Check(hr);return false;}
        }
        if(!Check(device->GetDepthStencilSurface(&depth)))return false;
        captured=true;return true;
    }
    ~SavedTargets() {
        if(captured) {
            for(DWORD s=0;s<4;++s)device->SetTexture(s,nullptr);
            device->SetDepthStencilSurface(nullptr);
            for(unsigned i=1;i<count;++i)device->SetRenderTarget(i,nullptr);
            device->SetRenderTarget(0,targets[0]);
            for(unsigned i=1;i<count;++i)device->SetRenderTarget(i,targets[i]);
            device->SetDepthStencilSurface(depth);
            //setrendertarget resets the viewport, so the captured state is applied last
            s_state->Apply();
        }
        for(auto& t:targets)Drop(t);Drop(depth);
    }
};

static bool Common(IDirect3DDevice9* d,const CameraFrame& c) {
    const struct {D3DRENDERSTATETYPE state;DWORD value;} settings[]={
        {D3DRS_ZENABLE,TRUE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ZFUNC,static_cast<DWORD>(c.Reversed()?D3DCMP_GREATEREQUAL:D3DCMP_LESSEQUAL)},
        {D3DRS_STENCILENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},
        {D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_SCISSORTESTENABLE,FALSE},
        {D3DRS_COLORWRITEENABLE,15},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},
        {D3DRS_CLIPPLANEENABLE,0},{D3DRS_CLIPPING,TRUE},{D3DRS_MULTISAMPLEANTIALIAS,TRUE},{D3DRS_MULTISAMPLEMASK,0xFFFFFFFF},
        {D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0}
    };
    for(auto s:settings)if(!Check(d->SetRenderState(s.state,s.value)))return false;
    if(!Check(d->SetStreamSourceFreq(0,1)))return false;
    for(unsigned s=0;s<4;++s) {
        if(!Check(d->SetTexture(s,nullptr)))return false;
        const struct {D3DSAMPLERSTATETYPE type;DWORD value;} sampler[]={
            {D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},
            {D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE},
            {D3DSAMP_MAXMIPLEVEL,0},{D3DSAMP_MIPMAPLODBIAS,0}};
        for(auto a:sampler)if(!Check(d->SetSamplerState(s,a.type,a.value)))return false;
    }
    float origin[4]={c.origin.x,c.origin.y,c.origin.z,0};
    return Check(d->SetVertexShaderConstantF(0,c.view,4))&&Check(d->SetVertexShaderConstantF(4,c.projection,4))
        &&Check(d->SetVertexShaderConstantF(8,origin,1));
}
static bool Upload(IDirect3DVertexBuffer9* vb,const Damage::PieceVertex* source,unsigned count) {
    if(!count)return true;
    void* p=nullptr;
    if(!Check(vb->Lock(0,count*sizeof(Damage::PieceVertex),&p,D3DLOCK_DISCARD)))return false;
    memcpy(p,source,count*sizeof(Damage::PieceVertex));return Check(vb->Unlock());
}
static bool FillTable(const Damage::Scene& s_damage) {
    D3DLOCKED_RECT r;
    if(!Check(s_table->LockRect(0,&r,nullptr,D3DLOCK_DISCARD)))return false;
    auto row=[&](unsigned y){return reinterpret_cast<float*>(static_cast<char*>(r.pBits)+y*r.Pitch);};
    memset(row(0),0,kTableWidth*16);memset(row(1),0,kTableWidth*16);memset(row(2),0,kTableWidth*16);memset(row(3),0,kTableWidth*16);
    unsigned bone=s_damage.ActiveSeeds(Damage::BoneSet),first=bone<Damage::kSeedRow?bone:Damage::kSeedRow;
    memcpy(row(0),s_damage.SeedConstants(Damage::BoneSet),first*16);
    memcpy(row(3),s_damage.SeedConstants(Damage::BoneSet)+first*4,(bone-first)*16);
    memcpy(row(1),s_damage.SeedConstants(Damage::BrainSet),s_damage.ActiveSeeds(Damage::BrainSet)*16);
    memcpy(row(2),s_damage.ClusterConstants(),s_damage.ActiveClusters()*16);
    return Check(s_table->UnlockRect(0));
}
//a frame's counts are folded into its shot once both have come back, a frame or more after they were asked
static void CollectBlood() {
    for(unsigned k=0;k<2;++k) {
        if(!s_bloodPending[k])continue;
        DWORD pixels=0;HRESULT hr=s_bloodQueries[k]->GetData(&pixels,sizeof pixels,0);
        if(hr==S_FALSE)continue;
        s_bloodPending[k]=false;s_bloodSample.pixels[k]=hr==S_OK?pixels:0;s_bloodSample.answered[k]=hr==S_OK;
    }
    auto& sample=s_bloodSample;auto& shot=s_bloodShot;
    if(s_bloodPending[0]||s_bloodPending[1]||!(sample.answered[0]||sample.answered[1]))return;
    if(sample.sequence==shot.sequence) {
        unsigned outside=sample.pixels[0]/sample.samples,inside=sample.pixels[1];++shot.measured;
        if(shot.distance<0||outside+inside>shot.outside+shot.inside) {
            shot.outside=outside;shot.inside=inside;shot.distance=sample.distance;shot.angle=sample.angle;shot.dropPixels=sample.dropPixels;
        }
    }
    sample.answered[0]=sample.answered[1]=false;
}
//a new shot hands the last one over, and a shot hands itself over once its blood has landed and been counted
static void TrackBlood(uint64_t sequence,const Damage::Spray& spray,bool flying) {
    auto& shot=s_bloodShot;
    if(sequence&&sequence!=shot.sequence) {
        if(shot.frames){s_bloodDone=shot;s_bloodReady=true;}
        shot={};shot.sequence=sequence;
    }
    if(!shot.sequence)return;
    shot.counted=s_bloodQueries[0]&&s_bloodQueries[1];
    if(flying) {
        unsigned jets[Damage::kJetKinds],drops[Damage::kJetKinds];spray.Tally(shot.sequence,jets,drops);++shot.frames;
        for(unsigned k=0;k<Damage::kJetKinds;++k){shot.jets[k]=jets[k]>shot.jets[k]?jets[k]:shot.jets[k];shot.droplets[k]=drops[k]>shot.droplets[k]?drops[k]:shot.droplets[k];}
    }else if(shot.frames&&!s_bloodPending[0]&&!s_bloodPending[1]) {
        s_bloodDone=shot;s_bloodReady=true;shot={};shot.sequence=sequence?sequence:s_bloodDone.sequence;
    }
}
struct MaterialStyle {float color[4],cut[4];};
//cut colours show sawn diploe, pulped brain, dentin and torn organs where a cut exposes back faces
static const MaterialStyle kStyles[kMaterialCount]={
    {{.82f,.74f,.57f,24},{.6f,.34f,.22f,10}},{{.68f,.37f,.32f,48},{.4f,.06f,.05f,40}},
    {{.48f,.29f,.14f,12},{.6f,.34f,.22f,10}},{{.92f,.86f,.70f,64},{.86f,.76f,.56f,20}},
    {{.86f,.76f,.72f,40},{.7f,.5f,.46f,16}},{{.55f,.13f,.11f,36},{.38f,.05f,.04f,30}},{{.82f,.52f,.5f,20},{.62f,.24f,.22f,16}},
    {{.42f,.12f,.08f,40},{.3f,.05f,.03f,30}},{{.84f,.6f,.5f,30},{.6f,.28f,.22f,20}},{{.52f,.2f,.14f,40},{.35f,.07f,.05f,30}},
    {{.86f,.76f,.7f,36},{.66f,.38f,.34f,20}},{{.6f,.22f,.22f,32},{.42f,.08f,.08f,26}}};
//fresh arterial red, darker where a droplet is seen through
static const MaterialStyle kBlood={{.62f,.035f,.025f,70},{.3f,.02f,.012f,40}};

bool Draw(IDirect3DDevice9* d,const Model& model,const CameraFrame& c,const HeadFrame& head,unsigned mode,const Appearance& style,const Impact::Frame* impact) {
    s_error=S_OK;
    if(!d||model.meshes.empty()||model.meshes.size()>128||!c.viewport.Width||!c.viewport.Height)return false;
    IDirect3DSurface9 *target=nullptr,*depth=nullptr;D3DSURFACE_DESC desc{},ds{};
    if(!Check(d->GetRenderTarget(0,&target)))return false;
    HRESULT hr=target->GetDesc(&desc);Drop(target);
    if(!Check(hr)||!Check(d->GetDepthStencilSurface(&depth)))return false;
    hr=depth->GetDesc(&ds);Drop(depth);
    if(!Check(hr)||desc.MultiSampleType!=ds.MultiSampleType||desc.MultiSampleQuality!=ds.MultiSampleQuality
        ||desc.Width>ds.Width||desc.Height>ds.Height||c.viewport.X+c.viewport.Width>desc.Width||c.viewport.Y+c.viewport.Height>desc.Height)return false;
    if(s_device!=d||s_desc.Width!=desc.Width||s_desc.Height!=desc.Height||s_desc.MultiSampleType!=desc.MultiSampleType||s_desc.MultiSampleQuality!=desc.MultiSampleQuality) {
        if(!Create(d,desc)){Release();return false;}
    }
    unsigned region=head.region<kRegions?head.region:HeadRegion;
    if(!Prepare(d,model,region)){Release();return false;}
    s_region=region;auto& s_damage=s_scenes[region];auto& meshes=s_meshes[region];
    if(s_restoring[region]){s_restoring[region]=false;s_damage.Restore(s_restore[region]);s_restore[region].clear();}
    SavedTargets saved(d);if(!saved.Capture())return false;
    if(!Common(d,c))return false;
    bool validImpact=impact&&impact->path.valid&&Finite(impact->path.entry)&&Finite(impact->path.exit)
        &&std::isfinite(impact->age)&&impact->age>=0;
    if(validImpact) {
        float length=Length(impact->path.exit-impact->path.entry);
        validImpact=length>=.5f&&length<=Impact::kLongestPath*BodySize(head);
    }
    if(validImpact)s_damage.Update(model,head,style,*impact,s_severity,s_blood);
    else if(impact)s_damage.Advance(head,style,*impact);
    else s_damage.Clear();
    //an unmapped follow-up keeps showing the wounds of earlier hits on this head
    Impact::Frame settled{};const Impact::Frame* shot=validImpact?impact:nullptr;
    if(!shot&&impact&&s_damage.Wounded()) {
        settled.path=*s_damage.LatestPath();settled.age=60;settled.bullet=s_damage.LatestBullet();shot=&settled;
    }
    float constants[20]{};
    if(shot) {
        Vec3 direction=shot->path.exit-shot->path.entry;float length=Length(direction);Normalize(direction);
        float reveal=Clamp(shot->age/.03f,0,1);reveal=reveal*reveal*(3-2*reveal);
        const auto entry=shot->path.entry;
        const float values[12]={entry.x,entry.y,entry.z,shot->bullet.Bore(),direction.x,direction.y,direction.z,length,
            Impact::Progress(*shot),1.5f+48*reveal,1,shot->age};
        memcpy(constants,values,sizeof values);
        Vec3 sectionNormal=Impact::SectionNormal(*shot,head,c);
        constants[12]=sectionNormal.x;constants[13]=sectionNormal.y;constants[14]=sectionNormal.z;
        constants[15]=Length(sectionNormal)>.5f?shot->bullet.Section():0;
        Vec3 offset={style.up,style.forward,style.side};
        Vec3 centre=Fit::Warp(head.fit,{10,0,0})*style.scale+offset;
        constants[16]=centre.x;constants[17]=centre.y;constants[18]=centre.z;
        //only the skull opens a window above the wound
        constants[19]=region==HeadRegion?(Fit::Warp(head.fit,{7.5f,0,0})*style.scale+offset).x:1e6f;
    }
    const float bullet[4]={shot?shot->bullet.length:0,0,0,0};
    const float counts[4]={float(s_damage.ActiveSeeds(Damage::BoneSet)),float(s_damage.ActiveSeeds(Damage::BrainSet)),float(s_damage.ActiveClusters()),0};
    //a ps_3_0 loop runs at most 255 times, so the bone seeds past the first row have their own loop
    int bones=int(s_damage.ActiveSeeds(Damage::BoneSet)),firstRow=bones<int(Damage::kSeedRow)?bones:int(Damage::kSeedRow);
    const int loops[16]={firstRow,0,1,0,int(s_damage.ActiveSeeds(Damage::BrainSet)),0,1,0,int(s_damage.ActiveClusters()),0,1,0,bones-firstRow,0,1,0};
    //one pixel spans depth*2/(width*p00) world units, anatomy units divide out head and style scale
    float span=fabsf(c.projection[0])*float(c.viewport.Width)*head.head.scale*fmaxf(style.scale,.01f);
    const float pixel[4]={span>0?2/span:0,0,0,0};
    if(!Check(d->SetPixelShaderConstantF(6,constants,5))||!Check(d->SetPixelShaderConstantF(13,bullet,1))
        ||!Check(d->SetPixelShaderConstantF(14,counts,1))||!Check(d->SetPixelShaderConstantF(15,pixel,1))||!Check(d->SetPixelShaderConstantI(0,loops,4))||!FillTable(s_damage)
        ||!Check(d->SetTexture(3,s_table)))return false;
    if(s_plateRevision!=s_damage.PlateRevision()||s_plateRegion!=region) {
        unsigned plates=0;for(unsigned kind=0;kind<kFractureKinds;++kind)plates+=s_damage.PlatePart(kind).count;
        if(!Upload(s_plates,s_damage.PlateVertices().data(),plates))return false;
        s_plateRevision=s_damage.PlateRevision();s_plateRegion=region;
    }
    const auto& fragments=s_damage.Fragments();
    if(s_debrisRevision!=fragments.Revision()||s_debrisRegion!=region) {
        //discarding leaves the whole buffer undefined, each burst's used span is written again
        void* p=nullptr;
        if(!Check(s_debris->Lock(0,Damage::kBursts*Damage::kBurstVertices*sizeof(Damage::PieceVertex),&p,D3DLOCK_DISCARD)))return false;
        for(unsigned burst=0;burst<Damage::kBursts;++burst) {
            auto used=fragments.Used(burst);
            memcpy(static_cast<Damage::PieceVertex*>(p)+used.first,fragments.Vertices().data()+used.first,used.count*sizeof(Damage::PieceVertex));
        }
        if(!Check(s_debris->Unlock()))return false;
        s_debrisRevision=fragments.Revision();s_debrisRegion=region;
    }
    const auto& h=head.head;
    float headConstants[24]={h.r[0],h.r[1],h.r[2],0,h.r[3],h.r[4],h.r[5],0,h.r[6],h.r[7],h.r[8],0,
        h.t.x,h.t.y,h.t.z,h.scale,style.scale,style.scale,style.scale,0,style.up,style.forward,style.side,0};
    float influenceRows[kInfluences*12];
    for(unsigned k=0;k<kInfluences;++k) {
        const auto& n=head.influences[k];
        const float rows[12]={n.r[0]*n.scale,n.r[1]*n.scale,n.r[2]*n.scale,n.t.x,n.r[3]*n.scale,n.r[4]*n.scale,n.r[5]*n.scale,n.t.y,
            n.r[6]*n.scale,n.r[7]*n.scale,n.r[8]*n.scale,n.t.z};
        memcpy(influenceRows+k*12,rows,sizeof rows);
    }
    if(!Check(d->SetVertexShaderConstantF(9,headConstants,6))||!Check(d->SetVertexShaderConstantF(229,influenceRows,kInfluences*3)))return false;
    float clock=s_damage.SceneClock();
    auto useMaterial=[&](unsigned material,bool fragment) {
        const float surface[4]={FractureKind(material)!=kNoFracture||material==kBoneInteriorMaterial?1.0f:0.0f,
            SoftKind(material)!=kNoFracture?1.0f:0.0f,fragment?1.0f:0.0f,0};
        return Check(d->SetPixelShaderConstantF(0,kStyles[material].color,1))&&Check(d->SetPixelShaderConstantF(11,surface,1))
            &&Check(d->SetPixelShaderConstantF(12,kStyles[material].cut,1));
    };
    auto drawPlates=[&]() {
        if(!s_damage.Plates())return true;
        if(!Check(d->SetVertexDeclaration(s_pieceDecl))||!Check(d->SetVertexShader(s_plateVS))
            ||!Check(d->SetVertexShaderConstantF(37,s_damage.PlateRows(),s_damage.Plates()*3))
            ||!Check(d->SetStreamSource(0,s_plates,0,sizeof(Damage::PieceVertex))))return false;
        for(unsigned kind=0;kind<kFractureKinds;++kind) {
            auto range=s_damage.PlatePart(kind);
            if(range.count&&(!useMaterial(kFractureMaterials[kind],true)||!Check(d->DrawPrimitive(D3DPT_TRIANGLELIST,range.first,range.count/3))))return false;
        }
        return true;
    };
    auto drawDebris=[&](bool exterior) {
        if(!Check(d->SetVertexDeclaration(s_pieceDecl))||!Check(d->SetVertexShader(s_debrisVS))
            ||!Check(d->SetStreamSource(0,s_debris,0,sizeof(Damage::PieceVertex))))return false;
        unsigned materials[Damage::kParts];
        for(unsigned part=0;part<Damage::kParts;++part)materials[part]=part<kFractureKinds?kFractureMaterials[part]:kSoftMaterials[part-kFractureKinds];
        float rows[Damage::kBurstPieces*12];
        for(unsigned burst=0;burst<Damage::kBursts;++burst) {
            if(!fragments.Live(burst,clock))continue;
            unsigned pieces=fragments.Transforms(burst,clock,c.origin,rows);
            if(!pieces||!Check(d->SetVertexShaderConstantF(37,rows,pieces*3)))continue;
            for(unsigned part=0;part<Damage::kParts;++part) {
                auto range=fragments.PartOf(burst,part);if(!range.count)continue;
                if(!exterior&&!useMaterial(materials[part],true))return false;
                if(!Check(d->DrawPrimitive(D3DPT_TRIANGLELIST,range.first,range.count/3)))return false;
            }
        }
        return true;
    };
    const auto& spray=s_damage.Blood();
    auto drawSpray=[&](bool exterior) {
        if(!s_bloodSpray||!spray.Any(clock))return true;
        if(!Check(d->SetVertexDeclaration(s_pieceDecl))||!Check(d->SetVertexShader(s_sprayVS))
            ||!Check(d->SetStreamSource(0,s_droplets,0,sizeof(Damage::PieceVertex))))return false;
        if(!exterior) {
            const float surface[4]={0,0,0,1};
            if(!Check(d->SetPixelShaderConstantF(0,kBlood.color,1))||!Check(d->SetPixelShaderConstantF(11,surface,1))
                ||!Check(d->SetPixelShaderConstantF(12,kBlood.cut,1)))return false;
        }
        float jet[Damage::kSprayConstants*4];
        for(unsigned k=0;k<Damage::kJets;++k) {
            if(!spray.Live(k,clock))continue;
            unsigned droplets=spray.Constants(k,clock,c.origin,jet);
            if(!Check(d->SetVertexShaderConstantF(37,jet,Damage::kSprayConstants))
                ||!Check(d->DrawPrimitive(D3DPT_TRIANGLELIST,0,droplets*Damage::kDropletTriangles)))return false;
        }
        return true;
    };
    bool flying=s_bloodSpray&&spray.Any(clock);
    if(impact){CollectBlood();TrackBlood(validImpact?impact->sequence:0,spray,flying);}
    bool measuring=impact&&flying&&s_bloodShot.sequence&&s_bloodShot.counted&&!s_bloodPending[0]&&!s_bloodPending[1];
    if(measuring) {
        auto& sample=s_bloodSample;sample=BloodSample{};sample.sequence=s_bloodShot.sequence;
        sample.samples=desc.MultiSampleType>=D3DMULTISAMPLE_2_SAMPLES?unsigned(desc.MultiSampleType):1;
        if(const Impact::Path* path=validImpact?&impact->path:s_damage.LatestPath()) {
            Vec3 toward=path->entry-Impact::Eye(head,c),line=path->exit-path->entry;
            sample.distance=Length(toward)*head.head.scale;
            if(Normalize(toward)&&Normalize(line))sample.angle=acosf(Clamp(Dot(toward,line),-1,1))*57.29578f;
            const auto& entry=Damage::kJetStyles[Damage::EntryJet];
            if(sample.distance>0)sample.dropPixels=(entry.sizeLo+entry.sizeHi)*.5f*head.head.scale*fabsf(c.projection[0])*float(c.viewport.Width)/sample.distance;
        }
    }
    auto begin=[&](unsigned k){if(measuring&&FAILED(s_bloodQueries[k]->Issue(D3DISSUE_BEGIN)))measuring=false;return true;};
    auto end=[&](unsigned k){if(measuring)s_bloodPending[k]=SUCCEEDED(s_bloodQueries[k]->Issue(D3DISSUE_END));return true;};
    //inside the window every droplet draws over the anatomy, outside it the solid body still hides what flies behind
    auto drawThrough=[&]() {
        if(!flying)return true;
        const DWORD test=c.Reversed()?D3DCMP_GREATEREQUAL:D3DCMP_LESSEQUAL;
        return Check(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS))&&Check(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE))
            &&Check(d->SetTexture(0,s_mask))&&Check(d->SetPixelShader(s_sprayThroughPS))&&begin(1)&&drawSpray(false)&&end(1)
            &&Check(d->SetRenderState(D3DRS_ZFUNC,test))&&Check(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
    };
    auto drawBullet=[&]() {
        unsigned count=s_damage.Bullets();
        return !shot||!count||(Check(d->SetVertexDeclaration(s_anatomyDecl))&&Check(d->SetVertexShader(s_bulletVS))
            &&Check(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,count/3,s_damage.Bullet(),sizeof(Vertex))));
    };
    auto drawAnatomy=[&]() {
        for(size_t i=0;i<model.meshes.size();++i) {
            const auto& m=model.meshes[i];if(!(m.visibility&(shot?1:mode))||s_damage.Ruptured(unsigned(i)))continue;
            if(!useMaterial(m.material,false)||!Check(d->SetStreamSource(0,meshes[i].vb,0,sizeof(Vertex)))
                ||!Check(d->SetIndices(meshes[i].ib))||!Check(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,
                    static_cast<UINT>(m.vertices.size()),0,static_cast<UINT>(m.indices.size()/3))))return false;
        }
        return true;
    };
    if(!Check(d->SetVertexShaderConstantF(15,Fit::Centers(),kFitAnchors))
        ||!Check(d->SetVertexShaderConstantF(24,head.fit.coefficients[0],kFitCoefficients)))return false;
    for(unsigned i=1;i<s_targetCount;++i)if(!Check(d->SetRenderTarget(i,nullptr)))return false;
    auto mask=s_multisample?s_multisample:s_maskSurface;
    if(!Check(d->SetRenderTarget(0,mask))||!Check(d->SetViewport(&c.viewport))
        ||!Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0))||!Check(d->SetVertexDeclaration(s_maskDecl))
        //visible triangles contribute coverage, so a rear neck fade cannot erase the front jaw
        ||!Check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE))||!Check(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE))
        ||!Check(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE))||!Check(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_MAX))
        ||!Check(d->SetVertexShader(s_maskVS))||!Check(d->SetPixelShader(s_maskPS)))return false;
    const auto p=c.projection;
    const float maskDepth[12]={p[2],p[6],p[10],p[14],p[3],p[7],p[11],p[15],
        style.depthTolerance*head.head.scale,c.viewport.MinZ,c.viewport.MaxZ-c.viewport.MinZ,0};
    MaskVertex apertures[RevealMask::kApertureTriangles*3];
    RevealMask::BuildApertures(head,apertures);
    if(head.skinVertices>kSkinVertices||head.skinTriangles>kSkinTriangles)return Check(E_INVALIDARG);
    //head and neck weight fades out at the base of the neck, a body region's weight where the next region takes over
    const float revealRange[4]={region==HeadRegion?.02f:kBodyRevealLow,region==HeadRegion?.3f:kBodyRevealHigh,0,0};
    if(!Check(d->SetPixelShaderConstantF(2,maskDepth,3))||!Check(d->SetPixelShaderConstantF(16,revealRange,1))
        ||(head.face&&!Check(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,kHeadVertices,kHeadTriangles,head.indices,D3DFMT_INDEX16,head.vertices,sizeof(MaskVertex))))
        ||(head.skinTriangles&&!Check(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,head.skinVertices,head.skinTriangles,head.skinIndices,D3DFMT_INDEX16,head.skin,sizeof(MaskVertex))))
        ||(head.face&&!Check(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,RevealMask::kApertureTriangles,apertures,sizeof(MaskVertex)))))return false;
    if(!Check(d->SetDepthStencilSurface(nullptr))||!Check(d->SetRenderTarget(0,s_exteriorSurface)))return false;
    if(s_multisample&&!Check(d->StretchRect(s_multisample,nullptr,s_maskSurface,nullptr,D3DTEXF_NONE)))return false;
    if(!Check(d->SetRenderTarget(0,s_multisample?s_multisample:s_exteriorSurface))||!Check(d->SetDepthStencilSurface(saved.depth))
        ||!Check(d->SetViewport(&c.viewport))||!Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0)))return false;
    bool damaged=shot||fragments.LiveVertices(clock)||(s_bloodSpray&&spray.Any(clock));
    if(damaged) {
        float screen[4]={1.0f/desc.Width,1.0f/desc.Height,style.opacity,0};
        //pieces that leave the skin outline are composited from this coverage pass
        if(!Check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE))||!Check(d->SetTexture(0,s_mask))
            ||!Check(d->SetPixelShaderConstantF(1,screen,1))||!Check(d->SetPixelShader(s_anatomyExteriorPS))||!drawPlates()
            ||!Check(d->SetPixelShader(s_exteriorPS))||!drawDebris(true)||!begin(0)||!drawSpray(true)||!end(0)||!drawBullet())return false;
    }
    if(!Check(d->SetDepthStencilSurface(nullptr))||!Check(d->SetRenderTarget(0,s_anatomySurface)))return false;
    if(s_multisample&&!Check(d->StretchRect(s_multisample,nullptr,s_exteriorSurface,nullptr,D3DTEXF_NONE)))return false;
    if(!Check(d->SetDepthStencilSurface(s_depth))||!Check(d->SetViewport(&c.viewport))
        ||!Check(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,c.Reversed()?0.0f:1.0f,0))
        ||!Check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE))
        ||!Check(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE))||!Check(d->SetVertexDeclaration(s_anatomyDecl))
        ||!Check(d->SetVertexShader(s_anatomyVS))||!Check(d->SetPixelShader(s_anatomyPS)))return false;
    if(!drawAnatomy())return false;
    if(damaged) {
        if(!drawPlates()||!Check(d->SetPixelShader(s_damagePS))||!drawDebris(false)||!drawSpray(false)||!drawThrough()
            ||!Check(d->SetPixelShader(s_bulletPS))||!drawBullet())return false;
    }

    if(!Check(d->SetDepthStencilSurface(nullptr))||!Check(d->SetRenderTarget(0,saved.targets[0]))||!Check(d->SetViewport(&c.viewport))
        ||!Check(d->SetRenderState(D3DRS_ZENABLE,FALSE))||!Check(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE))
        ||!Check(d->SetRenderState(D3DRS_COLORWRITEENABLE,7))||!Check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE))
        ||!Check(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA))||!Check(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA))
        ||!Check(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD))||!Check(d->SetTexture(0,s_mask))||!Check(d->SetTexture(1,s_anatomy))||!Check(d->SetTexture(2,s_exterior))
        ||!Check(d->SetVertexDeclaration(s_quadDecl))||!Check(d->SetVertexShader(s_compositeVS))||!Check(d->SetPixelShader(s_compositePS)))return false;
    float screen[4]={1.0f/desc.Width,1.0f/desc.Height,style.opacity,0};
    float l=-1-1.0f/c.viewport.Width,r=1-1.0f/c.viewport.Width,t=1+1.0f/c.viewport.Height,b=-1+1.0f/c.viewport.Height;
    float u0=float(c.viewport.X)/desc.Width,u1=float(c.viewport.X+c.viewport.Width)/desc.Width;
    float v0=float(c.viewport.Y)/desc.Height,v1=float(c.viewport.Y+c.viewport.Height)/desc.Height;
    float bounds[4]={u0+screen[0]*.5f,v0+screen[1]*.5f,u1-screen[0]*.5f,v1-screen[1]*.5f};
    if(!Check(d->SetPixelShaderConstantF(1,screen,1))||!Check(d->SetPixelShaderConstantF(5,bounds,1)))return false;
    const float quad[6][4]={{l,t,u0,v0},{r,t,u1,v0},{r,b,u1,v1},{l,t,u0,v0},{r,b,u1,v1},{l,b,u0,v1}};
    return Check(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,quad,sizeof quad[0]));
}
}
