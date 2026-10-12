#include "ShotAudit.h"
#include "Runtime.h"
#include <cstdio>
#include <cstring>
#include <share.h>
#include <memory>
#include <type_traits>

namespace XRay::Audit {
static SRWLOCK s_lock=SRWLOCK_INIT;
//records carry the neck skin, so they live on the heap rather than in the image or on the render thread's stack
static std::unique_ptr<Record> s_pending[2];
static unsigned s_count=0,s_stages=0;
static uint32_t s_generation=0;
static uint64_t s_sequence=0,s_session=0;
static char s_directory[MAX_PATH];
static bool s_enabled=false;

bool Project(const CameraFrame& camera,Vec3 world,Vec3& screen) {
    Vec3 relative=world-camera.origin;
    const float* v=camera.view;const float* p=camera.projection;
    float eye[4];
    for(unsigned i=0;i<4;++i)eye[i]=relative.x*v[i]+relative.y*v[i+4]+relative.z*v[i+8]+v[i+12];
    float clip[4]{};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)clip[i]+=eye[j]*p[j*4+i];
    if(!std::isfinite(clip[3])||clip[3]<=.0001f)return false;
    screen={camera.viewport.X+(clip[0]/clip[3]+1)*camera.viewport.Width*.5f,
        camera.viewport.Y+(1-clip[1]/clip[3])*camera.viewport.Height*.5f,clip[2]/clip[3]};
    return Finite(screen);
}
bool CentreRay(const CameraFrame& camera,Vec3& origin,Vec3& direction) {
    const float* v=camera.view;const float* p=camera.projection;
    //only the ordinary perspective form supports this diagnostic, rendering has no such restriction
    if(fabsf(p[11]-1)>.0001f||fabsf(p[15])>.0001f||fabsf(p[0])<.0001f||fabsf(p[5])<.0001f
        ||fabsf(p[1])+fabsf(p[4])+fabsf(p[12])+fabsf(p[13])>.0001f)return false;
    Vec3 eye={-p[8]/p[0],-p[9]/p[5],1};
    direction={Dot({v[0],v[1],v[2]},eye),Dot({v[4],v[5],v[6]},eye),Dot({v[8],v[9],v[10]},eye)};
    origin=camera.origin-Vec3{v[12]*v[0]+v[13]*v[1]+v[14]*v[2],v[12]*v[4]+v[13]*v[5]+v[14]*v[6],v[12]*v[8]+v[13]*v[9]+v[14]*v[10]};
    return Finite(origin)&&Normalize(direction);
}
//version 1 predates the neck bend, version 2 the skin weights and neck skin, version 3 the region and influence table,
//version 4 the posed bones and the hit's carrier bone, version 5 the larger skin and the anatomy set
struct LegacyVertex {Vec3 world,local;};
struct HeadV1 {Transform head;HeadFit fit;LegacyVertex vertices[kHeadVertices];uint16_t indices[kHeadTriangles*3];};
struct HeadV2:HeadV1 {Transform neck;float neckError;};
struct HeadV3 {
    Transform head;HeadFit fit;MaskVertex vertices[kHeadVertices];uint16_t indices[kHeadTriangles*3];Transform neck;float neckError;
    uint32_t skinVertices,skinTriangles,skinShapes,skinSkipped;MaskVertex skin[8192];uint16_t skinIndices[16384*3];
};
struct HeadV5 {
    Transform head;HeadFit fit;MaskVertex vertices[kHeadVertices];uint16_t indices[kHeadTriangles*3];Influences influences;float neckError;uint32_t region;
    bool face,noGenitals;uint32_t skinVertices,skinTriangles,skinShapes,skinSkipped;MaskVertex skin[8192];uint16_t skinIndices[16384*3];Influences posedBones;
};
//version 4 is version 5 up to the posed bones, with the genitals flag still padding
struct alignas(HeadV5) HeadV4 {uint8_t bytes[offsetof(HeadV5,posedBones)];};
static_assert(offsetof(HeadFrame,noGenitals)==offsetof(HeadFrame,face)+1&&offsetof(HeadFrame,anatomy)==offsetof(HeadFrame,face)+2
    &&offsetof(HeadFrame,skinVertices)==offsetof(HeadFrame,face)+4,"the genitals flag and anatomy set live in padding before the skin counts");
