#include "rpg/surface_gravity.hpp"
#include "simulation.hpp"

namespace iron::rpg {
namespace {
constexpr float footGap=.15f;
constexpr float captureRange=68.f;
bool largeStructure(const Target &t) {
    return t.spawnType<0&&t.active&&!t.escaped&&t.health>0&&t.mesh&&
        int(t.half.x>=4)+int(t.half.y>=4)+int(t.half.z>=4)>=2;
}
bool intersectsBounds(Vec3 from,Vec3 to,const Target &t) {
    const auto delta=to-from;
    float entryFraction=0,exitFraction=1;
    const float start[]={from.x-t.center.x,from.y-t.center.y,from.z-t.center.z};
    const float step[]={delta.x,delta.y,delta.z};
    const float half[]={t.half.x+.1f,t.half.y+.1f,t.half.z+.1f};
    for(int axis=0;axis<3;++axis){
        if(std::abs(step[axis])<1e-6f){
            if(std::abs(start[axis])>half[axis])return false;
            continue;
        }
        const float a=(-half[axis]-start[axis])/step[axis],b=(half[axis]-start[axis])/step[axis];
        entryFraction=std::max(entryFraction,std::min(a,b));exitFraction=std::min(exitFraction,std::max(a,b));
        if(entryFraction>exitFraction)return false;
    }
    return true;
}
bool fits(const World &world,const GravityContact &hit,float height,float radius) {
    const auto up=hit.normal;
    const auto side=unit(cross(up,std::abs(up.y)<.85f?Vec3{0,1,0}:Vec3{0,0,1}));
    const auto forward=cross(up,side);
    for(const auto offset:{Vec3{},side*radius,side*(-radius),forward*radius,forward*(-radius)}){
        const auto start=hit.point+up*.42f+offset;
        const auto top=start+up*(height-.24f);
        if(top.y<world.surfaceHeight(top.x,top.z)-.1f||gravityRay(world,start,top,true))return false;
    }
    return true;
}
std::optional<GravityContact> supportAt(const World &world,Vec3 feet,Vec3 up) {
    auto support=gravityRay(world,feet+up*.7f,feet-up*1.2f,true);
    if(support&&dot(support->normal,up)>.64f)return support;
    return {};
}
void attach(SurfaceGravity &state,Vec3 &feet,const GravityContact &contact) {
    state.frame=alignGravity(state.frame,contact.normal);
    state.mode=GravityMode::Attached;
    state.drift={};
    feet=contact.point+contact.normal*footGap;
}
void floatFree(SurfaceGravity &state,Vec3 drift={}) {
    state.mode=GravityMode::Floating;
    state.drift=drift;
    state.delay=.32f;
}
Vec3 closestPoint(Vec3 p,Vec3 a,Vec3 b,Vec3 c,Vec3 normal) {
    const auto projected=p-normal*dot(p-a,normal);
    if(dot(cross(b-a,projected-a),normal)>=0&&dot(cross(c-b,projected-b),normal)>=0&&
       dot(cross(a-c,projected-c),normal)>=0)return projected;
    auto segment=[&](Vec3 from,Vec3 to){
        const auto edge=to-from;
        return from+edge*std::clamp(dot(p-from,edge)/std::max(.000001f,dot(edge,edge)),0.f,1.f);
    };
    const auto first=segment(a,b),second=segment(b,c),third=segment(c,a);
    const auto best=length(first-p)<=length(second-p)?first:second;
    return length(best-p)<=length(third-p)?best:third;
}
}

Quaternion gravityInverse(Quaternion q) {return {-q.x,-q.y,-q.z,q.w};}
Quaternion alignGravity(Quaternion frame,Vec3 normal) {
    const auto up=rotate(frame,{0,1,0});
    const float cosine=std::clamp(dot(up,normal),-1.f,1.f);
    if(cosine>.99999f)return frame;
    const auto axis=cosine<-.99999f?rotate(frame,{0,0,-1}):cross(up,normal);
    const auto rotation=normalized({axis.x,axis.y,axis.z,1+cosine});
    return normalized(compose(rotation,frame));
}
std::optional<GravityContact> gravityRay(const World &world,Vec3 from,Vec3 to,bool terrain) {
    const auto ray=to-from;
    std::optional<GravityContact> best;
    float fraction=1.00001f;
    if(terrain&&ray.y<-.0001f){
        auto margin=[&](float f){const auto p=from+ray*f;return p.y-world.surfaceHeight(p.x,p.z);};
        float f=2;
        if(margin(0)<-.05f)f=0;
        else{
            const int steps=std::clamp(int(std::ceil(std::hypot(ray.x,ray.z)/2)),1,64);
            for(int i=1;i<=steps;++i){
                const float next=float(i)/steps;
                if(margin(next)>-.0001f)continue;
                float low=float(i-1)/steps,high=next;
                for(int n=0;n<12;++n){
                    const float mid=(low+high)*.5f;
                    if(margin(mid)>=0)low=mid;else high=mid;
                }
                f=high;break;
            }
        }
        if(f>=0&&f<=1){
            const auto p=from+ray*f;
            const auto n=unit({world.surfaceHeight(p.x-.4f,p.z)-world.surfaceHeight(p.x+.4f,p.z),.8f,
                              world.surfaceHeight(p.x,p.z-.4f)-world.surfaceHeight(p.x,p.z+.4f)},{0,1,0});
            best=GravityContact{p,n,f};fraction=f;
        }
    }
    auto testMesh=[&](const WorldMesh &mesh,Vec3 origin,Quaternion orientation){
        const auto inverse=gravityInverse(orientation);
        const auto start=rotate(inverse,from-origin),dir=rotate(inverse,ray);
        for(size_t i=0;i+2<mesh.size();i+=3){
            const auto &a=mesh[i].position;
            const auto ab=mesh[i+1].position-a,ac=mesh[i+2].position-a;
            const auto p=cross(dir,ac);
            const auto determinant=dot(ab,p);
            if(std::abs(determinant)<1e-6f)continue;
            const auto offset=start-a;
            const float u=dot(offset,p)/determinant;
            if(u<-.00001f||u>1.00001f)continue;
            const auto q=cross(offset,ab);
            const float v=dot(dir,q)/determinant;
            if(v<-.00001f||u+v>1.00001f)continue;
            const float f=dot(ac,q)/determinant;
            if(f<.00001f||f>=fraction||f>1)continue;
            auto normal=rotate(orientation,unit(cross(ab,ac)));
            if(dot(normal,ray)>0)normal=normal*(-1);
            best=GravityContact{from+ray*f,normal,f};fraction=f;
        }
    };
    if(world.deckContact(from,to)<=1.f)testMesh(world.geometry().interiorDeckMesh,{},{});
    for(const auto &t:world.targets)
        if(largeStructure(t)&&intersectsBounds(from,from+ray*std::min(1.f,fraction),t))
            testMesh(*t.mesh,t.origin,angles(t.yaw,t.ai.pitch,t.ai.bank));
    return best;
}
bool beginGravityPull(const World &world,SurfaceGravity &state,Vec3 feet,const GravityContact &hit,float height,float radius) {
    if(state.mode==GravityMode::Pulling||!fits(world,hit,height,radius))return false;
    const auto centre=feet+state.up()*(height*.5f);
    const auto landing=hit.point+hit.normal*(height*.5f+footGap);
    if(gravityRay(world,centre,landing,true))return false;
    state.mode=GravityMode::Pulling;
    state.destination=hit.point;
    state.normal=hit.normal;
    state.drift={};
    return true;
}
bool gravityImpact(const World &world,SurfaceGravity &state,Vec3 feet,Vec3 displacement,float height,float radius) {
    if(length(displacement)<.0001f||state.mode==GravityMode::Pulling)return false;
    const auto direction=unit(displacement);
    const auto up=state.up();
    const auto sweep=displacement+direction*(radius+.2f);
    for(float level:{height*.55f,height-.22f,.65f}){
        const auto from=feet+up*level;
        auto hit=gravityRay(world,from,from+sweep);
        if(hit&&dot(hit->normal,direction)<-.22f&&dot(hit->normal,up)<.85f&&
           beginGravityPull(world,state,feet,*hit,height,radius))return true;
    }
    return false;
}
bool moveOnGravity(const World &world,SurfaceGravity &state,Vec3 &feet,Vec3 displacement,float height,float radius) {
    if(state.mode!=GravityMode::Attached)return false;
    const auto up=state.up();
    displacement=displacement-up*dot(up,displacement);
    if(length(displacement)<.0001f)return false;
    const auto centre=feet+up*(height*.55f);
    if(gravityRay(world,centre,centre+displacement+unit(displacement)*radius,true))return false;
    const auto next=feet+displacement;
    if(auto support=supportAt(world,next,up))attach(state,feet,*support);
    else{
        feet=next;
        floatFree(state,unit(displacement)*2.f+up*.8f);
    }
    return true;
}
void advanceGravity(const World &world,SurfaceGravity &state,Vec3 &feet,float height,float radius,float dt) {
    if(state.mode==GravityMode::Ground)return;
    const auto up=state.up();
    const auto centre=feet+up*(height*.5f);
    if(state.mode==GravityMode::Attached){
        if(!supportAt(world,feet,up))floatFree(state,up*2.f);
        return;
    }
    if(state.mode==GravityMode::Pulling){
        const auto support=supportAt(world,state.destination+state.normal*footGap,state.normal);
        if(!support){floatFree(state,up*1.5f);return;}
        const auto arrival=support->point+support->normal*(height*.5f+footGap);
        const auto delta=arrival-centre;
        const float distance=length(delta);
        const auto step=unit(delta)*std::min(distance,48.f*dt);
        if(gravityRay(world,centre,centre+step,true)){floatFree(state);return;}
        if(distance<=48.f*dt){attach(state,feet,*support);return;}
        feet=feet+step;
        return;
    }
    const auto next=centre+state.drift*dt;
    if(!gravityRay(world,centre,next,true))feet=feet+state.drift*dt;
    state.drift=state.drift*std::max(0.f,1-dt*.8f);
    state.delay-=dt;
    if(state.delay>0)return;
    state.delay=.4f;
    float best=captureRange+1;
    std::optional<GravityContact> nearest;
    auto inspect=[&](Vec3 ray){
        auto hit=gravityRay(world,centre,centre+ray*captureRange,true);
        if(!hit)return;
        const float distance=hit->fraction*captureRange;
        if(distance>=best||!fits(world,*hit,height,radius))return;
        const auto landing=hit->point+hit->normal*(height*.5f+footGap);
        if(gravityRay(world,centre,landing,true))return;
        nearest=hit;best=distance;
    };
    inspect({0,-1,0});
    for(const auto &target:world.targets){
        if(!largeStructure(target))continue;
        const auto delta=centre-target.center;
        const Vec3 nearestInBounds{std::max(0.f,std::abs(delta.x)-target.half.x),
                                   std::max(0.f,std::abs(delta.y)-target.half.y),
                                   std::max(0.f,std::abs(delta.z)-target.half.z)};
        if(length(nearestInBounds)>=best)continue;
        const auto orientation=angles(target.yaw,target.ai.pitch,target.ai.bank);
        const auto local=rotate(gravityInverse(orientation),centre-target.origin);
        const auto &mesh=*target.mesh;
        for(size_t i=0;i+2<mesh.size();i+=3){
            const auto a=mesh[i].position,b=mesh[i+1].position,c=mesh[i+2].position;
            const auto crossProduct=cross(b-a,c-a);
            if(length(crossProduct)<1e-6f)continue;
            const auto face=closestPoint(local,a,b,c,unit(crossProduct));
            if(length(face-local)>=std::min(best,captureRange)||length(face-local)<.001f)continue;
            inspect(unit(rotate(orientation,face-local)));
        }
    }
    if(nearest){beginGravityPull(world,state,feet,*nearest,height,radius);return;}
    state.drift.y=std::min(state.drift.y,-4.f);
}
}
