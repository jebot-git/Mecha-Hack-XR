#pragma once

// Render the two ends of the working stroke from the same camera and pose.
// The hand and equipment names come from a deployed session. Only the phase is
// overridden so image differences isolate weapon motion and material changes.
template<class Capture>
void captureWeaponWorkings(iron::Renderer& renderer, Capture& capture) {
    using namespace iron;
    using namespace iron::rpg;
    Session session(4271);
    session.add(1,"Weapons inspection");
    Dice creation{4271};
    auto& pilot=session.member(1);
    pilot.sheet=createSheet(Chassis::Brawler,PilotKind::Engineer,creation);
    finishCreation(pilot.sheet);
    GameSettings settings;
    settings.opener=5;
    settings.enemyCount=1;
    session.command(1,{{"op","settings"},{"settings",settings}});
    session.command(1,{{"op","ready"},{"value",true}});
    session.command(1,{{"op","deploy"}});
    equipGear(pilot.sheet,LeftHand,Gear::Empty);
    equipGear(pilot.sheet,RightHand,Gear::Empty);
    Input input;
    for(int hand=0;hand<2;++hand) {
        input.handValid[hand]=true;
        input.handPoses[hand].position=input.hands[hand]={hand?.40f:-.40f,-.13f,-.75f};
    }
    constexpr EnvironmentSettings day{Lighting::Day,Weather::Clear,7};
    for(auto family:weaponFamilies())for(auto rarity:{Rarity::Common,Rarity::Uncommon,Rarity::Rare}) {
        const auto gear=weaponVariant(family,rarity);
        const std::string id=gearInfo(gear).id;
        if(!availableEquipment(pilot.sheet,gear))recoverWeapon(pilot.sheet,gear,{});
        equipGear(pilot.sheet,RightHand,gear);
        session.command(1,{{"op","input"},{"epoch",session.epoch},{"input",coop::inputWire(input)}});
        session.step(1.f/60);
        auto deployed=session.view(1,renderer.armRig());
        const auto& arm=deployed.arms[RightHand];
        if(!arm.tracked||deployed.rpgModelNames[RightHand]!=gearVisualId(gear))
            throw std::runtime_error("Missing equipped weapon at the pilot wrist: "+id);
        if(rarity==Rarity::Common)
            capture(deployed,"seat-"+id,deployed.position,{0,-.10f,-1},true,day);
        World stage;
        stage.rpgPrototype=true;
        stage.rpgChassis=-1;
        stage.mapPilots.push_back(static_cast<const Pilot&>(deployed));
        auto& seen=stage.mapPilots.back();
        const float front=melee(gear)?-weaponTuning(gear)->muzzle.z+gearInfo(gear).range:-weaponTuning(gear)->muzzle.z;
        const auto focus=arm.wrist+rotate(arm.aim,{0,.04f,-front*.43f});
        const auto camera=focus+rotate(arm.aim,{front*.78f+1.25f,front*.25f+.38f,-front*.36f});
        const auto direction=unit(focus-camera);
        for(bool active:{false,true}) {
            seen.rpgWeaponPhase[RightHand]=active?8.f/24.f:0.f;
            capture(stage,(active?"action-":"ready-")+id,camera,direction,false,day);
        }
    }
}
