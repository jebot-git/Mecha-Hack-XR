#include "simulation.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <queue>
#include <stdexcept>
using namespace iron;
using Json=nlohmann::json;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static float flat(Vec3 p){return std::hypot(p.x,p.z);}
static Json layout(int cells,int spacing,const std::string& kind,uint32_t seed){
    Json nodes=Json::array(),roads=Json::array(),zones=Json::array();int side=cells+1;float half=cells*spacing*.5f;
    for(int z=0;z<side;++z)for(int x=0;x<side;++x)nodes.push_back({x*spacing-half,z*spacing-half});
    for(int z=0;z<side;++z)for(int x=0;x<side;++x){int a=z*side+x;if(x<cells)roads.push_back({{"nodes",{a,a+1}},{"width",z%2?20:28}});if(z<cells)roads.push_back({{"nodes",{a,a+side}},{"width",x%2?20:28}});}
    for(int n:{side*side-1,side*(side-1),side-1}){
        zones.push_back({{"node",n},{"radius",8},{"kind",kind=="eliminate"?"encounter":kind}});if(zones.size()==size_t(std::min(cells,3)))break;
    }
    return {{"version",2},{"seed",seed},{"bounds",{-half-24,-half-24,half+24,half+24}},{"nodes",nodes},{"roads",roads},{"spawn",{{"node",0},{"radius",8}}},{"objectives",zones},{"mission",{{"kind",kind},{"extraction",true}}},{"extraction",{{"node",0},{"radius",8}}}};
}
static World fixture(const Json& recipe,int cells,int spacing){
    World world;world.targets.clear();world.editGeometry().terrain.clear();world.indexTerrain();world.game=1;world.missionPrototype=world.proceduralLayout=true;world.prototypeSeed=recipe.at("seed");
    auto p=recipe.at("nodes")[0].get<std::array<float,2>>();world.position={p[0],0,p[1]};float half=cells*spacing*.5f;
    for(int z=0;z<cells;++z)for(int x=0;x<cells;++x){Target t;t.center=t.origin={(x+.5f)*spacing-half,10,(z+.5f)*spacing-half};t.half={(spacing-48)*.5f,10,(spacing-48)*.5f};world.targets.push_back(t);}
    return world;
}
static Json signature(const World& world){
    Json actors=Json::array();for(const auto& t:world.targets)if(t.spawnType>=0){Json routes=Json::array();for(auto p:t.route)routes.push_back({p.x,p.y,p.z});actors.push_back({{"type",t.spawnType},{"source",t.assetGame},{"zone",t.proceduralZone},{"position",{t.origin.x,t.origin.y,t.origin.z}},{"yaw",t.yaw},{"goal",t.prototypeObjective},{"route",routes}});}return actors;
}
static void walkingBypasses(const World& world,const Json& recipe){
    // Independent local flood fill checks the final roster, rather than relying
    // on a particular three-segment bypass chosen during placement.
    constexpr int side=81,mid=side/2;
    for(const auto& actor:world.targets)if(actor.spawnType>=0&&actor.spawnType!=1&&actor.spawnType!=18){
        Vec3 forward{};
        for(const auto& road:recipe.at("roads")){
            auto ends=road.at("nodes").get<std::array<int,2>>();auto a=recipe.at("nodes")[ends[0]].get<std::array<float,2>>(),b=recipe.at("nodes")[ends[1]].get<std::array<float,2>>();
            bool horizontal=a[1]==b[1];float across=horizontal?std::abs(actor.center.z-a[1]):std::abs(actor.center.x-a[0]);
            float along=horizontal?(actor.center.x-a[0])/(b[0]-a[0]):(actor.center.z-a[1])/(b[1]-a[1]);
            if(across<.01f&&along>=0&&along<=1){forward=horizontal?Vec3{b[0]>a[0]?1.f:-1.f,0,0}:Vec3{0,0,b[1]>a[1]?1.f:-1.f};break;}
        }
        check(flat(forward)>0,"actor is not on a connected road");
        std::array<bool,side*side> free{},visited{},goal{};
        for(int z=0;z<side;++z)for(int x=0;x<side;++x){auto point=actor.center+Vec3{float(x-mid),0,float(z-mid)};point.y=6;bool clear=std::abs(point.x)<996.5f&&std::abs(point.z)<996.5f;
            for(const auto& obstacle:world.targets)if(obstacle.active&&!obstacle.escaped&&obstacle.health>0){auto delta=point-obstacle.center;
                if(std::abs(delta.x)<obstacle.half.x+3.5f&&std::abs(delta.z)<obstacle.half.z+3.5f&&std::abs(delta.y)<obstacle.half.y+6){clear=false;break;}}
            free[z*side+x]=clear;
        }
        float extent=std::abs(forward.x)*actor.half.x+std::abs(forward.z)*actor.half.z;std::queue<int> queue;
        for(int d=int(std::ceil(extent+3.5f));d<=int(std::ceil(extent+8));++d){
            int x=mid+int(forward.x*d),z=mid+int(forward.z*d);if(x>=0&&z>=0&&x<side&&z<side&&free[z*side+x])goal[z*side+x]=true;
            x=mid-int(forward.x*d);z=mid-int(forward.z*d);if(x>=0&&z>=0&&x<side&&z<side&&free[z*side+x]){visited[z*side+x]=true;queue.push(z*side+x);}
        }
        bool reached=false;
        while(!queue.empty()&&!reached){int id=queue.front();queue.pop();if(goal[id]){reached=true;break;}for(auto step:std::array<std::array<int,2>,4>{{{{1,0}},{{-1,0}},{{0,1}},{{0,-1}}}}){int x=id%side+step[0],z=id/side+step[1];if(x>=0&&z>=0&&x<side&&z<side){int next=z*side+x;if(free[next]&&!visited[next]){visited[next]=true;queue.push(next);}}}}
        check(reached,"final actor roster blocks player bypass around an encounter");
    }
}
static void inspect(const World& world,const Json& recipe){
    std::vector<const Target*> actors;std::vector<unsigned> zones(recipe.at("objectives").size());unsigned patrols=0;bool families[3]{};
    unsigned aircraft=0;
    for(const auto& t:world.targets)if(t.spawnType>=0){
        if(t.spawnType==1||t.spawnType==18){
            ++aircraft;check(!t.prototypeObjective&&t.proceduralZone==-1,"air threat became a fly-away kill-goal dependency");check(!t.ai.airborne&&t.route.size()==4,"normal flight mistaken for falling deployment");
            check(flat(t.center-world.position)>=89.999f,"aircraft starts too close to player");
            check(t.ai.parent==-1&&t.ai.generator==-1&&t.ai.attachment==-1&&t.ai.identity==-1,"aircraft retained campaign references");
            float highest=0;for(const auto& vertex:world.geometry().terrain)highest=std::max(highest,vertex.position.y);for(const auto& object:world.targets)if(object.spawnType<0)highest=std::max(highest,object.center.y+object.half.y);
            check(t.center.y-t.half.y>=highest+17.999f,"aircraft intersects skyline or hills");
            for(auto waypoint:t.route)check(std::abs(waypoint.y-t.ai.spawn.y)<.001f&&std::abs(waypoint.x)<=980&&std::abs(waypoint.z)<=980,"aircraft patrol leaves safe flight height/bounds");
            continue;
        }
        check(t.spawnType==0||t.spawnType==2||t.spawnType==14,"unsupported actor family");families[t.spawnType==0?0:t.spawnType==2?1:2]=true;
        check(t.active&&t.health>0&&!t.generated&&!t.processed&&!t.escaped,"encounter starts with invalid actor lifecycle");
        check(!t.protectedTarget&&!t.invulnerable&&t.tag==255&&t.action==0,"campaign objective state leaked into encounter");
        check(t.ai.parent==-1&&t.ai.attachment==-1&&t.ai.generator==-1&&t.ai.identity==-1&&t.ai.targetSlot==0&&t.ai.respawnRange==0,"campaign actor references leaked into encounter");
        check(t.proceduralZone>=0&&size_t(t.proceduralZone)<zones.size(),"actor missing objective assignment");++zones[t.proceduralZone];
        bool eliminate=recipe.at("objectives")[t.proceduralZone].at("kind")=="encounter";check(t.prototypeObjective==eliminate,"guard incorrectly required for recon/strike");
        check(std::abs(t.center.y-t.half.y-world.surfaceHeight(t.origin.x,t.origin.z))<.05f,"actor is not grounded");check(flat(t.center-world.position)>=59.999f,"actor starts too close to player");check(world.groundPassable(t.origin,std::hypot(t.half.x,t.half.z)),"ground actor footprint enters water or steep slope");
        for(const auto& zone:recipe.at("objectives")){auto p=recipe.at("nodes").at(zone.at("node").get<int>()).get<std::array<float,2>>();check(flat(t.center-Vec3{p[0],0,p[1]})>=zone.at("radius").get<float>()+std::hypot(t.half.x,t.half.z)+1.999f,"actor blocks mission clear disc");}
        for(const auto& obstacle:world.targets)if(&t!=&obstacle&&obstacle.active&&obstacle.health>0){auto d=t.center-obstacle.center;check(std::abs(d.x)>=t.half.x+obstacle.half.x||std::abs(d.z)>=t.half.z+obstacle.half.z||std::abs(d.y)>=t.half.y+obstacle.half.y,"actor overlaps scenery or another actor");}
        for(auto* other:actors)check(flat(t.center-other->center)>=19.999f,"actors are bunched together");
        actors.push_back(&t);
        if(!t.route.empty()){
            ++patrols;check(t.spawnType!=2&&t.route.size()==2,"invalid mobile patrol role");auto delta=t.route[0]-t.route[1];check(delta.x==0||delta.z==0,"patrol shortcuts across blocks");check(flat(delta)>=15.999f&&flat(delta)<=24.001f,"unbounded patrol leg");
            auto radius=std::hypot(t.ai.localHalf.x,t.ai.localHalf.z);
            for(int sample=0;sample<=24;++sample){auto center=t.center+delta*(sample/24.f);
                for(const auto& obstacle:world.targets)if(&obstacle!=&t){auto d=center-obstacle.center;check(std::abs(d.x)>=radius+obstacle.half.x||std::abs(d.z)>=radius+obstacle.half.z||std::abs(d.y)>=t.half.y+obstacle.half.y,"final patrol corridor crosses scenery or another actor's body");}
            }
        }
    }
    check(!actors.empty()&&actors.size()<=64&&aircraft<=8&&actors.size()+aircraft<=72,"actor budget exceeded");for(auto count:zones)check(count>0,"objective has no defenders");check(patrols>0,"no mobile patrols generated");
    if(recipe.at("mission").at("kind")=="eliminate")check(families[0]&&families[1]&&families[2],"eliminate map lacks mixed enemy families");
}
int main(int argc,char** argv){try{
    check(argc==2,"retail path");World source;source.loadMissionPrototype(argv[1],42);std::vector<Target> normal;
    for(int type:{0,2,14}){auto found=std::find_if(source.targets.begin(),source.targets.end(),[&](const auto& t){return t.prototypeObjective&&t.spawnType==type;});check(found!=source.targets.end(),"missing real actor template");normal.push_back(*found);}
    for(auto pair:{std::array<int,2>{0,1},std::array<int,2>{1,18}}){World flying;flying.loadMission(argv[1],0,pair[0],false);auto found=std::find_if(flying.targets.begin(),flying.targets.end(),[&](const auto& target){return target.spawnType==pair[1]&&!target.generated;});check(found!=flying.targets.end(),"missing real aircraft template");auto actor=*found;actor.assetGame=pair[0];actor.health=80;normal.push_back(actor);}
    auto hard=normal;for(auto& t:hard)t.health*=2;
    // Dirty references exercise sanitization rather than relying on already clean templates.
    for(auto& t:normal){t.ai.parent=7;t.ai.generator=3;t.ai.identity=4;t.ai.attachment=9;t.ai.respawnRange=500;t.ai.targetSlot=5;}
    unsigned cases=0;
    for(uint32_t seed:{0u,1u,42u,UINT32_MAX})for(auto dimensions:{std::array<int,2>{2,80},std::array<int,2>{4,120},std::array<int,2>{8,180}})for(auto kind:{"eliminate","strike","recon"}){
        auto recipe=layout(dimensions[0],dimensions[1],kind,seed);auto world=fixture(recipe,dimensions[0],dimensions[1]);populateProceduralEncounters(world,recipe,normal);inspect(world,recipe);
        auto replay=fixture(recipe,dimensions[0],dimensions[1]);populateProceduralEncounters(replay,recipe,normal);check(signature(world)==signature(replay),"encounter placement is not deterministic");
        auto difficult=fixture(recipe,dimensions[0],dimensions[1]);populateProceduralEncounters(difficult,recipe,hard);check(signature(world)==signature(difficult),"difficulty changes encounter positions or composition");++cases;
        if(seed==42&&dimensions[0]==4&&std::string(kind)=="eliminate")walkingBypasses(world,recipe);
    }
    for(auto kind:{"eliminate","strike","recon"}){
        auto maximum=layout(8,180,kind,42);maximum["objectives"]=Json::array();for(int node=80;node>=73;--node)maximum["objectives"].push_back({{"node",node},{"radius",8},{"kind",std::string(kind)=="eliminate"?"encounter":kind}});
        auto world=fixture(maximum,8,180);populateProceduralEncounters(world,maximum,normal);inspect(world,maximum);++cases;
    }
    for(auto kind:{"eliminate","strike","recon"}){
        auto expanded=layout(4,120,kind,42);expanded["version"]=3;expanded["encounters"]={{"ground_per_zone",6},{"air_units",3}};auto world=fixture(expanded,4,120);populateProceduralEncounters(world,expanded,normal);inspect(world,expanded);
        unsigned ground=0,air=0;for(const auto& actor:world.targets)if(actor.spawnType>=0){if(actor.spawnType==1||actor.spawnType==18)++air;else ++ground;}
        check(ground>=15&&air==3,"expanded default does not increase enemy counts and include aircraft");auto replay=fixture(expanded,4,120);populateProceduralEncounters(replay,expanded,hard);check(signature(world)==signature(replay),"expanded difficulty changes encounters");++cases;
    }
    auto waterRecipe=layout(4,120,"eliminate",17);waterRecipe["version"]=3;waterRecipe["encounters"]={{"ground_per_zone",6},{"air_units",3}};auto wet=fixture(waterRecipe,4,120);wet.proceduralTerrain=true;wet.waterRegions.push_back({{-264,-15,264,15},-1,6});
    for(const auto& road:waterRecipe.at("roads")){auto ends=road.at("nodes").get<std::array<int,2>>();auto a=waterRecipe.at("nodes")[ends[0]].get<std::array<float,2>>(),b=waterRecipe.at("nodes")[ends[1]].get<std::array<float,2>>();float half=road.at("width").get<float>()/2;wet.waterCrossings.push_back({std::min(a[0],b[0])-half,std::min(a[1],b[1])-half,std::max(a[0],b[0])+half,std::max(a[1],b[1])+half});}
    populateProceduralEncounters(wet,waterRecipe,normal);inspect(wet,waterRecipe);++cases;
    auto capped=layout(8,180,"eliminate",31);capped["version"]=3;capped["encounters"]={{"ground_per_zone",12},{"air_units",8}};capped["objectives"]=Json::array();for(int node=80;node>=73;--node)capped["objectives"].push_back({{"node",node},{"radius",8},{"kind","encounter"}});
    auto crowded=fixture(capped,8,180);populateProceduralEncounters(crowded,capped,normal);inspect(crowded,capped);++cases;
    for(auto controls:{Json{{"ground_per_zone",13},{"air_units",0}},Json{{"ground_per_zone",6},{"air_units",9}}}){auto invalid=layout(2,80,"eliminate",42);invalid["version"]=3;invalid["encounters"]=controls;auto world=fixture(invalid,2,80);auto before=world.targets.size();bool failed=false;try{populateProceduralEncounters(world,invalid,normal);}catch(const std::runtime_error&){failed=true;}check(failed&&world.targets.size()==before,"invalid count cap changed the world");}
    auto recipe=layout(2,80,"eliminate",42);auto blocked=fixture(recipe,2,80);Target wall;wall.center={0,6,0};wall.half={999,6,999};blocked.targets.push_back(wall);auto before=blocked.targets.size();bool failed=false;try{populateProceduralEncounters(blocked,recipe,normal);}catch(const std::runtime_error&){failed=true;}check(failed&&blocked.targets.size()==before,"blocked layout silently produces unsafe or partial encounters");
    std::cout<<cases<<" procedural encounter cases: small/large layouts, ground/air counts, mission types, clear starts/zones, water crossings, safe flight/patrols, determinism and difficulty passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
