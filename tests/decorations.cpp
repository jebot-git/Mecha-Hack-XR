#include "simulation.hpp"
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <nlohmann/json.hpp>
using namespace iron;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(int argc,char** argv){try{check(argc==2,"assets path");for(int game=0;game<2;++game){World w;w.loadMission(argv[1],0,game);w.targets.clear();w.projectiles.clear();w.editGeometry().terrain.clear();w.indexTerrain();
    check(w.debrisVertices.contains(0)&&w.debrisVertices.contains(0x36a74)&&w.debrisVertices.contains(0x36a94),"default and both bridge debris models have collision vertices");
    for(unsigned kind=0;kind<5;++kind){auto& mesh=w.pickupMeshes[kind];check(!mesh.empty(),"pickup mesh exists");float low=100,high=-100;bool textured=false,uvSpan=false;float u=mesh[0].u,v=mesh[0].v;
        for(auto p:mesh){low=std::min(low,p.position.y);high=std::max(high,p.position.y);textured|=p.textured!=0;uvSpan|=p.u!=u||p.v!=v;check(p.u>=0&&p.u<=1&&p.v>=0&&p.v<=1,"pickup atlas bounds");}
        check(std::abs(low+1.5f)<.001f&&high>=1.5f&&high<=1.517f,"three metre crate shell retains ground contact with separated surface details");
        if(kind==0)check(high>1.507f,"coplanar crate details are lifted clear of the shell");
        check(kind==0||(textured&&uvSpan),"marked pickup crates retain original texture coordinates");
    }
    for(auto& [model,vertices]:w.debrisVertices)for(float scale:{.65f,1.f,2.f})for(float dt:{.01f,.033f,.1f}){
        w.effects.clear();w.effect(WeaponEffect::Debris,{0,0,0},{3,-4,2},Weapon::Empty,scale);w.effects.back().model=model;
        for(int i=0;i<int(2.9f/dt);++i){w.stepProjectiles(dt);auto& fx=w.effects.front();float pitch=fx.age*1600*2*pi/2048,low=10000;
            for(auto p:vertices)low=std::min(low,fx.position.y+(p.y*std::cos(pitch)-p.z*std::sin(pitch))*scale);
            float surface=w.surfaceHeight(fx.position.x,fx.position.z);
            check(low>=surface-.0001f,"rotating debris never penetrates the visible terrain");if(std::abs(fx.position.y-w.debrisFloor(fx))<.0001f)check(std::abs(low-surface)<.0001f,"contact point rests exactly on terrain");
        }
    }
    // Raised/sloped terrain catches the former y=0-only contact calculation.
    w.editGeometry().terrain={{{-20,1,-20},{}},{{20,5,-20},{}},{{20,5,20},{}},{{-20,1,-20},{}},{{20,5,20},{}},{{-20,1,20},{}}};w.indexTerrain();
    check(std::abs(w.surfaceHeight(5,3)-3.5f)<.0001f,"sloped terrain height uses source geometry");
    for(auto& [id,vertices]:w.debrisVertices){WeaponEffect fx;fx.kind=WeaponEffect::Debris;fx.model=id;fx.position={5,0,3};fx.size=1.3f;fx.age=.7f;fx.position.y=w.debrisFloor(fx);
        float yaw=fx.age*700*2*pi/2048,pitch=fx.age*1600*2*pi/2048,minimum=1000;
        for(auto v:vertices){float y=(v.y*std::cos(pitch)-v.z*std::sin(pitch))*fx.size,z=(v.y*std::sin(pitch)+v.z*std::cos(pitch))*fx.size;
            float x=v.x*fx.size*std::cos(yaw)+z*std::sin(yaw);float ground=3+.1f*(fx.position.x+x);minimum=std::min(minimum,fx.position.y+y-ground);}
        check(std::abs(minimum)<.0001f,"rendered fragment contacts raised/sloped terrain without penetration");
    }
    Pickup crate{{5,0,3},1};auto position=w.pickupPosition(crate);float low=100;
    for(auto& v:w.pickupMeshes[w.pickupTypes[1]])low=std::min(low,v.position.y);check(std::abs(position.y+low-3.5f)<.0001f,"crate stands on the visible terrain");
    Target tank;tank.spawnType=0;tank.origin={5,.66f,3};tank.mesh=std::make_shared<WorldMesh>(WorldMesh{{{-.5f,-.66f,-.5f},{}},{{.5f,1,.5f},{}}});w.updateEnemyBounds(tank);
    check(std::abs(tank.origin.y-.66f-3.5f)<.0001f,"tank tracks rest on terrain without changing mesh shape");
}
    std::array<bool,3> checkedDucts{};
    // Exercise real mission geometry, including the reported IS1 mission 2.
    for(int game=0;game<2;++game)for(int mission=0;mission<(game?20:16);++mission){
        World w;w.loadMission(argv[1],mission,game);
        for(const auto& target:w.targets)if(target.spawnType<0&&(target.type==1||(!game&&target.type==4))){
            int fixture=game?2:target.type==4?1:0;if(checkedDucts[fixture])continue;
            auto file=std::filesystem::path(argv[1])/(game?"is2/gameplay/world-6bb68.json":target.type==4?"gameplay/world-24550.json":"gameplay/world-228a0.json");
            nlohmann::json source;std::ifstream(file)>>source;size_t vertex=0;
            for(size_t face=0;face<source.at("faces").size();++face){
                auto ids=source.at("faces")[face].at("indices").get<std::vector<size_t>>();
                // These source ducts contain exact front/back polygon pairs.
                bool paired=fixture==1?(face>=10&&face<=19)||(face>=37&&face<=46):fixture==2?(face>=14&&face<=17):(face>=12&&face<=15);
                auto n=source.at("normals")[face].get<std::array<int,4>>();Vec3 normal{float(n[0]),float(n[1]),-float(n[2])};
                for(size_t tri=1;tri+1<ids.size();++tri){
                    check(vertex+2<target.mesh->size(),"duct triangle coverage");
                    const auto& a=target.mesh->at(vertex);const auto& b=target.mesh->at(vertex+1);const auto& c=target.mesh->at(vertex+2);
                    if(a.oneSided!=float(paired)||b.oneSided!=a.oneSided||c.oneSided!=a.oneSided)throw std::runtime_error("Duct fixture "+std::to_string(fixture)+" face "+std::to_string(face)+" sided="+std::to_string(a.oneSided)+" expected="+std::to_string(paired));
                    if(paired)check(dot(cross(b.position-a.position,c.position-a.position),normal)>0,"duct winding must match its outward lighting normal on both sides");
                    vertex+=3;
                }
            }
            check(vertex==target.mesh->size(),"duct facing metadata preserves all source triangles");checkedDucts[fixture]=true;
        }
        for(const auto& v:w.geometry().terrain)check(v.position.y==0,"retail ground overlays share a continuous physical plane");
        for(auto& t:w.targets)if(t.spawnType<0&&t.wreck&&!t.wreck->empty()){
            auto p=w.wreckPosition(t);float low=1e9f;
            for(const auto& v:*t.wreck){auto q=rotate(angles(t.yaw,t.ai.pitch,t.ai.bank),v.position);low=std::min(low,p.y+q.y);}
            check(std::abs(low)<.0001f,"persistent building rubble rests on ground, independently of intact model origin");
        }
        auto selected=std::find_if(w.targets.begin(),w.targets.end(),[](const auto& t){return t.type==0&&!t.fragmentModels.empty();});
        if(selected!=w.targets.end()){
            w.effects.clear();w.targetDestroyed(*selected);float bottom=1e9f;
            for(auto& v:*selected->mesh)bottom=std::min(bottom,v.position.y+selected->origin.y);
            float firstLayer=1e9f;for(auto& fx:w.effects)if(fx.kind==WeaponEffect::Debris)firstLayer=std::min(firstLayer,fx.position.y);
            check(std::abs(firstLayer-bottom-selected->fragmentStep*.5f)<.0001f,"building fragment grid starts at its base instead of adding the center height twice");
        }
    }
    check(std::all_of(checkedDucts.begin(),checkedDucts.end(),[](bool value){return value;}),"both campaigns and factory duct fixtures exercised");
    std::cout<<"IS1/IS2 terrain continuity, persistent wrecks, textured pickups and rotating fragment contacts passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
