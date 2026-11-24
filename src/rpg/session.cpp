#include "rpg/session.hpp"
#include "rpg/cockpit_arm_rest.hpp"
#include "rpg/weapon_ports.hpp"
#include "rpg/voidmaw_siege.hpp"
#include "rpg/mecha_ranged.hpp"
#include <utility>
#include "rpg/enemy_effects.hpp"
#include "rpg/weak_points.hpp"
#include "rpg/kaiju_audio.hpp"
#include "rpg/kaiju_audio.hpp"
#include "rpg/campaign_story.hpp"
#include "rpg/mission_board.hpp"
#include "rpg/manual_missions.hpp"
#include "rpg/manual_enemy_ai.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/enemy_animation.hpp"
#include "rpg/destruction.hpp"
#include "cockpit_glass.hpp"
#include "config.hpp"
#include "searchlight.hpp"
#include <stdexcept>
#include <future>
namespace iron::rpg {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Enemy, spec, hp, maxHp, tactic, cover,
                                   ambushed, decisionClock, fireClock, disabled,
                                   corrosion, corrosionOwner, regenClock, anchorX, anchorZ, transformed, spawned, manualClock, combined, group,boss,bossPhase,bossTell,heat,ventRemaining,overheated,injuries,missingArms,subsystemDamage)

static constexpr double lastWindSeconds=10, rescueSeconds=60, naniteHoldSeconds=10;
static constexpr double tickTolerance=1e-6;
static constexpr std::array<const char *,8> hullFaultNames={
  "SENSORS FRIED","ARM LOST","HOBBLED","TARGETING OFFLINE","COMMS MALFUNCTION",
  "SEVERE STRUCTURAL DAMAGE","MODULE DESTROYED","REACTOR FAILURE"
};

static Json point(Vec3 p) { return Json::array({p.x, p.y, p.z}); }
static Vec3 point(const Json &j) {
  auto a = j.get<std::array<float, 3>>();
  return {a[0], a[1], a[2]};
}
static Json gravityWire(const SurfaceGravity &state) {
  const auto q=state.frame;
  return Json::array({int(state.mode),q.x,q.y,q.z,q.w});
}
static SurfaceGravity readGravity(const Json &j) {
  const auto v=j.get<std::array<float,5>>();
  Quaternion frame{v[1],v[2],v[3],v[4]};
  if(!(v[0]>=1&&v[0]<=3)||std::floor(v[0])!=v[0]||!valid(frame))
    throw std::runtime_error("Invalid surface gravity orientation");
  SurfaceGravity state;
  state.mode=GravityMode(int(v[0]));state.frame=normalized(frame);
  return state;
}
CombatCue kaijuCall(const EnemySpec &spec) {
  // Rift and distant calls must carry to the ambient hearing radius.
  if(spec.traits&(Flying|Dismantler))return CombatCue::KaijuShriek;
  if(std::string_view(spec.id)=="kaiju_baron")return CombatCue::KaijuRoar;
  return spec.kind==EnemyKind::SmallKaiju?CombatCue::KaijuShriek:CombatCue::KaijuRoar;
}
CombatCue kaijuAttackCue(const EnemyAttack &attack) {
  const std::string_view name=attack.name;
  if(name=="Thundering Roar"||name=="Gravesong")return CombatCue::KaijuRoar;
  if(name=="Piercing Screech")return CombatCue::KaijuShriek;
  if(name=="Screeching Sonar")return CombatCue::KaijuSonar;
  if(name=="Maw Beam"||name=="Miniature Maw Beam")return CombatCue::KaijuDischarge;
  if(name=="Caustic Spit"||name=="Caustic Web")return CombatCue::KaijuSpray;
  if(name=="Dorsal Spines"||name=="Caustic Spines"||name=="Rocket Elbow")return CombatCue::KaijuSpines;
  if(name=="Rending Mandibles"||name=="Chittering Mandibles"||name=="Grave Mandibles")
    return CombatCue::KaijuMandibles;
  if(name=="Crushing Bite"||name=="Rending Maw"||name=="Consume"||name=="Consume Core")
    return CombatCue::KaijuBite;
  if(name=="Grapple"||name=="Coiling Tentacles"||name=="Bone Axe")return CombatCue::KaijuImpact;
  // Talons, claws and the barbed tail all strike across the target.
  return attack.range<=12?CombatCue::KaijuSwipe:CombatCue::KaijuSpray;
}
CombatCue kaijuWindupCue(const EnemyAttack &attack) {
  const std::string_view name=attack.name;
  if(name=="Gravesong")return CombatCue::KaijuSonar;
  if(name=="Caustic Spines"||name=="Dorsal Spines"||name=="Rocket Elbow")
    return CombatCue::KaijuMandibles;
  return CombatCue::KaijuHiss;
}
void emitKaijuSound(World &world,CombatCue cue,Vec3 origin) {
  if(world.sounds.size()<96)world.sounds.push_back({SoundEvent::Special,int(cue),origin});
}
static bool skitteringVoidmaw(const EnemySpec &spec) {
  return spec.kind==EnemyKind::SmallKaiju&&
      std::none_of(spec.attacks.begin(),spec.attacks.end(),[](const auto &a){return a.range>30;});
}
bool canBurrow(const EnemySpec &spec) {
  return std::string_view(spec.id)=="kaiju_tunnel_wurm"||skitteringVoidmaw(spec);
}
static bool meleeVoidmaw(const Enemy &enemy) {
  const auto &spec=combatSpec(enemy);
  return (spec.kind==EnemyKind::SmallKaiju||spec.kind==EnemyKind::LargeKaiju)&&
      !enemy.transformed&&(enemy.tactic==1||spec.optimal<=12)&&
      std::any_of(spec.attacks.begin(),spec.attacks.end(),[](const auto &a){return a.range<=12;});
}
// A mirrored heavy mount reserves a slot; only its owner actually holds gear.
static bool surfaceGravityAllowed(const Session &s,Vec3 position) {
  const auto location=s.mission.recipe.location;
  return s.biome==1||s.biome==2||s.biome==7||location==6||location==8||location==9||
         s.world.indoors(position);
}
static float gravityHeight(const Target &body) {
  return std::clamp(body.ai.localCenter.y+body.ai.localHalf.y,3.f,28.f);
}
static float gravityRadius(const Target &body) {
  return std::clamp(std::min(body.ai.localHalf.x,body.ai.localHalf.z),1.f,7.f);
}
static void resetChassisPower(Member &m){
  m.garrison=false;m.reinforced=false;m.berserkRetreat=0;
  m.chassisCooldown=0;m.chassisHeld=true;
  m.sheet.powerRouting=0;m.sheet.kineticCharge=false;
}
// A held rail windup is interrupted by losing control. The Buster quality can
// retain a charged shot between presses.
static void interruptWindup(Member &m,int hand){
  m.lanceReady[hand]=false;
  if(m.weaponCycles[hand].stage==WeaponCycle::Charge && !m.busterReady[hand])
    m.weaponCycles[hand]={};
}
static bool handOccupied(const Member &m,int hand) {
  return m.sheet.equipment[hand]!=Gear::Empty &&
         equipmentOwner(m.sheet,hand)==hand && !m.handStashed[hand];
}
static bool scannerGear(Gear gear){
  return gear==Gear::Scan||gear==Gear::LongRangeScanners;
}
static bool selectableFireMode(Gear gear){
  return weaponFamily(gear)==Gear::Rifle||weaponFamily(gear)==Gear::PulseCarbine;
}
static bool overShoulder(const HandPose &pose,int hand) {
  const auto p=pose.position;
  return pose.valid && p.x*(hand?1.f:-1.f)>.12f && std::abs(p.x)<.9f &&
         p.y>-.1f && p.y<.8f && p.z>.12f && p.z<.8f;
}
static float mobility(const Member &m,int refinement=0) {
  return std::clamp(float(m.sheet.attributes[1]-refinement)+(m.nodeMobility>0?1.f:0.f),3.f,24.f)*
      (m.mobilityDisrupt>0?.65f:1.f)*injuryMovementScale(m.injuries);
}
static bool pilotedMecha(const EnemySpec &spec) {
  return spec.kind==EnemyKind::SmallMecha||spec.kind==EnemyKind::LargeMecha;
}
static bool bladeGuard(const Enemy &enemy,const EnemySpec &spec){
  return !enemy.transformed&&enemy.missingArms!=3&&duelWeapon(spec);
}
static bool swimmingAt(const EnemySpec &spec,const World &world,int biome,Vec3 origin){
  return (spec.traits&Submersible)&&biome==2&&world.waterDepthAt(origin)>0;
}
static EnemyMotor motorFor(const EnemySpec &spec,bool transformed,bool swimming){
  if(spec.kind==EnemyKind::SmallKaiju||spec.kind==EnemyKind::LargeKaiju){
    if(spec.traits&Flying)return EnemyMotor::CreatureWing;
    if(swimming)return EnemyMotor::CreatureSwim;
    const std::string_view id=spec.id;
    if(spec.kind==EnemyKind::SmallKaiju||spec.traits&(Submersible|Dismantler|Carrier)||
       id=="kaiju_tunnel_wurm"||id=="kaiju_stalker"||id=="gravesong_sovereign")
      return EnemyMotor::CreatureCrawl;
    return EnemyMotor::Creature;
  }
  if(groundDriveTank(spec))return EnemyMotor::Tracked;
  if(transformed||spec.traits&(Flying|Hover|Starship))return EnemyMotor::Turbine;
  if(spec.traits&Submersible)return EnemyMotor::Submersible;
  if(pilotedMecha(spec)||spec.kind==EnemyKind::Labor)return EnemyMotor::Walker;
  return spec.kind==EnemyKind::Installation?EnemyMotor::Machinery:EnemyMotor::Tracked;
}
static void recharge(ThrusterState &drive,float capacity,float rate,float dt) {
  const float idle=std::max(0.f,dt-drive.remaining);
  drive.remaining=std::max(0.f,drive.remaining-dt);
  drive.charge=std::min(capacity,drive.charge+idle*rate);
}
static bool ignite(ThrusterState &drive,Vec3 velocity,float duration=.32f) {
  if(drive.remaining>0||drive.charge<1.f)return false;
  drive.charge-=1.f;
  drive.velocity=velocity;
  drive.remaining=drive.pulseDuration=duration;
  return true;
}

