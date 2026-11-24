#include "cockpit_glass.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace iron {
namespace {
Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
const std::vector<Vec3> front{
    {0,-.70f,-1.53f},{1.18f,-.60f,-1.20f},{1.18f,-.18f,-1.20f},
    {1.05f,.67f,-1.40f},{.82f,.83f,-1.47f},{.42f,.88f,-1.52f},
    {-.42f,.88f,-1.52f},{-.82f,.83f,-1.47f},{-1.05f,.67f,-1.40f},
    {-1.18f,-.18f,-1.20f},{-1.18f,-.60f,-1.20f}
};
void pane(CockpitGlazing& mesh,const std::vector<Vec3>& outline,int plane,Vec3 centre){
    auto uv=[&](Vec3 p)->std::array<float,2>{return plane==0?std::array{p.x,p.y}:plane==1?std::array{-p.z,p.y}:std::array{p.x,-p.z};};
    float lo[2]={1e6f,1e6f},hi[2]={-1e6f,-1e6f};
    for(auto p:outline){auto q=uv(p);for(int i=0;i<2;++i){lo[i]=std::min(lo[i],q[i]);hi[i]=std::max(hi[i],q[i]);}}
    unsigned start=unsigned(mesh.vertices.size());
    for(size_t i=0;i<outline.size();++i){auto a=outline[i],b=outline[(i+1)%outline.size()];auto normal=cross(a-centre,b-centre);float size=length(normal);if(size<1e-7f)continue;normal=normal*(1/size);
        for(auto p:{centre,a,b}){auto q=uv(p);mesh.vertices.push_back({p,normal,{(q[0]-lo[0])/(hi[0]-lo[0]),(q[1]-lo[1])/(hi[1]-lo[1])},q});}
    }
    mesh.panes.push_back({start,unsigned(mesh.vertices.size())-start});
}
}
CockpitGlazing cockpitGlazing(){
    CockpitGlazing mesh;
    pane(mesh,front,0,{0,.05f,-1.53f});
    // Full side openings include the lower knee and rear upper return. The old
    // short quads left both of those regions unglazed.
    for(float side:{-1.f,1.f}){
        std::vector<Vec3> edge{{side*1.21f,-.90f,.55f},{side*1.22f,-.60f,-.45f},
            {side*1.18f,-.18f,-1.20f},{side*1.05f,.67f,-1.40f},
            {side*.90f,.94f,-.55f},{side*.88f,.88f,.65f}};
        pane(mesh,edge,1,{side*1.08f,.05f,-.40f});
    }
    // Roof cutouts follow the arched brow, then the two upper spine segments.
    pane(mesh,{{-1.05f,.67f,-1.40f},{-.82f,.83f,-1.47f},{-.42f,.88f,-1.52f},
        {.42f,.88f,-1.52f},{.82f,.83f,-1.47f},{1.05f,.67f,-1.40f},
        {.90f,.94f,-.55f},{-.90f,.94f,-.55f}},2,{0,.93f,-1.02f});
    pane(mesh,{{-.90f,.94f,-.55f},{.90f,.94f,-.55f},{.88f,.88f,.65f},{-.88f,.88f,.65f}},2,{0,.91f,.05f});
    return mesh;
}
Vec3 cockpitFrontGlass(float x,float y){
    static const auto mesh=cockpitGlazing();
    y=std::clamp(y,-.58f,.87f);float low=1e6f,high=-1e6f;
    for(size_t i=0;i<front.size();++i){auto a=front[i],b=front[(i+1)%front.size()];
        if((a.y<=y&&b.y>=y)||(b.y<=y&&a.y>=y))if(std::abs(b.y-a.y)>1e-6f){float at=a.x+(b.x-a.x)*(y-a.y)/(b.y-a.y);low=std::min(low,at);high=std::max(high,at);}}
    if(low<high)x=std::clamp(x,low+.004f,high-.004f);
    const auto& p=mesh.panes[0];
    for(unsigned i=p.first;i<p.first+p.count;i+=3){auto a=mesh.vertices[i].position,b=mesh.vertices[i+1].position,c=mesh.vertices[i+2].position;
        float d=(b.y-c.y)*(a.x-c.x)+(c.x-b.x)*(a.y-c.y);if(std::abs(d)<1e-8f)continue;
        float u=((b.y-c.y)*(x-c.x)+(c.x-b.x)*(y-c.y))/d,v=((c.y-a.y)*(x-c.x)+(a.x-c.x)*(y-c.y))/d,w=1-u-v;
        if(u>=-.0001f&&v>=-.0001f&&w>=-.0001f)return {x,y,a.z*u+b.z*v+c.z*w+.0015f};
    }
    return {x,y,-1.5285f};
}
}
