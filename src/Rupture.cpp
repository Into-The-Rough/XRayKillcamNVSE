#include "Rupture.h"
#include <cfloat>

namespace XRay::Damage {
unsigned PlanRuptures(const Model& model,const Shot& shot,Rupture* out,unsigned capacity) {
    //the anatomy is placed where its influences hold it beside the frame-space path
    if(!shot.posing||!(shot.length>0))return 0;
    Vec3 line=shot.exit-shot.entry;float span=Length(line),reach=kRuptureReach*WoundScale(shot.bullet)*sqrtf(fmaxf(shot.severity,1));
    if(!Normalize(line))return 0;
    unsigned count=0;
    for(size_t n=0;n<model.meshes.size()&&count<capacity;++n) {
        const auto& mesh=model.meshes[n];
        if(BurstSeverity(mesh.material)>shot.severity||!(mesh.visibility&1)||mesh.vertices.empty())continue;
        float best=FLT_MAX,at=0;Vec3 sum{0,0,0};
        for(const auto& v:mesh.vertices) {
            Vec3 p=shot.posing->Place(v.position,v.bind,v.bones);sum=sum+p;
            Vec3 offset=p-shot.entry;float along=Clamp(Dot(offset,line),0,span),distance=Length(offset-line*along);
            if(distance<best){best=distance;at=along;}
        }
        if(best>reach)continue;
        out[count++]={sum*(1.0f/float(mesh.vertices.size())),line,at/span*shot.length*shot.secondsPerUnit,uint16_t(n),fmaxf(shot.severity,1)};
    }
    return count;
}
unsigned Spill(const Model& model,const Posing& posing,const Rupture& rupture,uint64_t key,unsigned* budgets,
    std::vector<PieceVertex>& staging,unsigned first,PieceMotion* pieces,unsigned capacity) {
    if(rupture.mesh>=model.meshes.size()||model.meshes[rupture.mesh].vertices.empty())return 0;
    const auto& mesh=model.meshes[rupture.mesh];
    Vec3 lo{FLT_MAX,FLT_MAX,FLT_MAX},hi{-FLT_MAX,-FLT_MAX,-FLT_MAX},centre{0,0,0};
    for(const auto& v:mesh.vertices) {
        lo={fminf(lo.x,v.position.x),fminf(lo.y,v.position.y),fminf(lo.z,v.position.z)};
        hi={fmaxf(hi.x,v.position.x),fmaxf(hi.y,v.position.y),fmaxf(hi.z,v.position.z)};centre=centre+v.position;
    }
    centre=centre*(1.0f/float(mesh.vertices.size()));
    //an ellipsoid fills pi/6 of its box, each lump a sphere of an equal share
    Vec3 size=hi-lo;float volume=.5236f*size.x*size.y*size.z*kRuptureSwell;
    unsigned lumps=unsigned(Clamp(float(kRupturePieces)*rupture.force,float(kRupturePieces),float(kRupturePiecesMost)));
    float radius=cbrtf(volume/float(lumps)*.2387f)*posing.scale;
    Random random(key,rupture.mesh+101u);
    unsigned added=0;
    for(unsigned i=0;i<lumps&&first+added<capacity&&budgets[(first+added)/kBurstPieces]>=kChunkVertices;++i) {
        const auto& v=mesh.vertices[unsigned(random.Next()*float(mesh.vertices.size()))%mesh.vertices.size()];
        Vec3 at=posing.Place(centre+(v.position-centre)*random.Range(.15f,.7f),v.bind,v.bones);
        Vertex chunk[kChunkVertices];Chunk(radius*random.Range(.7f,1.3f),uint32_t(key*2654435761u)^(i*0x9E3779B9u)^rupture.mesh,chunk);
        unsigned piece=first+added;
        for(const auto& c:chunk)staging.push_back({at+c.position,c.normal,c.ao,float(piece)});
        Vec3 out=at-rupture.centre;if(!Normalize(out))out=rupture.direction;
        Vec3 axis{random.Range(-1,1),random.Range(-1,1),random.Range(-1,1)};if(!Normalize(axis))axis={0,0,1};
        Vec3 velocity=(rupture.direction*random.Range(20,55)+out*random.Range(25,50))*(posing.scale*rupture.force);
        pieces[piece]={at,velocity,axis,rupture.time+random.Range(0,.003f),random.Range(1.0f,1.5f),random.Range(4,11)*(random.Next()<.5f?-1.0f:1.0f)};
        budgets[piece/kBurstPieces]-=kChunkVertices;++added;
    }
    return added;
}
}