Session::Session(uint32_t value,bool networked) : dice{value}, networkGame(networked), seed(value) {
  campaign.active=true;campaign.seed=value;campaign.randomState=value;
  world.targets.clear();
  world.rpgPrototype = true;
}
void Session::log(const std::string &text) {
  events.push_back(text);
  if (events.size() > 8)
    events.erase(events.begin());
  ++sequence;
}
Member &Session::member(uint32_t id) {
  for (auto &m : members)
    if (m.id == id)
      return m;
  throw std::runtime_error("Unknown pilot");
}
void Session::add(uint32_t id, const std::string &name) {
  if (!id || members.size() >= 4 || phase != Phase::Hangar)
    throw std::runtime_error("Join requires a free lobby slot");
  for (auto &m : members)
    if (m.id == id)
      throw std::runtime_error("Duplicate pilot");
  Member m;
  m.id = id;
  m.name = validatePlayerName(name);
  Dice creation{0x4d454348u};
  m.sheet = createSheet(Chassis::Striker, PilotKind::Engineer, creation);
  m.body.playerSlot = unsigned(members.size());
  members.push_back(m);
  if (!host)
    host = id;
  log(m.name + " JOINED");
}
void Session::remove(uint32_t id) {
  for(size_t i=0;i<enemies.size();++i)
    if(enemies[i].grapplePilot==id)releaseVoidmawGrapple(i);
  const bool wasHost=host==id;
  const bool assembling=std::any_of(members.begin(),members.end(),[](const Member &m){return m.inSortie;});
  std::erase_if(members, [&](auto &m) { return m.id == id; });
  if(wasHost){
    host=members.empty()?0:members.front().id;
    if(host)campaign.standings=member(host).sheet.career.standings;
  }
  if(assembling&&phase==Phase::Briefing&&mission.mapIndex==0)phase=Phase::Hangar;
  for(auto &m:members){m.ready=false;if(wasHost)m.inSortie=false;}
  advanceCampaign();
  log("ROSTER CHANGED");
}
void Session::enterSortie(Member &pilot,bool enter){
  if(phase!=Phase::Hangar&&(phase!=Phase::Briefing||mission.mapIndex!=0||enter))
    throw std::runtime_error("The sortie room is available between missions");
  if(enter){
    if(!pilot.sheet.created||campaign.awaitingAdvance)
      throw std::runtime_error("Finish pilot preparation before entering the sortie room");
    if(pilot.id!=host&&!member(host).inSortie)
      throw std::runtime_error("Waiting for host to open the sortie room");
    if(!pilot.inSortie){pilot.inSortie=true;pilot.ready=false;}
    return;
  }
  if(!pilot.inSortie)return;
  pilot.inSortie=pilot.ready=false;
  if(phase==Phase::Briefing)phase=Phase::Hangar;
  if(pilot.id==host)for(auto &other:members)other.inSortie=other.ready=false;
}
void Session::playerCommand(uint32_t id,const Json &j){
  const auto op=j.at("op").get<std::string>();
  if(op=="sortie"){enterSortie(member(id),j.at("value").get<bool>());return;}
  if(op=="ready"&&phase==Phase::Briefing&&mission.mapIndex!=0)
    throw std::runtime_error("The party is already deployed");
  if(op=="ready"&&j.at("value").get<bool>()&&!member(id).inSortie)
    throw std::runtime_error("Enter the sortie room before readying up");
  if(op=="ready"&&!j.at("value").get<bool>()&&phase==Phase::Briefing&&
      mission.mapIndex==0&&member(id).inSortie){
    member(id).ready=false;phase=Phase::Hangar;return;
  }
  if(op=="deploy"&&phase==Phase::Briefing&&mission.mapIndex==0&&
      std::any_of(members.begin(),members.end(),[](const Member &m){return !m.inSortie||!m.ready;}))
    throw std::runtime_error("Every pilot must enter the sortie room and ready up");
  if(op=="settings"||op=="quality"||op=="rest"||op=="next")throw std::runtime_error("Mission parameters and equipment rewards are generated automatically");
  if(op=="equip"){int slot=j.at("slot").get<int>();auto gear=gearRead(j.at("gear").get<std::string>());auto &pilot=member(id);if(slot>=0&&slot<MountCount&&pilot.sheet.equipment[slot]==gear&&equipmentOwner(pilot.sheet,slot)==slot&&
      (j.value("copy",-1)==-1||equippedWeaponCopy(pilot.sheet,slot)==j.value("copy",-1)))return;}
  if(op=="reroll_missions"&&j.size()!=1)throw std::runtime_error("Regenerate the complete campaign without overrides");
  command(id,j);
}
void Session::command(uint32_t id, const Json &j) {
  auto &m = member(id);
  std::string op = j.at("op");
  if (op == "input") {
    if ((phase != Phase::Combat && phase != Phase::Complete) ||
        j.at("epoch").get<uint64_t>() != epoch)
      return;
    Input in = coop::inputRead(j.at("input"));
    if (j.contains("systems"))
      in.ping = j.at("systems").get<std::array<bool, 2>>();
    if(m.repairHold.target && (!in.focused || !in.headValid || !in.fire[m.repairHold.slot]))
      m.repairHold={};
    m.input = in;
    m.inputAge = 0;
    return;
  }
  if(op=="trade"){
    if(phase!=Phase::Hangar||campaign.awaitingAdvance)throw std::runtime_error("Trade post is available between missions after debrief");
    auto next=m.sheet;auto action=j.at("action").get<std::string>();
    if(action=="buy")purchase(next,gearRead(j.at("gear")));
    else if(action=="refill"){
      const int slot=j.at("slot").get<int>();
      if(slot<0||slot>=MountCount)throw std::runtime_error("Invalid mount");
      const auto gear=next.equipment[slot];
      if(!consumable(gear)||equipmentOwner(next,slot)!=slot||gear!=gearRead(j.at("gear")))
        throw std::runtime_error("Select an installed consumable to refill");
      purchase(next,gear);
    }
    else if(action=="sell")sellEquipment(next,gearRead(j.at("gear")),j.value("copy",-1));
    else if(action=="tune")tuneWeapon(next,gearRead(j.at("gear")),j.at("copy"),WeaponStat(j.at("stat").get<int>()),j.value("award",false));
    else if(action=="refine_pilot")refinePilot(next,PilotRefinement(j.at("stat").get<int>()),j.value("award",false));
    else if(action=="frame_upgrade")upgradeFrame(next,moduleRead(j.at("module")));
    else if(action=="craft")craftFromScrap(next,gearRead(j.at("gear")),j.value("count",1));
    else if(action=="buy_armor"){
      const auto fitted=next.armor;
      const auto stock=Armor(j.at("armor").get<int>());
      if(stock==Armor::None)throw std::runtime_error("Choose an armor set");
      purchaseArmor(next,stock);
      next.armor=fitted;
    }
    else if(action=="sell_armor")sellArmor(next,Armor(j.at("armor").get<int>()));
    else throw std::runtime_error("Unknown trade action");
    validate(next);prepare(next);m.sheet=std::move(next);m.ready=false;return;
  }
  if(op=="quality"){
    if(phase!=Phase::Hangar)throw std::runtime_error("Weapon refit requires lobby");
    auto next=m.sheet;equipQuality(next,j.at("slot"),Quality(j.at("quality").get<int>()));m.sheet=std::move(next);m.ready=false;return;
  }
  if(op=="equip"){
    if(phase!=Phase::Hangar&&phase!=Phase::Combat&&phase!=Phase::Complete)throw std::runtime_error("Equipment is unavailable");
    auto next=m.sheet;equipGear(next,j.at("slot").get<int>(),gearRead(j.at("gear").get<std::string>()),j.value("copy",-1));
    auto caps=next;prepare(caps);next.maxAp=caps.maxAp;next.ap=std::min(next.ap,next.maxAp);
    next.maxReactor=caps.maxReactor;next.reactor=std::min(next.reactor,next.maxReactor);
    for(int hand=0;hand<2;++hand)
      if(next.equipment[hand]!=m.sheet.equipment[hand]||next.heavyOwners[0]!=m.sheet.heavyOwners[0]||
         equippedWeaponCopy(next,hand)!=equippedWeaponCopy(m.sheet,hand))
        {m.handStashed[hand]=false;m.bursts[hand]={};m.lanceReady[hand]=false;m.busterReady[hand]=false;m.weaponCycles[hand]={};}
    for(int slot=LeftShoulder;slot<MountCount;++slot)
      if(next.equipment[slot]!=m.sheet.equipment[slot])m.activeScanners&=~(1u<<slot);
    m.sheet=std::move(next);m.blocked.fill(true);
    m.handSupported.fill(false);m.recoil.fill(0);
    m.stashGripHeld=m.input.ping;return;
  }
  if (op == "configure") {
    if (phase != Phase::Hangar && !(phase==Phase::Complete&&campaign.victorious&&!m.configured))
      throw std::runtime_error("Prepare in lobby only");
    Sheet candidate;
    if (j.contains("profile")) {
      candidate = j.at("profile").get<Sheet>();
      if(!j.at("profile").contains("career"))importLegacyCareer(candidate);
      retireObsoleteEquipment(candidate);
      validate(candidate);
      normalizeCombatProfile(candidate);
      prepare(candidate);
    } else {
      Dice creation{0x4d454348u};
      candidate = createSheet(Chassis(j.at("chassis").get<int>()),
                              PilotKind(j.at("pilot").get<int>()), creation,
                              1);
      if (j.contains("loadout"))
        customize(candidate, j.at("loadout"));
      retireObsoleteEquipment(candidate);
    }
    if(id==host&&(!m.configured||j.value("resume_campaign",false))){
      if(candidate.campaign.active){
        campaign=candidate.campaign;campaign.standings=candidate.career.standings;seed=campaign.seed;boardRevision=campaign.revision;dice.state=campaign.randomState;
        selectedMission=0;for(int slot=0;slot<9;++slot)if(!(campaign.completed&(1u<<slot))){selectedMission=slot;break;}
        for(auto &pilot:members)pilot.ready=false;
        if(campaign.victorious){phase=Phase::Complete;mission.recipe=campaignMission(*this,0);mission.recipe.enemies.clear();mission.recipe.tactics.clear();mission.contractReward=0;}
      }else if(j.value("resume_campaign",false)&&m.configured){
        ++boardRevision;campaign=restartCampaign(candidate.campaign);campaign.active=true;campaign.seed=seed;campaign.revision=boardRevision;selectedMission=0;
      }else inheritCampaignLegacy(campaign,candidate.campaign);
      campaign.standings=candidate.career.standings;
    }
    m.campaignAwarded=campaign.victorious&&candidate.lastCampaignAward==campaignAwardKey(campaign);
    if(!m.campaignAwarded)candidate.campaign=campaign;
    m.sheet=candidate;resetChassisPower(m);m.configured=true;m.configuration=j.value("configuration",m.configuration+1);
    m.debriefAccepted=m.campaignAwarded||(!campaign.victorious&&!campaign.awaitingAdvance);
    m.handStashed.fill(false);m.handSupported.fill(false);m.recoil.fill(0);
    if(j.contains("name"))m.name=validatePlayerName(j.at("name").get<std::string>());
    m.ready = false;
    checkpointCampaign();advanceCampaign();
    log(m.name + " PREPARED");
    return;
  }
  if(op=="debrief"){
    if(phase!=Phase::Complete&&!(phase==Phase::Hangar&&campaign.awaitingAdvance))throw std::runtime_error("Debrief is not available");
    if(m.debriefAccepted)return;
    if(campaign.victorious){
      if(m.campaignAwarded)return;
      m.sheet=claimCampaignAward(m.sheet,campaign,Chassis(j.value("chassis",int(m.sheet.chassis))));
      m.campaignAwarded=true;m.debriefAccepted=true;
      m.handStashed.fill(false);
      log(m.name+" / CAMPAIGN AWARD +1 / STARTER KIT RESTORED");return;
    }
    m.debriefAccepted=true;
    checkpointCampaign();advanceCampaign();
    log(m.name+" / DEBRIEF ACCEPTED");return;
  }
  if (op == "ready") {
    if(phase==Phase::Briefing&&!j.at("value").get<bool>()){phase=Phase::Hangar;for(auto &pilot:members)pilot.ready=false;return;}
    if (phase != Phase::Hangar)
      throw std::runtime_error("Ready in lobby only");
    if(j.at("value").get<bool>()&&(campaign.awaitingAdvance||campaign.victorious))
      throw std::runtime_error("The party must finish debrief first");
    if(j.at("value").get<bool>()&&!campaignFinale(campaign)&&(campaign.completed&(1u<<selectedMission)))throw std::runtime_error("Choose an uncleared mission");
    m.ready = j.at("value").get<bool>();
    if (std::all_of(members.begin(), members.end(),
                    [](auto &p) { return p.ready; }))
      prepareMission();
    return;
  }
  if (id != host)
    throw std::runtime_error("Host controls mission setup");
  if (op == "reroll_missions") {
    if (!canRegenerateCampaign(*this))
      throw std::runtime_error("Regenerate before starting the campaign; completed missions are permanent");
    ++boardRevision;
    campaign=restartCampaign(campaign);selectedMission=0;
    for(auto &p:members)p.inSortie=p.ready=false;
    checkpointCampaign();
    log("NEW NINE-MISSION CAMPAIGN GENERATED");
    return;
  }
  if (op == "select_mission") {
    if (phase != Phase::Hangar)
      throw std::runtime_error("Select a mission in the hangar");
    int slot = j.at("slot").get<int>();
    if (slot < 0 || slot >= 9)
      throw std::runtime_error("Mission slot must be 0..8");
    if(campaignFinale(campaign)&&slot!=0)throw std::runtime_error("Only the final operation is available");
    if(!campaignFinale(campaign)&&(campaign.completed&(1u<<slot)))throw std::runtime_error("Mission already cleared; choose another mission");
    selectedMission = slot;
    for (auto &p : members) p.inSortie=p.ready=false;
    log("MISSION " + std::to_string(slot + 1) + " SELECTED");
    return;
  }
  if (op == "settings") {
    if (phase != Phase::Hangar)
      throw std::runtime_error("Settings require lobby");
    auto candidate = j.at("settings").get<GameSettings>();
    validateSettings(candidate);
    settings = candidate;
    for (auto &p : members)
      p.ready = false;
    return;
  }
  if (op == "deploy") {
    if (phase != Phase::Briefing)
      throw std::runtime_error("Deploy from the mission briefing");
    phase = Phase::Combat;
    for(auto &pilot:members)pilot.inSortie=pilot.ready=false;
    clock = 0;
    mission.openerTime = 0;
    if (mission.recipe.opener == 1 || mission.recipe.opener == 4)
      for (auto &pilot : members) {
        pilot.sheet.hidden = true;
        pilot.hiddenRemaining = 6;
      }
    const auto entry=missionEntryPosition(world,0);
    const float arrival=sampleInsertion(openerKind(),0,world.waterDepthAt(entry)).duration;
    for(auto &pilot:members){
      if(mission.recipe.campaignSupply>=2){pilot.shield=std::min(6,1+mission.recipe.campaignSupply/2);pilot.shieldRemaining=arrival+20;}
      if(mission.recipe.campaignIntel>=2){pilot.sheet.hidden=true;pilot.hiddenRemaining=std::max(pilot.hiddenRemaining,arrival+std::min(8.f,2.f+mission.recipe.campaignIntel*.25f));}
    }
    stageOpener(0);
    return;
  }
  if ((op == "next" || op == "hangar") &&
      std::any_of(members.begin(), members.end(), [](const auto &p) {
        return p.body.lifeSpent && p.body.ejectionTime < 7;
      }))
    throw std::runtime_error("Wait for forced ejection to finish");
  if((op=="next"||op=="hangar")&&phase==Phase::Complete&&std::any_of(members.begin(),members.end(),[&](const Member &p){return (campaign.awaitingAdvance&&!p.debriefAccepted)||(campaign.victorious&&!p.campaignAwarded);}))throw std::runtime_error("Wait for every pilot to finish debrief");
  if (op == "next" && phase == Phase::Complete && campaign.victorious)
    op = "hangar";
  if (op == "next") {
    if (phase != Phase::Complete)
      throw std::runtime_error("Clear the mission first");
    ++missionNumber;
    for(int next=1;next<=9;++next)if(!(campaign.completed&(1u<<((selectedMission+next)%9)))){selectedMission=(selectedMission+next)%9;break;}
    prepareMission();
  } else if (op == "rest") {
    if (phase != Phase::Complete || salvage < 1)
      throw std::runtime_error("Rest requires cleared mission and salvage");
    --salvage;
    for (auto &p : members)
      if (!p.ejected) {
        p.sheet.rest(dice);
        p.sheet.repairKits=p.sheet.inventory[size_t(Gear::Repair)];
      }
    log("RESTED");
  } else if (op == "hangar") {
    if (phase == Phase::Combat)
      throw std::runtime_error("Finish the encounter first");
    if(campaign.victorious){++boardRevision;campaign=restartCampaign(member(host).sheet.campaign);selectedMission=0;for(auto &p:members){p.campaignAwarded=false;p.debriefAccepted=false;}checkpointCampaign();}
    if(phase!=Phase::Hangar)for(int slot=0;slot<9;++slot)if(!(campaign.completed&(1u<<slot))){selectedMission=slot;break;}
    phase = Phase::Hangar;
    for (auto &p : members) {
      p.ready = false;
      p.handStashed.fill(false);p.handSupported.fill(false);p.recoil.fill(0);
      p.activeScanners=0;p.nextSensorPulse=0;
      p.lastFault=0;p.faultNoticeUntil=0;p.criticalFaultRolled=false;p.emergency=HullEmergency::None;p.ejectAt=0;p.repairHold={};
      p.injuries = 0;
      p.missingArms = 0;
      p.body.rpgInjuries = p.body.rpgMissingArms = 0;
      p.body.rpgKnockdown={};p.body.gravity={};p.body.damageUntil={};
      p.ejected = false;
      p.body.lifeSpent = false;
      p.body.ejectionTime = 0;
      p.body.health = 100;
      p.body.missionStatus = MissionStatus::Range;
      resetChassisPower(p);prepare(p.sheet);
    }
  } else
    throw std::runtime_error("Unknown RPG command");
}
struct Session::SectorPreparation {
  uint64_t fromEpoch;
  int region;
  float radius;
  MissionRecipe recipe;
  struct Result { World world;std::vector<DungeonSite> sites; };
  std::shared_future<Result> result;
};
void Session::prefetchNextSector(){
  if(phase!=Phase::Combat||!mission.exitOpen||mission.mapIndex+1>=mission.mapCount||
     (nextSector&&nextSector->fromEpoch==epoch))return;
  auto recipe=campaignMission(*this,selectedMission,mission.mapIndex+1);
  recipe.opener=3;recipe.maps=mission.mapCount;recipe.rewardCredits=mission.contractReward;
  const float radius=recipe.radius>0?recipe.radius:settings.extent;
  const int region=settings.world;
  // Only the immutable recipe crosses threads. Rest rolls, enemy bookkeeping
  // and the epoch change still run exactly once on the simulation thread.
  auto task=std::async(std::launch::async,[recipe,region,radius]{
    SectorPreparation::Result out;
    buildMissionEnvironment(out.world,recipe.seed,recipe.environment,radius,recipe.enemies,
        recipe.location,recipe.objective,region,recipe.opener,recipe.planetoidFeature,recipe.siteCount,&out.sites,&recipe);
    return out;
  });
  nextSector=std::make_shared<SectorPreparation>(SectorPreparation{epoch,region,radius,std::move(recipe),task.share()});
}
void Session::loadSectorEnvironment(std::vector<DungeonSite> *sites){
  auto pending=std::move(nextSector);
  const auto &r=mission.recipe;
  if(pending&&pending->fromEpoch+1==epoch&&pending->region==settings.world&&pending->radius==missionRadius()
      &&environmentSeed==r.seed&&biome==r.environment&&Json(pending->recipe)==Json(r)){
    const auto& prepared=pending->result.get();
    world=prepared.world;
    if(sites)*sites=prepared.sites;
    return;
  }
  buildMissionEnvironment(world,environmentSeed,biome,missionRadius(),r.enemies,r.location,r.objective,
                          settings.world,r.opener,r.planetoidFeature,r.siteCount,sites,&r);
}
void Session::prepareMission(bool continuation) {
  validateSettings(settings);
  int previousReward=mission.contractReward,previousMaps=mission.mapCount,previousLosses=mission.structuresLost;
  const int previousEncounterCredits=mission.encounterCredits;
  int mapIndex=continuation?mission.mapIndex+1:0;
  auto previousCycle=environmentCycle;
  const auto offended=mission.offendedNeutral,withdrawn=mission.withdrawnAllies,lost=mission.lostAllies;
  const auto kills=mission.factionKills;const auto support=mission.supportedFactions;
  if(continuation)for(auto &m:members)if(!m.ejected)m.sheet.rest(dice);
  mission = MissionState{};
  weatherFront={};
  encounterClock=10;ambientWaves=0;ambientReserves.clear();ambientBounties.clear();npcRadioReady=routineRadioReady=0;radioSequence=0;radioRotation={};ambientKaijuReady=12;
  mission.mapIndex=mapIndex;
  mission.recipe=campaignMission(*this,selectedMission,mapIndex);
  if(continuation){
    mission.encounterCredits=previousEncounterCredits;
    mission.structuresLost=previousLosses;
    mission.offendedNeutral=offended;mission.withdrawnAllies=withdrawn;mission.lostAllies=lost;
    mission.factionKills=kills;mission.supportedFactions=support;
  }
  mission.mapCount=continuation?previousMaps:mission.recipe.maps;mission.recipe.maps=mission.mapCount;
  mission.contractReward=continuation?previousReward:mission.recipe.rewardCredits;
  mission.recipe.rewardCredits=mission.contractReward;
  if(continuation)mission.recipe.opener=3;
  mission.visited.clear();
  for(auto &m:members){m.thrusters={};m.body.rpgThrust={};m.funnels=m.shieldNodes=m.veriform=false;m.precog=m.nodeMobility=m.rangeExtension=m.immobile=m.mobilityDisrupt=m.systemDisrupt=0;m.precogTarget=-1;m.manualClock=0;m.busterReady.fill(false);m.beltTarget.fill(-1);}
  ++epoch;
  clock = 0;
  phase = Phase::Briefing;
  events.clear();
  enemies.clear();
  navigation.clear();
  tacticalDecisions.clear();factionContacts.clear();enemyMemory.clear();tacticalClock=0;navigationTurn=0;
  escort=EscortRoute{};
  traps.clear();
  supplies.clear();
  ordnance.clear();
  biome = mission.recipe.environment;
  terrain = environmentCatalog()[biome].name;
  difficult = environmentCatalog()[biome].speed < 1;
  environmentSeed = mission.recipe.seed;
  loadSectorEnvironment(&mission.sites);
  addMissionAssets(world, mission, missionRadius());
  for(size_t n=0;n<mission.sites.size();++n){
    auto &site=mission.sites[n];
    Dice cache{mission.recipe.seed^0x73696465u^uint32_t(n*0x9e3779b9u)};
    if(int(cache.next()%100)>=25+10*mission.recipe.difficulty)continue;
    std::vector<int> rooms;
    for(size_t i=1;i<site.rooms.size();++i)if(site.rooms[i].kind!=3)rooms.push_back(int(i));
    if(!rooms.empty())site.moduleCacheRoom=rooms[cache.next()%rooms.size()];
  }
  environmentCycle = EnvironmentCycle{};
  environmentCycle.seed = mission.recipe.seed;
  environmentCycle.startHour = mission.recipe.opener == 1 ? 22 : 8;
  if(continuation){environmentCycle=previousCycle;environmentCycle.advance(environmentCycle.daySeconds/24);}
  auto exit=missionNavigablePosition(world,{0,0,-missionRadius()*.65f},12);
  mission.exit={exit.x,exit.y,exit.z};
  if(mission.recipe.objective==2)
    prepareEscort({0,0,missionRadius()*.55f},exit);
  updateEnvironment();
  for (size_t i = 0; i < mission.recipe.enemies.size(); ++i) {
    int id = mission.recipe.enemies[i];
    const auto &spec=enemySpec(id);
    Enemy e;
    e.spec = id;
    if(i==0&&mission.recipe.boss>=0)e.boss=mission.recipe.boss;
    e.tactic = i<mission.recipe.tactics.size()?mission.recipe.tactics[i]:mission.recipe.tactic;
    e.hp = e.maxHp = e.boss>=0?int(std::lround(160*(1.f+.6f*(members.size()-1)))):enemySpec(id).hd * 8;
    e.fireClock =
        mission.recipe.opener == 5 ? 0 : 1 + float(dice.roll(6)) * .2f;
    if(e.boss>=0)e.fireClock=1.7f;
    auto &body = world.targets[i];
    body.health=float(e.hp);body.ai.maxHealth=float(e.maxHp);
    float actorDepth=(spec.traits&(Flying|Hover|Submersible))?-1.f:
                       (biome==2&&spec.kind!=EnemyKind::Conventional?-1.f:std::min(world.maxWadeDepth,body.ai.localHalf.y));
    if (mission.recipe.objective == 6 && !(spec.traits&Submersible)) {
      Vec3 desired=body.origin;desired.z=-missionRadius()*.65f+float(i%4)*24;desired.x=float(int(i/4)-int(mission.recipe.enemies.size()/8))*40;
      body.origin=missionNavigablePosition(world,desired,
                 std::max(body.half.x,body.half.z)+1,int(i),actorDepth);
      world.updateEnemyBounds(body);
    }
    if(mission.recipe.depotCount>=0&&mission.recipe.objective==9&&i==0&&!(spec.traits&Submersible)){
      // Stage the capture target in the operational area, away from the extraction beacon.
      body.origin=missionNavigablePosition(world,{0,0,-missionRadius()*.12f},std::max(body.half.x,body.half.z)+1,int(i),actorDepth);
      world.updateEnemyBounds(body);
    }else if (mission.recipe.opener == 5 && i == 0 && !(biome==2&&(spec.traits&Submersible))) {
      body.origin=missionNavigablePosition(world,{0,0,missionRadius()*.65f-40},
                 std::max(body.half.x,body.half.z)+1,int(i),actorDepth);
      world.updateEnemyBounds(body);
    }
    e.anchorX = body.origin.x;
    e.anchorZ = body.origin.z;
    e.burrowReady=canBurrow(combatSpec(e));
    enemies.push_back(e);
    if (e.tactic == 9) {
      Vec3 trap = body.origin +
                  unit(Vec3{0, 0, missionRadius() * .65f} - body.origin) * 20;
      trap.y = world.surfaceHeight(trap.x, trap.z);
      traps.push_back({trap, false});
    }
  }
  for (size_t i = 0; i < members.size(); ++i) {
    auto &m = members[i];
    Sheet sheet = m.sheet;
    uint32_t id = m.id,configuration=m.configuration;
    std::string name = m.name;
    unsigned injuries = m.injuries, missingArms=m.missingArms;
    bool ejected = m.ejected;
    const bool criticalFaultRolled=continuation&&m.criticalFaultRolled;
    const bool room=m.inSortie,ready=m.ready;
    float loadedRounds=m.explosive;
    auto stashed=m.handStashed;
    auto pending=std::move(m.pendingRecovery);
    const int depotCargo=m.pendingDepotSalvage;
    const bool fieldAmmo=m.fieldAmmo;
    const unsigned activeScanners=m.activeScanners;
    m = Member{};
    if(continuation){m.pendingDepotSalvage=depotCargo;m.explosive=loadedRounds;m.handStashed=stashed;m.pendingRecovery=std::move(pending);m.fieldAmmo=fieldAmmo;m.activeScanners=activeScanners;}
    m.id = id;
    m.name = name;
    m.inSortie=!continuation&&room;
    m.ready=m.inSortie&&ready;
    m.sheet = sheet;resetChassisPower(m);m.configured=true;m.configuration=configuration;
    m.injuries = injuries;
    m.missingArms = missingArms;
    m.ejected = ejected;
    m.criticalFaultRolled=criticalFaultRolled;
    m.body.playerSlot = unsigned(i);
    m.body.armRig=world.armRig;
    m.body.position = missionEntryPosition(world,unsigned(i));
    m.body.missionStatus = MissionStatus::Active;
    if (m.ejected) {
      // Lost pilots remain spectators until hangar repair; do not replay their
      // completed ejection when the surviving squad starts another mission.
      m.body.lifeSpent = true;
      m.body.ejectionTime = 7;
      m.body.health = 0;
      m.body.missionStatus = MissionStatus::Failed;
    }
    m.input.focused = false;
    m.blocked.fill(true);
    m.punchHeld.fill(true);
    m.sheet.targetLock = m.sheet.hidden = false;
  }
  checkpointCampaign();
  if(mission.recipe.manualMission>=0)log(friendlyChatter()[manualRandom(mission.recipe.seed,ManualRng::Chatter)%friendlyChatter().size()].name);
  planMissionEncounters(*this);
  spawnFactionDetachments();
  log(std::string("MISSION: ") +
      missionObjectives()[mission.recipe.objective].name);
}
InsertionKind Session::openerKind() const {
  Vec3 entry=missionEntryPosition(world,0);
  entry.y=world.surfaceHeight(entry.x,entry.z)+12;
  return insertionKindForOpener(mission.recipe.opener,world.indoors(entry),world.waterDepthAt(entry));
}
bool Session::insertionLocked() const {
  Vec3 entry=missionEntryPosition(world,0);
  return sampleInsertion(openerKind(),mission.openerTime,world.waterDepthAt(entry)).controlsLocked;
}
void Session::stageOpener(float dt) {
  float previous=mission.openerTime;
  mission.openerTime += dt;
  auto kind=openerKind();
  const auto insertion=sampleInsertion(kind,mission.openerTime);
  const bool ending=sampleInsertion(kind,previous).active&&!insertion.active;
  for (auto &m : members) {
    if (m.ejected || m.body.lifeSpent) continue;
    const float ground=world.surfaceHeight(m.body.position.x,m.body.position.z);
    if(!insertion.active&&!ending)continue;
    m.body.position.y=ground+insertion.altitude;
    if (kind==InsertionKind::Drop && insertionLandedBetween(kind,previous,mission.openerTime)) {
      // A landing is a single authoritative event, independent of rendering or
      // tick subdivision. Squadmates are exempt from one another's impact.
      constexpr float radius=15,damage=80;
      Vec3 impact=m.body.position+Vec3{0,2,0};
      blastScenery(impact,radius,damage);
      for (size_t i=0;i<enemies.size();++i) {
        const auto &t=world.targets[i];
        Vec3 nearest{std::clamp(impact.x,t.center.x-t.half.x,t.center.x+t.half.x),
                     std::clamp(impact.y,t.center.y-t.half.y,t.center.y+t.half.y),
                     std::clamp(impact.z,t.center.z-t.half.z,t.center.z+t.half.z)};
        if (length(nearest-impact)<=radius)hitEnemy(m,i,int(damage),Quality::None,true);
      }
      world.destruction(m.body.position,12,Weapon::Empty);
      world.sounds.push_back({SoundEvent::LargeExplosion,0,m.body.position});
    }
    if (kind==InsertionKind::Warp && previous<warpArrivalTime && mission.openerTime>=warpArrivalTime)
      world.sounds.push_back({SoundEvent::Shot,int(Weapon::Rail),m.body.bodyPoint(6)});
  }
}
void Session::stepMission(float dt) {
  if (phase != Phase::Combat)
    return;
  auto &state = mission;
  if(state.exitOpen){
    prefetchNextSector();
    Vec3 exit{state.exit[0],state.exit[1],state.exit[2]};
    bool all=std::all_of(members.begin(),members.end(),[&](const Member &m){return m.ejected?m.body.ejectionTime>=7:m.sheet.hp>0&&std::hypot(m.body.position.x-exit.x,m.body.position.z-exit.z)<=30;});
    state.transitTime=all?state.transitTime+dt:0;
    if(state.transitTime>=2){log("PARTY TRANSIT / SHORT REST");prepareMission(true);}
    return;
  }
  auto everyoneAtExtraction=[&]{
    return std::all_of(members.begin(),members.end(),[&](const Member &m){
      return m.ejected||m.sheet.hp<=0||
          std::hypot(m.body.position.x-world.missionEntry.x,m.body.position.z-world.missionEntry.z)<=30;
    });
  };
  auto unclaimed=[&]{
    return std::any_of(supplies.begin(),supplies.end(),[&](const SupplyDrop &drop){
      if(drop.kind==SafeBoxKind::FieldSupplies||drop.kind==SafeBoxKind::ModuleCache)return false;
      const auto mask=world.pickups[drop.pickup].collectedBy;
      return std::any_of(members.begin(),members.end(),[&](const Member &m){
        return !m.ejected&&m.sheet.hp>0&&!(mask&(1u<<m.body.playerSlot));
      });
    });
  };
  auto complete=[&]{
    phase=Phase::Complete;
    state.recoveryOpen=false;
    salvage+=2;
    for(auto &m:members){
      m.explosive=0;
      bankRecoveredGear(m);
      bankDepotSalvage(m);
      if(!state.recipe.finale){++m.sheet.missionsCompleted;m.sheet.credits=std::min(1000000,m.sheet.credits+state.contractReward+state.encounterCredits);}
      m.debriefAccepted=false;
      m.ongoing=0;
    }
    recordCampaignVictory();
    ordnance.clear();
    world.projectiles.clear();
    log("MISSION COMPLETE");
  };
  if(state.recoveryOpen){
    state.transitTime=everyoneAtExtraction()?state.transitTime+dt:0.f;
    if((!unclaimed()||state.transitTime>=5.f)&&std::none_of(members.begin(),members.end(),
        [](const Member &m){return m.recoveryPending();}))complete();
    return;
  }
  if(state.recipe.manualMission>=0){
    state.manualElapsed+=dt;state.manualPulse+=dt;
    if(state.recipe.countdownSeconds>0&&state.manualElapsed>=state.recipe.countdownSeconds){phase=Phase::Defeat;log("OPERATION WINDOW CLOSED");return;}
    if(!state.manualOpportunity&&state.manualElapsed>=20){
      state.manualOpportunity=true;
      for(auto &ally:members)if(!ally.ejected){
        switch(state.recipe.opportunity){
        case 0:ally.shield=std::max(ally.shield,3);ally.shieldRemaining=12;break;
        case 1:ally.precog=12;ally.precogTarget=enemies.empty()?-1:0;break;
        case 2:ally.sheet.coolant(1.f);break;
        case 3:for(auto &enemy:enemies)enemy.disabled=std::max(enemy.disabled,6.f);break;
        case 4:ally.sheet.inventory[size_t(Gear::VerilianBattery)]=std::min(99,ally.sheet.inventory[size_t(Gear::VerilianBattery)]+1);break;
        case 5:if(!enemies.empty()&&enemies[0].hp>0)blast(ally,world.targets[0].center,10,6,Quality::None);break;
        }
      }
    }
    if(state.recipe.rechargeSeconds>0&&state.manualPulse>=state.recipe.rechargeSeconds){
      state.manualPulse=0;
      switch(state.recipe.complication){
      case 0:{int spec=state.recipe.enemies.empty()?3:state.recipe.enemies[0];spawnManualEnemy(spec,{0,0,-missionRadius()*.5f},state.recipe.tactic);break;}
      case 1:for(auto &ally:members)ally.systemDisrupt=6;break;
      case 2:for(auto &ally:members)ally.mobilityDisrupt=6;break;
      case 3:for(auto &ally:members)if(!world.indoors(ally.body.position+Vec3{0,12,0}))ally.sheet.stress(dice,.5f);break;
      case 4:for(auto &ally:members)if(!world.indoors(ally.body.position+Vec3{0,12,0})){Shot warning;warning.body.position=ally.body.position+Vec3{0,60,0};warning.body.velocity={0,-12,0};warning.body.life=6;warning.body.damage=4;warning.body.radius=8;warning.body.shooter=-1;warning.body.weapon=Weapon::Rocket;launch(std::move(warning));}break;
      case 5:{int hd=1;for(auto &e:enemies)hd=std::max(hd,combatSpec(e).hd);int elite=3;for(size_t i=0;i<enemyCatalog().size();++i)if(enemySpec(i).hd<=std::min(10,hd+1)&&enemySpec(i).hd>enemySpec(elite).hd)elite=int(i);spawnManualEnemy(elite,{0,0,-missionRadius()*.55f},6);break;}
      }
    }
  }
  int objective = state.recipe.objective;
  int missionEnemies=0,defeated=0,liveHostiles=0;
  for(size_t i=0;i<enemies.size();++i){
    const auto &e=enemies[i];
    liveHostiles+=e.hp>0&&world.targets[i].active&&factionAttacksPlayers(*this,i);
    if(e.ambient||factionDetachment(*this,i))continue;
    ++missionEnemies;defeated+=e.hp<=0;
  }
  const bool allDead=defeated==missionEnemies;
  bool success = false;
  auto nearPlayer = [&](Vec3 p, float radius) {
    return std::any_of(members.begin(), members.end(), [&](auto &m) {
      return !m.ejected && m.sheet.hp > 0 &&
             length(m.body.position - p) <= radius;
    });
  };
  Vec3 extraction=world.missionEntry;
  extraction.y = world.surfaceHeight(extraction.x, extraction.z);
  if (objective == 0) {
    state.progress = float(defeated);
    success = allDead;
    if(state.recipe.boss>=0){
      // The original command guard must be defeated. Summoned waves and random arrivals are optional.
      missionEnemies=int(std::min(enemies.size(),state.recipe.enemies.size()));
      state.progress=float(std::count_if(enemies.begin(),enemies.begin()+missionEnemies,[](const auto &e){return e.hp<=0;}));
      success=missionEnemies>0&&int(state.progress)==missionEnemies;
    }
  } else if (objective == 2) {
    bool accompanied=false;
    for(int index:state.assets) {
      const auto &vehicle=world.targets[size_t(index)];
      if(vehicle.health<=0) {
        phase=Phase::Defeat;
        log("CONVOY VEHICLE LOST");
        return;
      }
      accompanied|=nearPlayer(vehicle.origin,85);
    }
    success=stepEscort(dt,accompanied);
  } else if (objective == 1 || objective == 8) {
    const auto &asset=world.targets[state.assets[0]];
    if(asset.health<=0) {
      phase=Phase::Defeat;
      log("MISSION ASSET LOST");
      return;
    }
    if(objective==1||nearPlayer(asset.origin,75))state.progress+=dt;
    success=state.progress>=objectiveDuration(state.recipe,objective==1?90.f:45.f);
  } else if (objective == 3 || objective == 4) {
    for (size_t i = 0; i < state.assets.size(); ++i)
      if (nearPlayer(world.targets[state.assets[i]].origin, 20)) {
        state.visited[i] = true;
        world.targets[state.assets[i]].active = false;
      }
    state.progress =
        float(std::count(state.visited.begin(), state.visited.end(), true));
    state.recovered = state.progress == float(state.assets.size());
    success = state.recovered && (objective == 3 || nearPlayer(extraction, 30));
  } else if (objective == 5) {
    state.progress = 0;
    for (int i : state.assets)
      if (world.targets[i].health <= 0)
        ++state.progress;
    success = state.progress == float(state.assets.size());
  } else if (objective == 6) {
    for (size_t i = 0; i < enemies.size(); ++i)
      if (!enemies[i].ambient && !factionDetachment(*this,i) && enemies[i].hp > 0 &&
          world.targets[i].origin.z > missionRadius() * .8f) {
        phase = Phase::Defeat;
        log("DEFENSE LINE BREACHED");
        return;
      }
    state.progress = float(defeated);
    success = allDead;
  } else if (objective == 7) {
    if (nearPlayer(world.targets[state.assets[0]].origin, 120))
      state.progress += dt;
    success = state.progress >= objectiveDuration(state.recipe,45) || allDead;
  } else if (objective == 9) {
    if (!state.recovered && enemies[0].hp <= enemies[0].maxHp / 4 &&
        nearPlayer(world.targets[0].origin, 15)) {
      state.recovered = true;
      enemies[0].hp = 0;
      world.targets[0].health = 0;world.targets[0].active=false;
      state.progress = 1;
    }
    success = state.recovered && nearPlayer(extraction, 30);
  }
  world.counters = {int(state.progress),
                    objective == 1                     ? int(objectiveDuration(state.recipe,90))
                    : objective == 7 || objective == 8 ? int(objectiveDuration(state.recipe,45))
                    : objective == 0 || objective == 6 ? missionEnemies
                    : objective == 9 ? 1
                                     : int(state.assets.size()),
                    liveHostiles};
  if (success) {
    if(std::any_of(members.begin(),members.end(),[](const Member &m){return m.recoveryPending();}))return;
    if(state.mapIndex+1<state.mapCount){state.exitOpen=true;state.transitTime=0;log("MAP CLEAR / WHOLE PARTY TO TRANSIT");return;}
    if(unclaimed()){
      state.recoveryOpen=true;state.transitTime=0.f;
      log("OBJECTIVE COMPLETE / RECOVER SAFE BOXES OR RETURN TO EXTRACTION");
    }else complete();
  }
}
void Session::updateEnvironment() {
  const auto sample = missionEnvironment(mission.recipe, environmentCycle);
  world.climate = missionClimate(biome, mission.recipe.location);
  world.environment = sample.settings;
  // The preceding front settles or melts during the next one. Derive it from
  // the same clock on late joins, map transfers and host snapshot restores.
  world.previousSurfaceWeather=Weather::Clear;
  if(environmentCycle.seconds>=environmentCycle.weatherSeconds){
    auto previous=environmentCycle;
    previous.seconds=(std::floor(previous.seconds/previous.weatherSeconds)-.5)*previous.weatherSeconds;
    world.previousSurfaceWeather=missionEnvironment(mission.recipe,previous).settings.weather;
  }
  world.environmentSeconds = sample.weatherSeconds;
  world.night = sample.settings.lighting == Lighting::Night;
}
void Session::eject(Member &m) {
  resetChassisPower(m);
  if (m.body.lifeSpent) return;
  for(size_t i=0;i<enemies.size();++i)
    if(enemies[i].grapplePilot==m.id)releaseVoidmawGrapple(i);
  m.body.rpgKnockdown={};m.body.gravity={};
  // Use the engine's forced ejection path: hatch, launch, spin and blast cues.
  m.ejected = true;
  m.emergency=HullEmergency::None;m.ejectAt=0;m.repairHold={};
  m.sheet.hp = 0;
  m.body.missionStatus = MissionStatus::Active;
  m.body.health = 100;
  world.damagePilot(m.body, 100000);
  log(m.name + " FORCED EJECTION");
}
int Session::absorbShield(Member &m,int amount){
  const int absorbed=std::min(m.shield,amount);
  if(absorbed<=0)return amount;
  m.shield-=absorbed;
  m.frameShieldDelay=std::max(m.frameShieldDelay,6.f);
  const auto point=m.body.bodyPoint(9);
  addEffect({WeaponEffect::Sparks,point,{},Weapon::Rail,0,2.f,rpgEnemyShieldHit});
  world.sounds.push_back({SoundEvent::Special,int(CombatCue::Shield),point});
  return amount-absorbed;
}
void Session::rollFault(Member &m,bool allowModuleLoss) {
  std::vector<int> available;
  for(int result=1;result<=(allowModuleLoss?7:6);++result){
    if(result==7 && std::none_of(m.sheet.modules.begin(),m.sheet.modules.end(),
          [](Module module){return !frameUpgrade(module);}))continue;
    if(result==2 ? m.missingArms!=3 : !(m.injuries&(1u<<(result-1))))
      available.push_back(result);
  }
  if(!available.empty())applyDisabledResult(m,available[dice.roll(int(available.size()))-1]);
}
void Session::startHullEmergency(Member &m) {
  resetChassisPower(m);
  const bool solo=!networkGame && mission.recipe.partySize==1 && members.size()==1;
  m.emergency=solo?HullEmergency::LastWind:HullEmergency::AwaitRepair;
  m.ejectAt=clock+(solo?lastWindSeconds:rescueSeconds);
  m.droneControl=false;
  m.body.health=0;m.repairHold={};
  for(size_t i=0;i<enemies.size();++i)
    if(enemies[i].grapplePilot==m.id)releaseVoidmawGrapple(i);
  if(!solo)rollFault(m,true);
  world.sounds.push_back({SoundEvent::Special,int(CombatCue::Failed),m.body.bodyPoint(6)});
  log(m.name+(solo?" / EJECTION IN 10 S / DESTROY A HOSTILE":" / DISABLED / NANITE RESCUE IN 60 S"));
}
Member &Session::naniteTarget(Member &user) {
  Member *target=&user;
  float nearest=11;
  const Vec3 aim=direction(user.body.aimYaw,user.body.aimPitch);
  for(auto &ally:members){
    if(ally.id==user.id||ally.ejected||ally.sheet.hp>=ally.sheet.maxHp)continue;
    const Vec3 delta=ally.body.bodyPoint(6)-user.body.bodyPoint(6);
    const float distance=length(delta);
    if(distance>=nearest||dot(unit(delta),aim)<=.6f)continue;
    const auto origin=user.body.bodyPoint(6);
    if(length(world.traceAim(origin,unit(delta),distance,false).position-origin)+.05f<distance)continue;
    target=&ally;nearest=distance;
  }
  return *target;
}
void Session::advanceRepair(Member &user,int slot) {
  auto &target=naniteTarget(user);
  if(target.emergency!=HullEmergency::AwaitRepair){
    if(user.repairHold.slot==slot){
      user.repairHold={};user.blocked[slot]=true;return;
    }
    if(target.sheet.hp>0 && target.sheet.hp<target.sheet.maxHp)
      useRepairNanites(user,target);
    return;
  }
  auto &hold=user.repairHold;
  if(!hold.target){hold={target.id,slot,clock};return;}
  if(hold.slot!=slot || hold.target!=target.id)return;
  if(clock-hold.started+tickTolerance<naniteHoldSeconds || hold.started+naniteHoldSeconds>target.ejectAt+tickTolerance)return;
  useRepairNanites(user,target);
  user.blocked[slot]=true;
  user.repairHold={};
}
void Session::useRepairNanites(Member &user,Member &target,bool emergency){
  auto &stock=user.sheet.inventory[size_t(Gear::Repair)];
  const auto &info=gearInfo(Gear::Repair);
  --stock;
  user.sheet.repairKits=stock;
  for(int mount=LeftShoulder;mount<MountCount;++mount)
    if(user.sheet.equipment[mount]==Gear::Repair)user.body.cooldown[mount]=info.cooldown;
  const bool rescue=target.emergency==HullEmergency::AwaitRepair;
  if(!emergency){
    user.sheet.stress(dice,info.stress);
    if(!rescue&&!injuryCheck(user,2))return;
  }
  if(rescue){
    target.emergency=HullEmergency::None;target.ejectAt=0;
    log(target.name+" / FIELD REPAIR COMPLETE");
  }
  target.sheet.hp=std::min(target.sheet.maxHp,target.sheet.hp+dice.roll(4)+1);
  target.body.health=100.f*target.sheet.hp/target.sheet.maxHp;
  if(rescue)target.faultNoticeUntil=clock+5;
  world.sounds.push_back({SoundEvent::Special,int(CombatCue::Repair),target.body.bodyPoint(7)});
}
void Session::takeDamage(Member &m, int amount, bool plasma, Vec3 towardSource,bool energy) {
  if (m.ejected || m.sheet.hp <= 0)
    return;
  amount = std::max(0, amount);
  if(energy||plasma)amount=absorbShield(m,amount);
  if(m.barricade&&!plasma&&amount>0)amount=int(std::lround(float(amount)*.5f/mountedModuleOutput(m.sheet,Gear::BarricadeMode)));
  if(m.garrison&&amount>0)amount=std::max(0,amount-dice.roll(4));
  const int durability=m.sheet.hp+m.sheet.ap;
  const int hull=m.sheet.hp;
  if (plasma)
    m.sheet.hp = std::max(0, m.sheet.hp - amount);
  else
    m.sheet.damage(amount);
  if(m.sheet.hp+m.sheet.ap<durability) {
    m.body.markDamage(towardSource,world.elapsed);
    world.sounds.push_back({SoundEvent::Damage,0,m.body.position});
  }
  if(phase==Phase::Combat && !m.criticalFaultRolled && hull*4>m.sheet.maxHp &&
     m.sheet.hp>0 && m.sheet.hp*4<=m.sheet.maxHp){
    m.criticalFaultRolled=true;
    rollFault(m,false);
  }
  if(phase==Phase::Combat&&m.sheet.hp>0&&m.sheet.hp<hull&&m.sheet.hp*4<=m.sheet.maxHp&&
     m.sheet.inventory[size_t(Gear::Repair)]>0){
    for(int slot=LeftShoulder;slot<MountCount;++slot){
      if(m.sheet.equipment[slot]!=Gear::Repair||m.body.cooldown[slot]>.000001f)continue;
      useRepairNanites(m,m,true);
      log(m.name+" / EMERGENCY NANITES ACTIVATED");
      break;
    }
  }
  if(m.sheet.hp==0)startHullEmergency(m);
}
void Session::applyDisabledResult(Member &m,int result) {
  if(result<1||result>8)throw std::runtime_error("Invalid disabled mecha result");
  m.injuries|=1u<<(result-1);
  m.lastFault=result;m.faultNoticeUntil=clock+5;
  log(m.name+" / "+hullFaultNames[result-1]);
  if(result==2) {
    unsigned hand=unsigned(dice.roll(2)-1);
    if(m.missingArms&(1u<<hand))hand=1-hand;
    m.missingArms|=1u<<hand;
  }
  if(result==4)m.sheet.targetLock=false;
  if(result==7) {
    auto mounted=std::find_if(m.sheet.equipment.rbegin(),m.sheet.equipment.rend(),
        [](Gear gear){return gearInfo(gear).module!=Module::Count;});
    auto installed=std::find_if(m.sheet.modules.rbegin(),m.sheet.modules.rend(),[](Module module){return !frameUpgrade(module);});
    if(mounted==m.sheet.equipment.rend()&&installed==m.sheet.modules.rend())return;
    const auto removed=mounted==m.sheet.equipment.rend()?*installed:gearInfo(*mounted).module;
    destroyModule(m.sheet,removed);
    if(removed==Module::SiegeMode)m.siege=false;
    if(removed==Module::BarricadeMode)m.barricade=false;
    if(removed==Module::VehicleMode)m.vehicle=false;
    if(removed==Module::BoosterSprint)m.boost=0;
    if(removed==Module::StaticCloud){m.sheet.hidden=false;m.hiddenRemaining=0;}
    if(removed==Module::FragMines)m.mine=false;
    if(removed==Module::SentryTurret||removed==Module::ParticleBarrier) {
      size_t offset=m.body.playerSlot*2+(removed==Module::SentryTurret?1:0);
      if(world.targets.size()>=8&&offset<8){auto &target=world.targets[world.targets.size()-8+offset];target.active=false;target.health=0;}
      if(removed==Module::SentryTurret)m.turret=false;
    }
    log(m.name+" / DESTROYED "+moduleName(removed));
  }
  m.body.rpgInjuries=m.injuries;m.body.rpgMissingArms=m.missingArms;
  for(int h=0;h<2;++h)if(!m.body.armAvailable(h)){
    m.body.arms[h].tracked=false;m.gestures[h].reset();m.body.fistFlash[h]=0;
  }
  if(result==8)eject(m);
}
void Session::hitEnemy(Member &m, size_t i, int damage, Quality quality,
                       bool isMelee) {
  auto &e = enemies.at(i);
  if (e.hp <= 0)
    return;
  if(damage>0)noteFactionAttack(i);
  auto &spec = combatSpec(e);
  int die = damageDie(m.sheet);
  if (quality == Quality::Deadly && damage < die / 2)
    damage = dice.roll(die);
  const bool energy=!isMelee&&(quality==Quality::Plasma||quality==Quality::Beam||quality==Quality::Arc);
  auto reduction = [&](int hit) {
    if(quality!=Quality::Plasma)hit-=spec.plating;
    if(energy){
      int shielding=e.transformed?4:spec.shield;
      if (shielding)
        for(int n=0;n<spec.shieldDice;++n)hit -= dice.roll(shielding);
    }
    return std::max(0, hit);
  };
  if(energy&&(spec.traits&ShieldWall)&&npcAllyInRange(*this,i,0,30))
    damage=std::max(0,damage-4);
  if(energy&&(spec.traits&(PairAegis|PairGladius))&&
     npcAllyInRange(*this,i,spec.traits&PairAegis?PairGladius:PairAegis,90))
    damage=std::max(0,damage-dice.roll(10));
  damage = reduction(damage);
  if(damage>0&&(quality==Quality::Thermal||quality==Quality::Plasma||quality==Quality::Arc))
    addReactorHeat(e.heat,e.overheated,e.ventRemaining,quality==Quality::Arc?.36f:.25f);
  if (quality == Quality::Chainblade) {
    damage += reduction(dice.roll(die));
    m.sheet.reactorTest(dice);
  }
  if (damage > 0 && quality == Quality::Arc) {
    e.disabled = 3;
    m.sheet.reactorTest(dice);
    const auto point=world.targets[i].center;
    addEffect({WeaponEffect::Sparks,point,unit(m.body.position-point),Weapon::Rail,0,2.2f,rpgWeaponArc});
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Arc),point});
  }
  if (damage > 0 && quality == Quality::Corrosive){
    e.corrosion=3;e.corrosionOwner=m.id;
  }
  e.hp = std::max(0, e.hp - damage);
  if (mission.recipe.objective == 9 && i == 0 && !mission.recovered &&
      e.hp <= e.maxHp / 4) {
    e.hp = std::max(1, e.hp);
    e.disabled = 99999;
  }
  world.targets[i].health = float(e.hp);
  damageVoidmawGrapple(i,damage);
  addEffect(
      {WeaponEffect::Impact, world.targets[i].center, {}, Weapon::Rifle, 0, 1});
  if(spec.acid)for(auto &ally:members)if(!ally.ejected&&ally.sheet.hp>0&&length(world.targets[i].origin-ally.body.position)<=10)takeDamage(ally,spec.acid,false,world.targets[i].origin-ally.body.position);
  if((spec.traits&ShockArmor)&&length(world.targets[i].origin-m.body.position)<=10)takeDamage(m,dice.roll(4),false,world.targets[i].origin-m.body.position);
  if (e.hp == 0){finishEnemyDeath(&m,i);noteFactionKill(i);}
  if(damage>0&&isMelee&&quality==Quality::Lodium&&dice.roll(20)==1)m.sheet.ap=std::min(m.sheet.maxAp,m.sheet.ap+1);
}
void Session::damageNpc(size_t i,int amount,AttackEffect effect,Quality quality){
  auto &e=enemies[i];
  if(e.hp<=0)return;
  const auto &spec=combatSpec(e);
  if(quality==Quality::Plasma||quality==Quality::Beam||quality==Quality::Arc||quality==Quality::Thermal){
    const int shield=e.transformed?4:spec.shield;
    if(shield&&amount>0){
      for(int die=0;die<spec.shieldDice;++die)amount-=dice.roll(shield);
      if(amount<=0)return;
    }
  }
  if(quality!=Quality::Plasma)amount-=spec.plating;
  if(effect==AttackEffect::Stun||effect==AttackEffect::Immobilize||effect==AttackEffect::DisruptMobility||
     effect==AttackEffect::DisruptSystems)e.disabled=std::max(e.disabled,1.2f);
  if(effect==AttackEffect::Reactor||effect==AttackEffect::DoubleReactor||quality==Quality::Thermal)
    addReactorHeat(e.heat,e.overheated,e.ventRemaining,effect==AttackEffect::DoubleReactor?.65f:.29f);
  if(amount<=0)return;
  e.hp=std::max(0,e.hp-amount);
  if(mission.recipe.objective==9&&i==0&&!mission.recovered&&e.hp<=e.maxHp/4){
    e.hp=std::max(1,e.hp);e.disabled=99999;
  }
  world.targets[i].health=float(e.hp);
  damageVoidmawGrapple(i,amount);
  if(e.hp==0)finishEnemyDeath(nullptr,i);
}
void Session::damageScenery(size_t index, float damage) {
  if (index < enemies.size() || index >= world.targets.size() ||
      !std::isfinite(damage) || damage <= 0) return;
  auto &target = world.targets[index];
  if (!target.active || target.invulnerable || target.health <= 0) return;
  target.rpgInitialHealth=std::max(target.rpgInitialHealth,target.health);
  target.health = std::max(0.f, target.health - damage);
  auto material=destructionMaterial(target.generatorAsset);
  if(target.health>0&&target.health<target.rpgInitialHealth*.5f&&
     (material==DestructionMaterial::Wood||volatileScenery(target.generatorAsset))&&world.waterDepthAt(target.origin)<1)target.rpgBurn=std::max(target.rpgBurn,8.f);
  if(target.health==0)beginDestruction(index);
}
void Session::crushFoot(Vec3 foot,Member *pilot) {
  for(size_t i=0;i<world.targets.size();++i){
    const auto &target=world.targets[i];
    if(!world.crushable(target)||!world.footContact(foot,target))continue;
    if(i>=enemies.size()){
      damageScenery(i,target.health);
    }else if(pilot&&enemies[i].boss<0){
      hitEnemy(*pilot,i,enemies[i].hp+64,Quality::Plasma,false);
    }
  }
}
static constexpr float voidmawCanopyApproach=.34f;
void Session::positionVoidmawGrapple(size_t i,const Member &pilot) {
  auto &body=world.targets[i];
  const auto &e=enemies[i];
  const auto fall=sampleKnockdown(pilot.body.rpgKnockdown,world.elapsed);
  const auto facing=pilot.body.gravityFacing();
  const auto cockpit=compose(facing,fall.orientation);
  auto window=voidmawCanopyContact(e.grappleSide);
  window.z-=.018f; // Place the biting edge just outside the glass.
  const Vec3 point=pilot.body.bodyPoint(12)+rotate(facing,fall.offset)+rotate(cockpit,window);
  const float phase=std::clamp((world.elapsed-e.grappleAt)/voidmawCanopyApproach,0.f,1.f);
  const float approach=phase*phase*(3-2*phase);
  // Belly against the cowling, weight below the jaws. Compose with the
  // cockpit's knockdown as well as its gravity frame on walls and ceilings.
  const auto hanging=angles(pi+e.grappleSide*voidmawHangYaw,voidmawHangPitch,
                            e.grappleSide*voidmawHangRoll);
  const auto local=compose(angles(pilot.body.facing()),compose(fall.orientation,hanging));
  const auto forward=rotate(local,{0,0,-1}),right=rotate(local,{1,0,0}),up=rotate(local,{0,1,0});
  const float landedYaw=std::atan2(-forward.x,-forward.z);
  body.yaw=wrap(e.grappleYaw+wrap(landedYaw-e.grappleYaw)*approach);
  body.ai.pitch=std::atan2(forward.y,std::hypot(forward.x,forward.z))*approach;
  body.ai.bank=std::atan2(right.y,up.y)*approach;
  body.ai.weaponYaw=body.yaw;body.ai.weaponPitch=0;
  body.ai.gravity=pilot.body.gravity;
  const auto pose=compose(body.ai.gravity.frame,angles(body.yaw,body.ai.pitch,body.ai.bank));
  const auto destination=point-rotate(compose(cockpit,hanging),e.grappleMuzzle);
  body.origin=e.grappleReturn+(destination-e.grappleReturn)*approach+
               pilot.body.gravity.up()*(1.6f*std::sin(pi*phase));
  body.center=body.origin+rotate(pose,body.ai.localCenter);
  body.half={};
  for(const auto axis:{rotate(pose,{body.ai.localHalf.x,0,0}),rotate(pose,{0,body.ai.localHalf.y,0}),rotate(pose,{0,0,body.ai.localHalf.z})})
    body.half=body.half+Vec3{std::abs(axis.x),std::abs(axis.y),std::abs(axis.z)};
  body.ai.velocity={};
}
bool Session::startVoidmawGrapple(size_t i,Member &pilot) {
  auto &e=enemies[i];
  auto &body=world.targets[i];
  if(e.grappleCooldown>0||pilot.ejected||pilot.sheet.hp<=0||pilot.body.lifeSpent||
     pilot.vehicle||pilot.vehicleBlend>.01f||
     std::any_of(enemies.begin(),enemies.end(),[&](const Enemy &other){return other.grapplePilot==pilot.id;}))return false;
  const auto gap=body.origin-pilot.body.position;
  const auto relative=rotate(gravityInverse(pilot.body.gravityFacing()),gap);
  const float horizontal=std::hypot(relative.x,relative.z);
  if(horizontal>std::clamp(std::max(body.ai.localHalf.x,body.ai.localHalf.z)+4.5f,6.f,13.f)||
     std::abs(relative.y)>std::max(7.f,body.ai.localHalf.y)||!enemyCanSeeMember(*this,i,pilot)||
     !world.enemyLineOfSight(body.center,pilot.body.bodyPoint(12),int(i),-1))return false;
  e.grappleSide=relative.x<0?-1.f:1.f;
  e.grappleReturn=body.origin;e.grapplePilot=pilot.id;e.grappleDamage=0;
  e.grappleAt=world.elapsed;e.grappleYaw=body.yaw;
  const auto center=body.ai.localCenter;
  e.grappleMuzzle={center.x,center.y,center.z-body.ai.localHalf.z};
  if(body.mesh){
    if(const auto *rig=enemyAnimationRig(originalAssetRoot()/"models",body.generatorAsset,body.mesh->size())){
      if(const auto socket=voidmawBiteSocket(*rig,body,e.grappleSide))
        e.grappleMuzzle=socket->atImpact;
    }
  }
  e.chargedAttack=-1;e.bossTell=0;e.fireClock=.9f;
  body.ai.rpgGrapplePilot=pilot.id;body.ai.airborne=true;
  body.ai.voidmawPose={VoidmawPose::Cling,world.elapsed,0,e.grappleSide};
  pilot.droneControl=false;
  pilot.thrusters.remaining=0;pilot.body.rpgThrust={};
  body.ai.combatMotion={};
  body.ai.muzzleFlash=0;
  positionVoidmawGrapple(i,pilot);
  emitKaijuSound(world,CombatCue::KaijuHiss,body.center);
  return true;
}
void Session::releaseVoidmawGrapple(size_t i) {
  auto &e=enemies[i];
  if(!e.grapplePilot)return;
  auto &body=world.targets[i];
  const auto pilot=std::find_if(members.begin(),members.end(),[&](const Member &m){return m.id==e.grapplePilot;});
  e.grapplePilot=0;e.grappleDamage=0;e.grappleCooldown=4.f;
  body.ai.rpgGrapplePilot=0;
  body.ai.voidmawPose={VoidmawPose::Recover,world.elapsed,.7f,e.grappleSide};
  body.ai.combatMotion.play(EnemyAction::Stagger,world.elapsed,.7f);
  body.ai.pitch=body.ai.bank=0;
  if(e.hp<=0){body.ai.gravity={};return;} // Let the existing corpse fall start at the canopy.
  e.disabled=std::max(e.disabled,.7f);
  if(body.ai.gravity.active()){
    body.ai.gravity.mode=GravityMode::Floating;
    body.ai.gravity.delay=.32f;
    body.ai.gravity.drift=body.ai.gravity.up()*3.5f;
    return;
  }
  body.ai.airborne=(combatSpec(e).traits&(Flying|Hover))!=0;
  const float radius=std::clamp(std::max(body.ai.localHalf.x,body.ai.localHalf.z),2.f,18.f);
  if(pilot!=members.end()){
    for(float angle:{0.f,.8f,-.8f,1.65f,-1.65f,pi}){
      auto p=pilot->body.position+direction(pilot->body.facing()+angle,0)*(radius+6.f);
      p.y=world.surfaceHeight(p.x,p.z);
      if(!insideArea(p,missionRadius()*.98f)||!clearPosition(world,p,radius,int(i))||
         (!body.ai.airborne&&world.waterDepthAt(p,radius)>world.waterTraversalLimitAt(p,radius)))continue;
      body.origin=p;
      world.updateEnemyBounds(body);
      return;
    }
  }
  body.origin=e.grappleReturn;
  world.updateEnemyBounds(body);
}
void Session::damageVoidmawGrapple(size_t i,int damage) {
  auto &enemy=enemies[i];
  if(!enemy.grapplePilot||enemy.hp<=0)return;
  enemy.grappleDamage+=damage;
  if(enemy.grappleDamage>=std::clamp((enemy.maxHp+5)/6,4,18)||enemy.disabled>60){
    emitKaijuSound(world,kaijuCall(combatSpec(enemy)),world.targets[i].center);
    releaseVoidmawGrapple(i);
  }
}
bool Session::startVoidmawBurrow(size_t i,const Member &pilot) {
  auto &enemy=enemies[i];
  auto &body=world.targets[i];
  if(!enemy.burrowReady||body.ai.airborne||body.ai.gravity.active()||pilot.body.rpgKnockdown.active(world.elapsed)||
     biome==1||biome==2||biome==7||mission.recipe.location==6||
     mission.recipe.location==7||mission.recipe.location==9)return false;
  const bool small=combatSpec(enemy).kind==EnemyKind::SmallKaiju;
  if(small&&biome!=0&&biome!=5&&biome!=6)return false;
  if(pilot.body.gravity.active()||pilot.vehicle||pilot.vehicleBlend>.01f)return false;
  Vec3 offset=body.origin-pilot.body.position;
  const float distance=std::hypot(offset.x,offset.z);
  if(std::abs(offset.y)>5.f||distance>(small?30.f:24.f)||(small&&distance<13.f)||
     world.indoors(body.center)||world.indoors(pilot.body.position+Vec3{0,8,0}))return false;
  offset.y=0;
  const float radius=std::clamp(std::min(body.ai.localHalf.x,body.ai.localHalf.z),1.5f,8.f);
  if(world.waterDepthAt(body.origin,radius)>1.f)return false;
  Vec3 exit;
  bool found=false;
  const float flank=i%2?-1.15f:1.15f;
  for(float angle:{small?flank:0.f,small?-flank:.4f,small?.65f:-.4f}){
    exit=pilot.body.position+rotate(angles(angle),unit(offset))*(small?6.5f:5.f);
    exit.y=world.surfaceHeight(exit.x,exit.z);
    if(!insideArea(exit,missionRadius()*.98f)||!clearPosition(world,exit,radius,int(i))||
       world.indoors(exit+Vec3{0,4,0})||world.waterDepthAt(exit,radius)>1.f||
       !world.enemyLineOfSight(exit+Vec3{0,3,0},pilot.body.bodyPoint(3),int(i),-1))continue;
    if(small&&!world.enemyLineOfSight(body.origin+Vec3{0,3,0},exit+Vec3{0,3,0},int(i),-1))continue;
    found=true;
    break;
  }
  if(!found)return false;
  enemy.burrowReady=false;enemy.burrowImpact=true;enemy.arrivalDelay=small?.82f:.62f;
  enemy.chargedAttack=-1;enemy.bossTell=0;
  emitKaijuSound(world,CombatCue::KaijuBurrow,body.origin);
  body.active=false;body.origin=exit;
  world.updateEnemyBounds(body);
  addEffect({WeaponEffect::Smoke,exit,{0,1,0},Weapon::Empty,0,small?3.6f:5.f,rpgArrivalBurrow});
  addEffect({WeaponEffect::Sparks,exit,{},Weapon::Empty,0,0,rpgRadarArrival});
  world.sounds.push_back({SoundEvent::Special,int(CombatCue::ArrivalBurrow),exit});
  return true;
}
void Session::voidmawEruption(size_t i) {
  const auto &body=world.targets[i];
  emitKaijuSound(world,CombatCue::KaijuBurrow,body.origin);
  const auto &spec=combatSpec(enemies[i]);
  const float radius=std::clamp(6.f+spec.hd*.38f,7.f,10.f);
  for(auto &pilot:members){
    const auto gap=pilot.body.position-body.origin;
    if(pilot.ejected||pilot.sheet.hp<=0||pilot.body.lifeSpent||pilot.body.rpgKnockdown.active(world.elapsed)||
       pilot.vehicleBlend>.01f||std::abs(gap.y)>4.f||std::hypot(gap.x,gap.z)>radius||
       !world.enemyLineOfSight(body.origin+Vec3{0,2,0},pilot.body.position+Vec3{0,3,0},int(i),-1))continue;
    const float side=rotate(angles(-pilot.body.facing()),gap).x;
    pilot.body.rpgKnockdown={world.elapsed,side<0?-1.f:1.f};
    pilot.droneControl=false;pilot.thrusters.remaining=0;pilot.body.rpgThrust={};
    pilot.body.walkingSpeed=0;
    takeDamage(pilot,std::clamp(spec.hd+2,3,9),false,body.origin-pilot.body.position);
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::MetalHit),pilot.body.position});
  }
}
void Session::moveRpgEnemy(size_t i,Vec3 displacement,float dt,bool airborne) {
  auto &target=world.targets[i];
  if(target.ai.fallSpeed>0&&!airborne)return;
  const auto &spec=combatSpec(enemies[i]);
  const bool crushing=!airborne&&!target.ai.gravity.active()&&(pilotedMecha(spec)||spec.kind==EnemyKind::LargeKaiju);
  const auto motor=motorFor(spec,enemies[i].transformed,airborne&&(spec.traits&Submersible));
  // Keep gait contacts that apply crushing damage even for the legless wurm.
  // Its movement sound comes from body friction along the ground.
  const bool footfalls=!airborne&&(motor==EnemyMotor::Walker||motor==EnemyMotor::Creature||
                                   (motor==EnemyMotor::CreatureCrawl&&!(spec.traits&Submersible)));
  const auto before=target.origin;
  const float phase=target.ai.gaitPhase;
  const bool attached=target.ai.gravity.mode==GravityMode::Attached;
  if(enemies[i].ambient&&enemies[i].airstrikeUntil<=0&&(i>=enemyMemory.size()||enemyMemory[i].remaining<=0)){
    const auto &e=enemies[i];
    const Vec3 offset{target.origin.x-e.anchorX,0,target.origin.z-e.anchorZ};
    const auto travel=Vec3{displacement.x,0,displacement.z};
    const auto desired=offset+travel;
    const float limit=std::max(30.f,length(offset));
    if(length(desired)>limit){
      const auto clamped=unit(desired)*limit-offset;
      displacement.x=clamped.x;displacement.z=clamped.z;
    }
  }
  if(attached){
    if(!moveOnGravity(world,target.ai.gravity,target.origin,displacement,gravityHeight(target),gravityRadius(target)))return;
    target.ai.velocity=(target.origin-before)*(1.f/dt);
    world.updateEnemyBounds(target);
  }else if(target.ai.gravity.active()||!world.moveEnemy(i,displacement,dt,airborne,crushing,groundDriveTank(spec)))return;
  if(!footfalls)return;
  auto motion=target.origin-before;
  const float travel=attached?length(motion):std::hypot(motion.x,motion.z);
  const auto actorFrame=compose(target.ai.gravity.frame,angles(target.yaw));
  const bool reverse=dot(motion,rotate(actorFrame,{0,0,-1}))<0;
  const float stride=travel*(100.f/96);
  if(attached)target.ai.gaitPhase=std::fmod(phase+(reverse?-stride:stride)+18.f,18.f);
  for(unsigned foot=0;foot<2;++foot){
    const float event=(foot?12.f:3.f)+(reverse?1.f:0.f);
    const float gap=std::fmod((reverse?phase-event:event-phase)+36.f,18.f);
    if((gap==0?18.f:gap)>stride)continue;
    const Vec3 contact=target.origin+
        rotate(actorFrame,{std::clamp(target.ai.localHalf.x*.6f,1.7f,3.5f)*(foot?-1.f:1.f),0,0});
    if(motor==EnemyMotor::Walker){
      if(world.sounds.size()<96)world.sounds.push_back({SoundEvent::Special,int(CombatCue::EnemyStep),contact});
    }else if(std::string_view(spec.id)!="kaiju_tunnel_wurm"){
      const auto cue=motor==EnemyMotor::CreatureCrawl?CombatCue::KaijuSkitter:CombatCue::KaijuTread;
      emitKaijuSound(world,cue,contact);
    }
    if(crushing)crushFoot(contact,nullptr);
    if(spec.kind==EnemyKind::LargeMecha||spec.kind==EnemyKind::LargeKaiju){
      for(auto &pilot:members)
        if(!pilot.ejected&&pilot.sheet.hp>0&&std::abs(pilot.body.position.y-contact.y)<1.4f&&
           std::hypot(pilot.body.position.x-contact.x,pilot.body.position.z-contact.z)<2.2f)
          takeDamage(pilot,std::max(2,spec.hd/2),false,target.origin-pilot.body.position);
    }
  }
}
static WeaponEffect landingWave(const World &world,Vec3 feet,float radius){
  const auto hit=gravityRay(world,feet+Vec3{0,.35f,0},feet-Vec3{0,.35f,0},true);
  const auto normal=hit?hit->normal:Vec3{0,1,0};
  return {WeaponEffect::Impact,feet+normal*.18f,normal,Weapon::Empty,0,radius,rpgLandingWave};
}
void Session::stepPilotFall(Member &m,float dt) {
  auto &feet=m.body.position;
  auto &drive=m.thrusters;
  if(!m.combatControls()||m.body.gravity.active()||zeroGravity()){
    drive.downSpeed=0;return;
  }
  const float floor=world.pilotSupportHeight(feet,.02f);
  drive.altitude=std::max(0.f,feet.y-floor);
  const float water=world.waterLevelAt(feet);
  const bool variableLift=feet.y<water||biome==7||mission.recipe.location==6;
  if(drive.liftRemaining>0||(variableLift&&drive.remaining>0&&drive.velocity.y>0)){
    drive.downSpeed=0;return;
  }
  const float impact=stepMechaDescent(feet,floor,drive.downSpeed,water,dt);
  drive.altitude=std::max(0.f,feet.y-floor);
  const float radius=landingWaveRadius(impact);
  if(radius==0)return;
  m.body.rpgLanding=std::clamp(impact/18.f,.22f,1.f);
  addEffect(landingWave(world,feet,radius));
  world.sounds.push_back({SoundEvent::Footstep,0,feet});
}
bool Session::boostMember(Member &m,const Input &in,bool movementAllowed,float dt) {
  auto &drive=m.thrusters;
  const float water=world.waterLevelAt(world.position);
  const bool flooded=world.position.y<water;
  const bool vacuum=zeroGravity()||biome==7||mission.recipe.location==6;
  const bool surfaceDrive=surfaceGravityAllowed(*this,world.bodyPoint(8))||world.gravity.active();
  const float floor=world.pilotSupportHeight(world.position);
  const float wetDepth=flooded?water-floor:0.f;
  const float altitude=std::max(0.f,world.position.y-floor);
  constexpr float bodyHeight=13.f, groundLiftHeight=bodyHeight*.5f, groundLiftDuration=.32f;
  const float ceiling=world.interiorCeilingAt(world.position+Vec3{0,bodyHeight-1.f,0},3.5f);
  const bool fixedLift=!vacuum&&!flooded&&!world.gravity.active();
  const float verticalLimit=fixedLift?groundLiftHeight:flooded?wetDepth:36.f;
  const float maxAltitude=std::max(0.f,std::min(verticalLimit,ceiling-floor-bodyHeight));
  const bool tilted=std::hypot(in.steer,in.forward)>.35f;
  const bool thrust=in.thrust&&tilted&&!drive.thrustHeld;
  const bool ascend=in.ascend&&!drive.ascendHeld;
  drive.thrustHeld=in.thrust&&(tilted||drive.thrustHeld);
  drive.ascendHeld=in.ascend;
  const float stat=mobility(m);
  const float unrefined=mobility(m,m.sheet.refinements[size_t(PilotRefinement::Mobility)]);
  const float cap=std::clamp(1.f+std::floor(unrefined/5.f),1.f,4.f);
  if(!movementAllowed)drive.remaining=drive.liftRemaining=0;
  recharge(drive,cap,.20f+.021f*stat,dt);
  const float speed=(23.f+.6f*stat)*(m.sheet.armor==Armor::Heavy?.88f:1.f);
  Vec3 launch{};
  bool combined=false;
  if(movementAllowed&&!m.sheet.overheated&&world.gravity.mode!=GravityMode::Pulling){
    const bool canLift=world.gravity.active()||(maxAltitude>1.f&&altitude<maxAltitude-.1f);
    const auto planar=rotate(compose(world.gravity.frame,angles(world.chassisYaw)),
                             unit(Vec3{in.steer,0,-in.forward}));
    combined=canLift&&in.ascend&&in.thrust&&tilted&&(thrust||ascend);
    if(combined)
      launch=planar*(1.7f*speed)+world.gravity.up()*(vacuum?speed*.65f:19.f);
    else if(ascend&&canLift)
      launch=world.gravity.up()*(vacuum?speed:std::min(speed,19.f));
    else if(thrust)
      launch=planar*speed;
  }
  const bool extend=combined&&drive.remaining>.12f&&!drive.combined;
  if(length(launch)>0&&(extend||ignite(drive,launch,combined?.56f:.32f))){
    if(extend){drive.velocity=launch;drive.remaining=drive.pulseDuration=.56f;}
    if(fixedLift&&ascend)
      drive.liftRemaining=groundLiftDuration*std::clamp((maxAltitude-altitude)/groundLiftHeight,0.f,1.f);
    drive.combined=combined;
    m.sheet.stress(dice,combined?(extend?.46f:.64f):.18f);
    addEffect({WeaponEffect::Sparks,world.position+Vec3{0,2,0},unit(launch),Weapon::Empty,0,1.6f,rpgEnemyBoost});
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Boost),world.position});
  }
  const bool active=drive.remaining>0;
  if(movementAllowed&&(active||drive.liftRemaining>0)){
    auto delta=active?drive.velocity*(dt*(.55f+.45f*drive.remaining/drive.pulseDuration)):Vec3{};
    if(active&&surfaceDrive&&gravityImpact(world,world.gravity,world.position,delta,13.f,3.5f)){
      drive.remaining=0;world.rpgLanding=.5f;
    }else if(world.gravity.active()){
      if(active&&dot(drive.velocity,world.gravity.up())>1&&world.gravity.mode==GravityMode::Attached){
        world.gravity.mode=GravityMode::Floating;
        world.gravity.delay=.5f;
        world.gravity.drift=drive.velocity;
      }else if(active){
        if(world.gravity.mode==GravityMode::Floating)world.gravity.drift=drive.velocity;
        else world.movePilot(world.position+delta);
      }
    }else{
      const float upwardTime=std::min(dt,drive.liftRemaining);
      drive.liftRemaining-=upwardTime;
      const float lift=fixedLift?(groundLiftHeight/groundLiftDuration)*upwardTime:std::max(0.f,delta.y);
      // The charged ascent is measured from takeoff. Crossing a ledge cannot
      // shorten it just because the support under the pilot moved farther down.
      const float room=fixedLift?ceiling-world.position.y-bodyHeight:maxAltitude-altitude;
      delta.y=std::min(lift,std::max(0.f,room));
      if(length(delta)>0)world.movePilot(world.position+delta);
      drive.altitude=std::max(0.f,world.position.y-world.pilotSupportHeight(world.position));
    }
  }
  world.rpgThrustCharge=drive.charge;
  world.rpgThrustCapacity=int(cap);
  world.rpgThrust=active?rotate(gravityInverse(world.gravityFacing()),drive.velocity)*(drive.remaining/(speed*drive.pulseDuration)):Vec3{};
  return active;
}
void Session::blastScenery(Vec3 center, float radius, float damage) {
  if (!std::isfinite(radius) || radius <= 0) return;
  for (size_t i = enemies.size(); i < world.targets.size(); ++i) {
    const auto &target = world.targets[i];
    Vec3 contact{std::clamp(center.x,target.center.x-target.half.x,target.center.x+target.half.x),
                 std::clamp(center.y,target.center.y-target.half.y,target.center.y+target.half.y),
                 std::clamp(center.z,target.center.z-target.half.z,target.center.z+target.half.z)};
    if (length(contact-center) <= radius) damageScenery(i,damage);
  }
}
void Session::blast(Member &m, Vec3 center, float radius, int damage,
                    Quality quality, bool self,int maxTargets,AttackEffect effect) {
  std::vector<size_t> candidates;for(size_t i=0;i<enemies.size();++i)if(enemies[i].hp>0&&length(world.targets[i].center-center)<=radius)candidates.push_back(i);
  std::stable_sort(candidates.begin(),candidates.end(),[&](size_t a,size_t b){return length(world.targets[a].center-center)<length(world.targets[b].center-center);});
  if(maxTargets>0&&candidates.size()>size_t(maxTargets))candidates.resize(size_t(maxTargets));
  for(size_t i:candidates){
    hitEnemy(m,i,damage,quality,false);
    if(effect==AttackEffect::Stun&&damage>0)enemies[i].disabled=std::max(enemies[i].disabled,3.f);
    if(effect==AttackEffect::Push){auto desired=world.targets[i].origin+unit(world.targets[i].origin-center)*20;if(clearPosition(world,desired,3,int(i))){world.targets[i].origin=desired;world.updateEnemyBounds(world.targets[i]);}}
  }
  for (auto &ally : members)
    if ((self || ally.id != m.id) &&
        length(ally.body.position + Vec3{0, 6, 0} - center) <= radius)
      takeDamage(ally,damage,quality==Quality::Plasma,center-ally.body.bodyPoint(6),
                 quality==Quality::Arc||quality==Quality::Beam);
  blastScenery(center,radius,float(damage));
}
bool Session::fire(Member &m, int slot, Vec3 origin, Vec3 ray, int damage,
                   Quality quality, float range, bool followup) {
  const auto gear=m.sheet.equipment[slot];
  const auto family=weaponFamily(gear);
  const auto *tuning=ranged(gear)?weaponTuning(gear):nullptr;
  const auto pattern=tuning?tuning->pattern:AttackPattern::Single;
  const bool precision=tuning&&selectableFireMode(gear)&&m.rifleSemi;
  const bool utilityArm=m.sheet.chassis==Chassis::Vanguard;
  const bool unsupported=slot<2&&tuning&&heavy(gear)&&!m.handSupported[slot]&&!utilityArm;
  const bool legacyEnergy=gear==Gear::ChestBeam||quality==Quality::Beam||quality==Quality::Buster;
  const auto visual=legacyEnergy?Weapon::Rail:tuning?tuning->visual:Weapon::Rifle;
  float cone=spreadRadians(m.sheet,gear,m.input.forward,m.blind>0||injuryDisadvantage(m.injuries,0,true));
  if(tuning)cone=std::hypot(cone,tuning->dispersion);
  if(slot<2&&m.sheet.has(Module::ShoulderHardpoint))cone*=.85f;
  if(m.systemDisrupt>0)cone*=1.7f;
  cone*=environmentCatalog()[biome].sensors;
  if(quality==Quality::Sniper&&std::abs(m.input.forward)<.1f)cone*=.35f;
  if(precision)cone*=.56f;
  const auto sight=world.traceAim(origin,ray,range);
  const float aimedRange=length(sight.position-origin);
  if(m.precog>0&&sight.target==m.precogTarget)cone*=.5f;
  if(sight.target>=0&&size_t(sight.target)<enemies.size()){
    auto &victim=enemies[size_t(sight.target)];
    const auto &spec=combatSpec(victim);
    cone*=victim.transformed&&aimedRange>30?2.f:manualEnemySpread(spec,aimedRange);
    if((spec.traits&Ambush)&&!victim.ambushed){cone*=2;victim.ambushed=true;}
  }
  if(utilityArm&&slot<2&&tuning&&heavy(gear)&&m.handSupported[slot])cone*=.7f;
  if(quality==Quality::Artillery&&aimedRange<30)cone*=2;
  if(quality==Quality::SawedOff&&aimedRange>30)cone*=2;
  if(pattern!=AttackPattern::Scatter)cone=std::clamp(cone,.001f,heavy(gear)?pi/60:pi/120);
  if(quality==Quality::Buster)cone=std::min(cone,.005f);
  if(tuning)cone*=weaponStats(m.sheet,gear,equippedWeaponCopy(m.sheet,slot)).spreadFactor;
  const auto aim=attackDirection(ray,m.body.facing(),slot<2?handYawLimit:headYawLimit);
  const Vec3 across=unit(cross(aim,Vec3{0,1,0}),{1,0,0});
  const Vec3 up=cross(across,aim);
  const auto ports=weaponPorts(gear);
  int count=tuning?tuning->projectiles:1;
  const bool burst=pattern==AttackPattern::Burst&&!precision;
  const int burstSize=burst?count:1;
  if(burst||precision)count=1;
  // Start a volley only if its simultaneous projectiles fit as a unit.
  if(ordnance.size()+size_t(count)>maxLiveProjectiles){
    std::erase_if(ordnance,[](const Shot &shot){return shot.body.life<=0;});
    if(ordnance.size()+size_t(count)>maxLiveProjectiles)return false;
  }
  bool fired=false;
  for(int n=0;n<count;++n){
    Shot shot;
    auto &body=shot.body;
    body.position=origin;
    if(slot<2 && !ports.empty()){
      // The mesh reflects X at the left wrist. Its port heights and the hand's
      // roll/pitch apply before the unchanged ballistic spread calculation.
      const size_t port=(size_t(m.body.shots[slot])*count+n)%ports.size();
      auto offset=ports[port];
      if(slot==LeftHand)offset.x=-offset.x;
      body.position=origin+rotate(m.body.arms[slot].aim,offset);
    }
    shot.launchOrigin=body.position;
    Vec3 trajectory=aim;
    if(pattern==AttackPattern::Scatter&&n){
      const float angle=float(n-1)*2*pi/float(count-1);
      trajectory=unit(aim+(across*std::cos(angle)+up*std::sin(angle))*cone);
    }else if(count>1)
      trajectory=unit(aim+across*((n-(count-1)*.5f)*.028f));
    trajectory=deviated(dice,trajectory,pattern==AttackPattern::Scatter?cone*.15f:cone);
    const float speed=tuning?tuning->velocity:180.f;
    body.velocity=trajectory*speed;
    body.damage=float(pattern==AttackPattern::Scatter?std::max(1,damage/count+(n<damage%count)):damage);
    body.mount=slot;body.weapon=visual;body.shooter=int(m.id);
    body.life=range/speed;
    shot.quality=quality;
    if(tuning){
      body.radius=tuning->blast;
      if(pattern==AttackPattern::Lob){
        body.kind=ProjectileKind::Bomb;
        body.velocity=body.velocity+up*tuning->lift;
        body.life=std::min(3.5f,body.life);
      }
      if(pattern==AttackPattern::Rocket||pattern==AttackPattern::Seek){
        body.kind=ProjectileKind::Rocket;
        body.hull=pattern==AttackPattern::Seek?3.f:2.f;
        if(pattern==AttackPattern::Seek&&sight.target>=0&&size_t(sight.target)<enemies.size())
          body.homingTarget=sight.target;
      }
      if(pattern==AttackPattern::Pulse||family==Gear::PulseCarbine)shot.quality=Quality::Plasma;
    }
    if(!launch(std::move(shot)))break;
    fired=true;
  }
  if(!fired)return false;
  if(slot<2 && weaponTuning(gear)){
    // Each shot in a burst restarts the mechanism, while simultaneous pellets
    // actuate the mount once. Charge winds up to frame 9 and releases from it.
    const bool charged=m.weaponCycles[slot].stage==WeaponCycle::Charge;
    const bool nextInBurst=burst && (!followup || m.bursts[slot].remaining>1);
    float stroke=nextInBurst?.075f:std::clamp(m.body.cooldown[slot],.075f,.6f);
    m.weaponCycles[slot]={world.elapsed,stroke,charged?WeaponCycle::Release:WeaponCycle::Stroke};
  }
  if(tuning){
    if(burst&&!followup)m.bursts[slot]={gear,quality,burstSize-1,damage,.075f};
    const bool missile=pattern==AttackPattern::Rocket||pattern==AttackPattern::Seek;
    const bool large=heavy(gear)||pattern==AttackPattern::Scatter;
    const unsigned fx=missile?rpgRocketIgnition:pattern==AttackPattern::Scatter?rpgScatterMuzzle:
        family==Gear::PulseCarbine?rpgPulseMuzzle:large?rpgWeaponHeavy:rpgWeaponMuzzle;
    addEffect({WeaponEffect::Sparks,origin+aim*.12f,aim,visual,0,large?1.35f:.65f,fx});
  }
  m.recoil[slot]=std::min(unsupported?.65f:.16f,m.recoil[slot]+
      (unsupported?.36f:pattern==AttackPattern::Scatter?.072f:heavy(gear)?.035f:.009f));
  m.sheet.targetLock=false;
  m.sheet.hidden=false;
  m.hiddenRemaining=0;
  ++m.body.shots[slot];
  if(family==Gear::Cannon||pattern==AttackPattern::Scatter)
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Cannon),origin});
  else if(family==Gear::PulseCarbine)
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Pulse),origin,Weapon::Rail});
  else world.sounds.push_back({SoundEvent::Shot,int(visual),origin});
  return true;
}
void Session::stepWeaponBurst(Member &m,int slot,float dt){
  auto &burst=m.bursts[slot];
  if(!burst.remaining)return;
  if(burst.gear!=m.sheet.equipment[slot]||!m.combatControls()||m.stun>0||
     !m.body.arms[slot].tracked||m.handStashed[slot]||m.vehicle||
     phase!=Phase::Combat||insertionLocked()){
    burst={};return;
  }
  burst.untilNext-=dt;
  while(burst.untilNext<=0&&burst.remaining>0){
    const auto &arm=m.body.arms[slot];
    const auto stats=weaponStats(m.sheet,burst.gear,equippedWeaponCopy(m.sheet,slot));
    fire(m,slot,arm.muzzle,arm.forward,burst.damage,burst.quality,stats.range,true);
    --burst.remaining;burst.untilNext+=.075f;
  }
}
bool Session::launch(Shot shot) {
  if(shot.body.shooter<0&&shot.body.owner>=0&&size_t(shot.body.owner)<enemies.size())
    shot.npcCanHitPilots=factionAttacksPlayers(*this,size_t(shot.body.owner));
  if (ordnance.size() >= maxLiveProjectiles) {
    std::erase_if(ordnance, [](const auto &s) { return s.body.life <= 0; });
    if (ordnance.size() >= maxLiveProjectiles) return false;
  }
  ordnance.push_back(std::move(shot));
  return true;
}
bool Session::addEffect(WeaponEffect effect) {
  if (!world.effect(effect.kind, effect.position, effect.direction, effect.weapon, effect.size))
    return false;
  world.effects.back() = std::move(effect);
  return true;
}
bool Session::injuryCheck(Member &m,int attribute,bool attack) {
  if(!injuryDisadvantage(m.injuries,attribute,attack))return true;
  if(test(dice,m.sheet,attribute,m.injuries,attack).success)return true;
  log(m.name+" / DAMAGED SYSTEM CHECK FAILED");return false;
}
void Session::stepChassis(Member &m,float dt){
  auto &s=m.sheet;
  if(phase!=Phase::Combat||insertionLocked()||m.ejected||s.hp<=0)return;
  const bool berserk=s.chassis==Chassis::Hybrid&&s.overheated;
  if(berserk){
    s.coolant(1.f);
    const auto reaction=dice.roll(8);
    if(reaction<=4)takeDamage(m,dice.roll(damageDie(s)));
    else if(reaction<=6)m.berserkRetreat=.85f;
    else{
      for(size_t i=0;i<enemies.size();++i)
        if(world.targets[i].active&&enemies[i].hp>0&&length(world.targets[i].center-m.body.bodyPoint(6))<=30)
          hitEnemy(m,i,dice.roll(4),Quality::None,false);
      world.sounds.push_back({SoundEvent::Special,int(CombatCue::KaijuShriek),m.body.bodyPoint(6)});
    }
  }
  if(!s.kineticCharge&&m.berserkRetreat<=0)return;
  const Vec3 origin=m.body.bodyPoint(7);
  int target=-1;
  float closest=s.kineticCharge?30.f:180.f;
  for(size_t i=0;i<enemies.size();++i){
    const auto &body=world.targets[i];
    const auto distance=length(body.center-origin);
    if(!body.active||enemies[i].hp<=0||distance>=closest||!factionAttacksPlayers(*this,i)||
       !world.enemyLineOfSight(origin,body.center,-1,int(i)))continue;
    closest=distance;target=int(i);
  }
  if(s.kineticCharge){
    s.kineticCharge=false;
    if(target>=0){
      hitEnemy(m,target,dice.roll(damageDie(s)),Quality::None,false);
      addEffect({WeaponEffect::Sparks,world.targets[target].center,{0,1,0},Weapon::Rail,0,1.6f,rpgWeaponHeavy});
    }
  }
  if(m.berserkRetreat>0){
    Vec3 away=target>=0?m.body.position-world.targets[target].origin:direction(m.body.facing()+pi,0);
    away.y=0;away=unit(away);
    const auto distance=std::min(dt,m.berserkRetreat)*22.f;
    const auto steps=std::max(1,int(std::ceil(distance/.5f)));
    for(int i=0;i<steps;++i){
      auto next=m.body.position+away*(distance/steps);
      next.y=world.walkableHeight(next);
      if(!insideArea(next,missionRadius()*.98f)||std::abs(next.y-m.body.position.y)>.6f||
         !world.groundPassable(next,3.5f)||!clearPosition(world,next,3.5f)||
         std::any_of(members.begin(),members.end(),[&](const Member &p){
           return p.id!=m.id&&!p.ejected&&length(next-p.body.position)<4&&length(next-p.body.position)<length(m.body.position-p.body.position);
         }))break;
      m.body.position=next;
    }
    m.berserkRetreat=std::max(0.f,m.berserkRetreat-dt);
  }
}
void Session::useChassis(Member &m) {
  auto &s = m.sheet;
  if (phase != Phase::Combat || insertionLocked() ||
      s.hp<=0 || (s.overheated&&s.chassis!=Chassis::Striker) || m.stun > 0 || m.chassisCooldown > 0)
    return;
  const int attribute=s.chassis==Chassis::Scout?1:
      (s.chassis==Chassis::Striker||s.chassis==Chassis::Hybrid)?3:s.chassis==Chassis::Brawler?0:2;
  if(!injuryCheck(m,attribute,s.chassis==Chassis::Brawler)){
    m.chassisCooldown=8;s.reactorTest(dice);return;
  }
  if (s.chassis == Chassis::Scout) {
    s.hidden = true;
    m.hiddenRemaining = 6;
  } else if (s.chassis == Chassis::Titan) {
    m.shield = dice.roll(4);
    m.shieldRemaining = 6;
  } else if (s.chassis == Chassis::Striker) {
    Member *other = nullptr;
    float nearest = 11;
    auto aim = direction(m.body.aimYaw, m.body.aimPitch);
    for (auto &p : members) {
      auto delta = p.body.position - m.body.position;
      if (p.id != m.id && !p.ejected && p.sheet.reactorStress>.1f &&
          length(delta) < nearest && dot(unit(delta), aim) > .5f) {
        nearest = length(delta);
        other = &p;
      }
    }
    if(other)other->sheet.coolant(.65f);
    else if(s.reactorStress>.15f)s.coolant(.48f);
    else return;
  } else if(s.chassis==Chassis::Auxiliary){
    Member *receiver=&m;
    float nearest=30.f;
    const auto aim=direction(m.body.aimYaw,m.body.aimPitch);
    for(auto &ally:members){
      auto delta=ally.body.position-m.body.position;
      if(ally.id==m.id||ally.ejected||ally.sheet.hp<=0||length(delta)>=nearest||dot(unit(delta),aim)<.5f)continue;
      receiver=&ally;nearest=length(delta);
    }
    const float transfer=receiver==&m?0.f:std::min(.30f,receiver->sheet.reactorStress);
    if(receiver->reinforced&&transfer<=0)return;
    receiver->reinforced=true;
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Ready),receiver->body.bodyPoint(7)});
    if(transfer>0){receiver->sheet.coolant(transfer);s.stress(dice,transfer);}
  } else if(s.chassis==Chassis::Colossus){
    m.garrison=!m.garrison;
    world.sounds.push_back({SoundEvent::Special,int(m.garrison?CombatCue::Shield:CombatCue::Ready),m.body.bodyPoint(6)});
    if(!m.garrison){m.chassisCooldown=.6f;return;}
  } else if(s.chassis==Chassis::Hybrid){
    const auto forward=direction(m.body.aimYaw,m.body.aimPitch);
    const auto origin=m.body.bodyPoint(10)+forward*2.f;
    Shot beam;beam.body.position=beam.launchOrigin=origin;
    const auto aim=deviated(dice,forward,.04f*12.f/std::max(3,s.attributes[3]));
    beam.body.velocity=aim*240.f;beam.body.life=.75f;beam.body.damage=dice.roll(damageDie(s));
    beam.body.shooter=int(m.id);beam.body.weapon=Weapon::Rail;beam.quality=Quality::Beam;
    if(!launch(beam))return;
    addEffect({WeaponEffect::Sparks,origin,aim,Weapon::Rail,0,2.f,rpgPulseMuzzle});
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Pulse),origin});
  } else if(s.chassis==Chassis::Vanguard){
    s.powerRouting=s.powerRouting==1?-1:1;
    world.sounds.push_back({SoundEvent::Special,int(s.powerRouting>0?CombatCue::Charge:CombatCue::Ready),m.body.bodyPoint(7)});
    m.chassisCooldown=.8f;
    return;
  } else {
    auto hit = world.traceAim(m.body.position + Vec3{0, 6, 0},
                              direction(m.body.aimYaw, 0), 30);
    if (hit.target < 0 || size_t(hit.target) >= enemies.size())
      return;
    auto destination = world.targets[hit.target].origin;
    Vec3 next = destination + unit(m.body.position - destination) * 9;
    if (!clearPosition(world, next, 2, hit.target))
      return;
    m.body.position = next;
    hitEnemy(m, size_t(hit.target), dice.roll(damageDie(s)),
             Quality::None, true);
  }
  m.chassisCooldown = 8;
  if(s.chassis!=Chassis::Striker)s.reactorTest(dice);
}
bool pilotRevealed(const Member &m){
  return searchlightActive(m.body)||(m.scanExposureRemaining>0&&m.sheet.hp>0&&!m.ejected&&
      !hasInjury(m.injuries,MechaInjury::Sensors));
}
void Session::stepSensors(Member &m,float dt){
  m.nextSensorPulse=std::max(0.f,m.nextSensorPulse-dt);
  bool standard=false;
  int longRangeSlot=-1;
  for(int slot=LeftShoulder;slot<MountCount;++slot){
    const auto gear=m.sheet.equipment[slot];
    if(!scannerGear(gear))m.activeScanners&=~(1u<<slot);
    if(!(m.activeScanners&(1u<<slot)))continue;
    standard|=gear==Gear::Scan;
    if(gear==Gear::LongRangeScanners)longRangeSlot=slot;
  }
  if((!standard&&longRangeSlot<0)||m.nextSensorPulse>0||phase!=Phase::Combat||insertionLocked()||
     m.weatherJam>0||hasInjury(m.injuries,MechaInjury::Sensors)||!m.sheet.canAct()||m.ejected)return;
  if(longRangeSlot>=0){
    const auto stats=weaponStats(m.sheet,Gear::LongRangeScanners,equippedWeaponCopy(m.sheet,longRangeSlot));
    m.sheet.stress(dice,stats.heat);
    m.nextSensorPulse=m.rangeExtension=stats.cooldown;
    scanSensors(m,5.f*stats.output,standard);
  }else{
    m.nextSensorPulse=6.f;
    scanSensors(m,4.f,true);
  }
}
void Session::scanSensors(Member &m,float duration,bool exposesPilot){
  m.scanRemaining=m.scanDuration=duration;
  if(exposesPilot)m.scanExposureRemaining=1.5f;
  if(++m.scanSweep==0)++m.scanSweep;
  m.scannedContacts.clear();m.scannedActors.clear();
  m.scannedBearings=0;
  for(size_t index=0;index<enemies.size();++index){
    const auto &target=world.targets[index];
    if(!target.active||target.escaped||enemies[index].hp<=0||target.health<=0)continue;
    if(const auto *side=factionDetachment(*this,index);
       side&&side->factionRole==FactionRole::Allied&&!factionAttacksPlayers(*this,index))continue;
    const Vec3 delta=target.center-m.body.position;
    if(std::hypot(delta.x,delta.z)<PassiveRadar::range){
      m.scannedContacts.push_back({target.center.x,target.center.y,target.center.z});
      m.scannedActors.push_back(index);
    }else{
      const float direction=std::atan2(delta.x,-delta.z)*PassiveRadar::sectors/(2*pi);
      const int bearing=(int(std::lround(direction))+PassiveRadar::sectors)%PassiveRadar::sectors;
      m.scannedBearings|=uint16_t(1u<<bearing);
    }
  }
}
void Session::activate(Member &m, int slot, bool punch) {
  auto &s = m.sheet;
  if(!punch&&scannerGear(s.equipment[slot])){
    if(phase==Phase::Combat&&!insertionLocked()&&!m.held[slot]&&s.hp>0&&!m.ejected){
      m.activeScanners^=1u<<slot;
      stepSensors(m,0);
    }
    return;
  }
  if (phase != Phase::Combat || insertionLocked() ||
      !m.combatControls() || (s.overheated&&(punch||s.equipment[slot]!=Gear::BoostKit)) || m.stun > 0 || m.ejected || m.body.cooldown[slot] > .000001f)
    return;
  if(slot<2&&!punch&&m.handStashed[equipmentOwner(s,slot)])return;
  if(punch&&handOccupied(m,slot)&&heavy(s.equipment[slot]))return;
  if(!punch&&equipmentOwner(s,slot)!=slot)return;
  Gear gear = punch ? Gear::Blade : s.equipment[slot];
  const auto *tuning=punch?nullptr:weaponTuning(gear);
  const bool precision=slot<2&&tuning&&selectableFireMode(gear)&&m.rifleSemi;
  if(precision&&m.held[slot])return;
  if(!punch&&!mountAllowed(gear,slot))return;
  if((m.vehicle||m.vehicleBlend>.01f)&&gear!=Gear::VehicleMode)return;
  if (gear == Gear::Empty || gear == Gear::Shield || gear == Gear::Comlink)
    return;
  if (slot < 2 &&
      (!m.body.arms[slot].tracked || !m.body.armAvailable(slot)))
    return;
  if(hasInjury(m.injuries,MechaInjury::LostArm)&&pairedWeapon(s.chassis,gear))return;
  if(consumable(gear)&&s.inventory[size_t(gear)]<=0)return;
  bool toggle = gear == Gear::SiegeMode || gear == Gear::BarricadeMode ||
                gear == Gear::VehicleMode;
  if (toggle && m.held[slot])
    return;
  if (m.vehicle && (melee(gear) || ranged(gear)))
    return;
  Vec3 origin = slot < 2
                    ? m.body.arms[slot].muzzle
                    : m.body.position + Vec3{slot % 2 ? 2.f : -2.f,
                                             slot >= LeftHip ? 6.f : 12.f, 0};
  if(slot>=2&&(m.body.gravity.active()||m.body.rpgKnockdown.active(world.elapsed))){
    const auto fall=sampleKnockdown(m.body.rpgKnockdown,world.elapsed);
    const auto seat=m.body.bodyPoint(12);
    const auto yaw=m.body.gravityFacing();
    const auto rotation=compose(compose(yaw,fall.orientation),angles(-m.body.facing()));
    origin=seat+rotate(yaw,fall.offset)+rotate(rotation,origin-m.body.position-Vec3{0,12,0});
  }
  Vec3 ray = slot < 2 ? m.body.arms[slot].forward
                      : direction(m.body.aimYaw, m.body.aimPitch);
  Quality quality = punch ? Quality::None : s.qualities[slot];
  const auto stats=weaponStats(s,gear,punch?-1:equippedWeaponCopy(s,slot));
  auto used = [&](bool reactor = false) {
    // Carry sub-frame cooldown debt only for a held action. A new press fires
    // immediately; sustained cadence stays stable at different simulation rates.
    float debt = m.held[slot] && !punch ? std::min(0.f, m.body.cooldown[slot]) : 0;
    m.body.cooldown[slot] = stats.cooldown * (quality == Quality::Quick ? .5f : quality==Quality::Automated?.8f:quality==Quality::BeltFed&&m.beltTarget[slot]>=0?.45f:1.f) + debt;
    if(pairedWeapon(s.chassis,gear))m.body.cooldown[slot^1]=m.body.cooldown[slot];
    s.stress(dice, stats.heat*(precision?.62f:1.f));
    if (reactor)
      s.reactorTest(dice);
    if (consumable(gear))
      --s.inventory[size_t(gear)];
  };
  if(activateManual(m,slot,gear,origin,ray))return;
  if (melee(gear) || ranged(gear)) {
    if(tuning&&tuning->windup>0&&!m.lanceReady[slot]){
      m.lanceReady[slot]=true;
      m.body.cooldown[slot]=tuning->windup;
      m.weaponCycles[slot]={world.elapsed,tuning->windup,WeaponCycle::Charge};
      if(pairedWeapon(s.chassis,gear))m.body.cooldown[slot^1]=tuning->windup;
      addEffect({WeaponEffect::Sparks,origin,ray,Weapon::Rail,0,tuning->windup*2.7f,rpgEnemyCharge});
      world.sounds.push_back({SoundEvent::Special,int(CombatCue::Charge),origin,tuning->visual});
      return;
    }
    if(quality==Quality::Buster&&!m.busterReady[slot]){
      s.reactorTest(dice);m.busterReady[slot]=true;m.body.cooldown[slot]=.8f;
      if(slot<2)m.weaponCycles[slot]={world.elapsed,.8f,WeaponCycle::Charge};
      addEffect({WeaponEffect::Sparks,origin,ray,Weapon::Rail,0,1.4f,rpgEnemyCharge});
      world.sounds.push_back({SoundEvent::Special,int(CombatCue::Charge),origin,tuning?tuning->visual:Weapon::Empty});
      return;
    }
    int die = damageDie(s);
    if ((punch && !s.has(Module::RocketFists)) || quality == Quality::Quick)
      die = die>=12?10:die>=10?8:die==8?6:4;
    int rawDamage=quality==Quality::Unstable||quality==Quality::BeltFed?dice.roll(die):(die+2)/2;
    int damage = rawDamage + int(precision) + (heavy(gear) ? 2 : 0) +
                 (m.siege && ranged(gear) ? 2 : 0) +
                 (m.explosive > 0 && ranged(gear) ? dice.roll(6) : 0);
    if(m.reinforced){damage+=dice.roll(6);m.reinforced=false;}
    if(m.garrison)damage+=dice.roll(4);
    if(tuning)damage+=tuning->damageBias;
    if(m.siege&&ranged(gear))damage=int(std::lround(damage*mountedModuleOutput(s,Gear::SiegeMode)));
    if(m.fieldAmmo&&ranged(gear))++damage;
    if(tuning&&tuning->pattern==AttackPattern::Scatter)damage+=3;
    if(tuning&&precision&&tuning->pattern==AttackPattern::Burst)damage+=2;
    damage=std::max(1,damage);
    if(quality==Quality::Beam||quality==Quality::Thermal||quality==Quality::Dual)++damage;
    if(quality==Quality::Buster){damage+=4;m.busterReady[slot]=false;s.targetLock=true;}
    if(quality==Quality::ScrapFed){auto &scrap=s.inventory[size_t(Gear::Scrap)];if(scrap>0){--scrap;damage+=3;}else if(s.ap>0){--s.ap;damage+=3;}}
    if(quality==Quality::Unstable){if(rawDamage==die){int bonus=dice.roll(die);damage+=bonus;if(bonus==1)takeDamage(m,die);}if(rawDamage==1)takeDamage(m,die);}
    if (s.hidden && s.chassis == Chassis::Scout)
      damage += dice.roll(6);
    used();
    if(quality==Quality::BeltFed&&m.beltTarget[slot]>=0){auto hit=world.traceAim(origin,ray,stats.range);if(hit.target!=m.beltTarget[slot]){m.beltTarget[slot]=-1;m.body.cooldown[slot]=3;return;}}
    if (ranged(gear) || quality == Quality::Thrown) {
      if(slot<2)m.lanceReady[slot]=false;
      if(fire(m, slot, origin, ray, damage, quality,
           quality == Quality::Thrown ? 30 : stats.range))ordnance.back().feedMisfire=quality==Quality::BeltFed&&rawDamage==1;
      else if(slot<2 && m.weaponCycles[slot].stage==WeaponCycle::Charge)m.weaponCycles[slot]={};
    } else {
      s.targetLock=false;
      if(heavy(gear)&&slot<2&&!m.heavySupported[0]&&s.chassis!=Chassis::Vanguard)m.recoil[slot]=.36f;
      auto hit = world.traceAim(origin, ray, tuning?stats.range:10);
      ++m.body.punches[slot];
      m.body.fistFlash[slot] = .3f;
      if(!punch)m.weaponCycles[slot]={world.elapsed,std::clamp(m.body.cooldown[slot],.075f,.6f),
                        quality==Quality::Buster?WeaponCycle::Release:WeaponCycle::Stroke};
      addEffect({WeaponEffect::Sparks,origin+ray*.8f,ray,Weapon::Empty,0,weaponFamily(gear)==Gear::ChainCutter?1.5f:1.2f,weaponFamily(gear)==Gear::ChainCutter?rpgCutterSparks:rpgEnemyMelee});
      world.sounds.push_back({weaponFamily(gear)==Gear::ChainCutter?SoundEvent::CutterMotor:SoundEvent::Punch,0,origin,punch?Weapon::Empty:Weapon::Cutter});
      if(!injuryCheck(m,0,true))return;
      if (hit.target >= 0) {
        if (size_t(hit.target) < enemies.size())
          {
           auto &defender=enemies[size_t(hit.target)];
           auto &actor=world.targets[size_t(hit.target)];
           auto &motion=actor.ai.combatMotion;
           const bool riposte=m.riposteRemaining>0&&m.riposteTarget==hit.target;
           // A long ready stance can hold guard throughout a weapon cooldown.
           // The time to raise/lower the guarding arm stays the same.
           const float guardAge=world.elapsed-motion.started;
           const float guardEdge=std::min(.08f,motion.duration*.18f);
           if(!riposte&&defender.disabled<=0&&!defender.overheated&&
              motion.action==EnemyAction::Guard&&guardAge>=guardEdge&&guardAge<=motion.duration-guardEdge&&
              bladeGuard(defender,combatSpec(defender))&&
              dot(unit(m.body.position-actor.origin),rotate(actor.ai.gravity.frame,direction(actor.yaw,0)))>.55f){
             motion.play(EnemyAction::Parry,world.elapsed,.40f,motion.technique);
             defender.fireClock=std::max(.43f,defender.fireClock);
             m.comboCount=0;m.comboRemaining=0;s.hidden=false;
             addEffect({WeaponEffect::Sparks,hit.position,ray*-1.f,Weapon::Rail,0,1.8f,rpgEnemyShieldHit});
             world.sounds.push_back({SoundEvent::Special,int(CombatCue::MetalHit),hit.position});
             return;
           }
           const bool alternating=slot!=m.comboHand||
              (length(m.swingMotion)>.01f&&dot(unit(m.swingMotion),unit(m.lastMeleeMotion))<-.25f);
           if(m.comboRemaining>0&&m.comboTarget==hit.target&&alternating)
             m.comboCount=std::min(3,m.comboCount+1);
           else m.comboCount=1;
           m.comboTarget=hit.target;m.comboHand=slot;m.comboRemaining=1.45f;
           m.lastMeleeMotion=m.swingMotion;
           if(riposte)m.riposteRemaining=0;
           damage+=m.comboCount-1+(m.comboCount==3?2:0)+(riposte?5:0);
           if(m.comboCount==3||riposte)enemies[size_t(hit.target)].disabled=std::max(.65f,enemies[size_t(hit.target)].disabled);
           hitEnemy(m, size_t(hit.target), damage, quality, true);
          if(punch&&s.has(Module::PistonFists)&&m.pistonCooldown<=0){hitEnemy(m,size_t(hit.target),dice.roll(die)+2,Quality::None,true);s.reactorTest(dice);m.pistonCooldown=3;}}
        else damageScenery(size_t(hit.target),float(damage));
      }
      s.hidden = false;
    }
    return;
  }
  const float baseRange=gearInfo(gear).range;
  const float rangeFactor=baseRange>0?stats.range/baseRange:1.f;
  auto reach=[&](float range){return rangeFactor*(m.rangeExtension>0?(range<=10?30.f:range<=30?90.f:180.f):range);};
  switch (gear) {
  case Gear::Repair:
    if(s.hp>0)advanceRepair(m,slot);
    break;
  case Gear::NanoShroud:
    if (s.ap >= s.maxAp)
      return;
    used(true);
    if(!injuryCheck(m,2))break;
    s.ap = std::min(s.maxAp, s.ap + int(std::lround(dice.roll(4)*stats.output)));
    break;
  case Gear::BoostKit:
    used();
    s.ap = std::min(s.maxAp, s.ap + dice.roll(4));
    s.coolant(.75f);
    m.thrusters.charge+=1.f;
    break;
  case Gear::ExplosiveRounds:
    if(m.explosive<=0){used();m.explosive=1;}
    break;
  case Gear::SiegeMode:
    used(!m.siege);
    m.siege = !m.siege;
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Motor),origin});
    break;
  case Gear::BarricadeMode:
    used(!m.barricade);
    m.barricade = !m.barricade;
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Motor),origin});
    break;
  case Gear::VehicleMode:
    if(m.body.gravity.active()||m.body.rpgKnockdown.active(world.elapsed)||std::any_of(enemies.begin(),enemies.end(),
       [&](const auto &enemy){return enemy.grapplePilot==m.id;}))break;
    used(!m.vehicle);
    m.vehicle = !m.vehicle;
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Motor),origin});
    if(m.vehicle){m.siege=m.barricade=false;m.droneControl=false;}
    break;
  case Gear::StaticCloud:
    used(true);
    if(!injuryCheck(m,1))break;
    blast(m, m.body.position + Vec3{0, 6, 0}, reach(10), dice.roll(4), Quality::None,false,2);
    addEffect({WeaponEffect::Sparks,m.body.position+Vec3{0,5,0},ray,Weapon::Rail,0,4.f,rpgWeaponArc});
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Arc),m.body.position});
    s.hidden = true;
    m.hiddenRemaining = 6;
    break;
  case Gear::QuakeGenerator:
    used(true);
    if(!injuryCheck(m,3,true))break;
    blast(m, m.body.position + Vec3{0, 6, 0}, reach(10), dice.roll(6), Quality::None,false,2,AttackEffect::Push);
    addEffect({WeaponEffect::Sparks,m.body.position,ray,Weapon::Empty,0,reach(10),rpgWeaponPulse});
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Pulse),m.body.position});
    break;
  case Gear::ParticleBarrier: {
    size_t index = world.targets.size() - 8 + m.body.playerSlot * 2;
    auto &barrier = world.targets[index];
    if(barrier.active&&barrier.health>0){used();barrier.active=false;barrier.health=0;break;}
    used(true);
    barrier.active = true;
    barrier.health = 20;
    barrier.origin = m.body.position + direction(m.body.facing(), 0) * (8*stats.output);
    barrier.origin.y=world.walkableHeight(barrier.origin);
    barrier.yaw = m.body.facing();
    world.updateEnemyBounds(barrier);
    addEffect({WeaponEffect::Sparks,barrier.center,ray,Weapon::Empty,0,4.5f,rpgWeaponBarrier});
    world.sounds.push_back({SoundEvent::Special,int(CombatCue::Shield),barrier.center});
    break;
  }
  case Gear::Grappler: {
    used(true);
    if(!injuryCheck(m,0))break;
    auto hit = world.traceAim(origin, ray, reach(30));
    if(surfaceGravityAllowed(*this,m.body.bodyPoint(8))||m.body.gravity.active()){
      const auto surface=gravityRay(world,origin,origin+ray*reach(30));
      if(surface&&(hit.target<0||size_t(hit.target)>=enemies.size()||length(hit.position-origin)>=length(surface->point-origin))){
        if(beginGravityPull(world,m.body.gravity,m.body.position,*surface,13.f,3.5f)){
          m.thrusters.remaining=0;
          addEffect({WeaponEffect::Sparks,surface->point,surface->normal,Weapon::Empty,0,1.4f,rpgEnemyShieldHit});
          break;
        }
      }
    }
    if (hit.target >= 0 && size_t(hit.target) < enemies.size()) {
      hitEnemy(m, size_t(hit.target), dice.roll(4), Quality::None, false);
      auto target =
          m.body.position +
          unit(world.targets[hit.target].origin - m.body.position) * 9;
      if (clearPosition(world, target, 2, hit.target)) {
        world.targets[hit.target].origin = target;
        world.updateEnemyBounds(world.targets[hit.target]);
      }
    }
    break;
  }
  case Gear::ChestBeam: {
    used(true);
    if(!injuryCheck(m,0,true))break;
    if (fire(m, slot, origin, ray, dice.roll(8), Quality::Beam, reach(10))){
      ordnance.back().effect = AttackEffect::Push;
      addEffect({WeaponEffect::Sparks,origin,ray,Weapon::Rail,0,2.f,rpgWeaponHeavy});
    }
    break;
  }
  case Gear::MissileBarrage: {
    used(true);
    if(!injuryCheck(m,0,true))break;
    bool launched=false;
    for (int n = 0; n < 4; ++n) {
      if (fire(m, slot, origin, ray, dice.roll(4), Quality::None, reach(90))) {
        ordnance.back().body.kind = ProjectileKind::Rocket;
        ordnance.back().body.weapon=Weapon::Rocket;
        if(!world.sounds.empty()&&world.sounds.back().kind==SoundEvent::Shot)world.sounds.back().weapon=int(Weapon::Rocket);
        ordnance.back().body.radius = 5;
        launched=true;
      }
    }
    if(launched)addEffect({WeaponEffect::Sparks,origin,ray,Weapon::Rocket,0,1.2f,rpgRocketIgnition});
    break;
  }
  case Gear::ArcGrenade:
  case Gear::FragGrenade: {
    if(m.drone>0||m.held[slot])break;
    used();
    m.drone=22.f;
    m.dronePosition=origin+unit(ray)*1.8f;
    m.droneYaw=m.droneLaunchYaw=m.body.aimYaw;
    m.dronePitch=0;
    m.droneHeadYaw=m.input.headYaw;
    m.droneHeadPitch=m.input.headPitch;
    m.droneArc=gear==Gear::ArcGrenade;
    m.droneControl=true;
    m.droneGripHeld=m.input.ping[0];
    m.droneDetonateHeld=true;
    world.sounds.push_back({SoundEvent::Shot,int(Weapon::Cruise),m.dronePosition});
    break;
  }
  default:
    break;
  }
}
static float impactFraction(Vec3 from, Vec3 to, Vec3 center, Vec3 half) {
  Vec3 delta = to - from;
  float low = 0, high = 1;
  for (int axis = 0; axis < 3; ++axis) {
    float p = axis == 0   ? from.x
              : axis == 1 ? from.y
                          : from.z,
          d = axis == 0   ? delta.x
              : axis == 1 ? delta.y
                          : delta.z,
          c = axis == 0   ? center.x
              : axis == 1 ? center.y
                          : center.z,
          h = axis == 0   ? half.x
              : axis == 1 ? half.y
                          : half.z;
    if (std::abs(d) < .00001f) {
      if (p < c - h || p > c + h)
        return 2;
    } else {
      float a = (c - h - p) / d, b = (c + h - p) / d;
      if (a > b)
        std::swap(a, b);
      low = std::max(low, a);
      high = std::min(high, b);
      if (low > high)
        return 2;
    }
  }
  return low;
}
bool Session::redeployCarrierAllies(size_t i,float targetRange) {
  const auto &enemy=enemies[i];
  const auto &body=world.targets[i];
  if(enemy.spec>=34||!(combatSpec(enemy).traits&Carrier)||targetRange>=90.f)return false;
  int delivered=0;
  for(size_t ally=0;ally<enemies.size()&&delivered<2;++ally){
    auto &carried=world.targets[ally];
    if(ally==i||enemies[ally].hp<=0||!carried.active||carried.escaped||factionsAtWar(*this,i,ally)||
       (combatSpec(enemies[ally]).traits&Flying)||
       length(carried.origin-body.origin)>=30.f)continue;
    const float radius=std::clamp(std::max(carried.half.x,carried.half.z),3.f,12.f);
    auto p=body.origin+rotate(angles(body.yaw),{(delivered%2?1.f:-1.f)*(radius+8),0,0});
    p.y=world.surfaceHeight(p.x,p.z);
    const float depth=std::min(world.maxWadeDepth,carried.ai.localHalf.y);
    if(!insideArea(p,missionRadius()*.98f)||!clearPosition(world,p,radius,int(ally))||
       world.waterDepthAt(p,radius)>depth+.001f)continue;
    carried.origin=p;
    world.updateEnemyBounds(carried);
    ++delivered;
  }
  return delivered>0;
}

