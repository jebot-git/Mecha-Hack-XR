#include "simulation.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace iron {
namespace {
using Json=nlohmann::json;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(std::string("Invalid procedural reinforcements: ")+message);}
bool integer(const Json& j,unsigned lo,unsigned hi){return j.is_number_integer()&&j.get<double>()>=lo&&j.get<double>()<=hi;}
bool number(const Json& j,float lo,float hi){return j.is_number()&&std::isfinite(j.get<double>())&&j.get<double>()>=lo&&j.get<double>()<=hi;}
bool air(const Target& t){return t.spawnType==1||t.spawnType==18;}
bool alive(const Target& t){return t.active&&t.health>0&&!t.escaped;}
ProceduralSpawnProfile profile(const Target& t){return air(t)?ProceduralSpawnProfile::AirPatrol:t.spawnType==2?ProceduralSpawnProfile::Emplacement:t.spawnType==14?ProceduralSpawnProfile::Walker:ProceduralSpawnProfile::Armour;}
float pilotRange(const World& world,Vec3 point){
    float result=std::hypot(world.position.x-point.x,world.position.z-point.z);
    if(world.cooperative)for(const auto* p:world.teammates)if(!p->lifeSpent&&p->health>0)result=std::min(result,std::hypot(p->position.x-point.x,p->position.z-point.z));
    return result;
}
bool intersects(Vec3 from,Vec3 to,Vec3 center,Vec3 half){
    auto d=to-from;float low=0,high=1;
    for(int axis=0;axis<3;++axis){float p=axis==0?from.x:axis==1?from.y:from.z,v=axis==0?d.x:axis==1?d.y:d.z,c=axis==0?center.x:axis==1?center.y:center.z,h=axis==0?half.x:axis==1?half.y:half.z;
        if(std::abs(v)<.00001f){if(std::abs(p-c)>h)return false;}
        else {float a=(c-h-p)/v,b=(c+h-p)/v;if(a>b)std::swap(a,b);low=std::max(low,a);high=std::min(high,b);if(low>high)return false;}}
    return true;
}
bool safeSpawn(const World& world,const ProceduralSpawnSite& site){
    const auto& t=site.prototype;bool flying=air(t);
    float distance=pilotRange(world,t.center);
    if(distance<(flying?90.f:60.f)||distance>world.proceduralPopulation.activationRange)return false;
    const auto& b=world.movementBounds;
    if(t.center.x-t.half.x<b[0]+2||t.center.z-t.half.z<b[1]+2||t.center.x+t.half.x>b[2]-2||t.center.z+t.half.z>b[3]-2)return false;
    for(const auto& other:world.targets)if(alive(other)){
        if(intersects(t.center,t.center,other.center,t.half+other.half+Vec3{1,1,1}))return false;
        if(other.spawnType>=0&&air(other)==flying&&std::hypot(t.center.x-other.center.x,t.center.z-other.center.z)<(flying?40.f:20.f))return false;
    }
    if(flying)return true;
    float radius=std::hypot(t.half.x,t.half.z);
    if(!world.groundPassable(t.origin,radius))return false;
    for(float x:{-t.half.x,t.half.x})for(float z:{-t.half.z,t.half.z})if(std::abs(world.surfaceHeight(t.center.x+x,t.center.z+z)-(t.center.y-t.half.y))>.5f)return false;
    // Keep a player-sized bypass beside a replenished body. Dynamic enemies
    // may occupy a previously safe lane, so repeat this at emission time.
    auto forward=site.forward,side=Vec3{-forward.z,0,forward.x};
    float lateral=std::abs(side.x)*t.half.x+std::abs(side.z)*t.half.z,along=std::abs(forward.x)*t.half.x+std::abs(forward.z)*t.half.z;
    if(lateral+1>site.roadHalfWidth)return false;
    auto centre=t.center;centre.y=world.surfaceHeight(centre.x,centre.z)+6;
    for(float sign:{-1.f,1.f}){
        auto a=centre-forward*(along+5),d=centre+forward*(along+5),offset=side*(sign*(lateral+4.5f));std::array<Vec3,4> path{a,a+offset,d+offset,d};bool clear=true;
        for(size_t n=1;n<path.size()&&clear;++n){auto from=path[n-1],to=path[n];int steps=std::max(1,int(std::ceil(std::hypot(to.x-from.x,to.z-from.z)/2)));
            for(int k=0;k<=steps;++k){auto p=from+(to-from)*(float(k)/steps);if(!world.groundPassable(p,3.5f)){clear=false;break;}}
            from.y=world.surfaceHeight(from.x,from.z)+6;to.y=world.surfaceHeight(to.x,to.z)+6;
            for(const auto& other:world.targets)if(alive(other)&&intersects(from,to,other.center,other.half+Vec3{3.5f,6,3.5f})){clear=false;break;}
        }
        if(clear)return true;
    }
    return false;
}
}

