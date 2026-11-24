#pragma once

// Use deployed session views so the gallery exercises the regular per-pilot
// arm rig, mount selection, aim-mask and chassis lookup used by the client.
template<class Capture>
void captureCockpitInteriors(iron::Renderer& renderer, const std::filesystem::path& assets, Capture& capture) {
    using namespace iron;
    using namespace iron::rpg;
    Session game(4271);
    game.add(1,"Cockpit review");
    GameSettings settings;
    settings.opener=5;
    settings.enemyCount=1;
    game.command(1,{{"op","settings"},{"settings",settings}});
    game.command(1,{{"op","ready"},{"value",true}});
    game.command(1,{{"op","deploy"}});
    auto& pilot=game.member(1);
    Input input;
    for(int hand=0;hand<2;++hand){
        const float side=hand?1.f:-1.f;
        input.handValid[hand]=true;
        input.hands[hand]=input.handPoses[hand].position={side*.40f,-.13f,-.75f};
        input.handPoses[hand].aim=input.handPoses[hand].grip=angles(-side*.11f,.01f);
    }
    auto advance=[&]{
        game.command(1,{{"op","input"},{"epoch",game.epoch},{"input",coop::inputWire(input)}});
        game.step(1.f/60);
    };
    constexpr EnvironmentSettings day{Lighting::Day,Weather::Clear,7};
    for(size_t i=0;i<chassisIds.size();++i){
        const std::string id=chassisIds[i];
        if(!std::filesystem::is_regular_file(assets/"cockpit"/"interiors"/(id+".mesh")))
            throw std::runtime_error("Missing playable interior: "+id);
        pilot.body.chassisYaw=pilot.body.torsoYaw=0;
        pilot.sheet.chassis=Chassis(i);
        pilot.sheet.heavyOwners={-1,-1,-1};
        pilot.sheet.equipment.fill(Gear::Empty);
        pilot.sheet.equipment[LeftHip]=Gear::ChestBeam;
        advance();
        auto world=game.view(1,renderer.armRig());
        capture(world,"seat-"+id,world.position,{0,-.06f,-1},true,day,0);
        capture(world,"lean-"+id,world.position,{0,-.51f,-1},true,day,.04f,{.12f,-.10f,-.24f});
        auto changed=world;
        changed.rpgReadouts={"RDY","12","LINK","ON","3","LOST"};
        changed.rpgHeat=.9f;
        changed.health=45;
        capture(changed,"panels-"+id,world.position,{0,-.51f,-1},true,day,.04f,{.12f,-.10f,-.24f});
        pilot.sheet.equipment[LeftHand]=Gear::Blade;
        pilot.sheet.equipment[RightHand]=Gear::Rifle;
        advance();
        world=game.view(1,renderer.armRig());
        if(world.rpgAimMask!=7 || !world.arms[0].tracked || !world.arms[1].tracked)
            throw std::runtime_error("Armed cockpit preview lost tracking or mount selection: "+id);
        capture(world,"armed-"+id,world.position,{0,-.06f,-1},true,day,0);
        auto noAim=world;
        noAim.rpgAimMask=0;
        capture(noAim,"no-aim-"+id,world.position,{0,-.06f,-1},true,day,0);
        world.lifeSpent=true;
        world.ejectionTime=1.65f;
        capture(world,"hatch-"+id,world.position,{0,-.06f,-1},true,day,0);
    }
}