bool Session::startHeavyAttack(size_t i, const EnemyAttack &attack, const Member &pilot,
                               int attackIndex, Vec3 muzzle,int npcTarget) {
  auto &enemy=enemies[i];
  const auto &spec=combatSpec(enemy);
  const auto siege=siegeAttack(spec,attack);
  const auto role=mechaRangedRole(spec);
  const bool npcContact=!pilot.id&&role!=MechaRangedRole::None&&npcTarget>=0&&
      size_t(npcTarget)<enemies.size()&&factionsAtWar(*this,i,size_t(npcTarget));
  if((!pilot.id&&!npcContact)||enemy.transformed||attack.range<=18||(!siege&&attack.effect!=AttackEffect::Damage))
    return false;
  const bool mecha=spec.kind==EnemyKind::LargeMecha||spec.kind==EnemyKind::SmallMecha;
  const bool heavy=spec.kind==EnemyKind::Installation||spec.hd>=5;
  if(!mecha&&!heavy&&!siege)return false;

  const std::string_view name=attack.name;
  if(name=="Long-Range Beam Rifle")return false;
  const bool breath=siege.beam();
  const bool missile=name.find("Missile")!=name.npos||name.find("Rocket")!=name.npos||
      name.find("Seeker")!=name.npos;
  if(spec.kind==EnemyKind::LargeKaiju&&!siege&&!missile)return false;
  if(role==MechaRangedRole::None&&spec.kind==EnemyKind::SmallMecha&&attack.recharge==0&&
     attack.range<60&&enemy.tactic!=6&&!(spec.traits&(Ace|Commander)))return false;

  auto &body=world.targets[i];
  if(siege) muzzle=siegeEmitter(body,siege,world.elapsed,muzzle);
  const bool weave=role==MechaRangedRole::Skirmisher,rack=role==MechaRangedRole::MissileCarrier;
  const auto pattern=weave?HeavyPattern::Weave:rack?HeavyPattern::Loft:
      breath?HeavyPattern::Beam:missile?HeavyPattern::MissileCurtain:HeavyPattern::Crossfire;
  const auto cue=weave?rpgMechaWeaveTell:rack?rpgMechaRackTell:breath?rpgAtomicBreathWarning:rpgHeavyCrossfireWarning;
  const float windup=(weave||rack)?enemyEffectLife(cue):siege?siege.windup:enemy.boss>=0?
      campaignBoss(enemy.boss).windup:heavyCrossfireWindup;
  const auto aim=unit(pilot.body.bodyPoint(6)-muzzle);
  const auto weapon=(rack||missile)?Weapon::Rocket:breath?Weapon::Rail:Weapon::Rifle;
  const float lockDistance=length(pilot.body.bodyPoint(6)-muzzle);
  // A committed weave leaves its openings along the target's walkable plane,
  // including pilots or faction opponents attached to a wall or ceiling.
  const auto gravity=npcContact?world.targets[size_t(npcTarget)].ai.gravity.frame:pilot.body.gravity.frame;
  const auto up=(weave||rack)?rotate(gravity,{0,1,0}):Vec3{0,1,0};
  const auto right=(weave||rack)?rotate(gravity,{1,0,0}):Vec3{1,0,0};
  const auto lateral=unit(cross(aim,std::abs(dot(aim,up))<.85f?up:right));
  if(!addEffect({WeaponEffect::Sparks,muzzle,aim,weapon,std::max(0.f,enemyEffectLife(cue)-windup),(weave||rack)?lockDistance:attack.range,cue}))
    return true;
  enemy.heavyAttack={pattern,attack,muzzle,aim,lateral,windup,0,(i%2)!=0};
  enemy.heavyAttack.lockDistance=lockDistance;
  if(weave)enemy.heavyAttack.reverse=(int(world.elapsed/4.2f)+int(i))%2!=0;
  if(siege){
    body.ai.velocity={};
    body.ai.voidmawPose={VoidmawPose::Brace,world.elapsed,windup,enemy.heavyAttack.reverse?-1.f:1.f};
  }
  enemy.chargedAttack=attackIndex;
  enemy.chargeTarget=pilot.id;
  enemy.chargeWorldTarget=npcContact?npcTarget:-1;
  if(enemy.boss>=0){
    enemy.bossTell=windup;
    addEffect({WeaponEffect::Sparks,muzzle,aim,weapon,0,2.5f,rpgEnemyBossCharge});
  }
  body.ai.combatMotion.play(EnemyAction::RangedWindup,world.elapsed,windup,
                            weave?MechaTechnique::Skirt:rack?MechaTechnique::MissileRack:MechaTechnique::None);
  if(spec.kind==EnemyKind::LargeKaiju||spec.kind==EnemyKind::SmallKaiju)
    emitKaijuSound(world,siege.style==SiegeStyle::Axial?CombatCue::KaijuSonar:kaijuWindupCue(attack),muzzle);
  else world.sounds.push_back({SoundEvent::Special,int(CombatCue::Charge),muzzle,weapon});
  enemy.heavyAttack.redeployed=
      redeployCarrierAllies(i,length(pilot.body.position-body.origin));
  return true;
}

