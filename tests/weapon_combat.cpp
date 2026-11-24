#include "coop.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace iron;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static bool near(float a,float b,float tolerance=.002f){return std::abs(a-b)<tolerance;}
static World range(Weapon weapon=Weapon::Empty,int game=0){
    World w;w.targets.clear();w.game=game;Loadout loadout{};
    int mount=compatible(weapon,RightHand)?RightHand:compatible(weapon,LeftShoulder)?LeftShoulder:LeftHip;
    loadout[mount]=weapon;w.configure(loadout);w.wasFire.fill(false);return w;
}
static Projectile rocket(Vec3 position,Weapon weapon=Weapon::Rocket){
    Projectile p{position,{0,0,0},10,weaponInfo(weapon).damage,LeftShoulder};p.kind=ProjectileKind::Rocket;p.weapon=weapon;p.shooter=1;p.radius=weapon==Weapon::Rocket?5:0;return p;
}
static void bullet(World& w,Vec3 position,Vec3 velocity,float amount=2){Projectile p{position,velocity,2,amount,RightHand};p.shooter=0;p.weapon=Weapon::Rifle;w.projectiles.push_back(p);}
static void advance(World& w,float total,float dt){for(float time=0;time<total-.000001f;){float step=std::min(dt,total-time);w.stepProjectiles(step);time+=step;}}
int main(int argc,char** argv){try{
    check(argc==2,"retail path");
    std::ifstream proof(std::filesystem::path(argv[1])/"gameplay/weapon-audit.json");coop::Json source;proof>>source;
    for(auto& campaign:source.at("campaigns"))for(auto& row:campaign.at("weapons")){
        auto weapon=Weapon(row.at("native_id").get<int>());const auto& info=weaponInfo(weapon,campaign.at("game"));
        check(near(info.cooldown,row.at("interval_seconds"),.000001f),"runtime cadence matches independently extracted cartridge table");
        check(info.burst==row.at("burst").get<unsigned>(),"runtime firing mode matches cartridge table");
        if(row.contains("direct_damage"))check(near(info.damage,row.at("direct_damage")),"runtime ballistic payload matches cartridge damage");
    }
    for(int game:{0,1})for(int hz:{20,30,60,72,80,90,120,144,240,0}){
        std::vector<float> frames;float elapsed=0;
        while(elapsed<3-.000001f){float dt=hz?1.f/hz:std::array<float,5>{.007f,.043f,.013f,.089f,.021f}[frames.size()%5];dt=std::min(dt,3-elapsed);frames.push_back(dt);elapsed+=dt;}
        for(auto weapon:{Weapon::Rifle,Weapon::Gatling,Weapon::Rail,Weapon::Rocket,Weapon::Grenade,Weapon::Shotgun,Weapon::HeavyMachinegun,Weapon::AutoGrenade}){
            World w=range(weapon,game);int mount=compatible(weapon,RightHand)?RightHand:compatible(weapon,LeftShoulder)?LeftShoulder:LeftHip;Input in;in.fire[mount]=true;
            for(float dt:frames)w.step(in,dt,false);
            const auto& spec=weaponInfo(weapon,game);unsigned maximum=spec.burst?spec.burst:10000;
            unsigned expected=std::min(maximum,spec.cooldown>0?unsigned(std::ceil((3-frames.front())/spec.cooldown))+1:1u);
            check(w.shots[mount]>=expected-std::min(expected,1u)&&w.shots[mount]<=expected,"held cadence/burst is independent of refresh and irregular frame times");
            check(w.ammo[mount]==spec.capacity-w.shots[mount],"one round consumed per actual firing event");
            check(std::count_if(w.sounds.begin(),w.sounds.end(),[](auto& s){return s.kind==SoundEvent::Shot;})==w.shots[mount],"one sound per emitted round/burst member");
        }
        World cutter=range(Weapon::Cutter,game);Input cutting;cutting.fire[RightHand]=true;cutter.step(Input{},.001f,false);
        Target t;t.center=cutter.arms[RightHand].muzzle;t.half={.1f,.1f,.1f};t.health=1000;cutter.targets={t};
        for(float dt:frames)cutter.step(cutting,dt,false);
        check(near(cutter.targets[0].health,1000-54*3,.04f),"continuous cutter damage integrates elapsed time");
    }
    // Retain the negative timer remainder. Replacing it with a fresh interval
    // loses many rounds at 20 Hz or during jitter and fails this long-run check.
    for(int hz:{20,72,90,120,144}){World w=range(Weapon::Gatling);Input in;in.fire[LeftShoulder]=true;for(int i=0;i<hz*20;++i)w.step(in,1.f/hz,false);check(w.shots[LeftShoulder]>=299&&w.shots[LeftShoulder]<=301,"gatling retains phase for twenty seconds");}
    // A burst ends even while held, and releasing rearms it.
    for(auto weapon:{Weapon::Rifle,Weapon::AutoGrenade}){World w=range(weapon,1);int mount=weapon==Weapon::Rifle?RightHand:LeftHip;Input in;in.fire[mount]=true;for(int i=0;i<200;++i)w.step(in,.01f,false);check(w.shots[mount]==weaponInfo(weapon,1).burst,"held trigger stops at source burst count");in.fire[mount]=false;w.step(in,.1f,false);in.fire[mount]=true;for(int i=0;i<200;++i)w.step(in,.01f,false);check(w.shots[mount]==2*weaponInfo(weapon,1).burst,"release rearms next burst");}
    for(int game:{0,1})for(auto weapon:{Weapon::Rifle,Weapon::Gatling,Weapon::Rail,Weapon::Shotgun,Weapon::HeavyMachinegun,Weapon::Rocket,Weapon::Grenade,Weapon::Homing,Weapon::AutoGrenade})for(int spawn:{-1,0,3}){
        World w=range();w.game=game;Target t;t.center={0,20,0};t.half={1,1,1};t.health=10000;t.spawnType=spawn;w.targets={t};
        Projectile p{{0,20,5},{0,0,-100},3,weaponInfo(weapon,game).damage,LeftShoulder};p.weapon=weapon;p.shooter=0;
        p.kind=weapon==Weapon::Rocket||weapon==Weapon::Homing?ProjectileKind::Rocket:weapon==Weapon::Grenade||weapon==Weapon::AutoGrenade?ProjectileKind::Bomb:ProjectileKind::Bullet;w.projectiles={p};
        w.stepProjectiles(.1f);float expected=weapon==Weapon::Rocket?(spawn<0?30:20.1f):weapon==Weapon::AutoGrenade&&spawn==3?5:weaponInfo(weapon,game).damage;
        check(near(10000-w.targets[0].health,expected),"direct collision uses verified static/actor weapon damage");
        if(weapon==Weapon::Homing||weapon==Weapon::AutoGrenade)for(auto& fx:w.effects)check(fx.damageRate==0,"homing and auto grenade explosions have no invented splash damage");
    }
    for(int game:{0,1})for(int spawn:{-1,0,3,8,18}){
        World w=range();w.game=game;Target t;t.center={0,20,0};t.half={1,1,1};t.health=10000;t.spawnType=spawn;w.targets={t};w.missile={true,{0,20,2},0,0,5,0,0,0};
        w.step(Input{},.05f,false);float expected=game&&(spawn==3||spawn==8||spawn==18)?0:!game&&spawn==3?130:1000;
        check(!w.missile.active&&near(10000-w.targets[0].health,expected),"cruise direct collision preserves cartridge target exceptions");
        check(w.effects[0].damageRadius==20&&near(w.effects[0].damageRate,game?125.1f:125),"cruise leaves original timed blast profile");
    }
    {World w=range();Target t;t.center={0,20,0};t.half={1,1,1};w.targets={t};auto p=rocket({0,20,5});p.mount=-1;p.shooter=-1;p.damage=12;p.velocity={0,0,-100};w.projectiles={p};w.stepProjectiles(.1f);check(near(w.targets[0].health,88),"unmapped enemy explosive fallback is not double-counted as direct plus splash");}
    for(auto weapon:{Weapon::Rocket,Weapon::Homing}){
        World w=range();w.projectiles.push_back(rocket({0,20,0},weapon));unsigned needed=weapon==Weapon::Homing?6:2;
        for(unsigned i=0;i<needed;++i){bullet(w,{0,20,5},{0,0,-100});w.stepProjectiles(.1f);check(w.projectiles.size()==(i+1==needed?0u:1u),"rocket survives zero health, dies only below zero");}
        check(std::count_if(w.effects.begin(),w.effects.end(),[](auto& fx){return fx.kind==WeaponEffect::Blast;})==1,"one small explosion per intercepted rocket");
        for(auto& fx:w.effects)if(fx.kind==WeaponEffect::Blast)check(fx.size==3&&fx.damageRate==0,"interception explosion is visual, not ordinary impact splash");
    }
    {World w=range();auto r=rocket({-10,20,0});r.velocity={200,0,0};w.projectiles={r};bullet(w,{0,20,10},{0,0,-200},20);w.stepProjectiles(.1f);check(w.projectiles.empty(),"relative sweep catches crossing moving projectiles between frames");}
    {World w=range();Target wall;wall.center={0,20,3};wall.half={2,2,.1f};w.targets={wall};w.projectiles={rocket({0,20,0})};bullet(w,{0,20,5},{0,0,-100},20);w.stepProjectiles(.1f);check(w.projectiles.size()==1&&w.projectiles[0].hull==2,"wall blocks projectile interception");}
    {World w=range();w.projectiles={rocket({0,20,0}),rocket({0,20,-3})};bullet(w,{0,20,5},{0,0,-100},20);w.stepProjectiles(.1f);check(w.projectiles.size()==1&&w.projectiles[0].position.z==-3,"one bullet cannot destroy two rockets");}
    {World w=range();auto bomb=rocket({0,20,0});bomb.kind=ProjectileKind::Bomb;bomb.weapon=Weapon::Grenade;w.projectiles={bomb};bullet(w,{0,20,5},{0,0,-100},20);w.stepProjectiles(.1f);check(w.projectiles.size()==2,"grenades cannot be shot down");w.missile={true,{0,30,-10},0,0,5,0,0,0};bullet(w,{0,30,0},{0,0,-200},2000);w.stepProjectiles(.1f);check(w.missile.active,"cruise missile damage callback is immune");}
    for(auto weapon:{Weapon::Rocket,Weapon::Homing}){World w=range();w.projectiles={rocket({0,20,0},weapon)};w.strike({0,20,1},{0,0,-1},Weapon::Empty,7);w.stepProjectiles(.01f);check(w.projectiles.empty(),"melee uses actor damage against destructible rockets");}
    {World w=range();w.projectiles={rocket({0,20,0})};w.destruction({0,20,0},20,Weapon::Grenade,15,125);w.stepProjectiles(.05f);check(w.projectiles.empty(),"timed blast can intercept a rocket");}
    for(float dt:{1.f/20,1.f/72,1.f/90,1.f/120,.073f}){
        World w=range();Target t;t.center={0,20,0};t.half={1,1,1};t.health=1000;w.targets={t};w.destruction(t.center,20,Weapon::Grenade,15,125);w.stepProjectiles(.1f);
        check(near(w.targets[0].health,987.5f),"blast applies damage rate, not an immediate full hit");advance(w,.95f,dt);check(near(w.targets[0].health,875,.02f),"blast integrates exactly one second at every frame rate");
    }
    {World w=range();w.position={0,0,0};w.missionStatus=MissionStatus::Active;w.destruction({0,6,0},20,Weapon::Grenade,15,125);w.stepProjectiles(.1f);check(near(w.health,87.5f),"own grenade blast damages its pilot");}
    {World w=range();w.missionStatus=MissionStatus::Active;w.position={0,0,0};w.cooperative=true;Pilot other;other.position={0,0,0};other.missionStatus=MissionStatus::Active;w.teammates={&w,&other};w.destruction({0,6,0},20,Weapon::Grenade,15,125);w.stepProjectiles(.1f);check(near(w.health,87.5f)&&near(other.health,87.5f),"co-op blast damages each pilot once");}
    {World w=range();w.projectiles={rocket({0,20,0})};w.projectiles[0].hull=.5f;w.destruction({10,20,0},20,Weapon::Grenade,15,125);World copy;copy.targets=w.targets;coop::worldRead(coop::worldWire(w),copy);check(copy.projectiles[0].hull==.5f&&copy.effects[0].damageRate==125&&copy.effects[0].damageRadius==15,"partial rocket hull and persistent blast replicate");}
    std::cout<<"Cartridge cadence/payloads, 20-240 Hz/jitter, bursts, cutter integration, rocket interception and timed blasts pass\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