void validateProceduralReinforcements(const Json& recipe){
    if(!recipe.contains("reinforcements"))return;
    const auto& r=recipe.at("reinforcements");require(r.is_object(),"expected a policy object");
    if(r.value("mode",Json())!="continuous"){
        require(!r.contains("mode")||r.at("mode")=="finite","unknown mode");
        require(r.contains("per_zone")&&integer(r.at("per_zone"),0,12)&&r.contains("interval")&&number(r.at("interval"),5,120),"invalid finite reserve policy");return;
    }
    require(recipe.at("version")==4&&!r.contains("per_zone"),"continuous mode requires version four without finite tickets");
    require(r.contains("interval")&&number(r.at("interval"),5,120)&&r.contains("activation_range")&&number(r.at("activation_range"),100,2000),"invalid emission interval/activation range");
    require(r.contains("ground_limit")&&integer(r.at("ground_limit"),1,64)&&r.contains("air_limit")&&integer(r.at("air_limit"),0,8)&&r.contains("kill_quota")&&integer(r.at("kill_quota"),1,512),"invalid population cap/kill quota");
    require(recipe.contains("encounters")&&recipe.at("encounters").is_object(),"missing encounter settings");const auto& e=recipe.at("encounters");
    require(e.contains("ground_per_zone")&&integer(e.at("ground_per_zone"),1,12)&&e.at("ground_per_zone").get<size_t>()*recipe.at("objectives").size()<=r.at("ground_limit").get<size_t>(),"initial ground population exceeds cap");
    require(e.contains("air_units")&&integer(e.at("air_units"),0,8)&&e.at("air_units").get<unsigned>()<=r.at("air_limit").get<unsigned>(),"initial aircraft exceed cap");
}

void prepareContinuousProceduralReinforcements(World& world,const Json& recipe,const std::vector<ProceduralSpawnSite>& sites){
    validateProceduralReinforcements(recipe);const auto& r=recipe.at("reinforcements");
    require(!world.procedural.objectives.empty()&&world.procedural.objectives.size()==recipe.at("objectives").size()&&world.procedural.objectives.size()<=8,"objective state differs from recipe");
    ProceduralPopulation pool;pool.enabled=true;pool.groundLimit=r.at("ground_limit");pool.airLimit=r.at("air_limit");pool.interval=r.at("interval");pool.activationRange=r.at("activation_range");
    pool.groundPerZone=recipe.at("encounters").at("ground_per_zone");pool.airPerZone=std::max(1u,unsigned((pool.airLimit+world.procedural.objectives.size()-1)/world.procedural.objectives.size()));
    for(size_t zone=0;zone<world.procedural.objectives.size();++zone){
        auto& goal=world.procedural.objectives[zone];if(goal.kind==ProceduralGoalKind::Eliminate)goal.killQuota=r.at("kill_quota");
        for(auto kind:{ProceduralSpawnProfile::Armour,ProceduralSpawnProfile::Emplacement,ProceduralSpawnProfile::Walker,ProceduralSpawnProfile::AirPatrol}){
            if(kind==ProceduralSpawnProfile::AirPatrol&&!pool.airLimit)continue;
            ProceduralSpawner s;s.zone=int(zone);s.profile=kind;
            for(const auto& site:sites)if(site.prototype.proceduralZone==int(zone)&&profile(site.prototype)==kind&&s.sites.size()<6){
                if(std::any_of(s.sites.begin(),s.sites.end(),[&](const auto& old){return old.prototype.spawnType==site.prototype.spawnType&&length(old.prototype.origin-site.prototype.origin)<.01f;}))continue;
                s.sites.push_back(site);
            }
            if(s.sites.empty())continue;
            s.cursor=(world.prototypeSeed+zone*17+unsigned(kind)*7)%s.sites.size();s.clock=pool.interval*(.5f+.125f*unsigned(kind));pool.spawners.push_back(std::move(s));
        }
    }
    for(size_t n=0;n<world.targets.size();++n){auto& t=world.targets[n];if(t.spawnType<0)continue;
        t.generated=true;t.ai.generator=-1;t.ai.respawnRange=0;(air(t)?pool.airSlots:pool.groundSlots).push_back({n,0,alive(t)});
    }
    require(pool.groundSlots.size()<=pool.groundLimit&&pool.airSlots.size()<=pool.airLimit,"placed population exceeds pool");
    auto fill=[&](std::vector<ProceduralSpawnSlot>& slots,unsigned limit,bool flying){
        auto found=std::find_if(sites.begin(),sites.end(),[&](const auto& s){return air(s.prototype)==flying;});if(found==sites.end())return;
        while(slots.size()<limit){auto t=found->prototype;t.generated=true;t.active=false;t.health=0;t.processed=true;t.escaped=false;t.prototypeObjective=false;t.proceduralZone=-1;t.ai.generator=-1;
            slots.push_back({world.targets.size(),0,false});world.targets.push_back(std::move(t));}
    };
    fill(pool.groundSlots,pool.groundLimit,false);fill(pool.airSlots,pool.airLimit,true);
    world.proceduralPopulation=std::move(pool);
}

