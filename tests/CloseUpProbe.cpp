#include "../src/CloseUp.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using namespace XRay;
static void Check(bool ok,const char* name){if(!ok){printf("FAIL: %s\n",name);exit(1);}}
static Vec3 Column(const Transform& t,unsigned c){return {t.r[c],t.r[3+c],t.r[6+c]};}
static float Determinant(const Transform& t){return Dot(Cross(Column(t,0),Column(t,1)),Column(t,2));}
void TestCloseUpPose() {
    Vec3 wound{10,20,30};
    auto pose=CloseUp::Pose(wound,{0,1,0},28,0,1);
    Check(Length(pose.t-Vec3{10,-8,35.6f})<1e-3f,"behind the entry along the incoming shot, raised by a fifth of the distance");
    Vec3 look=wound-pose.t;Normalize(look);
    Check(Length(Column(pose,0)-look)<1e-4f,"looks at the wound along its first column");
    Check(Column(pose,1).z>0&&fabsf(Dot(Column(pose,0),Column(pose,1)))<1e-4f&&fabsf(Dot(Column(pose,1),Column(pose,2)))<1e-4f,"its second column is up, the axes orthogonal");
    Check(fabsf(Determinant(pose)-1)<1e-4f,"a proper rotation for a right-handed live camera");
    Check(fabsf(Determinant(CloseUp::Pose(wound,{0,1,0},28,0,-1))+1)<1e-4f,"and the live camera's handedness when it is the other");
    auto turned=CloseUp::Pose(wound,{0,1,0},28,1.5707963f,1);
    Check(Length(turned.t-Vec3{38,20,35.6f})<1e-3f&&fabsf(Length(turned.t-wound)-Length(pose.t-wound))<1e-3f,"the orbit turns about the vertical through the wound at the same distance");
    auto down=CloseUp::Pose(wound,{0,0,-1},28,0,1);
    bool finite=Finite(down.t);for(float x:down.r)finite&=std::isfinite(x);
    Check(finite&&fabsf(Determinant(down)-1)<1e-4f&&Column(down,0).z<-.99f,"a shot from straight above still gives a proper camera looking down");
    printf("PASS: close-up camera pose behind the wound, orbit, axes and handedness\n");
}
//a wall plane in front of the camera's way: the free distance from the wound toward an eye, or the whole way when it misses
static float Wall(Vec3 wound,Vec3 eye,Vec3 normal,float offset) {
    Vec3 d=eye-wound;float length=Length(d);Normalize(d);
    float facing=Dot(d,normal);if(facing>=-1e-6f)return length;
    float t=(offset-Dot(wound,normal))/facing;
    return t>=0&&t<length?t:length;
}
void TestCloseUpFollow() {
    Vec3 at{0,0,0};
    Check(Length(CloseUp::Follow(at,{1,0,0},1/60.f))<.15f,"one frame's jump of the bone moves the camera less than a sixth of it");
    Vec3 a=at,b=at;
    for(unsigned i=0;i<60;++i)a=CloseUp::Follow(a,{10,0,0},1/60.f);
    for(unsigned i=0;i<30;++i)b=CloseUp::Follow(b,{10,0,0},1/30.f);
    Check(Length(a-b)<1e-3f,"the follow eases the same at any frame rate");
    Check(Length(a-Vec3{10,0,0})<.01f,"a second later the camera has caught up with a wound that moved and stayed");
    printf("PASS: the close-up follows the wound smoothly, a one-frame jump moves it %.3f of the way\n",Length(CloseUp::Follow(at,{1,0,0},1/60.f)));
}
void TestCloseUpWalls() {
    Vec3 wound{0,0,50},shot{0,1,0};
    CloseUp::Framing open;auto pose=CloseUp::Frame(open,wound,shot,28,0,1,0,[&](Vec3 eye){return Length(eye-wound);});
    Check(open.chosen&&open.turn==0&&fabsf(open.distance-28)<1e-4f&&Length(pose.t-CloseUp::Pose(wound,shot,28,0,1).t)<1e-4f,"in the open the close-up keeps the shot's line and its distance");
    //a wall 10 units behind the entry, square to the shot
    auto behind=[&](Vec3 eye){return Wall(wound,eye,{0,1,0},-10);};
    CloseUp::Framing walled;pose=CloseUp::Frame(walled,wound,shot,28,0,1,0,behind);
    Check(walled.chosen&&fabsf(fabsf(walled.turn)-1.5708f)<1e-3f&&fabsf(walled.distance-28)<1e-4f&&behind(pose.t)>=Length(pose.t-wound)-1e-3f,
        "a wall behind the entry turns the camera along it, clear at its full distance");
    //boxed in 14 units all round: the roomiest turn, pulled in to the margin
    auto box=[&](float half){return [&wound,half](Vec3 eye){float free=Length(eye-wound);
        for(Vec3 n:{Vec3{1,0,0},Vec3{-1,0,0},Vec3{0,1,0},Vec3{0,-1,0}})free=fminf(free,Wall(wound,eye,n,Dot(wound,n)-half));return free;};};
    auto boxed=box(14);
    CloseUp::Framing tight;pose=CloseUp::Frame(tight,wound,shot,28,0,1,0,boxed);
    Vec3 out=pose.t-wound;Normalize(out);
    Check(tight.chosen&&tight.distance<28&&tight.distance>=CloseUp::kNearest&&boxed(wound+out*100)>=Length(pose.t-wound)+CloseUp::kWallMargin*.99f,"boxed in, it comes in to stay the margin clear of the walls");
    float in=tight.distance;CloseUp::Frame(tight,wound,shot,28,0,1,.1f,[&](Vec3 eye){return Length(eye-wound);});
    Check(fabsf(tight.distance-(in+CloseUp::kRegrow*.1f))<1e-3f,"out of the walls it backs out at its regrow rate, not at once");
    CloseUp::Framing pinned;CloseUp::Frame(pinned,wound,shot,28,0,1,0,box(3));
    Check(pinned.distance==CloseUp::kNearest,"never nearer the wound than the floor");
    CloseUp::Framing unknown;CloseUp::Frame(unknown,wound,shot,28,0,1,0,[&](Vec3){return -1.f;});
    Check(!unknown.chosen&&unknown.distance==28,"an untested frame keeps the shot's line and distance and tries again");
    printf("PASS: close-up camera turns off a wall behind the entry, comes in when boxed, never inside the floor, backs out slowly\n");
}
