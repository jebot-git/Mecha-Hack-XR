#include "coop.hpp"
#include <iostream>
#include <stdexcept>
#include <chrono>
using namespace iron;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static World range(Loadout loadout){World w;w.targets.clear();w.configure(loadout);w.step(Input{},.01f);return w;}
int main(int argc,char** argv){try{
    check(argc==2,"retail path");
    Progression campaign;campaign.rules.load(argv[1],1);
    for(auto [mission,weapon]:std::array<std::pair<int,Weapon>,4>{{{2,Weapon::Shotgun},{6,Weapon::Homing},{7,Weapon::HeavyMachinegun},{9,Weapon::AutoGrenade}}}){
        Progression progress;progress.rules=campaign.rules;
        for(int m=0;m<mission;++m)check(progress.complete(m),"prerequisite mission completion");
        check(!progress.weaponAvailable(weapon),"new weapon remains locked before its own reward");
        check(progress.complete(mission)&&progress.weaponAvailable(weapon),"correct IS2 mission unlocks distinct weapon");
    }
    Frontend skirmish;skirmish.skirmish=true;
    for(int w=int(Weapon::Shotgun);w<int(Weapon::Count);++w)check(skirmish.weaponAvailable(Weapon(w)),"skirmish unlocks all four additions");
    Progression saved;saved.rules=campaign.rules;saved.completed=saved.rules.fullMask();
    saved.loadout={Weapon::Shotgun,Weapon::HeavyMachinegun,Weapon::Homing,Weapon::Shield,Weapon::AutoGrenade,Weapon::Empty};
    auto savePath=std::filesystem::temp_directory_path()/("iron-is2-weapons-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".json");
    saved.save(savePath,false);Progression loaded;loaded.rules=campaign.rules;loaded.load(savePath);
    check(loaded.loadout==saved.loadout&&loaded.rules.weapons(loaded.completed)==8191,"all four additions survive campaign save reload with their unlocks");
    std::filesystem::remove(savePath);std::filesystem::remove(savePath.string()+".lock");
    for(int hand=0;hand<2;++hand)for(auto weapon:{Weapon::Shotgun,Weapon::HeavyMachinegun}){
        Frontend loadout;loadout.skirmish=true;loadout.screen=Screen::Loadout;loadout.loadout.fill(Weapon::Empty);
        loadout.weapon=int(Weapon::Cutter);check(loadout.equip(1-hand),"opposite cutter fits");loadout.weapon=int(weapon);
        check(loadout.equip(hand)&&loadout.loadout[1-hand]==Weapon::Cutter,"new handheld fits either occupied-hand arrangement");
        check(!loadout.equip(hand?LeftShoulder:RightHip),"new handheld rejected on body mounts");
        World w=range(loadout.loadout);Input fire;fire.fire[hand]=true;w.step(fire,.01f);
        check(w.shots[hand]==1&&w.ammo[hand]==weaponInfo(weapon).capacity-1,"one trigger consumes one shell/round");
        check(w.projectiles.size()==(weapon==Weapon::Shotgun?8u:1u),"shotgun emits eight pellets; heavy machine gun one round");
        for(auto& shot:w.projectiles){check(shot.weapon==weapon&&shot.kind==ProjectileKind::Bullet,"new gun preserves identity without explosive splash");check(length(shot.position-(w.arms[hand].muzzle+shot.velocity*.01f))<.001f,"new bullet starts at the rendered barrel");}
        auto shots=w.shots[hand];w.step(fire,.01f);check(w.shots[hand]==shots,"new gun respects cooldown");
    }
    Loadout guided{};guided[LeftShoulder]=Weapon::Homing;World w=range(guided);Input trigger;trigger.fire[LeftShoulder]=true;
    w.step(trigger,.01f);check(w.projectiles.empty()&&w.ammo[LeftShoulder]==6,"no lock means no launch or ammunition consumption");
    Target enemy;enemy.spawnType=0;enemy.center=w.eye()+Vec3{0,0,-70};enemy.half={2,2,2};w.targets={enemy};w.step(trigger,.01f);
    check(w.homingTarget==0&&w.projectiles.size()==1&&w.projectiles[0].homingTarget==0,"enemy lock creates a guided missile");
    check(w.projectiles[0].kind==ProjectileKind::Rocket&&w.projectiles[0].radius==0,"guided missile has direct damage and a visual explosion");
    auto initial=w.projectiles[0].velocity;w.targets[0].center.x+=8;w.stepProjectiles(.05f);
    check(w.projectiles[0].velocity.x>initial.x&&std::abs(length(w.projectiles[0].velocity)-length(initial))<.001f,"missile tracks a moving target without changing speed");
    World copy;copy.targets=w.targets;coop::worldRead(coop::worldWire(w),copy);check(copy.projectiles[0].homingTarget==0&&copy.projectiles[0].weapon==Weapon::Homing,"missile lock survives cooperative replication");
    Pilot replicated;coop::pilotRead(coop::pilotWire(w),replicated);check(replicated.homingTarget==0,"yellow target lock survives pilot snapshots");
    w.targets[0].health=0;w.stepProjectiles(.05f);check(w.projectiles[0].homingTarget==-1,"destroyed target releases missile guidance safely");
    World hidden=range(guided);Target wall;wall.center=hidden.eye()+Vec3{0,0,-30};wall.half={5,5,5};hidden.targets={wall,enemy};hidden.step(trigger,.01f);check(hidden.homingTarget<0&&hidden.projectiles.empty(),"scenery occludes missile lock");
    for(int mount:{LeftHip,RightHip}){
        Frontend loadout;loadout.skirmish=true;loadout.loadout.fill(Weapon::Empty);loadout.loadout[LeftHand]=Weapon::Shotgun;loadout.weapon=int(Weapon::AutoGrenade);
        int other=mount==LeftHip?RightHip:LeftHip;
        check(loadout.equip(mount),"auto grenade launcher fits an empty hip pair");loadout.weapon=int(Weapon::Rocket);
        check(!loadout.equip(other),"launcher reserves the other hip");
        World automatic=range(loadout.loadout);Input fire;fire.fire[other]=true;
        automatic.step(fire,.01f);check(automatic.shots[mount]==1&&automatic.projectiles.size()==1&&automatic.projectiles[0].weapon==Weapon::AutoGrenade,"either hip button fires the one paired launcher");
        check(automatic.projectiles[0].kind==ProjectileKind::Bomb&&automatic.projectiles[0].radius==0,"automatic grenade uses ballistic direct-hit ordnance");
        for(int i=0;i<30;++i)automatic.step(fire,.01f);check(automatic.shots[mount]>=2&&automatic.ammo[mount]==100-automatic.shots[mount],"held trigger repeats without duplicating ammo pools");
        Progression full;full.rules=campaign.rules;full.completed=full.rules.fullMask();check(coop::validLoadout(loadout.loadout,full),"server accepts the paired launcher and handheld");
        loadout.loadout[other]=Weapon::Rocket;check(!coop::validLoadout(loadout.loadout,full),"server rejects an occupied reserved hip");loadout.loadout[other]=Weapon::Empty;
        check(loadout.equip(other,true)&&loadout.loadout[mount]==Weapon::Empty,"grip plus either hip removes the paired launcher");
        check(loadout.loadout[LeftHand]==Weapon::Shotgun,"paired launcher removal preserves handheld weapons");
    }
    std::cout<<"IS2 unlocks, four distinct weapons, tracked barrels, paired hips and co-op guidance passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
