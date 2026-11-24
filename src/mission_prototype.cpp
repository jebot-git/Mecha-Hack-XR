#include "simulation.hpp"
#include "procedural_terrain.hpp"
#include <algorithm>
#include <array>
#include <queue>
#include <set>
#include <stdexcept>
#include <fstream>
#include <nlohmann/json.hpp>

namespace iron {
void World::loadMissionPrototype(const std::filesystem::path& root,uint32_t seed){
    if(cooperative)throw std::runtime_error("Mission prototype is singleplayer only");
    if(!std::filesystem::exists(root/"is2/shared/gameplay/missions.json"))
        throw std::runtime_error("Mission prototype requires both decoded games and the IS2 shared atlas");
    // First-stage test uses the IS2 desert layout. Campaign actors, scripts and
    // reinforcement references are removed before generating an independent mission.
    loadMission(root,0,1);
    World original;original.difficulty=difficulty;original.loadMission(root,0,0,false);
    auto findActor=[](const World& world,int type){
        for(const auto& t:world.targets)if(t.spawnType==type&&!t.generated&&t.mesh&&!t.mesh->empty())return t;
        throw std::runtime_error("Missing mission prototype actor template: "+std::to_string(type));
    };
    std::array<Target,3> prototypes{findActor(original,0),findActor(original,2),findActor(*this,14)};
    // The shared atlas is IS1 (158 rows) followed by IS2 (451 rows).
    // Clone meshes before remapping: templates and campaign instances stay intact.
    auto remap=[](std::shared_ptr<WorldMesh>& mesh,int source){
        if(!mesh)return;
        mesh=std::make_shared<WorldMesh>(*mesh);
        for(auto& v:*mesh)if(v.textured)v.v=(source?158.f+v.v*451.f:v.v*158.f)/609.f;
    };
    std::erase_if(targets,[](const Target& t){return t.spawnType>=0;});
    for(auto& t:targets){
        t.assetGame=1;t.tag=255;t.action=0;t.protectedTarget=false;t.invulnerable=false;t.ai.identity=-1;
        remap(t.mesh,1);remap(t.wreck,1);
    }
    generators.clear();cargoFlights.clear();pickups.clear();targetSlots.fill(-1);
    counters={};missionFlags=0;actorDeaths.fill(0);objectiveRegions.clear();objectiveStage=0;
    for(size_t i=0;i<prototypes.size();++i){
        auto& t=prototypes[i];t.assetGame=i==2?1:0;t.prototypeObjective=true;
        remap(t.mesh,t.assetGame);remap(t.wreck,t.assetGame);
        t.ai=EnemyAI{};t.ai.words[0]=18000;t.ai.words[1]=128;t.ai.words[2]=3;t.ai.words[3]=512;
        t.parameters=t.spawnType==0?2:0;t.trigger=0;t.spawnFlags=0;t.deathFlag=0;
        t.active=true;t.generated=false;t.protectedTarget=false;t.invulnerable=false;
        t.processed=false;t.escaped=false;t.route.clear();t.waypoint=0;
        t.health=(t.spawnType==14?160.f:80.f)*difficultyRatio;
        t.ai.maxHealth=t.ai.lastHealth=t.health;
    }
    // Conservative 4 m grid, inflated by 8 m: enough for the player and these
    // actor footprints. Four-neighbour flood fill guarantees a walkable corridor
    // to every chosen anchor without depending on destruction of scenery.
    constexpr int side=141,mid=side/2;constexpr float step=4,clearance=8;
    std::array<bool,side*side> open{},reachable{};
    auto point=[&](int id){return Vec3{position.x+(id%side-mid)*step,0,position.z+(id/side-mid)*step};};
    for(int id=0;id<side*side;++id){
        auto p=point(id);bool clear=std::abs(p.x)<990&&std::abs(p.z)<990;
        for(const auto& t:targets)if(t.active&&t.health>0&&t.center.y+t.half.y>0&&t.center.y-t.half.y<12&&
            std::abs(p.x-t.center.x)<t.half.x+clearance&&std::abs(p.z-t.center.z)<t.half.z+clearance){clear=false;break;}
        open[id]=clear;
    }
    const int start=mid*side+mid;
    if(!open[start])throw std::runtime_error("Mission prototype player anchor lacks clearance");
    std::queue<int> frontier;frontier.push(start);reachable[start]=true;
    while(!frontier.empty()){
        int id=frontier.front();frontier.pop();int x=id%side,z=id/side;
        for(auto delta:std::array<std::array<int,2>,4>{{{{-1,0}},{{1,0}},{{0,-1}},{{0,1}}}}){
            int nx=x+delta[0],nz=z+delta[1];if(nx<0||nz<0||nx>=side||nz>=side)continue;
            int next=nz*side+nx;if(open[next]&&!reachable[next]){reachable[next]=true;frontier.push(next);}
        }
    }
    // Defined integer PRNG and Fisher-Yates order make the recipe repeatable
    // across standard libraries. Seed zero is valid.
    uint32_t rng=seed;
    auto random=[&](){rng+=0x9e3779b9u;uint32_t x=rng;x=(x^(x>>16))*0x85ebca6bu;x=(x^(x>>13))*0xc2b2ae35u;return x^(x>>16);};
    std::vector<Vec3> candidates;
    for(int id=0;id<side*side;++id)if(reachable[id]){auto p=point(id);float d=length(p-Vec3{position.x,0,position.z});if(d>=65&&d<=240)candidates.push_back(p);}
    for(size_t i=candidates.size();i>1;--i)std::swap(candidates[i-1],candidates[random()%i]);
    unsigned count=8+random()%5,placed=0;
    std::vector<Vec3> anchors;
    for(auto p:candidates){
        if(std::any_of(anchors.begin(),anchors.end(),[&](Vec3 a){return length(a-p)<28;}))continue;
        // Cycling first guarantees both cartridges and all three families.
        Target t=prototypes[placed<3?placed:random()%prototypes.size()];
        t.origin=p;t.yaw=float(random()%2048)*pi/1024;t.ai.spawnYaw=t.ai.weaponYaw=t.yaw;
        updateEnemyBounds(t);t.ai.spawn=t.origin;
        if(t.half.x>clearance-step/2||t.half.z>clearance-step/2)throw std::runtime_error("Prototype actor exceeds placement clearance");
        targets.push_back(std::move(t));anchors.push_back(p);if(++placed==count)break;
    }
    if(placed!=count)throw std::runtime_error("Not enough reachable mission prototype anchors");
    // Reserve reachable supplies near the start, outside automatic pickup range.
    for(int id=0;id<side*side&&pickups.size()<3;++id)if(reachable[id]){
        auto p=point(id);float d=length(p-Vec3{position.x,0,position.z});
        if(d<18||d>30||std::any_of(pickups.begin(),pickups.end(),[&](const Pickup& supply){return length(supply.position-p)<12;}))continue;
        pickups.push_back({p,22});
    }
    missionPrototype=true;prototypeSeed=seed;randomState=seed?seed:0x49535852u;
    evaluateMission();
}

namespace {
using Json=nlohmann::json;
Vec3 recipeNode(const Json& recipe,int index,const World& world){auto p=recipe.at("nodes").at(index).get<std::array<float,2>>();return {p[0],world.surfaceHeight(p[0],p[1]),p[1]};}
void populateLayout(World& world,const std::filesystem::path& root,const Json& recipe){
    world.loadMissionPrototype(root,recipe.at("seed").get<uint32_t>());
    std::vector<Target> actors,scenery;
    std::set<std::pair<int,int>> types;
    for(auto t:world.targets){
        if(t.prototypeObjective)actors.push_back(t);
        else if(t.mesh&&!t.mesh->empty()&&t.half.x>1&&t.half.z>1&&types.insert({t.assetGame,t.type}).second)scenery.push_back(t);
    }
    World original;original.difficulty=world.difficulty;original.loadMission(root,0,0,false);
    for(auto t:original.targets)if(t.spawnType<0&&t.mesh&&!t.mesh->empty()&&t.half.x>1&&t.half.z>1&&types.insert({0,t.type}).second){
        t.assetGame=0;t.tag=255;t.action=0;t.protectedTarget=false;t.ai.identity=-1;
        for(auto mesh:{&t.mesh,&t.wreck})if(*mesh){*mesh=std::make_shared<WorldMesh>(**mesh);for(auto& v:**mesh)if(v.textured)v.v=v.v*158.f/609.f;}
        scenery.push_back(t);
    }
    if(recipe.at("version").get<int>()>=3&&recipe.at("encounters").at("air_units").get<int>()>0){
        auto appendAir=[&](const World& source,int type,int assetGame){
            auto found=std::find_if(source.targets.begin(),source.targets.end(),[&](const Target& target){return target.spawnType==type&&!target.generated&&target.mesh&&!target.mesh->empty();});
            if(found==source.targets.end())throw std::runtime_error("Missing procedural aircraft template");
            Target target=*found;target.assetGame=assetGame;target.prototypeObjective=false;target.health=80*world.difficultyRatio;
            for(auto mesh:{&target.mesh,&target.wreck})if(*mesh){*mesh=std::make_shared<WorldMesh>(**mesh);for(auto& vertex:**mesh)if(vertex.textured)vertex.v=(assetGame?158.f+vertex.v*451.f:vertex.v*158.f)/609.f;}
            actors.push_back(std::move(target));
        };
        appendAir(original,1,0);World is2;is2.difficulty=world.difficulty;is2.loadMission(root,0,1);appendAir(is2,18,1);
    }
    if(scenery.empty()||actors.empty())throw std::runtime_error("Missing procedural templates");
    world.targets.clear();world.editGeometry().terrain.clear();world.pickups.clear();world.projectiles.clear();world.effects.clear();world.sounds.clear();world.procedural={};
    if(recipe.at("version")==4)world.movementBounds=recipe.at("bounds").get<std::array<float,4>>();
    bool physical=recipe.at("version").get<int>()>=3;if(physical)buildProceduralTerrain(world,recipe);
    // Plot identifiers remain recipe identifiers even when a dense parcel holds
    // many buildings or a reservoir reserves an empty parcel.
    std::vector<std::vector<size_t>> plotTargets(recipe.at("plots").size());
    size_t plotIndex=0;
    for(const auto& plot:recipe.at("plots")){
        auto bounds=plot.at("bounds").get<std::array<float,4>>();std::string role=plot.value("role",std::string("industrial"));int count=physical?plot.at("buildings").get<int>():1;
        int cols=count?std::clamp(int(std::ceil(std::sqrt(count*(bounds[2]-bounds[0])/(bounds[3]-bounds[1])))),1,count):1,rows=count?(count+cols-1)/cols:1;
        for(int cell=0;cell<count;++cell){
            float width=(bounds[2]-bounds[0])/cols,depth=(bounds[3]-bounds[1])/rows;
            std::array<float,4> b{bounds[0]+(cell%cols)*width,bounds[1]+(cell/cols)*depth,bounds[0]+(cell%cols+1)*width,bounds[1]+(cell/cols+1)*depth};
            float margin=physical?std::min(8.f,.4f*std::min(width,depth)):4.f;
            std::vector<size_t> choices;float best=1e9f;
            for(size_t i=0;i<scenery.size();++i){auto& target=scenery[i];std::string source=target.assetGame?"is2":"is1";
                if(plot.contains("sources")&&std::find(plot.at("sources").begin(),plot.at("sources").end(),source)==plot.at("sources").end())continue;
                float scale=std::min({1.f,(width-margin)/(2*target.half.x),(depth-margin)/(2*target.half.z)});if(scale<.25f)continue;
                float span=2*std::max(target.half.x,target.half.z),height=2*target.half.y;
                float roleScore=role=="urban"?std::abs(height-45.f)/45.f:role=="storage"?std::abs(height-9.f)/9.f+std::abs(span-30.f)/60.f:std::abs(height-22.f)/22.f+std::abs(span-40.f)/80.f;
                float score=roleScore+4*(1-scale);
                if(score<best-.15f){best=score;choices.clear();}if(score<=best+.15f)choices.push_back(i);
            }
            if(choices.empty())throw std::runtime_error("No allowed procedural scenery fits plot "+std::to_string(plotIndex));
            uint32_t pick=world.prototypeSeed+uint32_t(plotIndex)*0x9e3779b9u+uint32_t(cell)*0xc2b2ae35u;pick=(pick^(pick>>16))*0x85ebca6bu;pick^=pick>>13;
            Target target=scenery[choices[pick%choices.size()]];float scale=std::min({1.f,(width-margin)/(2*target.half.x),(depth-margin)/(2*target.half.z)});
            for(auto mesh:{&target.mesh,&target.wreck})if(*mesh){*mesh=std::make_shared<WorldMesh>(**mesh);for(auto& vertex:**mesh)vertex.position=vertex.position*scale;}
            target.fragmentSpan=target.fragmentSpan*scale;target.fragmentStep*=scale;
            target.origin={(b[0]+b[2])/2,0,(b[1]+b[3])/2};target.ai.boundsReady=false;world.updateEnemyBounds(target);
            target.origin.x+=(b[0]+b[2])/2-target.center.x;target.origin.z+=(b[1]+b[3])/2-target.center.z;
            target.origin.y+=world.surfaceHeight(target.origin.x,target.origin.z)-(target.center.y-target.half.y);world.updateEnemyBounds(target);
            if(target.center.x-target.half.x<b[0]-.001f||target.center.x+target.half.x>b[2]+.001f||target.center.z-target.half.z<b[1]-.001f||target.center.z+target.half.z>b[3]+.001f)throw std::runtime_error("Procedural scenery exceeds plot");
            target.prototypeObjective=false;target.proceduralZone=-1;target.processed=false;target.active=true;target.escaped=false;target.invulnerable=false;
            plotTargets[plotIndex].push_back(world.targets.size());world.targets.push_back(std::move(target));
        }
        ++plotIndex;
    }
    auto rectangle=[&](float x0,float z0,float x1,float z1,Vec3 color){Vec3 a{x0,0,z0},b{x1,0,z0},c{x1,0,z1},d{x0,0,z1};for(auto p:{a,b,c,a,c,d})world.editGeometry().terrain.push_back({p,color});};
    if(!physical)for(const auto& road:recipe.at("roads")){
        auto ends=road.at("nodes").get<std::array<int,2>>();auto a=recipeNode(recipe,ends[0],world),b=recipeNode(recipe,ends[1],world);float half=road.at("width").get<float>()/2;
        Vec3 color=road.value("class",std::string("local"))=="arterial"?Vec3{.25f,.27f,.28f}:Vec3{.18f,.21f,.22f};
        rectangle(std::min(a.x,b.x)-half,std::min(a.z,b.z)-half,std::max(a.x,b.x)+half,std::max(a.z,b.z)+half,color);
    }
    if(!physical)world.indexTerrain();int spawn=recipe.at("spawn").at("node");world.position=recipeNode(recipe,spawn,world);world.chassisYaw=world.torsoYaw=0;
    for(const auto& road:recipe.at("roads")){auto ends=road.at("nodes").get<std::array<int,2>>();int other=ends[0]==spawn?ends[1]:ends[1]==spawn?ends[0]:-1;
        if(other>=0){auto forward=recipeNode(recipe,other,world)-world.position;world.chassisYaw=std::atan2(-forward.x,-forward.z);break;}}
    world.aimYaw=world.chassisYaw;world.aimPitch=0;
    std::string kind=recipe.at("version").get<int>()>=2?recipe.at("mission").at("kind").get<std::string>():"eliminate";
    if(recipe.at("version").get<int>()>=2){world.procedural.requireExtraction=recipe.at("mission").at("extraction");}
    world.procedural.extraction=world.position;world.procedural.extractionRadius=recipe.at("spawn").at("radius");
    if(world.procedural.requireExtraction){world.procedural.extraction=recipeNode(recipe,recipe.at("extraction").at("node"),world);world.procedural.extractionRadius=recipe.at("extraction").at("radius");}
    if(recipe.at("version")==4){world.procedural.segmentCount=int(recipe.at("segments").size());world.procedural.orderedSegments=recipe.at("mission").at("ordered_segments");}
    for(size_t i=0;i<recipe.at("objectives").size();++i){auto& zone=recipe.at("objectives")[i];ProceduralObjective objective;objective.segment=recipe.at("version")==4?zone.at("segment").get<int>():0;
        objective.kind=kind=="strike"?ProceduralGoalKind::Strike:kind=="recon"?ProceduralGoalKind::Recon:ProceduralGoalKind::Eliminate;
        objective.position=recipeNode(recipe,zone.at("node"),world);objective.radius=zone.at("radius");objective.holdSeconds=recipe.at("version").get<int>()>=2?recipe.at("mission").value("hold_seconds",4.f):4.f;
        if(objective.kind==ProceduralGoalKind::Strike){size_t plot=zone.at("plot");if(plotTargets.at(plot).empty())throw std::runtime_error("Strike target parcel has no building");
            size_t target=*std::min_element(plotTargets[plot].begin(),plotTargets[plot].end(),[&](size_t a,size_t b){return length(world.targets[a].center-objective.position)<length(world.targets[b].center-objective.position);});
            auto& building=world.targets.at(target);building.prototypeObjective=true;building.proceduralZone=int(i);
            building.health=building.ai.maxHealth=building.ai.lastHealth=300*world.difficultyRatio;objective.targets.push_back(target);objective.position=building.center;}
        world.procedural.objectives.push_back(std::move(objective));
    }
    world.proceduralLayout=true;
    world.climate=recipe.contains("terrain")?climateForBiome(recipe.at("terrain").at("biome").get<std::string>()):Climate::Temperate;
    if(recipe.contains("environment"))world.environment=restrictEnvironment(readEnvironment(recipe.at("environment")),world.climate);
    populateProceduralEncounters(world,recipe,actors);
    for(size_t i=0;i<world.procedural.objectives.size();++i){auto& objective=world.procedural.objectives[i];
        if(objective.kind==ProceduralGoalKind::Eliminate){for(size_t n=0;n<world.targets.size();++n)if(world.targets[n].prototypeObjective&&world.targets[n].spawnType>=0&&world.targets[n].proceduralZone==int(i))objective.targets.push_back(n);
            if(objective.targets.empty())throw std::runtime_error("Procedural encounter has no objective actors");}
    }
    // Keep useful supplies on the first connected road outside auto-collection.
    for(const auto& road:recipe.at("roads")){auto ends=road.at("nodes").get<std::array<int,2>>();int other=ends[0]==spawn?ends[1]:ends[1]==spawn?ends[0]:-1;
        if(other>=0){auto d=recipeNode(recipe,other,world)-world.position;if(length(d)<24)continue;world.pickups.push_back({world.position+d*(20/length(d)),22});break;}}
    if(!physical)world.groundColor={.22f,.30f,.19f};updateProceduralMission(world,0);
}
}
void World::loadProceduralPrototype(const std::filesystem::path& root,const std::filesystem::path& file){
    if(cooperative)throw std::runtime_error("Procedural mission is singleplayer only");
    std::error_code error;auto size=std::filesystem::file_size(file,error);if(error||size>1024*1024)throw std::runtime_error("Missing or oversized procedural recipe");
    std::ifstream input(file);if(!input)throw std::runtime_error("Missing procedural recipe");Json recipe;input>>recipe;loadProceduralRecipe(root,recipe);
}
void World::loadProceduralRecipe(const std::filesystem::path& root,const nlohmann::json& recipe){
    if(cooperative)throw std::runtime_error("Procedural mission is singleplayer only");
    validateProceduralRecipe(recipe);
    // Invalid content or a failed placement leaves the current mission intact.
    World next;static_cast<Pilot&>(next)=static_cast<const Pilot&>(*this);next.difficulty=difficulty;
    next.damageUntil={};populateLayout(next,root,recipe);*this=std::move(next);
}

}
