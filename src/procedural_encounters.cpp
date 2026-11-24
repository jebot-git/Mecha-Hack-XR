#include "simulation.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <queue>
#include <stdexcept>

namespace iron {
namespace {
using Json=nlohmann::json;
float flat(Vec3 p){return std::hypot(p.x,p.z);}
float bearing(Vec3 p){return std::atan2(-p.x,-p.z);}
struct Random {
    uint32_t state;
    uint32_t next(){state+=0x9e3779b9u;uint32_t x=state;x=(x^(x>>16))*0x85ebca6bu;x=(x^(x>>13))*0xc2b2ae35u;return x^(x>>16);}
};
struct Road {int a,b;Vec3 start,delta;float length,half;};
struct Anchor {size_t road;float along;Vec3 point;};
bool intersects(Vec3 from,Vec3 to,Vec3 center,Vec3 half){
    float low=0,high=1;auto d=to-from;
    for(int axis=0;axis<3;++axis){
        float p=axis==0?from.x:axis==1?from.y:from.z,v=axis==0?d.x:axis==1?d.y:d.z;
        float c=axis==0?center.x:axis==1?center.y:center.z,h=axis==0?half.x:axis==1?half.y:half.z;
        if(std::abs(v)<.00001f){if(std::abs(p-c)>h)return false;}
        else {float a=(c-h-p)/v,b=(c+h-p)/v;if(a>b)std::swap(a,b);low=std::max(low,a);high=std::min(high,b);if(low>high)return false;}
    }
    return true;
}
}

void populateProceduralEncounters(World& world,const Json& recipe,const std::vector<Target>& templates){
    if(templates.empty())throw std::runtime_error("Missing procedural encounter templates");
    const auto& nodes=recipe.at("nodes");const auto& zones=recipe.at("objectives");
    bool segmented=recipe.at("version")==4;
    const auto policy=recipe.value("reinforcements",Json::object());bool continuous=policy.value("mode",Json())=="continuous";
    if(nodes.size()>size_t(segmented?768:81)||zones.empty()||zones.size()>8||recipe.at("roads").size()>size_t(segmented?1536:144))throw std::runtime_error("Procedural encounter budget exceeded");
    auto point=[&](int id){auto p=nodes.at(id).get<std::array<float,2>>();return Vec3{p[0],world.surfaceHeight(p[0],p[1]),p[1]};};
    std::array<const Target*,3> families{};
    for(const auto& t:templates){int family=t.spawnType==0?0:t.spawnType==2?1:t.spawnType==14?2:-1;
        if(family>=0&&!families[family]&&t.mesh&&!t.mesh->empty()&&t.health>0)families[family]=&t;}
    if(std::any_of(families.begin(),families.end(),[](auto p){return !p;}))throw std::runtime_error("Missing tank, gun or Satyr encounter template");
    std::vector<Road> roads;std::vector<Anchor> anchors;
    std::vector<std::vector<std::pair<int,float>>> adjacency(nodes.size());
    for(const auto& value:recipe.at("roads")){
        auto ends=value.at("nodes").get<std::array<int,2>>();auto a=point(ends[0]),b=point(ends[1]);auto delta=b-a;float distance=flat(delta),half=value.at("width").get<float>()/2;
        if(!std::isfinite(distance)||distance<=0||!std::isfinite(half)||half<8||half>30||(delta.x!=0&&delta.z!=0))throw std::runtime_error("Invalid encounter road");
        size_t index=roads.size();roads.push_back({ends[0],ends[1],a,delta,distance,half});
        adjacency.at(ends[0]).push_back({ends[1],distance});adjacency.at(ends[1]).push_back({ends[0],distance});
        int count=std::clamp(int(distance/10),1,24);
        for(int i=1;i<=count;++i){float along=distance*i/(count+1);anchors.push_back({index,along,a+delta*(along/distance)});}
    }
    Random random{world.prototypeSeed^0x454e4354u};
    for(size_t i=anchors.size();i>1;--i)std::swap(anchors[i-1],anchors[random.next()%i]);
    std::vector<Target> placed;std::vector<size_t> placedRoads;
    std::vector<std::vector<size_t>> zoneOrders;std::vector<ProceduralSpawnSite> sites;
    auto groundSweep=[&](Vec3 from,Vec3 to,float radius){
        int samples=std::max(1,int(std::ceil(flat(to-from)/2)));
        for(int i=0;i<=samples;++i){auto p=from+(to-from)*(float(i)/samples);p.y=world.surfaceHeight(p.x,p.z);if(!world.groundPassable(p,radius))return false;}
        return true;
    };
    auto reserved=[&](Vec3 center,float radius){
        if(flat(center-world.position)<std::max(60.f,recipe.at("spawn").at("radius").get<float>()+radius+8))return true;
        for(const auto& zone:zones)if(flat(center-point(zone.at("node")))<zone.at("radius").get<float>()+radius+2)return true;
        if(recipe.contains("extraction")&&flat(center-point(recipe.at("extraction").at("node")))<recipe.at("extraction").at("radius").get<float>()+radius+4)return true;
        for(const auto& supply:world.pickups)if(flat(center-supply.position)<radius+5)return true;
        return false;
    };
    auto sweepClear=[&](Vec3 from,Vec3 to,Vec3 half,const Target* extra,bool includeActors){
        for(auto p:{from,to})if(p.x-half.x<world.movementBounds[0]+2||p.z-half.z<world.movementBounds[1]+2||p.x+half.x>world.movementBounds[2]-2||p.z+half.z>world.movementBounds[3]-2)return false;
        auto obstacle=[&](const Target& t){return t.active&&!t.escaped&&t.health>0&&(includeActors||t.spawnType<0)&&intersects(from,to,t.center,t.half+half);};
        for(const auto& t:world.targets)if(obstacle(t))return false;
        if(includeActors)for(const auto& t:placed)if(obstacle(t))return false;
        if(extra&&obstacle(*extra))return false;
        return true;
    };
    auto bypass=[&](const Target& t,const Road& road){
        Vec3 forward=road.delta*(1/road.length),side{-forward.z,0,forward.x};
        float lateral=std::abs(side.x)*t.half.x+std::abs(side.z)*t.half.z;
        float along=std::abs(forward.x)*t.half.x+std::abs(forward.z)*t.half.z;
        auto center=t.center;center.y=world.surfaceHeight(center.x,center.z)+6;
        for(float sign:{-1.f,1.f}){
            auto a=center-forward*(along+5),d=center+forward*(along+5),offset=side*(sign*(lateral+4.5f));
            auto b=a+offset,c=d+offset;
            for(auto p:{&a,&b,&c,&d})p->y=world.surfaceHeight(p->x,p->z)+6;
            if(groundSweep(a,b,3.5f)&&groundSweep(b,c,3.5f)&&groundSweep(c,d,3.5f)&&sweepClear(a,b,{3.5f,6,3.5f},&t,true)&&sweepClear(b,c,{3.5f,6,3.5f},&t,true)&&sweepClear(c,d,{3.5f,6,3.5f},&t,true))return true;
        }
        return false;
    };
    auto make=[&](const Target& source,const Anchor& anchor,bool patrol,int zone){
        Target t=source;float health=t.health;
        t.ai=EnemyAI{};t.ai.words[0]=t.spawnType==2?14000:11000;t.ai.words[1]=128;t.ai.words[2]=3;t.ai.words[3]=512;
        t.parameters=t.spawnType==0?4:0;t.trigger=t.spawnFlags=t.deathFlag=0;t.action=0;t.tag=255;
        t.active=true;t.generated=false;t.processed=false;t.escaped=false;t.protectedTarget=false;t.invulnerable=false;
        t.activationRange=10000;t.attackClock=.6f;t.route.clear();t.waypoint=0;t.proceduralZone=zone;
        t.prototypeObjective=zones.at(zone).value("kind",std::string("encounter"))=="encounter";
        t.origin=anchor.point;t.yaw=bearing(world.position-anchor.point);
        world.updateEnemyBounds(t);
        t.origin.x+=anchor.point.x-t.center.x;t.origin.z+=anchor.point.z-t.center.z;
        t.origin.y+=world.surfaceHeight(anchor.point.x,anchor.point.z)-(t.center.y-t.half.y);world.updateEnemyBounds(t);
        t.health=health;t.ai.maxHealth=t.ai.lastHealth=health;t.ai.spawn=t.origin;t.ai.spawnYaw=t.ai.weaponYaw=t.yaw;
        if(t.spawnType==2)t.speed=0;else if(t.speed<=0)t.speed=t.spawnType==0?6:10;
        if(patrol&&t.spawnType!=2){
            const auto& road=roads[anchor.road];float radius=std::hypot(t.ai.localHalf.x,t.ai.localHalf.z);
            Vec3 forward=road.delta*(1/road.length);
            for(float delta:{24.f,-24.f,16.f,-16.f}){
                if(anchor.along+delta<8||anchor.along+delta>road.length-8)continue;
                auto end=t.origin+forward*delta;end.y=t.origin.y+world.surfaceHeight(end.x,end.z)-world.surfaceHeight(t.origin.x,t.origin.z);auto endCenter=t.center+(end-t.origin);
                bool allowed=true;
                for(int sample=1;sample<=6;++sample)if(reserved(t.center+forward*(delta*sample/6),radius))allowed=false;
                if(!allowed)continue;
                // The patrol is a short out-and-back leg on one connected road,
                // never a straight-line shortcut through another building plot.
                if(groundSweep(t.origin,end,radius)&&sweepClear(t.center,endCenter,{radius,t.half.y,radius},nullptr,true)){
                    t.route={end,t.origin};if(t.spawnType==0)t.parameters=2;break;
                }
            }
        }
        return t;
    };
    auto tryPlace=[&](int family,size_t zone,const std::vector<size_t>& order,bool patrol){
        for(auto id:order){const auto& anchor=anchors[id];
            auto t=make(*families[family],anchor,patrol,int(zone));const auto& road=roads[anchor.road];
            float lateral=road.delta.x==0?t.half.x:t.half.z;
            if(lateral+1>road.half||reserved(t.center,std::hypot(t.half.x,t.half.z))||!world.groundPassable(t.origin,std::hypot(t.half.x,t.half.z)))continue;
            bool supported=true;for(float x:{-t.half.x,t.half.x})for(float z:{-t.half.z,t.half.z})if(std::abs(world.surfaceHeight(t.center.x+x,t.center.z+z)-(t.center.y-t.half.y))>.5f)supported=false;
            if(!supported||!sweepClear(t.center,t.center,t.half+Vec3{1,0,1},nullptr,true))continue;
            if(std::any_of(placed.begin(),placed.end(),[&](const auto& other){return flat(t.center-other.center)<20;}))continue;
            bool blocksPatrol=false;
            for(const auto& other:placed)if(!other.route.empty()){
                float radius=std::hypot(other.ai.localHalf.x,other.ai.localHalf.z)+1;
                auto end=other.center+(other.route.front()-other.origin);
                if(intersects(other.center,end,t.center,t.half+Vec3{radius,other.half.y,radius})){blocksPatrol=true;break;}
            }
            if(blocksPatrol)continue;
            // A later guard must not close the bypass that made an earlier
            // placement acceptable. All checks use the final growing roster.
            placed.push_back(std::move(t));placedRoads.push_back(anchor.road);bool walkable=true;
            for(size_t i=0;i<placed.size();++i)if(!bypass(placed[i],roads[placedRoads[i]])){walkable=false;break;}
            if(walkable)return true;
            placed.pop_back();placedRoads.pop_back();
        }
        return false;
    };
    for(size_t zone=0;zone<zones.size();++zone){
        int start=zones[zone].at("node");std::vector<float> distance(nodes.size(),std::numeric_limits<float>::infinity());
        using Visit=std::pair<float,int>;std::priority_queue<Visit,std::vector<Visit>,std::greater<Visit>> frontier;distance.at(start)=0;frontier.push({0,start});
        while(!frontier.empty()){auto [cost,id]=frontier.top();frontier.pop();if(cost>distance[id])continue;for(auto [next,weight]:adjacency[id])if(cost+weight<distance[next]){distance[next]=cost+weight;frontier.push({distance[next],next});}}
        std::vector<size_t> order;for(size_t i=0;i<anchors.size();++i){
            if(segmented&&recipe.at("roads")[anchors[i].road].value("segment",-1)!=zones[zone].at("segment").get<int>())continue;
            order.push_back(i);
        }
        auto score=[&](size_t id){auto& a=anchors[id];auto& r=roads[a.road];return std::min(distance[r.a]+a.along,distance[r.b]+r.length-a.along);};
        std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return score(a)<score(b);});
        if(continuous)zoneOrders.push_back(order);
        bool eliminate=zones[zone].value("kind",std::string("encounter"))=="encounter";
        bool expanded=recipe.value("version",1)>=3;
        int requested=expanded?recipe.value("encounters",Json::object()).value("ground_per_zone",6):(eliminate?3:2);
        if(requested<1||requested>12)throw std::runtime_error("Ground encounter count outside 1..12");
        int budget=64-(recipe.value("reinforcements",Json::object()).value("per_zone",0)>0?int(zones.size()):0);
        int allowance=budget/int(zones.size())+(zone<size_t(budget)%zones.size()?1:0);
        unsigned before=unsigned(placed.size());int count=std::min(requested,allowance);
        for(int n=0;n<count;++n){
            int family=eliminate?n%3:(n%2?2:int((world.prototypeSeed+zone+n/2)%2));bool patrol=family!=1&&n<3;
            bool accepted=tryPlace(family,zone,order,patrol);
            // Narrow/small recipes may not fit the Satyr's full body. A legal
            // smaller family is preferable to overlapping scenery or the start.
            for(int fallback=0;!accepted&&fallback<3;++fallback)accepted=tryPlace((family+fallback+1)%3,zone,order,patrol);
        }
        if(placed.size()==before)throw std::runtime_error("No safe connected encounter placement for objective "+std::to_string(zone+1));
    }
    if(continuous){
        auto record=[&](size_t index){const auto& road=roads[placedRoads[index]];sites.push_back({placed[index],road.delta*(1/road.length),road.half});};
        for(size_t i=0;i<placed.size();++i)record(i);
        // Alternate sites lie farther down the same sector's connected roads.
        // Validate real bodies, patrol clearance and player bypasses just as for
        // the initial roster; sites are prototypes, not additional live units.
        for(size_t zone=0;zone<zones.size();++zone)for(int family=0;family<3;++family){
            std::vector<size_t> distant;auto goal=point(zones[zone].at("node"));
            for(auto id:zoneOrders[zone]){float d=flat(anchors[id].point-goal);if(d>=100&&d<=450)distant.push_back(id);}
            if(tryPlace(family,zone,distant,family!=1)){record(placed.size()-1);placed.pop_back();placedRoads.pop_back();}
        }
    }
    if(placed.size()>64)throw std::runtime_error("Procedural ground actor budget exceeded");
    int airCount=recipe.value("version",1)>=3?recipe.value("encounters",Json::object()).value("air_units",3):0;
    if(airCount<0||airCount>8)throw std::runtime_error("Air encounter count outside 0..8");
    if(airCount){
        std::array<const Target*,2> airTemplates{};
        for(const auto& source:templates){int kind=source.spawnType==1?0:source.spawnType==18?1:-1;if(kind>=0&&!airTemplates[kind]&&source.mesh&&!source.mesh->empty())airTemplates[kind]=&source;}
        if(!airTemplates[0]||!airTemplates[1])throw std::runtime_error("Missing procedural helicopter or fighter template");
        float highest=0;
        for(const auto& vertex:world.geometry().terrain)highest=std::max(highest,vertex.position.y);
        for(const auto& t:world.targets)if(t.active&&!t.escaped&&t.health>0)highest=std::max(highest,t.center.y+t.half.y);
        auto bounds=recipe.at("bounds").get<std::array<float,4>>();
        std::vector<Target> aircraft;
        for(int number=0;number<airCount;++number){
            if(bounds[2]-bounds[0]<100||bounds[3]-bounds[1]<100)break;
            bool accepted=false;
            for(size_t attempt=0;attempt<anchors.size();++attempt){
                auto& anchor=anchors[(attempt+number*17)%anchors.size()];Target t=*airTemplates[number%2];float health=t.health;
                t.ai=EnemyAI{};t.ai.words[0]=15000;t.ai.words[1]=7000;t.ai.words[2]=128;t.ai.words[3]=3;
                t.parameters=t.trigger=t.spawnFlags=t.deathFlag=0;t.action=0;t.tag=255;t.proceduralZone=-1;t.prototypeObjective=false;
                t.active=true;t.generated=false;t.processed=false;t.escaped=false;t.protectedTarget=false;t.invulnerable=false;
                t.route.clear();t.waypoint=0;t.attackClock=1;t.speed=t.spawnType==1?16:40;
                float spanX=std::clamp((bounds[2]-bounds[0])*.2f,30.f,100.f),spanZ=std::clamp((bounds[3]-bounds[1])*.2f,30.f,100.f);
                float centerX=std::clamp(anchor.point.x,bounds[0]+spanX+20,bounds[2]-spanX-20),centerZ=std::clamp(anchor.point.z,bounds[1]+spanZ+20,bounds[3]-spanZ-20);
                std::array<Vec3,4> loop{{{centerX-spanX,0,centerZ-spanZ},{centerX+spanX,0,centerZ-spanZ},{centerX+spanX,0,centerZ+spanZ},{centerX-spanX,0,centerZ+spanZ}}};
                auto p=loop[(number+attempt)%loop.size()];if(flat(p-world.position)<90)continue;
                t.origin=p;t.yaw=bearing(loop[(number+attempt+1)%loop.size()]-p);world.updateEnemyBounds(t);
                t.origin.x+=p.x-t.center.x;t.origin.z+=p.z-t.center.z;
                // Keep the entire body above all scenery/terrain. The supported
                // flying AI uses spawn.y as its procedural cruise floor.
                t.origin.y+=highest+18+(number%3)*12-(t.center.y-t.half.y);world.updateEnemyBounds(t);
                if(t.center.x-t.half.x<world.movementBounds[0]+20||t.center.z-t.half.z<world.movementBounds[1]+20||t.center.x+t.half.x>world.movementBounds[2]-20||t.center.z+t.half.z>world.movementBounds[3]-20)continue;
                bool clear=true;
                for(const auto& other:aircraft)if(flat(t.center-other.center)<40||intersects(t.center,t.center,other.center,t.half+other.half+Vec3{4,4,4})){clear=false;break;}
                if(!clear)continue;
                for(size_t k=0;k<loop.size();++k){auto next=loop[(number+attempt+k+1)%loop.size()];next.y=t.origin.y;t.route.push_back(next);}
                t.health=health;t.ai.maxHealth=t.ai.lastHealth=health;t.ai.spawn=t.origin;t.ai.spawnYaw=t.ai.weaponYaw=t.yaw;
                aircraft.push_back(std::move(t));accepted=true;break;
            }
            // Small maps can have fewer safe flight anchors than the requested
            // cap. Air units are optional threats, never kill-goal dependencies.
            if(!accepted)break;
        }
        for(auto& aircraftTarget:aircraft)placed.push_back(std::move(aircraftTarget));
    }
    if(placed.size()>72)throw std::runtime_error("Procedural actor budget exceeded");
    if(continuous){
        std::array<const Target*,2> flyers{};for(const auto& t:placed){int family=t.spawnType==1?0:t.spawnType==18?1:-1;if(family>=0&&!flyers[family])flyers[family]=&t;}
        const size_t groundSites=sites.size();
        for(size_t zone=0;zone<zones.size();++zone){unsigned count=0;
            for(size_t i=0;i<groundSites&&count<3;++i){if(sites[i].prototype.proceduralZone!=int(zone))continue;const auto centre=sites[i].prototype.center;size_t before=sites.size();
                for(const auto* source:flyers)if(source){auto t=*source;auto delta=Vec3{centre.x-t.center.x,0,centre.z-t.center.z};t.origin=t.origin+delta;for(auto& p:t.route)p=p+delta;world.updateEnemyBounds(t);
                    auto b=world.movementBounds;bool inside=true;for(auto p:t.route)if(p.x<b[0]+20||p.z<b[1]+20||p.x>b[2]-20||p.z>b[3]-20)inside=false;
                    if(!inside)continue;
                    t.proceduralZone=int(zone);t.prototypeObjective=false;t.ai.spawn=t.origin;sites.push_back({std::move(t),{},0});
                }
                if(sites.size()>before)++count;
            }
        }
    }
    for(auto& actor:placed)world.targets.push_back(std::move(actor));
    if(continuous)prepareContinuousProceduralReinforcements(world,recipe,sites);
    else prepareProceduralReinforcements(world,policy.value("per_zone",0u),policy.value("interval",20.f));
}
void prepareProceduralReinforcements(World& world,unsigned perZone,float interval){
    if(perZone>12||!std::isfinite(interval)||interval<5||interval>120)throw std::invalid_argument("Invalid reinforcement policy");
    if(!perZone)return;
    for(size_t zone=0;zone<world.procedural.objectives.size();++zone){
        auto source=std::find_if(world.targets.begin(),world.targets.end(),[&](const auto& t){return t.active&&t.health>0&&t.proceduralZone==int(zone)&&(t.spawnType==0||t.spawnType==14);});
        if(source==world.targets.end())source=std::find_if(world.targets.begin(),world.targets.end(),[&](const auto& t){return t.active&&t.health>0&&t.proceduralZone==int(zone)&&t.spawnType>=0;});
        if(source==world.targets.end())throw std::runtime_error("No reinforcement anchor for objective");
        Generator g;g.prototype=*source;g.prototype.generated=true;g.prototype.active=true;g.prototype.ai.registered=false;g.prototype.ai.respawnRange=0;g.prototype.ai.generator=-1;
        g.prototype.ai.state=AIState::Dormant;g.prototype.ai.playerTarget=-1;g.prototype.ai.retarget=0;g.prototype.processed=false;g.prototype.escaped=false;
        g.origin=source->origin;g.remaining=perZone;g.interval=interval;g.clock=interval;
        Target slot=g.prototype;slot.active=false;slot.health=0;slot.processed=true;
        g.slots.push_back(world.targets.size());world.targets.push_back(std::move(slot));world.generators.push_back(std::move(g));
    }
}

}
