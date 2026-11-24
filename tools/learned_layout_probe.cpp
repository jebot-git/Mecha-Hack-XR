// Native placement/completion checks for corpus-trained recipe samples.
#include "simulation.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <map>
#include <set>
#include <iostream>
#include <stdexcept>
using namespace iron;
using Json=nlohmann::json;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static bool covered(const World& world,float x,float z){
    auto cell=world.geometry().groundTiles.find({int(std::floor(x/64)),int(std::floor(z/64))});if(cell==world.geometry().groundTiles.end())return false;
    for(auto id:cell->second){auto& t=world.geometry().groundTriangles[id];float a=((t.b.z-t.c.z)*(x-t.c.x)+(t.c.x-t.b.x)*(z-t.c.z))/t.denominator,b=((t.c.z-t.a.z)*(x-t.c.x)+(t.a.x-t.c.x)*(z-t.c.z))/t.denominator;if(a>=-.00001f&&b>=-.00001f&&a+b<=1.00001f)return true;}return false;
}
static void terrainChecks(const World& world){
    using Key=std::array<float,4>;using Points=std::set<std::pair<float,float>>;std::map<Key,std::vector<Points>> boundaries;
    size_t next=0;double area=0;
    for(auto& patch:world.geometry().terrainPatches){check(patch.first==next&&patch.count%3==0&&patch.count>0,"Invalid terrain draw range");next+=patch.count;check(next<=world.geometry().terrain.size(),"Terrain range overflow");
        std::array<Points,4> points;
        for(size_t n=patch.first;n<next;++n){auto p=world.geometry().terrain[n].position;check(p.x>=patch.low.x&&p.x<=patch.high.x&&p.z>=patch.low.z&&p.z<=patch.high.z&&p.y>=patch.low.y&&p.y<=patch.high.y,"Patch bounds miss vertices");
            if(p.x==patch.low.x)points[0].insert({p.z,p.y});if(p.x==patch.high.x)points[1].insert({p.z,p.y});if(p.z==patch.low.z)points[2].insert({p.x,p.y});if(p.z==patch.high.z)points[3].insert({p.x,p.y});}
        boundaries[{0,patch.low.x,patch.low.z,patch.high.z}].push_back(points[0]);boundaries[{0,patch.high.x,patch.low.z,patch.high.z}].push_back(points[1]);
        boundaries[{1,patch.low.z,patch.low.x,patch.high.x}].push_back(points[2]);boundaries[{1,patch.high.z,patch.low.x,patch.high.x}].push_back(points[3]);
        check(covered(world,(patch.low.x+patch.high.x)/2,(patch.low.z+patch.high.z)/2),"Terrain tile has a hole");
    }
    check(next==world.geometry().terrain.size(),"Terrain draw ranges leave vertices behind");
    for(auto& [key,sides]:boundaries){bool outer=key[0]==0?(key[1]==world.movementBounds[0]||key[1]==world.movementBounds[2]):(key[1]==world.movementBounds[1]||key[1]==world.movementBounds[3]);check(sides.size()==size_t(outer?1:2),"Missing neighbour terrain tile");if(!outer)check(sides[0]==sides[1],"Terrain seam has different vertices/heights");}
    for(size_t n=0;n<world.geometry().terrain.size();n+=3){auto a=world.geometry().terrain[n].position,b=world.geometry().terrain[n+1].position,c=world.geometry().terrain[n+2].position;double projected=double(b.x-a.x)*(c.z-a.z)-double(b.z-a.z)*(c.x-a.x);check(projected>0,"Degenerate/reversed terrain triangle");area+=projected/2;}
    double expected=double(world.movementBounds[2]-world.movementBounds[0])*(world.movementBounds[3]-world.movementBounds[1]);check(std::abs(area-expected)<expected*.000001,"Terrain mesh area does not cover world");
}
int main(int argc,char** argv){try{
    check(argc==3,"retail root and sample directory required");
    std::vector<std::filesystem::path> files;
    for(const auto& file:std::filesystem::directory_iterator(argv[2]))if(file.path().extension()==".json")files.push_back(file.path());
    std::sort(files.begin(),files.end());check(!files.empty(),"No recipe samples");
    unsigned buildings=0,actors=0,completed=0;Json cases=Json::array();
    for(const auto& path:files){
        std::cerr<<"Checking "<<path.filename()<<"\n";
        std::ifstream input(path);Json recipe;input>>recipe;validateProceduralRecipe(recipe);
        auto started=std::chrono::steady_clock::now();World world;world.loadProceduralRecipe(argv[1],recipe);
        double loadMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        bool segmented=recipe.at("version")==4;if(segmented)terrainChecks(world);
        check(world.proceduralLayout&&world.proceduralTerrain&&world.missionStatus==MissionStatus::Active,"Learned recipe did not load");
        check(world.groundPassable(world.position)&&!world.hasWater(world.position.x,world.position.z),"Insertion is not dry/passable");
        for(const auto& road:recipe.at("roads")){
            auto ids=road.at("nodes").get<std::array<int,2>>();auto a=recipe.at("nodes")[ids[0]].get<std::array<float,2>>(),b=recipe.at("nodes")[ids[1]].get<std::array<float,2>>();
            int steps=std::max(1,int(std::ceil(std::hypot(b[0]-a[0],b[1]-a[1])/5)));
            for(int i=0;i<=steps;++i){float t=float(i)/steps,x=a[0]+t*(b[0]-a[0]),z=a[1]+t*(b[1]-a[1]);check(covered(world,x,z),"Road has no ground mesh");check(world.groundPassable({x,world.surfaceHeight(x,z),z},3.5f),"Connected road contains impassable terrain/water");}
        }
        unsigned expected=0,scenery=0,units=0;
        for(auto& plot:recipe.at("plots"))expected+=plot.at("buildings").get<unsigned>();
        for(const auto& target:world.targets){
            check(std::isfinite(length(target.center)),"Non-finite target");
            if(target.spawnType<0){
                ++scenery;
                check(std::abs(target.center.y-target.half.y-world.surfaceHeight(target.center.x,target.center.z))<.02f,"Building is not grounded");
                auto d=target.center-(world.position+Vec3{0,6,0});
                check(std::abs(d.x)>=target.half.x+3.5f||std::abs(d.z)>=target.half.z+3.5f||std::abs(d.y)>=target.half.y+6,"Insertion obstructed by a building");
            }else if(target.active&&target.health>0&&!target.escaped){
                ++units;
                if(target.spawnType!=1&&target.spawnType!=18){
                    check(world.groundPassable(target.origin,std::hypot(target.half.x,target.half.z)),"Ground actor is on blocked terrain/water");
                    check(std::hypot(target.center.x-world.position.x,target.center.z-world.position.z)>=59.99f,"Ground actor is too close to insertion");
                }
                for(const auto& other:world.targets)if(&target!=&other&&other.active&&other.health>0){
                    auto delta=target.center-other.center;
                    check(std::abs(delta.x)>=target.half.x+other.half.x||std::abs(delta.y)>=target.half.y+other.half.y||std::abs(delta.z)>=target.half.z+other.half.z,"Actor overlaps another actor or scenery");
                }
            }
        }
        check(scenery==expected,"Learned building count changed during native placement");
        buildings+=scenery;actors+=units;
        unsigned crossed=0;
        if(segmented){
            // Guard actors are bypass-tested by placement. Here isolate static
            // scenery to test continuous pilot motion through every connector.
            std::vector<bool> active;for(auto& t:world.targets){active.push_back(t.active);if(t.spawnType>=0)t.active=false;}
            auto insertion=world.position;
            for(const auto& link:recipe.at("connections"))for(size_t n=1;n<link.at("nodes").size();++n){
                auto a=recipe.at("nodes")[link.at("nodes")[n-1].get<size_t>()].get<std::array<float,2>>(),b=recipe.at("nodes")[link.at("nodes")[n].get<size_t>()].get<std::array<float,2>>();
                world.position={a[0],world.surfaceHeight(a[0],a[1]),a[1]};int steps=std::max(1,int(std::ceil(std::hypot(b[0]-a[0],b[1]-a[1]))));
                for(int step=1;step<=steps;++step){float t=float(step)/steps;Vec3 proposed{a[0]+t*(b[0]-a[0]),0,a[1]+t*(b[1]-a[1])};world.movePilot(proposed);check(std::hypot(world.position.x-proposed.x,world.position.z-proposed.z)<.01f,"Pilot stalled crossing sector connector");if(std::abs(world.position.x)>1000||std::abs(world.position.z)>1000)++crossed;}
            }
            world.position=insertion;for(size_t i=0;i<world.targets.size();++i)world.targets[i].active=active[i];world.sounds.clear();
            // Out-of-order recon must not accumulate a hold in a later sector.
            if(world.procedural.orderedSegments&&world.procedural.segmentCount>1&&world.procedural.objectives.back().kind==ProceduralGoalKind::Recon){auto& later=world.procedural.objectives.back();world.position=later.position;for(int i=0;i<50;++i)updateProceduralMission(world,.1f);check(!later.complete&&later.progress==0,"Later sector activated before current sector");world.position=insertion;}
            auto malformed=recipe;malformed["segments"][0]["node_range"][1]=9999;bool rejected=false;auto before=world.position;
            try{world.loadProceduralRecipe(argv[1],malformed);}catch(const std::exception&){rejected=true;}check(rejected&&length(world.position-before)<.001f&&world.proceduralLayout,"Invalid sector range was not rejected transactionally");
        }
        for(auto& generator:world.generators)generator.remaining=0;
        const size_t targetStorage=world.targets.size();std::set<int> reinforcedTypes;
        auto checkPopulation=[&](){unsigned ground=0,aircraft=0;for(const auto& t:world.targets)if(t.spawnType>=0&&t.active&&t.health>0&&!t.escaped)(t.spawnType==1||t.spawnType==18?aircraft:ground)++;
            check(ground<=world.proceduralPopulation.groundLimit&&aircraft<=world.proceduralPopulation.airLimit&&world.targets.size()==targetStorage,"Reinforcement population/storage limit exceeded");};
        for(auto& objective:world.procedural.objectives){
            size_t zone=size_t(&objective-world.procedural.objectives.data());
            if(world.proceduralPopulation.enabled){
                float best=-1;Vec3 point=world.position;
                for(const auto& n:recipe.at("nodes")){Vec3 p{n[0].get<float>(),0,n[1].get<float>()};float distance=std::hypot(p.x-objective.position.x,p.z-objective.position.z);if(distance>world.proceduralPopulation.activationRange*.8f||!world.groundPassable(p))continue;
                    float nearest=10000;for(const auto& spawner:world.proceduralPopulation.spawners)if(spawner.zone==int(zone))for(const auto& site:spawner.sites)nearest=std::min(nearest,std::hypot(p.x-site.prototype.center.x,p.z-site.prototype.center.z));
                    if(nearest>best){best=nearest;point=p;}}
                world.position=point;world.position.y=world.surfaceHeight(point.x,point.z);
                const auto spawnedBefore=world.proceduralPopulation.spawned;
                for(int tick=0;tick<std::max(4096,int((objective.killQuota+3)*(std::ceil(world.proceduralPopulation.interval)+5)));++tick){auto before=world.proceduralPopulation.spawned;world.stepGenerators(1);checkPopulation();
                    if(world.proceduralPopulation.spawned>before){for(const auto& t:world.targets)if(t.spawnType>=0&&t.active&&t.health>0&&!t.escaped){
                        if(t.proceduralZone==int(zone))reinforcedTypes.insert(t.spawnType);
                        if(t.spawnType!=1&&t.spawnType!=18)check(world.groundPassable(t.origin,std::hypot(t.half.x,t.half.z)),"Reinforced ground body lacks support");
                        for(const auto& other:world.targets)if(&t!=&other&&other.active&&other.health>0&&!other.escaped){auto d=t.center-other.center;check(std::abs(d.x)>=t.half.x+other.half.x||std::abs(d.y)>=t.half.y+other.half.y||std::abs(d.z)>=t.half.z+other.half.z,"Reinforcement overlaps actor/scenery");}
                    }}
                    for(auto& t:world.targets)if(t.spawnType>=0&&t.active&&t.health>0&&!t.escaped&&(t.proceduralZone==int(zone)||t.spawnType==1||t.spawnType==18)){t.health=0;world.targetDestroyed(t);}
                    world.effects.clear();world.sounds.clear();updateProceduralMission(world,0);
                    if(objective.kind==ProceduralGoalKind::Eliminate){if(objective.complete)break;}
                    else if(tick>=int(world.proceduralPopulation.interval*3+10))break;
                }
                check(world.proceduralPopulation.spawned>spawnedBefore,"Sector spawners failed to replenish");
                if(objective.kind==ProceduralGoalKind::Eliminate){check(objective.complete&&objective.kills==objective.killQuota,"Continuous elimination quota failed");continue;}
            }
            if(objective.kind==ProceduralGoalKind::Recon){world.position=objective.position;for(int frame=0;frame<42;++frame)updateProceduralMission(world,.1f);check(objective.complete,"Recon hold failed");}
            else{
                check(!objective.targets.empty(),"Kill/strike goal has no targets");
                for(auto index:objective.targets){auto& target=world.targets.at(index);target.health=0;world.targetDestroyed(target);}
                updateProceduralMission(world,0);check(objective.complete,"Ordered kill objective failed");
            }
        }
        world.effects.clear();updateProceduralMission(world,0);
        if(world.procedural.requireExtraction){world.position=world.procedural.extraction;updateProceduralMission(world,0);}
        check(world.missionStatus==MissionStatus::Complete,"Learned mission did not complete");++completed;
        cases.push_back({{"file",path.filename().string()},{"reinforcement_emissions",world.proceduralPopulation.spawned},{"reinforcement_families",reinforcedTypes},{"target_slots",targetStorage-scenery},{"spawners",world.proceduralPopulation.spawners.size()},{"buildings",scenery},{"actors",units},{"completed",true},{"segments",world.procedural.segmentCount},{"terrain_triangles",world.geometry().terrain.size()/3},{"terrain_patches",world.geometry().terrainPatches.size()},{"terrain_vertex_bytes",world.geometry().terrain.size()*sizeof(WorldVertex)},{"load_ms",loadMs},{"travel_steps_outside_legacy_bounds",crossed}});
        if(segmented){world.loadMission(argv[1],0,0);check(world.movementBounds==std::array<float,4>{-1000,-1000,1000,1000}&&world.geometry().terrainPatches.empty(),"Expanded bounds/patches leaked into campaign");}
    }
    std::cout<<Json({{"passed",true},{"recipes",files.size()},{"completed",completed},{"buildings",buildings},{"actors",actors},{"cases",cases}}).dump(2)<<'\n';
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
