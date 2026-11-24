#include "rpg/session.hpp"
#include "rpg/kaiju_audio.hpp"
#include <stdexcept>
namespace iron::rpg {
std::span<const CampaignBoss> campaignBosses(){
  static const std::vector<CampaignBoss> bosses={
    {{"prism_regent","PRISM REGENT",10,EnemyKind::LargeMecha,55,1,4,0,Ace|ShockArmor,
       {{"Prism Lance",2,8,1,15,150},{"Funnel Crossfire",1,8,4,10,90,4,AttackEffect::Damage,false,2},{"Repulsor",1,8,2,0,15,0,AttackEffect::Push}},1},
      "aeon_primarch",0,8,"BREAK SIGHTLINES DURING THE LANCE CHARGE; CLOSE INSIDE 15M.","aeon_diode",1.6f},
    {{"null_choir","NULL CHOIR",10,EnemyKind::Installation,75,2,0,0,Barricaded,
       {{"Relay Beam",2,8,2,20,180},{"Sensor Null",0,4,4,10,90,4,AttackEffect::DisruptSystems,false,2},{"Point Defense",1,8,1,0,20}},1},
      "ion_disruptor_installation",0,2,"USE COVER THROUGH THE PULSE, THEN ATTACK INSIDE THE BEAM MINIMUM.","swarm_drone",1.8f},
    {{"ironwake_marshal","IRONWAKE MARSHAL",10,EnemyKind::LargeMecha,65,3,0,0,Commander,
       {{"Siege Mortar",2,10,2,30,180,4,AttackEffect::Push,false,2},{"Rotary Battery",1,10,2,10,90},{"Thermal Cleaver",2,6,1,0,10}},1},
      "terran_mammoth",1,8,"FLANK THE SIEGE LINE; THE MORTAR CANNOT FIRE INSIDE 30M.","terran_grunt",1.8f},
    {{"red_horizon","RED HORIZON",10,EnemyKind::Conventional,70,1,6,0,Flying|Starship|Carrier,
       {{"Broadside",2,8,2,25,150},{"Seeker Screen",1,10,4,10,90,4,AttackEffect::Damage,false,2},{"Ventral Turrets",1,10,2,0,25}},1},
      "dreadnought",1,7,"STRAFE THE BROADSIDE AND CLEAR EACH OF TWO LAUNCHED AIR WINGS.","interceptor",2.f},
    {{"crownless_duelist","CROWNLESS DUELIST",10,EnemyKind::LargeMecha,10,1,0,0,Ace|Leap,
       {{"Royal Cleave",2,10,1,0,12,0,AttackEffect::Push},{"Lodium Repeater",2,6,1,12,90},{"Challenge Pulse",0,4,2,0,30,4,AttackEffect::Reactor,false,3}},1},
      "neo_paladin",2,1,"KEEP DISTANCE FROM THE CLEAVE; PUNISH ITS VISIBLE CHARGE RECOVERY.","neo_knight",1.4f},
    {{"gravesong_sovereign","GRAVESONG SOVEREIGN",10,EnemyKind::LargeKaiju,15,1,0,2,Regenerate,
       {{"Grave Mandibles",2,10,1,0,12},{"Caustic Spines",2,6,2,12,90},{"Gravesong",0,4,4,0,35,4,AttackEffect::Pull,false,0}},1},
      "kaiju_broodqueen",3,6,"KEEP OUTSIDE ITS MAW; BURST THROUGH REGENERATION AND CLEAR TWO BROODS.","kaiju_drone",1.7f}
  };return bosses;
}
const CampaignBoss &campaignBoss(int index){auto bosses=campaignBosses();if(index<0||size_t(index)>=bosses.size())throw std::runtime_error("Unknown campaign boss");return bosses[size_t(index)];}
int bossBaseEnemy(int index){const auto &boss=campaignBoss(index);for(size_t i=0;i<enemyCatalog().size();++i)if(std::string_view(enemySpec(i).id)==boss.base)return int(i);throw std::runtime_error("Missing boss rig");}
void validateBossState(const Session &s){
  for(size_t i=0;i<s.enemies.size();++i){const auto &e=s.enemies[i];
    if(e.boss< -1||e.boss>=int(campaignBosses().size())||e.bossPhase<0||e.bossPhase>2||!std::isfinite(e.bossTell)||e.bossTell<0||e.bossTell>3)throw std::runtime_error("Invalid boss state");
    if(e.boss>=0&&(!s.mission.recipe.finale||s.mission.recipe.boss!=e.boss||i!=0||e.spec!=bossBaseEnemy(e.boss)))throw std::runtime_error("Boss outside campaign finale");
  }
}
void Session::stepBosses(float dt){
  for(size_t i=0;i<enemies.size();++i){auto &e=enemies[i];if(e.boss<0||e.hp<=0)continue;
    (void)dt;
    int phase=e.hp*3<=e.maxHp?2:e.hp*3<=e.maxHp*2?1:0;
    if(phase<=e.bossPhase)continue;
    // One bounded reinforcement wave per threshold, even if a large hit crosses both.
    const int first=e.bossPhase+1;e.bossPhase=phase;int boss=e.boss;
    e.disabled=std::max(e.disabled,1.5f);e.fireClock=2;
    const auto origin=world.targets[i].origin;auto reinforcement=campaignBoss(boss).reinforcement;
    if(campaignBoss(boss).spec.kind==EnemyKind::LargeKaiju)
      emitKaijuSound(world,CombatCue::KaijuRoar,world.targets[i].center);
    int child=0;for(size_t n=0;n<enemyCatalog().size();++n)if(std::string_view(enemySpec(n).id)==reinforcement)child=int(n);
    for(int wave=first;wave<=phase;++wave){
      bool lead=true;
      for(int n=0;n<std::min(6,2+int(members.size()));++n){
        float a=float(n)*2.39996f;
        if(spawnManualEnemy(child,origin+Vec3{std::cos(a)*45,0,std::sin(a)*45},4,0,{},lead))
          lead=false;
      }
    }
    log(std::string(campaignBoss(boss).spec.name)+" / PHASE "+std::to_string(phase+1)+" / REINFORCEMENTS");
  }
}
}
