#include "../src/Damage.h"
#include "../src/BodySkin.h"
#include "../src/Groin.h"
#include "ProbeRender.h"
#include "../src/HumanRest.h"
#include "../src/HorseRest.h"
#include "../src/Proportions.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace XRay;
namespace {
void Check(bool ok,const char* name){if(!ok){printf("FAIL: %s\n",name);exit(1);}}
struct Body {std::vector<Vec3> rest;std::vector<float> weight;std::vector<uint16_t> indices;};
Body LoadBody(const char* path) {
    Body body;FILE* file=nullptr;fopen_s(&file,path,"rb");Check(file!=nullptr,"private torso frame fixture");
    uint32_t counts[2];Check(fread(counts,sizeof counts,1,file)==1,"region fixture counts");
    std::vector<float> raw(counts[0]*4);body.indices.resize(counts[1]*3);
    Check(fread(raw.data(),4,raw.size(),file)==raw.size()&&fread(body.indices.data(),2,body.indices.size(),file)==body.indices.size(),"region fixture data");
    fclose(file);
    for(uint32_t i=0;i<counts[0];++i){body.rest.push_back({raw[i*4],raw[i*4+1],raw[i*4+2]});body.weight.push_back(raw[i*4+3]);}
    return body;
}
//Spine1 space is x up, y forward, z to the character's left, the world here is z up
Transform World() {
    Transform t{{0,0,-1,0,1,0,1,0,0},{500,-300,90},1};return t;
}
std::unique_ptr<Audit::Record> RegionRecord(const Body& body,uint32_t region,std::vector<Vec3>& scene,const char* rest=nullptr,uint8_t anatomy=HumanAnatomy) {
    auto r=std::make_unique<Audit::Record>();auto& f=r->head;
    f.head=World();f.face=false;f.region=region;f.fit=HeadFit{};f.anatomy=anatomy;
    std::vector<Vec3> world(body.rest.size());std::vector<uint16_t> slot;
    for(size_t i=0;i<world.size();++i)world[i]=f.head.Apply(body.rest[i]);
    Check(BodySkin::Add({world.data(),body.weight.data(),uint32_t(world.size()),body.indices.data(),uint32_t(body.indices.size())},slot,f)==BodySkin::Kept,"vanilla body as region skin");
    f.skinShapes=1;scene.clear();for(auto i:body.indices)scene.push_back(world[i]);
    //a creature fixture's own rest relations: its capture places the human anatomy through the human ones, at its bind pose,
    //sized by its spans, each influence is then the creature's bone back to the frame, out through the retargeted human bone
    //a horse places its own anatomy through its own rest pose, unscaled
    if(rest&&anatomy==HorseAnatomy) {
        FILE* file=nullptr;fopen_s(&file,rest,"rb");Check(file!=nullptr,"horse rest relations");
        for(unsigned k=0;k<kInfluences;++k) {
            Transform own;Check(fread(own.r,4,9,file)==9&&fread(&own.t,4,3,file)==3&&fread(&own.scale,4,1,file)==1,"horse rest relation");
            f.influences[k]=Compose(Inverse(own),Creature::Retarget(HorseAnatomy,region,k,Proportions{}));
        }
        fclose(file);
    }
    else if(rest) {
        //a creature's capture shows the torso without the external genitals
        f.noGenitals=true;
        std::string spansPath(rest);spansPath.replace(spansPath.rfind("rest.bin"),8,"spans.bin");
        FILE* file=nullptr;fopen_s(&file,spansPath.c_str(),"rb");Check(file!=nullptr,"creature spans");
        Spans spans;
        Check(fread(spans.reach,4,kInfluences,file)==kInfluences&&fread(&spans.chain,4,1,file)==1&&fread(spans.widths,4,2,file)==2&&fread(&spans.crown,4,1,file)==1,"creature span values");
        fclose(file);
        auto proportions=Creature::Measure(region,spans);f.fit=Creature::Fit(proportions);
        file=nullptr;fopen_s(&file,rest,"rb");Check(file!=nullptr,"creature rest relations");
        for(unsigned k=0;k<kInfluences;++k) {
            Transform own;Check(fread(own.r,4,9,file)==9&&fread(&own.t,4,3,file)==3&&fread(&own.scale,4,1,file)==1,"creature rest relation");
            f.influences[k]=Compose(Inverse(own),Creature::Retarget(HumanAnatomy,region,k,proportions));
        }
        fclose(file);
    }
    return r;
}
CameraFrame LookAt(Vec3 eye,Vec3 target,unsigned size) {
    CameraFrame c{};c.origin=target;
    Vec3 axis=target-eye;Normalize(axis);Vec3 up{0,0,1};Vec3 right=Cross(up,axis);Normalize(right);up=Cross(axis,right);
    Vec3 relative=eye-c.origin;
    const float view[16]={right.x,up.x,axis.x,0,right.y,up.y,axis.y,0,right.z,up.z,axis.z,0,-Dot(relative,right),-Dot(relative,up),-Dot(relative,axis),1};
    memcpy(c.view,view,sizeof view);
    float focal=1/tanf(.32f);
    const float projection[16]={focal,0,0,0,0,focal,0,0,0,0,1000.0f/999,1,0,0,-1000.0f/999,0};
    memcpy(c.projection,projection,sizeof projection);c.viewport={0,0,size,size,0,1};return c;
}
//the first skin crossing of a ray in frame space
bool Entry(const HeadFrame& f,Vec3 origin,Vec3 direction,Vec3& entry) {
    float best=1e9f;
    for(uint32_t i=0;i<f.skinTriangles*3;i+=3) {
        Vec3 a=f.skin[f.skinIndices[i]].local,e1=f.skin[f.skinIndices[i+1]].local-a,e2=f.skin[f.skinIndices[i+2]].local-a,p=Cross(direction,e2);
        float det=Dot(e1,p);if(fabsf(det)<1e-9f)continue;
        float inverse=1/det;Vec3 s=origin-a;float u=Dot(s,p)*inverse;if(u<0||u>1)continue;
        Vec3 q=Cross(s,e1);float v=Dot(direction,q)*inverse;if(v<0||u+v>1)continue;
        float t=Dot(e2,q)*inverse;if(t>0&&t<best)best=t;
    }
    entry=origin+direction*best;return best<1e9f;
}
void SavePixels(const std::vector<DWORD>& pixels,unsigned size,const char* name) {
    FILE* file=nullptr;fopen_s(&file,name,"wb");Check(file!=nullptr,"region preview output");
    fprintf(file,"P6\n%u %u\n255\n",size,size);
    for(auto p:pixels){unsigned char rgb[]={static_cast<unsigned char>(p>>16),static_cast<unsigned char>(p>>8),static_cast<unsigned char>(p)};fwrite(rgb,1,3,file);}
    fclose(file);
}
}
struct Route {const char* name;Vec3 start,direction;Vec3 eye;unsigned bone,soft;};
//expected minimum bone and soft runs along each route, from the anatomy each one passes through, in the region's frame
static const Route kTorsoRoutes[]={
    {"sternum and heart",{14,30,1.5f},{0,-1,0},{18,70,-30},2,1},
    {"liver",{6,30,-4},{0,-1,0},{12,70,-45},1,1},
    {"between the left ribs through both lungs",{16,-1,40},{0,0,-1},{20,10,75},0,2},
    {"abdomen",{-5,30,2},{0,-1,0},{2,70,-30},1,1},
    {"lumbar spine and intestines",{-2,-30,-1},{0,1,0},{6,-70,-35},1,1},
    {"groin from the side through both testes",{-20.3f,2.5f,30},{0,0,-1},{-14,30,45},0,2},
    {"groin from the front through the penis",{-17,30,.3f},{0,-1,0},{-12,60,-25},0,1},
};
static const Route kLegRoutes[]={
    {"left thigh and femur",{-15,25,8.2f},{0,-1,0},{-10,70,-15},1,0},
    {"left kneecap",{-31.2f,25,7.2f},{0,-1,0},{-26,70,-15},1,0},
    {"left shin through the tibia",{-46,25,7},{0,-1,0},{-40,70,-15},1,0},
    {"left foot from the front",{-63,25,7},{0,-1,0},{-50,60,-15},1,0},
};
static const Route kRightLegRoutes[]={
    {"right thigh from the side",{-12,1.5f,-40},{0,0,1},{-8,20,-75},1,0},
    {"right shin from behind",{-46,-25,-7},{0,1,0},{-40,-70,-25},1,0},
};
static const Route kLeftArmRoutes[]={
    {"left upper arm through the humerus",{9,25,22},{0,-1,0},{12,60,10},1,0},
    {"left forearm through radius and ulna",{16.85f,15.35f,37},{-.666f,-.746f,0},{30,40,25},2,0},
    {"left elbow from behind",{4,-25,28.5f},{0,1,0},{10,-60,15},1,0},
    {"left wrist bones from above",{5,-2.8f,45.5f},{-1,0,0},{40,20,40},1,0},
};
static const Route kRightArmRoutes[]={
    {"right upper arm from behind",{9,-25,-22},{0,1,0},{14,-60,-10},1,0},
    {"right forearm from below through the ulna",{-20,-4.5f,-37},{1,0,0},{-45,-20,-25},1,0},
};
//feral ghoul head space: x up, y forward, z to the creature's left, the head captured from the body mesh by its weights
static const Route kFeralHeadRoutes[]={
    {"forehead front to back",{11,30,0},{0,-1,0},{14,60,-25},1,1},
    {"temple from the side",{9,1,30},{0,0,-1},{12,20,60},1,1},
    {"jaw from the front",{3,30,0},{0,-1,0},{4,60,-25},1,0},
};
//the feral bind pose holds the arms at the sides, so the torso is shot front and back only, the rib cage follows the feral's
//hunched spine, so the liver shot passes below the ribs, and the belly sinks about 5 units behind the human intestines
static const Route kFeralTorsoRoutes[]={kTorsoRoutes[0],{"liver below the ribs",{6,30,-4},{0,-1,0},{12,70,-45},0,1},
    {"abdomen",{-5,30,.5f},{0,-1,0},{2,70,-30},1,0},kTorsoRoutes[4]};
