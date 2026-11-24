#include "rpg/session.hpp"
#include "destruction_visuals.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace iron;
using namespace iron::rpg;

static Session mission() {
    Session s(723);s.add(1,"Pilot");
    GameSettings setup;setup.maps=1;setup.location=0;setup.environment=3;
    setup.objective=0;setup.opener=5;setup.enemy=3;setup.enemyCount=1;
    s.command(1,{{"op","settings"},{"settings",setup}});
    s.command(1,{{"op","ready"},{"value",true}});
    s.command(1,{{"op","deploy"}});
    s.enemies[0].disabled=10000;
    s.world.editGeometry().terrain.clear();s.world.indexTerrain();
    s.world.interiorDecks.clear();s.world.interiorRegions.clear();s.world.waterRegions.clear();
    for(auto &target:s.world.targets)target.active=false;
    auto &m=s.member(1);m.body.position={};m.input.focused=false;
    return s;
}
static int waves(const World &world){
    return int(std::count_if(world.effects.begin(),world.effects.end(),
                             [](const auto &fx){return fx.model==rpgLandingWave;}));
}
int main(){
    constexpr float dry=-std::numeric_limits<float>::infinity();
    for(int fps:{30,60,120}){
        const float dt=1.f/fps;
        float speed=0,impact=0;Vec3 p{0,55,0};
        for(int i=0;i<fps*4;++i){
            float contact=stepMechaDescent(p,10,speed,dry,dt);
            if(contact>0){assert(impact==0);impact=contact;}
        }
        assert(std::abs(impact-42.f)<.02f&&p.y==10&&speed==0);
        p.y=30;
        for(int i=0;i<fps*2;++i)stepMechaDescent(p,0,speed,18,dt);
        assert(p.y>15.5f&&p.y<16.1f&&speed<=2.61f);
        for(int i=0;i<fps*10;++i)stepMechaDescent(p,0,speed,18,dt);
        assert(p.y==0&&speed==0);
    }

    {
        auto game=mission();auto &pilot=game.member(1);
        game.world.interiorDecks.push_back({{-15,-12,5,12},{-15,24,0},{5,24,0}});
        pilot.body.position={3,24,0};pilot.body.chassisYaw=0;
        pilot.input={};pilot.inputAge=0;game.step(1.f/60);
        pilot.input.thrust=pilot.input.ascend=true;pilot.input.steer=1;
        float peak=pilot.body.position.y;
        for(int i=0;i<220&&waves(game.world)==0;++i){
            pilot.inputAge=0;game.step(1.f/60);
            peak=std::max(peak,pilot.body.position.y);
        }
        assert(peak>30.3f&&peak<30.51f&&pilot.body.position.x>5&&pilot.body.position.y==0&&waves(game.world)==1);
    }
    auto s=mission();auto &m=s.member(1);
    // A live destructible roof, a lower landing below it, and the ground are
    // three separate supports at one XZ address.
    auto &roof=s.world.targets.back();
    roof={};roof.origin={0,30,0};roof.center=roof.origin;roof.half={12,1,12};
    auto mesh=std::make_shared<WorldMesh>();
    for(auto p:{Vec3{-12,0,-12},Vec3{12,0,-12},Vec3{12,0,12},
                Vec3{-12,0,-12},Vec3{12,0,12},Vec3{-12,0,12}})mesh->push_back({p,{1,1,1}});
    roof.mesh=mesh;roof.health=20;
    s.world.interiorDecks.push_back({{-20,-20,20,20},{-20,12,0},{20,12,0}});
    m.body.position.y=30;
    s.step(.05f);assert(std::abs(m.body.position.y-30)<.001f&&waves(s.world)==0);
    roof.health=0;s.step(.05f);assert(m.body.position.y<30&&m.body.position.y>29);
    for(int i=0;i<90&&waves(s.world)==0;++i)s.step(.025f);
    assert(m.body.position.y==12&&waves(s.world)==1&&m.body.rpgLanding>0);
    Session peer;peer.readSnapshot(s.snapshot());
    assert(peer.member(1).body.position.y==12&&waves(peer.world)==1);
    for(int i=0;i<30;++i)s.step(.05f);
    assert(waves(s.world)==0&&m.body.position.y==12);
    s.world.position=m.body.position;
    for(int i=0;i<25;++i)s.world.movePilot(s.world.position+Vec3{1,0,0});
    assert(s.world.position.x>20&&s.world.position.y==12);
    m.body.position=s.world.position;s.step(.1f);
    assert(m.body.position.y<12&&m.body.position.y>11.5f);
    s.biome=1;const float height=m.body.position.y;
    for(int i=0;i<16;++i)s.step(.1f);
    assert(m.body.position.y==height&&waves(s.world)==0);
    // Disabled ground mecha still fall, and peers see their airborne position.
    auto npc=mission();
    auto &actor=npc.world.targets.front();
    auto &enemy=npc.enemies.front();
    for(size_t id=0;id<enemyCatalog().size();++id){
        const auto &spec=enemySpec(id);
        if(spec.kind==EnemyKind::SmallMecha&&!(spec.traits&(Flying|Hover))){
            enemy.spec=int(id);break;
        }
    }
    actor.active=true;actor.ai.airborne=false;
    actor.origin={40,25+actor.ai.localHalf.y-actor.ai.localCenter.y,0};
    const float start=actor.origin.y;
    npc.step(.05f);
    assert(actor.ai.fallSpeed>0&&actor.origin.y<start&&actor.origin.y>start-1);
    Session witness;witness.readSnapshot(npc.snapshot());
    assert(std::abs(witness.world.targets.front().origin.y-actor.origin.y)<.01f);
    for(int i=0;i<120&&waves(npc.world)==0;++i)npc.step(.025f);
    assert(waves(npc.world)==1&&actor.ai.fallSpeed==0);
    std::cout<<"Pilot ledge departure, destroyed roof, deck landing, replicated impact, submerged sinking and zero G verified\n";
}
