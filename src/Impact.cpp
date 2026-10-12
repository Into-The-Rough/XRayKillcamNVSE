#include "Impact.h"
#include "Graphics.h"
#include "Proportions.h"
#include <cfloat>

namespace XRay::Impact {
static bool Triangle(Vec3 origin,Vec3 direction,Vec3 a,Vec3 b,Vec3 c,float& distance) {
    Vec3 e1=b-a,e2=c-a,p=Cross(direction,e2);
    float determinant=Dot(e1,p);
    if(fabsf(determinant)<1e-6f)return false;
    float inverse=1/determinant;
    Vec3 s=origin-a;
    float u=Dot(s,p)*inverse;
    if(u<-.0001f||u>1.0001f)return false;
    Vec3 q=Cross(s,e1);
    float v=Dot(direction,q)*inverse;
    if(v<-.0001f||u+v>1.0001f)return false;
    distance=Dot(e2,q)*inverse;
    return std::isfinite(distance);
}
bool Map(const Sample& sample,const HeadFrame& head,Path& out) {
    out={};
    if(!sample.valid||!Finite(sample.point)||!Finite(sample.direction))return false;
    //a limb moves against the frame between the hit and the first rendered frame, so the shot follows the bone it hit
    uint32_t carrier=sample.posed&&sample.carrier<kInfluences?sample.carrier:0;
    const Transform& at=sample.posed?(carrier?sample.carrierPose:sample.pose):head.head;
    if(!at.Valid()||!head.posedBones[carrier].Valid())return false;
    Transform toFrame=Compose(head.posedBones[carrier],Inverse(at));
    Vec3 point=toFrame.Apply(sample.point),direction=toFrame.Rotate(sample.direction);
    if(!Normalize(direction)||!Finite(point))return false;
    out.point=point;out.direction=direction;out.traced=sample.traced;
    if(sample.traced) {
        Vec3 origin=toFrame.Apply(sample.rayOrigin);
        if(!Finite(origin))return false;
        //a contact behind the observed query start came from another native query, so keep the contact itself
        float along=Dot(point-origin,direction);
        if(along>0) {
            Vec3 projected=origin+direction*along;
            out.correction=Length(projected-point);point=projected;
        }
    }
    float first=FLT_MAX,last=-FLT_MAX;
    //this intersects our copied display meshes, never the game's collision world
    auto trace=[&](const MaskVertex* vertices,uint32_t count,const uint16_t* indices,uint32_t triangles) {
        for(uint32_t i=0;i<triangles*3;i+=3) {
            auto a=indices[i],b=indices[i+1],c=indices[i+2];
            if(a>=count||b>=count||c>=count)return false;
            float distance;
            if(Triangle(point,direction,vertices[a].local,vertices[b].local,vertices[c].local,distance)) {
                if(distance<first)first=distance;
                if(distance>last)last=distance;
            }
        }
        return true;
    };
    //the face ends at the collar, the neck skin below and behind it closes the neck
    if((head.face&&!trace(head.vertices,kHeadVertices,head.indices,kHeadTriangles))
        ||head.skinVertices>kSkinVertices||head.skinTriangles>kSkinTriangles
        ||!trace(head.skin,head.skinVertices,head.skinIndices,head.skinTriangles))return false;
    //havok contact and the visible skin differ slightly, so a miss is rejected rather than turned into a centre shot
    float size=BodySize(head),longest=kLongestPath*size,slack=kContactSlack*size;
    if(first>last||!std::isfinite(first)||!std::isfinite(last)||last-first<.5f||last-first>longest
        ||first>slack||last< -slack||first< -longest||last>longest)return false;
    out.entry=point+direction*first;out.exit=point+direction*last;out.valid=true;
    return true;
}
static float Speed(const Frame& frame){return fmaxf(Length(frame.path.exit-frame.path.entry),6.6f)/kTravelTime;}
float Distance(const Frame& frame){return frame.age*Speed(frame);}
float TimeAt(const Frame& frame,float distance){return distance/Speed(frame);}
float BulletEnd(const Frame& frame){return TimeAt(frame,Length(frame.path.exit-frame.path.entry)+frame.bullet.length+4);}
float Progress(const Frame& frame){return Clamp(Distance(frame)/fmaxf(.5f,Length(frame.path.exit-frame.path.entry)),0,1);}
unsigned BuildBullet(const Frame& frame,Vertex* out,float size) {
    if(!out||!frame.path.valid||!std::isfinite(frame.age)||frame.age<0||frame.age>BulletEnd(frame))return 0;
    Vec3 forward=frame.path.exit-frame.path.entry;
    float length=Length(forward);
    if(!Normalize(forward)||length<.5f||length>kLongestPath*size)return 0;
    Vec3 right=Cross(forward,fabsf(forward.x)<.8f?Vec3{1,0,0}:Vec3{0,1,0});
    if(!Normalize(right))return 0;
    Vec3 up=Cross(right,forward);
    Vec3 tip=frame.path.entry+forward*Distance(frame);
    Vec3 shoulder=tip-forward*frame.bullet.nose,base=tip-forward*frame.bullet.length;
    unsigned count=0;
    auto triangle=[&](Vec3 a,Vec3 b,Vec3 c,Vec3 na,Vec3 nb,Vec3 nc) {
        out[count++]={a,na,1};out[count++]={b,nb,1};out[count++]={c,nc,1};
    };
    for(unsigned i=0;i<kBulletSides;++i) {
        float a=float(i)*6.283185307f/kBulletSides,b=float(i+1)*6.283185307f/kBulletSides;
        Vec3 na=right*cosf(a)+up*sinf(a),nb=right*cosf(b)+up*sinf(b);
        Vec3 pa=shoulder+na*frame.bullet.radius,pb=shoulder+nb*frame.bullet.radius,ba=base+na*frame.bullet.radius,bb=base+nb*frame.bullet.radius;
        float slope=frame.bullet.radius/frame.bullet.nose;
        Vec3 ta=na+forward*slope,tb=nb+forward*slope;Normalize(ta);Normalize(tb);
        triangle(tip,pa,pb,forward,ta,tb);
        triangle(pa,ba,pb,na,na,nb);triangle(pb,ba,bb,nb,na,nb);
        triangle(base,bb,ba,forward*-1,forward*-1,forward*-1);
    }
    return count;
}
Vec3 Eye(const HeadFrame& head,const CameraFrame& camera) {
    const float* v=camera.view;
    Vec3 eye={-(v[12]*v[0]+v[13]*v[1]+v[14]*v[2]),-(v[12]*v[4]+v[13]*v[5]+v[14]*v[6]),-(v[12]*v[8]+v[13]*v[9]+v[14]*v[10])};
    return head.head.Unapply(eye+camera.origin);
}
Vec3 SectionNormal(const Frame& frame,const HeadFrame& head,const CameraFrame& camera) {
    const float* v=camera.view;
    Vec3 normal=camera.projection[15]>.5f?head.head.Unrotate({-v[2],-v[6],-v[10]}):Eye(head,camera)-(frame.path.entry+frame.path.exit)*.5f;
    return Normalize(normal)?normal:Vec3{};
}

float ClockRate(float age,float crossing,float speed) {
    if(!(speed>0&&speed<1)||!std::isfinite(age))return 1;
    float t=Clamp((age-crossing-kImpactLinger)/kImpactEase,0,1);
    return speed+(1-speed)*t*t*(3-2*t);
}
void Playback::SetImpactSpeed(float speed){AcquireSRWLockExclusive(&lock_);impactSpeed_=std::isfinite(speed)?Clamp(speed,.05f,1):1;ReleaseSRWLockExclusive(&lock_);}
void Playback::Extend(uint64_t until){AcquireSRWLockExclusive(&lock_);if(target_&&until>expires_)expires_=until;ReleaseSRWLockExclusive(&lock_);}
uint64_t Playback::Start(uint32_t target,const Sample& sample,uint64_t now,float simulation,float duration) {
    AcquireSRWLockExclusive(&lock_);
    uint64_t lifetime=static_cast<uint64_t>((std::isfinite(duration)?Clamp(duration,1,30):5)*1000);
    target_=std::isfinite(simulation)?target:0;sample_=sample;path_={};started_=now;expires_=now+lifetime;
    //time no shot was drawn passes at the game's pace, so debris still in the air keeps flying as it would
    if(std::isfinite(simulation)&&simulation>lastSimulation_)clock_+=simulation-lastSimulation_;
    lastSimulation_=simulation;clockStart_=clock_;
    attempted_=false;reportReady_=false;++sequence_;
    uint64_t until=expires_;ReleaseSRWLockExclusive(&lock_);return until;
}
void Playback::Clear() {
    AcquireSRWLockExclusive(&lock_);target_=0;sample_={};path_={};attempted_=reportReady_=false;++sequence_;ReleaseSRWLockExclusive(&lock_);
}
int Playback::Snapshot(uint32_t target,const HeadFrame& head,uint64_t now,float simulation,Frame& out,Sample* source) {
    AcquireSRWLockExclusive(&lock_);
    if(!target||target!=target_){out={};ReleaseSRWLockExclusive(&lock_);return 0;}
    if(!std::isfinite(simulation)||simulation<lastSimulation_||now<started_||now>=expires_) {
        target_=0;reportReady_=false;out={};ReleaseSRWLockExclusive(&lock_);return 0;
    }
    bool mapped=!attempted_;
    if(mapped){Map(sample_,head,path_);attempted_=true;}
    Frame shape{};shape.path=path_;shape.bullet=sample_.bullet;
    float through=path_.valid?BulletEnd(shape):0;
    clock_+=double(simulation-lastSimulation_)*ClockRate(float(clock_-clockStart_),through,path_.valid?impactSpeed_:1);
    lastSimulation_=simulation;
    out={path_,float(clock_-clockStart_),float(expires_-started_)*.001f,sample_.bullet,sequence_,float(now-started_)*.001f,float(clock_)};
    if(source)*source=sample_;
    float crossing=TimeAt(out,Length(path_.exit-path_.entry));
    unsigned stage=out.age>=crossing?2:out.age>=crossing*.5f?1:0;
    if(mapped||stage!=reportStage_) {
        bool initial=mapped||(reportReady_&&report_.initial&&report_.frame.sequence==out.sequence);
        report_={out,stage,initial};reportStage_=stage;reportReady_=true;
        //limbs carry both sides, the larger bend is the one worth reading
        report_.neckBend=fmaxf(RotationAngle(head.influences[1]),head.region<kRegions&&kSided[head.region]?RotationAngle(head.influences[4]):0)*57.2957795f;report_.neckError=head.neckError;
        report_.skinTriangles=head.skinTriangles;report_.skinShapes=head.skinShapes;report_.skinSkipped=head.skinSkipped;report_.region=head.region;
        if(path_.valid&&sample_.posed) {
            Vec3 shown=head.head.Rotate(path_.direction),recorded=sample_.direction;
            if(Normalize(shown)&&Normalize(recorded))report_.headTurn=acosf(Clamp(Dot(shown,recorded),-1,1))*57.2957795f;
            report_.entryShift=Length(head.head.Apply(path_.entry)-sample_.pose.Apply(path_.entry));
        }
    }
    int status=path_.valid?1:sample_.valid?-1:-2;
    ReleaseSRWLockExclusive(&lock_);
    return status;
}
bool Playback::TakeReport(Report& out) {
    AcquireSRWLockExclusive(&lock_);bool ready=reportReady_;
    if(ready){out=report_;reportReady_=false;}
    ReleaseSRWLockExclusive(&lock_);return ready;
}
}
