#include "rpg/session.hpp"
#include "cockpit_effects.hpp"
#include <cassert>
#include <bit>
#include <fstream>
#include <iostream>
using namespace iron;
using namespace iron::rpg;
using namespace avp;
static Session fixture(uint32_t seed=723,bool solo=false){
    Session game(seed);game.add(1,"Injured pilot");if(!solo)game.add(2,"Engineer");
    GameSettings settings;settings.maps=1;settings.opener=5;settings.enemy=3;settings.enemyCount=1;
    settings.location=0;settings.environment=3;settings.objective=0;
    game.command(1,{{"op","settings"},{"settings",settings}});
    game.command(1,{{"op","ready"},{"value",true}});if(!solo)game.command(2,{{"op","ready"},{"value",true}});
    game.command(1,{{"op","deploy"}});game.enemies[0].disabled=10000;
    game.world.proceduralTerrain=false;game.world.editGeometry().terrain.clear();game.world.indexTerrain();
    game.world.waterRegions.clear();game.world.interiorRegions.clear();
    for(auto &t:game.world.targets)t.active=false;
    game.member(1).body.position={};if(!solo)game.member(2).body.position={300,0,300};
    return game;
}
static void step(Session &game,const Input &input,int count=1){
    for(int n=0;n<count;++n){
        game.command(1,{{"op","input"},{"epoch",game.epoch},{"input",coop::inputWire(input)}});
        game.step(1.f/60);
    }
}
static void incoming(Session &game,int damage){
    Shot shot;shot.body.position={0,6,1};shot.body.velocity={0,0,-100};shot.body.damage=damage;
    game.ordnance.push_back(shot);game.step(.05f);
}
static void drive(Session &game,unsigned id,Input input,int ticks){
    for(int i=0;i<ticks;++i){
        game.command(id,{{"op","input"},{"epoch",game.epoch},{"input",coop::inputWire(input)}});
        game.step(.1f);
    }
}
static void hullEmergencies(){
    auto solo=fixture(723,true);
    auto &pilot=solo.member(1);pilot.sheet.ap=0;pilot.sheet.hp=pilot.sheet.maxHp=20;
    pilot.sheet.equipment.fill(Gear::Empty);pilot.sheet.equipment[RightHand]=Gear::Rifle;
    incoming(solo,15);
    assert(pilot.sheet.hp==5&&pilot.criticalFaultRolled&&std::popcount(pilot.injuries)==1);
    const auto originalFault=pilot.injuries;
    pilot.sheet.hp=6;incoming(solo,1);assert(pilot.injuries==originalFault);
    // Isolate the last-wind firing input from the previous test's damaged arm.
    pilot.injuries=pilot.missingArms=0;
    incoming(solo,100);
    assert(pilot.sheet.hp==0 && pilot.emergency==HullEmergency::LastWind && !pilot.ejected);
    assert(solo.view(1).rpgPilotStatus.find("EJECTION IN 10 S")!=std::string::npos);
    Session peer;peer.readSnapshot(solo.snapshot());assert(peer.view(1).rpgPilotStatus==solo.view(1).rpgPilotStatus);
    auto timeout=solo;
    for(int i=0;i<99;++i)timeout.step(.1f);
    assert(!timeout.member(1).ejected&&timeout.phase==Phase::Combat);
    timeout.step(.1f);
    assert(timeout.member(1).ejected&&timeout.phase==Phase::Combat&&timeout.view(1).missionStatus==MissionStatus::Ejecting);
    for(int i=0;i<71;++i)timeout.step(.1f);
    assert(timeout.phase==Phase::Defeat&&timeout.view(1).missionStatus==MissionStatus::Failed);
    Input idle;idle.handValid.fill(true);drive(solo,1,idle,1);
    auto &actor=solo.world.targets[0];auto &enemy=solo.enemies[0];
    auto &arm=pilot.body.arms[RightHand];assert(arm.tracked);
    actor.active=true;actor.escaped=false;actor.invulnerable=false;actor.origin=arm.muzzle+arm.forward*8.f;
    actor.center=actor.origin;actor.half={3,3,3};actor.health=1;actor.rpgDeathAt=-1;
    enemy.hp=1;enemy.cover=false;enemy.disabled=10000;
    auto uncredited=solo;
    uncredited.enemies[0].corrosion=.01f;uncredited.enemies[0].corrosionOwner=0;
    uncredited.step(.05f);
    assert(uncredited.enemies[0].hp==0&&uncredited.member(1).emergency==HullEmergency::LastWind);
    auto credited=solo;
    credited.enemies[0].corrosion=.01f;credited.enemies[0].corrosionOwner=1;
    credited.step(.05f);
    assert(credited.enemies[0].hp==0&&credited.member(1).sheet.hp==5);
    Input shoot=idle;shoot.fire[RightHand]=true;drive(solo,1,shoot,4);
    assert(pilot.body.shots[RightHand]>0&&enemy.hp==0);
    assert(pilot.sheet.hp==5&&pilot.emergency==HullEmergency::None&&pilot.injuries&&!pilot.ejected);

    auto coop=fixture();auto &down=coop.member(1);auto &medic=coop.member(2);
    down.sheet.ap=0;incoming(coop,100);auto fault=down.injuries;
    assert(down.emergency==HullEmergency::AwaitRepair&&fault);
    auto noRescue=coop;
    for(int i=0;i<599;++i)noRescue.step(.1f);
    assert(!noRescue.member(1).ejected);noRescue.step(.1f);
    assert(noRescue.member(1).ejected&&noRescue.member(1).sheet.hp==0&&noRescue.phase==Phase::Combat);
    medic.body.position={0,0,8};medic.body.chassisYaw=medic.body.torsoYaw=0;
    medic.sheet.equipment.fill(Gear::Empty);medic.sheet.equipment[LeftShoulder]=Gear::Repair;
    medic.sheet.inventory[size_t(Gear::Repair)]=0;
    Input hold;hold.fire[LeftShoulder]=true;
    drive(coop,2,{},1);drive(coop,2,hold,3);assert(!medic.repairHold.target);
    medic.sheet.inventory[size_t(Gear::Repair)]=medic.sheet.repairKits=2;
    drive(coop,2,hold,55);
    assert(medic.repairHold.target==1&&down.sheet.hp==0&&medic.sheet.repairKits==2);
    Session remote;remote.readSnapshot(coop.snapshot());assert(remote.view(2).rpgPilotStatus==coop.view(2).rpgPilotStatus);
    drive(coop,2,{},1);assert(!medic.repairHold.target);
    drive(coop,2,hold,5);
    auto obstacle=coop;
    auto &wall=obstacle.world.targets.back();wall.active=true;wall.health=100;wall.spawnType=0;
    wall.center={0,6,4};wall.half={2,4,1};drive(obstacle,2,hold,1);
    assert(!obstacle.member(2).repairHold.target&&obstacle.member(2).sheet.repairKits==2);
    auto wrongAim=coop;
    wrongAim.member(2).body.chassisYaw=3.14f;drive(wrongAim,2,hold,1);
    assert(!wrongAim.member(2).repairHold.target);
    drive(coop,2,hold,50);medic.body.position.z=25;drive(coop,2,hold,1);assert(!medic.repairHold.target);
    medic.body.position.z=8;drive(coop,2,{},1);drive(coop,2,hold,55);
    for(int i=0;i<4;++i)coop.step(.1f);
    assert(!medic.repairHold.target&&down.sheet.hp==0);
    drive(coop,2,{},1);drive(coop,2,hold,100);
    assert(down.sheet.hp==0&&medic.sheet.repairKits==2);
    drive(coop,2,hold,1);
    assert(down.sheet.hp>=2&&down.sheet.hp<=5&&down.injuries==fault&&!down.ejected);
    assert(down.emergency==HullEmergency::None&&!medic.repairHold.target&&medic.sheet.repairKits==1);
    drive(coop,2,hold,110);assert(medic.sheet.repairKits==1);
}
int main(){
    hullEmergencies();
    // The tabletop disadvantages remain available to rule checks, alongside
    // their FPS adaptations. An advantage cancels a disadvantage.
    Dice creation{4};auto sheet=createSheet(Chassis::Striker,PilotKind::Engineer,creation);
    for(auto flag:{MechaInjury::Sensors,MechaInjury::Hobbled,MechaInjury::Targeting,MechaInjury::Severe}){
        for(int attr=0;attr<4;++attr)for(bool attack:{false,true}){
            Dice actual{91},expected=actual;bool penalty=injuryDisadvantage(uint32_t(flag),attr,attack);
            auto want=test(expected,sheet.attributes[attr],0,penalty?-1:0);
            auto got=test(actual,sheet,attr,uint32_t(flag),attack);
            assert(got.natural==want.natural&&actual.state==expected.state);
            actual={91};expected=actual;
            want=test(expected,sheet.attributes[attr],0,penalty?0:1);
            got=test(actual,sheet,attr,uint32_t(flag),attack,0,1);
            assert(got.natural==want.natural&&actual.state==expected.state);
        }
    }
    auto normal=fixture(),hobbled=normal;
    hobbled.member(1).injuries=uint32_t(MechaInjury::Hobbled);
    Input walk;walk.forward=1;step(normal,walk,60);step(hobbled,walk,60);
    assert(std::abs(length(hobbled.member(1).body.position)/length(normal.member(1).body.position)-.5f)<.002f);
    for(int lost=0;lost<2;++lost){
        auto game=fixture();auto &m=game.member(1);m.injuries=uint32_t(MechaInjury::LostArm);m.missingArms=1u<<lost;
        m.sheet.equipment.fill(Gear::Empty);m.sheet.equipment[lost]=Gear::Rifle;m.sheet.equipment[1-lost]=Gear::Blade;
        Input input;input.fire[lost]=true;input.punch[lost]=true;input.handValid.fill(true);
        step(game,input,10);assert(!m.body.arms[lost].tracked&&m.body.arms[1-lost].tracked);
        assert(m.body.shots[lost]==0&&m.body.punches[lost]==0);
        auto local=game.view(1),remote=game.view(2);
        assert(!local.armAvailable(lost)&&local.rpgModelNames[lost].empty()&&local.rpgReadouts[lost]=="LOST");
        assert(!remote.mapPilots[0].armAvailable(lost));
        m.sheet.equipment.fill(Gear::Empty);m.sheet.equipment[1-lost]=Gear::Cannon;
        input={};input.fire[1-lost]=true;step(game,input,10);assert(m.body.shots[1-lost]==0);
        auto snapshot=game.snapshot();Session client;client.readSnapshot(snapshot);
        assert(client.member(1).missingArms==m.missingArms&&!client.view(1).armAvailable(lost));
    }
    // A repaired severe injury stays visibly severe; HP remains authoritative.
    Pilot p;p.health=100;p.rpgInjuries=uint32_t(MechaInjury::Severe);CockpitEffects fx;fx.update(p,6,true);
    assert(fx.armor==6&&fx.fractureStage()==3&&p.health==100);
    for(float power:fx.power)assert(power<=.42f);
    p.health=50;fx.update(p,1,true);p.health=100;fx.update(p,1,true);assert(fx.fractureStage()==3);
    p.rpgInjuries=0;fx.update(p,1,true);assert(fx.fractureStage()==0);
    p.walkingSpeed=2;
    auto heel=[&](float phase,bool hurt){p.gaitPhase=phase;p.rpgInjuries=hurt?uint32_t(MechaInjury::Hobbled):0;CockpitEffects pose;pose.update(p,6,true);return pose.seat[13];};
    assert(std::abs(heel(3,true)-heel(3,false))>.1f);
    assert(std::abs(heel(12,true)-heel(12,false))<.04f);
    auto game=fixture();auto &m=game.member(1);m.injuries=uint32_t(MechaInjury::Sensors)|uint32_t(MechaInjury::Targeting)|uint32_t(MechaInjury::Severe);
    auto view=game.view(1);assert(!view.sensorsWorking()&&!view.targetingWorking());
    fx.update(view,6,true);assert(fx.power[1]==0);
    // Destroying body hardpoints removes their weapons and quality metadata;
    // passive capacity loss clamps resources without healing or refilling.
    sheet.equipment.fill(Gear::Empty);sheet.qualities.fill(Quality::None);
    sheet.equipment[RightHand]=Gear::Rifle;sheet.qualities[RightHand]=Quality::Sniper;
    sheet.hp=1;sheet.ap=0;destroyModule(sheet,Module::ShoulderHardpoint);
    assert(!sheet.has(Module::ShoulderHardpoint)&&sheet.equipment[RightHand]==Gear::Rifle&&sheet.qualities[RightHand]==Quality::Sniper);
    assert(sheet.hp==1&&sheet.ap==0);validate(sheet);
    sheet.modules={Module::SuperReactor};prepare(sheet);int full=sheet.maxReactor;sheet.ap=0;
    destroyModule(sheet,Module::SuperReactor);assert(sheet.maxReactor<full&&sheet.reactor==sheet.maxReactor&&sheet.ap==0);
    // Co-op disables immediately with one of seven faults. Ejection is time based.
    unsigned outcomes=0;uint32_t moduleSeed=0;
    for(uint32_t seed=1;seed<=128&&outcomes!=127;++seed){
        auto disabled=fixture(seed);auto &victim=disabled.member(1);victim.sheet.hp=1;victim.sheet.ap=0;
        victim.sheet.modules.push_back(Module::NanoShroud);
        auto before= victim.sheet.modules;Shot shot;shot.body.position={0,6,1};shot.body.velocity={0,0,-100};shot.body.damage=100;
        disabled.ordnance.push_back(shot);disabled.step(.05f);
        assert(victim.sheet.hp==0&&std::popcount(victim.injuries)==1);outcomes|=victim.injuries;
        if(hasInjury(victim.injuries,MechaInjury::LostArm))assert(victim.missingArms==1||victim.missingArms==2);
        if(hasInjury(victim.injuries,MechaInjury::ModuleDestroyed)){
            moduleSeed=seed;
            assert(victim.sheet.modules.size()+1==before.size()&&!victim.sheet.has(before.back()));validate(victim.sheet);
        }
        assert(!victim.ejected&&victim.emergency==HullEmergency::AwaitRepair);
        {
            auto flags=victim.injuries,missing=victim.missingArms;victim.sheet.hp=1;victim.emergency=HullEmergency::None;
            step(disabled,Input{});assert(victim.injuries==flags);
            disabled.phase=Phase::Complete;disabled.command(1,{{"op","next"}});
            assert(disabled.member(1).injuries==flags&&disabled.member(1).missingArms==missing);
            disabled.command(1,{{"op","hangar"}});
            assert(disabled.member(1).injuries==0&&disabled.member(1).missingArms==0);
            if(hasInjury(flags,MechaInjury::ModuleDestroyed))assert(!disabled.member(1).sheet.has(before.back()));
        }
    }
    assert(outcomes==127&&moduleSeed);
    for(int index=0;index<int(Module::Count);++index){
        auto module=Module(index);
        if(retiredModule(module))continue;
        auto disabled=fixture(moduleSeed);auto &member=disabled.member(1);
        member.sheet.equipment.fill(Gear::Empty);member.sheet.qualities.fill(Quality::None);member.sheet.modules={module};
        member.sheet.equipment[LeftShoulder]=moduleGear(module);
        if(module==Module::ShoulderHardpoint){member.sheet.equipment[RightHand]=Gear::Rifle;member.sheet.qualities[RightHand]=Quality::Sniper;}
        prepare(member.sheet);member.sheet.hp=1;member.sheet.ap=0;
        member.siege=member.barricade=member.vehicle=member.mine=member.turret=true;member.boost=5;
        member.sheet.targetLock=true;member.sheet.hidden=true;member.hiddenRemaining=5;
        for(unsigned slot=0;slot<2;++slot){auto &target=disabled.world.targets[disabled.world.targets.size()-8+slot];target.active=true;target.health=8;target.origin={200,0,200};disabled.world.updateEnemyBounds(target);}
        Shot incoming;incoming.body.position={0,6,1};incoming.body.velocity={0,0,-100};incoming.body.damage=100;
        disabled.ordnance.push_back(incoming);disabled.step(.05f);
        if(frameUpgrade(module)){assert(member.sheet.has(module));continue;}
        assert(hasInjury(member.injuries,MechaInjury::ModuleDestroyed)&&!member.sheet.has(module));
        assert(member.sheet.hp==0&&member.sheet.ap==0);validate(member.sheet);
        if(module==Module::SiegeMode)assert(!member.siege);
        if(module==Module::BarricadeMode)assert(!member.barricade);
        if(module==Module::VehicleMode)assert(!member.vehicle);
        if(module==Module::BoosterSprint)assert(member.boost==0);
        if(module==Module::FragMines)assert(!member.mine);
        if(module==Module::TargetingArray)assert(!member.sheet.targetLock);
        if(module==Module::StaticCloud)assert(!member.sheet.hidden&&member.hiddenRemaining==0);
        if(module==Module::SentryTurret)assert(!member.turret&&!disabled.world.targets[disabled.world.targets.size()-7].active);
        if(module==Module::ParticleBarrier)assert(!disabled.world.targets[disabled.world.targets.size()-8].active);
        Session restored;restored.readSnapshot(disabled.snapshot());assert(restored.member(1).sheet.has(module)==frameUpgrade(module));
    }

    std::cout<<"Hull emergencies, nanite rescue, stat penalties, limb authority/replication, persistent damage, gait and module cleanup passed\n";
}
