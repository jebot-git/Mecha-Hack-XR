#include "coop_prediction.hpp"
#include "destruction_visuals.hpp"
#include <stdexcept>
#include <algorithm>
namespace iron::coop {
using Json=nlohmann::json;
namespace {
Vec3 safeVelocity(Vec3 v){return std::isfinite(length(v))&&length(v)<=100?v:Vec3{};}
float gait(float phase,Vec3 velocity,float yaw,float age){float sign=dot(velocity,direction(yaw,0))>=0?1.f:-1.f;return std::fmod(phase+sign*std::hypot(velocity.x,velocity.z)*age*(100.f/96)+36,18.f);}
void pose(Pilot& out,const Pilot& anchor,Vec3 velocity,const std::array<float,4>& rates,float age){
    if(anchor.lifeSpent||anchor.health<=0||anchor.missionStatus!=MissionStatus::Active)age=0;
    if(age==0){out.position=anchor.position;out.chassisYaw=anchor.chassisYaw;out.torsoYaw=anchor.torsoYaw;out.aimYaw=anchor.aimYaw;out.aimPitch=anchor.aimPitch;out.gaitPhase=anchor.gaitPhase;out.arms=anchor.arms;return;}
    out.position=anchor.position+velocity*age;
    out.chassisYaw=wrap(anchor.chassisYaw+rates[0]*age);out.torsoYaw=std::clamp(anchor.torsoYaw+rates[1]*age,-pi/2,pi/2);
    out.aimYaw=wrap(out.facing()+attackYaw(anchor.aimYaw+rates[2]*age-out.facing()));out.aimPitch=std::clamp(anchor.aimPitch+rates[3]*age,-1.45f,1.45f);
    out.gaitPhase=gait(anchor.gaitPhase,velocity,anchor.chassisYaw,age);
    auto turn=angles(wrap(out.facing()-anchor.facing()));Vec3 oldEye=anchor.position+Vec3{0,12,0},newEye=out.position+Vec3{0,12,0};
    for(size_t h=0;h<2;++h){out.arms[h]=anchor.arms[h];auto& a=out.arms[h];
        for(auto* point:{&a.shoulder,&a.elbow,&a.wrist,&a.muzzle})*point=newEye+rotate(turn,*point-oldEye);
        a.forward=rotate(turn,a.forward);a.grip=compose(turn,a.grip);a.aim=compose(turn,a.aim);
    }
}
bool intersects(Vec3 from,Vec3 to,Vec3 center,Vec3 half){
    float low=0,high=1;std::array<float,3> a{from.x,from.y,from.z},b{to.x,to.y,to.z},c{center.x,center.y,center.z},h{half.x,half.y,half.z};
    for(size_t k=0;k<3;++k){float d=b[k]-a[k];if(std::abs(d)<1e-6f){if(std::abs(a[k]-c[k])>h[k])return false;}else{float x=(c[k]-h[k]-a[k])/d,y=(c[k]+h[k]-a[k])/d;if(x>y)std::swap(x,y);low=std::max(low,x);high=std::min(high,y);if(low>high)return false;}}return true;
}
}
void MotionPrediction::reset(){actors.clear();pilots.clear();projectiles.clear();effects.clear();previousTick=0;}
void MotionPrediction::capture(const World& w,const Json& players,uint64_t tick){
    float dt=previousTick&&tick>previousTick?float(tick-previousTick)/60:0;
    std::vector<Actor> next;next.reserve(w.targets.size());
    for(size_t i=0;i<w.targets.size();++i){const auto& t=w.targets[i];Actor a{t.origin,t.center,safeVelocity(t.ai.velocity),t.yaw,0,t.ai.gaitPhase,t.active&&!t.escaped&&t.health>0&&t.spawnType>=0};
        if(a.moving&&i<actors.size()&&actors[i].moving&&dt>0&&dt<=.25f&&length(a.origin-actors[i].origin)<=100*dt+1){float rate=wrap(a.yaw-actors[i].yaw)/dt;if(std::abs(rate)<=10)a.rate=rate;}
        next.push_back(a);
    }
    actors=std::move(next);pilots.clear();
    for(size_t i=0;i<w.mapPilots.size();++i){Person p;p.pilot=w.mapPilots[i];
        if(i<players.size()&&players[i].contains("motion")){auto& motion=players[i].at("motion");const auto& v=motion.at(0);if(!v.is_array()||v.size()!=3)throw std::runtime_error("Invalid motion vector");p.velocity=safeVelocity({v[0].get<float>(),v[1].get<float>(),v[2].get<float>()});p.rates=motion.at(1).get<std::array<float,4>>();for(float& rate:p.rates)if(!std::isfinite(rate)||std::abs(rate)>10)rate=0;}
        pilots.push_back(std::move(p));
    }
    projectiles=w.projectiles;effects=w.effects;previousTick=tick;
}
void MotionPrediction::present(World& w,float age)const{
    age=std::isfinite(age)?std::max(0.f,age):0.f;float t=std::min(age,horizon);
    for(size_t i=0;i<std::min(actors.size(),w.targets.size());++i){const auto& a=actors[i];auto& out=w.targets[i];float time=a.moving?t:0;
        out.origin=time>0?a.origin+a.velocity*time:a.origin;out.center=time>0?a.center+a.velocity*time:a.center;out.yaw=time>0?wrap(a.yaw+a.rate*time):a.yaw;out.ai.gaitPhase=time>0?gait(a.gait,a.velocity,a.yaw,time):a.gait;
    }
    for(size_t i=0;i<std::min(pilots.size(),w.mapPilots.size());++i){const auto& p=pilots[i];pose(w.mapPilots[i],p.pilot,p.velocity,p.rates,t);if(p.pilot.playerSlot==w.playerSlot)pose(w,p.pilot,p.velocity,p.rates,t);}
    if(age==0){w.projectiles=projectiles;w.effects=effects;return;}
    w.projectiles.clear();w.projectiles.reserve(projectiles.size());
    for(auto p:projectiles){
        if(age>=p.life||p.intercepted)continue;float time=std::min(age,projectileHorizon);Vec3 from=p.position;
        p.position=p.position+p.velocity*time;if(p.kind==ProjectileKind::Bomb){p.position.y-=9*time*time;p.velocity.y-=18*time;}
        bool hidden=w.groundContact(from,p.position)<=1;
        if(time>0)for(size_t i=0;!hidden&&i<w.targets.size();++i){const auto& target=w.targets[i];if(int(i)==p.owner||!target.active||target.escaped||target.health<=0||(p.owner>=0&&target.ai.parent==p.owner))continue;hidden=intersects(from,p.position,target.center,target.half);}
        if(!hidden)w.projectiles.push_back(p);
    }
    w.effects=effects;
    for(auto& fx:w.effects){fx.age+=t;if(fx.kind==WeaponEffect::Debris){fx.position=fx.position+fx.direction*t;fx.position.y-=9*t*t;fx.position.y=std::max(fx.position.y,w.debrisFloor(fx));}}
    std::erase_if(w.effects,[](const auto& fx){if(fx.model==rpgLandingWave)return fx.age>=destructionEffectLife(fx.model,int(fx.kind));const float trackedLife=rpg::enemyEffectLife(fx.model);if(trackedLife>0)return fx.age>=trackedLife;return fx.age>(fx.kind==WeaponEffect::Debris?3.f:(fx.kind==WeaponEffect::Blast||fx.kind==WeaponEffect::Smoke)?1.f:fx.kind==WeaponEffect::Muzzle?.09f:.3f);});
}
}