struct alignas(Impact::Sample) SampleV4 {uint8_t bytes[offsetof(Impact::Sample,carrier)];};
template<class Head> struct LegacyRecord {
    uint32_t magic,version,size,target,support,stage;uint64_t session;
    Head head;CameraFrame camera;Appearance appearance;std::conditional_t<std::is_same_v<Head,HeadV5>,Impact::Sample,SampleV4> source;Impact::Frame impact;
};
static_assert(sizeof(LegacyRecord<HeadV3>)==376136,"version 3 capture size");
static_assert(sizeof(LegacyRecord<HeadV4>)==376456,"version 4 capture size");
template<class Head> static bool Legacy(FILE* file,uint32_t version,Record& out) {
    auto legacy=std::make_unique<LegacyRecord<Head>>();
    if(fread(legacy.get(),sizeof *legacy,1,file)!=1||legacy->version!=version||legacy->size!=sizeof *legacy)return false;
    out={};
    out.magic=legacy->magic;out.size=sizeof out;out.target=legacy->target;out.support=legacy->support;out.stage=legacy->stage;
    out.session=legacy->session;
    memcpy(&out.source,&legacy->source,sizeof legacy->source);
    out.camera=legacy->camera;out.appearance=legacy->appearance;out.impact=legacy->impact;
    if constexpr(std::is_same_v<Head,HeadV4>||std::is_same_v<Head,HeadV5>) {
        const auto& h=*reinterpret_cast<const HeadV5*>(&legacy->head);auto& o=out.head;
        if(h.skinVertices>8192||h.skinTriangles>16384)return false;
        o.head=h.head;o.fit=h.fit;memcpy(o.vertices,h.vertices,sizeof o.vertices);memcpy(o.indices,h.indices,sizeof o.indices);
        o.influences=h.influences;o.neckError=h.neckError;o.region=h.region;o.face=h.face;
        //captures before the genitals flag held men's and women's anatomy alike, and every capture before the anatomy set was human
        o.noGenitals=std::is_same_v<Head,HeadV5>&&h.noGenitals;o.anatomy=HumanAnatomy;
        o.skinVertices=h.skinVertices;o.skinTriangles=h.skinTriangles;o.skinShapes=h.skinShapes;o.skinSkipped=h.skinSkipped;
        memcpy(o.skin,h.skin,h.skinVertices*sizeof(MaskVertex));memcpy(o.skinIndices,h.skinIndices,h.skinTriangles*3*sizeof(uint16_t));
        if constexpr(std::is_same_v<Head,HeadV5>)o.posedBones=h.posedBones;
        return true;
    }
    else {
    out.head.head=legacy->head.head;out.head.fit=legacy->head.fit;
    memcpy(out.head.indices,legacy->head.indices,sizeof out.head.indices);
    if constexpr(std::is_same_v<Head,HeadV3>) {
        memcpy(out.head.vertices,legacy->head.vertices,sizeof out.head.vertices);
        const auto& h=legacy->head;
        if(h.skinVertices>8192||h.skinTriangles>16384)return false;
        out.head.skinVertices=h.skinVertices;out.head.skinTriangles=h.skinTriangles;out.head.skinShapes=h.skinShapes;out.head.skinSkipped=h.skinSkipped;
        memcpy(out.head.skin,h.skin,h.skinVertices*sizeof(MaskVertex));memcpy(out.head.skinIndices,h.skinIndices,h.skinTriangles*3*sizeof(uint16_t));
    }else for(unsigned i=0;i<kHeadVertices;++i)out.head.vertices[i]={legacy->head.vertices[i].world,legacy->head.vertices[i].local,1};
    if constexpr(!std::is_same_v<Head,HeadV1>){out.head.influences[1]=legacy->head.neck;out.head.neckError=legacy->head.neckError;}
    return true;
    }
}
static bool Load(FILE* file,Record& out) {
    if(fseek(file,0,SEEK_END))return false;
    long length=ftell(file);
    if(fseek(file,0,SEEK_SET))return false;
    if(length==long(sizeof(Record)))return fread(&out,sizeof out,1,file)==1&&out.version==6&&out.size==sizeof out;
    if(length==long(sizeof(LegacyRecord<HeadV5>)))return Legacy<HeadV5>(file,5,out);
    if(length==long(sizeof(LegacyRecord<HeadV4>)))return Legacy<HeadV4>(file,4,out);
    if(length==long(sizeof(LegacyRecord<HeadV3>)))return Legacy<HeadV3>(file,3,out);
    if(length==long(sizeof(LegacyRecord<HeadV2>)))return Legacy<HeadV2>(file,2,out);
    if(length==long(sizeof(LegacyRecord<HeadV1>)))return Legacy<HeadV1>(file,1,out);
    return false;
}
bool Read(const char* path,Record& out) {
    FILE* file=nullptr;fopen_s(&file,path,"rb");if(!file)return false;
    bool ok=Load(file,out);fclose(file);
    bool influences=true;for(const auto& t:out.head.influences)influences&=t.Valid();
    for(const auto& t:out.head.posedBones)influences&=t.Valid();
    if(out.source.carrier>=kInfluences||(out.source.carrier&&!out.source.carrierPose.Valid()))return false;
    if(!ok||out.magic!=kMagic||!out.head.head.Valid()||!influences||!std::isfinite(out.head.neckError)||out.head.region>=kRegions||out.head.anatomy>=kAnatomies
        ||!out.camera.viewport.Width||!out.camera.viewport.Height||out.camera.viewport.X>8192||out.camera.viewport.Y>8192
        ||out.camera.viewport.Width>8192-out.camera.viewport.X||out.camera.viewport.Height>8192-out.camera.viewport.Y)return false;
    for(float x:out.camera.view)if(!std::isfinite(x))return false;
    for(float x:out.camera.projection)if(!std::isfinite(x))return false;
    if(!Finite(out.camera.origin)||!std::isfinite(out.impact.age)||out.impact.age<0)return false;
    const auto& a=out.appearance;const auto& b=out.impact.bullet;
    if(!std::isfinite(a.opacity)||a.opacity<0||a.opacity>1||!std::isfinite(a.scale)||a.scale<.5f||a.scale>1.5f
        ||!Finite({a.up,a.forward,a.side})||!std::isfinite(a.depthTolerance)||a.depthTolerance<0||a.depthTolerance>4
        ||!std::isfinite(b.radius)||b.radius<=0||!std::isfinite(b.length)||b.length<=0||!std::isfinite(b.nose)||b.nose<=0
        ||!std::isfinite(out.head.fit.chin)||!std::isfinite(out.head.fit.strength))return false;
    const auto& h=out.head;
    auto valid=[](const MaskVertex& v){return Finite(v.world)&&Finite(v.local)&&std::isfinite(v.reveal)&&v.reveal>=0&&v.reveal<=1;};
    for(const auto& vertex:h.vertices)if(!valid(vertex))return false;
    for(auto index:h.indices)if(index>=kHeadVertices)return false;
    if(h.skinVertices>kSkinVertices||h.skinTriangles>kSkinTriangles)return false;
    for(uint32_t i=0;i<h.skinVertices;++i)if(!valid(h.skin[i]))return false;
    for(uint32_t i=0;i<h.skinTriangles*3;++i)if(h.skinIndices[i]>=h.skinVertices)return false;
    for(const auto& row:out.head.fit.coefficients)for(float x:row)if(!std::isfinite(x))return false;
    return true;
}
void Init(const char* runtime) {
    s_enabled=_snprintf_s(s_directory,sizeof s_directory,_TRUNCATE,"%sXRayKillcamCaptures",runtime)>0;
    if(s_enabled)s_enabled=CreateDirectoryA(s_directory,nullptr)||GetLastError()==ERROR_ALREADY_EXISTS;
    s_session=GetTickCount64();
    Log(s_enabled?"shot audit enabled: %s (32 shots, two frames each; private geometry)":"shot audit unavailable: %s",s_directory);
}
void Select(uint32_t generation) {
    AcquireSRWLockExclusive(&s_lock);s_generation=generation;s_count=s_stages=0;s_sequence=0;ReleaseSRWLockExclusive(&s_lock);
}
void Submit(uint32_t generation,uint32_t target,const CameraFrame& camera,const HeadFrame& head,
    const Appearance& appearance,const Impact::Sample& source,const Impact::Frame& impact,unsigned support) {
    if(!s_enabled)return;
    AcquireSRWLockExclusive(&s_lock);
    if(generation==s_generation) {
        if(s_sequence!=impact.sequence){s_sequence=impact.sequence;s_stages=0;}
        unsigned stage=impact.age>=.12f?1:0,bit=1u<<stage;
        if(!(s_stages&bit)&&s_count<2) {
            s_pending[s_count]=std::make_unique<Record>();auto& r=*s_pending[s_count++];r.size=sizeof r;r.session=s_session;r.target=target;r.stage=stage;r.support=support;
            r.camera=camera;r.head=head;r.appearance=appearance;r.source=source;r.impact=impact;s_stages|=bit;
        }
    }
    ReleaseSRWLockExclusive(&s_lock);
}
void Drain() {
    AcquireSRWLockExclusive(&s_lock);
    if(!s_count){ReleaseSRWLockExclusive(&s_lock);return;}
    auto record=std::move(s_pending[0]);if(--s_count)s_pending[0]=std::move(s_pending[1]);
    ReleaseSRWLockExclusive(&s_lock);
    const Record& r=*record;
    char path[MAX_PATH];
    if(_snprintf_s(path,sizeof path,_TRUNCATE,"%s\\shot-%02u-%u.xrc",s_directory,unsigned(r.impact.sequence%32),r.stage)<0)return;
    FILE* file=_fsopen(path,"wb",_SH_DENYWR);bool saved=false;
    if(file){saved=fwrite(&r,sizeof r,1,file)==1;saved=fclose(file)==0&&saved;}
    Vec3 contact{},entry{},rawEntry{};
    bool contactOK=Project(r.camera,r.source.point,contact);
    bool entryOK=r.impact.path.valid&&Project(r.camera,r.head.head.Apply(r.impact.path.entry),entry);
    bool rawOK=r.impact.path.valid&&r.source.posed&&Project(r.camera,r.source.pose.Apply(r.impact.path.entry),rawEntry);
    Log("shot audit %u stage=%u: saved=%u file=%s",unsigned(r.impact.sequence),r.stage,saved?1:0,path);
    Log("shot screen: viewport=(%u,%u,%u,%u) contact=%u(%.1f,%.1f) shownEntry=%u(%.1f,%.1f) hitPoseEntry=%u(%.1f,%.1f)",
        r.camera.viewport.X,r.camera.viewport.Y,r.camera.viewport.Width,r.camera.viewport.Height,
        contactOK?1:0,contact.x,contact.y,entryOK?1:0,entry.x,entry.y,rawOK?1:0,rawEntry.x,rawEntry.y);
    Impact::Sample centre;Impact::Path centrePath;
    if(CentreRay(r.camera,centre.rayOrigin,centre.direction)) {
        Vec3 middle=r.head.head.Apply(Fit::Warp(r.head.fit,{8,0,0}));
        centre.valid=centre.traced=true;centre.point=centre.rayOrigin+centre.direction*Dot(middle-centre.rayOrigin,centre.direction);
        bool mapped=Impact::Map(centre,r.head,centrePath);
        Log("viewport centre (diagnostic only): mapped=%u entry=(%.2f,%.2f,%.2f) exit=(%.2f,%.2f,%.2f) poseScale=%.3f/%.3f poseMove=%.3f",
            mapped?1:0,centrePath.entry.x,centrePath.entry.y,centrePath.entry.z,centrePath.exit.x,centrePath.exit.y,centrePath.exit.z,
            r.source.posed?r.source.pose.scale:0,r.head.head.scale,r.source.posed?Length(r.head.head.t-r.source.pose.t):-1);
    }
}
}
