#include "cockpit_effects.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace iron;
using namespace avp;
static void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static float distance(const Matrix& a,const Matrix& b){float value=0;for(size_t i=0;i<a.size();++i)value=std::max(value,std::abs(a[i]-b[i]));return value;}
static bool finite(const Matrix& m){for(float v:m)if(!std::isfinite(v))return false;return true;}
static CockpitEffects run(unsigned fps){
    CockpitEffects fx;Pilot p;fx.reset();fx.update(p,0,true);
    for(unsigned frame=1;frame<=fps*2;++frame){
        float time=float(frame)/fps;p.health=time>=1?74:100;p.walkingSpeed=8;p.gaitPhase=std::fmod(time*8*(100.f/96),18.f);
        fx.update(p,1.f/fps,true);
    }
    return fx;
}
int main(){try{
    Pilot pilot;CockpitEffects fx;fx.reset();fx.update(pilot,0,false);
    check(!fx.booting()&&fx.bootTime==0,"loadout started boot timer");
    fx.update(pilot,0,true);check(fx.booting()&&fx.power[0]==0,"deployment did not start with dark instruments");
    fx.update(pilot,2,true);check(fx.booting()&&fx.power[0]>.99f,"power rails did not come online");
    fx.update(pilot,3,true);check(!fx.booting(),"boot did not complete in elapsed seconds");
    pilot.health=74;fx.update(pilot,0,true);check(fx.seat[13]<-.06f&&fx.seat[14]>.04f,"heavy damage did not immediately kick the seat");
    fx.update(pilot,3,true);check(distance(fx.seat,identity)<.000001f,"impact failed to decay");
    pilot.health=70;fx.update(pilot,.01f,true);check(distance(fx.seat,identity)<.000001f,"small damage caused heavy recoil");
    pilot.health=66;fx.update(pilot,.05f,true);check(distance(fx.seat,identity)>.015f,"split snapshot damage lost heavy impact");
    fx.update(pilot,3,true);pilot.health=25;fx.update(pilot,0,true);check(fx.fractureStage()==0,"glass cracked at exactly 25% armour");
    pilot.health=24.99f;fx.update(pilot,0,true);check(fx.fractureStage()==1&&fx.power[3]<.02f,"critical damage did not fracture glass/fail heading supply");
    pilot.health=12;fx.update(pilot,0,true);check(fx.fractureStage()==2,"fracture severity failed to increase");
    pilot.health=5;fx.update(pilot,0,true);check(fx.fractureStage()==3,"lowest armour lacks severe cracks");
    fx.update(pilot,.1f,false);check(fx.fractureStage()==3&&!fx.visible,"loss of focus removed existing glass damage");
    fx.update(pilot,3,true);pilot.health=100;fx.update(pilot,0,true);
    check(fx.fractureStage()==0&&distance(fx.seat,identity)<.000001f,"repair left cracks or caused a damage kick");
    for(float power:fx.power)check(power==1,"repair did not restore healthy instrument power");
    pilot.walkingSpeed=8;pilot.gaitPhase=3;fx.update(pilot,1,true);check(distance(fx.seat,identity)>.005f,"travel gait has no cockpit motion");
    pilot.walkingSpeed=0;fx.update(pilot,3,true);check(distance(fx.seat,identity)<.000001f,"walking shake persists while stationary");
    pilot.walkingSpeed=8;pilot.gaitPhase=12;fx.update(pilot,.5f,true);
    const auto time=fx.clock;fx.update(pilot,10,false);check(fx.clock==time&&fx.seat==identity&&fx.hull==identity,"inactive view advances shake or boot");
    pilot.missile.active=true;fx.update(pilot,.1f,true);check(!fx.visible&&fx.seat==identity,"cruise camera inherited seat recoil");
    pilot.missile.active=false;pilot.lifeSpent=true;fx.update(pilot,.1f,true);check(!fx.visible&&fx.seat==identity,"ejection inherited cockpit motion");
    pilot=Pilot{};fx.reset();fx.update(pilot,1,true);pilot.lifeSpent=true;pilot.health=0;fx.update(pilot,.1f,true);
    check(!fx.booting()&&!fx.visible,"destruction during boot left a powered self-test on the ejected cockpit");
    pilot=Pilot{};pilot.health=5;fx.reset();fx.update(pilot,0,true);
    check(fx.bootTime==0&&distance(fx.seat,identity)==0,"fresh round inherited damage recoil");
    pilot.health=100;fx.reset();fx.update(pilot,0,true);check(fx.fractureStage()==0,"fresh deployment inherited fractures");
    World is;
    is.missionStatus=MissionStatus::Active;
    fx.reset();fx.update(is,5,true);
    is.hurt(26,{1,0,0});
    fx.update(is,.016f,true);
    check(is.damageUntil[2]>is.elapsed&&distance(fx.seat,identity)>.04f,
          "IS impact did not register a right-side hit and seat kick");
    Pilot bearings;
    for(int i=0;i<8;++i){
        const float a=i*pi/4;
        bearings.markDamage({std::sin(a),0,-std::cos(a)},4.f);
        check(bearings.damageUntil[i]>5.f,"Damage bearing mapped to the wrong octant");
    }
    bearings.torsoYaw=pi/2;
    bearings.damageUntil={};
    bearings.markDamage({-1,0,0},5);
    check(bearings.damageUntil[0]>6.f,"Damage bearing ignored torso yaw");
    World rpg;
    rpg.rpgPrototype=true;
    fx.reset();fx.update(rpg,5,true);
    rpg.rpgArmor=84; // armour loss with hull health untouched
    fx.update(rpg,.016f,true);
    check(fx.armor==84&&distance(fx.seat,identity)>.02f,"RPG armour-only hit missed the inherited shake");
    check(armorLamp(100,4).y==1&&armorLamp(60,4).y<.1f,"Status lamps do not follow integrity");
    check(statusLampFill(30,0)==1&&statusLampFill(30,1)==.5f&&statusLampFill(30,2)==0,
          "Partial lamp fill must occupy one cell and leave the rest dark");
    rpg.rpgLocalPilot=7;rpg.rpgCanopyAttacker=0;rpg.elapsed=10;
    rpg.targets.resize(1);
    auto &claw=rpg.targets.front().ai;
    claw.rpgGrapplePilot=7;
    claw.voidmawPose={rpg::VoidmawPose::Cling,9,0,-1};
    claw.combatMotion.play(EnemyAction::Strike,10,.46f);
    fx.update(rpg,.016f,true);
    check(fx.slime.side==-1&&fx.slime.biteAt==10,"Attached bite failed to wet the victim's pane");
    rpg.rpgLocalPilot=8;fx.update(rpg,.016f,true);
    check(fx.slime.side==0,"Pilot inherited another pilot's saliva");
    rpg.rpgLocalPilot=7;fx.update(rpg,.016f,true);
    rpg.rpgCanopyAttacker=-1;rpg.elapsed=11;fx.update(rpg,.016f,true);
    check(fx.slime.side==-1&&fx.slime.lastContact==10,"Release erased or refreshed wet streaks");
    rpg.elapsed=12.1f;fx.update(rpg,.016f,true);
    check(fx.slime.side==0,"Released canopy retained expired residue");
    Pickup crate{{},rpgSafeBoxPickup,false,1u};
    check(!unclaimedSalvage(crate,0)&&unclaimedSalvage(crate,1),
          "Salvage visibility must be per pilot");
    const auto at30=run(30),at60=run(60),at72=run(72),at90=run(90),at120=run(120),at144=run(144);
    for(const auto& sample:{at30,at72,at90,at120,at144})check(distance(sample.seat,at60.seat)<.00002f&&distance(sample.hull,at60.hull)<.00002f,"animation depends on headset refresh rate");
    CockpitEffects forward,backward;Pilot walker;walker.walkingSpeed=8;
    walker.gaitPhase=2;forward.update(walker,1,true);walker.gaitPhase=3;forward.update(walker,0,true);
    walker.gaitPhase=5;backward.update(walker,1,true);walker.gaitPhase=4;backward.update(walker,0,true);
    check(distance(forward.seat,backward.seat)<.000001f,"reverse walking shake misses the reverse footfall phase");
    // The same rigid visual transform preserves the two eyes' relative pose.
    auto body=multiply(translation(31,12,-29),rotation(.8f));auto motion=multiply(body,multiply(at60.seat,inverseRigid(body)));
    auto left=multiply(body,translation(-.032f,0,0)),right=multiply(body,translation(.032f,0,0));
    auto before=multiply(inverseRigid(left),right),after=multiply(inverseRigid(multiply(motion,left)),multiply(motion,right));
    check(distance(before,after)<.00001f,"visual recoil changes binocular separation");
    pilot.health=std::numeric_limits<float>::quiet_NaN();pilot.gaitPhase=pilot.walkingSpeed=std::numeric_limits<float>::infinity();
    fx.update(pilot,std::numeric_limits<float>::quiet_NaN(),true);
    check(finite(fx.seat)&&finite(fx.hull)&&std::isfinite(fx.clock),"bad presentation data contaminated poses");
    std::cout<<"Cockpit boot, damage/repair, strict armour threshold, gait/stop, pause/missile/ejection, deployment reset, 30–144 Hz timing and stereo rigidity passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
