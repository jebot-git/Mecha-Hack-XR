#include "rpg/world_generation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace iron::rpg {
namespace {
constexpr float halfWidth=25.f, rampRun=110.f, headroom=32.f;
struct Alignment {
    Vec3 center;
    bool alongX;
    float halfSpan;
    Vec3 at(float station, float offset=0) const {
        return alongX ? center+Vec3{station,0,offset} : center+Vec3{offset,0,station};
    }
    float station(Vec3 p) const { return alongX?p.x-center.x:p.z-center.z; }
    float offset(Vec3 p) const { return alongX?p.z-center.z:p.x-center.x; }
    Vec3 size(float along,float vertical,float across) const {
        return alongX?Vec3{along,vertical,across}:Vec3{across,vertical,along};
    }
    float toe() const {return halfSpan+rampRun;}
};
Vec3 grounded(const World &w,Vec3 p) {p.y=w.surfaceHeight(p.x,p.z);return p;}

bool approachClear(const World &w,const Alignment &line,Vec3 end,Vec3 anchor,float sign) {
    // Join the ground network from outside the toe of the ramp.
    if(sign*line.station(anchor)<line.toe()-3) return false;
    const int count=std::max(1,int(std::ceil(length(anchor-end)/8)));
    for(int i=0;i<=count;++i) {
        const auto p=grounded(w,end+(anchor-end)*(float(i)/count));
        if(!w.groundPassable(p,8)||std::isfinite(w.interiorCeilingAt(p,15)))return false;
        for(const auto &t:w.targets) if(t.active&&t.health>0&&t.spawnType<0&&
            std::abs(t.center.x-p.x)<t.half.x+16&&std::abs(t.center.z-p.z)<t.half.z+16&&
            t.center.y-t.half.y<p.y+28&&t.center.y+t.half.y>p.y) return false;
    }
    return true;
}

// Riveted metal side frames on the canal span; the road flyover uses a concrete fascia.
// Keep the open web entirely outside the carriageway and inside the structural collider.
void fitBridgeTruss(Target &target,const Alignment &line) {
    auto frame=std::make_shared<WorldMesh>();
    const float length=line.halfSpan*2, h=target.half.y*2;
    const Vec3 iron{.31f,.39f,.39f}, ochre{.67f,.47f,.23f};
    auto position=[&](float station,float y,float side){
        return line.alongX?Vec3{station,y,side}:Vec3{side,y,station};
    };
    auto girder=[&](float x0,float y0,float x1,float y1,float thickness,Vec3 color){
        std::array<Vec3,8> v;
        for(int side=0;side<2;++side){
            const float z=(side?1.f:-1.f)*1.25f;
            v[side*4]=position(x0,y0,z);v[side*4+1]=position(x1,y1,z);
            v[side*4+2]=position(x1,y1+thickness,z);v[side*4+3]=position(x0,y0+thickness,z);
        }
        constexpr int quads[][4]={{0,3,2,1},{4,5,6,7},{0,1,5,4},{3,7,6,2},{0,4,7,3},{1,2,6,5}};
        for(int face=0;face<6;++face){
            for(int corner:{0,1,2,0,2,3}){
                const Vec3 p=v[quads[face][corner]];
                const float station=line.alongX?p.x:p.z;
                // Existing PSX steel trim; eight metres per repeat on the long elevation.
                const float side=line.alongX?p.z:p.x;
                const float u=(face>=4?side:station)/8;
                const float vCoord=(face==2||face==3?side:p.y)/8;
                frame->push_back({p,color*(face==2?.60f:face==3?1.f:.82f),u,vCoord,3,61});
            }
        }
    };
    girder(-line.halfSpan,0,line.halfSpan,0,1.0f,iron);
    girder(-line.halfSpan,h-1.15f,line.halfSpan,h-1.15f,1.15f,ochre);
    const int bays=std::max(4,int(std::round(length/14)));
    for(int i=0;i<bays;++i){
        const float low=std::lerp(-line.halfSpan,line.halfSpan,float(i)/bays);
        const float high=std::lerp(-line.halfSpan,line.halfSpan,float(i+1)/bays);
        girder(low,i%2?h-1.7f:.60f,high,i%2?.60f:h-1.7f,1.1f,iron);
    }
    target.mesh=frame;target.wreck=frame;
}

bool install(World &w,const Alignment &line,bool overWater) {
    auto first=grounded(w,line.at(-line.toe())),last=grounded(w,line.at(line.toe()));
    for(auto p:{first,last}) {
        if(p.x<w.movementBounds[0]+32||p.z<w.movementBounds[1]+32||
           p.x>w.movementBounds[2]-32||p.z>w.movementBounds[3]-32||
           w.waterDepthAt(p,12)>.1f) return false;
    }
    // An already-occupied footprint and a crossing in the insertion court are unsuitable.
    if(std::abs(line.station(w.missionEntry))<line.toe()+30&&
       std::abs(line.offset(w.missionEntry))<halfWidth+60)return false;
    for(const auto &t:w.targets) {
        if(!t.active||t.health<=0)continue;
        const auto h=line.size(line.toe()+18,0,halfWidth+18);
        if(std::abs(line.center.x-t.center.x)<h.x+t.half.x&&
           std::abs(line.center.z-t.center.z)<h.z+t.half.z)return false;
    }
    float top=std::max(first.y,last.y);
    for(float s:{-line.halfSpan,0.f,line.halfSpan})
        for(float offset:{-halfWidth,0.f,halfWidth})
            top=std::max(top,grounded(w,line.at(s,offset)).y);
    top+=headroom+2;
    if(top-std::min(first.y,last.y)>rampRun*.39f)return false;
    auto height=[&](float station){
        if(station<-line.halfSpan)return std::lerp(first.y,top,(station+line.toe())/rampRun);
        if(station>line.halfSpan)return std::lerp(top,last.y,(station-line.halfSpan)/rampRun);
        return top;
    };
    const int steps=int(std::ceil(line.toe()*2/8));
    for(int i=0;i<=steps;++i){
        float s=std::lerp(-line.toe(),line.toe(),float(i)/steps);
        for(float side:{-halfWidth+2,0.f,halfWidth-2}){
            const auto p=grounded(w,line.at(s,side));
            if(std::isfinite(w.interiorCeilingAt(p,8))||p.y>height(s)+.5f||
               ((i==0||i==steps)&&std::abs(p.y-height(s))>.65f)||
               w.waterDepthAt(p,4)>w.waterTraversalLimitAt(p,4))return false;
        }
    }
    // Preserve existing at-grade routes. Only the flat high span may cross one.
    for(auto edge:w.interiorNavLinks) {
        auto a=w.interiorNavNodes[edge[0]],b=w.interiorNavNodes[edge[1]];
        const int samples=std::max(1,int(std::ceil(length(b-a)/8)));
        for(int i=0;i<=samples;++i){
            auto p=a+(b-a)*(float(i)/samples);
            float s=line.station(p);
            if(std::abs(s)>line.toe()||std::abs(line.offset(p))>halfWidth+9)continue;
            const float gap=height(s)-p.y;
            if(gap>1.f&&gap<headroom)return false;
        }
    }
    // Piers stand outside both the crossing carriageway and protected lower routes.
    for(float sign:{-1.f,1.f})for(float side:{-1.f,1.f}){
        auto p=line.at(sign*(line.halfSpan-3),side*(halfWidth-2));
        if(missionRouteDistance(w,p)<25.f)return false;
    }
    const unsigned missing=unsigned(w.interiorNavNodes.size());
    unsigned anchors[]{missing,missing};
    for(int end=0;end<2;++end){
        float best=std::numeric_limits<float>::infinity();
        const float sign=end?1.f:-1.f;
        const auto toe=end?last:first;
        for(unsigned i=0;i<missing;++i){
            const auto p=w.interiorNavNodes[i];
            float dist=length(toe-p);
            if(dist>270||dist>=best)continue;
            if(approachClear(w,line,toe,p,sign)){best=dist;anchors[end]=i;}
        }
        if(anchors[end]==missing)return false;
    }
    auto left=line.at(-line.halfSpan),right=line.at(line.halfSpan);
    left.y=right.y=top;
    const Vec3 color=overWater?Vec3{.37f,.42f,.40f}:Vec3{.42f,.40f,.36f};
    auto slab=[&](Vec3 a,Vec3 b) {
        auto half=line.size(0,0,halfWidth);
        addMissionDeck(w,{{std::min(a.x,b.x)-half.x,std::min(a.z,b.z)-half.z,
                           std::max(a.x,b.x)+half.x,std::max(a.z,b.z)+half.z},a,b},color);
    };
    slab(first,left);slab(left,right);slab(right,last);
    unsigned previous=anchors[0];
    for(auto p:{first,left,right,last}){
        unsigned next=unsigned(w.interiorNavNodes.size());
        w.interiorNavNodes.push_back(p);w.interiorNavLinks.push_back({previous,next});previous=next;
    }
    w.interiorNavLinks.push_back({previous,anchors[1]});
    for(float side:{-1.f,1.f}){
        auto edge=line.at(0,side*(halfWidth-1));edge.y=top+.7f;
        addStructuralPiece(w,"city_barrier",edge,line.size(line.halfSpan,.7f,1));
        // Deep side girders describe the canal span; the road viaduct uses a shallow fascia.
        const float girder=overWater?4.f:1.25f;
        edge.y=top-girder;
        addStructuralPiece(w,"dock_pier",edge,line.size(line.halfSpan,girder,1.25f));
        if(overWater)fitBridgeTruss(w.targets.back(),line);
        for(float sign:{-1.f,1.f}){
            auto p=grounded(w,line.at(sign*(line.halfSpan-3),side*(halfWidth-2)));
            const float y=(p.y+top-1.2f)*.5f;
            addStructuralPiece(w,"city_barrier",{p.x,y,p.z},line.size(3.5f,y-p.y,2.8f));
        }
    }
    return true;
}
} // namespace

