#pragma once
#include "rpg/destruction.hpp"
#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
namespace iron::rpg {
// Developer-only export of runtime meshes/poses; no game assets are rewritten.
template<class Render> void previewDestruction(const std::filesystem::path &assets,const std::filesystem::path &out,Render render){
  using Json=nlohmann::json;Json samples=Json::array();
  for(const char *id:{"jump_pod","kaiju_spawnling","salvager_gunship","city_tower_spire","industrial","tree","oxygen_tank","rock"}){
    Session game(723);game.add(1,"Preview");GameSettings settings;settings.enemyCount=2;settings.opener=5;settings.objective=0;
    game.command(1,{{"op","settings"},{"settings",settings}});game.command(1,{{"op","ready"},{"value",true}});game.command(1,{{"op","deploy"}});
    game.enemies[0].disabled=10000;game.enemies[1].disabled=10000;game.enemies[0].hp=0;game.member(1).body.position={0,0,400};auto &world=game.world;world.rpgPrototype=true;world.prototypeSeed=722;
    world.proceduralTerrain=false;world.editGeometry().terrain.clear();world.indexTerrain();world.effects.clear();world.pickups.clear();world.interiorRegions.clear();world.waterRegions.clear();for(auto &t:world.targets)t.active=false;world.targets[1].active=true;world.targets[1].origin={450,0,450};world.updateEnemyBounds(world.targets[1]);
    Target target;applyOriginalModel(target,id);target.origin={0,0,0};target.active=true;target.spawnType=-1;target.health=100;
    bool actor=std::any_of(enemyCatalog().begin(),enemyCatalog().end(),[&](const auto &s){return std::string_view(id)==s.id;});
    if(actor)for(size_t n=0;n<enemyCatalog().size();++n)if(std::string_view(id)==enemySpec(n).id)game.enemies[0].spec=int(n);
    size_t index=actor?0:game.enemies.size();world.targets[index]=target;if(std::string_view(id)=="salvager_gunship")world.targets[index].origin.y=18;world.updateEnemyBounds(world.targets[index]);target=world.targets[index];
    float span=2*std::max({target.half.x,target.half.y,target.half.z,4.f});Vec3 center=target.center;if(target.origin.y>0){span=std::max(span,32.f);center.y=10;}
    auto exportMesh=[&](float age){
      const auto &t=world.targets[index];WorldMesh mesh=*t.mesh;bool dead=t.health<=0;
      const auto *rig=enemyAnimationRig(assets/"models",id,mesh.size());
      if(dead&&rig){auto pose=poseEnemyDeath(*rig,0,0,0,age,unsigned(index)^722);float floor=-1e9f;
        for(size_t p=0;p<rig->parts.size();++p)for(auto [first,count]:rig->parts[p].ranges)for(size_t n=first;n<first+count;++n){mesh[n].position=animatedPoint(pose.parts[p],mesh[n].position);floor=std::max(floor,-mesh[n].position.y);}
        for(auto &v:mesh){v.position.y+=std::max(floor,t.origin.y-9*age*age);v.color=v.color*.56f;}
      }else if(dead&&age>=1.6f){mesh=*t.wreck;for(auto &v:mesh)v.position.y+=t.rpgRestY;}
      else if(dead){auto pose=collapsePose(age,0);Vec3 origin{};
        if(destructionMaterial(id)==DestructionMaterial::Wood){float blend=std::clamp(age/1.6f,0.f,1.f);pose.rotation=angles(0,blend*blend*(3-2*blend)*pi/2);pose.scale={1,1,1};float floor=-1e9f;for(const auto &v:mesh)floor=std::max(floor,-rotate(pose.rotation,v.position).y);origin.y=floor;}
        for(auto &v:mesh){v.position=rotate(pose.rotation,{v.position.x*pose.scale.x,v.position.y*pose.scale.y,v.position.z*pose.scale.z})+origin;}}
      if(!dead)for(auto &v:mesh)v.position=v.position+t.origin;
      Json vertices=Json::array();for(const auto &v:mesh)vertices.push_back({v.position.x,v.position.y,v.position.z,v.color.x,v.color.y,v.color.z,v.u,v.v,v.textured,v.oneSided});
      samples.push_back({{"id",id},{"age",age},{"triangles",mesh.size()/3},{"vertices",vertices}});
    };
    render(world,center,span,std::string(id)+"-alive");exportMesh(-1);
    world.targets[index].health=0;game.step(1.f/60);float started=world.targets[index].rpgDeathAt;
    for(float age:{0.f,.25f,.6f,1.f,1.6f,2.5f,5.f}){while(world.elapsed+.0001f<started+age){float dt=std::min(1.f/60,started+age-world.elapsed);game.step(dt);}
      render(world,center,span,std::string(id)+"-"+std::to_string(int(age*1000)));exportMesh(age);
    }
  }
  std::ofstream(out/"runtime-poses.json")<<Json{{"schema",1},{"axis","Y up, -Z forward, metres"},{"samples",samples}}.dump();
}
}
