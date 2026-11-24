#include "simulation.hpp"
#include "frontend.hpp"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <limits>
using namespace iron;
using Json=nlohmann::json;
static Json recipe(const World& w){
    Json actors=Json::array();
    for(const auto& t:w.targets)if(t.prototypeObjective)actors.push_back({{"source_game",t.assetGame},{"type",t.spawnType},{"position",{t.origin.x,t.origin.y,t.origin.z}},{"yaw",t.yaw},{"health",t.health}});
    return {{"version",1},{"seed",w.prototypeSeed},{"layout",{{"game","is2"},{"mission",1}}},{"actors",actors}};
}
static Json layout(const std::string& kind="eliminate",bool extraction=false,int version=2){
    Json j={{"schema","iron-procedural-layout"},{"version",version},{"seed",42},{"units","metres"},{"plane","xz"},{"ground_y",0},{"pilot_radius",3.5},{"bounds",{-150,-150,150,150}},
        {"nodes",Json::array()},{"roads",Json::array()},{"plots",Json::array()},{"spawn",{{"node",0},{"radius",8}}},{"objectives",Json::array()}};
    for(int z=0;z<3;++z)for(int x=0;x<3;++x){int id=z*3+x;j["nodes"].push_back({x*120-120,z*120-120});if(x<2)j["roads"].push_back({{"nodes",{id,id+1}},{"width",24}});if(z<2)j["roads"].push_back({{"nodes",{id,id+3}},{"width",24}});}
    for(int z=0;z<2;++z)for(int x=0;x<2;++x){int id=z*2+x;j["plots"].push_back({{"id",id},{"bounds",{x*120-100,z*120-100,x*120-20,z*120-20}},{"sources",{id%2?"is2":"is1"}},{"role",id%2?"urban":"storage"}});}
    for(int n:{2,6,8})j["objectives"].push_back({{"node",n},{"radius",8},{"kind",kind=="eliminate"?"encounter":kind}});
    if(kind=="strike"){j["objectives"][0]["plot"]=1;j["objectives"][1]["plot"]=2;j["objectives"][2]["plot"]=3;}
    if(version==2)j["mission"]={{"kind",kind},{"extraction",extraction},{"hold_seconds",2}};
    if(extraction)j["extraction"]={{"node",0},{"radius",8}};
    return j;
}
static Json terrainLayout(const std::string& water="lake",const std::string& biome="hills"){
    auto recipe=layout("strike",true);recipe["version"]=3;
    recipe["terrain"]={{"biome",biome},{"amplitude",biome=="urban"?0:18},{"water",Json::array()}};
    recipe["encounters"]={{"ground_per_zone",4},{"air_units",2}};
    for(auto& plot:recipe["plots"])plot["buildings"]=4;
    if(water=="lake"){recipe["terrain"]["water"].push_back({{"bounds",{-95,-95,-25,-25}},{"level",-1},{"depth",6}});recipe["plots"][0]["buildings"]=0;}
    if(water=="canal"){
        recipe["terrain"]["water"].push_back({{"bounds",{15,-140,29,140}},{"level",-1},{"depth",6}});
        for(int i:{1,3})recipe["plots"][i]["bounds"][0]=32;
    }
    return recipe;
}
template<class F> static void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}assert(failed);}
static void checkProceduralRuntime(const std::filesystem::path& root){
    auto folder=std::filesystem::temp_directory_path()/("iron-procedural-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(folder);
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);}} cleanup{folder};
    auto file=folder/"recipe.json";auto write=[&](const Json& j){std::ofstream(file)<<j.dump();};
    // Both importer versions execute real world objectives, independent of
    // original campaign counters or a successful external Python prevalidation.
    for(auto kind:{std::string("eliminate"),std::string("strike"),std::string("recon")}){
        auto j=layout(kind,true);j["objectives"][0]["segment"]="unrelated legacy metadata";validateProceduralRecipe(j);write(j);World world;world.loadProceduralPrototype(root,file);assert(world.proceduralLayout&&world.missionPrototype&&world.procedural.objectives.size()==3);
        for(const auto& o:world.procedural.objectives)assert(o.segment==0);
        auto firstRoad=j.at("roads")[0].at("nodes").get<std::array<int,2>>();auto next=j.at("nodes")[firstRoad[1]].get<std::array<float,2>>();auto ahead=Vec3{next[0],0,next[1]}-world.position;assert(length(direction(world.chassisYaw,0)-ahead*(1/length(ahead)))<.001f&&world.torsoYaw==0);
        for(size_t p=0;p<j["plots"].size();++p)assert(world.targets[p].assetGame==int(p%2));
        World repeat;repeat.loadProceduralPrototype(root,file);assert(world.targets.size()==repeat.targets.size());for(size_t i=0;i<world.targets.size();++i){assert(length(world.targets[i].origin-repeat.targets[i].origin)==0);assert(world.targets[i].assetGame==repeat.targets[i].assetGame);}
        if(kind=="recon"){
            auto point=world.procedural.objectives[0].position;world.position=point;for(int tick=0;tick<5;++tick)updateProceduralMission(world,.1f);assert(world.procedural.objectives[0].progress>0);
            updateProceduralMission(world,std::numeric_limits<float>::quiet_NaN());float progress=world.procedural.objectives[0].progress;world.evaluateMission();assert(world.procedural.objectives[0].progress==progress);
            world.position=point+Vec3{30,0,0};updateProceduralMission(world,.1f);assert(world.procedural.objectives[0].progress==0);
            for(auto& objective:world.procedural.objectives){world.position=objective.position;for(int tick=0;tick<25;++tick)updateProceduralMission(world,.1f);assert(objective.complete);}
            assert(std::any_of(world.targets.begin(),world.targets.end(),[](auto& target){return target.spawnType>=0&&target.health>0;})); // guards are optional combat
        }else{
            for(auto& objective:world.procedural.objectives){assert(!objective.targets.empty());for(auto index:objective.targets){assert(world.targets[index].prototypeObjective);if(kind=="strike"){assert(world.targets[index].spawnType<0);assert(length(objective.position-world.targets[index].center)<.001f);}else assert(world.targets[index].spawnType>=0);world.targets[index].health=0;world.targetDestroyed(world.targets[index]);}}
            updateProceduralMission(world,0);assert(world.missionStatus==MissionStatus::Active); // effects and extraction remain
        }
        world.position=world.procedural.objectives.back().position;world.effects.clear();updateProceduralMission(world,0);assert(world.missionStatus==MissionStatus::Active&&world.procedural.extracting);
        world.position=world.procedural.extraction+Vec3{0,5,0};updateProceduralMission(world,0);assert(world.missionStatus==MissionStatus::Active);
        world.position=world.procedural.extraction;updateProceduralMission(world,0);assert(world.missionStatus==MissionStatus::Complete);
        Frontend menu;assert(!menu.recordResult(world)&&menu.campaign.completed==0);
        world.loadMission(root,0,0);assert(!world.proceduralLayout&&world.procedural.objectives.empty());
    }
    auto old=layout("eliminate",false,1);validateProceduralRecipe(old);write(old);World legacy;legacy.loadProceduralPrototype(root,file);assert(legacy.proceduralLayout&&!legacy.procedural.requireExtraction);
    for(auto& target:legacy.targets)if(target.prototypeObjective){target.health=0;legacy.targetDestroyed(target);}
    legacy.effects.clear();legacy.evaluateMission();assert(legacy.missionStatus==MissionStatus::Complete);
    write(layout("recon",false));World dying;dying.loadProceduralPrototype(root,file);dying.position=dying.procedural.objectives[0].position;dying.hurt(10000);for(int n=0;n<40;++n)updateProceduralMission(dying,.1f);assert(dying.missionStatus==MissionStatus::Ejecting&&!dying.procedural.objectives[0].complete);
    // Malformed recipes are rejected by native loading without replacing a live world.
    auto reject=[&](Json bad){write(bad);auto position=dying.position;rejects([&]{dying.loadProceduralPrototype(root,file);});assert(length(dying.position-position)==0&&dying.missionStatus==MissionStatus::Ejecting);};
    auto bad=layout();bad["roads"][0]["width"]=6;reject(bad);
    bad=layout();bad["plots"][0]["bounds"]={-125,-125,-115,-115};reject(bad);
    bad=layout();bad["nodes"].push_back({1,1});reject(bad);
    bad=layout();bad["roads"].push_back(bad["roads"][0]);reject(bad);
    bad=layout();bad["objectives"][0]["node"]=0;reject(bad);
    bad=layout();bad["seed"]=-1;reject(bad);
    bad=layout();bad["seed"]=4294967296ULL;reject(bad);
    bad=layout();bad["mission"]["hold_seconds"]=0;reject(bad);
    bad=layout();bad["mission"]["extraction"]=true;reject(bad);
    bad=layout("recon",true);bad["bounds"]={-80,-80,80,80};bad["nodes"]={{0,0},{20,0}};bad["roads"]=Json::array({{{"nodes",{0,1}},{"width",60}}});bad["plots"]=Json::array();bad["objectives"]=Json::array({{{"node",1},{"radius",8},{"kind","recon"}}});bad["extraction"]["radius"]=30;
    {auto noExtraction=bad;noExtraction["mission"]["extraction"]=false;noExtraction.erase("extraction");validateProceduralRecipe(noExtraction);}reject(bad);
    bad=layout("strike");bad["objectives"][0]["plot"]=2;reject(bad);
    bad=layout();bad["plots"][0]["sources"]={"missing"};reject(bad);
    bad=layout();bad["mission"]["kind"]="escort";reject(bad);
    // Real terrain is indexed separately from water. Denser parcels must not
    // break plot->goal bindings, and water must be a physical obstruction.
    for(auto water:{std::string("lake"),std::string("canal")}){
        auto generated=terrainLayout(water);write(generated);World physical;physical.loadProceduralPrototype(root,file);
        assert(physical.proceduralTerrain&&physical.proceduralLayout&&!physical.geometry().waterMesh.empty()&&physical.waterRegions.size()==1);
        float low=1e9f,high=-1e9f;for(auto& vertex:physical.geometry().terrain){low=std::min(low,vertex.position.y);high=std::max(high,vertex.position.y);}
        assert(low< -6&&high>5&&high-low>10);
        size_t buildings=0,aircraft=0;for(auto& target:physical.targets){
            if(target.spawnType<0){++buildings;float bottom=target.center.y-target.half.y;assert(std::abs(bottom-physical.surfaceHeight(target.center.x,target.center.z))<.02f);}
            if(target.spawnType==1||target.spawnType==18){
                ++aircraft;
                // airborne is the cargo-drop/falling state, not normal flight.
                assert(!target.prototypeObjective&&!target.ai.airborne&&target.route.size()==4&&target.speed>0);
                assert(target.center.y-target.half.y>=physical.surfaceHeight(target.center.x,target.center.z)+17.99f);
                assert(std::abs(target.ai.spawn.y-target.origin.y)<.001f);
            }
        }
        assert(buildings==(water=="lake"?12:16)&&aircraft==2);
        for(auto& objective:physical.procedural.objectives){assert(objective.targets.size()==1);auto& target=physical.targets[objective.targets[0]];assert(target.spawnType<0&&target.prototypeObjective);assert(length(objective.position-target.center)<.001f);}
        assert(physical.groundPassable(physical.position));
        if(water=="lake"){
            assert(physical.hasWater(-60,-60));assert(std::abs(physical.surfaceHeight(-60,-60)+7)<.01f);assert(!physical.groundPassable({-60,-7,-60}));
            for(auto& target:physical.targets)target.active=false;
            physical.position={-120,physical.surfaceHeight(-120,-60),-60};auto start=physical.position;
            physical.movePilot({-60,-7,-60});assert(length(physical.position-start)<.001f);
        }else{
            assert(physical.hasWater(22,-60)&&!physical.hasWater(22,0));assert(physical.surfaceHeight(22,0)>=0);
            for(auto& target:physical.targets)target.active=false;
            physical.position={0,physical.surfaceHeight(0,0),0};for(int step=0;step<80;++step)physical.movePilot(physical.position+Vec3{.5f,0,0});assert(physical.position.x>39.99f&&!physical.hasWater(physical.position.x,physical.position.z));
        }
        physical.loadMission(root,0,0);assert(!physical.proceduralTerrain&&physical.waterRegions.empty()&&physical.geometry().waterMesh.empty());
    }
    bad=terrainLayout();bad["terrain"]["amplitude"]=25;reject(bad);
    bad=terrainLayout();bad["terrain"]["water"][0]["level"]=2;reject(bad);
    bad=terrainLayout();bad["plots"][0]["buildings"]=1;reject(bad);
    bad=terrainLayout();bad["plots"][1]["buildings"]=0;reject(bad);
    bad=terrainLayout();bad["encounters"]["air_units"]=9;reject(bad);
    World cooperative;cooperative.cooperative=true;write(layout());rejects([&]{cooperative.loadProceduralPrototype(root,file);});
}
int main(int argc,char** argv){
    assert(argc==2||argc==3);std::filesystem::path root=argv[1];
    World first;first.loadMissionPrototype(root,42);auto expected=recipe(first);
    assert(first.missionPrototype&&first.game==1&&first.missionStatus==MissionStatus::Active);
    World repeated;repeated.loadMissionPrototype(root,42);assert(recipe(repeated)==expected);
    if(argc==3)std::ofstream(argv[2])<<expected.dump(2)<<'\n';
    for(uint32_t seed:{0u,1u,42u,4294967295u}){
        World world;world.loadMissionPrototype(root,seed);unsigned count=0;bool sources[2]{};
        assert(world.generators.empty()&&world.cargoFlights.empty());
        assert(world.pickups.size()==3);
        for(const auto& p:world.pickups)assert(length(p.position-Vec3{world.position.x,0,world.position.z})>=18);
        for(const auto& t:world.targets){
            assert(!t.protectedTarget&&t.action==0&&t.ai.identity==-1);
            if(t.mesh)for(const auto& v:*t.mesh)if(v.textured){
                assert(v.u>=0&&v.u<=1&&v.v>=0&&v.v<=1);
                if(t.assetGame==0)assert(v.v<=158.f/609.f);else assert(v.v>=158.f/609.f);
            }
            if(!t.prototypeObjective)continue;
            ++count;sources[t.assetGame]=true;
            assert(t.active&&t.health>0&&t.ai.respawnRange==0&&t.ai.targetSlot==0);
            assert(length(Vec3{t.origin.x-world.position.x,0,t.origin.z-world.position.z})>=65);
            for(const auto& obstacle:world.targets)if(obstacle.spawnType<0&&obstacle.active&&obstacle.center.y+obstacle.half.y>0&&obstacle.center.y-obstacle.half.y<12)
                assert(std::abs(t.origin.x-obstacle.center.x)>=obstacle.half.x+8||std::abs(t.origin.z-obstacle.center.z)>=obstacle.half.z+8);
        }
        assert(count>=8&&count<=12&&sources[0]&&sources[1]);
        if(seed!=42)assert(recipe(world)!=expected);
        // Original IS2 mission 1 completion must never fire in the prototype.
        world.counters={};world.evaluateMission();assert(world.missionStatus==MissionStatus::Active);
        Input input;for(int i=0;i<60;++i)world.step(input,1.f/60);
        assert(world.missionStatus==MissionStatus::Active);
        for(auto& t:world.targets)if(t.prototypeObjective){t.health=0;world.targetDestroyed(t);world.targetDestroyed(t);}
        world.evaluateMission();assert(world.missionStatus==MissionStatus::Active); // destruction effects settle first
        world.effects.clear();world.evaluateMission();assert(world.missionStatus==MissionStatus::Complete);
        Frontend menu;assert(!menu.recordResult(world)&&menu.campaign.completed==0);
    }
    World hard;hard.difficulty=World::Difficulty::Hard;hard.loadMissionPrototype(root,42);
    assert(hard.targets.size()==first.targets.size());
    for(size_t i=0;i<first.targets.size();++i)if(first.targets[i].prototypeObjective){assert(length(first.targets[i].origin-hard.targets[i].origin)==0);assert(hard.targets[i].health>=first.targets[i].health);}
    first.hurt(10000);assert(first.missionStatus==MissionStatus::Ejecting);first.effects.clear();first.evaluateMission();assert(first.missionStatus==MissionStatus::Ejecting);
    first.loadMission(root,0,1);assert(!first.missionPrototype); // ordinary loads reset mode
    World cooperative;cooperative.cooperative=true;bool rejected=false;try{cooperative.loadMissionPrototype(root,42);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
    checkProceduralRuntime(root);
    std::cout<<"Prototype seeds, mixed assets, clearance, difficulty, objective completion/failure, v1/v2 recipes, strike/recon/eliminate, extraction and atomic rejection passed\n";
}