void Session::stepHeavyAttack(size_t i,float dt) {
  auto &enemy=enemies[i];
  auto &pattern=enemy.heavyAttack;
  auto &body=world.targets[i];
  const auto siege=siegeAttack(combatSpec(enemy),pattern.attack);
  const bool weave=pattern.pattern==HeavyPattern::Weave,rack=pattern.pattern==HeavyPattern::Loft;
  const bool breath=pattern.pattern==HeavyPattern::Beam;
  const bool missile=pattern.pattern==HeavyPattern::MissileCurtain;
  const auto target=std::find_if(members.begin(),members.end(),[&](const Member &m){return m.id==enemy.chargeTarget;});
  const int npc=enemy.chargeWorldTarget;
  const bool npcLocked=!enemy.chargeTarget&&npc>=0&&size_t(npc)<enemies.size()&&
      world.targets[npc].active&&!world.targets[npc].escaped&&enemies[npc].hp>0&&factionsAtWar(*this,i,size_t(npc));
  const bool lost=!npcLocked&&(target==members.end()||target->ejected||target->sheet.hp<=0||
      target->body.lifeSpent||target->sheet.hidden||target->hiddenRemaining>0);
  if(lost)pattern.guidanceLost=true;
  if(enemy.disabled>0||enemy.transformed||body.ai.gravity.mode==GravityMode::Floating||
     body.ai.gravity.mode==GravityMode::Pulling||(pattern.pulse==0&&(lost||enemy.overheated))) {
    const auto cue=weave?rpgMechaWeaveTell:rack?rpgMechaRackTell:breath?rpgAtomicBreathWarning:rpgHeavyCrossfireWarning;
    std::erase_if(world.effects,[&](const auto &fx){
      return (fx.model==cue||fx.model==rpgEnemyBossCharge)&&length(fx.position-pattern.origin)<.01f;
    });
    if(siege)body.ai.voidmawPose={};
    pattern={};
    enemy.chargedAttack=-1;
    enemy.bossTell=0;
    enemy.fireClock=std::max(enemy.fireClock,.6f);
    if(body.ai.combatMotion.action==EnemyAction::RangedWindup)
      body.ai.combatMotion.play(EnemyAction::None,0,0);
    return;
  }

  if(pattern.pulse==0&&!pattern.redeployed&&!lost&&target!=members.end())
    pattern.redeployed=redeployCarrierAllies(i,length(target->body.position-body.origin));
  if(weave){
    const auto up=body.ai.gravity.up();
    const auto lateral=unit(pattern.lateral-up*dot(pattern.lateral,up),unit(cross(pattern.aim,up)));
    const float speed=hasInjury(enemy.injuries,MechaInjury::Hobbled)?2.5f:5.f;
    const auto step=lateral*((pattern.reverse?-speed:speed)*dt);
    if(insideArea(body.origin+step,missionRadius()*.98f))
      moveRpgEnemy(i,step,dt,(combatSpec(enemy).traits&(Flying|Hover))!=0);
  }
  pattern.timer-=dt;
  auto &motion=body.ai.combatMotion;
  motion.aim=1.f;
  const auto local=rotate(gravityInverse(body.ai.gravity.frame),pattern.aim);
  body.ai.weaponYaw=std::atan2(-local.x,-local.z);
  body.ai.weaponPitch=std::atan2(local.y,std::hypot(local.x,local.z));
  enemy.bossTell=enemy.boss>=0&&pattern.pulse==0?std::max(0.f,pattern.timer):0.f;
  if(pattern.timer>0)return;

  const int pulses=weave?mechaWaveLayers:rack?mechaRackSalvos:siege?siege.pulses:3;
  const float cadence=weave?mechaWaveCadence:rack?mechaRackCadence:siege?siege.cadence:missile?.30f:.24f;
  if(pattern.pulse==0){
    enemy.chargedAttack=-1;
    motion.play(EnemyAction::Fire,world.elapsed,cadence*float(pulses),
                weave?MechaTechnique::Skirt:rack?MechaTechnique::MissileRack:MechaTechnique::None);
    if(siege){
      body.ai.voidmawPose={VoidmawPose::Discharge,world.elapsed,cadence*float(pulses),
                           pattern.reverse?-1.f:1.f};
      enemy.radioReady=std::max(enemy.radioReady,world.elapsed+6.f);
    }else world.sounds.push_back({SoundEvent::Shot,int(breath?Weapon::Rail:(missile||rack)?Weapon::Rocket:Weapon::Rifle),pattern.origin});
  }
  while(pattern.timer<=0&&pattern.pulse<pulses){
    const auto &attack=pattern.attack;
    const int damageDice=attack.dice+manualEnemyExtraDice(*this,i);
    int damage=manualEnemyDamageBonus(*this,i);
    for(int die=0;die<damageDice;++die)damage+=dice.roll(attack.sides);
    const float diminished=enemy.missingArms==3&&!breath?.5f:1.f;
    damage=std::max(1,int(std::lround(damage*diminished*(rack?.32f:siege?siege.damageFraction:.42f))));
    const Vec3 origin=siege?siegeEmitter(body,siege,world.elapsed,pattern.origin):pattern.origin;
    // The beam is one sustained exhalation. Spines and globules launch in discrete salvos.
    if(siege&&(!breath||pattern.pulse==0))emitKaijuSound(world,kaijuAttackCue(attack),origin);
    // Freeze the warned endpoint when the creature commits. Recoil moves the
    // emitter along with its jaw or arm without tracking the dodging pilot.
    const Vec3 aim=siege?unit(pattern.origin+pattern.aim*attack.range-origin,pattern.aim):pattern.aim;
    if(weave||rack){
      launchMechaLayer(i,damage);
      addEffect({WeaponEffect::Sparks,body.center,aim,rack?Weapon::Rocket:Weapon::Rifle,0,1.6f,
                 rack?rpgRocketIgnition:rpgEnemyEnergy});
    }else if(breath) {
      const float sweep=siegeLaneOffset(siege,pattern.pulse,0,pattern.reverse);
      const Vec3 ray=patternLane(aim,pattern.lateral,sweep);
      const Vec3 end=origin+ray*attack.range;
      float stop=world.groundContact(origin,end);
      int scenery=-1;
      for(size_t j=0;j<world.targets.size();++j){
        const auto &obstacle=world.targets[j];
        if(j==i||!obstacle.active||obstacle.escaped||obstacle.health<=0||obstacle.spawnType==21)continue;
        const float hit=impactFraction(origin,end,obstacle.center,obstacle.half);
        if(hit<stop){stop=hit;scenery=int(j);}
      }
      Member *victim=nullptr;
      for(auto &m:members){
        if(m.ejected||m.sheet.hp<=0||m.body.lifeSpent)continue;
        const auto centre=m.body.bodyPoint(6);
        const auto inverse=gravityInverse(m.body.gravityFacing());
        const float hit=impactFraction(rotate(inverse,origin-centre),
            rotate(inverse,end-centre),{},{2.8f,6.8f,2.8f});
        if(hit<stop){stop=hit;victim=&m;}
      }
      const float reach=attack.range*std::clamp(stop,0.f,1.f);
      if(reach>.1f)addEffect({WeaponEffect::Sparks,origin,ray,Weapon::Rail,0,reach,rpgAtomicBreath});
      if(victim){
        const int penetrating=absorbShield(*victim,damage);
        if(penetrating>0){
          victim->sheet.stress(dice,.16f);
          if(attack.reactor)victim->sheet.reactorTest(dice);
          takeDamage(*victim,penetrating,false,origin-victim->body.bodyPoint(6));
        }
      }else if(scenery>=int(enemies.size()))damageScenery(size_t(scenery),float(damage));
      else if(scenery>=0&&factionsAtWar(*this,i,size_t(scenery)))
        damageNpc(size_t(scenery),damage,AttackEffect::Damage,Quality::Thermal);
      if(stop<=1.f)addEffect({WeaponEffect::Sparks,origin+ray*reach,ray*-1.f,Weapon::Rail,0,1.3f,rpgEnemyHit});
    }else {
      const int lanes=siege?siege.lanes:missile||combatSpec(enemy).kind==EnemyKind::SmallMecha?3:5;
      for(int lane=0;lane<lanes;++lane){
        const float offset=siege?siegeLaneOffset(siege,pattern.pulse,lane,pattern.reverse):heavyFanOffsets[pattern.pulse][lanes==3?lane*2:lane];
        const Vec3 ray=patternLane(aim,pattern.lateral,offset);
        Shot shot;
        shot.body.position=origin;
        shot.body.velocity=ray*(siege?siege.projectileSpeed:missile?48.f:65.f);
        shot.body.damage=float(damage);
        shot.body.owner=int(i);
        shot.body.mount=-1;
        shot.body.life=attack.range/length(shot.body.velocity);
        shot.body.weapon=missile?Weapon::Rocket:Weapon::Rifle;
        shot.reactor=attack.reactor;
        if(siege)shot.effect=attack.effect;
        if(missile){shot.body.kind=ProjectileKind::Rocket;shot.body.hull=2.f;}
        launch(std::move(shot));
      }
      addEffect({WeaponEffect::Sparks,origin,pattern.aim,missile?Weapon::Rocket:Weapon::Rifle,0,2.f,siege&&!missile?rpgEnemyDisruption:rpgEnemyKinetic});
    }
    body.ai.muzzleFlash=.12f;
    ++pattern.pulse;
    pattern.timer+=cadence;
  }
  if(pattern.pulse<pulses||pattern.timer>0)return;
  if(siege){
    body.ai.voidmawPose={VoidmawPose::SiegeRecover,world.elapsed,siege.recovery*.48f,
                         pattern.reverse?-1.f:1.f};
  }
  pattern={};
  enemy.fireClock=(weave?2.4f:rack?3.35f:siege?siege.recovery:enemy.boss>=0?1.f:1.22f)*(enemy.missingArms?1.3f:1.f);
  addReactorHeat(enemy.heat,enemy.overheated,enemy.ventRemaining,breath?.62f:.48f);
}

