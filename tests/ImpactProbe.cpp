#include "../src/Graphics.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
using namespace XRay;
unsigned checks=0;
void Check(bool pass,const char* name){++checks;if(!pass){printf("FAIL: %s\n",name);exit(1);}}
bool Near(Vec3 a,Vec3 b){return Length(a-b)<.0001f;}
HeadFrame Cube() {
    HeadFrame f{};f.head=Transform::Identity();
    const Vec3 points[]={{-4,-4,-4},{4,-4,-4},{4,4,-4},{-4,4,-4},{-4,-4,4},{4,-4,4},{4,4,4},{-4,4,4}};
    for(unsigned i=0;i<8;++i)f.vertices[i]={points[i],points[i]};
    const uint16_t indices[]={0,1,2,0,2,3,4,6,5,4,7,6,0,4,5,0,5,1,3,2,6,3,6,7,0,3,7,0,7,4,1,5,6,1,6,2};
    memcpy(f.indices,indices,sizeof indices);return f;
}
}
void TestImpact() {
    using namespace XRay;
    auto head=Cube();Impact::Sample shot;shot.valid=true;shot.point={0,-4,0};shot.direction={0,1,0};
    Impact::Path path;
    Check(Impact::Map(shot,head,path)&&Near(path.entry,{0,-4,0})&&Near(path.exit,{0,4,0}),"entry and exit from display surface along forward projectile direction");
    shot.direction={0,-1,0};shot.point={0,4,0};
    Check(Impact::Map(shot,head,path)&&Near(path.entry,{0,4,0})&&Near(path.exit,{0,-4,0}),"reverse shot swaps entry and exit");
    shot.point={10,-4,0};shot.direction={0,1,0};Check(!Impact::Map(shot,head,path),"surface miss does not fabricate centre shot");
    shot.point={0,-4,0};shot.direction={0,0,0};Check(!Impact::Map(shot,head,path),"zero direction rejected");
    shot.direction={0,std::numeric_limits<float>::quiet_NaN(),0};Check(!Impact::Map(shot,head,path),"nonfinite direction rejected");
    shot.direction={0,1,0};shot.point={0,-100,0};Check(!Impact::Map(shot,head,path),"remote contact rejected");
    //the face ends at the collar, so a neck shot leaves through skin the face mesh does not have
    auto open=Cube();const uint16_t nape[]={3,2,6,3,6,7};
    for(unsigned i=18;i<24;++i)open.indices[i]=0;
    Impact::Sample neck;neck.valid=true;neck.point={1,-4,-1};neck.direction={0,1,0};
    Check(!Impact::Map(neck,open,path),"an open collar gives one crossing and no path");
    for(unsigned i=0;i<8;++i)open.skin[i]=open.vertices[i];
    open.skinVertices=8;open.skinTriangles=2;memcpy(open.skinIndices,nape,sizeof nape);
    Check(Impact::Map(neck,open,path)&&Near(path.entry,{1,-4,-1})&&Near(path.exit,{1,4,-1}),"neck skin closes the path through the back of the neck");
    open.skinIndices[4]=8;Check(!Impact::Map(neck,open,path),"neck skin index outside its vertices rejected");
    open.skinIndices[4]=6;open.skinTriangles=kSkinTriangles+1;Check(!Impact::Map(neck,open,path),"neck skin over capacity rejected");
    Impact::Sample observed;observed.valid=true;observed.traced=true;observed.point={5,-4,0};observed.direction={0,1,0};observed.rayOrigin={2,-30,1};
    Check(Impact::Map(observed,head,path)&&Near(path.entry,{2,-4,1})&&Near(path.exit,{2,4,1}),"observed ray fixes displaced collision contact without inventing a new direction");
    Check(path.traced&&Near(path.point,observed.point)&&fabsf(path.correction-sqrtf(10))<.0001f,"diagnostics retain raw contact and perpendicular correction");
    observed.traced=false;Check(!Impact::Map(observed,head,path),"negative control: old contact-plus-heading line misses the same face");
    observed.traced=true;observed.posed=true;observed.pose=Transform::Identity();observed.pose.t={-67820,3450,8500};observed.pose.scale=1.25f;
    const float rayPose[]={0,1,0,0,0,1,1,0,0};memcpy(observed.pose.r,rayPose,sizeof rayPose);
    observed.point=observed.pose.Apply({5,-4,0});observed.rayOrigin=observed.pose.Apply({2,-30,1});observed.direction=observed.pose.Rotate({0,1,0});
    Check(Impact::Map(observed,head,path)&&Length(path.entry-Vec3{2,-4,1})<.01f&&Length(path.exit-Vec3{2,4,1})<.01f,"real-coordinate ray survives head rotation, scale and distant world position");
    observed.rayOrigin.x=std::numeric_limits<float>::quiet_NaN();Check(!Impact::Map(observed,head,path),"nonfinite observed origin rejected");
    //point-blank: the muzzle query starts past the face, so its contact came from another native query
    Impact::Sample muzzle;muzzle.valid=true;muzzle.traced=true;muzzle.point={0,-4,0};muzzle.direction={0,1,0};muzzle.rayOrigin={3,2,0};
    Check(Impact::Map(muzzle,head,path)&&Near(path.entry,{0,-4,0})&&Near(path.exit,{0,4,0})&&path.correction==0,"contact behind the observed query start keeps the native contact");
    shot.posed=true;shot.pose=Transform::Identity();shot.pose.t={300,-90,45};shot.pose.scale=2;
    const float rotate[]={0,-1,0,1,0,0,0,0,1};memcpy(shot.pose.r,rotate,sizeof rotate);
    shot.point=shot.pose.Apply({1,-4,2});shot.direction=shot.pose.Rotate({0,1,0});
    head.head.t={-900,40,0};
    Check(Impact::Map(shot,head,path)&&Near(path.entry,{1,-4,2})&&Near(path.exit,{1,4,2}),"impact-time pose survives later head translation/rotation with scaled actor");
    Impact::Playback playback;playback.Start(0x123,shot,1000,10);Impact::Frame frame;
    Check(playback.Snapshot(0x456,head,1010,10.01f,frame)==0&&!frame.path.valid,"stale victim cannot consume or cache a newer victim's shot");
    Impact::Sample captured;
    Check(playback.Snapshot(0x123,head,1010,10.01f,frame,&captured)==1&&Near(frame.path.entry,path.entry),"playback maps once from captured impact pose");
    Check(captured.posed&&Near(captured.point,shot.point)&&Near(captured.direction,shot.direction)
        &&!memcmp(&captured.pose,&shot.pose,sizeof shot.pose),"audit snapshot copies the same impact-time pose and ray used by mapping");
    Check(fabsf(frame.age-.01f)<.00001f&&fabsf(frame.realAge-.01f)<.00001f,"animation and reveal have explicit simulation and wall clocks");
    Impact::Report report;Check(playback.TakeReport(report)&&report.frame.path.valid&&report.frame.sequence==frame.sequence,"mapped path report contains the displayed sequence");
    Check(report.initial&&report.stage==0&&fabsf(report.headTurn-90)<.01f,"first report measures rotation relative to the recorded world heading");
    Check(fabsf(report.entryShift-Length(head.head.Apply(path.entry)-shot.pose.Apply(path.entry)))<.01f,"entry shift measures world movement from impact pose");
    Check(!playback.TakeReport(report),"trajectory report is consumed once rather than logged every frame");
    head.vertices[0].local={100,100,100};head.head.t={2000,4000,6000};
    playback.Snapshot(0x123,head,1015,10.015f,frame);Check(Near(frame.path.entry,path.entry),"mapped path stays attached rather than remapping against moving geometry");
    playback.Snapshot(0x123,head,1025,10.025f,frame);
    Check(playback.TakeReport(report)&&!report.initial&&report.stage==1,"middle-of-traversal report measures pose after recoil");
    playback.Snapshot(0x123,head,1030,10.03f,frame);Check(!playback.TakeReport(report),"pose diagnostics do not log each rendered frame");
    playback.Snapshot(0x123,head,1045,10.045f,frame);Check(playback.TakeReport(report)&&report.stage==2,"exit report measures final attached-path pose");
    playback.Snapshot(0x123,head,1050,10.05f,frame);Check(!playback.TakeReport(report),"exit diagnostics are emitted once");
    playback.Clear();Check(playback.Snapshot(0x123,head,1060,10.06f,frame)==0&&!frame.path.valid,"reset discards previous penetration");
    playback.Start(0x456,{},2000,11);Check(playback.Snapshot(0x456,head,2200,11.2f,frame)==-2&&!frame.path.valid,"missing impact retains full-head fallback");
    playback.Start(0x123,shot,3000,12);playback.Snapshot(0x123,head,3000,12,frame);
    Check(frame.age==0&&frame.path.valid&&Impact::Distance(frame)==0,"new hit starts at entry immediately without a pre-roll");
    {
        //at impact speed 0.3 the bullet takes 1/0.3 of the game's time to cross, then the clock eases back to the game's pace
        Impact::Playback slow;slow.SetImpactSpeed(.3f);slow.Start(0x123,shot,5000,20);Impact::Frame f;
        Check(slow.Snapshot(0x123,head,5000,20,f)==1&&f.path.valid&&f.age==0,"slowed playback starts at the entry");
        float crossing=Impact::BulletEnd(f),sim=20,crossed=-1,eased=-1;
        for(unsigned k=1;k<=4000&&eased<0;++k) {
            sim=20+k*.001f;float before=f.age,scene=f.sceneTime;slow.Snapshot(0x123,head,5000,sim,f);
            Check(f.age>before&&f.sceneTime>scene,"the X-ray clock only moves forward");
            if(crossed<0&&f.age>=crossing)crossed=sim-20;
            if(f.age>crossing+Impact::kImpactLinger+Impact::kImpactEase)eased=sim-20;
        }
        printf("impact at 0.3: bullet out after %.3f s of game time (crossing %.3f s), the game's pace again after %.3f s\n",crossed,crossing,eased);
        Check(fabsf(crossed-crossing/.3f)<.01f,"the bullet's crossing plays at the impact speed");
        float age=f.age;slow.Snapshot(0x123,head,5000,sim+.1f,f);
        Check(fabsf(f.age-age-.1f)<.0005f,"after the impact the X-ray keeps the game's pace");
        float scene=f.sceneTime;slow.Start(0x123,shot,5000,sim+1.1f);slow.Snapshot(0x123,head,5000,sim+1.1f,f);
        Check(f.age==0&&fabsf(f.sceneTime-scene-1)<.001f,"a new shot restarts its age while the shared clock carries on through the gap");
        Check(Impact::ClockRate(0,crossing,1)==1&&Impact::ClockRate(0,crossing,.3f)==.3f&&Impact::ClockRate(10,crossing,.3f)==1,"impact speed 1 leaves the game's pace");
    }
    frame={path,.02f,5};Vertex vertices[Impact::kBulletVertices];
    unsigned count=Impact::BuildBullet(frame,vertices,1);Check(count==Impact::kBulletVertices,"fixed complete bullet geometry");
    Vec3 expected=path.entry+(path.exit-path.entry)*Impact::Progress(frame);
    Check(Near(vertices[0].position,expected),"bullet tip follows measured segment at playback progress");
    for(unsigned i=0;i<count;++i)Check(Finite(vertices[i].position)&&fabsf(Length(vertices[i].normal)-1)<.0001f,"finite bullet vertices and unit normals");
    frame.age=Impact::BulletEnd(frame)+.01f;Check(!Impact::BuildBullet(frame,vertices,1),"projectile disappears after exiting");
    frame.age=-1;Check(!Impact::BuildBullet(frame,vertices,1),"negative playback time rejected");
    frame.age=std::numeric_limits<float>::quiet_NaN();Check(!Impact::BuildBullet(frame,vertices,1),"nonfinite playback time rejected");
    frame.path.valid=false;frame.age=.5f;Check(!Impact::BuildBullet(frame,vertices,1),"invalid path produces no cosmetic bullet");
    {Impact::Frame wide=frame;wide.path.valid=true;wide.path.exit=wide.path.entry+Vec3{0,60,0};wide.age=.01f;
     Check(!Impact::BuildBullet(wide,vertices,1)&&Impact::BuildBullet(wide,vertices,2)==Impact::kBulletVertices,"a 60 unit path is too long for a human body, not for one twice its size");}
    frame={path,.02f,5};head.head=Transform::Identity();CameraFrame camera{};
    for(unsigned i=0;i<4;++i)camera.view[i*5]=1;
    camera.origin={40,0,2};auto normal=Impact::SectionNormal(frame,head,camera);
    Check(Near(normal,{1,0,0}),"side-on section faces viewer");
    camera.origin={1,-20,2};normal=Impact::SectionNormal(frame,head,camera);
    Check(Near(normal,{0,-1,0}),"end-on view normal");
    camera.origin={0,0,0};camera.view[12]=-40;camera.view[14]=-2;
    Check(Near(Impact::SectionNormal(frame,head,camera),{1,0,0}),"camera translation reconstructed from row-vector view matrix");
    camera.projection[15]=1;
    Check(Near(Impact::SectionNormal(frame,head,camera),{0,0,-1}),"orthographic views use parallel rays");
    camera.projection[15]=0;
    const uint32_t ammoIDs[]={0x7EA27,0x6B53C,0x8ED02,0x2937E,0x8ECFF,0x6B53D,0x4240,0x8ED03,0x4241,0x1429CF,0x121133};
    auto pistol=Ammunition::Resolve(0x8ED03),rifle=Ammunition::Resolve(0x6B53C),heavy=Ammunition::Resolve(0x8ECFF);
    Check(rifle.radius<pistol.radius&&rifle.length>pistol.length,"308 is narrower but longer than 9mm");
    Check(heavy.radius>pistol.radius&&heavy.length>rifle.length,"50 MG has a larger diameter and longer body");
    Check(Ammunition::Resolve(0x13E43A).radius==pistol.radius,"9mm hollow point retains its calibre");
    Check(!Ammunition::Resolve(0xDEADBEEF).known&&!Ammunition::Resolve(0).known,"unknown and missing ammo use an explicit generic profile");
    Check(!Ammunition::Resolve(0x0108ED03).known,"other plugins' local ids cannot accidentally alias vanilla ammo");
    for(auto id:ammoIDs) {
        frame.bullet=Ammunition::Resolve(id);
        Check(frame.bullet.known&&frame.bullet.Section()<=1.5f*frame.bullet.radius,"every supported calibre has a proportionate channel");
        count=Impact::BuildBullet(frame,vertices,1);
        float radius=0,back=0;
        Vec3 forward=path.exit-path.entry;Normalize(forward);
        for(unsigned i=0;i<count;++i) {
            Vec3 delta=vertices[i].position-expected;float along=Dot(delta,forward);
            radius=fmaxf(radius,Length(delta-forward*along));back=fmaxf(back,-along);
        }
        Check(fabsf(radius-frame.bullet.radius)<.0001f&&fabsf(back-frame.bullet.length)<.0001f,"generated bullet dimensions match the ammo profile");
        frame.age=Impact::BulletEnd(frame)-.00001f;
        count=Impact::BuildBullet(frame,vertices,1);
        float minimum=1e20f;
        for(unsigned i=0;i<count;++i)minimum=fminf(minimum,Dot(vertices[i].position-frame.path.exit,forward));
        Check(count&&minimum>3.9f,"whole bullet clears the exit before disappearing");
        frame.age=.02f;
    }

    playback.Clear();shot.ammo=0x8ECFF;shot.bullet=heavy;playback.Start(0x123,shot,5000,20);
    shot.ammo=0x8ED03;shot.bullet=pistol;
    playback.Snapshot(0x123,head,5010,20.01f,frame);
    Check(frame.bullet.radius==heavy.radius&&frame.bullet.length==heavy.length,"playback owns its ammo dimensions after the source sample changes");
    auto first=frame.sequence;
    auto until=playback.Start(0x123,shot,5020,20.02f);
    playback.Snapshot(0x123,head,5020,20.02f,frame);
    Check(frame.sequence!=first&&frame.bullet.radius==pistol.radius&&frame.age==0,"same-victim shot replaces an active traversal immediately");
    Check(until==10020&&frame.duration==5,"new hit receives its own reveal deadline without a queued extension");
    first=frame.sequence;playback.Snapshot(0x123,head,8020,23.02f,frame);
    Check(frame.sequence==first&&frame.age>2.9f,"no stale shot replays after the former queue delay");
    playback.Start(0x456,shot,8100,23.1f);playback.Snapshot(0x456,head,8100,23.1f,frame);
    Check(frame.age==0,"another victim switches immediately");
    playback.Clear();Check(!playback.Snapshot(0x456,head,8200,23.2f,frame),"clear drops current playback");
    Check(!playback.TakeReport(report),"clear also discards stale trajectory reports");
    playback.Start(0x123,shot,13000,25);playback.Snapshot(0x123,head,13005,25.005f,frame);playback.Snapshot(0x123,head,13025,25.025f,frame);
    Check(playback.TakeReport(report)&&report.initial&&report.stage==1,"delayed report consumption retains initial path while replacing pose measurements");
    head=Cube();shot.posed=false;shot.point={0,-4,0};shot.direction={0,1,0};
    playback.Clear();playback.Start(0x123,shot,14000,26);playback.Snapshot(0x123,head,14005,26.005f,frame);
    Check(playback.TakeReport(report)&&report.frame.path.valid&&report.headTurn==-1&&report.entryShift==-1,"missing captured pose is marked unavailable rather than reported as zero drift");

    playback.Start(0x123,shot,20000,100);playback.Snapshot(0x123,head,20020,100.02f,frame);
    Check(frame.sceneTime==100.02f,"render snapshot carries the native accumulated scene clock for overlapping debris");
    Check(fabsf(Impact::Progress(frame)-.5f)<.001f,"normal-speed head traversal is halfway after 20 milliseconds");
    playback.Snapshot(0x123,head,20050,100.05f,frame);
    Check(Impact::Progress(frame)==1,"normal-speed bullet exits in under 50 milliseconds");
    playback.Start(0x123,shot,21000,101);playback.Snapshot(0x123,head,21020,101.002f,frame);
    Check(fabsf(Impact::Progress(frame)-.05f)<.001f,"tenfold slow motion advances only five percent over the same wall time");
    playback.Snapshot(0x123,head,21200,101.02f,frame);
    Check(fabsf(Impact::Progress(frame)-.5f)<.001f&&fabsf(frame.realAge-.2f)<.001f,"native slow motion stretches traversal and leaves the reveal clock independent");
    playback.Snapshot(0x123,head,21210,101.03f,frame);
    Check(fabsf(Impact::Progress(frame)-.75f)<.001f,"returning to normal speed advances from the accumulated simulation time");
    float stopped=frame.age;playback.Snapshot(0x123,head,21500,101.03f,frame);
    Check(frame.age==stopped&&frame.realAge>.49f,"paused scene freezes traversal while the real-time reveal expires normally");
    playback.Snapshot(0x123,head,21500,101.03f,frame);Check(frame.age==stopped,"duplicate render callbacks cannot advance the scene clock");
    playback.Snapshot(0x123,head,25650,101.03f,frame);
    Check(fabsf((frame.duration-frame.realAge)/.35f-1)<.001f,"fade starts at the real-time deadline even with a paused scene");
    Check(!playback.Snapshot(0x123,head,26000,101.03f,frame),"real-time deadline closes paused playback");
    playback.Start(0x123,shot,27000,102);playback.Snapshot(0x123,head,27010,102.01f,frame);
    Check(!playback.Snapshot(0x123,head,27020,102,frame),"backward scene clock invalidates the shot");
    Check(!playback.Snapshot(0x123,head,27030,103,frame)&&!playback.TakeReport(report),"clock recovery cannot revive stale playback or reports");
    playback.Start(0x123,shot,28000,std::numeric_limits<float>::quiet_NaN());
    Check(!playback.Snapshot(0x123,head,28010,104,frame),"nonfinite initial scene clock cannot start playback");
    playback.Start(0x123,shot,29000,105);
    Check(!playback.Snapshot(0x123,head,29010,std::numeric_limits<float>::infinity(),frame),"nonfinite sampled scene clock invalidates playback");
    until=playback.Start(0x123,shot,30000,106,1);
    Check(until==31000,"short reveal no longer inherits an artificial three-second minimum");
    printf("PASS: %u impact mapping, pose, timing and projectile geometry checks\n",checks);
}
