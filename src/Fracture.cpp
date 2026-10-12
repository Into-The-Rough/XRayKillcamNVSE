#include "Fracture.h"
#include <algorithm>
#include <cstring>

namespace XRay::Damage {
namespace {
constexpr float kTwoPi=6.283185307f;
//from and to are the donor positions crossed at start and end
struct Run {float start,end;Vec3 in,out,from,to;};
struct Shape {unsigned core,shards,plates,anchors;float coreRadius,plateRadius,ring,crack,minAngle,maxAngle,loose;};
Vec3 Perpendicular(Vec3 n) {
    Vec3 t=Cross(n,fabsf(n.x)<.8f?Vec3{1,0,0}:Vec3{0,1,0});Normalize(t);return t;
}
Shape ShapeOf(unsigned kind,float s) {
    //radii in game units for a 0.2 radius bullet, plate angles in degrees
    if(kind==EntryKind)return {3,5,6,7,.85f*s,2.4f*s,3.1f*s,1.6f,8,20,.2f};
    if(kind==ExitKind)return {4,7,8,9,1.6f*s,3.7f*s,4.6f*s,1.5f,28,58,.35f};
    return {2,0,0,5,.7f*s,0,1.5f*s,1.4f,0,0,0};
}
unsigned Merge(const Crossing* xs,unsigned n,const Surface& surface,unsigned set,float gap,float hi,Vec3 direction,Run* runs,unsigned capacity) {
    struct Interval {float a,b;Vec3 in,out,from,to;};
    constexpr unsigned kMeshes=128,kIntervals=256;
    Interval intervals[kIntervals];unsigned count=0;
    float open[kMeshes]{};Vec3 openNormal[kMeshes]{},openPoint[kMeshes]{};bool active[kMeshes]{};
    for(unsigned k=0;k<n;++k) {
        const auto& x=xs[k];
        if(x.mesh>=kMeshes||surface.SetOf(x.mesh)!=set)continue;
        //duplicate edge hits arrive as a second enter or a stray leave, both ignored
        if(x.entering){if(!active[x.mesh]){active[x.mesh]=true;open[x.mesh]=x.t;openNormal[x.mesh]=x.normal;openPoint[x.mesh]=x.point;}}
        else if(active[x.mesh]) {
            active[x.mesh]=false;
            if(count<kIntervals)intervals[count++]={open[x.mesh],x.t,openNormal[x.mesh],x.normal,openPoint[x.mesh],x.point};
        }
    }
    for(unsigned m=0;m<kMeshes;++m)if(active[m]&&count<kIntervals)
        intervals[count++]={open[m],hi,openNormal[m],openNormal[m]*-1,openPoint[m],openPoint[m]+direction*(hi-open[m])};
    std::sort(intervals,intervals+count,[](const Interval& a,const Interval& b){return a.a<b.a;});
    unsigned total=0;
    for(unsigned k=0;k<count;++k) {
        const auto& i=intervals[k];
        if(total&&i.a<=runs[total-1].end+gap) {
            auto& r=runs[total-1];
            if(i.b>r.end){r.end=i.b;r.out=i.out;r.to=i.to;}
        }else if(total<capacity)runs[total++]={i.a,i.b,i.in,i.out,i.from,i.to};
    }
    return total;
}
//the donor line through one run, exact at its crossings even where the neck bends the shot
struct Line {
    Vec3 base,direction;float start,rate;
    Vec3 At(float t) const {return base+direction*((t-start)*rate);}
};
struct Builder {
    const Shot& shot;Plan& plan;float scale;
    float TimeAt(float t) const {return fmaxf(t,0)*shot.secondsPerUnit;}
    Line LineOf(const Run& run) const {
        Vec3 direction=run.to-run.from;float span=Length(direction),length=run.end-run.start;
        if(span<.05f||length<.05f||!Normalize(direction))return {run.from,shot.direction,run.start,1};
        return {run.from,direction,run.start,span/length};
    }
    Seed* Add(unsigned set,Vec3 p,uint8_t state,float reach,float time,uint8_t cluster,uint8_t kind) {
        if(plan.count[set]>=plan.capacity[set])return nullptr;
        Seed& s=plan.seeds[set][plan.count[set]++];s=Seed{};
        s.position=p;s.state=state;s.reach=reach;s.time=time;s.cluster=cluster;s.wound=shot.wound;s.kind=kind;
        return &s;
    }
    void Fling(Seed* s,Vec3 velocity,Random& random,float life,float spinLo,float spinHi) {
        if(!s)return;
        Vec3 axis{random.Range(-1,1),random.Range(-1,1),random.Range(-1,1)};
        if(!Normalize(axis))axis={0,0,1};
        s->motion=velocity*shot.severity;s->axis=axis;s->life=life;s->spin=random.Range(spinLo,spinHi)*(random.Next()<.5f?-1.0f:1.0f);
    }
    bool AddCluster(unsigned kind,float t,Vec3 normal,Random& random,const Line& line) {
        Shape shape=ShapeOf(kind,scale);
        if(shot.severity>1) {
            //a harder shot breaks more bone over a wider ring, and more of its plates come loose
            auto more=[&](unsigned n){return unsigned(float(n)*shot.severity+.5f);};
            float grow=sqrtf(shot.severity);
            shape.core=more(shape.core);shape.shards=more(shape.shards);shape.plates=more(shape.plates);
            shape.coreRadius*=grow;shape.plateRadius*=grow;shape.ring*=grow;shape.loose=fminf(.9f,shape.loose+.5f*(shot.severity-1));
        }
        unsigned needed=shape.core+shape.shards+shape.plates+shape.anchors+2;
        if(plan.clusterCount>=plan.clusterCapacity||plan.count[BoneSet]+needed>plan.capacity[BoneSet])return false;
        Vec3 d=line.direction,c=line.At(t);
        if(kind==InnerKind||!Normalize(normal))normal=kind==ExitKind?d:d*-1;
        if(kind==EntryKind&&Dot(normal,d)>0)normal=normal*-1;
        if(kind==ExitKind&&Dot(normal,d)<0)normal=normal*-1;
        Vec3 along=d-normal*Dot(d,normal);
        if(!Normalize(along))along=Perpendicular(normal);
        Vec3 across=Cross(normal,along);
        float stretch=Clamp(1/fmaxf(fabsf(Dot(d,normal)),.35f),1,1.8f);
        auto place=[&](float radius,float angle){return c+along*(cosf(angle)*radius*stretch)+across*(sinf(angle)*radius);};
        auto radial=[&](Vec3 p){Vec3 r=p-c;r=r-normal*Dot(r,normal);return Normalize(r)?r:along;};
        uint8_t index=uint8_t(plan.clusterCount);float time=TimeAt(t);
        plan.clusters[plan.clusterCount++]={c,normal,time,shape.ring*stretch,shape.ring*stretch*shape.crack,uint8_t(kind),shot.wound};
        //measured carved cells: cores stay within ~1.6 units, oblique plates otherwise run 4+ units into deep bone
        float coreReach=shape.coreRadius*1.6f+.4f,plateReach=(kind==ExitKind?3.0f:2.4f)*scale,base=random.Range(0,kTwoPi);
        //side sign makes plates hinge toward the bullet: depressed at entry, everted at exit
        float side=Dot(normal,d)<0?-1.0f:1.0f;
        for(unsigned i=0;i<shape.core;++i) {
            float r=i?shape.coreRadius*random.Range(.45f,.75f):shape.coreRadius*random.Range(0,.2f);
            float a=base+kTwoPi*float(i)/float(shape.core>1?shape.core-1:1)+random.Range(-.4f,.4f);
            auto s=Add(BoneSet,place(r,a),Loose,coreReach,time,index,uint8_t(kind));
            if(!s)return false;
            Vec3 out=radial(s->position);
            if(kind==ExitKind)Fling(s,d*random.Range(60,95)+out*random.Range(12,30)+normal*random.Range(5,15),random,random.Range(.65f,1.0f),8,22);
            else if(kind==EntryKind&&i+1==shape.core)Fling(s,d*-random.Range(24,40)+out*random.Range(10,22),random,random.Range(.55f,.85f),10,24);
            else Fling(s,d*random.Range(24,42)+out*random.Range(6,14),random,random.Range(.5f,.8f),8,20);
        }
        //a ring of small shards between the hole and the plates fragments the rim instead of leaving clean panels
        float shardRadius=shape.coreRadius*1.45f;
        for(unsigned i=0;i<shape.shards;++i) {
            float step=kTwoPi/float(shape.shards),a=base+step*(float(i)+.5f+random.Range(-.35f,.35f));
            auto s=Add(BoneSet,place(shardRadius*random.Range(.85f,1.15f),a),Loose,coreReach,time,index,uint8_t(kind));
            if(!s)return false;
            Vec3 out=radial(s->position);
            if(kind==ExitKind)Fling(s,d*random.Range(50,85)+out*random.Range(20,40)+normal*random.Range(5,15),random,random.Range(.6f,1.0f),10,26);
            //about half the entry rim blows back toward the shooter, the rest is driven into the skull
            else if(i%2==0)Fling(s,d*-random.Range(22,40)+out*random.Range(14,28),random,random.Range(.5f,.85f),10,26);
            else Fling(s,d*random.Range(20,36)+out*random.Range(8,16),random,random.Range(.5f,.8f),10,24);
        }
        for(unsigned i=0;i<shape.plates;++i) {
            float step=kTwoPi/float(shape.plates),a=base+step*(float(i)+random.Range(-.3f,.3f));
            float r=shardRadius+(shape.plateRadius-shardRadius)*random.Range(.4f,.75f);
            bool loose=random.Next()<shape.loose;
            auto s=Add(BoneSet,place(r,a),loose?Loose:Plate,plateReach,time,index,uint8_t(kind));
            if(!s)return false;
            Vec3 out=radial(s->position);
            if(loose) {
                if(kind==ExitKind)Fling(s,d*random.Range(45,75)+out*random.Range(25,45),random,random.Range(.7f,1.1f),6,16);
                else Fling(s,d*random.Range(20,30)+out*random.Range(6,10),random,random.Range(.5f,.8f),6,14);
            }else {
                s->angle=side*random.Range(shape.minAngle,shape.maxAngle)*.01745329252f;
                s->motion=kind==ExitKind?d*random.Range(.08f,.2f)+normal*random.Range(.05f,.15f):d*random.Range(.04f,.1f);
            }
        }
        for(unsigned i=0;i<shape.anchors;++i) {
            float step=kTwoPi/float(shape.anchors),a=base+step*(float(i)+.5f+random.Range(-.25f,.25f));
            //anchors never break, so their reach carries how strongly the retained bone cracks along their borders
            float crack=random.Next()<.7f?random.Range(.55f,1):0;
            if(!Add(BoneSet,place(shape.ring*random.Range(.9f,1.12f),a),Anchor,crack,time,index,uint8_t(kind)))return false;
        }
        float depth=kind==InnerKind?1.2f:fmaxf(1.5f,1.4f*scale);
        return Add(BoneSet,c-normal*depth,Anchor,0,time,index,uint8_t(kind))&&Add(BoneSet,c+normal*depth,Anchor,0,time,index,uint8_t(kind));
    }
    void Crush(float from,float to,uint8_t cluster,unsigned kind,Random& random,const Line& line) {
        Vec3 d=line.direction;
        for(float t=from;t<=to;t+=.7f/shot.severity) {
            Vec3 side=Perpendicular(d);float a=random.Range(0,kTwoPi);
            Vec3 offset=side*cosf(a)+Cross(d,side)*sinf(a);
            auto s=Add(BoneSet,line.At(t)+offset*random.Range(0,.25f),Loose,.8f*scale,TimeAt(t),cluster,uint8_t(kind));
            if(!s)return;
            Fling(s,d*random.Range(30,50)+offset*10,random,random.Range(.5f,.8f),8,20);
        }
    }
    void Brain(const Run* runs,unsigned count,Random& random) {
        //a harder shot tears a wider channel
        float widen=1+.5f*(shot.severity-1);
        auto radius=[&](float t){float u=Clamp(t/fmaxf(shot.length,1),0,1);return (.8f+1.3f*u*u)*scale*widen;};
        auto add=[&](const Line& line,float t,Vec3 offset,float reach) {
            Vec3 d=line.direction;
            auto s=Add(BrainSet,line.At(t)+offset,Loose,reach,TimeAt(t),0,BrainKind);
            if(!s)return false;
            float u=Clamp(t/fmaxf(shot.length,1),0,1);
            Vec3 out=offset;if(!Normalize(out))out=Perpendicular(d);
            Fling(s,d*(18+55*u)+out*random.Range(8,25)*(.5f+u),random,random.Range(.9f,1.4f),3,8);
            return true;
        };
        for(unsigned k=0;k<count;++k) {
            const auto& run=runs[k];Line line=LineOf(run);Vec3 d=line.direction;
            float from=run.start-.15f,to=run.end+.15f,length=to-from;
            unsigned steps=unsigned(length/.7f)+1;
            for(unsigned i=0;i<=steps;++i) {
                float t=from+length*float(i)/float(steps);
                Vec3 side=Perpendicular(d);float a=random.Range(0,kTwoPi);
                Vec3 offset=(side*cosf(a)+Cross(d,side)*sinf(a))*(random.Range(0,.3f)*radius(t));
                if(!add(line,t,offset,radius(t)*random.Range(.85f,1.15f)))return;
            }
        }
        if(!count)return;
        //the entry wound spits brain back out along the track
        Line first=LineOf(runs[0]),last=LineOf(runs[count-1]);
        for(unsigned i=0,spits=unsigned(2*shot.severity+.5f);i<spits;++i) {
            float t=runs[0].start+.25f;Vec3 d=first.direction;
            Vec3 side=Perpendicular(d);float a=random.Range(0,kTwoPi);
            Vec3 offset=(side*cosf(a)+Cross(d,side)*sinf(a))*(.5f*radius(t));
            auto s=Add(BrainSet,first.At(t)+offset,Loose,.8f*radius(t),TimeAt(t),0,BrainKind);
            if(!s)return;
            Vec3 out=offset;Normalize(out);
            Fling(s,d*-random.Range(14,26)+out*random.Range(6,14),random,random.Range(.8f,1.2f),3,8);
        }
        float t=runs[count-1].end-.3f;Vec3 d=last.direction;
        for(unsigned i=0,spits=unsigned(3*shot.severity+.5f);i<spits;++i) {
            Vec3 side=Perpendicular(d);float a=random.Range(0,kTwoPi);
            Vec3 offset=(side*cosf(a)+Cross(d,side)*sinf(a))*(.7f*radius(t));
            if(!add(last,t,offset,.85f*radius(t)))return;
        }
    }
};
}
float Spacing(const Cluster& cluster) {return cluster.ring*.45f;}
void Chunk(float radius,uint32_t key,Vertex* out) {
    //icosahedron split once: 42 points, 80 faces
    const float t=1.618034f;
    Vec3 points[42]={{-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},{0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},{t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1}};
    const uint8_t faces[20][3]={{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
    unsigned count=12;uint8_t split[80][3];unsigned faceCount=0;
    uint8_t edges[30][3];unsigned edgeCount=0;
    auto middle=[&](uint8_t a,uint8_t b) {
        uint8_t lo=a<b?a:b,hi=a<b?b:a;
        for(unsigned e=0;e<edgeCount;++e)if(edges[e][0]==lo&&edges[e][1]==hi)return edges[e][2];
        points[count]=(points[a]+points[b])*.5f;edges[edgeCount][0]=lo;edges[edgeCount][1]=hi;edges[edgeCount++][2]=uint8_t(count);
        return uint8_t(count++);
    };
    for(const auto& f:faces) {
        uint8_t ab=middle(f[0],f[1]),bc=middle(f[1],f[2]),ca=middle(f[2],f[0]);
        const uint8_t tris[4][3]={{f[0],ab,ca},{f[1],bc,ab},{f[2],ca,bc},{ab,bc,ca}};
        for(const auto& tri:tris){split[faceCount][0]=tri[0];split[faceCount][1]=tri[1];split[faceCount++][2]=tri[2];}
    }
    Random random(key,7);
    float lumps[4][4];
    for(auto& lump:lumps){Vec3 d{random.Range(-1,1),random.Range(-1,1),random.Range(-1,1)};Normalize(d);lump[0]=d.x;lump[1]=d.y;lump[2]=d.z;lump[3]=random.Range(.15f,.4f);}
    Vec3 shape{random.Range(.75f,1.2f),random.Range(.65f,1),random.Range(.75f,1.2f)};
    for(unsigned i=0;i<count;++i) {
        Vec3 d=points[i];Normalize(d);float r=1;
        for(const auto& lump:lumps){float k=Dot(d,{lump[0],lump[1],lump[2]});r+=lump[3]*k*k*k;}
        points[i]={d.x*shape.x*r*radius,d.y*shape.y*r*radius,d.z*shape.z*r*radius};
    }
    for(unsigned f=0;f<faceCount;++f) {
        Vec3 a=points[split[f][0]],b=points[split[f][1]],c=points[split[f][2]];
        Vec3 n=Cross(b-a,c-a);if(!Normalize(n))n={0,0,1};
        out[f*3]={a,n,1};out[f*3+1]={b,n,1};out[f*3+2]={c,n,1};
    }
}
Runs PlanWound(const Model& model,const Surface& surface,const Shot& shot,Plan& plan) {
    Runs result;
    if(!(shot.length>.1f)||!Finite(shot.origin)||!Finite(shot.direction))return result;
    constexpr unsigned kCrossings=1024;
    std::vector<Crossing> crossings(kCrossings);
    float hi=shot.length+.6f;
    unsigned n=surface.Trace(model,shot.origin,shot.direction,-.6f,hi,crossings.data(),kCrossings);
    if(shot.posing) {
        std::vector<Vec3> posed;
        n+=surface.TraceBound(model,*shot.posing,shot.entry,shot.exit,shot.length,-.6f,hi,crossings.data()+n,kCrossings-n,posed);
        std::sort(crossings.begin(),crossings.begin()+n,[](const Crossing& a,const Crossing& b){return a.t<b.t;});
    }
    Run bone[16],brain[16];
    unsigned boneRuns=Merge(crossings.data(),n,surface,BoneSet,.9f,hi,shot.direction,bone,16);
    unsigned brainRuns=Merge(crossings.data(),n,surface,BrainSet,.6f,hi,shot.direction,brain,16);
    result.bone=boneRuns;result.brain=brainRuns;
    Builder builder{shot,plan,WoundScale(shot.bullet)};
    Random random(shot.sequence,shot.wound+1);
    unsigned inner=0;
    for(unsigned k=0;k<boneRuns;++k) {
        const auto& run=bone[k];
        float length=run.end-run.start,middle=(run.start+run.end)*.5f;
        unsigned kind=boneRuns==1?(middle>shot.length*.5f?ExitKind:EntryKind):k==0?EntryKind:k+1==boneRuns?ExitKind:InnerKind;
        if(kind==InnerKind&&++inner>2)continue;
        float inset=fminf(.15f,length*.5f);
        float t=kind==EntryKind?run.start+inset:kind==ExitKind?run.end-inset:middle;
        Vec3 normal=kind==EntryKind?run.in:kind==ExitKind?run.out:shot.direction*-1;
        uint8_t cluster=uint8_t(plan.clusterCount);
        Line line=builder.LineOf(run);
        if(!builder.AddCluster(kind,t,normal,random,line))break;
        if(length>1.6f)builder.Crush(run.start+.6f,run.end-.6f,cluster,kind,random,line);
    }
    builder.Brain(brain,brainRuns,random);
    return result;
}
void PruneAnchors(Seed* seeds,unsigned& count,const Cluster* clusters) {
    unsigned kept=0;
    for(unsigned i=0;i<count;++i) {
        bool drop=false;
        if(seeds[i].state==Anchor)for(unsigned j=0;j<count&&!drop;++j) {
            const auto& other=seeds[j];
            if(other.state==Anchor||other.cluster==seeds[i].cluster)continue;
            float spacing=Spacing(clusters[other.cluster])*.9f;
            Vec3 delta=seeds[i].position-other.position;
            drop=Dot(delta,delta)<spacing*spacing;
        }
        if(!drop)seeds[kept++]=seeds[i];
    }
    count=kept;
}
void Carver::Reserve(unsigned vertices) {
    for(auto& cell:parts_)for(auto& part:cell){part.vertices.clear();part.vertices.reserve(vertices);}
}
namespace {
struct ClipVertex {Vec3 p,n;float ao,bind,bones;};
constexpr unsigned kClipVertices=72,kPlanes=64;
unsigned ClipPlane(const ClipVertex* in,unsigned count,Vec3 normal,float offset,ClipVertex* out) {
    unsigned n=0;
    for(unsigned k=0;k<count;++k) {
        const auto& a=in[k];const auto& b=in[k+1==count?0:k+1];
        float da=Dot(normal,a.p)-offset,db=Dot(normal,b.p)-offset;
        if(da<=0&&n<kClipVertices)out[n++]=a;
        if((da<=0)!=(db<=0)&&n<kClipVertices) {
            float t=da/(da-db);
            out[n++]={a.p+(b.p-a.p)*t,a.n+(b.n-a.n)*t,a.ao+(b.ao-a.ao)*t,a.bind+(b.bind-a.bind)*t,a.bones};
        }
    }
    return n;
}
}
unsigned Carver::Carve(const Model& model,const Surface& surface,unsigned set,const Seed* seeds,unsigned count,const uint8_t* selected) {
    cells_=0;
    constexpr unsigned kSeeds=512,kRegions=64;
    if(count>kSeeds)count=kSeeds;
    int cellOf[kSeeds];
    struct Region {Vec3 center;float radius;} regions[kRegions];unsigned regionCount=0;
    Bounds box;
    for(unsigned i=0;i<count;++i) {
        cellOf[i]=-1;
        if(!selected[i]||seeds[i].state==Anchor||cells_>=kCells)continue;
        cellOf[i]=int(cells_);seed_[cells_]=i;
        for(auto& part:parts_[cells_]){part.vertices.clear();part.lo={1e20f,1e20f,1e20f};part.hi={-1e20f,-1e20f,-1e20f};part.area=0;}
        ++cells_;
        Vec3 p=seeds[i].position;float r=seeds[i].reach;
        box.lo={fminf(box.lo.x,p.x-r),fminf(box.lo.y,p.y-r),fminf(box.lo.z,p.z-r)};
        box.hi={fmaxf(box.hi.x,p.x+r),fmaxf(box.hi.y,p.y+r),fmaxf(box.hi.z,p.z+r)};
        //greedy bounding spheres keep the per-triangle prefilter to a few tests
        auto& last=regions[regionCount?regionCount-1:0];
        float gap=regionCount?Length(p-last.center):0;
        if(regionCount&&gap+r<=last.radius)continue;
        float merged=regionCount?(last.radius+gap+r)*.5f:1e20f;
        if(regionCount&&(merged<=4.5f||regionCount==kRegions)) {
            last.center=last.center+(p-last.center)*((merged-last.radius)/fmaxf(gap,1e-6f));last.radius=merged;
        }else regions[regionCount++]={p,r};
    }
    if(!cells_)return 0;
    const Grid& grid=surface.GridOf(set);
    if(grid.facets.empty())return cells_;
    float distance[kSeeds];unsigned candidates[kSeeds];
    ClipVertex polygon[kClipVertices],scratch[kClipVertices];
    auto emit=[&](unsigned cell,unsigned part,unsigned size) {
        auto& target=parts_[cell][part];
        for(unsigned v=1;v+1<size;++v) {
            const ClipVertex* tri[3]={&polygon[0],&polygon[v],&polygon[v+1]};
            float area=Length(Cross(tri[1]->p-tri[0]->p,tri[2]->p-tri[0]->p))*.5f;
            if(!(area>1e-9f))continue;
            target.area+=area;
            for(auto c:tri) {
                Vec3 normal=c->n;if(!Normalize(normal))normal={0,0,1};
                target.vertices.push_back({c->p,normal,c->ao,c->bind,c->bones});
                target.lo={fminf(target.lo.x,c->p.x),fminf(target.lo.y,c->p.y),fminf(target.lo.z,c->p.z)};
                target.hi={fmaxf(target.hi.x,c->p.x),fmaxf(target.hi.y,c->p.y),fmaxf(target.hi.z,c->p.z)};
            }
        }
    };
    int lo[3],hi[3];
    const float boxLo[3]={box.lo.x-grid.reach,box.lo.y-grid.reach,box.lo.z-grid.reach},boxHi[3]={box.hi.x+grid.reach,box.hi.y+grid.reach,box.hi.z+grid.reach};
    const float origin[3]={grid.origin.x,grid.origin.y,grid.origin.z};
    for(unsigned axis=0;axis<3;++axis) {
        lo[axis]=int(floorf((boxLo[axis]-origin[axis])/grid.cell));hi[axis]=int(floorf((boxHi[axis]-origin[axis])/grid.cell));
        lo[axis]=lo[axis]<0?0:lo[axis];hi[axis]=hi[axis]>=grid.size[axis]?grid.size[axis]-1:hi[axis];
    }
    for(int z=lo[2];z<=hi[2];++z)for(int y=lo[1];y<=hi[1];++y)for(int x=lo[0];x<=hi[0];++x) {
        uint32_t c=uint32_t((z*grid.size[1]+y)*grid.size[0]+x);
        uint32_t begin=grid.starts[c],end=grid.starts[c+1];
        if(begin==end)continue;
        //every facet centroid lies within this radius of its cell centre and every facet point within reach of its centroid
        float reach=grid.reaches[c],spread=grid.cell*.8661f+reach;
        Vec3 centre=grid.origin+Vec3{(x+.5f)*grid.cell,(y+.5f)*grid.cell,(z+.5f)*grid.cell};
        bool near=false;
        for(unsigned k=0;k<regionCount&&!near;++k){Vec3 d=centre-regions[k].center;float extent=regions[k].radius+spread;near=Dot(d,d)<=extent*extent;}
        if(!near)continue;
        //seeds farther than this can be neither nearest, runner-up nor a clipping plane for any facet here
        float closest=1e30f;
        for(unsigned j=0;j<count;++j){Vec3 d=centre-seeds[j].position;closest=fminf(closest,Dot(d,d));}
        float limit=sqrtf(closest)+2*spread+4*reach;limit*=limit;
        unsigned listed=0;
        for(unsigned j=0;j<count;++j){Vec3 d=centre-seeds[j].position;if(Dot(d,d)<=limit)candidates[listed++]=j;}
        bool owners=false;
        for(unsigned k=0;k<listed&&!owners;++k)owners=cellOf[candidates[k]]>=0;
        if(!owners)continue;
        for(uint32_t f=begin;f<end;++f) {
            const Facet& facet=surface.FacetOf(set,grid.facets[f]);
            const auto& mesh=model.meshes[facet.mesh];
            const Vertex& va=mesh.vertices[mesh.indices[facet.first]];const Vertex& vb=mesh.vertices[mesh.indices[facet.first+1]];const Vertex& vc=mesh.vertices[mesh.indices[facet.first+2]];
            Vec3 middle=facet.centre;float r=facet.radius;
            float q1=1e30f,q2=1e30f;int first=-1;
            for(unsigned k=0;k<listed;++k) {
                unsigned j=candidates[k];Vec3 d=middle-seeds[j].position;float q=Dot(d,d);distance[j]=q;
                if(q<q1){q2=q1;q1=q;first=int(j);}else if(q<q2)q2=q;
            }
            float d1=sqrtf(q1),d2=sqrtf(q2),owner=(d1+2*r)*(d1+2*r);
            //a triangle wholly inside one cell needs no bisector clipping
            bool interior=d2-d1>2*r;
            for(unsigned k=0;k<listed;++k) {
                unsigned i=candidates[k];int cell=cellOf[i];
                if(cell<0||(interior&&int(i)!=first)||distance[i]>owner)continue;
                const Seed& s=seeds[i];float di=sqrtf(distance[i]),bound=(di+2*r)*(di+2*r);
                if(di>s.reach+r)continue;
                polygon[0]={va.position,va.normal,va.ao,va.bind,va.bones};polygon[1]={vb.position,vb.normal,vb.ao,vb.bind,vb.bones};polygon[2]={vc.position,vc.normal,vc.ao,vc.bind,vc.bones};
                unsigned size=3;
                Vec3 si=s.position;float si2=Dot(si,si);
                if(!interior)for(unsigned m=0;m<listed&&size>=3;++m) {
                    unsigned j=candidates[m];
                    if(j==i||distance[j]>bound)continue;
                    Vec3 sj=seeds[j].position;
                    //keep |x-si|<=|x-sj|, the same nearest-seed predicate the pixel shader evaluates
                    size=ClipPlane(polygon,size,sj-si,(Dot(sj,sj)-si2)*.5f,scratch);
                    memcpy(polygon,scratch,size*sizeof(ClipVertex));
                }
                if(size>=3&&di+r>s.reach) {
                    //tangent planes toward the centroid, corners and edge midpoints bound the reach sphere well under a pixel
                    const Vec3 toward[7]={middle,va.position,vb.position,vc.position,(va.position+vb.position)*.5f,
                        (vb.position+vc.position)*.5f,(vc.position+va.position)*.5f};
                    for(unsigned q=0;q<7&&size>=3;++q) {
                        Vec3 out=toward[q]-si;if(!Normalize(out))continue;
                        size=ClipPlane(polygon,size,out,Dot(out,si)+s.reach,scratch);
                        memcpy(polygon,scratch,size*sizeof(ClipVertex));
                    }
                }
                if(size>=3)emit(unsigned(cell),facet.part,size);
            }
        }
    }
    return cells_;
}
}