void Session::criticalHit(Member &shooter,size_t index,Vec3 point,int &damage){
  auto &enemy=enemies[index];
  if(!pilotedMecha(combatSpec(enemy)))return;
  const auto part=hitWeakPoint(world.targets[index],point);
  if(part==WeakPoint::Count)return;
  damage+=std::max(2,damage/2);
  shooter.criticalRemaining=.75f;
  auto &wear=enemy.subsystemDamage[size_t(part)];
  const int threshold=std::clamp(enemy.maxHp/6,7,22);
  if(wear>=threshold)return;
  wear=std::min(threshold,wear+damage);
  addEffect({WeaponEffect::Sparks,point,{0,1,0},Weapon::Rail,0,1.5f,rpgEnemyShieldHit});
  if(wear<threshold)return;
  switch(part){
    case WeakPoint::Optics:enemy.injuries|=uint32_t(MechaInjury::Targeting)|uint32_t(MechaInjury::Sensors);break;
    case WeakPoint::Reactor:enemy.injuries|=uint32_t(MechaInjury::Severe);
      addReactorHeat(enemy.heat,enemy.overheated,enemy.ventRemaining,1.f);break;
    case WeakPoint::LeftArm:case WeakPoint::RightArm:{
      enemy.injuries|=uint32_t(MechaInjury::LostArm);
      enemy.missingArms|=part==WeakPoint::LeftArm?1u:2u;
      WeaponEffect debris{WeaponEffect::Debris,point,unit(point-world.targets[index].center)*7.f+Vec3{0,7,0},Weapon::Empty,0,1.1f};
      debris.model=rpgDebrisBase+3;addEffect(debris);break;
    }
    case WeakPoint::Drive:enemy.injuries|=uint32_t(MechaInjury::Hobbled);break;
    case WeakPoint::Count:break;
  }
  log(shooter.name+" / ENEMY SUBSYSTEM DISABLED");
}
bool Session::parryStrike(Member &victim,const Shot &shot){
  if(!shot.meleeStrike||shot.body.owner<0||size_t(shot.body.owner)>=enemies.size())return false;
  const auto incoming=unit(shot.body.velocity);
  const auto impact=shot.body.position;
  const bool directed=dot(shot.guardAxis,shot.guardAxis)>.5f;
  if(directed&&(!victim.combatControls()||victim.sheet.overheated||victim.stun>0))return false;
  if(dot(incoming*-1.f,rotate(victim.body.gravityFacing(),{0,0,-1}))<.08f)return false;
  for(int hand=0;hand<2;++hand){
    const auto &arm=victim.body.arms[hand];
    const auto gear=victim.sheet.equipment[hand];
    const bool shield=gear==Gear::Shield;
    if((!directed&&victim.parryWindow[hand]<=0)||!arm.tracked||!victim.body.armAvailable(hand)||
       (!shield&&!melee(gear))||victim.handStashed[hand])continue;
    // Directed strikes can be met with a steady crossed blade. The shield
    // covers a wider volume and does not require a particular edge angle.
    if(directed&&!shield&&std::abs(dot(arm.forward,shot.guardAxis))<.78f)continue;
    const auto blade=arm.forward*(heavy(gear)?3.5f:2.7f);
    for(float along:{.15f,.45f,.75f,1.f}){
      const auto guard=arm.wrist+blade*along;
      const float width=shield?2.2f:1.1f;
      if(impactFraction(impact-incoming*4.f,impact+incoming*.5f,guard,{width,width,width})>1.f)continue;
      victim.parryWindow.fill(0);
      victim.riposteRemaining=1.35f;victim.riposteTarget=shot.body.owner;
      auto &attacker=enemies[size_t(shot.body.owner)];
      const float stagger=attacker.boss>=0?.45f:.9f;
      attacker.disabled=std::max(attacker.disabled,stagger);
      world.targets[size_t(shot.body.owner)].ai.combatMotion.play(EnemyAction::Stagger,world.elapsed,stagger,
          directed?attacker.duel.cut:MechaTechnique::None);
      addEffect({WeaponEffect::Sparks,guard,incoming*-1.f,Weapon::Rail,0,1.8f,rpgEnemyShieldHit});
      world.sounds.push_back({SoundEvent::Special,int(CombatCue::MetalHit),guard});
      return true;
    }
  }
  return false;
}
void Session::stepProjectiles(float dt) {
  // Establish this tick's guided velocities before relative swept interception.
  for(auto &shot:ordnance)
    if(shot.body.life>0&&shot.body.visual==ProjectileVisual::LoftMissile)guideLoftMissile(shot,dt);
  for(auto &missile:ordnance){
    auto &rocket=missile.body;
    if(rocket.life<=0||rocket.kind!=ProjectileKind::Rocket)continue;
    for(auto &shot:ordnance){
      auto &bullet=shot.body;
      if(bullet.life<=0||bullet.kind!=ProjectileKind::Bullet||shot.meleeStrike)continue;
      if(rocket.shooter<0&&bullet.shooter<0){
        if(rocket.owner<0||bullet.owner<0||size_t(rocket.owner)>=enemies.size()||
           size_t(bullet.owner)>=enemies.size()||!factionsAtWar(*this,size_t(rocket.owner),size_t(bullet.owner)))continue;
      }else if(rocket.shooter>=0&&bullet.shooter>=0)continue;
      else if(rocket.shooter<0?!missile.npcCanHitPilots:!shot.npcCanHitPilots)continue;
      const auto separation=bullet.position-rocket.position;
      const auto travel=(bullet.velocity-rocket.velocity)*dt;
      const auto time=std::clamp(-dot(separation,travel)/std::max(.0001f,dot(travel,travel)),0.f,1.f);
      if(length(separation+travel*time)>1.15f)continue;
      const auto contact=rocket.position+rocket.velocity*(dt*time);
      auto unobstructed=[&](Vec3 from,Vec3 delta){
        const float distance=length(delta)*dt*time;
        if(distance<.01f)return true;
        return length(world.traceAim(from,unit(delta),distance).position-from)+.01f>=distance;
      };
      if(!unobstructed(rocket.position,rocket.velocity)||!unobstructed(bullet.position,bullet.velocity))continue;
      bullet.life=0;
      if(rocket.hull<0)rocket.hull=2;
      rocket.hull-=bullet.damage;
      if(rocket.hull>0)continue;
      rocket.life=0;rocket.intercepted=true;
      addEffect({WeaponEffect::Blast,contact,{},rocket.weapon,0,2.f,rpgWeaponBlast});
      world.sounds.push_back({SoundEvent::Explosion,0,contact});
      break;
    }
  }
  for (auto &shot : ordnance) {
    auto &p = shot.body;
    if(p.life<=0)continue;
    const Vec3 previous=p.position;
    const bool fromNpc=p.shooter<0&&p.owner>=0&&size_t(p.owner)<enemies.size();
    const bool canHitPilots=!fromNpc||shot.npcCanHitPilots;
    const auto attacksNpc=[&](size_t target){return fromNpc&&factionsAtWar(*this,size_t(p.owner),target);};
    if(p.kind==ProjectileKind::Rocket&&p.homingTarget>=0&&size_t(p.homingTarget)<enemies.size()){
      const auto &track=world.targets[size_t(p.homingTarget)];
      if(!track.active||track.health<=0)p.homingTarget=-1;
      else{
        const float speed=length(p.velocity);
        const auto forward=unit(p.velocity);
        const auto desired=unit(track.center-p.position,forward);
        const float turn=std::acos(std::clamp(dot(desired,forward),-1.f,1.f));
        p.velocity=unit(forward+(desired-forward)*std::min(1.f,2.7f*dt/std::max(.001f,turn)),forward)*speed;
      }
    }
    if (p.kind == ProjectileKind::Bomb)
      p.velocity.y -= 18 * dt;
    const float travelTime=shot.meleeStrike?std::min(dt,p.life):dt;
    Vec3 next = p.position + p.velocity * travelTime;
    const bool wave=p.visual==ProjectileVisual::WaveA||p.visual==ProjectileVisual::WaveB;
    const float skin=wave?mechaWaveRadius:0.f;
    const Vec3 padding{skin,skin,skin};
    float first = world.groundContact(p.position-Vec3{0,skin,0},next-Vec3{0,skin,0});
    int target = -1;
    Member *victim = nullptr,*droneVictim=nullptr;
    for (size_t i = 0; i < world.targets.size(); ++i) {
      auto &t = world.targets[i];
      if (!t.active || t.health <= 0 || int(i) == p.owner)
        continue;
      float fraction;
      if(i<enemies.size()&&t.ai.boundsReady&&pilotedMecha(combatSpec(enemies[i]))){
        const auto local=gravityInverse(compose(t.ai.gravity.frame,angles(t.yaw)));
        const auto from=rotate(local,p.position-t.center);
        const auto to=rotate(local,next-t.center);
        fraction=impactFraction(from,to,{},t.ai.localHalf+padding);
        const unsigned missing=enemies[i].missingArms;
        if(missing&&fraction<=1){
          const auto part=hitWeakPoint(t,p.position+(next-p.position)*fraction);
          if((part==WeakPoint::LeftArm&&(missing&1u))||(part==WeakPoint::RightArm&&(missing&2u))){
            auto torso=t.ai.localHalf;torso.x*=.62f;
            fraction=impactFraction(from,to,{},torso+padding);
          }
        }
      }else fraction=impactFraction(p.position,next,t.center,t.half+padding);
      if (fraction < first) {
        first = fraction;
        target = int(i);
      }
    }
    if(canHitPilots)for(auto &m:members)if(m.drone>0&&int(m.id)!=p.shooter){float fraction=impactFraction(p.position,next,m.dronePosition,Vec3{1,1,1}+padding);if(fraction<first){first=fraction;target=-1;victim=nullptr;droneVictim=&m;}}
    if(canHitPilots)for (auto &m : members)
      if (int(m.id) != p.shooter && !m.ejected && m.sheet.hp > 0) {
        float fraction = impactFraction(
            p.position, next, m.body.bodyPoint(6), Vec3{2,6,2}+padding);
        if(m.body.gravity.active()){
          const auto inverse=gravityInverse(m.body.gravityFacing());
          const auto centre=m.body.bodyPoint(6);
          fraction=impactFraction(rotate(inverse,p.position-centre),rotate(inverse,next-centre),{},Vec3{2,6,2}+padding);
        }
        if (fraction < first) {
          first = fraction;
          target = -1;
          victim = &m;droneVictim=nullptr;
        }
      }
    p.position = p.position + (next - p.position) * std::min(first, 1.f);
    p.life -= dt;
    bool ground =
        p.position.y <= world.walkableHeight(p.position,.02f)+.01f;
    if (first <= 1 || ground || (p.life <= 0 && p.radius > 0)) {
      p.life = 0;
      if(ground){auto contact=p.position;contact.y=world.walkableHeight(contact,.02f)+.2f;
        bool water=world.hasWater(contact.x,contact.z);
        if(water)for(const auto &region:world.waterRegions)if(proceduralWaterEdge(region,contact.x,contact.z)>=0)contact.y=std::max(contact.y,region.level+.05f);
        addEffect({WeaponEffect::Smoke,contact,{0,.8f,0},p.weapon,0,p.radius>0?5.f:1.5f,water?rpgSplash:rpgDust});
        if(!water&&p.radius>=3&&std::count_if(world.effects.begin(),world.effects.end(),[](const auto &fx){return fx.model==rpgScorch;})<12)
          addEffect({WeaponEffect::Impact,contact,{},p.weapon,0,std::clamp(p.radius*.4f,1.f,5.f),rpgScorch});
      }
      auto shooter = std::find_if(members.begin(), members.end(), [&](auto &m) {
        return int(m.id) == p.shooter;
      });
      if(shot.quality==Quality::Bouncing&&shot.ricochets==0&&shooter!=members.end()&&!victim&&(ground||target>=int(enemies.size()))){
        Vec3 normal{0,1,0};
        if(!ground&&target>=0){auto &surface=world.targets[target];Vec3 offset=p.position-surface.center;float x=std::abs(std::abs(offset.x)-surface.half.x),y=std::abs(std::abs(offset.y)-surface.half.y),z=std::abs(std::abs(offset.z)-surface.half.z);normal=x<y&&x<z?Vec3{offset.x<0?-1.f:1.f,0,0}:y<z?Vec3{0,offset.y<0?-1.f:1.f,0}:Vec3{0,0,offset.z<0?-1.f:1.f};}
        p.velocity=p.velocity-normal*(2*dot(p.velocity,normal));p.position=p.position+normal*.3f;p.life=.4f;p.damage=float(dice.roll(4));++shot.ricochets;shooter->sheet.reactorTest(dice);continue;
      }
      if (p.radius > 0 && shooter != members.end()) {
        blast(*shooter, p.position, p.radius, int(p.damage), shot.quality,
              true,shot.maxTargets,shot.effect);
        const bool electric=shot.effect==AttackEffect::Stun;
        addEffect({WeaponEffect::Blast,p.position,unit(p.velocity),p.weapon,0,
                   std::clamp(p.radius,3.f,7.f),electric?rpgWeaponArc:rpgWeaponBlast});
        if(electric)world.sounds.push_back({SoundEvent::Special,int(CombatCue::Arc),p.position});
        else world.sounds.push_back({SoundEvent::Explosion,0,p.position});
        continue;
      }
      if (p.radius > 0 && p.shooter < 0) {
        if(canHitPilots)for(auto &pilot:members){
          if(pilot.ejected||pilot.sheet.hp<=0)continue;
          const Vec3 body=pilot.body.bodyPoint(6);
          const Vec3 delta=body-p.position;
          const bool inBlast=shot.coverSensitiveBlast?
              std::hypot(delta.x,delta.z)<=p.radius&&std::abs(delta.y)<=8.f:length(delta)<=p.radius;
          if(&pilot!=victim&&!inBlast)continue;
          if(shot.coverSensitiveBlast && &pilot!=victim){
            const auto origin=p.position+unit(previous-p.position,{0,1,0})*.20f;
            const auto delta=body-origin;
            const auto hit=world.traceAim(origin,unit(delta),length(delta));
            if(length(hit.position-origin)+.10f<length(delta))continue;
          }
          takeDamage(pilot,int(p.damage),false,p.position-body);
        }
        for(size_t i=0;i<enemies.size();++i){
          const auto &body=world.targets[i];
          if(!body.active||!attacksNpc(i))continue;
          const auto delta=body.center-p.position;
          const bool inBlastRange=shot.coverSensitiveBlast?
              std::hypot(delta.x,delta.z)<=p.radius&&std::abs(delta.y)<=8.f:length(delta)<=p.radius;
          if(int(i)!=target&&(!inBlastRange||!world.enemyLineOfSight(p.position+unit(previous-p.position)*.2f,
              body.center,p.owner,int(i))))continue;
          damageNpc(i,int(p.damage),shot.effect,shot.quality);
        }
        if(canHitPilots)blastScenery(p.position,p.radius,p.damage);
        if(shot.coverSensitiveBlast)
          world.sounds.push_back({SoundEvent::Explosion,0,p.position});
        addEffect({WeaponEffect::Blast,
                                 p.position,
                                 {},
                                 Weapon::Grenade,
                                 0,
                                 p.radius});
        continue;
      }
      if(droneVictim){detonateDrone(*droneVictim);continue;}
      if (victim) {
        if(parryStrike(*victim,shot))continue;
        if(!shot.meleeStrike)addEffect({WeaponEffect::Sparks,p.position,unit(p.velocity)*-1.f,p.weapon,0,.85f,rpgEnemyHit});
        int damage = int(p.damage);
        if (shot.effect == AttackEffect::Consume)
          damage = victim->sheet.hp + victim->sheet.ap + victim->shield;
        if(shot.quality==Quality::Thermal)victim->sheet.stress(dice,.16f);
        int previousAp=victim->sheet.ap;
        const bool energy=!shot.meleeStrike&&(shot.quality==Quality::Plasma||shot.quality==Quality::Beam||
            shot.quality==Quality::Arc||(fromNpc&&p.weapon==Weapon::Rail));
        if(fromNpc)
          addEffect({WeaponEffect::Sparks,p.position,unit(p.velocity*-1.f),
                     p.weapon,0,1.6f,energy&&victim->shield>0?rpgEnemyShieldHit:rpgEnemyHit});
        takeDamage(*victim,damage,shot.quality==Quality::Plasma,p.velocity*-1.f,energy);
        if(p.owner>=0&&size_t(p.owner)<enemies.size()&&(combatSpec(enemies[p.owner]).traits&Dismantler))victim->sheet.ap=std::max(0,victim->sheet.ap-std::max(0,previousAp-victim->sheet.ap));
        if (shot.reactor || shot.effect == AttackEffect::Reactor ||
            shot.effect == AttackEffect::DoubleReactor)
          victim->sheet.reactorTest(dice);
        if (shot.effect == AttackEffect::DoubleReactor)
          victim->sheet.reactorTest(dice);
        if (shot.effect == AttackEffect::Stun)
          victim->stun = std::clamp(
              36.f / std::max(3.f, victim->sheet.attributes[3]*(injuryDisadvantage(victim->injuries,3)?.5f:1.f)), 1.f, 6.f);
        if (shot.effect == AttackEffect::Blind)
          victim->blind = std::clamp(
              72.f / std::max(3.f, victim->sheet.attributes[2]*(injuryDisadvantage(victim->injuries,2)?.5f:1.f)), 2.f, 10.f);
        if(shot.effect==AttackEffect::Immobilize)victim->immobile=3.f*float(dice.roll(4));
        if(shot.effect==AttackEffect::DisruptMobility)victim->mobilityDisrupt=3.f*float(dice.roll(4));
        if(shot.effect==AttackEffect::DisruptSystems){victim->mobilityDisrupt=3.f*float(dice.roll(4));victim->systemDisrupt=victim->mobilityDisrupt;}
        if (shot.effect == AttackEffect::Ongoing) {
          victim->ongoing = float(dice.roll(4)) * 3;
          victim->ongoingDamage = std::max(1, damage);
          victim->ongoingTick = 3;
          if(p.owner>=0&&size_t(p.owner)<enemies.size()&&std::string_view(combatSpec(enemies[p.owner]).id)=="terran_mammoth"){victim->ongoing=3;victim->ongoingDamage=dice.roll(6)+dice.roll(6);}
        }
        if (shot.effect == AttackEffect::Pull && p.owner >= 0) {
          auto desired =
              world.targets[p.owner].origin +
              unit(victim->body.position - world.targets[p.owner].origin) * 9;
          if (clearPosition(world, desired, 2, p.owner))
            victim->body.position = desired;
        }
        if(shot.effect==AttackEffect::Push&&p.shooter<0&&!victim->ejected){
          Vec3 direction{p.velocity.x,0,p.velocity.z};
          if(length(direction)>.01f){
            direction=unit(direction);
            // Sweep in small steps so knockback cannot cross walls or ledges.
            for(int step=0;step<6;++step){
              auto displaced=victim->body.position+direction*.5f;
              displaced.y=world.walkableHeight(displaced);
              const bool overlapsAlly=std::any_of(members.begin(),members.end(),[&](const Member &ally){
                return &ally!=victim&&!ally.ejected&&ally.sheet.hp>0&&
                    length(displaced-ally.body.position)<4&&
                    length(displaced-ally.body.position)<length(victim->body.position-ally.body.position);
              });
              if(overlapsAlly||!insideArea(displaced,missionRadius()*.98f)||
                 std::abs(displaced.y-victim->body.position.y)>.6f||
                 !world.groundPassable(displaced,2)||!clearPosition(world,displaced,2))break;
              victim->body.position=displaced;
            }
          }
        }
        if (p.owner >= 0 && size_t(p.owner) < enemies.size() &&
            (length(world.targets[p.owner].origin - victim->body.position) <= 10
                 ? victim->sheet.has(Module::ExothermicPlating)
                 : victim->sheet.has(Module::ReflectiveShielding)) &&
            damage > 0) {
          hitEnemy(*victim, size_t(p.owner), dice.roll(6), Quality::None,
                   false);
          victim->sheet.reactorTest(dice);
          }
      } else if (target >= 0) {
        if(!shot.meleeStrike)addEffect({WeaponEffect::Sparks,p.position,unit(p.velocity)*-1.f,p.weapon,0,.9f,rpgEnemyHit});
        if (size_t(target) < enemies.size() && shooter != members.end()) {
          int damage = int(p.damage);
          criticalHit(*shooter,size_t(target),p.position,damage);
          if(shot.quality==Quality::Artillery&&length(p.position-shot.launchOrigin)>=30)damage+=dice.roll(4);
          if(shot.quality==Quality::SawedOff&&length(p.position-shot.launchOrigin)<=30)++damage;
          hitEnemy(*shooter, size_t(target), damage, shot.quality, false);
          if(shot.quality==Quality::BeltFed&&p.mount>=0&&p.mount<MountCount){if(shot.feedMisfire){shooter->beltTarget[p.mount]=-1;shooter->body.cooldown[p.mount]=3;}else shooter->beltTarget[p.mount]=target;}
          if (shot.effect == AttackEffect::Push) {
            auto &body = world.targets[target];
            auto desired =
                body.origin + unit(body.origin - shooter->body.position) * 20;
            if (clearPosition(world, desired, 3, target)) {
              body.origin = desired;
              world.updateEnemyBounds(body);
            }
          }
        }else if(size_t(target)<enemies.size()){
          if(attacksNpc(size_t(target)))damageNpc(size_t(target),int(p.damage),shot.effect,shot.quality);
        }else if(canHitPilots||!world.targets[size_t(target)].protectedTarget)
          damageScenery(size_t(target),p.damage);
      }
    }
  }
  std::erase_if(ordnance, [](auto &s) { return s.body.life <= 0; });
  world.projectiles.clear();
  for (auto &s : ordnance)
    world.projectiles.push_back(s.body);

}
void Session::npcRadio(size_t actor, RadioLine line) {
  const bool urgent=line==RadioLine::EnemyDistress || line==RadioLine::FriendlyContact;
  if (world.elapsed < npcRadioReady || world.sounds.size() >= 96 ||
      (!urgent && (world.elapsed < routineRadioReady || world.elapsed < enemies[actor].radioReady)))
    return;
  const int faction = actorFaction(*this, actor);
  if (faction < 0 || faction >= npcRadioFactions)
    return;
  auto &rotation=radioRotation[size_t(faction)][size_t(line)];
  if(world.elapsed<rotation.ready)return;
  const Vec3 origin = world.targets[actor].center;
  if (std::none_of(members.begin(), members.end(), [&](const Member &member) {
        const auto delta = member.body.position - origin;
        return member.combatControls() && !member.body.lifeSpent &&
               dot(delta, delta) < npcRadioMaxRange * npcRadioMaxRange;
      }))
    return;
  const auto roll=manualRandom(mission.recipe.seed,ManualRng::Chatter,radioSequence++);
  const unsigned crew=manualRandom(mission.recipe.seed,ManualRng::Chatter,unsigned(actor))%2;
  world.sounds.push_back({SoundEvent::Special,radioCue(faction,line,rotation.next(crew,roll)),origin,
                          Weapon::Empty,line==RadioLine::EnemyDistress?-1:int(actor)});
  // Reserve airtime for a complete transmission. Distress and first allied
  // check-in can use the quiet interval between routine reports.
  npcRadioReady=world.elapsed+7.f;
  routineRadioReady=world.elapsed+16.f+float((roll>>8)%8);
  rotation.ready=world.elapsed+radioRepeatInterval(line)+float((roll>>16)%12);
  enemies[actor].radioReady=world.elapsed+26.f;
}

void Session::ambientKaijuCall(std::span<const Vec3> observers) {
  if (world.elapsed < ambientKaijuReady || observers.empty() || world.sounds.size() >= 96)
    return;
  ambientKaijuReady = world.elapsed + 3.f;
  size_t speaker = enemies.size();
  float nearest = 230.f * 230.f;
  for (size_t i=0; i<enemies.size(); ++i) {
    const auto &body = world.targets[i];
    const auto &enemy = enemies[i];
    if (enemy.hp <= 0 || !body.active || body.escaped || world.elapsed < enemy.radioReady ||
        enemy.grapplePilot || enemy.heavyAttack.pattern != HeavyPattern::None)
      continue;
    const auto kind = combatSpec(enemy).kind;
    if (kind != EnemyKind::SmallKaiju && kind != EnemyKind::LargeKaiju)
      continue;
    float distance = nearest;
    for (const auto observer : observers) {
      const auto delta = body.center - observer;
      distance = std::min(distance, dot(delta, delta));
    }
    if (distance >= 65.f * 65.f && distance < nearest) {
      speaker = i;
      nearest = distance;
    }
  }
  if (speaker == enemies.size())
    return;
  auto &enemy = enemies[speaker];
  emitKaijuSound(world,kaijuCall(combatSpec(enemy)),world.targets[speaker].center);
  enemy.radioReady = world.elapsed + 18.f;
  ambientKaijuReady = world.elapsed + 20.f;
}