void stepContinuousProceduralReinforcements(World& world,float dt){
    auto& pool=world.proceduralPopulation;
    if(!pool.enabled||!world.proceduralLayout||world.missionStatus!=MissionStatus::Active||world.lifeSpent||world.health<=0||!std::isfinite(dt)||dt<=0||pool.spawners.empty())return;
    auto cooldowns=[&](auto& slots){for(auto& slot:slots){auto& t=world.targets.at(slot.target);
        if(alive(t)&&pilotRange(world,t.center)>pool.activationRange+200)world.retireEnemy(t,true);
        bool living=alive(t);if(slot.wasAlive&&!living)slot.cooldown=3;slot.wasAlive=living;if(!living)slot.cooldown=std::max(0.f,slot.cooldown-dt);}};
    cooldowns(pool.groundSlots);cooldowns(pool.airSlots);
    unsigned ground=0,aircraft=0;std::array<unsigned,8> groundZones{},airZones{};
    for(const auto& t:world.targets)if(t.spawnType>=0&&alive(t)){bool flying=air(t);++(flying?aircraft:ground);if(t.proceduralZone>=0&&t.proceduralZone<8)++(flying?airZones:groundZones)[size_t(t.proceduralZone)];}
    int current=world.procedural.segmentCount;
    for(const auto& goal:world.procedural.objectives)if(!goal.complete)current=std::min(current,goal.segment);
    auto open=[&](const ProceduralSpawner& s){const auto& goal=world.procedural.objectives.at(size_t(s.zone));return !goal.complete&&(!goal.killQuota||goal.kills<goal.killQuota)&&(!world.procedural.orderedSegments||goal.segment==current)&&pilotRange(world,goal.position)<=pool.activationRange;};
    for(auto& s:pool.spawners)if(open(s))s.clock=std::max(-pool.interval,s.clock-dt);
    size_t begin=pool.nextSpawner;unsigned emitted=0;
    for(size_t n=0;n<pool.spawners.size()&&emitted<2;++n){size_t id=(begin+n)%pool.spawners.size();auto& s=pool.spawners[id];
        if(!open(s)||s.clock>0)continue;
        bool flying=s.profile==ProceduralSpawnProfile::AirPatrol;
        if((flying?aircraft:ground)>=(flying?pool.airLimit:pool.groundLimit)||(flying?airZones:groundZones)[size_t(s.zone)]>=(flying?pool.airPerZone:pool.groundPerZone)){s.clock=0;continue;}
        auto& slots=flying?pool.airSlots:pool.groundSlots;
        auto free=std::find_if(slots.begin(),slots.end(),[&](const auto& slot){const auto& t=world.targets.at(slot.target);return slot.cooldown<=0&&(!t.active||t.escaped||(t.health<=0&&t.processed));});
        if(free==slots.end()){s.clock=0;continue;}
        bool spawned=false;
        for(size_t attempt=0;attempt<s.sites.size();++attempt){auto index=(s.cursor+attempt)%s.sites.size();const auto& site=s.sites[index];if(!safeSpawn(world,site))continue;
            auto t=site.prototype;t.generated=true;t.active=true;t.processed=false;t.escaped=false;t.ai.generator=-1;t.ai.respawnRange=0;t.ai.registered=false;t.ai.state=AIState::Dormant;t.ai.playerTarget=-1;t.ai.retarget=0;t.ai.velocity={};t.ai.spawn=t.origin;t.ai.spawnYaw=t.ai.weaponYaw=t.yaw;t.ai.maxHealth=t.ai.lastHealth=t.health;
            world.targets.at(free->target)=std::move(t);free->wasAlive=true;free->cooldown=0;
            ++(flying?aircraft:ground);++(flying?airZones:groundZones)[size_t(s.zone)];++s.spawned;++pool.spawned;++emitted;
            s.cursor=(index+1)%s.sites.size();pool.nextSpawner=(id+1)%pool.spawners.size();spawned=true;break;
        }
        // Blocked sites retry twice per second without timer debt. Long frames emit a bounded wave,
        // and never accumulate an unbounded catch-up burst after an obstruction.
        s.clock=spawned?(s.clock<=-pool.interval?pool.interval:std::max(.001f,s.clock+pool.interval)):.5f;
    }
}
}
