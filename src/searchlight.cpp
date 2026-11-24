#include "searchlight.hpp"
#include "simulation.hpp"
#include <limits>
namespace iron {
namespace {
float intersect(Vec3 from,Vec3 to,const Target& t,Vec3* normal=nullptr){
    float low=0,high=1;auto delta=to-from;Vec3 face{};
    for(unsigned axis=0;axis<3;++axis){float p=axis==0?from.x:axis==1?from.y:from.z,d=axis==0?delta.x:axis==1?delta.y:delta.z;
        float c=axis==0?t.center.x:axis==1?t.center.y:t.center.z,h=axis==0?t.half.x:axis==1?t.half.y:t.half.z;
        if(std::abs(d)<.00001f){if(std::abs(p-c)>h)return 2;}
        else {float a=(c-h-p)/d,b=(c+h-p)/d;float sign=-1;if(a>b){std::swap(a,b);sign=1;}
            if(a>low){low=a;face=axis==0?Vec3{sign,0,0}:axis==1?Vec3{0,sign,0}:Vec3{0,0,sign};}high=std::min(high,b);if(low>high)return 2;}
    }
    if(normal)*normal=face;
    return low;
}
}
VisibilityRanges visibilityRanges(const World& w){
    if(!w.visibilityRules())return {10000,10000};
    if(weatherLightningActive(w.activeEnvironment(),w.weatherTime()))return {250,250};
    auto view=environmentVisuals(w.activeEnvironment(),w.night,w.skyColor,w.groundColor,-1);
    float base=view.density>0?std::min(250.f,1/view.density):250.f;if(view.night)base=std::min(base,90.f);
    return {base,base<250?std::min(250.f,base*2):base};
}
Vec3 searchlightOrigin(const Pilot& p){return p.bodyPoint(10)+rotate(p.gravityFacing(),{0,0,-3});}
Vec3 searchlightDirection(const Pilot& p){return rotate(p.gravityFacing(),direction(0,-.12f));}
bool searchlightActive(const Pilot& p){return p.searchlightOn&&!p.lifeSpent&&p.health>0&&p.missionStatus==MissionStatus::Active;}
bool searchlightCovers(const World& w,const Pilot& p,Vec3 point){
    if(!w.visibilityRules()||!searchlightActive(p))return false;
    auto delta=point-searchlightOrigin(p);float distance=length(delta);
    return length(point-(p.bodyPoint(6)))<=visibilityRanges(w).lit&&distance>0&&dot(unit(delta),searchlightDirection(p))>=.9063078f; // 25-degree half cone
}
bool visibilityInRange(const World& w,const Pilot& observer,Vec3 point,bool revealed){
    if(!w.visibilityRules())return true;
    auto ranges=visibilityRanges(w);float distance=length(point-(observer.position+Vec3{0,6,0}));
    return distance<=(revealed?ranges.lit:ranges.base)||searchlightCovers(w,observer,point);
}
bool World::canPerceive(const Pilot& observer,Vec3 point,bool revealed,int ignored)const{
    if(!visibilityRules())return true;
    if(!visibilityInRange(*this,observer,point,revealed))return false;
    auto from=observer.position+Vec3{0,6,0};if(groundContact(from,point)<.999f)return false;
    for(size_t i=0;i<targets.size();++i){const auto& t=targets[i];if(int(i)==ignored||!t.active||t.escaped||t.health<=0||t.spawnType==21)continue;
        // An observer standing inside its own actor bounds must not block itself.
        if(std::abs(from.x-t.center.x)<=t.half.x&&std::abs(from.y-t.center.y)<=t.half.y&&std::abs(from.z-t.center.z)<=t.half.z)continue;
        if(intersect(from,point,t)<.999f)return false;
    }return true;
}
SearchlightBeam traceSearchlight(const World& w,const Pilot& p){
    SearchlightBeam beam;beam.origin=searchlightOrigin(p);beam.direction=searchlightDirection(p);
    float range=visibilityRanges(w).lit;auto end=beam.origin+beam.direction*range;float first=w.groundContact(beam.origin,end);bool groundHit=first<=1;Vec3 normal{0,1,0};
    for(const auto& t:w.targets){if(!t.active||t.escaped||t.health<=0||t.spawnType==21)continue;Vec3 face;float f=intersect(beam.origin,end,t,&face);if(f<first){first=f;normal=face;groundHit=false;}}
    beam.hit=first<=1;first=std::min(first,1.f);beam.end=beam.origin+(end-beam.origin)*first;beam.length=length(beam.end-beam.origin);
    if(beam.hit&&groundHit){float x=beam.end.x,z=beam.end.z;normal=unit(Vec3{w.surfaceHeight(x-.5f,z)-w.surfaceHeight(x+.5f,z),1,w.surfaceHeight(x,z-.5f)-w.surfaceHeight(x,z+.5f)});}
    beam.normal=normal;return beam;
}
}