void Session::enemiesAct(float dt) {
  stepManualEnemies(dt);
  stepBosses(dt);
  if (navigation.size()!=enemies.size())navigation.resize(enemies.size());
  enemyMemory.resize(enemies.size());
  tacticalClock-=dt;
  // Death/concealment invalidates a selected pilot inside the planning cadence.
  for(const auto &decision:tacticalDecisions)if(decision.memberIndex>=0) {
    if(size_t(decision.memberIndex)>=members.size()) {tacticalClock=0;break;}
    const auto &m=members[size_t(decision.memberIndex)];
    if(m.id!=decision.targetId || !m.combatControls() || m.sheet.hidden || m.hiddenRemaining>0 || m.body.lifeSpent) {tacticalClock=0;break;}
  }
  if(tacticalClock<=0 || tacticalDecisions.size()!=enemies.size()) {
    std::vector<uint32_t> previous;
    for(const auto &decision:tacticalDecisions)previous.push_back(decision.targetId);
    stepEncounterGroups(*this,.35f);
    tacticalDecisions=planEnemyTactics(*this,previous);
    factionContacts=factionCombatContacts(*this);
    tacticalClock=.35f;
  }
  // Reserve planning work in rotating actor order while retaining the fixed
  // combat/physics order. Four grants bound total work to 16 edges/256 samples
  // and ensure high-index actors cannot be starved by difficult earlier routes.
  std::vector<EnemyNavigationBudget> planningBudgets(enemies.size(),{0,0});
  size_t examined=0,granted=0;
  while(examined<enemies.size() && granted<4) {
    const size_t index=navigationTurn%enemies.size();
    navigationTurn=(index+1)%enemies.size();++examined;
    const auto &candidate=enemies[index];
    const auto &actor=world.targets[index];
    if(candidate.hp<=0 || candidate.disabled>0 || !actor.active || actor.escaped ||
       combatSpec(candidate).kind==EnemyKind::Installation)continue;
    planningBudgets[index]={4,64};++granted;
  }
  std::vector<Vec3> observers;
  observers.reserve(members.size()*2);
  for(const auto &pilot:members){
    if(!pilot.combatControls()||pilot.body.lifeSpent)continue;
    observers.push_back(pilot.body.position);
    if(pilot.drone>0)observers.push_back(pilot.dronePosition);
  }
  for (size_t i = 0; i < enemies.size(); ++i) {
    auto &e = enemies[i];
    auto &spec = combatSpec(e);
    unsigned traits=spec.traits|(e.transformed?unsigned(Flying):0u);
    auto &body = world.targets[i];
    body.ai.rpgCrushing=!e.transformed&&pilotedMecha(spec);
    // A wound vocal marks crossing half health. Repeated hits below it stay quiet.
    if((spec.kind==EnemyKind::SmallKaiju||spec.kind==EnemyKind::LargeKaiju)&&body.active&&!body.escaped&&
       e.hp>0&&e.hp<=e.maxHp/2&&body.ai.lastHealth>e.maxHp/2&&!e.grapplePilot&&world.elapsed>=e.radioReady){
      emitKaijuSound(world,CombatCue::KaijuHiss,body.center);
      e.radioReady=world.elapsed+6.f;
    }
    body.ai.alert=e.hp<body.ai.lastHealth?2.f:std::max(0.f,body.ai.alert-dt);
    body.ai.lastHealth=float(e.hp);
    e.grappleCooldown=std::max(0.f,e.grappleCooldown-dt);
    body.ai.muzzleFlash = std::max(0.f, body.ai.muzzleFlash - dt);
    auto &motion=body.ai.combatMotion;
    if(!motion.active(world.elapsed))motion.play(EnemyAction::None,0,0);
    motion.aim=std::max(0.f,motion.aim-dt*4.f);
    motion.retreat=std::max(0.f,motion.retreat-dt*5.f);
    const auto previousVelocity=body.ai.velocity;
    body.ai.velocity={};
    enemyMemory[i].remaining=std::max(0.f,enemyMemory[i].remaining-dt);
    if (e.hp <= 0 || !body.active || body.escaped){
      if(e.duel.phase!=DuelPhase::Idle)stepMechaDuel(i,nullptr,-1,dt);
      continue;
    }
    if(e.airstrikeUntil>0&&world.elapsed>=e.airstrikeUntil){
      finishAmbientActor(i,false);
      e.hp=0;body.health=0;body.active=false;body.escaped=true;
      continue;
    }
    if(!zeroGravity()&&body.ai.gravity.mode==GravityMode::Floating)body.ai.gravity={};
    body.ai.rpgFallEnabled=pilotedMecha(spec)&&!(traits&(Flying|Hover))&&!zeroGravity()&&
        !body.ai.airborne&&!body.ai.gravity.active()&&!e.grapplePilot;
    if(body.ai.rpgFallEnabled){
      const float sole=body.ai.localCenter.y-body.ai.localHalf.y;
      auto feet=body.origin+Vec3{0,sole,0};
      const float floor=world.pilotSupportHeight(feet);
      if(feet.y>floor+.85f||body.ai.fallSpeed>0){
        const float impact=stepMechaDescent(feet,floor,body.ai.fallSpeed,world.waterLevelAt(feet),dt);
        body.origin.y=feet.y-sole;
        world.updateEnemyBounds(body);
        const float radius=landingWaveRadius(impact);
        if(radius>0)addEffect(landingWave(world,feet,radius));
      }
    }else body.ai.fallSpeed=0;
    if(body.ai.gravity.active()&&!e.grapplePilot){
      const auto before=body.origin;
      advanceGravity(world,body.ai.gravity,body.origin,gravityHeight(body),gravityRadius(body),dt);
      body.ai.velocity=(body.origin-before)*(1.f/dt);
      world.updateEnemyBounds(body);
    }
    stepReactorHeat(e.heat,e.overheated,e.ventRemaining,dt,
        hasInjury(e.injuries,MechaInjury::Severe)?.07f:e.boss>=0?.14f:.11f);
    e.disabled = std::max(0.f, e.disabled - dt);
    if(e.corrosion>0){
      e.corrosion-=dt;
      if(e.corrosion<=0){
        const int oldHp=e.hp;
        e.hp=std::max(0,e.hp-dice.roll(4));
        body.health=float(e.hp);
        damageVoidmawGrapple(i,oldHp-e.hp);
        if(!e.hp){
          auto owner=std::find_if(members.begin(),members.end(),[&](const Member &m){return m.id==e.corrosionOwner;});
          finishEnemyDeath(owner==members.end()?nullptr:&*owner,i);
          if(e.corrosionOwner)noteFactionKill(i);
          continue;
        }
        e.corrosionOwner=0;
      }
    }
    if(stepFactionWithdrawal(i,dt,planningBudgets[i]))continue;
    if(e.grapplePilot){
      const auto pilot=std::find_if(members.begin(),members.end(),[&](const Member &m){return m.id==e.grapplePilot;});
      if(pilot==members.end()||pilot->ejected||pilot->body.lifeSpent||pilot->sheet.hp<=0){
        releaseVoidmawGrapple(i);
        continue;
      }
      positionVoidmawGrapple(i,*pilot);
      const auto pose=compose(body.ai.gravity.frame,angles(body.yaw,body.ai.pitch,body.ai.bank));
      const auto jaw=body.origin+rotate(pose,e.grappleMuzzle);
      const float contactAt=e.grappleAt+voidmawCanopyApproach;
      if(world.elapsed>=contactAt&&world.elapsed-dt<contactAt)
        emitKaijuSound(world,CombatCue::KaijuImpact,jaw);
      if(e.disabled<=0){
        const float previous=e.fireClock;
        e.fireClock=std::max(0.f,e.fireClock-dt);
        if(previous>voidmawBiteWindup&&e.fireClock>0&&e.fireClock<=voidmawBiteWindup)
          motion.play(EnemyAction::MeleeWindup,world.elapsed,e.fireClock);
        if(e.fireClock<=0){
          e.fireClock=1.3f;
          motion.play(EnemyAction::Strike,world.elapsed,voidmawBiteFollow);
          emitKaijuSound(world,CombatCue::KaijuBite,jaw);
          takeDamage(*pilot,std::clamp(2+spec.hd/2,2,7),false,body.origin-pilot->body.position);
        }
      }
      continue;
    }
    if(e.heavyAttack.pattern!=HeavyPattern::None){
      stepHeavyAttack(i,dt);
      continue;
    }
    if (e.disabled > 0) {
      if(e.duel.phase!=DuelPhase::Idle)stepMechaDuel(i,nullptr,-1,dt);
      e.vehiclePass={};
      if(e.chargedAttack>=0)e.fireClock=std::max(e.fireClock,.6f);
      e.chargedAttack=-1;
      e.bossTell=0;
      if(motion.action!=EnemyAction::Stagger)motion.play(EnemyAction::None,0,0);
      continue;
    }
    if(body.ai.gravity.mode==GravityMode::Floating||body.ai.gravity.mode==GravityMode::Pulling)continue;
    const bool guardWindow=e.fireClock>.72f&&e.fireClock-dt<=.72f;
    e.fireClock = std::max(0.f, e.fireClock - dt);
    e.boostEffectClock=std::max(0.f,e.boostEffectClock-dt);
    const bool boostCapable=pilotedMecha(spec)&&!hasInjury(e.injuries,MechaInjury::Hobbled)&&!e.transformed&&!(traits&(Flying|Hover));
    if(boostCapable)recharge(e.thrusters,spec.kind==EnemyKind::LargeMecha?3.f:2.f,.25f+.023f*spec.hd,dt);
    bool releasingCharge=false;
    Member *chargeVictim=nullptr;
    std::optional<Member> chargedTarget;
    if(e.chargedAttack>=0){
      if(e.chargeTarget){
        auto locked=std::find_if(members.begin(),members.end(),[&](const Member &m){return m.id==e.chargeTarget;});
        if(locked!=members.end()&&enemyCanSeeMember(*this,i,*locked))chargeVictim=&*locked;
      }else if(e.chargeWorldTarget>=0&&size_t(e.chargeWorldTarget)<world.targets.size()){
        const auto index=size_t(e.chargeWorldTarget);
        const auto &target=world.targets[index];
        if(target.active&&!target.escaped&&target.health>0&&
           (index>=enemies.size()||(enemies[index].hp>0&&factionsAtWar(*this,i,index)))){
          chargedTarget.emplace();
          chargedTarget->sheet.hp=1;
          chargedTarget->sheet.attributes.fill(8);
          auto contact=target.center;
          if(index>=enemies.size()){
            contact={std::clamp(body.center.x,target.center.x-target.half.x,target.center.x+target.half.x),
                     std::clamp(body.center.y,target.center.y-target.half.y,target.center.y+target.half.y),
                     std::clamp(body.center.z,target.center.z-target.half.z,target.center.z+target.half.z)};
          }
          chargedTarget->body.position=contact-Vec3{0,6,0};
          chargeVictim=&*chargedTarget;
        }
      }
      if(!chargeVictim||e.transformed!=e.chargedTransformed){
        e.chargedAttack=-1;
        chargeVictim=nullptr;
        e.fireClock=.6f;
        motion.play(EnemyAction::None,0,0);
      }else{
        e.chargeRemaining=std::max(0.f,e.chargeRemaining-dt);
        releasingCharge=e.chargeRemaining==0;
      }
      if(e.boss>=0)e.bossTell=chargeVictim?e.chargeRemaining:0;
    }
    e.decisionClock -= dt;
    if (traits & Regenerate) {
      e.regenClock += dt;
      if (e.regenClock >= 3) {
        e.regenClock = 0;
        e.hp = std::min(e.maxHp, e.hp + dice.roll(8));
        body.health = float(e.hp);
      }
    }
    Member *victim = nullptr;
    Pilot observer;
    observer.position = body.origin;
    float best = 1e9f;
    auto &decision=tacticalDecisions[i];
    auto &memory=enemyMemory[i];
    if(decision.memberIndex>=0 && size_t(decision.memberIndex)<members.size()) {
      auto &candidate=members[size_t(decision.memberIndex)];
      if(candidate.id==decision.targetId && enemyCanSeeMember(*this,i,candidate)) {
        victim=&candidate;best=length(candidate.body.position-body.origin);
        if(memory.remaining<=0&&best<180&&world.sounds.size()<96){
          if(spec.kind==EnemyKind::SmallKaiju || spec.kind==EnemyKind::LargeKaiju){
            if (world.elapsed >= e.radioReady){
              const bool ambush=(spec.traits&(Camo|Dismantler))&&best<60.f;
              emitKaijuSound(world,ambush?CombatCue::KaijuHiss:kaijuCall(spec),body.center);
              e.radioReady = world.elapsed + 16.f;
            }
          }else{
            npcRadio(i, RadioLine::EnemyContact);
            world.sounds.push_back({SoundEvent::Special,int(CombatCue::EnemyAlert),body.center});
          }
        }
        memory={candidate.id,candidate.body.position,4.f};
      } else tacticalClock=0;
    }
    // Search the last observed location without tracking an occluded player.
    // Concealment, ejection and death revoke this memory immediately.
    Member searchTarget;
    bool searching=false;
    if(!victim && memory.target && memory.remaining>0) {
      auto known=std::find_if(members.begin(),members.end(),[&](const Member &m){return m.id==memory.target;});
      if(known==members.end() || !known->combatControls() || known->sheet.hidden || known->hiddenRemaining>0 || known->body.lifeSpent)memory.remaining=0;
      else if(length(memory.position-body.origin)>5) {
        searchTarget.body.position=memory.position;
        victim=&searchTarget;best=length(memory.position-body.origin);searching=true;
      }
    }
    Member assetTarget;
    assetTarget.sheet.attributes.fill(8);
    bool protecting = mission.recipe.objective == 1 ||
                      mission.recipe.objective == 2 ||
                      mission.recipe.objective == 8;
    bool assetPriority = false;
    int victimTargetIndex=-1;
    const bool hostileToPlayers=factionAttacksPlayers(*this,i);
    const auto *detachment=factionDetachment(*this,i);
    Member npcTarget;
    if(!chargeVictim&&i<factionContacts.size()&&factionContacts[i]>=0){
      const auto index=size_t(factionContacts[i]);
      const auto &npc=world.targets[index];
      const float distance=length(npc.origin-body.origin);
      if(npc.active&&!npc.escaped&&enemies[index].hp>0&&factionsAtWar(*this,i,index)&&
         (!victim||searching||!hostileToPlayers||distance<best*.8f)){
        npcTarget.sheet.attributes.fill(8);npcTarget.sheet.hp=enemies[index].hp;
        npcTarget.body.position=npc.center-Vec3{0,6,0};
        victim=&npcTarget;victimTargetIndex=int(index);best=distance;searching=false;
      }
    }
    if (!detachment && !e.ambient && protecting && !(victim&&victim->id&&meleeVoidmaw(e)) &&
        !(e.group>=0&&size_t(e.group)<mission.groups.size()&&mission.groups[size_t(e.group)].site>=0&&!victim)) {
      float assetBest=best+20;
      for(int index:mission.assets) {
        if(index<0 || size_t(index)>=world.targets.size())continue;
        const auto &asset=world.targets[size_t(index)];
        float distance=length(asset.origin-body.origin);
        if(asset.active && asset.health>0 && distance<assetBest) {
          assetTarget.body.position=asset.origin;
          assetTarget.sheet.hp=150;
          victim=&assetTarget;best=distance;assetBest=distance;
          assetPriority=true;victimTargetIndex=index;searching=false;
        }
      }
    }
    Member invasionGoal;
    invasionGoal.sheet.attributes.fill(8);
    if (!detachment && !e.ambient && mission.recipe.objective == 6 && !victim) {
      invasionGoal.body.position = {0, 0, missionRadius() * .82f};
      victim = &invasionGoal;
      best = length(victim->body.position - body.origin);
    }
    // Close-combat units breach destructible cover between them and a target.
    // Structural hull pieces stay solid; the map's door network remains the route.
    Member breachTarget;
    if (victim && !searching && !body.ai.gravity.active() && std::any_of(spec.attacks.begin(),spec.attacks.end(),
                             [](const auto &a){return a.range <= 10;})) {
      Vec3 rayEnd = victim->body.bodyPoint(6);
      float first = 1;
      int obstacle = -1;
      for (size_t k=enemies.size();k<world.targets.size();++k) {
        const auto &cover=world.targets[k];
        if (!cover.active || cover.health<=0 || cover.invulnerable) continue;
        float fraction=impactFraction(body.center,rayEnd,cover.center,cover.half);
        if (fraction<first) {first=fraction;obstacle=int(k);}
      }
      if (obstacle>=0) {
        const auto &cover=world.targets[size_t(obstacle)];
        Vec3 contact{std::clamp(body.center.x,cover.center.x-cover.half.x,cover.center.x+cover.half.x),
                     std::clamp(body.center.y,cover.center.y-cover.half.y,cover.center.y+cover.half.y),
                     std::clamp(body.center.z,cover.center.z-cover.half.z,cover.center.z+cover.half.z)};
        breachTarget.sheet.attributes.fill(8);
        breachTarget.sheet.hp=150;
        breachTarget.body.position=contact-Vec3{0,6,0};
        victim=&breachTarget;
        best=length(victim->body.position-body.origin);
        assetPriority=true;victimTargetIndex=obstacle;searching=false;
      }
    }
    if(chargeVictim){
      victim=chargeVictim;
      best=length(victim->body.position-body.origin);
      searching=false;
      victimTargetIndex=chargedTarget?e.chargeWorldTarget:-1;
      assetPriority=victimTargetIndex>=0&&size_t(victimTargetIndex)>=enemies.size();
    }
    const auto *duelTarget=(!chargeVictim&&!searching&&!assetPriority)?victim:nullptr;
    if(stepMechaDuel(i,duelTarget,victimTargetIndex,dt))continue;
    if(!chargeVictim&&stepVehicleAttack(i,
          !searching&&victim&&(victim->id||victimTargetIndex>=0)?victim:nullptr,
          victimTargetIndex,dt,planningBudgets[i]))continue;
    if(!victim){
      if(detachment&&detachment->factionRole==FactionRole::Allied){
        followFactionPilots(i,dt,planningBudgets[i]);
        continue;
      }
      const auto *group=e.group>=0&&size_t(e.group)<mission.groups.size()?&mission.groups[size_t(e.group)]:nullptr;
      // Like the IS activation gate, use all pilots rather than the view
      // frustum. Weather visibility can reach 250 m even during a dark mission.
      const float wakeRange=e.distantPatrol?285.f:325.f;
      e.distantPatrol=!observers.empty()&&e.boss<0&&!detachment&&!body.ai.gravity.active()&&body.ai.alert<=0&&
        !(spec.traits&(Veriform|Combiner|Carrier|Starship))&&
        (e.ambient||(group&&group->role==int(EncounterRole::Patrol)&&group->alert<=0))&&
        std::all_of(observers.begin(),observers.end(),[&](Vec3 position){
          const auto delta=position-body.origin;
          return dot(delta,delta)>wakeRange*wakeRange;
        });
      if(!e.distantPatrol){
        e.pendingPatrolTime=0;
        patrolEnemy(i,dt,&planningBudgets[i]);
        continue;
      }
      // Stagger 5 Hz navigation and movement. Combat, damage, and status
      // timers above this branch continue to advance at the host tick rate.
      constexpr double frequency=5;
      const double offset=double((i*7)%12)/(12*frequency);
      const double now=world.elapsed+offset;
      e.pendingPatrolTime=std::min(.2f,e.pendingPatrolTime+dt);
      if(std::floor(now*frequency)==std::floor((now-dt)*frequency)){
        body.ai.velocity=previousVelocity;
        continue;
      }
      patrolEnemy(i,std::exchange(e.pendingPatrolTime,0.f),&planningBudgets[i]);
      continue;
    }
    e.distantPatrol=false;
    e.pendingPatrolTime=0;
    const bool closePursuit=victim->id&&!searching&&!assetPriority&&meleeVoidmaw(e)&&e.grappleCooldown<=0;
    if(closePursuit&&!e.overheated&&
       e.voidmawAssault.phase==VoidmawAssaultPhase::Idle&&
       startVoidmawBurrow(i,*victim))continue;
    if(victim->id&&!chargeVictim&&!searching&&!assetPriority&&!e.overheated&&
       stepVoidmawAssault(i,*victim,dt))continue;
    if (e.decisionClock <= 0) {
      e.cover = false;
      if (!e.ambient && (e.tactic == 0 || e.tactic == 2 || e.tactic == 9)) {
        float nearest = 70;
        for (size_t k = enemies.size(); k < world.targets.size(); ++k) {
          auto &cover = world.targets[k];
          if (!cover.active || cover.health <= 0 || cover.spawnType == 21 ||
              cover.half.y<1 || cover.center.y-cover.half.y>body.origin.y+12)
            continue;
          float distance = length(cover.origin - body.origin);
          Vec3 anchor =
              cover.origin + unit(cover.origin - victim->body.position) *
                                 (std::max(cover.half.x, cover.half.z) + 5);
          anchor.y = world.surfaceHeight(anchor.x, anchor.z);
          float reach=0;for(const auto &attack:spec.attacks)reach=std::max(reach,attack.range);
          if (distance < nearest && (e.tactic!=0||length(anchor-victim->body.position)<reach*.95f) && clearPosition(world, anchor, 3, int(i))) {
            e.anchorX = anchor.x;
            e.anchorZ = anchor.z;
            e.cover = true;
            nearest = distance;
          }
        }
      }
      e.decisionClock = 2.5f;
    }
    if (best <= 45)
      e.ambushed = true;
    Vec3 anchor{e.anchorX, body.origin.y, e.anchorZ};
    auto plan =
        tacticalPlan(e.tactic, body.origin, victim->body.position, spec.optimal,
                     e.fireClock, clock, unsigned(i), anchor, e.ambushed);
    Vec3 coverDestination{};
    if (e.cover && (e.tactic == 0 || e.tactic == 2)) {
      auto side = cross(unit(victim->body.position - body.origin), {0, 1, 0});
      auto desired =
          anchor + side * (e.fireClock <= 1 ? (i % 2 ? 10.f : -10.f) : 0.f);
      coverDestination=desired;
      plan.direction = length(desired - body.origin) > 2
                           ? unit(desired - body.origin)
                           : Vec3{};
    }
    if (e.tactic == 4) {
      for (size_t j = 0; j < enemies.size(); ++j)
        if (j != i && enemies[j].hp > 0) {
          auto delta = body.origin - world.targets[j].origin;
          delta.y = 0;
          if (length(delta) < 30)
            plan.direction = plan.direction + unit(delta) * 1.5f;
        }
      if (length(plan.direction) > 1)
        plan.direction = unit(plan.direction);
    }
    if (!detachment && !e.ambient && mission.recipe.objective == 6 && best > 50)
      plan.direction = unit(Vec3{0, 0, missionRadius() * .82f} - body.origin);
    Vec3 tacticalDestination=body.origin+plan.direction*30.f;
    if(e.cover && (e.tactic==0 || e.tactic==2))tacticalDestination=coverDestination;
    else if(e.tactic==0 || e.tactic==8 || e.tactic==1 || e.tactic==6) {
      auto back=body.origin-victim->body.position;back.y=0;
      float standOff=(e.tactic==1 || e.tactic==6)?7.f:spec.optimal*.9f;
      tacticalDestination=victim->body.position+unit(back)*standOff;
    }
    // Layer squad spacing, lane clearance, hazard evasion and any active
    // boost over the chassis' preferred ranged movement.
    if(!searching&&!assetPriority&&!chargeVictim&&!e.overheated&&!e.transformed&&
       (victim->id||victim==&npcTarget)&&!e.cover&&
       !(mission.recipe.objective==6&&best>50.f)&&!(e.tactic==9&&!e.ambushed))
      steerRangedMecha(mechaRangedRole(spec),spec,body,victim->body.position,world.elapsed,i,plan,tacticalDestination);
    if(victim!=&npcTarget&&!assetPriority && !searching && decision.positioning &&
       spec.kind!=EnemyKind::Installation && e.tactic!=2 &&
       !(e.tactic==9 && !e.ambushed) && !e.cover &&
       mission.recipe.objective!=6) {
      tacticalDestination=decision.destination;
      auto offset=tacticalDestination-body.origin;offset.y=0;
      // Preserve doctrine pursuit/withdrawal; formation is a bounded lateral
      // adjustment except when an ally obstructs the firing lane.
      auto formation=length(offset)>3?unit(offset):Vec3{};
      if(length(plan.direction)>.001f || !decision.fireLaneClear)
        plan.direction=unit(plan.direction+formation*(decision.fireLaneClear?.55f:1.5f));
      if(length(offset)<=3 && length(plan.direction)<.01f)plan.direction={};
    }
    if(searching) {
      tacticalDestination=memory.position;
      auto delta=memory.position-body.origin;delta.y=0;
      plan.direction=unit(delta);plan.fire=false;
    }
    bool boosting=false;
    Vec3 dodge{};
    if(!searching&&world.crushable(body)){
      for(const auto &pilot:members){
        Vec3 gap=body.origin-pilot.body.position;
        if(std::abs(gap.y)>4.f||std::hypot(gap.x,gap.z)>15.f)continue;
        gap.y=0;
        const auto approach=direction(pilot.body.chassisYaw,0)*pilot.input.forward;
        const auto boost=rotate(angles(pilot.body.facing()),pilot.body.rpgThrust);
        if(pilot.inputAge>.25f||!pilot.input.focused||
           dot(unit(gap),approach+boost)<.25f||!enemyCanSeeMember(*this,i,pilot))continue;
        const auto lateral=cross(unit(gap),Vec3{0,1,0});
        auto side=(i%2?lateral:lateral*-1.f);
        if(!clearPosition(world,body.origin+side*6.f,3.f,int(i)))side=side*-1.f;
        dodge=unit(side+unit(gap)*.5f);
        break;
      }
    }
    if(length(dodge)>.001f&&!chargeVictim){
      plan.direction=dodge;
      tacticalDestination=body.origin+dodge*15.f;
      plan.speed=std::max(plan.speed,1.4f);
    }
    if(chargeVictim){
      plan.direction={};
      tacticalDestination=body.origin;
      plan.fire=releasingCharge;
    }else if(length(dodge)<.001f&&!assetPriority&&!searching&&!e.cover&&!decision.positioning&&mission.recipe.objective!=6){
      const auto strafe=enemyRecoveryStrafe(*this,i,victim->body.position);
      if(length(strafe)>.001f){
        boosting=true;
        plan.direction=strafe;
        plan.speed*=1.65f;
        tacticalDestination=body.origin+strafe*8.f;
      }
    }
    const bool smallClimber=spec.kind==EnemyKind::SmallKaiju&&!e.transformed;
    const bool surfaceDrive=surfaceGravityAllowed(*this,body.center)||body.ai.gravity.active();
    if(boostCapable&&surfaceDrive&&victim->body.gravity.active()&&!searching&&!assetPriority&&!chargeVictim&&best<38.f){
      const auto toward=victim->body.bodyPoint(6)-body.center;
      if(length(toward)>3&&gravityRay(world,body.center,body.center+toward)){
        plan.direction=unit(toward);
        boosting=true;
      }
    }
    if(boostCapable&&!e.overheated&&!chargeVictim&&!searching&&
       (boosting||length(dodge)>.001f||(plan.preferMelee&&best>10.f&&best<25.f&&e.fireClock<.2f))&&
       length(plan.direction)>.001f&&ignite(e.thrusters,unit(plan.direction))){
      addReactorHeat(e.heat,e.overheated,e.ventRemaining,.21f);
      const auto launch=unit(e.thrusters.velocity);
      addEffect({WeaponEffect::Sparks,body.center-launch*body.half.z*.65f,launch,Weapon::Empty,0,1.6f,rpgEnemyBoost});
      world.sounds.push_back({SoundEvent::Special,int(CombatCue::EnemyThrust),body.origin});
    }
    const bool npcThrust=boostCapable&&!chargeVictim&&e.thrusters.remaining>0;
    if(npcThrust){
      plan.direction=e.thrusters.velocity;
      tacticalDestination=body.origin+plan.direction*12.f;
    }
    if(closePursuit&&!chargeVictim&&length(dodge)<.001f){
      auto approach=victim->body.position-body.origin;approach.y=0;
      const bool skitter=skitteringVoidmaw(spec)&&!body.ai.gravity.active();
      const auto heading=unit(approach,{});
      const auto lateral=cross(heading,Vec3{0,1,0})*(i%2?-1.f:1.f);
      const float flank=skitter?std::clamp((best-9.f)/12.f,0.f,.85f):0.f;
      tacticalDestination=victim->body.position+lateral*(flank*12.f);
      plan.direction=unit(heading+lateral*flank,heading);
      plan.speed=std::max(plan.speed,skitter?2.65f:1.8f);
      plan.preferMelee=true;
    }
    if(!searching&&!assetPriority&&!chargeVictim&&!e.overheated&&
       !e.transformed&&!body.ai.gravity.active())
      steerSiegeVoidmaw(spec,body,victim->body.position,world.elapsed,i,plan,tacticalDestination);
    float speed = traits & (Flying | Hover | Leap)  ? 8.f
                  : spec.kind == EnemyKind::Conventional ? 7.f
                                                         : 4.f;
    if (!(traits & (Flying | Hover)))
      speed *= environmentCatalog()[biome].speed;
    speed *= plan.speed*(e.transformed?2.f:((traits&Submersible)&&world.waterDepthAt(body.origin)<=0)?1.f:manualEnemySpeed(spec,biome));
    if(npcThrust)speed=21.f+.5f*spec.hd;
    if(hasInjury(e.injuries,MechaInjury::Hobbled))speed*=.55f;
    else if(e.overheated&&spec.kind!=EnemyKind::Installation){
      auto away=body.origin-victim->body.position;away.y=0;
      plan.direction=unit(away);
      tacticalDestination=body.origin+plan.direction*14.f;
      speed*=.85f;
    }
    const bool surfaceWalker=body.ai.gravity.mode==GravityMode::Attached;
    auto approach=victim->body.position-body.origin;
    const auto up=body.ai.gravity.up();
    if(surfaceWalker&&!searching&&!e.overheated){
      auto tangent=approach-up*dot(approach,up);
      if(smallClimber&&std::abs(up.y)<.5f&&world.indoors(body.center)&&best>8){
        const auto ascent=unit(Vec3{0,1,0}-up*up.y);
        tangent=tangent+ascent*(std::max(6.f,best*.65f)-dot(tangent,ascent));
      }
      if(length(tangent)>2&&!npcThrust)plan.direction=unit(tangent);
    }
    const auto facing=rotate(gravityInverse(body.ai.gravity.frame),approach);
    if(std::hypot(facing.x,facing.z)>.01f)body.yaw=std::atan2(-facing.x,-facing.z);
    world.updateEnemyBounds(body);
    if (spec.kind != EnemyKind::Installation) {
      bool swimming=swimmingAt(spec,world,biome,body.origin);
      bool airborne = !surfaceWalker&&((traits & (Flying | Hover))||swimming);
      const bool grappleShot=surfaceDrive&&!assetPriority&&!searching&&victim->id&&
          std::any_of(spec.attacks.begin(),spec.attacks.end(),[](const auto &a){return a.effect==AttackEffect::Pull;});
      if(grappleShot&&e.grappleCooldown<=0&&
         (victim->body.gravity.active()||!enemyFiringLaneClear(*this,i,victim->body.bodyPoint(6)))){
        const auto ray=unit(victim->body.bodyPoint(6)-body.center);
        if(auto hit=gravityRay(world,body.center,body.center+ray*30.f);
           hit&&beginGravityPull(world,body.ai.gravity,body.origin,*hit,gravityHeight(body),gravityRadius(body))){
          e.grappleCooldown=3.f;
          addEffect({WeaponEffect::Sparks,hit->point,hit->normal,Weapon::Empty,0,1.4f,rpgEnemyShieldHit});
          continue;
        }
      }
      if((smallClimber||(npcThrust&&surfaceDrive&&!searching))&&!chargeVictim){
        auto travel=plan.direction;
        if(smallClimber&&!surfaceWalker){
          approach.y=0;
          if(length(approach)>.001f)travel=unit(approach);
        }
        if(gravityImpact(world,body.ai.gravity,body.origin,travel*(speed*dt),gravityHeight(body),gravityRadius(body))){
          e.thrusters.remaining=0;
          continue;
        }
      }
      if(!surfaceWalker&&!(surfaceDrive&&npcThrust))plan.direction=enemyNavigationDirection(world,i,plan.direction,tacticalDestination,
                                               speed,dt,airborne,navigation[i],&planningBudgets[i]);
      Vec3 displacement = plan.direction * (speed * dt);
      if(!surfaceWalker)displacement.y = 0;
      if(groundDriveTank(spec)&&std::hypot(displacement.x,displacement.z)>.0001f){
        const float yaw=std::atan2(-displacement.x,-displacement.z);
        body.yaw=wrap(body.yaw+std::clamp(wrap(yaw-body.yaw),-1.75f*dt,1.75f*dt));
        const auto forward=direction(body.yaw,0);
        displacement=forward*std::max(0.f,dot(displacement,forward));
      }
      if (airborne) {
        float altitude = swimming?2.f:traits & Flying ? (closePursuit&&best<30?4.f:22.f) : 3.f;
        Vec3 ahead=body.origin+displacement;
        float floor=world.walkableHeight(ahead);
        if (biome!=2) floor+=world.waterDepthAt(ahead);
        float ceiling=world.interiorCeilingAt({ahead.x,floor+1,ahead.z},std::max(body.half.x,body.half.z));
        float top=body.ai.localCenter.y+body.ai.localHalf.y;
        float nextHeight=std::min(floor+altitude,ceiling-top-.5f);
        displacement.y=std::max(floor,nextHeight)-body.origin.y;
        if(traits&Hover)displacement.y=std::clamp(displacement.y,-3.f*dt,3.f*dt);
        else if(closePursuit&&traits&Flying)displacement.y=std::max(displacement.y,-14.f*dt);
      }
      Vec3 next = body.origin + displacement;
      // Existing foot contact still crushes cover when a melee plan stops to
      // attack. Requiring movement let large actors remain wedged in buildings.
      if (!airborne && !surfaceWalker &&
          (spec.kind==EnemyKind::LargeKaiju || spec.kind==EnemyKind::LargeMecha ||
           spec.kind==EnemyKind::Labor)) {
        Vec3 feet=next+rotate(angles(body.yaw),{body.ai.localCenter.x,0,body.ai.localCenter.z})+Vec3{0,2,0};
        for (size_t k=enemies.size();k<world.targets.size();++k) {
          const auto &cover=world.targets[k];
          if (std::abs(feet.x-cover.center.x)<=body.half.x+cover.half.x &&
              std::abs(feet.z-cover.center.z)<=body.half.z+cover.half.z &&
              cover.center.y-cover.half.y<=feet.y+2 &&
              cover.center.y+cover.half.y>=feet.y-2)
            damageScenery(k,float(spec.hd)*8*dt);
        }
      }
      const auto before=body.origin;
      if (insideArea(next, missionRadius() * .98f))
        moveRpgEnemy(i, displacement, dt, airborne);
      auto travel=body.origin-before;
      travel.y=0;
      if(boosting&&!boostCapable&&length(travel)>.015f&&e.boostEffectClock<=0){
        addEffect({WeaponEffect::Sparks,body.origin+Vec3{0,2,0},unit(travel),Weapon::Empty,0,1.6f,rpgEnemyBoost});
        world.sounds.push_back({SoundEvent::Special,int(CombatCue::EnemyThrust),body.origin});
        e.boostEffectClock=.55f;
      }
      world.updateEnemyBounds(body);
    }
    // Movement can change both range and visibility, especially for aircraft.
    observer.position=body.origin;
    best=length(victim->body.position-body.origin);
    if(closePursuit&&!e.overheated&&!chargeVictim&&length(dodge)<.001f){
      // Give a ready special attack its usual windup before committing to the
      // canopy. Otherwise the lunge would starve abilities such as Consume.
      const bool specialReady=e.fireClock<=0&&std::any_of(spec.attacks.begin(),spec.attacks.end(),[&](const auto &attack){
        return attack.effect!=AttackEffect::Damage&&attack.range<=12&&best>=attack.minimum&&best<=attack.range;
      });
      if(!specialReady&&startVoidmawGrapple(i,*victim))continue;
    }
    const Vec3 muzzle=body.center+up*(body.ai.localHalf.y*.2f);
    const Vec3 aimRay=rotate(gravityInverse(body.ai.gravity.frame),victim->body.bodyPoint(6)-muzzle);
    body.ai.weaponYaw=body.yaw;
    body.ai.weaponPitch=std::atan2(aimRay.y,std::max(.001f,std::hypot(aimRay.x,aimRay.z)));
    const float backwards=-dot(body.ai.velocity,rotate(body.ai.gravity.frame,direction(body.yaw,0)));
    if(backwards>.6f)motion.retreat=std::min(std::min(1.f,backwards/3.f),motion.retreat+dt*11.f);
    if(!searching&&!e.overheated){
      if(!chargeVictim&&guardWindow&&victim->id!=0&&best<=14&&
         bladeGuard(e,spec)&&motion.action==EnemyAction::None)
        motion.play(EnemyAction::Guard,world.elapsed,.44f);
      if(!e.transformed&&std::any_of(spec.attacks.begin(),spec.attacks.end(),[&](const EnemyAttack &attack){
        return attack.range>12&&best>=attack.minimum&&best<=attack.range;
      }))motion.aim=std::min(1.f,motion.aim+dt*9.f);
    }
    if (e.overheated || (!releasingCharge&&e.fireClock > 0) || !plan.fire ||
        !visibilityInRange(world, observer, victim->body.bodyPoint(6),
                           !assetPriority && pilotRevealed(*victim)))
      continue;
    float recovery = ((traits & Ace) ? 1.2f : 2.8f) * (e.tactic == 6   ? .65f
                                                         : e.tactic == 8 ? 1.15f
                                                                         : 1.f);
    const auto &attacks=(e.transformed?enemySpec(size_t(std::find_if(enemyCatalog().begin(),enemyCatalog().end(),[](const EnemySpec &entry){return std::string_view(entry.id)=="interceptor";})-enemyCatalog().begin())):spec).attacks;
    std::vector<const EnemyAttack*> eligible;bool inRange=false;
    const auto *lostWeapon=(!e.transformed&&e.missingArms==3)?duelWeapon(spec):nullptr;
    if(releasingCharge&&lostWeapon==&attacks[size_t(e.chargedAttack)]){
      e.chargedAttack=-1;e.bossTell=0;e.fireClock=.4f;continue;
    }
    for(const auto &attack:attacks)if(&attack!=lostWeapon&&!releasingCharge&&best>=attack.minimum&&best<=attack.range){
      inRange=true;if(!attack.recharge||dice.roll(6)>=attack.recharge)eligible.push_back(&attack);
    }
    if(plan.preferMelee&&std::any_of(eligible.begin(),eligible.end(),[](auto a){return a->range<=10;}))
      std::erase_if(eligible,[](auto a){return a->range>10;});
    // Prefer a successfully recharged special, then vary overlapping basic
    // attacks. Taking the last stat-block entry starved earlier actions forever.
    if(std::any_of(eligible.begin(),eligible.end(),[](auto a){return a->recharge>0;}))
      std::erase_if(eligible,[](auto a){return a->recharge==0;});
    // Out-of-range pursuit is not a fired attack. Fake cooldowns made hit-and-
    // run units retreat forever without first getting close enough to shoot.
    if(!releasingCharge&&eligible.empty()){if(inRange)e.fireClock=recovery;continue;}
    const auto *attack=releasingCharge?&attacks.at(size_t(e.chargedAttack)):
        eligible[eligible.size()==1?0:size_t(dice.roll(int(eligible.size()))-1)];
    const bool kaiju=spec.kind==EnemyKind::SmallKaiju||spec.kind==EnemyKind::LargeKaiju;
    if(releasingCharge){
      e.chargedAttack=-1;
      if(best<attack->minimum||best>attack->range||
         !enemyFiringLaneClear(*this,i,victim->body.bodyPoint(6),victimTargetIndex)){
        e.fireClock=.6f;
        if(attack->range<=12){
          motion.play(EnemyAction::Strike,world.elapsed,.46f);
          addEffect({WeaponEffect::Sparks,muzzle,unit(victim->body.bodyPoint(6)-muzzle),Weapon::Empty,0,1.7f,rpgNpcMeleeSlash});
          if(kaiju)emitKaijuSound(world,CombatCue::KaijuSwipe,muzzle);
          else if(world.sounds.size()<96)
            world.sounds.push_back({SoundEvent::Special,int(CombatCue::Melee),muzzle});
        }
        continue;
      }
    }
    std::vector<Member *> victims;
    if (best >= attack->minimum && best <= attack->range &&
        enemyFiringLaneClear(*this,i,victim->body.bodyPoint(6),victimTargetIndex))
      victims.push_back(victim);
    for (auto &m : members)
      if ((victimTargetIndex<0||size_t(victimTargetIndex)>=enemies.size()) &&
          &m!=victim && enemyCanSeeMember(*this,i,m) &&
          length(m.body.position - body.origin) >= attack->minimum &&
          length(m.body.position - body.origin) <= attack->range &&
          visibilityInRange(world,observer,m.body.bodyPoint(6),pilotRevealed(m)) &&
          enemyFiringLaneClear(*this,i,m.body.bodyPoint(6)))
        victims.push_back(&m);
    const bool primaryFirst=!victims.empty()&&victims.front()==victim;
    std::sort(victims.begin()+(primaryFirst?1:0), victims.end(), [&](auto a, auto b) {
      return length(a->body.position - body.origin) <
             length(b->body.position - body.origin);
    });
    if(victims.empty()){e.fireClock=.15f;continue;}
    const auto shotDirection=unit(victims.front()->body.bodyPoint(6)-muzzle);
    const Member &firstTarget=*victims.front();
    if(!releasingCharge&&startHeavyAttack(i,*attack,firstTarget,int(attack-attacks.data()),muzzle,victimTargetIndex))continue;
    const bool closeCombat=attack->range<=12;
    const bool chargeRanged=firstTarget.id&&(e.boss>=0||(attack->recharge&&attack->dice*attack->sides>=18));
    if(!releasingCharge&&((closeCombat&&(firstTarget.id||victimTargetIndex>=0))||chargeRanged)){
      e.chargedAttack=int(attack-attacks.data());
      e.chargeTarget=firstTarget.id;
      e.chargeWorldTarget=firstTarget.id?-1:victimTargetIndex;
      e.chargedTransformed=e.transformed;
      e.chargeRemaining=e.boss>=0?campaignBoss(e.boss).windup:closeCombat?.48f:.75f;
      motion.play(closeCombat?EnemyAction::MeleeWindup:EnemyAction::RangedWindup,world.elapsed,e.chargeRemaining);
      if(e.boss>=0)e.bossTell=e.chargeRemaining;
      if(closeCombat){
        const float age=std::max(0.f,enemyEffectLife(rpgEnemyMeleeTell)-e.chargeRemaining);
        addEffect({WeaponEffect::Sparks,muzzle,shotDirection,Weapon::Empty,age,1.8f,rpgEnemyMeleeTell});
      }else{
        addEffect({WeaponEffect::Sparks,muzzle,shotDirection,Weapon::Rail,0,2.5f,e.boss>=0?rpgEnemyBossCharge:rpgEnemyCharge});
        if(!kaiju)world.sounds.push_back({SoundEvent::Special,int(CombatCue::Charge),muzzle});
      }
      if(kaiju)emitKaijuSound(world,kaijuWindupCue(*attack),muzzle);
      continue;
    }
    e.fireClock=(e.boss>=0?.65f+.45f*(2-e.bossPhase):recovery)*(e.missingArms?1.3f:1.f);
    const std::string_view attackName=attack->name;
    const bool guided=attack->range>12&&(attackName.find("Missile")!=attackName.npos||
        attackName.find("Rocket")!=attackName.npos);
    const bool disruption=attack->effect==AttackEffect::Reactor||attack->effect==AttackEffect::DoubleReactor||
        attack->effect==AttackEffect::Blind||attack->effect==AttackEffect::Stun||
        attack->effect==AttackEffect::DisruptMobility||attack->effect==AttackEffect::DisruptSystems;
    const bool energy=(releasingCharge&&firstTarget.id)||attackName.find("Beam")!=std::string_view::npos||
        attackName.find("Particle")!=std::string_view::npos||attackName.find("Plasma")!=std::string_view::npos;
    const unsigned effect=closeCombat?rpgNpcMeleeSlash:disruption?rpgEnemyDisruption:
        energy?rpgEnemyEnergy:rpgEnemyKinetic;
    const Weapon weapon=guided?Weapon::Rocket:disruption||(energy&&attack->range>12)?Weapon::Rail:Weapon::Rifle;
    int fired = 0;
    bool emitted=false;
    for (auto *m : victims) {
      if (fired++ >= attack->targets)
        break;
      int damage = 0;
      for (int n = 0; n < attack->dice+manualEnemyExtraDice(*this,i); ++n)
        damage += dice.roll(attack->sides);
      damage+=manualEnemyDamageBonus(*this,i);
      const float targetRange=length(m->body.position-body.origin);
      if (e.spec == 4 && targetRange >= 60)
        damage += dice.roll(4);
      if (e.spec == 9 && targetRange <= 10)
        damage += dice.roll(4);
      for (size_t ally = 0; ally < enemies.size(); ++ally)
        if (ally != i && enemies[ally].hp > 0 && !factionsAtWar(*this,i,ally) &&
            (combatSpec(enemies[ally]).traits & Commander) &&
            length(world.targets[ally].origin - body.origin) < 30) {
          damage += dice.roll(4);
          break;
        }
      Vec3 origin = muzzle;
      auto ray = m->body.bodyPoint(6) - origin;
      auto defenseValue=[&](int attribute){float value=float(m->sheet.attributes[attribute])+(attribute==1&&m->nodeMobility>0?1.f:0.f);if(injuryDisadvantage(m->injuries,attribute))value*=.5f;if((attribute==1&&m->mobilityDisrupt>0)||(attribute==2&&m->systemDisrupt>0))value*=.65f;return value;};
      int defense=attack->defense;
      if(attack->alternateDefense>=0&&defenseValue(attack->alternateDefense)>defenseValue(defense))defense=attack->alternateDefense;
      float spread =
          .016f * (.5f + (float(m->sheet.attributes[defense])+(defense==1&&m->nodeMobility>0?1.f:0.f)) / 14.f) /
          std::sqrt(float(spec.hd));
      if(m->mobilityDisrupt>0&&defense==1)spread*=.65f;
      if(m->systemDisrupt>0&&defense==2)spread*=.65f;
      if(injuryDisadvantage(m->injuries,defense))spread*=.5f;
      if(m->sheet.armor==Armor::Heavy&&(m->sheet.chassis==Chassis::Scout||m->sheet.chassis==Chassis::Striker))spread*=.5f;
      spread *= plan.spread;
      if(hasInjury(e.injuries,MechaInjury::Targeting))spread*=2.6f;
      if(e.missingArms&&attack->range<=12)damage=std::max(1,damage/(e.missingArms==3?3:2));
      if(e.missingArms==3&&attack->range>12)damage=std::max(1,damage/2);
      if (std::any_of(members.begin(),members.end(),[&](const Member &cover){return cover.barricade&&!cover.ejected&&cover.sheet.hp>0&&length(cover.body.position-m->body.position)<=10;}))
        spread *= 2;
      Shot shot;
      shot.body.position = origin;
      shot.body.velocity =
          deviated(dice, ray, spread) * (length(ray) <= 10 ? 110.f : 70.f);
      shot.body.damage = float(attack->effect==AttackEffect::Consume?damage:std::max(1,int(std::lround(damage*.72f))));
      shot.body.mount = -1;
      shot.body.owner = int(i);
      shot.body.life = 3;
      shot.body.weapon = weapon;
      if(guided){shot.body.kind=ProjectileKind::Rocket;shot.body.hull=2.f;}
      shot.effect = attack->effect;
      shot.reactor = attack->reactor;
      shot.meleeStrike=attack->range<=12;
      if(shot.meleeStrike)shot.body.life=.16f;
      if(energy&&!disruption)shot.quality=Quality::Thermal;
      if(launch(std::move(shot))) {
        body.ai.muzzleFlash=.12f;
        if(!emitted){
          addEffect({WeaponEffect::Sparks,origin,shotDirection,weapon,0,1.7f,effect});
          if(kaiju){
            emitKaijuSound(world,kaijuAttackCue(*attack),origin);
            e.radioReady=world.elapsed+16.f;
          }else if(closeCombat){
            world.sounds.push_back({SoundEvent::Special,int(CombatCue::Melee),origin});
          }else{
            world.sounds.push_back({SoundEvent::Shot,int(weapon),origin});
          }
          emitted=true;
        }
      }
    }
    if(emitted){
      if(!kaiju)
        npcRadio(i, detachment&&detachment->factionRole==FactionRole::Allied&&!detachment->retaliating
                        ? RadioLine::FriendlySupport : RadioLine::EnemyAttack);
      motion.play(closeCombat?EnemyAction::Strike:EnemyAction::Fire,world.elapsed,closeCombat?.46f:.28f);
      addReactorHeat(e.heat,e.overheated,e.ventRemaining,disruption?.55f:energy?.48f:attack->range<=12?.12f:.32f);
      if(e.overheated){
        addEffect({WeaponEffect::Sparks,body.center,{0,1,0},Weapon::Empty,0,1.6f,rpgEnemyBoost});
        if(kaiju)emitKaijuSound(world,CombatCue::KaijuHiss,body.center);
        else world.sounds.push_back({SoundEvent::Special,int(CombatCue::EnemyThrust),body.center});
      }
    }
    redeployCarrierAllies(i,best);
  }
  ambientKaijuCall(observers);
  reinforceManualEnemies();
  stepAmbientEncounters(dt);
}
void Session::trapsStep(float) {
  for (auto &h : traps)
    if (!h.spent)
      for (auto &m : members)
        if (!m.ejected && m.sheet.hp > 0 &&
            length(m.body.position - h.position) < 12) {
          h.spent = true;
          takeDamage(m,dice.roll(6)+dice.roll(6),false,h.position-m.body.position);
          for (size_t i = 0; i < enemies.size(); ++i)
            if (length(world.targets[i].origin - h.position) < 100)
              enemies[i].ambushed = true;
          addEffect(
              {WeaponEffect::Blast, h.position, {}, Weapon::Grenade, 0, 5});
          break;
        }
}
void Session::step(float dt) {
  if (!std::isfinite(dt) || dt <= 0 || dt > .1f ||
      (phase != Phase::Combat && phase != Phase::Complete && phase != Phase::Defeat))
    return;
  world.sounds.clear();
  environmentCycle.advance(dt);
  updateEnvironment();
  // Cinematics keep advancing after defeat and when tracking/focus is lost.
  for (auto &m : members) {
    if (m.ejected && !m.body.lifeSpent) eject(m);
    if (m.body.lifeSpent && m.body.ejectionTime < 7) {
      m.body.rpgInjuries=m.injuries;m.body.rpgMissingArms=m.missingArms;
      static_cast<Pilot &>(world) = m.body;
      world.stepEjection(dt);
      m.body = world;
    }
  }
  world.elapsed += dt;
  stepDestruction(dt);
  if (phase == Phase::Defeat) return;
  clock += dt;
  if (phase == Phase::Combat)
    stageOpener(dt);
  const bool cinematicDescent=phase==Phase::Combat&&insertionLocked();
  for (auto &m : members) {
    m.body.health = 100.f * m.sheet.hp / std::max(1, m.sheet.maxHp);
    m.vehicleBlend=std::clamp(m.vehicleBlend+(m.vehicle?1.f:-1.f)*dt/1.6f,0.f,1.f);
    m.body.rpgVehicleBlend=m.vehicleBlend;
    m.body.rpgLanding=std::max(0.f,m.body.rpgLanding-dt*3.9f);
    if(m.combatControls()&&m.body.gravity.active())
      advanceGravity(world,m.body.gravity,m.body.position,13.f,3.5f,dt);
    if(!zeroGravity()&&m.body.gravity.mode==GravityMode::Floating)m.body.gravity={};
    if(!cinematicDescent)stepPilotFall(m,dt);
    stepChassis(m,dt);
    m.sheet.stepThermal(dt);
    stepManual(m,dt);
    m.pistonCooldown=std::max(0.f,m.pistonCooldown-dt);
    for (float *timer :
         {&m.weatherJam,&m.hiddenRemaining, &m.shieldRemaining, &m.stun, &m.blind, &m.boost,
          &m.frameShieldDelay, &m.scanRemaining, &m.scanExposureRemaining, &m.chassisCooldown})
      *timer = std::max(0.f, *timer - dt);
    if (m.hiddenRemaining == 0)
      m.sheet.hidden = false;
    if(m.shieldRemaining==0)m.shield=0;
    if(m.shield==0&&m.frameShieldDelay<=0&&!m.ejected&&m.sheet.hp>0&&m.sheet.has(Module::RetractableShield)){
      m.shield=2;m.shieldRemaining=30.f;m.frameShieldDelay=10.f;
    }
    for (auto &r : m.recoil)
      r *= std::exp(-5.f*dt);
    if (m.ongoing > 0) {
      m.ongoing -= dt;
      m.ongoingTick -= dt;
      if (m.ongoingTick <= 0) {
        m.ongoingTick = 3;
        takeDamage(m, m.ongoingDamage);
      }
    }
    if(m.scanRemaining==0){m.scannedContacts.clear();m.scannedActors.clear();m.scannedBearings=0;}
    stepSensors(m,dt);
    for(auto &timer:m.parryWindow)timer=std::max(0.f,timer-dt);
    m.comboRemaining=std::max(0.f,m.comboRemaining-dt);
    m.riposteRemaining=std::max(0.f,m.riposteRemaining-dt);
    m.criticalRemaining=std::max(0.f,m.criticalRemaining-dt);
    m.inputAge += dt;
    auto in = m.input;
    if (m.inputAge > .25f)
      in.focused = false;
    const bool knocked=m.body.rpgKnockdown.active(world.elapsed);
    const bool grappled=std::any_of(enemies.begin(),enemies.end(),[&](const auto &e){return e.grapplePilot==m.id;});
    bool active = in.focused && in.headValid && m.combatControls();
    if(m.repairHold.target){
      const int slot=m.repairHold.slot;
      if(!active||!in.fire[slot]||m.stun>0||m.sheet.overheated||m.vehicle||m.droneControl||
         m.body.cooldown[slot]>.000001f||m.blocked[slot]||m.sheet.equipment[slot]!=Gear::Repair||
         m.sheet.inventory[size_t(Gear::Repair)]<=0)
        m.repairHold={};
    }
    if (!active) {
      for(int hand=0;hand<2;++hand)interruptWindup(m,hand);
      if(m.drone>0){Input idle;idle.focused=false;stepDrone(m,idle,dt);}
      m.fireModeHeld=true;
      m.parryWindow.fill(0);m.swingMotion={};
      m.thrusters.remaining=m.thrusters.liftRemaining=0;
      m.thrusters.thrustHeld=!in.focused||in.thrust;
      m.thrusters.ascendHeld=!in.focused||in.ascend;
      m.body.rpgThrust={};
      m.vehicleSpeed=0;
      m.handSupported.fill(false);m.heavySupported[0]=false;
      m.stashGripHeld=in.ping;
      m.blocked = in.fire;
      m.held = in.fire;
      m.punchHeld = in.punch;
      m.chassisHeld = m.input.ping[1];
      for (auto &g : m.gestures)
        g.reset();
      for (auto &a : m.body.arms)
        a.tracked = false;
      for (auto &c : m.body.cooldown)
        c = std::max(0.f, c - dt);
      continue;
    }
    if(phase==Phase::Combat&&!insertionLocked()&&m.stun<=0&&m.combatControls()&&!m.sheet.overheated){
      if(in.fireMode&&!m.fireModeHeld&&
         (selectableFireMode(m.sheet.equipment[LeftHand])||selectableFireMode(m.sheet.equipment[RightHand])))
        m.rifleSemi=!m.rifleSemi;
    }
    m.fireModeHeld=in.fireMode;
    std::array<bool,2> shoulderGrip{};
    for(int hand=0;hand<2;++hand){
      shoulderGrip[hand]=overShoulder(in.handPoses[hand],hand);
      bool press=in.ping[hand]&&!m.stashGripHeld[hand];
      // Losing tracking while gripping requires a release before re-grabbing.
      m.stashGripHeld[hand]=in.ping[hand];
      if(press&&shoulderGrip[hand]&&m.body.armAvailable(hand)&&!m.vehicle&&
         m.vehicleBlend<=.01f&&!m.droneControl&&!insertionLocked()&&
         m.combatControls()&&!m.sheet.overheated&&m.stun<=0&&equipmentOwner(m.sheet,hand)==hand&&
         (ranged(m.sheet.equipment[hand])||melee(m.sheet.equipment[hand]))){
        m.handStashed[hand]=!m.handStashed[hand];
        m.weaponCycles[hand]={};m.lanceReady[hand]=false;m.busterReady[hand]=false;
        m.recoil[hand]=0;m.gestures[hand].reset();
        m.blocked[hand]=true;
        if(pairedWeapon(m.sheet.chassis,m.sheet.equipment[hand]))m.blocked[hand^1]=true;
      }
      if(shoulderGrip[hand])in.ping[hand]=false;
    }
    if(m.drone<=0||knocked||grappled||m.sheet.hp==0)m.droneControl=false;
    if(m.drone>0){
      if(!knocked&&!grappled&&m.sheet.hp>0&&in.ping[0]&&!m.droneGripHeld){
        m.droneControl=!m.droneControl;
        m.droneDetonateHeld=true;
      }
      m.droneGripHeld=m.input.ping[0];
      const bool piloting=m.droneControl;
      stepDrone(m,in,dt);
      if(piloting){
        for(int hand=0;hand<2;++hand)interruptWindup(m,hand);
        m.thrusters.remaining=m.thrusters.liftRemaining=0;m.body.rpgThrust={};m.body.walkingSpeed=0;
        m.blocked=in.fire;m.held=in.fire;
        for(auto &arm:m.body.arms)arm.tracked=false;
        continue;
      }
    }
    // Native IK uses the carried weapon category; RPG models remain selected
    // by the presentation names rebuilt in view().
    for (int slot=0; slot<MountCount; ++slot) {
      auto gear = m.sheet.equipment[slot];
      m.body.weapons[slot] = (equipmentOwner(m.sheet,slot)!=slot||(slot<2&&m.handStashed[slot]))?Weapon::Empty:ranged(gear) ? Weapon::Rifle :
                              melee(gear) ? Weapon::Cutter : Weapon::Empty;
    }
    m.body.rpgInjuries=m.injuries;m.body.rpgMissingArms=m.missingArms;
    m.body.rpgChassis=int(m.sheet.chassis);
    static_cast<Pilot &>(world) = m.body;
    world.teammates.clear();
    for (auto &p : members)
      if (p.id != m.id)
        world.teammates.push_back(&p.body);
    world.cooperative = true;
    Input movement = in;
    movement.fire.fill(false);
    movement.ping.fill(false);
    movement.punch.fill(false);
    movement.handValid.fill(false);
    // Recoil belongs to the tracked firing pose, rather than a visual-only
    // world-X rotation. IK, muzzle, reticle and projectile all use this pose.
    for(int hand=0;hand<2;++hand){
      float recoil=m.recoil[hand];
      movement.handPoses[hand].position=movement.handPoses[hand].position+rotate(movement.handPoses[hand].aim,{0,.08f*recoil,.18f*recoil});
      auto kick=angles(0,recoil);
      movement.handPoses[hand].aim=compose(movement.handPoses[hand].aim,kick);
      movement.handPoses[hand].grip=compose(movement.handPoses[hand].grip,kick);
    }
    bool inserting = insertionLocked();
    if (inserting||knocked) {
      movement.steer = 0;
      movement.turn = 0;
    }
    const bool mobile=!inserting&&!knocked&&(phase==Phase::Complete||
        (m.combatControls()&&m.stun<=0&&m.immobile<=0&&!m.siege&&!m.barricade));
    if(!mobile)movement.forward=0;
    else if (!m.vehicle)
      movement.forward *=
          environmentCatalog()[biome].speed *
          (mission.recipe.opener == 4 && mission.openerTime < 8 ? .6f : 1.f);
    movement.rpgWalkScale=std::clamp(float(m.sheet.attributes[1])/12.f,.40f,1.65f)*
        (m.mobilityDisrupt>0?.65f:1.f)*(m.sheet.overheated?.68f:1.f);
    if(m.garrison)movement.rpgWalkScale*=.56f;
    if(grappled)movement.rpgWalkScale*=.48f;
    movement.forward *= world.waterMovementScale(m.body.position,3.5f);
    if(m.sheet.armor==Armor::Heavy)movement.forward*=m.sheet.chassis==Chassis::Scout?.78f:.88f;
    bool transforming=m.vehicleBlend>.001f&&(!m.vehicle||m.vehicleBlend<.999f);
    bool driving=m.vehicle&&m.vehicleBlend>=.999f&&mobile;
    float speed = m.boost > 0 ? 2.f : 1.f;
    if(transforming||driving){movement.forward=0;movement.steer=0;movement.turn=0;}
    const Vec3 previous=m.body.position;
    const size_t firstFootstep=world.sounds.size();
    const bool thrusting=boostMember(m,in,phase==Phase::Combat&&mobile&&!transforming&&!driving&&!m.vehicle&&!grappled,dt);
    if(thrusting){movement.forward=0;movement.steer=0;}
    world.step(movement, dt, false);
    if(driving){
      float throttle=std::abs(in.forward)>.15f?in.forward:0,steer=std::abs(in.steer)>.15f?in.steer:0;
      float desired=throttle*(throttle<0?12.f:24.f)*mountedModuleOutput(m.sheet,Gear::VehicleMode)*(m.sheet.overheated?.68f:1.f)*injuryMovementScale(m.injuries)*world.waterMovementScale(m.body.position,3.5f);
      m.vehicleSpeed+=std::clamp(desired-m.vehicleSpeed,-18.f*dt,12.f*dt);
      world.chassisYaw=wrap(world.chassisYaw-steer*1.1f*dt*(.35f+.65f*std::min(1.f,std::abs(m.vehicleSpeed)/10)));
      world.torsoYaw+=std::clamp(-world.torsoYaw,-2.f*dt,2.f*dt);
      Vec3 start=world.position;world.movePilot(world.position+direction(world.chassisYaw,0)*(m.vehicleSpeed*dt));
      if(length(world.position-start)<std::abs(m.vehicleSpeed)*dt*.1f)m.vehicleSpeed=0;
      world.walkingSpeed=0;world.aimYaw=world.facing()+in.headYaw;
    }else m.vehicleSpeed=0;
    m.body = world;
    for(int hand=0;hand<2;++hand){
      const auto *tuning=weaponTuning(m.sheet.equipment[hand]);
      auto &arm=m.body.arms[hand];
      if(tuning&&arm.tracked&&handOccupied(m,hand))
        arm.muzzle=arm.wrist+rotate(arm.aim,tuning->muzzle);
    }
    if (speed > 1&&!transforming&&!driving&&!thrusting) {
      Vec3 proposed =
          m.body.position + (m.body.position - previous) * (speed - 1);
      world.movePilot(proposed);
      m.body.position = world.position;
    }
    if (!insideArea(m.body.position, missionRadius() * .98f))
      m.body.position = previous;
    if(phase==Phase::Combat&&mobile&&!driving&&!transforming){
      const size_t lastFootstep=world.sounds.size();
      for(size_t i=firstFootstep;i<lastFootstep;++i)
        if(world.sounds[i].kind==SoundEvent::Footstep)crushFoot(world.sounds[i].position,&m);
    }
    const auto previousSupport=m.handSupported;
    m.handSupported.fill(false);
    for(int hand=0;hand<2;++hand){
      const int off=hand^1;
      auto &primary=m.body.arms[hand],&support=m.body.arms[off];
      if(!handOccupied(m,hand)||handOccupied(m,off)||!in.ping[off]||!primary.tracked||!support.tracked||
         m.vehicleBlend>.01f||inserting)continue;
      const Vec3 grip=primary.wrist+rotate(primary.aim,{hand?-.28f:.28f,.05f,-1.10f});
      if(length(support.wrist-grip)>(previousSupport[hand]?3.15f:2.45f))continue;
      m.handSupported[hand]=true;
      braceArm(support,off,primary,grip,world.cockpitOrientation(),world.armRig,armRestFor(world.rpgChassis,off));
    }
    m.heavySupported={false,true,true};
    if(m.sheet.chassis==Chassis::Vanguard)m.heavySupported[0]=heavy(m.sheet.equipment[LeftHand])||heavy(m.sheet.equipment[RightHand]);
    else if(heavy(m.sheet.equipment[LeftHand])){
      const int owner=equipmentOwner(m.sheet,LeftHand);
      m.heavySupported[0]=m.handSupported[owner];
    }
    for (int hand = 0; hand < 2; ++hand) {
      if(m.handSupported[hand^1]){
        m.gestures[hand].reset();
        m.punchHeld[hand]=in.punch[hand];
        continue;
      }
      const auto motion=in.hands[hand]-m.gestures[hand].previous;
      const bool swing=m.gestures[hand].sample(
          in.hands[hand],in.handValid[hand]&&m.body.arms[hand].tracked,dt);
      if(swing||(in.punch[hand]&&!m.punchHeld[hand])){
        const bool blade=handOccupied(m,hand)&&melee(m.sheet.equipment[hand])&&
            m.body.armAvailable(hand)&&m.body.arms[hand].tracked;
        m.swingMotion=swing?motion:Vec3{};
        const bool shield=m.sheet.equipment[hand]==Gear::Shield&&!m.handStashed[hand];
        if((blade||shield)&&m.combatControls()&&!m.sheet.overheated&&m.stun<=0&&swing)m.parryWindow[hand]=shield?.42f:.30f;
        if(!shield)activate(m,hand,!blade);
        m.swingMotion={};
      }
      m.punchHeld[hand] = in.punch[hand];
    }
    if (in.ping[1] && !m.chassisHeld&&!m.handSupported[LeftHand])
      useChassis(m);
    m.chassisHeld = m.input.ping[1];
    auto fire=in.fire;
    for(int slot=0;slot<MountCount;++slot){int owner=equipmentOwner(m.sheet,slot);if(owner!=slot){fire[owner]=fire[owner]||fire[slot];fire[slot]=false;}}
    for(int hand=0;hand<2;++hand){
      if(!fire[hand] || !m.body.arms[hand].tracked)interruptWindup(m,hand);
      stepWeaponBurst(m,hand,dt);
    }
    if(m.repairHold.target){
      auto &target=naniteTarget(m);
      if(target.id!=m.repairHold.target || target.emergency!=HullEmergency::AwaitRepair){
        if(target.emergency!=HullEmergency::AwaitRepair)m.blocked[m.repairHold.slot]=true;
        m.repairHold={};
      }
    }
    for (int slot = 0; slot < MountCount; ++slot) {
      if (!fire[slot]){m.blocked[slot] = false;m.beltTarget[slot]=-1;}
      if(fire[slot]&&!m.blocked[slot]&&(!melee(m.sheet.equipment[slot])||!m.held[slot]||weaponFamily(m.sheet.equipment[slot])==Gear::ChainCutter))
        activate(m,slot);
      m.held[slot] = fire[slot];
    }
  }
  world.teammates.clear();

  if (phase == Phase::Combat && !insertionLocked()){
    stepWeatherHazards();
    // Enemy movement needs every live player, including idle/unfocused pilots.
    // Keep these transient pointers out of snapshots and Session copies.
    world.cooperative=true;for(auto &m:members)world.teammates.push_back(&m.body);
    enemiesAct(dt);world.teammates.clear();
  }
  stepProjectiles(dt);
  if(phase==Phase::Combat&&!insertionLocked()){stepDungeonSites();collectSupplies();}
  if (phase == Phase::Combat && !insertionLocked()) trapsStep(dt);
  if(phase==Phase::Combat){
    for(auto &m:members){
      if(m.emergency!=HullEmergency::None && clock+tickTolerance>=m.ejectAt)applyDisabledResult(m,8);
    }
    const bool operational=std::any_of(members.begin(),members.end(),[](const Member &m){return !m.ejected&&m.sheet.hp>0;});
    const bool pending=std::any_of(members.begin(),members.end(),[](const Member &m){
      return m.recoveryPending();
    });
    if(!operational && !pending){phase=Phase::Defeat;log("FIRETEAM DISABLED");}
    else if(operational && !insertionLocked())stepMission(dt);
  }
  if(phase==Phase::Defeat){
    mission.encounterCredits=0;
    for(auto &m:members){m.pendingRecovery.clear();m.pendingDepotSalvage=0;m.fieldAmmo=false;m.explosive=0;}
  }
  if(phase!=Phase::Combat)
    for(size_t i=0;i<enemies.size();++i)releaseVoidmawGrapple(i);
}
Json Session::snapshot(const coop::SceneryBaseline *baseline) const {
  Json players = Json::array(), trapData = Json::array();
  auto savedCampaign=campaign;savedCampaign.seed=seed;savedCampaign.revision=boardRevision;
  for (auto &m : members){
    Json sheet=m.sheet;bool shared=sheet.at("campaign")==Json(savedCampaign);if(shared)sheet.erase("campaign");
    players.push_back({{"id", m.id},
                       {"name", m.name},
                       {"ready", m.ready}, {"in_sortie",m.inSortie},
                       {"body", coop::pilotWire(m.body)},
                       {"cooldowns", m.body.cooldown},
                       {"sheet", sheet},{"shared_campaign",shared},
                       {"injuries", m.injuries},
                       {"missing_arms", m.missingArms},
                       {"debrief",m.debriefAccepted},{"configured",m.configured},{"configuration",m.configuration},{"campaign_awarded",m.campaignAwarded},{"heavy_supported",m.heavySupported},{"hand_supported",m.handSupported},{"hand_stashed",m.handStashed},{"recoil",m.recoil},
                       {"manual",{{"funnels",m.funnels},{"nodes",m.shieldNodes},{"veriform",m.veriform},{"precog",m.precog},{"immobile",m.immobile},{"mobility",m.mobilityDisrupt},{"systems",m.systemDisrupt},{"node_mobility",m.nodeMobility},{"range_extension",m.rangeExtension},{"precog_target",m.precogTarget},{"clock",m.manualClock},{"piston",m.pistonCooldown},{"buster",m.busterReady},{"belt",m.beltTarget}}},
                       {"ejected", m.ejected},
                       {"shield", m.shield},
                       {"cloak", m.hiddenRemaining},
                       {"siege", m.siege},
                       {"barricade", m.barricade},
                       {"vehicle_blend",m.vehicleBlend},{"vehicle_speed",m.vehicleSpeed},
                       {"vehicle", m.vehicle},{"drone_time",m.drone},{"scan_remaining",m.scanRemaining},{"scan_duration",m.scanDuration},
                       {"scan_exposure",m.scanExposureRemaining},{"active_scanners",m.activeScanners},{"next_sensor_pulse",m.nextSensorPulse},{"weather_jam",m.weatherJam},{"scan_sweep",m.scanSweep},{"scan_contacts",m.scannedContacts},{"scan_actors",m.scannedActors},{"scan_bearings",m.scannedBearings},
                       {"rifle_semi",m.rifleSemi},{"combat_feedback",{m.comboCount,m.comboRemaining,m.riposteRemaining,m.criticalRemaining}},{"drone_position",point(m.dronePosition)},{"drone_yaw",m.droneYaw},{"drone_pitch",m.dronePitch},{"drone_control",m.droneControl}});
    for(int hand=0;hand<2;++hand){
      const auto &cycle=m.weaponCycles[hand];
      if(cycle.active(world.elapsed))
        players.back()["weapon_cycles"].push_back({hand,int(cycle.stage),cycle.started,cycle.duration});
    }
    if(m.garrison)players.back()["garrison"]=true;
    if(m.reinforced)players.back()["reinforced"]=true;
    if(m.berserkRetreat>0)players.back()["berserk_retreat"]=m.berserkRetreat;
    if(m.lastFault && clock<m.faultNoticeUntil)players.back()["fault_notice"]={m.lastFault,m.faultNoticeUntil};
    if(m.criticalFaultRolled)players.back()["critical_fault"]=true;
    if(m.emergency!=HullEmergency::None)players.back()["hull_emergency"]={int(m.emergency),m.ejectAt};
    if(m.repairHold.target)players.back()["nanite_hold"]={m.repairHold.target,m.repairHold.slot,m.repairHold.started};
    if(!m.pendingRecovery.empty())players.back()["pending_recovery"]=m.pendingRecovery;
    if(m.pendingDepotSalvage)players.back()["depot_caches"]=m.pendingDepotSalvage;
    if(!m.recoveredGear.empty())players.back()["recovered_gear"]=m.recoveredGear;
    if(m.explosive>0)players.back()["loaded_rounds"]=true;
    if(m.fieldAmmo)players.back()["field_ammo"]=true;
    if(m.body.rpgKnockdown.active(world.elapsed))
      players.back()["knockdown"]={m.body.rpgKnockdown.started,m.body.rpgKnockdown.side};
    if(m.body.gravity.active())players.back()["surface_gravity"]=gravityWire(m.body.gravity);
  }
  Json enemyData=enemies;
  for(size_t i=0;i<enemies.size();++i){
    if(enemies[i].ambient)enemyData[i]["ambient"]=true;
    if(enemies[i].grapplePilot)enemyData[i]["grapple_pilot"]=enemies[i].grapplePilot;
    const auto &target=world.targets[i];
    const auto &pose=target.ai.voidmawPose;
    if(target.active&&!target.escaped&&enemies[i].hp>0&&pose.phase!=VoidmawPose::Default)
      enemyData[i]["voidmaw_pose"]={int(pose.phase),pose.started,pose.duration,pose.side};
    if(world.targets[i].ai.gravity.active())
      enemyData[i]["surface_gravity"]=gravityWire(world.targets[i].ai.gravity);
  }
  for (auto &h : traps)
    trapData.push_back({point(h.position), h.spent});
  Json supplyData=Json::array();
  for(const auto &drop:supplies){
    // Older clients render both encounter crates using the garrison visual.
    const bool field=drop.kind==SafeBoxKind::FieldSupplies;
    auto row=Json::array({drop.pickup,int(field?SafeBoxKind::Garrison:drop.kind),drop.seed,drop.difficulty});
    if(field)row.push_back(true);
    supplyData.push_back(std::move(row));
  }
  return {{"schema", schema},
          {"host", host},
          {"epoch", epoch},
          {"sequence", sequence},
          {"phase", phase},
          {"seed", seed},
          {"environment", environmentSeed},
          {"mission_number", missionNumber},
          {"selected_mission", selectedMission},
          {"board_revision", boardRevision},
          {"completed_missions",campaign.completed},{"campaign",savedCampaign},
          {"environment_clock", environmentCycleWire(environmentCycle)},
          {"mission", mission},
          {"salvage", salvage},
          {"clock", clock},
          {"biome", biome},
          {"settings", settings},
          {"players", players},
          {"enemies", std::move(enemyData)},
          {"traps", trapData},{"supplies",supplyData},
          {"world", coop::worldWire(world,baseline)},
          {"events", events}};
}
void Session::readSnapshot(const Json &j,const Json *scenery) {
  if (j.at("schema") != schema)
    throw std::runtime_error("RPG protocol mismatch");
  if (j.at("world").at("projectiles").size() > maxLiveProjectiles ||
      j.at("world").at("effects").size() > maxLiveEffects ||
      j.at("world").at("pickups").size() > 256)
    throw std::runtime_error("RPG snapshot exceeds live simulation limits");
  uint32_t incoming = j.at("environment");
  const auto incomingCount=j.at("world").at("targets").size()+(scenery?scenery->size():0);
  const auto incomingActors=j.at("enemies").size();
  if((scenery&&j.at("world").at("targets").size()!=incomingActors)||incomingActors>incomingCount||incomingCount>maxEncounterTargets)
    throw std::runtime_error("Invalid replicated world size");
  // An encounter inserts actors ahead of the scenery. Only a new map
  // generation (including replaying the same seed) invalidates that scenery.
  const size_t previousActors=enemies.size();
  const bool rebuild=incoming!=environmentSeed||j.at("epoch")!=epoch||
      incomingCount-incomingActors!=world.targets.size()-previousActors;
  host = j.at("host");
  epoch = j.at("epoch");
  sequence = j.at("sequence");
  phase = Phase(j.at("phase").get<int>());
  seed = j.at("seed");
  environmentSeed = incoming;
  missionNumber = j.at("mission_number");
  selectedMission = j.at("selected_mission");
  boardRevision = j.at("board_revision");
  if(!j.at("completed_missions").is_number_unsigned())throw std::runtime_error("Invalid campaign completion state");
  campaign.completed=j.at("completed_missions").get<unsigned>();if(campaign.completed>511)throw std::runtime_error("Invalid campaign completion state");
  auto replicatedCampaign=j.at("campaign").get<CampaignState>();validateCampaign(replicatedCampaign);
  if(replicatedCampaign.completed!=campaign.completed||replicatedCampaign.seed!=seed||replicatedCampaign.revision!=boardRevision)throw std::runtime_error("Inconsistent campaign snapshot");
  campaign=replicatedCampaign;
  if (selectedMission < 0 || selectedMission >= 9)
    throw std::runtime_error("Invalid replicated mission slot");
  environmentCycle = readEnvironmentCycle(j.at("environment_clock"));
  mission = j.at("mission").get<MissionState>();
  if(mission.mapCount<1||mission.mapCount>4||mission.mapIndex<0||mission.mapIndex>=mission.mapCount)
    throw std::runtime_error("Invalid replicated sector index");
  const auto &wireMission=j.at("mission");
  if(!wireMission.contains("depots")&&wireMission.value("salvageDepot",-1)>=0)
    mission.depots.push_back({wireMission.at("salvageDepot").get<int>(),wireMission.value("salvageDepotOpened",false)});
  if(mission.encounterCredits<0||mission.encounterCredits>1000000)throw std::runtime_error("Invalid encounter credits");
  if(!validManualRecipe(mission.recipe))throw std::runtime_error("Invalid replicated mission recipe");
  salvage = j.at("salvage");
  clock = j.at("clock");
  biome = j.at("biome");
  terrain = environmentCatalog()[biome].name;
  difficult = environmentCatalog()[biome].speed < 1;
  settings = j.at("settings").get<GameSettings>();
  validateSettings(settings);
  events = j.at("events");
  members.clear();
  enemies = j.at("enemies").get<std::vector<Enemy>>();
  for(size_t i=0;i<enemies.size();++i){
    enemies[i].ambient=j.at("enemies")[i].value("ambient",false);
    enemies[i].grapplePilot=j.at("enemies")[i].value("grapple_pilot",0u);
  }
  validateEncounterState(*this,incomingCount);
  validateBossState(*this);
  navigation.clear();
  tacticalDecisions.clear();factionContacts.clear();enemyMemory.clear();tacticalClock=0;navigationTurn=0;
  escort=EscortRoute{};
  if (rebuild) {
    loadSectorEnvironment(nullptr);
    auto presentationMission = mission;
    addMissionAssets(world, presentationMission, missionRadius());

  }
  syncManualEnemyModels(world,enemies,rebuild?mission.recipe.enemies.size():previousActors,&mission.recipe);
  for (auto &p : j.at("players")) {
    Member m;
    m.id = p.at("id");
    m.name = p.at("name");
    m.ready = p.at("ready");
    m.inSortie=p.at("in_sortie");
    m.sheet = p.at("sheet").get<Sheet>();
    validateRoster(m.sheet);
    validateWeaponCopies(m.sheet);
    if(p.at("shared_campaign").get<bool>())m.sheet.campaign=campaign;
    if(!p.at("sheet").contains("career"))importLegacyCareer(m.sheet);
    validatePilotCareer(m.sheet.career);
    validateCampaign(m.sheet.campaign);
    validateRefinements(m.sheet);
    if(p.contains("manual")){auto &v=p.at("manual");m.funnels=v.value("funnels",false);m.shieldNodes=v.value("nodes",false);m.veriform=v.value("veriform",false);m.precog=v.value("precog",0.f);m.immobile=v.value("immobile",0.f);m.mobilityDisrupt=v.value("mobility",0.f);m.systemDisrupt=v.value("systems",0.f);m.nodeMobility=v.value("node_mobility",0.f);m.rangeExtension=v.value("range_extension",0.f);m.precogTarget=v.value("precog_target",-1);m.manualClock=v.value("clock",0.f);m.pistonCooldown=v.value("piston",0.f);m.busterReady=v.value("buster",std::array<bool,MountCount>{});m.beltTarget=v.value("belt",std::array<int,MountCount>{-1,-1,-1,-1,-1,-1});}
    coop::pilotRead(p.at("body"), m.body);
    if(p.contains("surface_gravity"))m.body.gravity=readGravity(p.at("surface_gravity"));
    if(p.contains("knockdown")){
      const auto pose=p.at("knockdown").get<std::array<float,2>>();
      m.body.rpgKnockdown={pose[0],pose[1]};
      if(!m.body.rpgKnockdown.active(j.at("world").at("elapsed").get<float>())||
          std::abs(pose[1])!=1.f)throw std::runtime_error("Invalid cockpit knockdown");
    }
    m.body.armRig=world.armRig;
    m.body.cooldown = p.at("cooldowns");
    if(p.contains("weapon_cycles")){
      const auto &cycles=p.at("weapon_cycles");
      const float now=j.at("world").at("elapsed");
      if(!cycles.is_array()||cycles.size()>2)throw std::runtime_error("Invalid weapon cycle list");
      for(const auto &entry:cycles){
        if(!entry.is_array()||entry.size()!=4||!entry[0].is_number_integer()||!entry[1].is_number_integer())
          throw std::runtime_error("Invalid weapon cycle");
        const int hand=entry[0],stage=entry[1];
        const float started=entry[2],duration=entry[3];
        if(hand<0||hand>1||m.weaponCycles[hand].duration!=0||stage<0||stage>WeaponCycle::Release||
           !(started>=0&&started<=now+.001f&&duration>0&&duration<=2.f))
          throw std::runtime_error("Invalid weapon cycle fields");
        m.weaponCycles[hand]={started,duration,WeaponCycle::Stage(stage)};
        if(stage==WeaponCycle::Charge && weaponFamily(m.sheet.equipment[hand])==Gear::RailLance)
          m.lanceReady[hand]=true;
      }
    }
    if(p.contains("fault_notice")){
      const auto &v=p.at("fault_notice");m.lastFault=v.at(0);m.faultNoticeUntil=v.at(1);
      if(m.lastFault<1 || m.lastFault>8)throw std::runtime_error("Invalid hull fault notice");
    }
    m.criticalFaultRolled=p.value("critical_fault",false);
    if(p.contains("hull_emergency")){
      const auto &v=p.at("hull_emergency");int mode=v.at(0);
      if(mode<1||mode>2)throw std::runtime_error("Invalid hull emergency");
      m.emergency=HullEmergency(mode);m.ejectAt=v.at(1);
    }
    if(p.contains("nanite_hold")){
      const auto &v=p.at("nanite_hold");m.repairHold={v.at(0),v.at(1),v.at(2)};
      if(m.repairHold.slot<LeftShoulder||m.repairHold.slot>=MountCount)throw std::runtime_error("Invalid nanite mount");
    }
    m.injuries = p.at("injuries");
    m.missingArms=p.at("missing_arms");
    m.configured=p.at("configured");m.configuration=p.at("configuration");m.campaignAwarded=p.at("campaign_awarded");
    m.debriefAccepted=p.at("debrief");m.heavySupported=p.at("heavy_supported");m.handSupported=p.at("hand_supported");m.handStashed=p.at("hand_stashed");m.recoil=p.at("recoil");m.stashGripHeld.fill(true);
    if(m.injuries>255||m.missingArms>3)throw std::runtime_error("Invalid replicated mecha injuries");
    m.ejected = p.at("ejected");
    m.shield = p.at("shield");
    m.hiddenRemaining = p.at("cloak");
    m.garrison=p.value("garrison",false);
    m.reinforced=p.value("reinforced",false);
    m.berserkRetreat=p.value("berserk_retreat",0.f);
    m.siege = p.at("siege");
    m.barricade = p.at("barricade");
    m.vehicle = p.at("vehicle");
    m.vehicleBlend=p.at("vehicle_blend");m.vehicleSpeed=p.at("vehicle_speed");
    m.drone=p.at("drone_time");m.scanRemaining=p.at("scan_remaining");
    m.scanDuration=p.at("scan_duration");m.scanExposureRemaining=p.value("scan_exposure",m.scanRemaining);m.scanSweep=p.at("scan_sweep");
    m.activeScanners=p.value("active_scanners",0u);
    m.nextSensorPulse=p.value("next_sensor_pulse",0.f);
    if(m.activeScanners>=(1u<<MountCount)||!std::isfinite(m.nextSensorPulse)||
       m.nextSensorPulse<0||m.nextSensorPulse>6.f)throw std::runtime_error("Invalid sensor pulse state");
    m.weatherJam=p.at("weather_jam");
    if(!std::isfinite(m.weatherJam)||m.weatherJam<0||m.weatherJam>6)
      throw std::runtime_error("Invalid weather sensor effect");
    if(p.at("scan_contacts").size()>maxEncounterEnemies)throw std::runtime_error("Radar contact limit exceeded");
    m.scannedContacts=p.at("scan_contacts").get<decltype(m.scannedContacts)>();
    m.scannedActors=p.at("scan_actors").get<decltype(m.scannedActors)>();
    if(m.scannedActors.size()>maxEncounterEnemies)throw std::runtime_error("Radar actor limit exceeded");
    const auto &feedback=p.at("combat_feedback");m.comboCount=feedback.at(0);m.comboRemaining=feedback.at(1);m.riposteRemaining=feedback.at(2);m.criticalRemaining=feedback.at(3);
    m.scannedBearings=p.at("scan_bearings");m.rifleSemi=p.at("rifle_semi");
    m.dronePosition=point(p.at("drone_position"));m.droneYaw=p.at("drone_yaw");m.dronePitch=p.value("drone_pitch",0.f);m.droneControl=p.at("drone_control");
    m.explosive=p.value("loaded_rounds",false)?1.f:0.f;
    m.fieldAmmo=p.value("field_ammo",false);
    m.pendingDepotSalvage=p.value("depot_caches",int(p.value("pending_depot_salvage",false)));
    auto recoverable=[](Gear gear){
      return gear>Gear::Empty&&gear<Gear::Count&&!retiredGear(gear)&&
          (weaponTuning(gear)||gearInfo(gear).module!=Module::Count);
    };
    if(p.contains("pending_recovery")){
      const auto &items=p.at("pending_recovery");
      if(!items.is_array()||items.size()>maxRecoveryItems)throw std::runtime_error("Invalid recovery list");
      m.pendingRecovery=items.get<std::vector<RecoveredItem>>();
      for(const auto &item:m.pendingRecovery)
        if(!recoverable(item.gear)||(item.rarity<Rarity::Common||item.rarity>Rarity::Rare||(!gradedModule(item.gear)&&item.rarity!=Rarity::Common))||
           std::any_of(item.roll.begin(),item.roll.end(),[](int value){return value< -1||value>1;}))
          throw std::runtime_error("Invalid recovery weapon roll");
    }
    if(p.contains("recovered_gear")){
      const auto &items=p.at("recovered_gear");
      if(!items.is_array()||items.size()>maxRecoveryItems)throw std::runtime_error("Invalid recovery list");
      m.recoveredGear=items.get<std::vector<Gear>>();
      for(auto gear:m.recoveredGear)if(!recoverable(gear))throw std::runtime_error("Invalid recovery equipment");
    }
    members.push_back(std::move(m));
  }
  traps.clear();
  for (auto &h : j.at("traps"))
    traps.push_back({point(h.at(0)), h.at(1)});
  supplies.clear();
  for(const auto &row:j.at("supplies")){
    if(supplies.size()>=128)throw std::runtime_error("Supply cap exceeded");
    if(!row.is_array()||(row.size()!=4&&row.size()!=5))throw std::runtime_error("Invalid supply record");
    auto kind=SafeBoxKind(row.at(1).get<int>());
    if(kind!=SafeBoxKind::Dungeon&&kind!=SafeBoxKind::Garrison&&kind!=SafeBoxKind::Depot&&kind!=SafeBoxKind::ModuleCache)
      throw std::runtime_error("Invalid supply kind");
    if(row.size()==5){
      if(kind!=SafeBoxKind::Garrison||!row.at(4).get<bool>())throw std::runtime_error("Invalid field supplies");
      kind=SafeBoxKind::FieldSupplies;
    }
    supplies.push_back({row.at(0),kind,row.at(2),row.at(3)});
  }
  updateEnvironment();
  world.sounds.clear();
  coop::worldRead(j.at("world"),world,scenery);
  for(size_t i=0;i<enemies.size();++i){
    const auto &row=j.at("enemies")[i];
    auto &ai=world.targets[i].ai;
    ai.gravity=row.contains("surface_gravity")?readGravity(row.at("surface_gravity")):SurfaceGravity{};
    ai.rpgGrapplePilot=enemies[i].grapplePilot;
    ai.voidmawPose={};
    if(row.contains("voidmaw_pose")){
      const auto &v=row.at("voidmaw_pose");
      const auto kind=combatSpec(enemies[i]).kind;
      if(!v.is_array()||v.size()!=4||!v[0].is_number_integer()||
         (kind!=EnemyKind::SmallKaiju&&kind!=EnemyKind::LargeKaiju))
        throw std::runtime_error("Invalid replicated Voidmaw pose");
      const int phase=v[0];
      const float started=v[1],duration=v[2],side=v[3];
      if(phase<=int(VoidmawPose::Default)||phase>int(VoidmawPose::SiegeRecover)||
         !std::isfinite(started)||started<0||started>world.elapsed+.001f||
         !(duration>=0&&duration<=8)||(side!=-1.f&&side!=1.f)||
         (phase==int(VoidmawPose::Cling))!=(ai.rpgGrapplePilot!=0))
        throw std::runtime_error("Invalid replicated Voidmaw pose fields");
      ai.voidmawPose={VoidmawPose(phase),started,duration,side};
      if(ai.rpgGrapplePilot){enemies[i].grappleSide=side;enemies[i].grappleAt=started;}
    }
  }
  std::vector<uint32_t> grappled;
  for(size_t i=0;i<enemies.size();++i)if(const auto id=enemies[i].grapplePilot){
    if(enemies[i].hp<=0||!world.targets[i].active||
       std::find(grappled.begin(),grappled.end(),id)!=grappled.end()||
       std::none_of(members.begin(),members.end(),[id](const auto &m){return m.id==id&&!m.ejected&&m.sheet.hp>0;}))
      throw std::runtime_error("Invalid replicated Voidmaw grapple");
    grappled.push_back(id);
  }
  std::array<bool,256> safeBoxIndices{};
  const size_t allowedDepots=mission.recipe.finale?0:mission.recipe.depotCount<0?
      size_t(mission.mapIndex+1==mission.mapCount):size_t(mission.recipe.depotCount);
  if(mission.depots.size()>allowedDepots)throw std::runtime_error("Too many replicated salvage depots");
  int previousDepot=-1;
  for(const auto &depot:mission.depots){
    if(depot.target<=previousDepot||size_t(depot.target)<enemies.size()||size_t(depot.target)>=world.targets.size()||
       world.targets[size_t(depot.target)].generatorAsset!="cover_cargo_armored"||
       (depot.opened&&world.targets[size_t(depot.target)].health>0))
      throw std::runtime_error("Invalid replicated salvage depot");
    previousDepot=depot.target;
  }
  std::array<bool,3> depotBoxes{};
  std::array<int,4> claims{};
  for(const auto &drop:supplies){
    if(drop.pickup>=world.pickups.size()||drop.pickup>=safeBoxIndices.size()||safeBoxIndices[drop.pickup]||
       world.pickups[drop.pickup].kind!=rpgSafeBoxPickup||world.pickups[drop.pickup].collectedBy>15||
       drop.difficulty<0||drop.difficulty>2)throw std::runtime_error("Invalid replicated safe box");
    safeBoxIndices[drop.pickup]=true;
    if(drop.kind==SafeBoxKind::Depot){
      bool matched=false;
      for(size_t i=0;i<mission.depots.size();++i){
        Dice roll{mission.recipe.seed^0x6465706fu^unsigned(i)};
        if(drop.seed!=roll.next()||!mission.depots[i].opened||depotBoxes[i])continue;
        depotBoxes[i]=matched=true;
        for(unsigned slot=0;slot<claims.size();++slot)claims[slot]+=int(bool(world.pickups[drop.pickup].collectedBy&(1u<<slot)));
        break;
      }
      if(!matched)throw std::runtime_error("Invalid replicated depot cache");
    }
  }
  for(size_t i=0;i<mission.depots.size();++i)
    if(mission.depots[i].opened!=depotBoxes[i])throw std::runtime_error("Invalid replicated salvage objective state");
  for(const auto &m:members){
    if(m.body.playerSlot>=4||m.pendingDepotSalvage<0||m.pendingDepotSalvage>3*mission.mapIndex+claims[m.body.playerSlot])
      throw std::runtime_error("Invalid replicated depot claim");
  }
  prefetchNextSector();
}
World Session::view(uint32_t id, const ArmRig &rig,float poseLead) const {
  World result = world;
  result.teammates.clear();
  result.rpgPoseLead=poseLead;
  result.rpgDrones.clear();for(const auto &m:members)if(m.drone>0&&!m.ejected)result.rpgDrones.push_back({m.dronePosition,m.droneYaw,m.id});
  result.mapPilots.clear();
  result.cooperative = false;
  result.rpgLocalPilot=id;
  result.rpgCanopyAttacker=-1;
  result.rpgInsertion=int(openerKind());
  result.rpgInsertionTime=mission.openerTime;
  result.rpgTransitActive=mission.exitOpen||mission.recoveryOpen;
  result.rpgTransitPosition=mission.recoveryOpen?result.missionEntry:Vec3{mission.exit[0],mission.exit[1],mission.exit[2]};
  if(mission.recoveryOpen)
    result.rpgTransitPosition.y=result.surfaceHeight(result.rpgTransitPosition.x,result.rpgTransitPosition.z);
  for(const auto &m:members)if(!m.ejected&&(!mission.recoveryOpen||m.sheet.hp>0)){
    ++result.rpgTransitRequired;
    if(m.sheet.hp>0&&std::hypot(m.body.position.x-result.rpgTransitPosition.x,m.body.position.z-result.rpgTransitPosition.z)<=30)
      ++result.rpgTransitReady;
  }
  for(size_t i=0;i<enemies.size();++i)if(enemies[i].boss>=0)result.targets[i].rpgTint=enemies[i].bossTell>0?Vec3{1,.28f,.12f}:campaignBoss(enemies[i].boss).faction==3?Vec3{.72f,.38f,.62f}:Vec3{1,.7f,.38f};
  for(size_t i=0;i<enemies.size();++i){
    if(const auto *side=factionDetachment(*this,i)){
      const bool hostile=factionAttacksPlayers(*this,i);
      result.targets[i].team=hostile?Team::Attackers:
          side->factionRole==FactionRole::Allied?Team::Defenders:Team::Neutral;
      if(!hostile&&side->factionRole==FactionRole::Neutral)
        result.targets[i].rpgTint={1.f,.87f,.47f};
      if(!hostile&&side->factionRole==FactionRole::Allied&&result.targets[i].active&&
         !result.targets[i].escaped&&enemies[i].hp>0)
        result.rpgRadar.allies.push_back(result.targets[i].origin);
    }
    const auto &spec=combatSpec(enemies[i]);
    result.targets[i].ai.rpgMotor=motorFor(spec,enemies[i].transformed,swimmingAt(spec,world,biome,result.targets[i].origin));
    result.targets[i].rpgEnemyInjuries=enemies[i].injuries;
    result.targets[i].rpgEnemyMissingArms=enemies[i].missingArms;
    result.targets[i].ai.rpgGrapplePilot=enemies[i].grapplePilot;
    if(id&&enemies[i].grapplePilot==id&&enemies[i].hp>0&&result.targets[i].active&&!result.targets[i].escaped)
      result.rpgCanopyAttacker=int(i);
  }
  if(phase==Phase::Combat){
    auto &radar=result.rpgRadar;
    result.rpgSalvageDepots.clear();
    for(const auto &d:mission.depots){
      if(size_t(d.target)>=result.targets.size())continue;
      const auto &depot=result.targets[size_t(d.target)];
      if(depot.active&&depot.health>0)result.rpgSalvageDepots.push_back(depot.center+Vec3{0,depot.half.y+3.f,0});
    }
    for(const auto &fx:result.effects)
      if(fx.model==rpgRadarArrival&&fx.age>=0&&fx.age<enemyEffectLife(fx.model))
        radar.arrivals.push_back({fx.position,fx.age/enemyEffectLife(fx.model)});
    auto objectiveAt=[&](size_t index){
      const auto &target=result.targets[index];
      if(target.active&&!target.escaped&&target.health>0)
        radar.objectives.push_back(target.origin);
    };
    const int objective=mission.recipe.objective;
    if(!result.rpgTransitActive){
      for(int index:mission.assets)
        if(index>=0&&size_t(index)<result.targets.size())objectiveAt(size_t(index));
      if(mission.recipe.boss>=0){
        for(size_t i=0;i<std::min(enemies.size(),mission.recipe.enemies.size());++i)objectiveAt(i);
      }else if(objective==9&&!enemies.empty())objectiveAt(0);
      else if(objective==0||objective==6){
        auto contractAlive=[&](size_t actor){
          return !enemies[actor].ambient&&enemies[actor].hp>0&&
              result.targets[actor].active&&!result.targets[actor].escaped;
        };
        // Published engagement areas remain fixed even when patrols move.
        for(const auto &group:mission.groups){
          if(group.factionRole!=FactionRole::Contract||group.route.empty()||
             std::none_of(group.actors.begin(),group.actors.end(),[&](int actor){
               return actor>=0&&size_t(actor)<enemies.size()&&contractAlive(size_t(actor));
             }))continue;
          const auto &site=group.site>=0&&size_t(group.site)<mission.sites.size()?
              mission.sites[group.site].entrance:group.route.front();
          radar.objectives.push_back({site[0],site[1],site[2]});
        }
        for(size_t actor=0;actor<enemies.size();++actor)
          if(enemies[actor].group<0&&contractAlive(actor))
            radar.objectives.push_back({enemies[actor].anchorX,0,enemies[actor].anchorZ});
      }
      if((objective==4||objective==9)&&mission.recovered)
        radar.objectives.push_back(result.missionEntry);
    }
  }
  result.mission = selectedMission;
  result.objective=mission.exitOpen?"TRANSIT: ALL PILOTS AT BEACON / "+std::to_string(result.rpgTransitReady)+"/"+std::to_string(result.rpgTransitRequired):std::string(missionObjectives()[mission.recipe.objective].name)+" / MAP "+std::to_string(mission.mapIndex+1)+"/"+std::to_string(mission.mapCount);
  if(mission.recipe.objective==2&&!mission.exitOpen)
    result.objective+=" / "+std::to_string(int(mission.progress*100))+"%";
  if(mission.recipe.boss>=0&&!enemies.empty()){
    const auto &boss=enemies[0];
    const int guards=int(std::count_if(enemies.begin()+1,enemies.begin()+std::clamp(mission.recipe.enemies.size(),size_t(1),enemies.size()),[](const auto &e){return e.hp>0;}));
    result.objective=std::string(campaignBoss(mission.recipe.boss).spec.name)+" / HULL "+std::to_string(std::max(0,boss.hp))+" / PHASE "+std::to_string(boss.bossPhase+1)+(boss.bossTell>0?" / CHARGING":"")+" / GUARD "+std::to_string(guards);
  }
  else if(mission.recipe.finale)result.objective="FINAL BATTLE / DEFEAT THE FACTION FIELD ARMY";
  if(mission.recoveryOpen)result.objective="EXTRACT / RECOVER SAFE BOXES";
  for (auto &m : members) {
    Pilot p = m.body;
    p.rpgChassis = int(m.sheet.chassis);
    uint32_t appearance=m.sheet.creationSeed;
    for(unsigned char c:m.name)appearance=(appearance^c)*16777619u;
    p.rpgHullVariant=originalVariant(std::string("chassis_")+chassisId(m.sheet.chassis),appearance).ends_with("_alt");
    p.rpgInjuries=m.injuries|(m.systemDisrupt>0?uint32_t(MechaInjury::Comms):0u);p.rpgMissingArms=m.missingArms;
    p.rpgVehicleBlend=m.vehicleBlend;p.rpgAimMask=0;
    p.rpgBodyRecoil=std::clamp(*std::max_element(m.recoil.begin(),m.recoil.end())/.24f,0.f,1.f);
    for(int slot=0;slot<MountCount;++slot){auto gear=m.sheet.equipment[slot];if(!mountAllowed(gear,slot)||!targetedGear(gear,m.sheet.oneShotModule)||equipmentOwner(m.sheet,slot)!=slot||(consumable(gear)&&m.sheet.inventory[size_t(gear)]<=0))continue;
      if(slot<2&&!m.handStashed[slot]&&p.armAvailable(slot))p.rpgAimMask|=1u<<slot;else if(slot>=2)p.rpgAimMask|=4;}
    if(m.vehicleBlend>.01f||m.vehicle||!p.targetingWorking())p.rpgAimMask=0;
    p.rpgInjuries=m.injuries|(m.systemDisrupt>0?uint32_t(MechaInjury::Comms):0u);p.rpgMissingArms=m.missingArms;
    for(int h=0;h<2;++h)if(!p.armAvailable(h))p.arms[h].tracked=false;
    p.weapons.fill(Weapon::Empty);
    for (int slot=0; slot<MountCount; ++slot) {
      auto gear = m.sheet.equipment[slot];
      bool linked=equipmentOwner(m.sheet,slot)!=slot;
      bool stashed=slot<2&&m.handStashed[slot]&&!linked;
      bool unavailable=(slot<2&&(!p.armAvailable(slot)||stashed))||linked;
      if(slot<2){
        if(melee(gear)){
          const auto *copy=weaponCopy(m.sheet,gear,equippedWeaponCopy(m.sheet,slot));
          const auto i=size_t(WeaponStat::Handling);
          p.rpgWeaponHandling[slot]=copy?copy->roll[i]+copy->tuning[i]:0;
        }
        if(!unavailable && weaponTuning(gear))p.rpgWeaponPhase[slot]=m.weaponCycles[slot].pose(world.elapsed+poseLead);
      }
      if(slot<2)p.rpgStashedModels[slot]=stashed?originalVariant(std::string(gearVisualId(gear)),m.sheet.creationSeed^uint32_t(m.sheet.qualities[slot])^uint32_t(gear)):"";
      p.rpgModelNames[slot] = gear == Gear::Empty||unavailable ? "" :
                              originalVariant(std::string(gearVisualId(gear)),m.sheet.creationSeed^uint32_t(m.sheet.qualities[slot])^uint32_t(gear));
      if (!unavailable&&ranged(gear)) p.weapons[slot] = weaponTuning(gear)->visual;
      else if (!unavailable&&melee(gear)) p.weapons[slot] = Weapon::Cutter;
      else p.weapons[slot]=Weapon::Empty;
    }
    p.health = 100.f * m.sheet.hp / std::max(1, m.sheet.maxHp);
    if (m.id == id) {
      result.rpgDroneView=m.droneControl&&m.drone>0&&!m.ejected&&m.sheet.hp>0;result.rpgDroneEye=m.dronePosition;result.rpgDroneYaw=m.droneYaw;result.rpgDronePitch=m.dronePitch;result.rpgDroneRemaining=m.drone;
      if(result.rpgDroneView)result.mapPilots.push_back(p);
      result.rpgInsertionGround=world.surfaceHeight(p.position.x,p.position.z);
      static_cast<Pilot &>(result) = p;
      if (!p.lifeSpent) {
        if (phase == Phase::Complete) result.missionStatus = MissionStatus::Complete;
        else if (phase == Phase::Defeat) result.missionStatus = MissionStatus::Failed;
      }
      result.armRig = rig;
      for (int slot = 0; slot < MountCount; ++slot) {
        auto g = m.sheet.equipment[slot];
        result.rpgReadouts[slot] =
            (slot<2&&!p.armAvailable(slot))?"LOST":(slot<2&&m.handStashed[equipmentOwner(m.sheet,slot)])?"STOW":equipmentOwner(m.sheet,slot)!=slot?"LINK":(slot<2&&pairedWeapon(m.sheet.chassis,g)&&!m.handSupported[slot])?"BRACE":g == Gear::Empty ? "--"
            : scannerGear(g)?((m.activeScanners&(1u<<slot))?"ON":"OFF")
            : consumable(g)  ? std::to_string(m.sheet.inventory[size_t(g)])
            : m.body.cooldown[slot] > .05f
                ? std::to_string(int(std::ceil(m.body.cooldown[slot])))
                : slot<2&&selectableFireMode(g)?(m.rifleSemi?"SEMI":weaponTuning(g)->pattern==AttackPattern::Burst?"BURST":"AUTO"):"RDY";
      }
      std::copy_n(p.weapons.begin(),2,result.rpgAudioWeapons.begin());
      result.weapons.fill(Weapon::Empty);
      for (int hand = 0; hand < 2; ++hand)
        result.weapons[hand] = (equipmentOwner(m.sheet,hand)!=hand||m.handStashed[hand])?Weapon::Empty:ranged(m.sheet.equipment[hand])  ? Weapon::Rifle
                               : melee(m.sheet.equipment[hand]) ? Weapon::Cutter
                                                                : Weapon::Empty;
      result.rpgPilotStatus=m.explosive>0?"HIGH-PRESSURE AMMO ACTIVE":m.fieldAmmo?"OVERPRESSURE AMMO ACTIVE":"";
      for(const auto &site:mission.sites)if(p.position.x>site.bounds[0]&&p.position.x<site.bounds[2]&&p.position.z>site.bounds[1]&&p.position.z<site.bounds[3]&&world.indoors(p.position+Vec3{0,12,0}))
        result.rpgPilotStatus=site.cleared?"SITE SECURED / SAFE BOX":"SITE / CLEAR GUARDS AND CORE";
      if(m.riposteRemaining>0)result.rpgPilotStatus="PARRY / RIPOSTE";
      else if(m.comboRemaining>0&&m.comboCount>=2)result.rpgPilotStatus="COMBO x"+std::to_string(m.comboCount);
      else if(m.criticalRemaining>0)result.rpgPilotStatus="CRITICAL HIT";
      if(phase==Phase::Combat&&result.rpgPilotStatus.empty()){
        if(const auto *hazard=weatherHazard(result.environment.weather)){
          const bool sheltered=result.indoors(p.position)||result.indoors(p.bodyPoint(10))||
              (result.environment.weather==Weather::Snowstorm&&!result.exposedToPrecipitation(p.bodyPoint(10)));
          const bool nearWarning=!sheltered&&std::any_of(result.effects.begin(),result.effects.end(),[&](const auto &fx){
            return (fx.model==rpgWeatherWarning||fx.model==rpgWeatherMeteor)&&
                length(fx.position-p.position)<float(hazard->radius+20);
          });
          result.rpgPilotStatus=std::string(hazard->label)+(sheltered?" / SHELTERED":
              nearWarning?(hazard->radius>0?" / CLEAR MARKED ZONE":" / SEEK COVER"):
              m.weatherJam>0?" / SENSOR JAM":"");
        }
      }
      if(std::any_of(enemies.begin(),enemies.end(),[&](const auto &e){return e.grapplePilot==m.id;}))
        result.rpgPilotStatus="VOIDMAW ON CANOPY / FIRE TO DISLODGE";
      for(size_t i=0;i<enemies.size();++i){
        const auto &enemy=enemies[i];
        if(!pilotedMecha(combatSpec(enemy))||enemy.hp<=0)continue;
        if(enemy.hp*2<=enemy.maxHp)result.targets[i].rpgWeakReveal=.68f;
        if(m.precog>0&&int(i)==m.precogTarget)result.targets[i].rpgWeakReveal=1.f;
      }
      if(m.emergency!=HullEmergency::None){
        const auto remaining=std::to_string(int(std::ceil(std::max(0.,m.ejectAt-clock))));
        result.rpgPilotStatus="EJECTION IN "+remaining+" S / "+
            (m.emergency==HullEmergency::LastWind?"DESTROY A HOSTILE":"AWAITING NANITES");
      }else if(m.repairHold.target){
        result.rpgPilotStatus="HOLD NANITES / "+std::to_string(int(std::clamp(clock-m.repairHold.started,0.,10.)))+" / 10 S";
      }else if(m.lastFault && clock<m.faultNoticeUntil){
        result.rpgPilotStatus=std::string("HULL FAULT / ")+hullFaultNames[m.lastFault-1];
      }else if(phase==Phase::Combat){
        for(const auto &ally:members)if(ally.emergency==HullEmergency::AwaitRepair){
          result.rpgPilotStatus=ally.name+" DISABLED / AIM AND HOLD NANITES";break;
        }
      }
      result.rpgAp=m.sheet.ap;
      result.rpgMaxAp=m.sheet.maxAp;
      result.rpgArmor = 100.f*std::clamp(float(m.sheet.hp+m.sheet.ap)/
                                        std::max(1,m.sheet.maxHp+m.sheet.maxAp),0.f,1.f);
      result.rpgHeat = m.sheet.reactorStress;
      result.rpgOverheated = m.sheet.overheated;
      result.rpgPhase = int(phase);
      if(!result.targetingWorking()){result.homingTarget=-1;result.pingTarget=-1;}
      if(m.weatherJam>0)result.rpgInjuries|=uint32_t(MechaInjury::Sensors);
      auto &radar=result.rpgRadar;
      radar.enemyPrefix=enemies.size();
      const bool sensorsOnline=result.sensorsWorking()&&!p.lifeSpent&&m.sheet.hp>0&&phase==Phase::Combat;
      radar.exposesPilot=sensorsOnline&&m.scanExposureRemaining>0;
      if(sensorsOnline&&m.scanRemaining>0&&m.scanDuration>0){
        radar.strength=std::clamp(m.scanRemaining/m.scanDuration,0.f,1.f);
        radar.sweep=m.scanSweep;
        radar.distantBearings=m.scannedBearings;
        for(size_t n=0;n<m.scannedContacts.size();++n){
          const auto &contact=m.scannedContacts[n];
          Vec3 color{1.f,.23f,.12f};
          if(n<m.scannedActors.size()){
            const auto actor=m.scannedActors[n];
            if(actor<enemies.size()){
              if(const auto *side=factionDetachment(*this,actor);side&&!factionAttacksPlayers(*this,actor)){
                if(side->factionRole==FactionRole::Allied)continue;
                color={1.f,.77f,.26f};
              }
            }
          }
          radar.contacts.push_back({contact[0],contact[1],contact[2]});
          radar.contactColors.push_back(color);
        }
        for(size_t index:m.scannedActors)
          if(index<enemies.size()&&pilotedMecha(combatSpec(enemies[index])))
            result.targets[index].rpgWeakReveal=std::max(result.targets[index].rpgWeakReveal,radar.strength);
      }
      for (size_t i = 0; i < enemies.size(); ++i)
        if ((combatSpec(enemies[i]).traits & Camo) &&
            !result.targets[i].protectedTarget && m.drone == 0 &&
            length(world.targets[i].origin - m.body.position) >
                (30 + 3 * m.sheet.attributes[2]) * (injuryDisadvantage(m.injuries,2)?.5f:1.f) /
                    environmentCatalog()[biome].sensors)
          result.targets[i].active = false;
    } else
      result.mapPilots.push_back(p);
  }
  return result;
}
std::vector<std::string> Session::hud(uint32_t) const { return {}; }
} // namespace iron::rpg
