#include "skirmish.hpp"
#include "simulation.hpp"
#include "frontend.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace iron;
using Json=nlohmann::json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F action,const char* message){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,message);}
void geometry(const Json& recipe){
    validateProceduralRecipe(recipe);auto& nodes=recipe.at("nodes");check(recipe.at("version")==3&&nodes.size()<=81&&recipe.at("plots").size()<=64&&recipe.at("roads").size()<=144,"native recipe budget");
    // Independently traverse the emitted graph and check distinct objective sites.
    std::vector<std::vector<size_t>> adjacency(nodes.size());for(auto& road:recipe.at("roads")){auto ends=road.at("nodes").get<std::array<size_t,2>>();adjacency[ends[0]].push_back(ends[1]);adjacency[ends[1]].push_back(ends[0]);}
    std::vector<int> distance(nodes.size(),-1);std::vector<size_t> queue{recipe.at("spawn").at("node")};distance[queue.front()]=0;
    for(size_t n=0;n<queue.size();++n)for(auto other:adjacency[queue[n]])if(distance[other]<0){distance[other]=distance[queue[n]]+1;queue.push_back(other);}
    check(queue.size()==nodes.size()&&recipe.at("roads").size()>=nodes.size(),"native road graph disconnected or has no alternate route");
    std::set<size_t> targets;for(auto& goal:recipe.at("objectives")){size_t node=goal.at("node");check(distance[node]>=3&&targets.insert(node).second,"native objective too near insertion or duplicated");}
    size_t buildings=0;for(auto& plot:recipe.at("plots"))buildings+=plot.at("buildings").get<size_t>();check(buildings>0&&buildings<=768,"native scenery budget");
}
void complete(World& world){
    for(auto& generator:world.generators)generator.remaining=0;
    for(auto& objective:world.procedural.objectives){
        if(objective.kind==ProceduralGoalKind::Recon){world.position=objective.position;for(int frame=0;frame<42;++frame)updateProceduralMission(world,.1f);check(objective.complete,"native recon hold did not complete");}
        else {check(!objective.targets.empty(),"native kill/strike goal has no targets");for(auto index:objective.targets){auto& target=world.targets.at(index);target.health=0;world.targetDestroyed(target);}}
    }
    world.effects.clear();updateProceduralMission(world,0);
    if(world.procedural.requireExtraction){world.position=world.procedural.extraction;updateProceduralMission(world,0);}
    check(world.missionStatus==MissionStatus::Complete,"native mission objective/extraction did not complete");
}
}
int main(int argc,char** argv){try{
    check(argc==2||argc==3,"retail path and optional recipe output");auto root=std::filesystem::path(argv[1]);SkirmishSettings settings;settings.seed=42;
    auto first=generateSkirmishRecipe(settings);geometry(first);check(first==generateSkirmishRecipe(settings),"native seed is not deterministic");check(first==Json::parse(first.dump()),"native recipe JSON roundtrip");
    check(first.at("generator")=="xorshift32-v3"&&first.at("nodes")[0]==Json::array({-240,-240}),"native generation stream marker/anchor");
    auto different=settings;different.seed=43;auto other=generateSkirmishRecipe(different);check(other.at("nodes")!=first.at("nodes")&&other.at("roads")!=first.at("roads"),"different seeds do not vary layout");
    if(argc==3)std::ofstream(argv[2])<<first.dump(2)<<'\n';
    for(uint32_t seed:{0u,1u,UINT32_MAX}){auto value=settings;value.seed=seed;auto recipe=generateSkirmishRecipe(value);geometry(recipe);check(recipe==generateSkirmishRecipe(value),"boundary seed nondeterminism");}
    unsigned boundaryCases=0;
    for(int cells:{skirmishMinCells,skirmishMaxCells})for(int density:{skirmishMinDensity,skirmishMaxDensity})for(int ground:{skirmishMinGroundPerZone,skirmishMaxGroundPerZone})for(int air:{skirmishMinAirUnits,skirmishMaxAirUnits}){
        auto value=settings;value.cells=cells;value.density=density;value.groundPerZone=ground;value.airUnits=air;geometry(generateSkirmishRecipe(value));++boundaryCases;
    }
    for(int field=0;field<10;++field){auto invalid=settings;switch(field){case 0:invalid.cells=1;break;case 1:invalid.cells=9;break;case 2:invalid.density=0;break;case 3:invalid.density=17;break;case 4:invalid.groundPerZone=0;break;case 5:invalid.groundPerZone=13;break;case 6:invalid.airUnits=-1;break;case 7:invalid.airUnits=9;break;case 8:invalid.terrain="lava";break;case 9:invalid.water="ocean";break;}rejects([&]{generateSkirmishRecipe(invalid);},"invalid native settings accepted");}
    {auto invalid=settings;invalid.kind=static_cast<ProceduralGoalKind>(99);rejects([&]{generateSkirmishRecipe(invalid);},"unknown native mission kind accepted");}
    unsigned runtimeCases=0;
    for(auto biome:{"urban","grassland","desert","hills"})for(auto water:{"none","lake","canal"})for(auto kind:{ProceduralGoalKind::Eliminate,ProceduralGoalKind::Strike,ProceduralGoalKind::Recon}){
        auto value=settings;value.terrain=biome;value.water=water;value.kind=kind;auto recipe=generateSkirmishRecipe(value);geometry(recipe);
        World world;world.loadProceduralRecipe(root,recipe);check(world.missionPrototype&&world.proceduralLayout&&world.proceduralTerrain&&world.missionStatus==MissionStatus::Active,"native recipe did not load as isolated physical mission");
        check(world.procedural.objectives.size()==3&&world.groundPassable(world.position)&&!world.hasWater(world.position.x,world.position.z),"native insertion/objectives invalid");
        size_t expected=0;for(auto& plot:recipe.at("plots"))expected+=plot.at("buildings").get<size_t>();size_t buildings=0,ground=0,air=0;
        for(auto& target:world.targets){
            if(target.generated&&!target.active)continue;
            if(target.spawnType<0){++buildings;check(std::abs(target.center.y-target.half.y-world.surfaceHeight(target.center.x,target.center.z))<.02f,"native building floats above physical terrain");}
            else if(target.spawnType==1||target.spawnType==18){++air;check(!target.prototypeObjective&&!target.ai.airborne&&target.route.size()==4,"native aircraft has invalid patrol/objective role");}
            else {++ground;check(target.proceduralZone>=0&&target.proceduralZone<3&&world.groundPassable(target.origin,std::hypot(target.half.x,target.half.z)),"native ground unit has unsafe placement");}
        }
        check(buildings==expected&&buildings>recipe.at("plots").size()*3,"native urban density not populated");check(ground>=3&&ground<=18&&air<=3,"native actor counts exceed settings");
        check(world.waterRegions.size()==size_t(std::string(water)=="none"?0:1)&&world.geometry().waterMesh.empty()==world.waterRegions.empty(),"native water mesh/physics disagreement");
        if(!world.waterRegions.empty()){
            bool foundWet=false;auto bounds=world.waterRegions.front().bounds;
            for(int x=1;x<=5&&!foundWet;++x)for(int z=1;z<=5;++z){float px=bounds[0]+(bounds[2]-bounds[0])*x/6,pz=bounds[1]+(bounds[3]-bounds[1])*z/6;if(world.hasWater(px,pz,3.5f)){check(!world.groundPassable({px,world.surfaceHeight(px,pz),pz}),"native water is walkable");foundWet=true;break;}}
            check(foundWet,"native water has no physical wet area");
        }
        auto before=world.position;auto invalid=recipe;invalid["version"]=99;rejects([&]{world.loadProceduralRecipe(root,invalid);},"invalid native recipe loaded");check(length(world.position-before)<.001f&&world.missionStatus==MissionStatus::Active,"failed native recipe replaced live world");
        complete(world);Frontend menu;check(!menu.recordResult(world)&&menu.campaign.completed==0,"Skirmish modified campaign progress");++runtimeCases;
    }
    World cooperative;cooperative.cooperative=true;rejects([&]{cooperative.loadProceduralRecipe(root,first);},"Skirmish accepted co-op mode");
    auto noExtract=settings;noExtract.extraction=false;noExtract.kind=ProceduralGoalKind::Recon;World direct;direct.loadProceduralRecipe(root,generateSkirmishRecipe(noExtract));complete(direct);check(!direct.procedural.requireExtraction,"extraction switch ignored");
    std::cout<<"Native Skirmish determinism, "<<boundaryCases<<" boundary settings, "<<runtimeCases<<" biome/water/objective runtime combinations, density, placement and isolated completion passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
