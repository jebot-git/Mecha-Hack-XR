#include "coop.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace iron;
using namespace iron::coop;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> static void rejects(F action,const char* message){bool failed=false;try{action();}catch(const std::runtime_error&){failed=true;}check(failed,message);}
static void clearBody(const World& world,Vec3 point){
    check(std::isfinite(point.x)&&std::isfinite(point.y)&&std::isfinite(point.z),"spawn must be finite");
    check(std::abs(point.x)<=996.5f&&std::abs(point.z)<=996.5f,"spawn footprint stays within world bounds");
    check(std::abs(point.y-world.surfaceHeight(point.x,point.z))<.001f,"spawn stands on terrain");
    for(const auto& target:world.targets){
        if(target.health<=0||target.escaped||(!target.active&&(target.spawnType<0||target.generated)))continue;
        bool overlap=std::abs(point.x-target.center.x)<target.half.x+2&&std::abs(point.z-target.center.z)<target.half.z+2&&point.y<target.center.y+target.half.y&&point.y+12>target.center.y-target.half.y;
        check(!overlap,"full player body overlaps scenery or an authored actor");
    }
}
static void formation(const World& world,const std::vector<Vec3>& points){
    for(size_t i=0;i<points.size();++i){
        clearBody(world,points[i]);
        auto displacement=points[i]-world.position;check(std::hypot(displacement.x,displacement.z)<=64.001f,"spawn search remains near authored start");
        // Check the entire first eight metres independently of the swept test.
        for(int step=1;step<=32;++step)clearBody(world,points[i]+direction(world.chassisYaw,0)*(step*.25f));
        for(size_t j=0;j<i;++j){auto delta=points[i]-points[j];check(std::hypot(delta.x,delta.z)>=9.999f,"spawned players retain ten metres of separation");
            auto local=rotate(angles(-world.chassisYaw),delta);check(std::abs(local.x)>=5.999f||std::abs(local.z)>=39.999f,"teammate blocks initial hip-weapon firing lane");}
    }
}
int main(int argc,char** argv){try{
    check(argc==2||argc==3,"retail path and optional report");
    World fixture;fixture.position={};fixture.mission=0;fixture.targets.clear();
    std::array<unsigned,4> slots{0,1,2,3};auto open=spawnPositions(fixture,slots);formation(fixture,open);
    check(length(open[0])==0&&open[1].x==10&&open[2].x==-10&&open[3].x==20,"open field uses lateral formation and retains host anchor");
    Target obstacle;obstacle.center={10,6,0};obstacle.half={3,6,3};fixture.targets={obstacle};auto moved=spawnPositions(fixture,slots);formation(fixture,moved);check(length(moved[1]-open[1])>0,"blocked preferred start is relocated");
    std::array<unsigned,1> one{0};obstacle.center={0,1,0};obstacle.half={3,1,3};fixture.targets={obstacle};auto low=spawnPositions(fixture,one);check(length(low[0])>0,"low scenery cannot intersect feet even below torso collision probe");
    obstacle.center={0,6,0};obstacle.half={3,6,3};obstacle.active=false;obstacle.spawnType=3;fixture.targets={obstacle};auto dormant=spawnPositions(fixture,one);check(length(dormant[0])>0,"dormant authored actor start is reserved");
    fixture.targets[0].generated=true;check(length(spawnPositions(fixture,one)[0])==0,"unused generated actor pool does not block start");
    obstacle.active=true;obstacle.spawnType=-1;obstacle.center={0,24,0};obstacle.half={10,2,10};fixture.targets={obstacle};check(length(spawnPositions(fixture,one)[0])==0,"overhead scenery above full body does not block spawn");
    obstacle.center={0,6,0};obstacle.half={100,6,100};fixture.targets={obstacle};rejects([&]{spawnPositions(fixture,one);},"fully obstructed search fails without unsafe fallback");
    fixture.targets.clear();fixture.position={999,0,999};formation(fixture,spawnPositions(fixture,slots));
    fixture.position={};std::array<unsigned,2> duplicate{0,0};rejects([&]{spawnPositions(fixture,duplicate);},"duplicate slot rejected");std::array<unsigned,1> invalid{4};rejects([&]{spawnPositions(fixture,invalid);},"out-of-range slot rejected");
    unsigned formations=0,deployments=0;Json missions=Json::array();
    for(int game=0;game<2;++game)for(int mission=0;mission<(game?20:16);++mission)for(auto difficulty:{World::Difficulty::Normal,World::Difficulty::Hard}){
        World world;world.difficulty=difficulty;world.loadMission(argv[1],mission,game);
        // Every nonempty slot subset, in every join order, includes host migration
        // and a new member reusing a lower vacant slot.
        for(unsigned mask=1;mask<16;++mask){std::vector<unsigned> occupied;for(unsigned slot=0;slot<4;++slot)if(mask&(1u<<slot))occupied.push_back(slot);
            do{auto points=spawnPositions(world,occupied);formation(world,points);++formations;}while(std::next_permutation(occupied.begin(),occupied.end()));}
        Room room(argv[1]);room.campaign.rules.load(argv[1],game);room.difficulty=difficulty;room.mission=mission;room.host=1;for(int id=1;id<=4;++id)room.add(id);room.start();
        std::vector<Vec3> starts;for(auto& player:room.players)starts.push_back(player.pilot.position);formation(world,starts);room.step(tick);
        for(auto& player:room.players){
            clearBody(room.world,player.pilot.position);static_cast<Pilot&>(room.world)=player.pilot;room.world.teammates.clear();for(auto& other:room.players)if(other.id!=player.id)room.world.teammates.push_back(&other.pilot);
            for(int bearing=0;bearing<8;++bearing){room.world.position=player.pilot.position;for(int step=0;step<4;++step)room.world.movePilot(room.world.position+direction(bearing*pi/4,0)*.25f);check(length(room.world.position-player.pilot.position)>.999f,"actual movement cannot exit spawn in every direction");}
            room.world.position=player.pilot.position;for(int step=0;step<32;++step)room.world.movePilot(room.world.position+direction(player.pilot.chassisYaw,0)*.25f);check(length(room.world.position-player.pilot.position)>7.999f,"actual forward walk-out blocked");
        }
        room.start();for(size_t i=0;i<starts.size();++i)check(length(room.players[i].pilot.position-starts[i])<.001f,"restart changes deterministic formation");++deployments;
        if(difficulty==World::Difficulty::Normal){Json positions=Json::array();for(auto point:starts)positions.push_back({point.x,point.y,point.z});missions.push_back({{"campaign",game+1},{"mission",mission+1},{"positions",positions}});}
    }
    Json report={{"missions",missions},{"ordered_formations",formations},{"deployment_and_restart_cases",deployments},{"player_spacing_metres",10},{"forward_walk_out_metres",8},{"search_radius_metres",64},{"blocked_spawns",0},{"body_overlaps",0},{"forward_friendly_obstructions",0}};
    if(argc==3)std::ofstream(argv[2])<<report.dump(2)<<'\n';
    std::cout<<formations<<" ordered formations, "<<deployments<<" deployments/restarts, all 36 missions and both difficulties: clear bodies, 10m separation, 8m exit and clear forward lanes\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
