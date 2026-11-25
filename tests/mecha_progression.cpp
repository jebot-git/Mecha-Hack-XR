#include "rpg/session.hpp"
#include "rpg/profiles.hpp"
#include "rpg/preparation.hpp"
#include "rpg/original_assets.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
using namespace iron;
using namespace iron::rpg;
static Session fixture(int maps=1){
    Session s(713);s.add(1,"Atlas");s.add(2,"Beacon");
    for(auto &m:s.members)finishCreation(m.sheet);
    GameSettings settings;settings.maps=1;settings.maps=maps;settings.enemy=3;settings.enemyCount=1;
    settings.objective=0;settings.opener=5;settings.environment=3;settings.location=0;
    s.command(1,{{"op","settings"},{"settings",settings}});
    s.command(1,{{"op","ready"},{"value",true}});s.command(2,{{"op","ready"},{"value",true}});s.command(1,{{"op","deploy"}});
    s.enemies[0].disabled=10000;s.world.proceduralTerrain=false;s.world.editGeometry().terrain.clear();s.world.indexTerrain();s.world.interiorRegions.clear();s.world.waterRegions.clear();
    for(auto &t:s.world.targets)t.active=false;
    s.member(1).body.position={};s.member(2).body.position={300,0,300};
    Input idle;s.command(1,{{"op","input"},{"epoch",s.epoch},{"input",coop::inputWire(idle)},{"systems",idle.ping}});s.step(1.f/60);return s;
}
static void input(Session &s,const Input &in,float dt=1.f/60){s.command(1,{{"op","input"},{"epoch",s.epoch},{"input",coop::inputWire(in)},{"systems",in.ping}});s.step(dt);}
static void win(Session &s){for(auto &e:s.enemies)e.hp=0;s.step(.05f);}
template<class F>static bool rejects(F f){try{f();return false;}catch(const std::exception &){return true;}}
int main(){
    for(int c=0;c<4;++c){Dice d{uint32_t(c+1)};auto s=createSheet(Chassis(c),PilotKind::Engineer,d);
        assert(s.level==1&&s.modules.empty()&&s.moduleChoices==0&&s.maxHp>=14&&s.maxHp<=18);
        for(int g=0;g<int(Gear::Count);++g)if(consumable(Gear(g)))assert(s.inventory[g]==int(Gear(g)==Gear::Repair));
        assert(rejects([&]{equipGear(s,LeftHip,Gear::Comlink);}));finishCreation(s);validate(s);
    }
    Dice d{19};auto market=createSheet(Chassis::Striker,PilotKind::Engineer,d);
    startingEquipment(market,true);assert(market.credits==120&&market.armor==Armor::None);
    purchase(market,Gear::Rifle);purchaseArmor(market,Armor::Light);purchase(market,Gear::Blade);
    assert(market.credits==20);equipGear(market,LeftHand,Gear::Rifle);equipGear(market,RightHand,Gear::Blade);finishCreation(market);
    auto unchanged=Json(market);assert(rejects([&]{purchase(market,Gear::Cannon);}));assert(Json(market)==unchanged);
    auto heavy=createSheet(Chassis::Titan,PilotKind::Engineer,d);finishCreation(heavy);
    assert(heavy.equipment[0]==Gear::Cannon&&heavy.equipment[1]==Gear::Cannon&&equipmentOwner(heavy,1)==0);
    equipGear(heavy,RightHand,Gear::Cannon);assert(equipmentOwner(heavy,0)==1);
    equipGear(heavy,LeftHand,Gear::Empty);assert(heavy.equipment[0]==Gear::Empty&&heavy.equipment[1]==Gear::Empty);
    heavy.modules={Module::ShoulderHardpoint};assert(rejects([&]{equipGear(heavy,RightShoulder,Gear::Cannon);}));
    assert(rejects([&]{equipGear(heavy,LeftHip,Gear::Cannon);}));
    auto directory=std::filesystem::temp_directory_path()/("mecha-progression-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{std::filesystem::path path;~Cleanup(){std::filesystem::remove_all(path);}}cleanup{directory};
    std::set<WeaponRoll> rolls;
    Dice tuningRolls{918};
    for(int i=0;i<512;++i)rolls.insert(rollWeaponStats(tuningRolls));
    assert(rolls.size()==27);
    auto fitted=market;
    const auto rifle=Gear::Rifle;
    recoverWeapon(fitted,rifle,{-1,1,-1});
    recoverWeapon(fitted,rifle,{1,-1,1});
    equipGear(fitted,LeftHand,rifle,1);
    equipGear(fitted,RightHand,rifle,2);
    assert(rejects([&]{equipGear(fitted,RightHand,rifle,1);}));
    const auto plain=weaponStats(fitted,rifle,0),slower=weaponStats(fitted,rifle,1),faster=weaponStats(fitted,rifle,2);
    assert(slower.cooldown>plain.cooldown&&slower.heat<plain.heat&&slower.spreadFactor>plain.spreadFactor);
    assert(faster.cooldown<plain.cooldown&&faster.heat>plain.heat&&faster.spreadFactor<plain.spreadFactor);
    fitted.inventory[size_t(Gear::Scrap)]=10;
    tuneWeapon(fitted,rifle,2,WeaponStat::Cycle);
    tuneWeapon(fitted,rifle,2,WeaponStat::Cycle);
    assert(std::abs(weaponStats(fitted,rifle,2).cooldown-plain.cooldown*.91f)<.0001f&&
           fitted.inventory[size_t(Gear::Scrap)]==4&&weaponTuneCount(weaponCopy(fitted,rifle,1))==0);
    const Json tuned=fitted;
    assert(rejects([&]{tuneWeapon(fitted,rifle,2,WeaponStat::Heat);}));
    assert(rejects([&]{tuneWeapon(fitted,rifle,0,WeaponStat::Count);}));
    assert(Json(fitted)==tuned);
    assert(rejects([&]{sellEquipment(fitted,rifle,2);}));
    sellEquipment(fitted,rifle,0);
    assert(equippedWeaponCopy(fitted,LeftHand)==0&&equippedWeaponCopy(fitted,RightHand)==1);
    assert(weaponTuneCount(weaponCopy(fitted,rifle,1))==2);
    saveProfile(directory/"tuned.json",fitted,"Workshop");
    const auto restored=loadProfile(directory/"tuned.json");
    assert(weaponTuneCount(weaponCopy(restored,rifle,1))==2&&equippedWeaponCopy(restored,RightHand)==1);
    auto invalid=restored;invalid.weaponCopies.back().tuning={1,1,1};
    assert(rejects([&]{validate(invalid);}));
    Session shop;shop.add(1,"Workshop");
    shop.command(1,{{"op","configure"},{"profile",restored}});
    const Json order={{"op","trade"},{"action","tune"},{"gear","rifle"},{"copy",0},{"stat",int(WeaponStat::Handling)}};
    shop.playerCommand(1,order);
    assert(weaponTuneCount(weaponCopy(shop.member(1).sheet,rifle,0))==1);
    assert(shop.member(1).sheet.inventory[size_t(Gear::Scrap)]==2);
    Session workshopReplica;workshopReplica.readSnapshot(shop.snapshot());
    assert(Json(workshopReplica.member(1).sheet)==Json(shop.member(1).sheet));
    auto fieldWorkshop=fixture();
    assert(rejects([&]{fieldWorkshop.playerCommand(1,order);}));
    ProfileStore profiles(directory);
    for(int slot=0;slot<4;++slot){Dice creation{uint32_t(slot+41)};auto s=createSheet(Chassis(slot),PilotKind(slot),creation);finishCreation(s);s.missionsCompleted=slot;profiles.save(slot,s,"Pilot "+std::to_string(slot+1));}
    auto first=profiles.load(0)->sheet;first.missionsCompleted=9;profiles.save(0,first,"Atlas");profiles.select(3);
    ProfileStore reopened(directory);assert(reopened.active()==3&&reopened.load(0)->sheet.missionsCompleted==9);
    for(int slot=1;slot<4;++slot)assert(reopened.load(slot)->sheet.missionsCompleted==slot&&reopened.load(slot)->sheet.level==1);
    assert(rejects([&]{profiles.load(4);}));
    // Schema-two imports retain their level/stock and leave the source intact.
    auto legacy=createSheet(Chassis::Titan,PilotKind::Engineer,d,3);
    legacy.equipment.fill(Gear::Empty);legacy.equipment[RightHand]=Gear::Cannon;legacy.equipment[RightHip]=Gear::Comlink;
    legacy.inventory[size_t(Gear::Repair)]=7;legacy.modules={Module::RetractableShield};
    Json oldSheet=legacy;
    for(auto key:{"created","owned","ownedArmor","heavyOwners","oneShotStock","moduleChoices","creationSeed","creationSwap","starterModule","lastCampaignAward"})oldSheet.erase(key);
    auto legacyRoot=directory/"legacy";std::filesystem::create_directories(legacyRoot);
    auto oldText=Json{{"schema",2},{"callsign","Veteran"},{"sheet",oldSheet}}.dump();
    {std::ofstream file(legacyRoot/"default.json");file<<oldText;}
    ProfileStore migrated(legacyRoot);auto imported=migrated.load(0)->sheet;
    assert(imported.created&&imported.level==1&&imported.inventory[size_t(Gear::Repair)]==7&&imported.owned[size_t(Gear::Cannon)]==1);
    assert(imported.equipment[LeftHand]==Gear::Cannon&&imported.equipment[RightHip]==Gear::Empty);
    assert(imported.has(Module::RetractableShield)&&imported.starterModule==Module::Count);
    {std::ifstream file(legacyRoot/"default.json");std::string text((std::istreambuf_iterator<char>(file)),{});assert(text==oldText);}
    for(auto field:{"armor","oneShotModule"}){
        Json broken=Json::parse(oldText);broken["sheet"][field]=999;
        {std::ofstream file(legacyRoot/"invalid.json");file<<broken;}
        assert(rejects([&]{loadProfile(legacyRoot/"invalid.json");}));
    }
    // Reloading creation cannot stack swaps or reroll trained attributes.
    Preparation draft;draft.sheet=createSheet(Chassis::Scout,PilotKind::Commander,d);auto original=draft.sheet.attributes;
    draft.selected=1;draft.edit(1);saveProfile(directory/"draft.json",draft.sheet,"Draft");
    Preparation resumed;resumed.sheet=loadProfile(directory/"draft.json");auto attributes=resumed.sheet.attributes;
    resumed.selected=0;resumed.edit(1);assert(resumed.sheet.attributes!=attributes&&resumed.sheet.attributes[1]<attributes[1]);
    // Real two-map objective, group gate and one atomic short rest.
    auto game=fixture(2);auto epoch=game.epoch;auto &a=game.member(1);a.sheet.hp=1;a.sheet.ap=0;a.sheet.reactor=4;a.injuries=uint32_t(MechaInjury::Hobbled);a.sheet.inventory[size_t(Gear::Repair)]=3;
    a.pendingRecovery={{Gear::RifleRare,{-1,1,0}}};a.fieldAmmo=true;
    win(game);assert(game.phase==Phase::Combat&&game.mission.exitOpen&&game.member(1).sheet.pendingLevels==0);
    Vec3 exit{game.mission.exit[0],game.mission.exit[1],game.mission.exit[2]};game.member(1).body.position=exit;
    for(int n=0;n<30;++n)game.step(.1f);assert(game.epoch==epoch);
    game.member(2).body.position=exit+Vec3{12,0,0};
    for(int n=0;n<22&&game.epoch==epoch;++n)game.step(.1f);
    assert(game.epoch==epoch+1&&game.phase==Phase::Briefing&&game.mission.mapIndex==1);
    assert(game.member(1).sheet.hp>1&&game.member(1).sheet.ap==game.member(1).sheet.maxAp&&game.member(1).sheet.reactor==game.member(1).sheet.maxReactor);
    assert(game.member(1).injuries==uint32_t(MechaInjury::Hobbled)&&game.member(1).sheet.inventory[size_t(Gear::Repair)]==3);
    assert(length(game.member(1).body.position-game.member(2).body.position)>=11.9f);
    assert(game.member(1).pendingRecovery==(std::vector<RecoveredItem>{{Gear::RifleRare,{-1,1,0}}})&&game.member(1).fieldAmmo&&game.member(1).sheet.owned[size_t(Gear::RifleRare)]==0);
    game.command(1,{{"op","deploy"}});win(game);
    assert(game.phase==Phase::Complete&&game.member(1).sheet.level==1&&game.member(1).sheet.pendingLevels==0&&game.member(1).sheet.missionsCompleted==1);
    assert(game.member(1).pendingRecovery.empty()&&!game.member(1).fieldAmmo&&game.member(1).sheet.owned[size_t(Gear::RifleRare)]==1);
    assert((weaponCopy(game.member(1).sheet,Gear::RifleRare,0)->roll==WeaponRoll{-1,1,0}));
    assert(rejects([&]{game.command(1,{{"op","next"}});}));
    game.command(1,{{"op","debrief"}});auto completed=Json(game.member(1).sheet);assert(game.member(1).sheet.level==1);
    game.command(1,{{"op","debrief"}});assert(Json(game.member(1).sheet)==completed);
    game.command(2,{{"op","debrief"}});game.command(1,{{"op","hangar"}});assert(!game.member(1).injuries);
    // Modules use credits without changing armour, attributes or hull.
    auto loadout=createSheet(Chassis::Scout,PilotKind::Engineer,d);
    const auto fixedHull=loadout.maxHp;const auto fixedAttributes=loadout.attributes;
    assert(rejects([&]{installModule(loadout,Module::NanoShroud);}));
    loadout.credits=modulePurchaseCost;
    installModule(loadout,Module::NanoShroud);
    assert(loadout.credits==0&&loadout.modules.size()==1&&loadout.maxHp==fixedHull&&loadout.attributes==fixedAttributes);
    // Hand support requires an actual nearby tracked offhand and held grip.
    auto unbraced=fixture(),braced=unbraced;
    for(auto *s:{&unbraced,&braced}){auto &m=s->member(1);m.sheet=createSheet(Chassis::Titan,PilotKind::Engineer,d);finishCreation(m.sheet);}
    Input fire;fire.fire[LeftHand]=true;input(unbraced,fire);
    fire.ping[RightHand]=true;fire.handPoses[LeftHand].position={0,-.13f,-.7f};fire.handPoses[RightHand].position={.05f,-.13f,-.85f};input(braced,fire);
    assert(!unbraced.member(1).heavySupported[0]&&braced.member(1).heavySupported[0]);
    assert(unbraced.member(1).recoil[0]>braced.member(1).recoil[0]*4);
    auto light=fixture();light.member(1).sheet.equipment[LeftHand]=Gear::Rifle;
    fire={};fire.fire[LeftHand]=true;input(light,fire);assert(!light.member(1).handSupported[0]&&light.member(1).recoil[0]<.02f);
    auto lightSupported=fixture();lightSupported.member(1).sheet.equipment[LeftHand]=Gear::Rifle;lightSupported.member(1).sheet.equipment[RightHand]=Gear::Empty;
    fire.ping[RightHand]=true;fire.handPoses[0].position={0,-.13f,-.7f};fire.handPoses[1].position={.05f,-.13f,-.85f};input(lightSupported,fire);
    assert(lightSupported.member(1).handSupported[0]&&lightSupported.member(1).recoil[0]<.02f);
    auto linked=fixture();linked.member(1).sheet=createSheet(Chassis::Titan,PilotKind::Engineer,d);fire={};fire.fire[0]=fire.fire[1]=true;input(linked,fire);
    assert(linked.member(1).body.shots[0]==1&&linked.member(1).body.shots[1]==0);
    // Structure destruction releases debris and never rolls a safe-box reward.
    auto loot=fixture();size_t index=loot.enemies.size();
    for(int n=0;n<20;++n){
        auto &t=loot.world.targets[index];t.active=true;t.processed=false;t.invulnerable=false;t.health=1;t.origin={100,0,0};applyOriginalModel(t,"building");
        Shot shot;shot.body.position={100,5,15};shot.body.velocity={0,0,-300};shot.body.damage=20;loot.ordnance.push_back(shot);loot.step(.1f);
        assert(t.health==0);
    }
    assert(loot.supplies.empty()&&loot.world.pickups.empty());
    // The consumable drone has a real movable feed and can be shot down.
    auto scout=fixture();auto &pilot=scout.member(1);pilot.sheet.inventory[size_t(Gear::FragGrenade)]=1;pilot.sheet.equipment[LeftShoulder]=Gear::FragGrenade;
    fire={};fire.fire[LeftShoulder]=true;input(scout,fire);assert(pilot.drone>0&&pilot.droneControl&&scout.view(1).rpgDroneView);
    Vec3 body=pilot.body.position,drone=pilot.dronePosition;fire={};fire.forward=1;input(scout,fire);
    assert(length(pilot.body.position-body)<.0001f&&length(pilot.dronePosition-drone)>.01f);
    Shot incoming;incoming.body.position=pilot.dronePosition+Vec3{0,0,-5};incoming.body.velocity={0,0,100};incoming.body.damage=1;scout.ordnance.push_back(incoming);scout.step(.05f);
    assert(pilot.drone==0&&!scout.view(1).rpgDroneView);
    // Each consumable spends stock and produces its intended effect.
    for(auto gear:{Gear::Repair,Gear::BoostKit,Gear::ExplosiveRounds,Gear::ArcGrenade,Gear::FragGrenade}){
        auto s=fixture();auto &m=s.member(1);m.sheet.equipment[LeftShoulder]=gear;m.sheet.inventory[size_t(gear)]=1;
        m.sheet.hp=1;m.sheet.ap=0;m.sheet.reactor=4;if(gear==Gear::BoostKit)m.sheet.reactorStress=.8f;
        s.member(2).body.position={20,0,0};fire={};fire.fire[LeftShoulder]=true;input(s,fire);
        assert(m.sheet.inventory[size_t(gear)]==0);
        if(gear==Gear::Repair)assert(m.sheet.hp>1);
        if(gear==Gear::BoostKit)assert(m.sheet.ap>0&&m.sheet.reactor==4&&m.sheet.reactorStress<.36f);
        if(gear==Gear::ExplosiveRounds){assert(m.explosive>0);fire={};for(int n=0;n<700;++n)input(s,fire);assert(m.explosive>0);}
        if(gear==Gear::ArcGrenade||gear==Gear::FragGrenade){
            assert(s.ordnance.empty()&&m.drone>0&&m.droneControl&&m.droneArc==(gear==Gear::ArcGrenade));
            fire={};input(s,fire);fire.fire[RightHand]=true;input(s,fire);
            assert(m.drone==0&&!s.view(1).rpgDroneView);
        }
    }
    // Original active modules retain their established world/state checks.
    // The13 Mission Manual modules have separate manual runtime tests.
    std::set<Module> modules;
    for(int index=0;index<int(Module::DismantlerArms);++index){
        Module module=Module(index);Gear gear=moduleGear(module);if(gear==Gear::Empty||retiredGear(gear))continue;
        auto s=fixture();auto &m=s.member(1);m.sheet=createSheet(Chassis::Striker,PilotKind::Engineer,d);
        m.sheet.modules={module};m.sheet.equipment.fill(Gear::Empty);m.sheet.heavyOwners={-1,-1,-1};m.sheet.equipment[LeftShoulder]=gear;prepare(m.sheet);m.sheet.ap=0;
        fire={};fire.fire[LeftShoulder]=true;Vec3 before=m.body.position;input(s,fire);
        switch(module){
        case Module::JumpJets:assert(length(m.body.position-before)>50);break;
        case Module::TargetingArray:assert(m.sheet.targetLock);break;
        case Module::NanoShroud:assert(m.sheet.ap>0);break;
        case Module::ChestBeam:assert(s.ordnance.size()==1&&s.ordnance[0].effect==AttackEffect::Push);break;
        case Module::MissileBarrage:assert(s.ordnance.size()==4);for(const auto &shot:s.ordnance)assert(shot.body.kind==ProjectileKind::Rocket);break;
        case Module::SiegeMode:assert(m.siege);break;
        case Module::StaticCloud:assert(m.sheet.hidden);break;
        case Module::BarricadeMode:assert(m.barricade);break;
        case Module::BoosterSprint:assert(m.boost>4);break;
        case Module::VehicleMode:assert(m.vehicle);break;
        case Module::FragMines:assert(m.mine);break;
        case Module::SentryTurret:assert(m.turret&&s.world.targets[s.world.targets.size()-7].active);break;
        case Module::ParticleBarrier:assert(s.world.targets[s.world.targets.size()-8].health==20);break;
        case Module::Grappler:case Module::QuakeGenerator:{
            // Aim a real nearby target, then verify pull/push displacement.
            fire={};for(int n=0;n<370;++n)input(s,fire);
            auto &target=s.world.targets[0];target.active=true;target.origin={-2,4,-16};s.world.updateEnemyBounds(target);
            if(module==Module::QuakeGenerator){target.origin={0,0,-6};s.world.updateEnemyBounds(target);}
            Vec3 old=target.origin;fire.fire[LeftShoulder]=true;if(module==Module::Grappler)fire.headPitch=-.45f;input(s,fire);
            if(length(target.origin-old)<=2){auto hit=s.world.traceAim(m.body.arms[0].muzzle,m.body.arms[0].forward,30);std::cerr<<moduleId(module)<<" displacement="<<length(target.origin-old)<<" trace="<<hit.target<<" health="<<s.enemies[0].hp<<" phase="<<int(s.phase)<<" cooldown="<<m.body.cooldown[LeftShoulder]<<" origin="<<m.body.arms[0].muzzle.x<<","<<m.body.arms[0].muzzle.y<<","<<m.body.arms[0].muzzle.z<<" target="<<target.center.x<<","<<target.center.y<<","<<target.center.z<<"\n";}
            assert(length(target.origin-old)>2);break;
        }
        default:assert(false);
        }
        modules.insert(module);
        if(module==Module::SiegeMode||module==Module::BarricadeMode||module==Module::VehicleMode||module==Module::FragMines||module==Module::ParticleBarrier){
            fire={};for(int n=0;n<330;++n)input(s,fire);m.sheet.overheated=false;m.sheet.ventRemaining=0;
            fire.fire[LeftShoulder]=true;input(s,fire);
            if(module==Module::SiegeMode)assert(!m.siege);
            if(module==Module::BarricadeMode)assert(!m.barricade);
            if(module==Module::VehicleMode)assert(!m.vehicle);
            if(module==Module::FragMines)assert(!m.mine);
            if(module==Module::ParticleBarrier)assert(!s.world.targets[s.world.targets.size()-8].active);
        }
    }
    for(auto module:{Module::RocketFists,Module::ShoulderHardpoint,Module::SuperReactor,Module::ExothermicPlating,Module::ReflectiveShielding,Module::RetractableShield}){
        auto s=fixture();auto &m=s.member(1);m.sheet.modules={module};prepare(m.sheet);
        if(module==Module::SuperReactor)assert(m.sheet.maxReactor==10);
        if(module==Module::RetractableShield){input(s,Input{});assert(m.sheet.maxAp==4&&m.shield==2);}
        if(module==Module::ShoulderHardpoint){assert(rejects([&]{equipGear(m.sheet,RightShoulder,Gear::Rifle);}));assert(m.sheet.has(Module::ShoulderHardpoint));}
        if(module==Module::ExothermicPlating||module==Module::ReflectiveShielding){
            auto &target=s.world.targets[0];target.origin=module==Module::ExothermicPlating?Vec3{0,0,-8}:Vec3{0,0,-40};s.world.updateEnemyBounds(target);
            int hp=s.enemies[0].hp;Shot incoming;incoming.body.position={0,6,2};incoming.body.velocity={0,0,-120};incoming.body.damage=1;incoming.body.owner=0;
            s.ordnance.push_back(incoming);s.step(.05f);assert(s.enemies[0].hp<hp);
        }
        modules.insert(module);
    }
    assert(modules.size()==16);
    auto progression=createSheet(Chassis::Scout,PilotKind::Maverick,d);
    progression.credits=modulePurchaseCost*4;
    for(int n=0;progression.modules.size()<moduleLimit;++n)if(!retiredModule(Module(n))&&!frameUpgrade(Module(n))&&!progression.has(Module(n)))installModule(progression,Module(n));
    assert(progression.modules.size()==moduleLimit&&progression.level==1);
    std::cout<<"Level-one creation, four profiles/migration, economy, paired/support weapons, group short rests, debrief, remaining core modules, consumables, structure drop removal and controllable drone passed\n";
}
