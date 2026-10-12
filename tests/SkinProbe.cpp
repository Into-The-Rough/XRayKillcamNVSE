#include "../src/BodySkin.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>

using namespace XRay;
static void Check(bool ok,const char* name){if(!ok){printf("FAIL: %s\n",name);exit(1);}}
void TestSkin() {
    auto frame=std::make_unique<HeadFrame>();auto& f=*frame;
    f.head=Transform::Identity();f.head.t={10,20,30};f.head.scale=1.1f;
    std::vector<uint16_t> slot;
    //a body shape reaches from the collar to the feet, far below the head
    const Vec3 world[]={{10,20,30-9},{14,20,30-9},{10,24,30-120},{14,24,30-120}};
    const float reveal[]={.8f,1.4f,0,0};const uint16_t indices[]={0,1,2,1,3,2};
    Check(BodySkin::Add({world,reveal,4,indices,6},slot,f)==BodySkin::Kept&&f.skinVertices==4&&f.skinTriangles==2,"a shape reaching the feet is kept whole");
    Check(fabsf(f.skin[2].local.z+120/1.1f)<1e-3f&&f.skin[1].reveal==1&&f.skin[0].reveal==.8f,"head-space positions and clamped weights");
    Check(f.skinIndices[3]==1&&f.skinIndices[5]==2,"shared vertices are stored once");
    Vec3 bad[4];for(unsigned i=0;i<4;++i)bad[i]=world[i];bad[3].x=std::numeric_limits<float>::quiet_NaN();
    Check(BodySkin::Add({bad,reveal,4,indices,6},slot,f)==BodySkin::BadVertex&&f.skinVertices==4&&f.skinTriangles==2,"a corrupt vertex rejects its shape and leaves the frame as it was");
    const uint16_t outside[]={0,1,4};
    Check(BodySkin::Add({world,reveal,4,outside,3},slot,f)==BodySkin::BadTriangles&&f.skinTriangles==2,"an index outside the shape is rejected");
    std::vector<Vec3> many(4000,Vec3{0,0,0});std::vector<float> weights(4000,0);std::vector<uint16_t> list;
    for(uint16_t i=0;i+2<4000;i+=3){list.push_back(i);list.push_back(uint16_t(i+1));list.push_back(uint16_t(i+2));}
    unsigned kept=0;BodySkin::Skip last=BodySkin::Kept;
    while((last=BodySkin::Add({many.data(),weights.data(),4000,list.data(),uint32_t(list.size())},slot,f))==BodySkin::Kept)++kept;
    Check(last==BodySkin::OverCapacity&&f.skinVertices<=kSkinVertices&&f.skinTriangles<=kSkinTriangles&&kept==2,"capacity is enforced per whole shape");
    printf("PASS: skin assembly keeps whole body shapes, rejects corrupt ones and respects capacity\n");
}
//skin over anatomy that opens skin opens by distance, everything else keeps its weight reveal
void TestOpenOver() {
    auto frame=std::make_unique<HeadFrame>();auto& f=*frame;
    f.head=Transform::Identity();f.influences=RestInfluences();
    Model model;Mesh testis{};testis.material=kTestisMaterial;testis.visibility=3;testis.first=testis.second=3;
    for(Vec3 p:{Vec3{0,0,0},Vec3{1,1,1}})testis.vertices.push_back({p,{0,0,1},1,1,51});
    Mesh rib{};rib.material=kBoneMaterial;rib.visibility=3;rib.vertices.push_back({{20,0,0},{0,0,1},1,1,0});
    model.meshes={testis,rib};
    //the pelvis has moved the testis 10 units along y since rest
    f.influences[3].t={0,10,0};
    const Vec3 at[]={{.5f,10.5f,.5f},{.5f,10.5f,3.f},{.5f,10.5f,6.f},{20,0,0},{.5f,.5f,.5f}};
    for(unsigned i=0;i<5;++i)f.skin[i]={at[i],at[i],.1f};
    f.skinVertices=5;
    BodySkin::OpenOver(model,f);
    Check(f.skin[0].reveal==1&&f.skin[1].reveal>.1f&&f.skin[1].reveal<1&&f.skin[2].reveal==.1f,"skin over the posed testis opens fully, fades within reach and stays shut beyond it");
    Check(f.skin[3].reveal==.1f&&f.skin[4].reveal==.1f,"bone opens nothing and the testis's rest position is not where it is now");
    Model plain;plain.meshes={rib};f.skin[0].reveal=.1f;BodySkin::OpenOver(plain,f);
    Check(f.skin[0].reveal==.1f,"an anatomy without such meshes leaves the skin as weighted");
    //focused on the genitals, the region's weights give way to a wider window over them
    const Vec3 margin{.5f,10.5f,4.5f};f.skin[5]={margin,margin,.9f};f.skinVertices=6;
    for(unsigned i=0;i<6;++i)f.skin[i].reveal=.9f;
    BodySkin::OpenOver(model,f,true);
    Check(f.skin[0].reveal==1&&f.skin[1].reveal==1&&f.skin[5].reveal>.4f&&f.skin[5].reveal<.6f,"focused, the skin over the testis and a margin round it opens");
    Check(f.skin[2].reveal==0&&f.skin[3].reveal==0&&f.skin[4].reveal==0,"and the region's own weights close everywhere else");
    for(unsigned i=0;i<6;++i)f.skin[i].reveal=.9f;
    BodySkin::OpenOver(plain,f,true);
    Check(f.skin[3].reveal==.9f,"a focus without such anatomy leaves the skin as weighted");
    printf("PASS: skin over the genitals opens by distance, posed with the pelvis, and alone when focused on them\n");
}
static Transform Pose(float yaw,float pitch,Vec3 t,float scale) {
    float cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch);
    return {{cy,-sy*cp,sy*sp,sy,cy*cp,-cy*sp,0,sp,cp},t,scale};
}
static bool Same(const Transform& a,const Transform& b) {
    Vec3 probes[]={{0,0,0},{3,-2,5},{-7,1,2}};
    for(auto p:probes){Vec3 d=a.Apply(p)-b.Apply(p);if(Dot(d,d)>1e-6f)return false;}
    return true;
}
//each shape sees the same skeleton through its own skin space, so every bone pair it binds states a rest relation
void TestSkinBinds() {
    const Transform rest[]={Pose(.1f,.2f,{0,0,90},1),Pose(.7f,-.3f,{-14,-3,102},1),Pose(1.1f,.4f,{-29,-4,94},1),Pose(.9f,.1f,{-44,0,85},1),
        Pose(-.7f,.3f,{14,-3,102},1),Pose(-1.f,.2f,{29,-4,94},1),Pose(-.8f,.0f,{44,0,85},1)};
    auto relation=[&](unsigned k){return Compose(Inverse(rest[k]),rest[0]);};
    auto add=[&](SkinBinds& b,std::initializer_list<unsigned> bones,Transform skinSpace) {
        auto& shape=b.shapes[b.shapeCount++];shape.present=0;
        for(unsigned k:bones){shape.present|=1u<<k;shape.skinToBone[k]=Compose(Inverse(rest[k]),skinSpace);}
    };
    auto binds=std::make_unique<SkinBinds>();auto& b=*binds;
    //the forearm and hand only reach the frame through the upper arm, and that shape comes last
    add(b,{1,2,3},Pose(2.f,.5f,{3,4,5},1.f));
    add(b,{3},Pose(-1.f,.2f,{0,9,0},1.f));
    add(b,{0,1},Pose(0,0,{0,0,0},1.f));
    b.Resolve();
    Check(b.found[0]&&b.found[1]&&b.found[2]&&b.found[3]&&!b.found[4]&&!b.found[5]&&!b.found[6],"links chain across shapes and unbound influences stay unresolved");
    Check(Same(b.relation[1],relation(1))&&Same(b.relation[2],relation(2))&&Same(b.relation[3],relation(3)),"chained relations match the skeleton whatever each shape's skin space");
    auto direct=std::make_unique<SkinBinds>();auto& d=*direct;
    add(d,{1,4},Pose(.3f,.1f,{1,1,1},1.f));add(d,{0,1},Pose(-.4f,.2f,{2,0,0},1.f));add(d,{0,4},Pose(.9f,-.6f,{0,3,0},1.f));d.Resolve();
    Check(d.found[4]&&Same(d.relation[4],relation(4)),"a direct frame link and a chain agree");
    printf("PASS: rest relations resolve directly and through chains across shapes in different skin spaces\n");
}
