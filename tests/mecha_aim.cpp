#include "rpg/session.hpp"
#include "rpg/cockpit_arm_rest.hpp"
#include "rpg/weapon_ports.hpp"
#include "rpg/original_assets.hpp"
#include <fstream>
#include <iostream>
using namespace iron;
using namespace iron::rpg;
namespace {
constexpr float dt=1.f/60;
Json checks=Json::array();
void check(const char *name,bool passed,Json evidence=Json::object()) {
  checks.push_back({{"name",name},{"passed",passed},{"evidence",evidence}});
}
Session fixture() {
  Session s(713);s.add(1,"Aim regression");finishCreation(s.member(1).sheet);
  GameSettings settings;settings.maps=1;settings.enemy=3;settings.enemyCount=1;
  settings.objective=0;settings.opener=5;settings.environment=3;settings.location=0;
  s.command(1,{{"op","settings"},{"settings",settings}});
  s.command(1,{{"op","ready"},{"value",true}});s.command(1,{{"op","deploy"}});
  s.enemies[0].disabled=10000;
  s.world.proceduralTerrain=false;s.world.editGeometry().terrain.clear();s.world.indexTerrain();
  s.world.interiorRegions.clear();s.world.waterRegions.clear();
  for(auto &target:s.world.targets)target.active=false;
  s.member(1).body.position={17,0,-45};
  s.command(1,{{"op","input"},{"epoch",s.epoch},{"input",coop::inputWire(Input{})}});s.step(dt);
  return s;
}
void submit(Session &s,const Input &input) {
  s.command(1,{{"op","input"},{"epoch",s.epoch},{"input",coop::inputWire(input)},{"systems",input.ping}});s.step(dt);
}
void equip(Session &s,Gear gear,int hand) {
  auto &m=s.member(1);m.sheet.equipment.fill(Gear::Empty);
  m.sheet.equipment[hand]=gear;normalizeHeavyEquipment(m.sheet);
}
std::string modelName(const Member &m,int hand){auto gear=m.sheet.equipment[hand];return originalVariant(gearVisualId(gear),m.sheet.creationSeed^uint32_t(m.sheet.qualities[hand])^uint32_t(gear));}
float angle(Vec3 a,Vec3 b) {return std::acos(std::clamp(dot(unit(a),unit(b)),-1.f,1.f));}
}
int main(int argc,char **argv) {try {
  auto base=fixture();const auto tip=weaponTuning(Gear::Rifle)->muzzle;
  float restError=0,replicaError=0;
  bool independentTracking=true;
  for(size_t chassis=0;chassis<chassisIds.size();++chassis){
    auto session=base;
    session.member(1).sheet.chassis=Chassis(chassis);
    session.member(1).body.chassisYaw=.92f;session.member(1).body.torsoYaw=-.27f;
    submit(session,Input{});
    auto local=session.view(1);
    Session guest;guest.readSnapshot(session.snapshot());
    auto remote=guest.view(1);
    for(int hand=0;hand<2;++hand){
      const auto &pose=cockpitArmRest[chassis][size_t(hand)];
      const auto &arm=local.arms[hand];
      independentTracking=independentTracking&&arm.tracked;
      const auto worldPoint=[&](Vec3 p){return local.eye()+rotate(local.cockpitOrientation(),p);};
      restError=std::max({restError,length(arm.shoulder-worldPoint(pose.shoulder)),
                         length(arm.elbow-worldPoint(pose.elbow)),length(arm.wrist-worldPoint(pose.wrist))});
      const auto &copy=remote.arms[hand];
      replicaError=std::max({replicaError,length(copy.shoulder-arm.shoulder),
                             length(copy.elbow-arm.elbow),length(copy.wrist-arm.wrist)});
      Input lost;lost.handPoses[hand].valid=false;submit(session,lost);
      auto missing=session.view(1);
      independentTracking=independentTracking&&!missing.arms[hand].tracked&&missing.arms[hand^1].tracked;
    }
  }
  check("All chassis reproduce the authored neutral arm joints",restError<.002f,{{"maximum_error_m",restError}});
  check("Tracking loss disables only the affected chassis arm",independentTracking);
  check("Replicated arm joints preserve chassis-specific poses",replicaError<.001f,{{"maximum_error_m",replicaError}});
  float originError=0,visualAxisError=0,shotAngle=0;int poses=0;
  for(auto gear:{Gear::Rifle,Gear::Cannon})for(int hand=0;hand<2;++hand)
    for(float torso:{0.f,.9f,-2.3f})for(float yaw:{0.f,.7f,-1.2f})for(float pitch:{-.4f,.3f})for(float recoil:{0.f,.4f}) {
      auto s=base;equip(s,gear,hand);auto &m=s.member(1);m.body.chassisYaw=torso;m.recoil[hand]=recoil;
      Input input;input.handPoses[hand].aim=angles(yaw,pitch,.6f);
      input.handPoses[hand].grip=angles(yaw,pitch,.6f);input.fire[hand]=true;
      submit(s,input);auto view=s.view(1);const auto &arm=view.arms[hand];
      if(s.ordnance.empty()){check("Tracked shot exists",false);continue;}
      const auto &shot=s.ordnance.back().body;
      Vec3 expected=arm.wrist+rotate(arm.aim,weaponTuning(gear)->muzzle);
      originError=std::max(originError,length(shot.position-shot.velocity*dt-expected));
      visualAxisError=std::max(visualAxisError,angle(rotate(arm.aim,{0,0,-1}),arm.forward));
      shotAngle=std::max(shotAngle,angle(shot.velocity,rotate(arm.aim,{0,0,-1})));++poses;
    }
  check("Projectile origin matches rendered muzzle in both hands",poses==144&&originError<.001f,{{"poses",poses},{"maximum_error_m",originError}});
  check("Rendered recoil and targeting ray share one axis",visualAxisError<.001f,{{"maximum_error_degrees",visualAxisError*180/pi}});
  check("Tracked shots stay near rendered barrel direction through recoil",shotAngle<=3*pi/180+.001f,{{"maximum_error_degrees",shotAngle*180/pi}});

  float worstSpread=0,worstLightSpread=0;int attempts=0;
  for(auto gear:{Gear::Rifle,Gear::Cannon})for(int biome=0;biome<8;++biome) {
    auto s=base;equip(s,gear,RightHand);s.biome=biome;s.mission.recipe.environment=biome;
    s.member(1).sheet.attributes[0]=3;Input input;input.fire[RightHand]=true;input.forward=1;
    for(int n=0;n<32;++n) {
      auto &m=s.member(1);m.recoil[RightHand]=.55f;m.body.cooldown.fill(0);
      m.sheet.overheated=false;m.sheet.reactorStress=0;s.ordnance.clear();submit(s,input);
      if(s.ordnance.empty()){check("Worst-case shot exists",false);continue;}
      float deviation=angle(s.ordnance.back().body.velocity,m.body.arms[RightHand].forward);
      worstSpread=std::max(worstSpread,deviation);
      if(gear==Gear::Rifle)worstLightSpread=std::max(worstLightSpread,deviation);++attempts;
    }
  }
  check("Unsupported sustained spread stays bounded in every environment",attempts==512&&worstSpread<=3*pi/180+.001f,{{"shots",attempts},{"maximum_deviation_degrees",worstSpread*180/pi}});

  check("One-handed weapons retain tight unsupported dispersion in every environment",worstLightSpread<=1.5f*pi/180+.001f,{{"maximum_deviation_degrees",worstLightSpread*180/pi}});

  auto moved=base;equip(moved,Gear::Rifle,RightHand);Input input;
  submit(moved,input);auto first=moved.member(1).body.arms[1].muzzle;
  input.handPoses[1].position={.2f,-.3f,-.5f};input.handPoses[1].aim=angles(.65f,.2f);submit(moved,input);
  bool responds=length(moved.member(1).body.arms[1].muzzle-first)>.5f;
  input.handPoses[1].valid=false;input.fire[1]=true;submit(moved,input);
  check("Moving hands update origins and invalid tracking prevents firing",responds&&moved.ordnance.empty()&&!moved.member(1).body.arms[1].tracked);

  auto braced=base;equip(braced,Gear::Rifle,LeftHand);input={};input.ping[RightHand]=true;input.fire[LeftHand]=true;
  input.handPoses[LeftHand].position={0,-.13f,-.7f};input.handPoses[RightHand].position={.05f,-.13f,-.85f};
  int bursts=0;float bracedSpread=0,bracedOriginError=0;bool continuousFire=true;
  for(int tick=0;tick<600;++tick){
    auto &m=braced.member(1);
    continuousFire&=!m.sheet.overheated;
    auto before=m.body.shots[LeftHand];submit(braced,input);
    if(m.body.shots[LeftHand]>before&&!braced.ordnance.empty()){
      ++bursts;auto &shot=braced.ordnance.back().body;auto view=braced.view(1);auto &arm=view.arms[LeftHand];
      bracedSpread=std::max(bracedSpread,angle(shot.velocity,rotate(arm.aim,{0,0,-1})));
      bracedOriginError=std::max(bracedOriginError,length(shot.position-shot.velocity*dt-(arm.wrist+rotate(arm.aim,tip))));
    }
  }
  check("Ten seconds of rifle fire without a reactor shutdown",continuousFire);
  check("Ten seconds of braced fire retain muzzle alignment and accuracy",braced.member(1).handSupported[LeftHand]&&bursts>=10&&bracedSpread<=1.5f*pi/180+.001f&&bracedOriginError<.001f,{{"bursts",bursts},{"maximum_error_m",bracedOriginError},{"maximum_deviation_degrees",bracedSpread*180/pi}});
  // Bracing changes physical recoil. Both barrels start at the same aim.
  auto heavySolo=base;equip(heavySolo,Gear::Cannon,LeftHand);auto heavyBrace=heavySolo;
  input={};input.handPoses[0].position={0,-.13f,-.7f};input.handPoses[1].position={.05f,-.13f,-.85f};
  input.fire[0]=true;submit(heavySolo,input);input.ping[1]=true;submit(heavyBrace,input);
  check("Two-handed support reduces recoil with the same first-shot dispersion",
    heavyBrace.member(1).handSupported[0]&&
    length(heavySolo.ordnance.back().body.velocity-heavyBrace.ordnance.back().body.velocity)<.001f&&
    heavySolo.member(1).recoil[0]>heavyBrace.member(1).recoil[0]*8);
  const auto &primary=heavyBrace.member(1).body.arms[LeftHand];
  const auto snapped=heavyBrace.member(1).body.arms[RightHand].wrist;
  check("The support wrist snaps to the weapon and is replicated",
        length(snapped-primary.wrist-rotate(primary.aim,{.28f,.05f,-1.10f}))<.001f);
  Session braceReplica;braceReplica.readSnapshot(heavyBrace.snapshot());
  check("The support arm survives snapshot roundtrip",length(braceReplica.view(1).arms[RightHand].wrist-snapped)<.001f);
  input.ping[1]=false;input.fire[0]=false;submit(heavyBrace,input);
  check("Letting go of the support grip restores controller tracking",!heavyBrace.member(1).handSupported[0]&&
        length(heavyBrace.member(1).body.arms[RightHand].wrist-snapped)>.1f);
  auto beforeKick=heavySolo.member(1).body.arms[0];input.fire.fill(false);input.ping.fill(false);
  submit(heavySolo,input);auto kicked=heavySolo.member(1).body.arms[0];
  check("Unsupported heavy recoil animates grip and knocks the actual barrel aim upward",
    kicked.forward.y>beforeKick.forward.y+.2f&&
    rotate(kicked.grip,{0,0,-1}).y>rotate(beforeKick.grip,{0,0,-1}).y+.2f);
  for(int n=0;n<60;++n)submit(heavySolo,input);
  check("Recoil recovers while controller pose remains stationary",heavySolo.member(1).recoil[0]<.01f);

  auto stash=base;equip(stash,Gear::Rifle,LeftHand);stash.member(1).sheet.equipment[RightHand]=Gear::Blade;
  input={};input.ping[1]=true;input.handPoses[0].position={0,-.13f,-.7f};input.handPoses[1].position={.05f,-.13f,-.85f};
  submit(stash,input);
  check("An equipped offhand cannot brace a weapon",!stash.member(1).handSupported[0]);
  input.ping[1]=false;submit(stash,input);
  auto savedEquipment=stash.member(1).sheet.equipment;auto powers=stash.member(1).chassisCooldown;
  input.handPoses[1].position={.35f,.05f,.25f};input.ping[1]=true;submit(stash,input);
  auto stowed=stash.view(1);
  check("Over-shoulder grip stashes the weapon without changing the loadout or activating chassis power",
    stash.member(1).handStashed[1]&&stash.member(1).sheet.equipment==savedEquipment&&
    stowed.weapons[1]==Weapon::Empty&&stowed.rpgModelNames[1].empty()&&
    stowed.rpgStashedModels[1]==modelName(stash.member(1),1)&&!(stowed.rpgAimMask&2)&&
    stash.member(1).chassisCooldown<=powers);
  submit(stash,input);
  check("Holding the shoulder grip does not repeatedly toggle stash",stash.member(1).handStashed[1]);
  input.handPoses[1].position={.05f,-.13f,-.85f};submit(stash,input);
  check("A stashed weapon frees its hand for support",stash.member(1).handSupported[0]);
  Session stashReplica;stashReplica.readSnapshot(stash.snapshot());auto remote=stashReplica.view(1);
  check("Stash and recoil replicate with shoulder presentation",stashReplica.member(1).handStashed==stash.member(1).handStashed&&
    stashReplica.member(1).recoil==stash.member(1).recoil&&remote.rpgStashedModels==stash.view(1).rpgStashedModels);
  input.ping[1]=false;submit(stash,input);input.handPoses[1].position={.35f,.05f,.25f};
  input.ping[1]=true;input.handPoses[1].valid=false;submit(stash,input);
  input.handPoses[1].valid=true;submit(stash,input);
  check("Tracking recovery with a held grip cannot retrieve a weapon",stash.member(1).handStashed[1]);
  input.ping[1]=false;submit(stash,input);input.ping[1]=true;submit(stash,input);
  check("Release and grab over the shoulder retrieves the same weapon",!stash.member(1).handStashed[1]&&
    stash.view(1).rpgModelNames[1]==modelName(stash.member(1),1)&&stash.view(1).rpgStashedModels[1].empty());

  input={};input.handPoses[1].position={.35f,.05f,.25f};input.ping[1]=true;
  input.focused=false;submit(stash,input);input.focused=true;submit(stash,input);
  check("Focus recovery requires releasing the grip before stashing",!stash.member(1).handStashed[1]);

  for(int hand=0;hand<2;++hand){
    auto linkedStash=base;equip(linkedStash,Gear::Cannon,hand);
    input={};input.ping[hand]=true;input.handPoses[hand].position={hand?.35f:-.35f,.05f,.25f};
    linkedStash.member(1).drone=10;submit(linkedStash,input);
    input={};input.fire[0]=input.fire[1]=true;submit(linkedStash,input);
    check("A stashed two-handed weapon cannot fire through either linked trigger",
      linkedStash.member(1).handStashed[hand]&&linkedStash.ordnance.empty()&&
      !linkedStash.member(1).droneControl&&
      linkedStash.view(1).rpgStashedModels[hand^1].empty()&&
      linkedStash.view(1).rpgStashedModels[hand]==modelName(linkedStash.member(1),hand),{{"hand",hand}});
  }
  Session replica;replica.readSnapshot(braced.snapshot());
  const auto &rig=replica.member(1).body.armRig;
  check("Replicated pilot retains the original authoritative muzzle rig",length(rig.muzzle[1]-originalArmRig().muzzle[1])<.0001f);
  bool catalog=true;
  for(auto family:weaponFamilies()){
    for(auto rarity:{Rarity::Common,Rarity::Uncommon,Rarity::Rare}){
      const auto gear=weaponVariant(family,rarity);
      catalog&=weaponFamily(gear)==family&&weaponRarity(gear)==rarity&&
        gearRead(gearInfo(gear).id)==gear&&heavy(gear)==heavy(family)&&
        !originalMesh(gearVisualId(gear))->empty();
    }
    const auto &common=gearInfo(family),&uncommon=gearInfo(weaponVariant(family,Rarity::Uncommon));
    const auto &rare=gearInfo(weaponVariant(family,Rarity::Rare));
    catalog&=uncommon.cooldown<common.cooldown&&uncommon.stress<common.stress&&rare.stress>common.stress;
  }
  check("All 36 weapons have stable IDs, native art, and rarity-specific stats",catalog);
  auto shotFixture=[&](Gear gear){auto s=base;equip(s,gear,RightHand);return s;};
  Input trigger;trigger.fire[RightHand]=true;
  auto worn=shotFixture(Gear::Rifle),tuned=worn;
  for(auto *session:{&worn,&tuned}){
    auto &sheet=session->member(1).sheet;
    const int copy=sheet.owned[size_t(Gear::Rifle)];
    recoverWeapon(sheet,Gear::Rifle,{-1,-1,-1});
    equipGear(sheet,RightHand,Gear::Rifle,copy);
  }
  auto &tunedPilot=tuned.member(1);
  tunedPilot.sheet.inventory[size_t(Gear::Scrap)]=10;
  const int copy=equippedWeaponCopy(tunedPilot.sheet,RightHand);
  tuneWeapon(tunedPilot.sheet,Gear::Rifle,copy,WeaponStat::Cycle);
  tuneWeapon(tunedPilot.sheet,Gear::Rifle,copy,WeaponStat::Heat);
  submit(worn,trigger);submit(tuned,trigger);
  check("Equipped copy tuning changes authoritative weapon cadence and reactor heat",
    worn.ordnance.size()==1&&tuned.ordnance.size()==1&&
    tunedPilot.body.cooldown[RightHand]<worn.member(1).body.cooldown[RightHand]&&
    tunedPilot.sheet.reactorStress<worn.member(1).sheet.reactorStress);
  auto scatter=shotFixture(Gear::ScattergunRare);submit(scatter,trigger);
  check("Rare scattergun fires seven distinct pellets on one trigger pull",
    scatter.ordnance.size()==7&&scatter.member(1).body.shots[1]==1&&
    angle(scatter.ordnance.front().body.velocity,scatter.ordnance.back().body.velocity)>.02f);
  {
    size_t portsChecked=0;float worstPortError=0;
    bool allBores=true;
    for(auto family:{Gear::Scattergun,Gear::RocketRack,Gear::SeekerPod})
      for(auto rarity:{Rarity::Common,Rarity::Uncommon,Rarity::Rare})for(int hand=0;hand<2;++hand){
        const auto gear=weaponVariant(family,rarity);
        const auto ports=weaponPorts(gear);
        auto s=base;equip(s,gear,hand);
        auto &pilot=s.member(1);Input fire;fire.fire[hand]=true;
        fire.handPoses[hand].aim=angles(.35f,.13f,.52f);
        unsigned seen=0;
        for(size_t volley=0;volley<ports.size();++volley){
          s.ordnance.clear();pilot.body.cooldown.fill(0);pilot.sheet.overheated=false;pilot.sheet.reactorStress=0;
          submit(s,fire);
          const auto &arm=pilot.body.arms[hand];
          const int rounds=weaponTuning(gear)->projectiles;
          allBores&=s.ordnance.size()==size_t(rounds);
          for(size_t n=0;n<s.ordnance.size();++n){
            const size_t port=(volley*rounds+n)%ports.size();
            auto off=ports[port];if(hand==LeftHand)off.x=-off.x;
            const Vec3 exit=arm.muzzle+rotate(arm.aim,off);
            worstPortError=std::max(worstPortError,length(s.ordnance[n].launchOrigin-exit));
            seen|=1u<<port;++portsChecked;
          }
        }
        allBores&=seen==(1u<<ports.size())-1;
      }
    check("Tilted and mirrored scatter, rack and seeker mounts launch from every authored bore",
          allBores&&portsChecked>0&&worstPortError<.001f,{{"ports_checked",portsChecked},{"maximum_error_m",worstPortError}});
  }
  auto burst=shotFixture(Gear::RifleRare);submit(burst,trigger);
  bool timedBurst=burst.ordnance.size()==1;
  for(int tick=0;tick<11;++tick)submit(burst,{});
  timedBurst&=burst.ordnance.size()==3&&burst.member(1).body.shots[1]==3&&burst.member(1).bursts[1].remaining==0;
  check("Rare rifle completes a time-based three-round burst after releasing the trigger",timedBurst);
  {
    auto cycle=shotFixture(Gear::RifleRare);
    unsigned releases=0;
    float worstError=0;
    bool restart=true;
    for(int tick=0;tick<14;++tick){
      const unsigned before=cycle.member(1).body.shots[RightHand];
      submit(cycle,tick==0?trigger:Input{});
      if(cycle.member(1).body.shots[RightHand]==before)continue;
      ++releases;
      restart&=cycle.view(1).rpgWeaponPhase[RightHand]<.001f;
      const float phase=cycle.view(1,{},.025f).rpgWeaponPhase[RightHand];
      restart&=phase>0;
      Session peer;peer.readSnapshot(cycle.snapshot());
      const auto observed=peer.view(1,{},.025f);
      const auto exterior=peer.view(0,{},.025f);
      worstError=std::max({worstError,std::abs(observed.rpgWeaponPhase[RightHand]-phase),
                          std::abs(exterior.mapPilots.at(0).rpgWeaponPhase[RightHand]-phase)});
    }
    for(int tick=0;tick<65;++tick)submit(cycle,{});
    check("Burst follow-ups restart the same animated mechanism on local and remote views",
          restart&&releases==3&&worstError<.00001f&&cycle.view(1).rpgWeaponPhase[RightHand]==0&&
          !cycle.snapshot().at("players").at(0).contains("weapon_cycles"));
  }
  {
    auto blades=shotFixture(Gear::Blade);
    auto &sheet=blades.member(1).sheet;
    const int index=sheet.owned[size_t(Gear::Blade)];
    recoverWeapon(sheet,Gear::Blade,{0,0,1});
    equipGear(sheet,RightHand,Gear::Blade,index);
    sheet.inventory[size_t(Gear::Scrap)]=10;
    tuneWeapon(sheet,Gear::Blade,index,WeaponStat::Handling);
    submit(blades,trigger);
    auto stow=Input{};
    stow.handPoses[RightHand].position={.35f,.05f,.25f};
    stow.ping[RightHand]=true;submit(blades,stow);
    Session observer;observer.readSnapshot(blades.snapshot());
    const auto remote=observer.view(0).mapPilots.at(0);
    check("Stowed melee mesh keeps the fitted copy's effective reach after replication",
          !remote.rpgStashedModels[RightHand].empty()&&remote.rpgWeaponHandling[RightHand]==2&&
          remote.rpgWeaponPhase[RightHand]==0&&
          std::abs(weaponStats(sheet,Gear::Blade,index).range/gearInfo(Gear::Blade).range-1.06f)<.001f);
  }
  auto lobber=shotFixture(Gear::GrenadeLobberRare);submit(lobber,trigger);
  check("Rare lobber delivers three arcing shells with bounded blast radius",
    lobber.ordnance.size()==3&&std::all_of(lobber.ordnance.begin(),lobber.ordnance.end(),[](const Shot &s){
      return s.body.kind==ProjectileKind::Bomb&&s.body.velocity.y>6.f&&s.body.radius>0.f&&s.body.life<=3.5f;
    }));
  auto lance=shotFixture(Gear::RailLance);submit(lance,trigger);
  const bool charges=lance.ordnance.empty()&&lance.member(1).lanceReady[1];
  auto cancelled=lance;
  for(int tick=0;tick<10;++tick)submit(cancelled,trigger);
  const float chargedPhase=cancelled.view(1).rpgWeaponPhase[RightHand];
  Session chargeObserver;chargeObserver.readSnapshot(cancelled.snapshot());
  submit(cancelled,{});
  check("Rail windup advances before discharge, replicates and cancels on release",
        chargedPhase>0&&chargedPhase<1.f/3&&cancelled.ordnance.empty()&&
        cancelled.view(1).rpgWeaponPhase[RightHand]==0&&
        std::abs(chargeObserver.view(1).rpgWeaponPhase[RightHand]-chargedPhase)<.00001f);
  for(int tick=0;tick<40;++tick)submit(lance,trigger);
  check("Rail lance winds up before releasing its high-speed projectile",
    charges&&lance.ordnance.size()==1&&length(lance.ordnance.front().body.velocity)>250.f&&
    lance.view(1).rpgWeaponPhase[RightHand]>=1.f/3);
  auto seeker=shotFixture(Gear::SeekerPod);submit(seeker,{});
  auto &locked=seeker.world.targets.front();
  const auto arm=seeker.member(1).body.arms[1];
  locked.center=arm.muzzle+arm.forward*80;locked.origin=locked.center;
  locked.half={4,4,4};locked.health=100;locked.active=true;locked.ai.boundsReady=false;
  submit(seeker,trigger);
  check("Seeker missile acquires the enemy under the tracked muzzle reticle",
    seeker.ordnance.size()==1&&seeker.ordnance.front().body.homingTarget==0);
  auto intercept=shotFixture(Gear::Rifle);
  Shot bullet,missile;
  bullet.body.position={10,25,-40};bullet.body.velocity={90,0,0};bullet.body.life=1;bullet.body.damage=4;bullet.body.shooter=1;
  missile.body.position={12,25,-40};missile.body.velocity={-90,0,0};missile.body.life=1;missile.body.damage=200;
  missile.body.kind=ProjectileKind::Rocket;missile.body.weapon=Weapon::Rocket;missile.body.hull=2;
  intercept.ordnance={bullet,missile};submit(intercept,{});
  check("An opposing round intercepts a rocket in flight without applying its warhead damage",
    intercept.ordnance.empty()&&intercept.member(1).sheet.hp==intercept.member(1).sheet.maxHp);
  int failed=0;for(const auto &c:checks)if(!c.at("passed").get<bool>())++failed;
  Json report={{"passed",failed==0},{"failed",failed},{"checks",checks}};
  if(argc==2){std::ofstream output(argv[1]);output<<report.dump(2)<<'\n';if(!output)throw std::runtime_error("Cannot write aim report");}
  std::cout<<report.dump(2)<<'\n';return failed?1:0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 2;}}