//the feral bind pose angles the arm out and down, so its routes follow that arm in Spine2 space,
//and a front shot down the forearm's middle passes between radius and ulna, so the forearm is shot across them
static const Route kFeralLeftArmRoutes[]={
    {"upper arm through the humerus",{3,25,22.2f},{0,-1,0},{8,60,10},1,0},
    {"upper arm from behind",{3,-25,22.2f},{0,1,0},{8,-60,10},1,0},
    {"forearm across radius and ulna",{13.3f,-2.45f,46.3f},{-.876f,0,-.46f},{0,60,30},1,0},
    {"forearm from above",{25,-2.4f,37.1f},{-1,0,0},{0,60,30},1,0},
    {"hand from the front",{-9.9f,25,48.1f},{0,-1,0},{-5,60,40},1,0},
    {"hand from above",{25,-.8f,48.1f},{-1,0,0},{-5,60,40},1,0},
};
//super mutant frame spaces have the human axes, the anatomy sized by the measured spans: the torso 1.55 long and 1.81 wide,
//the head 1.21, each limb bone by its own length
static const Route kMutantHeadRoutes[]={
    {"forehead front to back",{13,40,0},{0,-1,0},{16,80,-30},1,1},
    {"temple from the side",{11,0,40},{0,0,-1},{14,25,75},1,1},
    {"jaw from the front",{3.6f,40,0},{0,-1,0},{5,80,-30},1,0},
};
static const Route kMutantTorsoRoutes[]={
    {"sternum and heart",{20.7f,50,2.9f},{0,-1,0},{28,110,-50},2,1},
    {"liver",{10.3f,50,-3.1f},{0,-1,0},{18,110,-70},1,1},
    {"abdomen",{-13,50,.5f},{0,-1,0},{-4,110,-50},0,1},
    {"lumbar spine and intestines",{-3,-50,-1},{0,1,0},{8,-110,-55},1,1},
};
static const Route kMutantLegRoutes[]={
    {"left thigh and femur",{-22,45,13},{0,-1,0},{-15,100,-20},1,0},
    {"left kneecap",{-43.9f,45,13.4f},{0,-1,0},{-38,100,-20},1,0},
    {"left shin through the tibia",{-58,45,12.9f},{0,-1,0},{-52,100,-20},1,0},
    {"left foot from the front",{-82.5f,60,13.7f},{0,-1,0},{-70,100,-20},1,0},
};
static const Route kMutantLeftArmRoutes[]={
    {"upper arm through the humerus",{9.4f,40,35.5f},{0,-1,0},{14,90,15},1,0},
    {"upper arm from behind",{9.4f,-40,35.5f},{0,1,0},{14,-90,15},1,0},
    {"forearm from the front",{-4,40,52.5f},{0,-1,0},{0,90,35},1,0},
    {"hand from the front",{-15,40,69},{0,-1,0},{-10,90,55},1,0},
};
//horse frame spaces from tools/horse_reference.py: Head, Spine1, Pelvis and Spine2 in the HAP horse's rest pose
static const Route kHorseHeadRoutes[]={
    {"skull from the side",{9.4f,-25,1.25f},{0,1,0},{4,-60,7.1f},1,0},
    {"neck from the side",{1.71f,-25,-31.7f},{0,1,0},{-4.5f,-60,-27.9f},1,0},
    {"jaw from below",{43.77f,0,-24.18f},{-.678f,0,.735f},{59.4f,-20,-23.4f},1,0},
    {"braincase from the side",{.24f,-25,2.33f},{0,1,0},{-3.8f,-60,6.7f},2,1},
};
static const Route kHorseTorsoRoutes[]={
    {"ribs from the side",{7.04f,-30,12.16f},{0,1,0},{-3.1f,-70,2.8f},2,0},
    {"shoulders through the scapulae and humeri",{5.64f,-30,42.59f},{0,1,0},{-2.6f,-70,32.7f},2,0},
    {"spine from above",{-38.1f,0,5.44f},{1,0,-.02f},{-53.5f,-25,-16.3f},1,0},
    {"hips through the femurs",{8.16f,-30,-32.97f},{0,1,0},{1.1f,-70,-37.3f},2,0},
    {"heart from the side",{9.29f,-30,24.52f},{0,1,0},{2.3f,-70,24.7f},0,1},
    {"gut from the side",{8.66f,-30,-7.48f},{0,1,0},{1.7f,-70,-7.3f},0,1},
};
static const Route kHorseLegRoutes[]={
    {"shins from the side",{-10.22f,30,-46.87f},{0,-1,0},{-9.2f,60,-44.9f},1,0},
    {"hind cannons from the side",{-15.22f,25,-66.87f},{0,-1,0},{-9.2f,55,-59.9f},1,0},
};
static const Route kHorseArmRoutes[]={
    {"forearms from the side",{37.79f,30,13.41f},{0,-1,0},{32.9f,60,17.6f},1,0},
    {"front cannons from the side",{62.74f,25,11.53f},{0,-1,0},{57.9f,55,16.7f},1,0},
};
struct Case {const char* name;uint32_t region;const char* fixture;const Route* routes;unsigned count;const char* rest=nullptr;uint8_t anatomy=HumanAnatomy;};
static const Case kCases[]={
    {"torso",TorsoRegion,"tests/output/reveal/upperbody.torso.bin",kTorsoRoutes,7},
    {"leftleg",LegsRegion,"tests/output/reveal/upperbody.leftleg.bin",kLegRoutes,4},
    {"rightleg",LegsRegion,"tests/output/reveal/upperbody.rightleg.bin",kRightLegRoutes,2},
    {"leftarm",ArmsRegion,"tests/output/reveal/upperbody.leftarm.bin",kLeftArmRoutes,4},
    {"rightarm",ArmsRegion,"tests/output/reveal/upperbody.rightarm.bin",kRightArmRoutes,2},
    //the human anatomy on a creature's own skeleton and body, tools/creature_reference.py
    {"feralhead",HeadRegion,"tests/output/reveal/feralghoul.head.bin",kFeralHeadRoutes,3,"tests/output/reveal/feralghoul.head.rest.bin"},
    {"feraltorso",TorsoRegion,"tests/output/reveal/feralghoul.torso.bin",kFeralTorsoRoutes,4,"tests/output/reveal/feralghoul.torso.rest.bin"},
    {"feralleftleg",LegsRegion,"tests/output/reveal/feralghoul.leftleg.bin",kLegRoutes,4,"tests/output/reveal/feralghoul.leftleg.rest.bin"},
    {"feralleftarm",ArmsRegion,"tests/output/reveal/feralghoul.leftarm.bin",kFeralLeftArmRoutes,6,"tests/output/reveal/feralghoul.leftarm.rest.bin"},
    {"mutanthead",HeadRegion,"tests/output/reveal/supermutant.head.bin",kMutantHeadRoutes,3,"tests/output/reveal/supermutant.head.rest.bin"},
    {"mutanttorso",TorsoRegion,"tests/output/reveal/supermutant.torso.bin",kMutantTorsoRoutes,4,"tests/output/reveal/supermutant.torso.rest.bin"},
    {"mutantleftleg",LegsRegion,"tests/output/reveal/supermutant.leftleg.bin",kMutantLegRoutes,4,"tests/output/reveal/supermutant.leftleg.rest.bin"},
    {"mutantleftarm",ArmsRegion,"tests/output/reveal/supermutant.leftarm.bin",kMutantLeftArmRoutes,4,"tests/output/reveal/supermutant.leftarm.rest.bin"},
    //the horse's own anatomy on the HAP horse body, tools/horse_reference.py
    {"horsehead",HeadRegion,"tests/output/reveal/horse.head.bin",kHorseHeadRoutes,4,"tests/output/reveal/horse.head.rest.bin",HorseAnatomy},
    {"horsetorso",TorsoRegion,"tests/output/reveal/horse.torso.bin",kHorseTorsoRoutes,6,"tests/output/reveal/horse.torso.rest.bin",HorseAnatomy},
    {"horselegs",LegsRegion,"tests/output/reveal/horse.leftleg.bin",kHorseLegRoutes,2,"tests/output/reveal/horse.leftleg.rest.bin",HorseAnatomy},
    {"horsearms",ArmsRegion,"tests/output/reveal/horse.leftarm.bin",kHorseArmRoutes,2,"tests/output/reveal/horse.leftarm.rest.bin",HorseAnatomy},
};
//the shot the renderer plans from a mapped path: anatomy space is the frame's posed space brought back through the fit
static Damage::Shot PlannedShot(const HeadFrame& f,const Impact::Path& path,const Posing& posing,uint64_t sequence) {
    Vec3 entry=Fit::Unwarp(f.fit,path.entry),exit=Fit::Unwarp(f.fit,path.exit);
    Damage::Shot shot;shot.origin=entry;shot.direction=exit-entry;shot.length=Length(shot.direction);Normalize(shot.direction);
    shot.bullet=Ammunition::Resolve(0);shot.secondsPerUnit=.002f;shot.sequence=sequence;shot.entry=path.entry;shot.exit=path.exit;shot.posing=&posing;
    return shot;
}
//planning alone, the same path the renderer plans from
void TestRegionPlanning(const Model& anatomy,unsigned which) {
    const auto& c=kCases[which];
    auto body=LoadBody(c.fixture);
    std::vector<Vec3> scene;auto record=RegionRecord(body,c.region,scene,c.rest,c.anatomy);const auto& f=record->head;
    const Model& torso=anatomy;
    Damage::Surface surface;surface.Reserve(torso);
    auto posing=Damage::PosingOf(f,Appearance{});
    unsigned index=0;
    for(unsigned n=0;n<c.count;++n) {
        const auto& route=c.routes[n];
        Vec3 direction=route.direction;Normalize(direction);Vec3 entry;
        Check(Entry(f,route.start,direction,entry),"route meets the skin");
        Impact::Sample sample;sample.valid=true;sample.point=f.head.Apply(entry);sample.direction=f.head.Rotate(direction);
        Impact::Path path;Check(Impact::Map(sample,f,path),"route maps through the body skin");
        auto shot=PlannedShot(f,path,posing,50+index);
        Damage::Seed seeds[Damage::kSets][Damage::kBoneSeeds];Damage::Cluster clusters[Damage::kClusters];Damage::Plan plan;
        for(unsigned set=0;set<Damage::kSets;++set){plan.seeds[set]=seeds[set];plan.capacity[set]=Damage::kSeedCapacity[set];}
        plan.clusters=clusters;plan.clusterCapacity=Damage::kClusters;
        auto runs=Damage::PlanWound(torso,surface,shot,plan);
        printf("%s plan %u %s: length=%.2f boneRuns=%u softRuns=%u clusters=%u seeds=%u/%u\n",c.name,index,route.name,shot.length,runs.bone,runs.brain,
            plan.clusterCount,plan.count[Damage::BoneSet],plan.count[Damage::BrainSet]);
        Check(runs.bone>=route.bone&&runs.brain>=route.soft,route.name);
        ++index;
    }
    printf("PASS: %s wound planning crosses the expected bone and soft tissue on %u routes\n",c.name,c.count);
}
//the groin routes cross the testes and penis where the pelvis holds them, the others do not
void TestGroin(const Model& torso) {
    auto body=LoadBody(kCases[0].fixture);
    std::vector<Vec3> scene;auto record=RegionRecord(body,TorsoRegion,scene);const auto& f=record->head;
    unsigned crossed=0,passing=0;
    for(unsigned n=0;n<kCases[0].count;++n) {
        const auto& route=kTorsoRoutes[n];Vec3 direction=route.direction;Normalize(direction);Vec3 entry;
        Check(Entry(f,route.start,direction,entry),"route meets the skin");
        Impact::Sample sample;sample.valid=true;sample.point=f.head.Apply(entry);sample.direction=f.head.Rotate(direction);
        Impact::Path path;Check(Impact::Map(sample,f,path),"route maps");
        bool groin=strncmp(route.name,"groin",5)==0;
        bool isNear=Groin::Near(torso,entry,direction,8),isCrossed=Groin::Crossed(torso,f,path,.15f);
        Check(isCrossed==groin,"only the groin routes cross the testes or penis");
        Check(!isCrossed||isNear,"a crossing route passes the cheap check");
        crossed+=isCrossed;passing+=isNear;
    }
	Model female;
	Check(female.Load("mod\\meshes\\XRayKillcamNVSE\\torso-female.xra"), "women's torso");
	Check(!Groin::Near(female,{-20,30,0},{0,-1,0},8),"women's torso has nothing to cross");
    printf("PASS: groin test crosses on %u of %u torso routes, %u pass the cheap check\n",crossed,kCases[0].count,passing);
}
//a route's mapped path and the shot the renderer plans from it
static Damage::Shot RouteShot(const HeadFrame& f,const Route& route,const Posing& posing,uint64_t sequence,Impact::Path& path) {
    Vec3 direction=route.direction;Normalize(direction);Vec3 entry;
    Check(Entry(f,route.start,direction,entry),"route meets the skin");
    Impact::Sample sample;sample.valid=true;sample.point=f.head.Apply(entry);sample.direction=f.head.Rotate(direction);
    Check(Impact::Map(sample,f,path),"route maps through the body skin");
    return PlannedShot(f,path,posing,sequence);
}
//a shot through or beside a testis bursts it whole, its lumps fly and blood sprays from it, shots elsewhere burst nothing
void TestRupture(const Model& torso) {
    auto body=LoadBody(kCases[0].fixture);
    std::vector<Vec3> scene;auto record=RegionRecord(body,TorsoRegion,scene);const auto& f=record->head;
    auto posing=Damage::PosingOf(f,Appearance{});
    unsigned testes=0;for(const auto& m:torso.meshes)testes+=Bursts(m.material);
    Check(testes==2,"the men's torso has two testes that burst");
    unsigned burst[8]{};Impact::Path side;
    for(unsigned n=0;n<kCases[0].count;++n) {
        Impact::Path path;auto shot=RouteShot(f,kTorsoRoutes[n],posing,300+n,path);
        Damage::Rupture ruptures[Damage::kRuptures];burst[n]=Damage::PlanRuptures(torso,shot,ruptures,Damage::kRuptures);
        for(unsigned k=0;k<burst[n];++k)Check(Bursts(torso.meshes[ruptures[k].mesh].material)&&ruptures[k].time>=0&&ruptures[k].time<=shot.length*shot.secondsPerUnit+1e-6f,
            "a burst names a testis and falls within the shot");
        if(n==5)side=path;
        printf("torso route %u %s: %u testes burst\n",n,kTorsoRoutes[n].name,burst[n]);
    }
    Check(burst[5]==2,"the side shot through both testes bursts both");
    Check(!burst[0]&&!burst[1]&&!burst[2]&&!burst[3]&&!burst[4],"shots above the groin burst nothing");
	Model female;
	Check(female.Load("mod\\meshes\\XRayKillcamNVSE\\torso-female.xra"), "women's torso");
	{Impact::Path path;auto shot=RouteShot(f,kTorsoRoutes[5],posing,320,path);Damage::Rupture none[Damage::kRuptures];
     Check(!Damage::PlanRuptures(female,shot,none,Damage::kRuptures),"women's torso has nothing to burst");}
    auto damage=std::make_unique<Damage::Scene>();damage->Reserve(torso);
    Impact::Frame frame{side,0,5,Ammunition::Resolve(0),330,0,10};frame.bullet.radius=.15f;
    damage->Update(torso,f,Appearance{},frame);
    unsigned gone=0;for(unsigned i=0;i<torso.meshes.size();++i)gone+=damage->Ruptured(i);
    Check(!gone,"the testes stay until the bullet reaches them");
    uint8_t cues[16];unsigned heard=damage->TakeCues(cues,16);
    Check(heard==1&&cues[0]==Damage::HitCue,"the bullet entering sounds at once");
    unsigned jets=0;
    for(unsigned k=0;k<Damage::kJets;++k)jets+=damage->Blood().JetAt(k).live&&damage->Blood().JetAt(k).sequence==330;
    Check(jets==5,"the shot throws blood from its entry, its exit and the mist there, and each burst testis");
    const auto& exit=damage->Blood().JetAt(1);Vec3 along=side.exit-side.entry;Normalize(along);along=f.head.Rotate(along);
    Check(exit.kind==Damage::ExitJet&&Length(exit.origin-f.head.Apply(side.exit))<1e-3f&&Dot(exit.direction,along)>.999f,"the exit jet leaves the exit wound along the shot");
    frame.age=1;frame.sceneTime=11;damage->Update(torso,f,Appearance{},frame);
    for(unsigned i=0;i<torso.meshes.size();++i)gone+=damage->Ruptured(i)&&Bursts(torso.meshes[i].material);
    const auto& report=damage->Report();
    Check(gone==2&&report.ruptures==2,"both testes are gone once the bullet has passed");
    Check(damage->Fragments().Pieces(0)>=2*Damage::kRupturePieces&&report.brainPieces>=2*Damage::kRupturePieces,"each burst testis flies as its lumps");
    heard=damage->TakeCues(cues,16);unsigned counts[Damage::kCueKinds]{};
    for(unsigned k=0;k<heard;++k)++counts[cues[k]];
    Check(!counts[Damage::HitCue]&&!counts[Damage::BoneCue]&&counts[Damage::OrganCue]==1&&counts[Damage::BurstCue]==2&&counts[Damage::SprayCue]==1
        &&cues[heard-1]==Damage::SprayCue,"then the torn tissue and each burst testis, and the blood last as the bullet leaves");
    Check(!damage->TakeCues(cues,16),"each cue sounds once");
    //three bone breaks sound as the first and the last
    Damage::Cluster three[3];three[0].time=.004f;three[1].time=.012f;three[2].time=.008f;Damage::Cue planned[Damage::kCues];
    unsigned n=Damage::PlanCues(nullptr,0,three,3,nullptr,0,.02f,planned);
    Check(n==4&&planned[0].kind==Damage::HitCue&&planned[1].kind==Damage::BoneCue&&planned[1].time==.004f&&planned[2].kind==Damage::BoneCue
        &&planned[2].time==.012f&&planned[3].kind==Damage::SprayCue,"a wound sounds its first and last bone break, in time order");
    unsigned live=0;for(unsigned k=0;k<Damage::kJets;++k)live+=damage->Blood().Live(k,10.3f);
    Check(live==5&&!damage->Blood().Any(12),"the jets fly for under a second");
    printf("PASS: testes burst on %u of %u torso routes into %u pieces with %u blood jets, and the shot's sounds come in order\n",unsigned(burst[5]!=0)+unsigned(burst[6]!=0),kCases[0].count,
        damage->Fragments().Pieces(0),jets);
}
//a killing shot through the heart: more bone loose, a wider channel, the heart bursting, and the exit pumping blood
void TestKillShot(const Model& torso) {
    auto body=LoadBody(kCases[0].fixture);
    std::vector<Vec3> scene;auto record=RegionRecord(body,TorsoRegion,scene);const auto& f=record->head;
    auto posing=Damage::PosingOf(f,Appearance{});
    Damage::Surface surface;surface.Reserve(torso);
    unsigned bone[2]{},soft[2]{},loose[2]{},bursts[2]{};float reach[2]{};
    for(unsigned k=0;k<2;++k) {
        Impact::Path path;auto shot=RouteShot(f,kTorsoRoutes[0],posing,500,path);shot.severity=k?1.8f:1;
        Damage::Seed seeds[Damage::kSets][Damage::kBoneSeeds];Damage::Cluster clusters[Damage::kClusters];Damage::Plan plan;
        for(unsigned set=0;set<Damage::kSets;++set){plan.seeds[set]=seeds[set];plan.capacity[set]=Damage::kSeedCapacity[set];}
        plan.clusters=clusters;plan.clusterCapacity=Damage::kClusters;
        Damage::PlanWound(torso,surface,shot,plan);
        bone[k]=plan.count[Damage::BoneSet];soft[k]=plan.count[Damage::BrainSet];
        for(unsigned i=0;i<bone[k];++i)loose[k]+=seeds[Damage::BoneSet][i].state==Damage::Loose;
        for(unsigned i=0;i<soft[k];++i)reach[k]=fmaxf(reach[k],seeds[Damage::BrainSet][i].reach);
        Damage::Rupture ruptures[Damage::kRuptures];bursts[k]=Damage::PlanRuptures(torso,shot,ruptures,Damage::kRuptures);
        for(unsigned r=0;r<bursts[k];++r)Check(torso.meshes[ruptures[r].mesh].material==kHeartMaterial,"only the heart bursts on the sternum route");
    }
    printf("sternum and heart: bone seeds %u -> %u (loose %u -> %u), soft seeds %u -> %u, widest tear %.2f -> %.2f, bursts %u -> %u\n",
        bone[0],bone[1],loose[0],loose[1],soft[0],soft[1],reach[0],reach[1],bursts[0],bursts[1]);
    Check(!bursts[0]&&bursts[1]==1,"the heart bursts on a killing shot only");
    Check(loose[1]>loose[0]&&reach[1]>reach[0]*1.3f,"a killing shot looses more bone and tears a wider channel");
    Damage::Spray plain,kill;Damage::Rupture heart{};heart.direction={0,-1,0};
    Vec3 entry{0,10,0},exit{0,-10,0};
    plain.Emit(Transform::Identity(),0,1,entry,exit,.04f,nullptr,0,1,1.5f);kill.Emit(Transform::Identity(),0,1,entry,exit,.04f,&heart,1,1.8f,1.5f);
    unsigned jets[2]{},drops[2]{};float rows[Damage::kSprayConstants*4];
    for(unsigned j=0;j<Damage::kJets;++j) {
        if(plain.JetAt(j).live){++jets[0];drops[0]+=plain.Constants(j,.05f,{},rows);}
        if(kill.JetAt(j).live){++jets[1];drops[1]+=kill.Constants(j,.05f,{},rows);}
    }
    printf("blood: ordinary hit %u jets %u droplets, killing shot %u jets %u droplets\n",jets[0],drops[0],jets[1],drops[1]);
    Check(jets[0]==3&&jets[1]==3+2+1&&drops[1]>drops[0]*2,"a killing shot pumps the exit twice more and throws over twice the blood");
    printf("PASS: a killing shot shatters more, tears wider, bursts the heart and bleeds harder\n");
}
//a shin swung about the knee between the hit and the first rendered frame: the shot rides the calf it hit
void TestCarrier() {
    auto body=LoadBody(kCases[1].fixture);
    std::vector<Vec3> scene;auto record=RegionRecord(body,LegsRegion,scene);auto& f=record->head;
    Vec3 start{-46,25,7},direction{0,-1,0},entry;
    Check(Entry(f,start,direction,entry),"the shin route meets the captured skin");
    //the calf's pose in the frame when captured, and its world pose at the hit, 25 degrees further forward about the knee
    Transform calf=Transform::Identity();calf.t={-31,2,7};f.posedBones[2]=calf;
    //the knee hinges about the frame's z, its left-right axis
    float angle=.44f;Transform swing={{cosf(angle),-sinf(angle),0,sinf(angle),cosf(angle),0,0,0,1},{0,0,0},1};
    Transform knee=Transform::Identity();knee.t=calf.t;
    Transform atHit=Compose(f.head,Compose(knee,Compose(swing,Compose(Inverse(knee),calf))));
    Transform moved=Compose(atHit,Inverse(Compose(f.head,calf)));
    Impact::Sample sample;sample.valid=sample.posed=true;sample.pose=f.head;
    sample.point=moved.Apply(f.head.Apply(entry));sample.direction=moved.Rotate(f.head.Rotate(direction));
    sample.carrier=2;sample.carrierPose=atHit;
    Impact::Path path;Check(Impact::Map(sample,f,path)&&Length(path.point-entry)<1e-3f&&Length(path.entry-entry)<.05f,"the carried shot lands where it hit the shin");
    sample.carrier=0;Impact::Path frozen;bool mapped=Impact::Map(sample,f,frozen);
    Check(Length(frozen.point-entry)>3,"held to the pelvis the same hit lands several units off the shin");
    printf("PASS: a shot on a swinging shin rides the calf (frame-held contact %.2f units off, %s)\n",Length(frozen.point-entry),mapped?"still crossing skin":"missing the skin");
}
//the same routes through the plugin's renderer over a depth-primed vanilla body
void TestRegionRender(IDirect3DDevice9* d,const Model& torso,unsigned which) {
    const auto& c=kCases[which];
    auto body=LoadBody(c.fixture);
    std::vector<Vec3> scene;auto record=RegionRecord(body,c.region,scene,c.rest,c.anatomy);
    //as the runtime does after each capture
    BodySkin::OpenOver(torso,record->head);
    unsigned index=0;uint64_t sequence=100;
    for(unsigned n=0;n<c.count;++n) {
        const auto& route=c.routes[n];
        auto& r=*record;Vec3 direction=route.direction;Normalize(direction);
        Vec3 entry;Check(Entry(r.head,route.start,direction,entry),"route meets the skin");
        Impact::Sample sample;sample.valid=true;sample.point=r.head.head.Apply(entry);sample.direction=r.head.head.Rotate(direction);
        Impact::Path path;Check(Impact::Map(sample,r.head,path),"route maps through the body skin");
        r.impact={path,0,5,Ammunition::Resolve(0),++sequence,0,10};r.impact.bullet.radius=.15f;
        r.camera=LookAt(r.head.head.Apply(route.eye),r.head.head.Apply((path.entry+path.exit)*.5f),512);
        r.appearance=Appearance{};
        Graphics::DamageReport report;
        const float ages[]={.03f,.1f,1.0f};
        for(unsigned a=0;a<3;++a) {
            r.impact.age=ages[a];r.impact.sceneTime=10+ages[a];
            auto pixels=Probe::Render(d,torso,r,&report,&scene);
            char name[MAX_PATH];sprintf_s(name,"tests/output/%s-route-%u-%u.ppm",c.name,index,a);SavePixels(pixels,512,name);
        }
        Vec3 middle=(path.entry+path.exit)*.5f;
        r.camera=LookAt(r.head.head.Apply(middle+(route.eye-middle)*.35f),r.head.head.Apply(middle),512);
        r.impact.age=1;r.impact.sceneTime=11;
        char close[MAX_PATH];sprintf_s(close,"tests/output/%s-route-%u-close.ppm",c.name,index);SavePixels(Probe::Render(d,torso,r,nullptr,&scene),512,close);
        printf("%s route %u %s: length=%.2f boneRuns=%u softRuns=%u breaks=%u/%u/%u plates=%u fragments=%u pieces=%u\n",c.name,index,route.name,
            Length(path.exit-path.entry),report.boneRuns,report.brainRuns,report.entry,report.inner,report.exit,report.plates,report.fragments,report.brainPieces);
        Check(report.boneRuns>=route.bone,"rendered route breaks the bone it crosses");
        ++index;
    }
    printf("PASS: %s anatomy maps, breaks and tears along %u routes through the vanilla body\n",c.name,c.count);
}
//a groin shot reveals the skin over the genitals only, and its blood shows beyond the skin
void TestGroinRender(IDirect3DDevice9* d,const Model& torso) {
    auto body=LoadBody(kCases[0].fixture);
    std::vector<Vec3> scene;
    for(unsigned n=5;n<7;++n) {
        const auto& route=kTorsoRoutes[n];
        auto whole=RegionRecord(body,TorsoRegion,scene);auto focused=RegionRecord(body,TorsoRegion,scene);auto closed=RegionRecord(body,TorsoRegion,scene);
        BodySkin::OpenOver(torso,whole->head);BodySkin::OpenOver(torso,focused->head,true);
        for(uint32_t i=0;i<closed->head.skinVertices;++i)closed->head.skin[i].reveal=0;
        Vec3 direction=route.direction;Normalize(direction);Vec3 entry;
        Check(Entry(whole->head,route.start,direction,entry),"groin route meets the skin");
        Impact::Sample sample;sample.valid=true;sample.point=whole->head.head.Apply(entry);sample.direction=whole->head.head.Rotate(direction);
        Impact::Path path;Check(Impact::Map(sample,whole->head,path),"groin route maps");
        Vec3 middle=(path.entry+path.exit)*.5f;
        auto camera=LookAt(whole->head.head.Apply(route.eye),whole->head.head.Apply(middle),512);
        for(auto* r:{whole.get(),focused.get(),closed.get()}) {
            r->impact={path,1,5,Ammunition::Resolve(0),uint64_t(400+n),0,11};r->impact.bullet.radius=.15f;r->camera=camera;r->appearance=Appearance{};
        }
        closed->impact.path.valid=false;
        auto base=Probe::Render(d,torso,*closed,nullptr,&scene);
        auto count=[&](const std::vector<DWORD>& pixels){unsigned changed=0;for(size_t i=0;i<pixels.size();++i)changed+=pixels[i]!=base[i];return changed;};
        auto all=Probe::Render(d,torso,*whole,nullptr,&scene);unsigned wide=count(all);
        char wideName[MAX_PATH];sprintf_s(wideName,"tests/output/groin-route-%u-torso.ppm",n);SavePixels(all,512,wideName);
        unsigned narrow=0;
        const float ages[]={.03f,.08f,1.0f};
        for(unsigned a=0;a<3;++a) {
            focused->impact.age=ages[a];focused->impact.sceneTime=10+ages[a];
            auto pixels=Probe::Render(d,torso,*focused,nullptr,&scene);
            char name[MAX_PATH];sprintf_s(name,"tests/output/groin-route-%u-%u.ppm",n,a);SavePixels(pixels,512,name);
            if(a==2)narrow=count(pixels);
        }
        //the killcam's close-up distance
        focused->camera=LookAt(whole->head.head.Apply(middle+(route.eye-middle)*(28/Length(route.eye-middle))),whole->head.head.Apply(middle),512);
        focused->impact.age=.06f;focused->impact.sceneTime=10.06f;
        char closeName[MAX_PATH];sprintf_s(closeName,"tests/output/groin-route-%u-close.ppm",n);SavePixels(Probe::Render(d,torso,*focused,nullptr,&scene),512,closeName);
        focused->camera=camera;
        printf("groin route %u %s: whole torso reveal changes %u pixels, the groin window %u\n",n,route.name,wide,narrow);
        Check(narrow>1500&&narrow*2<wide,"a groin shot opens a window over the genitals, well inside the torso's reveal");
        //blood in flight at the game's default amount, with and without the spray
        focused->impact.age=.08f;focused->impact.sceneTime=10.08f;Graphics::SetShotForce(1,1.5f);
        auto wet=Probe::Render(d,torso,*focused,nullptr,&scene);
        Graphics::SetBloodSpray(false);auto dry=Probe::Render(d,torso,*focused,nullptr,&scene);Graphics::SetBloodSpray(true);
        unsigned red=0;
        for(size_t i=0;i<wet.size();++i) {
            unsigned r=(wet[i]>>16)&255,g=(wet[i]>>8)&255,b=wet[i]&255;
            red+=wet[i]!=dry[i]&&r>g*2&&r>b*2;
        }
        Graphics::SetShotForce(1,1);
        printf("groin route %u: %u blood pixels in flight\n",n,red);
        Check(red>200,"the spray shows as red droplets");
    }
    printf("PASS: groin shots reveal only the genitals and throw visible blood\n");
}
//the same shot through the sternum and heart as an ordinary hit and as a killing one, at the killcam's distance
void TestKillRender(IDirect3DDevice9* d,const Model& torso) {
    auto body=LoadBody(kCases[0].fixture);
    std::vector<Vec3> scene;auto record=RegionRecord(body,TorsoRegion,scene);auto& r=*record;
    BodySkin::OpenOver(torso,r.head);
    const auto& route=kTorsoRoutes[0];Vec3 direction=route.direction;Normalize(direction);Vec3 entry;
    Check(Entry(r.head,route.start,direction,entry),"sternum route meets the skin");
    Impact::Sample sample;sample.valid=true;sample.point=r.head.head.Apply(entry);sample.direction=r.head.head.Rotate(direction);
    Impact::Path path;Check(Impact::Map(sample,r.head,path),"sternum route maps");
    Vec3 middle=(path.entry+path.exit)*.5f,eye=middle+direction*-28.f+Vec3{6,0,-6};
    r.camera=LookAt(r.head.head.Apply(eye),r.head.head.Apply(middle),512);r.appearance=Appearance{};
    const float ages[]={.02f,.05f,.12f};unsigned changed[2]{};std::vector<DWORD> first[3];
    for(unsigned k=0;k<2;++k) {
        Graphics::SetShotForce(k?1.8f:1,1.5f);
        for(unsigned a=0;a<3;++a) {
            r.impact={path,ages[a],5,Ammunition::Resolve(0),uint64_t(600+k),0,10+ages[a]};r.impact.bullet.radius=.15f;
            Graphics::DamageReport report;auto pixels=Probe::Render(d,torso,r,&report,&scene);
            char name[MAX_PATH];sprintf_s(name,"tests/output/kill-heart-%s-%u.ppm",k?"kill":"hit",a);SavePixels(pixels,512,name);
            if(!k)first[a]=pixels;else for(size_t i=0;i<pixels.size();++i)changed[1]+=pixels[i]!=first[a][i];
            if(a==2)printf("sternum %s: fragments=%u pieces=%u ruptured=%u vertices=%u\n",k?"kill":"hit",report.fragments,report.brainPieces,report.ruptures,report.vertices);
            if(a==2&&k)Check(report.ruptures==1,"the killing shot bursts the heart");
        }
    }
    Graphics::SetShotForce(1,1);
    Check(changed[1]>20000,"the killing shot looks different from the ordinary hit");
    printf("PASS: a killing shot through the heart renders harder than the same ordinary hit (%u pixels differ)\n",changed[1]);
}
//from the close-up straight behind the entry, a killing shot's blood shows: what flies out the back through the window,
//and the occlusion counts the runtime logs see it
void TestBloodSeen(IDirect3DDevice9* d,const Model& torso) {
    auto body=LoadBody(kCases[0].fixture);
    std::vector<Vec3> scene;auto record=RegionRecord(body,TorsoRegion,scene);auto& r=*record;
    BodySkin::OpenOver(torso,r.head);
    const auto& route=kTorsoRoutes[0];Vec3 direction=route.direction;Normalize(direction);Vec3 entry;
    Check(Entry(r.head,route.start,direction,entry),"sternum route meets the skin");
    Impact::Sample sample;sample.valid=true;sample.point=r.head.head.Apply(entry);sample.direction=r.head.head.Rotate(direction);
    Impact::Path path;Check(Impact::Map(sample,r.head,path),"sternum route maps");
    r.camera=LookAt(r.head.head.Apply(path.entry+direction*-28.f+Vec3{0,0,5.6f}),r.head.head.Apply(path.entry),512);r.appearance=Appearance{};
    Graphics::SetShotForce(1.8f,1.5f);
    const float ages[]={.02f,.04f,.06f,.08f,.1f};
    r.impact={path,.02f,5,Ammunition::Resolve(0),800,0,10.02f};r.impact.bullet.radius=.15f;
    auto wet=Probe::RenderFrames(d,torso,r,ages,5,&scene);
    auto report=Graphics::BloodInFlight();
    SavePixels(wet,512,"tests/output/blood-close.ppm");
    Graphics::SetBloodSpray(false);auto dry=Probe::Render(d,torso,r,nullptr,&scene);Graphics::SetBloodSpray(true);
    Graphics::SetShotForce(1,1);
    unsigned red=0;
    for(size_t i=0;i<wet.size();++i) {
        unsigned cr=(wet[i]>>16)&255,cg=(wet[i]>>8)&255,cb=wet[i]&255;
        red+=wet[i]!=dry[i]&&cr>cg*2&&cr>cb*2;
    }
    printf("close-up kill blood: %u red pixels; jets %u/%u/%u/%u droplets %u/%u/%u/%u; counted %u frames, at most %u outside and %u inside the window; %.0f units, %.0fdeg, entry drops %.1f px\n",
        red,report.jets[0],report.jets[1],report.jets[2],report.jets[3],report.droplets[0],report.droplets[1],report.droplets[2],report.droplets[3],
        report.measured,report.outside,report.inside,report.distance,report.angle,report.dropPixels);
    Check(report.sequence==800&&report.jets[0]==1&&report.jets[1]==3&&report.jets[3]==1,"the report counts the shot's jets");
    Check(!report.counted||(report.measured>0&&report.inside>500),"blood flying out the back is counted inside the window");
    Check(red>1500,"the close-up shows the killing shot's blood");
    Check(report.distance>27&&report.distance<30&&report.angle<15,"the report places the camera behind the entry");
    printf("PASS: a killing shot's blood shows from behind the entry and the occlusion counts see it\n");
}
//a part brought back from memory draws the same settled wound as the shot left to settle
void TestRestoreRender(IDirect3DDevice9* d,const Model& torso) {
    auto body=LoadBody(kCases[0].fixture);
    std::vector<Vec3> scene;auto record=RegionRecord(body,TorsoRegion,scene);auto& r=*record;
    BodySkin::OpenOver(torso,r.head);
    const auto& route=kTorsoRoutes[0];Vec3 direction=route.direction;Normalize(direction);Vec3 entry;
    Check(Entry(r.head,route.start,direction,entry),"sternum route meets the skin");
    Impact::Sample sample;sample.valid=true;sample.point=r.head.head.Apply(entry);sample.direction=r.head.head.Rotate(direction);
    Impact::Path path;Check(Impact::Map(sample,r.head,path),"sternum route maps");
    Vec3 middle=(path.entry+path.exit)*.5f;
    r.camera=LookAt(r.head.head.Apply(middle+direction*-28.f+Vec3{6,0,-6}),r.head.head.Apply(middle),512);r.appearance=Appearance{};
    r.impact={path,3,5,Ammunition::Resolve(0),700,0,13};r.impact.bullet.radius=.15f;
    auto settled=Probe::Render(d,torso,r,nullptr,&scene);
    auto plan=std::make_unique<Damage::Scene>();plan->Reserve(torso);plan->Update(torso,r.head,Appearance{},r.impact);
    std::vector<Damage::Kept> kept;plan->Keep(kept);
    Graphics::RestoreWounds(TorsoRegion,kept);
    r.impact.path.valid=false;r.impact.sequence=701;
    auto back=Probe::Render(d,torso,r,nullptr,&scene);
    unsigned different=0,shown=0;
    for(size_t i=0;i<back.size();++i){different+=back[i]!=settled[i];shown+=settled[i]!=0xFF302C27;}
    SavePixels(settled,512,"tests/output/restore-settled.ppm");SavePixels(back,512,"tests/output/restore-back.ppm");
    printf("brought-back torso wound: %u of %u pixels differ from the settled shot\n",different,shown);
    Check(shown>20000&&different<shown/100,"a wound brought back draws as the settled shot");
    printf("PASS: a part brought back with its kept wound draws the same settled damage\n");
}
void TestTorso(IDirect3DDevice9* d,const Model& torso) {
    unsigned bone=0,cartilage=0,soft[kSoftKinds]{};
    for(const auto& m:torso.meshes) {
        Check(m.first<kInfluences&&m.second<kInfluences&&m.visibility==3,"torso meshes bind to known influences and show in both modes");
        bone+=m.material==kBoneMaterial;cartilage+=m.material==kCartilageMaterial;
        if(SoftKind(m.material)!=kNoFracture)++soft[SoftKind(m.material)];
    }
    Check(bone>=40&&cartilage==14&&soft[0]==1&&soft[1]==1&&soft[2]==2&&soft[3]==1&&soft[4]==2&&soft[5]==2&&soft[6]==2&&soft[7]==1,
        "skeleton, costal cartilage, cord, heart, lungs, liver, stomach and intestines, kidneys, testes and penis");
    //women's torso is the same anatomy without the external genitals
	Model female;
	Check(female.Load("mod\\meshes\\XRayKillcamNVSE\\torso-female.xra"), "women's torso loads");
	unsigned genital=0;for(const auto& m:female.meshes)genital+=m.material==kTestisMaterial||m.material==kPenisMaterial;
    Check(!genital&&female.meshes.size()+3==torso.meshes.size(),"women's torso leaves out exactly the testes and penis");
    printf("PASS: men's torso %zu batches, women's %zu without the genitals\n",torso.meshes.size(),female.meshes.size());
    TestRegionRender(d,torso,0);
    auto body=LoadBody(kCases[0].fixture);
    std::vector<Vec3> scene;auto record=RegionRecord(body,TorsoRegion,scene);
    //every influence bent differently, the shader blend must match the CPU placement the damage code uses
    Influences bent=RestInfluences();
    for(unsigned k=1;k<kInfluences;++k) {
        Vec3 axis{float(k%3==0),float(k%3==1),float(k%3==2)};float a=(8.0f+4*float(k))*.01745329252f,c=cosf(a),si=sinf(a),one=1-c;
        Transform t{{c+axis.x*axis.x*one,axis.x*axis.y*one-axis.z*si,axis.x*axis.z*one+axis.y*si,axis.y*axis.x*one+axis.z*si,c+axis.y*axis.y*one,
            axis.y*axis.z*one-axis.x*si,axis.z*axis.x*one-axis.y*si,axis.z*axis.y*one+axis.x*si,c+axis.z*axis.z*one},{.3f*float(k),-.2f,.1f*float(k)},1};
        bent[k]=t;
    }
    auto& r=*record;r.impact={};r.head.influences=bent;
    //a wide skin sheet in front of the chest reveals every pixel it covers
    const Vec3 sheet[]={{-30,25,-30},{30,25,-30},{30,25,30},{-30,25,30}};const uint16_t quad[]={0,1,2,0,2,3};
    r.head.skinVertices=4;r.head.skinTriangles=2;memcpy(r.head.skinIndices,quad,sizeof quad);
    for(unsigned i=0;i<4;++i)r.head.skin[i]={r.head.head.Apply(sheet[i]),sheet[i],1};
    r.camera=LookAt(r.head.head.Apply({14,60,-20}),r.head.head.Apply({4,0,0}),512);
    auto gpu=Probe::Render(d,torso,r);
    Model placed=torso;Posing posing{HeadFit{},1,{}};posing.influences=bent;
    for(auto& m:placed.meshes)for(auto& v:m.vertices){v.position=posing.Place(v.position,v.bind,v.bones,&v.normal);v.bind=1;v.bones=0;}
    r.head.influences=RestInfluences();
    auto cpu=Probe::Render(d,placed,r);
    SavePixels(gpu,512,"tests/output/torso-bent-gpu.ppm");SavePixels(cpu,512,"tests/output/torso-bent-cpu.ppm");
    unsigned different=0,anatomy=0;
    for(size_t i=0;i<gpu.size();++i) {
        bool mismatch=false;
        for(unsigned channel=0;channel<3;++channel)mismatch|=abs(int((gpu[i]>>(channel*8))&255)-int((cpu[i]>>(channel*8))&255))>2;
        different+=mismatch;anatomy+=gpu[i]!=0xFF302C27;
    }
    printf("torso GPU/CPU influence blend: %u of %u covered pixels differ by more than 2/255\n",different,anatomy);
    Check(anatomy>20000&&different<anatomy/500,"shader blends all seven torso influences like the CPU placement");
    printf("PASS: GPU torso influence blend matches CPU placement\n");
    //a creature's proportions: the frame's scale and girth in the fit, each influence scaled about its bone
    Proportions large;for(unsigned k=0;k<kInfluences;++k)large.scale[k]=1.3f+.05f*float(k);large.girth={1,1.15f,1.15f};
    Influences grown=bent;for(unsigned k=1;k<kInfluences;++k)grown[k]=Compose(bent[k],Creature::Retarget(HumanAnatomy,TorsoRegion,k,large));
    r.head.fit=Creature::Fit(large);r.head.influences=grown;
    r.camera=LookAt(r.head.head.Apply({20,90,-30}),r.head.head.Apply({6,0,0}),512);
    gpu=Probe::Render(d,torso,r);
    placed=torso;Posing scaled{r.head.fit,1,{}};scaled.influences=grown;
    for(auto& m:placed.meshes)for(auto& v:m.vertices){v.position=scaled.Place(v.position,v.bind,v.bones,&v.normal);v.bind=1;v.bones=0;}
    r.head.fit=HeadFit{};r.head.influences=RestInfluences();
    cpu=Probe::Render(d,placed,r);
    SavePixels(gpu,512,"tests/output/torso-creature-gpu.ppm");SavePixels(cpu,512,"tests/output/torso-creature-cpu.ppm");
    different=anatomy=0;
    for(size_t i=0;i<gpu.size();++i) {
        bool mismatch=false;
        for(unsigned channel=0;channel<3;++channel)mismatch|=abs(int((gpu[i]>>(channel*8))&255)-int((cpu[i]>>(channel*8))&255))>2;
        different+=mismatch;anatomy+=gpu[i]!=0xFF302C27;
    }
    printf("torso GPU/CPU creature fit and scaled influences: %u of %u covered pixels differ by more than 2/255\n",different,anatomy);
    Check(anatomy>20000&&different<anatomy/500,"shader applies a creature's fit and scaled influences like the CPU placement");
    printf("PASS: GPU creature torso placement matches CPU placement\n");
}
