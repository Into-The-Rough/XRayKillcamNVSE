#include "Damage.h"
#include "Proportions.h"
#include <cstring>
#include <windows.h>

namespace XRay::Damage {
Posing PosingOf(const HeadFrame& head,const Appearance& style) {
    Posing posing{head.fit,style.scale>.01f?style.scale:1,{style.up,style.forward,style.side}};
    posing.influences=head.influences;return posing;
}
Scene::~Scene() {
    if(!thread_)return;
    //process exit has already stopped the worker, an explicit teardown wakes it and waits briefly
    quit_=true;SetEvent(wake_);WaitForSingleObject(thread_,1000);
    CloseHandle(thread_);CloseHandle(wake_);CloseHandle(done_);
}
unsigned long __stdcall Scene::Worker(void* context) {
    auto scene=static_cast<Scene*>(context);
    for(;;) {
        WaitForSingleObject(scene->wake_,INFINITE);
        if(scene->quit_)return 0;
        Shatter(*scene->model_,scene->surface_,scene->carver_,scene->work_);
        scene->job_.store(2,std::memory_order_release);SetEvent(scene->done_);
    }
}
void Scene::Wait() {
    while(job_.load(std::memory_order_acquire)==1)WaitForSingleObject(done_,50);
}
void Scene::Reserve(const Model& model) {
    //the worker reads the model and surface, never rebuild them under it
    Wait();job_=0;
    model_=&model;surface_.Reserve(model);carver_.Reserve(1024);debris_.Reserve();
    plates_.assign(kPlateVertices,{});work_.plates.assign(kPlateVertices,{});
    Clear();
}
void Scene::Clear() {debris_.Clear();spray_.Clear();heardCount_=0;ClearWounds();}
void Scene::ClearWounds() {
    //a job still running for the old wounds is discarded when it finishes
    ++generation_;++woundRevision_;woundCount_=0;dirty_=false;
    seedCount_[BoneSet]=seedCount_[BrainSet]=0;clusterCount_=0;liveWounds_=0;
    plateCount_=0;for(auto& range:plateParts_)range={};++plateRevision_;
    activeSeeds_[BoneSet]=activeSeeds_[BrainSet]=0;activeClusters_=0;
    ruptureCount_=0;memset(ruptured_,0,sizeof ruptured_);
    bullets_=0;bulletReleased_=false;summary_={};
}
void Scene::Remove(unsigned wound) {
    for(unsigned i=wound;i+1<woundCount_;++i)wounds_[i]=wounds_[i+1];
    --woundCount_;dirty_=true;++woundRevision_;
}
bool Scene::Snapshot(Work& work) const {
    work.seedCount[BoneSet]=work.seedCount[BrainSet]=0;work.clusterCount=0;work.wounds=woundCount_;
    Seed merged[kSceneBoneSeeds];unsigned count=0;
    for(unsigned w=0;w<woundCount_;++w) {
        const auto& wound=wounds_[w];
        work.sequence[w]=wound.sequence;work.born[w]=wound.born;work.release[w]=!wound.released;
        work.ruptureCount[w]=wound.ruptureCount;memcpy(work.ruptures[w],wound.ruptures,wound.ruptureCount*sizeof(Rupture));work.severity[w]=wound.severity;
        if(work.clusterCount+wound.clusterCount>kSceneClusters)return false;
        if(count+wound.seedCount[BoneSet]>kSceneBoneSeeds||work.seedCount[BrainSet]+wound.seedCount[BrainSet]>kSceneBrainSeeds)return false;
        for(unsigned k=0;k<wound.seedCount[BoneSet];++k) {
            auto& s=merged[count++];s=wound.seeds[BoneSet][k];
            s.cluster=uint8_t(s.cluster+work.clusterCount);s.wound=uint8_t(w);s.source=uint8_t(k);
        }
        for(unsigned k=0;k<wound.seedCount[BrainSet];++k) {
            auto& s=work.seeds[BrainSet][work.seedCount[BrainSet]++];s=wound.seeds[BrainSet][k];s.wound=uint8_t(w);s.source=uint8_t(k);
        }
        for(unsigned c=0;c<wound.clusterCount;++c){work.clusters[work.clusterCount]=wound.clusters[c];work.clusters[work.clusterCount++].wound=uint8_t(w);}
    }
    //drop anchors that would sit inside another cluster's broken bone
    for(unsigned i=0;i<count;++i) {
        bool drop=false;
        if(merged[i].state==Anchor)for(unsigned j=0;j<count&&!drop;++j) {
            const auto& other=merged[j];
            if(other.state==Anchor||other.cluster==merged[i].cluster)continue;
            float spacing=Spacing(work.clusters[other.cluster])*.9f;Vec3 delta=merged[i].position-other.position;
            drop=Dot(delta,delta)<spacing*spacing;
        }
        if(!drop)work.seeds[BoneSet][work.seedCount[BoneSet]++]=merged[i];
    }
    return true;
}
void Shatter(const Model& model,const Surface& surface,Carver& carver,Work& work) {
    work.plateCount=0;for(auto& range:work.plateParts)range={};
    //the vertices left in each of a wound's bursts, the next piece draws on the burst its index falls in
    unsigned budget[kWounds][kWoundBursts];
    for(unsigned w=0;w<kWounds;++w) {
        for(auto& part:work.staging[w])part.clear();
        work.pieceCount[w]=work.fragments[w]=work.brainPieces[w]=work.vertices[w]=0;
        for(auto& room:budget[w])room=kBurstVertices;
    }
    //a burst organ goes first, the track's own pieces take what it leaves
    for(unsigned w=0;w<work.wounds;++w)if(work.release[w])for(unsigned k=0;k<work.ruptureCount[w];++k) {
        const auto& rupture=work.ruptures[w][k];
        unsigned part=kFractureKinds+SoftKind(model.meshes[rupture.mesh].material);
        if(part>=kParts)continue;
        unsigned added=Spill(model,work.posing,rupture,work.sequence[w],budget[w],work.staging[w][part],work.pieceCount[w],work.pieces[w],kWoundPieces);
        work.pieceCount[w]+=added;work.brainPieces[w]+=added;work.vertices[w]+=added*kChunkVertices;
    }
    auto release=[&](unsigned set,unsigned cell) {
        const auto& seed=work.seeds[set][carver.SeedOf(cell)];unsigned w=seed.wound;
        unsigned size=0;for(unsigned p=0;p<Carver::kParts;++p)size+=unsigned(carver.PartOf(cell,p).vertices.size());
        if(w>=kWounds||!size||work.pieceCount[w]>=kWoundPieces)return;
        //a piece too big for what is left of this burst starts the wound's next burst, the slots between never fly
        unsigned need=set==BrainSet?kChunkVertices:size,piece=work.pieceCount[w];
        if(need>budget[w][piece/kBurstPieces]&&piece/kBurstPieces+1<kWoundBursts&&piece%kBurstPieces) {
            unsigned next=(piece/kBurstPieces+1)*kBurstPieces;
            for(;piece<next;++piece)work.pieces[w][piece]={{},{},{0,0,1},0,-1,0};
            work.pieceCount[w]=piece;
        }
        unsigned& room=budget[w][piece/kBurstPieces];
        if(set==BrainSet) {
            //torn brain leaves as a compact chunk sized to the tissue it removed, not as a paper-thin surface patch
            if(room<kChunkVertices)return;
            Vec3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};float area=0,bind=0,largest=-1,bones=0;unsigned kind=0;
            for(unsigned p=0;p<kSoftKinds;++p) {
                const auto& part=carver.PartOf(cell,p);area+=part.area;
                if(part.vertices.empty())continue;
                lo={fminf(lo.x,part.lo.x),fminf(lo.y,part.lo.y),fminf(lo.z,part.lo.z)};hi={fmaxf(hi.x,part.hi.x),fmaxf(hi.y,part.hi.y),fmaxf(hi.z,part.hi.z)};
                for(const auto& v:part.vertices)bind+=v.bind;
                //the chunk takes the colour and the bones of the tissue that gave most of it
                if(part.area>largest){largest=part.area;kind=p;bones=part.vertices[0].bones;}
            }
            Vec3 n{0,0,1};Vec3 center=work.posing.Place((lo+hi)*.5f,bind/float(size),bones,&n);
            float radius=Clamp(sqrtf(area)*.32f,.22f,.85f*sqrtf(work.severity[w]))*work.posing.scale;
            Vertex chunk[kChunkVertices];Chunk(radius,uint32_t(carver.SeedOf(cell)*2654435761u)^uint32_t(work.sequence[w]),chunk);
            for(const auto& v:chunk)work.staging[w][kFractureKinds+kind].push_back({center+v.position,v.normal,v.ao,float(piece)});
            room-=kChunkVertices;
            work.pieces[w][work.pieceCount[w]++]={center,seed.motion*work.posing.scale,seed.axis,seed.time,seed.life,seed.spin};
            ++work.brainPieces[w];work.vertices[w]+=kChunkVertices;
            return;
        }
        if(size>room)return;
        room-=size;
        Vec3 center{};
        for(unsigned p=0;p<kFractureKinds;++p) {
            for(const auto& v:carver.PartOf(cell,p).vertices) {
                Vec3 n=v.normal;Vec3 position=work.posing.Place(v.position,v.bind,v.bones,&n);
                work.staging[w][p].push_back({position,n,v.ao,float(piece)});
                center=center+position;
            }
        }
        center=center*(1.0f/float(size));
        work.pieces[w][work.pieceCount[w]++]={center,seed.motion*work.posing.scale,seed.axis,seed.time,seed.life,seed.spin};
        ++work.fragments[w];work.vertices[w]+=size;
    };
    uint8_t brain[kSceneBrainSeeds]{};
    for(unsigned i=0;i<work.seedCount[BrainSet];++i)brain[i]=work.release[work.seeds[BrainSet][i].wound];
    unsigned cells=carver.Carve(model,surface,BrainSet,work.seeds[BrainSet],work.seedCount[BrainSet],brain);
    for(unsigned cell=0;cell<cells;++cell)release(BrainSet,cell);
    uint8_t selected[kSceneBoneSeeds]{};
    for(unsigned i=0;i<work.seedCount[BoneSet];++i) {
        const auto& seed=work.seeds[BoneSet][i];
        selected[i]=seed.state==Plate||(seed.state==Loose&&work.release[seed.wound]);
    }
    cells=carver.Carve(model,surface,BoneSet,work.seeds[BoneSet],work.seedCount[BoneSet],selected);
    unsigned plateOf[Carver::kCells],total=0;
    for(unsigned cell=0;cell<cells;++cell) {
        plateOf[cell]=~0u;
        unsigned index=carver.SeedOf(cell),size=0;
        auto& seed=work.seeds[BoneSet][index];
        if(seed.state==Loose){release(BoneSet,cell);continue;}
        for(unsigned p=0;p<Carver::kParts;++p)size+=unsigned(carver.PartOf(cell,p).vertices.size());
        if(!size)continue;
        //a plate without storage stays attached bone instead of leaving an unexplained hole
        if(work.plateCount>=kPlates||total+size>kPlateVertices){seed.state=Anchor;continue;}
        plateOf[cell]=work.plateCount;work.plateSeeds[work.plateCount++]=index;total+=size;
    }
    unsigned used=0;
    for(unsigned part=0;part<kFractureKinds;++part) {
        work.plateParts[part].first=used;
        for(unsigned cell=0;cell<cells;++cell)if(plateOf[cell]!=~0u)
            for(const auto& v:carver.PartOf(cell,part).vertices)work.plates[used++]={v.position,v.normal,v.ao,float(plateOf[cell]),v.bind,v.bones};
        work.plateParts[part].count=used-work.plateParts[part].first;
    }
    for(unsigned k=0;k<work.plateCount;++k) {
        unsigned index=work.plateSeeds[k];auto& seed=work.seeds[BoneSet][index];
        if(seed.hinged)continue;
        const auto& cluster=work.clusters[seed.cluster];
        Vec3 radial=seed.position-cluster.center;radial=radial-cluster.normal*Dot(radial,cluster.normal);
        if(!Normalize(radial))radial=Cross(cluster.normal,fabsf(cluster.normal.x)<.8f?Vec3{1,0,0}:Vec3{0,1,0});
        Normalize(radial);
        float outer=-1e20f;
        for(unsigned cell=0;cell<cells;++cell)if(carver.SeedOf(cell)==index)
            for(unsigned p=0;p<Carver::kParts;++p)for(const auto& v:carver.PartOf(cell,p).vertices)outer=fmaxf(outer,Dot(v.position-cluster.center,radial));
        Vec3 axis=Cross(cluster.normal,radial);Normalize(axis);
        seed.hinge=cluster.center+radial*outer;seed.axis=axis;seed.hinged=true;
    }
}
void Scene::Submit(const HeadFrame& head,const Appearance& style) {
    if(!dirty_||job_.load(std::memory_order_acquire)!=0||!model_)return;
    while(!Snapshot(work_)&&woundCount_>1)Remove(0);
    dirty_=false;
    if(!woundCount_)return;
    work_.generation=generation_;work_.posing=PosingOf(head,style);work_.pose=head.head;
    for(unsigned w=0;w<woundCount_;++w)wounds_[w].released=true;
    if(!async_){Shatter(*model_,surface_,carver_,work_);job_=2;Adopt();return;}
    if(!thread_) {
        wake_=CreateEventA(nullptr,FALSE,FALSE,nullptr);done_=CreateEventA(nullptr,FALSE,FALSE,nullptr);
        thread_=wake_&&done_?CreateThread(nullptr,0,Worker,this,0,nullptr):nullptr;
        if(!thread_){Shatter(*model_,surface_,carver_,work_);job_=2;Adopt();return;}
    }
    job_.store(1,std::memory_order_release);SetEvent(wake_);
}
void Scene::Adopt() {
    if(job_.load(std::memory_order_acquire)!=2)return;
    if(work_.generation==generation_) {
        for(unsigned set=0;set<kSets;++set) {
            seedCount_[set]=work_.seedCount[set];memcpy(seeds_[set],work_.seeds[set],seedCount_[set]*sizeof(Seed));
        }
        clusterCount_=work_.clusterCount;memcpy(clusters_,work_.clusters,clusterCount_*sizeof(Cluster));
        liveWounds_=work_.wounds;memcpy(born_,work_.born,sizeof born_);
        plateCount_=work_.plateCount;memcpy(plateSeeds_,work_.plateSeeds,sizeof plateSeeds_);
        unsigned vertices=0;
        for(unsigned kind=0;kind<kFractureKinds;++kind){plateParts_[kind]=work_.plateParts[kind];vertices+=plateParts_[kind].count;}
        memcpy(plates_.data(),work_.plates.data(),vertices*sizeof(PieceVertex));++plateRevision_;
        //keep hinge lines with the planned seeds so later snapshots swing the same plates the same way
        for(unsigned k=0;k<plateCount_;++k) {
            const auto& seed=seeds_[BoneSet][plateSeeds_[k]];
            for(unsigned w=0;w<woundCount_;++w)if(wounds_[w].sequence==work_.sequence[seed.wound]&&seed.source<wounds_[w].seedCount[BoneSet]) {
                auto& planned=wounds_[w].seeds[BoneSet][seed.source];
                planned.hinge=seed.hinge;planned.axis=seed.axis;planned.hinged=true;
            }
        }
        ruptureCount_=0;
        for(unsigned w=0;w<work_.wounds;++w)for(unsigned k=0;k<work_.ruptureCount[w];++k){ruptures_[ruptureCount_]=work_.ruptures[w][k];ruptureWound_[ruptureCount_++]=uint8_t(w);}
        for(unsigned w=0;w<work_.wounds;++w)if(work_.release[w])debris_.Commit(work_.pose,work_.born[w],work_.pieces[w],work_.pieceCount[w],work_.staging[w]);
        unsigned last=work_.wounds-1;
        for(unsigned w=0;w<woundCount_;++w)if(wounds_[w].sequence==work_.sequence[last]) {
            const auto& wound=wounds_[w];
            summary_={};summary_.sequence=wound.sequence;summary_.wounds=work_.wounds;
            summary_.boneRuns=wound.runs.bone;summary_.brainRuns=wound.runs.brain;
            for(unsigned c=0;c<wound.clusterCount;++c) {
                auto kind=wound.clusters[c].kind;
                summary_.entry+=kind==EntryKind;summary_.inner+=kind==InnerKind;summary_.exit+=kind==ExitKind;
            }
        }
        for(unsigned k=0;k<plateCount_;++k)summary_.plates+=seeds_[BoneSet][plateSeeds_[k]].wound==last;
        summary_.fragments=work_.fragments[last];summary_.brainPieces=work_.brainPieces[last];summary_.vertices=work_.vertices[last];
        summary_.ruptures=work_.ruptureCount[last];
    }
    job_.store(0,std::memory_order_release);
}
void Scene::Animate(float clock) {
    for(unsigned set=0;set<kSets;++set) {
        unsigned active=0;
        for(unsigned i=0;i<seedCount_[set];++i) {
            const auto& s=seeds_[set][i];
            if(s.wound>=liveWounds_||clock-born_[s.wound]<s.time)continue;
            float* out=seedConstants_[set]+active*4;
            //reach, cluster and state share w: reach<32, cluster<32 so the shader can split them exactly
            out[0]=s.position.x;out[1]=s.position.y;out[2]=s.position.z;
            out[3]=fminf(s.reach,31.9f)+32.0f*float(set==BoneSet?s.cluster:0)+1024.0f*float(s.state);
            ++active;
        }
        activeSeeds_[set]=active;
    }
    memset(ruptured_,0,sizeof ruptured_);
    for(unsigned k=0;k<ruptureCount_;++k) {
        unsigned w=ruptureWound_[k],mesh=ruptures_[k].mesh;
        if(w<liveWounds_&&mesh<128&&clock-born_[w]>=ruptures_[k].time)ruptured_[mesh>>5]|=1u<<(mesh&31);
    }
    activeClusters_=clusterCount_;
    for(unsigned c=0;c<clusterCount_;++c) {
        const auto& cluster=clusters_[c];
        float age=cluster.wound<liveWounds_?clock-born_[cluster.wound]-cluster.time:-1;
        float grow=age<0?0:Clamp(age/.025f,0,1);
        float* out=clusterConstants_+c*4;
        out[0]=cluster.center.x;out[1]=cluster.center.y;out[2]=cluster.center.z;
        out[3]=age<0?0:cluster.ring+(cluster.crack-cluster.ring)*grow*(2-grow);
    }
    for(unsigned k=0;k<plateCount_;++k) {
        const auto& s=seeds_[BoneSet][plateSeeds_[k]];float* row=plateRows_+k*12;
        float age=s.wound<liveWounds_?clock-born_[s.wound]-s.time:-1;
        if(!(age>=0)||!s.hinged){memset(row,0,12*sizeof(float));continue;}
        //damped hinge: starts at rest, settles at the planned angle with a small overshoot
        float settle=1-expf(-age/.035f)*cosf(age*30);
        float angle=s.angle*settle,c=cosf(angle),si=sinf(angle);
        auto rotate=[&](Vec3 v){return v*c+Cross(s.axis,v)*si+s.axis*(Dot(s.axis,v)*(1-c));};
        Vec3 x=rotate({1,0,0}),y=rotate({0,1,0}),z=rotate({0,0,1});
        Vec3 offset=s.hinge-rotate(s.hinge)+s.motion*Clamp(settle,0,1.2f);
        const float values[12]={x.x,y.x,z.x,offset.x,x.y,y.y,z.y,offset.y,x.z,y.z,z.z,offset.z};
        memcpy(row,values,sizeof values);
    }
}
void Scene::Update(const Model& model,const HeadFrame& head,const Appearance& style,const Impact::Frame& frame,float severity,float blood) {
    float clock=Clock(frame);clock_=clock;
    bool valid=frame.path.valid&&Finite(frame.path.entry)&&Finite(frame.path.exit)&&std::isfinite(frame.age)&&frame.age>=0;
    if(!valid){Advance(head,style,frame);return;}
    Adopt();
    bool fresh=!woundCount_||wounds_[woundCount_-1].sequence!=frame.sequence;
    if(!fresh) {
        auto& latest=wounds_[woundCount_-1];
        //a changed path or a replay that rewinds the same shot rebuilds it with the current head pose
        if(Length(latest.path.entry-frame.path.entry)>.001f||Length(latest.path.exit-frame.path.exit)>.001f
            ||latest.bullet.radius!=frame.bullet.radius||frame.age<latest.age) {
            Remove(woundCount_-1);fresh=true;
        }else{latest.born=clock-frame.age;latest.age=frame.age;}
    }else if(frame.sceneTime<0)ClearWounds();
    if(fresh) {
        if(woundCount_==kWounds)Remove(0);
        auto& wound=wounds_[woundCount_++];
        wound.sequence=frame.sequence;wound.born=clock-frame.age;wound.age=frame.age;wound.path=frame.path;wound.bullet=frame.bullet;wound.released=false;
        wound.seedCount[BoneSet]=wound.seedCount[BrainSet]=0;wound.clusterCount=0;wound.runs={};wound.ruptureCount=0;wound.cueCount=0;
        Posing posing=PosingOf(head,style);
        Vec3 entry=Fit::Unwarp(head.fit,(frame.path.entry-posing.offset)*(1/posing.scale)),exit=Fit::Unwarp(head.fit,(frame.path.exit-posing.offset)*(1/posing.scale));
        Shot shot;shot.origin=entry;shot.direction=exit-entry;shot.length=Length(shot.direction);
        shot.bullet=frame.bullet;shot.sequence=frame.sequence;
        shot.entry=frame.path.entry;shot.exit=frame.path.exit;shot.posing=&posing;
        wound.severity=shot.severity=Clamp(severity,1,3);
        if(Normalize(shot.direction)&&shot.length>.1f) {
            float fitted=Length(frame.path.exit-frame.path.entry);
            shot.secondsPerUnit=Impact::TimeAt(frame,1)*fitted/shot.length;
            Plan plan;
            for(unsigned set=0;set<kSets;++set){plan.seeds[set]=wound.seeds[set];plan.capacity[set]=kSeedCapacity[set];}
            plan.clusters=wound.clusters;plan.clusterCapacity=kClusters;
            wound.runs=PlanWound(model,surface_,shot,plan);
            wound.ruptureCount=PlanRuptures(model,shot,wound.ruptures,kRuptures);
            spray_.Emit(head.head,wound.born,frame.sequence,frame.path.entry,frame.path.exit,Impact::TimeAt(frame,fitted),wound.ruptures,wound.ruptureCount,
                wound.severity,blood);
            for(unsigned set=0;set<kSets;++set)wound.seedCount[set]=plan.count[set];
            wound.clusterCount=plan.clusterCount;
            wound.cueCount=PlanCues(wound.seeds[BrainSet],wound.seedCount[BrainSet],wound.clusters,wound.clusterCount,wound.ruptures,wound.ruptureCount,
                Impact::TimeAt(frame,fitted),wound.cues);
        }
        dirty_=true;bulletReleased_=false;++woundRevision_;
    }
    Submit(head,style);
    Animate(clock);
    Hear(clock);
    bullets_=Impact::BuildBullet(frame,bullet_,BodySize(head));
    if(Impact::Distance(frame)>=Length(frame.path.exit-frame.path.entry)+frame.bullet.length&&!bulletReleased_) {
        bulletRelease_=head.head;bulletReleased_=true;
    }
    if(bulletReleased_)for(unsigned i=0;i<bullets_;++i) {
        auto& v=bullet_[i];v.position=head.head.Unapply(bulletRelease_.Apply(v.position));
        v.normal=head.head.Unrotate(bulletRelease_.Rotate(v.normal));
    }
}
void Scene::Advance(const HeadFrame& head,const Appearance& style,const Impact::Frame& frame) {
    float clock=Clock(frame);clock_=clock;
    Adopt();
    //without a shared scene clock earlier wounds cannot be timed against this shot
    if(frame.sceneTime<0)ClearWounds();
    Submit(head,style);
    bullets_=0;
    Animate(clock);
    Hear(clock);
}
void Scene::Hear(float clock) {
    for(unsigned w=0;w<woundCount_;++w)for(unsigned k=0;k<wounds_[w].cueCount;++k) {
        auto& cue=wounds_[w].cues[k];
        if(cue.fired||!(clock-wounds_[w].born>=cue.time))continue;
        cue.fired=true;
        if(heardCount_<sizeof heard_)heard_[heardCount_++]=cue.kind;
    }
}
unsigned Scene::TakeCues(uint8_t* out,unsigned capacity) {
    unsigned count=heardCount_<capacity?heardCount_:capacity;
    memcpy(out,heard_,count);heardCount_=0;
    return count;
}
unsigned Scene::Status() const {
    if(!liveWounds_)return 0;
    return (summary_.entry?Graphics::EntryBreak:0)|(summary_.exit?Graphics::ExitBreak:0)|(summary_.inner?Graphics::InnerBreak:0)
        |(summary_.plates?Graphics::HingedPlates:0)|(summary_.fragments?Graphics::BoneFragments:0)|(summary_.brainPieces||summary_.brainRuns?Graphics::BrainTorn:0);
}
void Scene::Keep(std::vector<Kept>& out) const {
    out.resize(woundCount_);
    for(unsigned w=0;w<woundCount_;++w) {
        const auto& wound=wounds_[w];auto& k=out[w];
        k.sequence=wound.sequence;k.path=wound.path;k.bullet=wound.bullet;k.runs=wound.runs;k.severity=wound.severity;
        for(unsigned set=0;set<kSets;++set)k.seeds[set].assign(wound.seeds[set],wound.seeds[set]+wound.seedCount[set]);
        k.clusters.assign(wound.clusters,wound.clusters+wound.clusterCount);
        k.ruptures.assign(wound.ruptures,wound.ruptures+wound.ruptureCount);
    }
}
void Scene::Restore(const std::vector<Kept>& kept) {
    ClearWounds();
    //born so long ago that every break has settled, already released so nothing flies again
    constexpr float kSettled=-1e9f;
    size_t first=kept.size()>kWounds?kept.size()-kWounds:0;
    for(size_t i=first;i<kept.size();++i) {
        const auto& k=kept[i];auto& wound=wounds_[woundCount_++];
        wound.sequence=k.sequence;wound.born=kSettled;wound.age=0;wound.path=k.path;wound.bullet=k.bullet;wound.released=true;
        wound.runs=k.runs;wound.severity=k.severity;wound.cueCount=0;
        for(unsigned set=0;set<kSets;++set) {
            wound.seedCount[set]=unsigned(k.seeds[set].size()<kSeedCapacity[set]?k.seeds[set].size():kSeedCapacity[set]);
            memcpy(wound.seeds[set],k.seeds[set].data(),wound.seedCount[set]*sizeof(Seed));
        }
        wound.clusterCount=unsigned(k.clusters.size()<kClusters?k.clusters.size():kClusters);
        memcpy(wound.clusters,k.clusters.data(),wound.clusterCount*sizeof(Cluster));
        wound.ruptureCount=unsigned(k.ruptures.size()<kRuptures?k.ruptures.size():kRuptures);
        memcpy(wound.ruptures,k.ruptures.data(),wound.ruptureCount*sizeof(Rupture));
    }
    dirty_=woundCount_>0;
}
}
