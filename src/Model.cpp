#include "Model.h"
#include <cstdio>
#include <cstring>

namespace XRay {
bool Model::Load(const char* path) {
    meshes.clear();
    FILE* f=nullptr;
    if (fopen_s(&f,path,"rb") || !f) return false;
    struct Close { FILE* f; ~Close(){fclose(f);} } close{f};
    auto read=[&](void* p,size_t n){return fread(p,1,n,f)==n;};
    uint32_t header[4];
    //version 2 stores eight floats per vertex and binds between the head and neck, version 3 adds the influence pair
    if (!read(header,sizeof header)||memcmp(header,"XRA1",4)||(header[1]!=2&&header[1]!=3)||!header[2]||header[2]>128||header[3]) return false;
    size_t stride=header[1]==2?sizeof(float)*8:sizeof(Vertex);
    std::vector<Mesh> next;
    uint32_t totalVertices=0,totalIndices=0;
    for (uint32_t n=0;n<header[2];++n) {
        uint32_t h[4];
        if (!read(h,sizeof h)||h[0]>=kMaterialCount||!h[1]||h[1]>3||!h[2]||h[2]>65535||!h[3]||h[3]%3||h[3]>600000) return false;
        totalVertices+=h[2]; totalIndices+=h[3];
        if (totalVertices>400000||totalIndices>1200000) return false;
        Mesh m{h[0],h[1],{}, {}}; m.vertices.resize(h[2]); m.indices.resize(h[3]);
        bool neck=false;
        for (auto& v:m.vertices) {if (!read(&v,stride)) return false;neck|=v.bind<1;}
        if (header[1]==2) for (auto& v:m.vertices) v.bones=neck?1.0f:0.0f;
        if (!read(m.indices.data(),m.indices.size()*sizeof(uint16_t))) return false;
        m.first=FirstInfluence(m.vertices[0].bones);m.second=SecondInfluence(m.vertices[0].bones);
        for (const auto& v:m.vertices) {
            float nn=v.normal.x*v.normal.x+v.normal.y*v.normal.y+v.normal.z*v.normal.z;
            if (!Finite(v.position)||!Finite(v.normal)||!std::isfinite(v.ao)||v.ao<0||v.ao>1||nn<.5f||nn>1.5f
                ||!std::isfinite(v.bind)||v.bind<0||v.bind>1||v.bones!=float(m.first*16+m.second)||m.first>=kInfluences||m.second>=kInfluences
                ||fabsf(v.position.x)>100||fabsf(v.position.y)>100||fabsf(v.position.z)>100) return false;
        }
        for (auto i:m.indices) if (i>=m.vertices.size()) return false;
        for (const auto& v:m.vertices) m.bound|=m.first!=0||(v.bind<1&&m.second!=0);
        next.push_back(std::move(m));
    }
    if (fgetc(f)!=EOF||ferror(f)) return false;
    meshes=std::move(next); return true;
}
}