void buildMissionCrossing(World &w,int biome,int location,int region,float extent) {
    if(extent<750||region>=2||biome==2||w.prototypeSeed%3!=0||
       !w.interiorDecks.empty()||(location!=0&&location!=4&&location!=6))return;
    if(location==0||location==4){
        for(const auto &water:w.waterRegions){
            const auto &b=water.bounds;
            const bool alongX=b[2]-b[0]<b[3]-b[1];
            const float width=alongX?b[2]-b[0]:b[3]-b[1];
            if(width<28||width>extent*.45f)continue;
            for(float station:{.5f,.30f,.70f,.16f,.84f}){
                Vec3 p{std::lerp(b[0],b[2],alongX?.5f:station),0,
                       std::lerp(b[1],b[3],alongX?station:.5f)};
                if(w.waterDepthAt(grounded(w,p))<1)continue;
                if(install(w,{p,alongX,width*.5f+12},true))return;
            }
        }
    }
    if(location==4)return;
    for(auto edge:w.interiorNavLinks){
        const auto a=w.interiorNavNodes[edge[0]],b=w.interiorNavNodes[edge[1]];
        const auto delta=b-a;
        if(std::max(std::abs(delta.x),std::abs(delta.z))<85||
           std::min(std::abs(delta.x),std::abs(delta.z))>1)continue;
        for(float fraction:{.5f,.33f,.67f}){
            auto p=a+delta*fraction;p.y=0;
            if(w.waterDepthAt(grounded(w,p),20)>.1f)continue;
            if(install(w,{p,std::abs(delta.x)<1,50.f},false))return;
        }
    }
}
} // namespace iron::rpg
