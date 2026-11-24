#include "rpg/menu.hpp"
#include "rpg/mission_board.hpp"
#include "rpg/manual_missions.hpp"
#include "rpg/preparation.hpp"
#include "rpg/profiles.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
using namespace iron;using namespace iron::rpg;
template<class F>bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
static Module nextModule(const Sheet &s){for(int i=0;i<int(Module::Count);++i)if(!s.has(Module(i)))return Module(i);throw std::runtime_error("No available module");}
static void accept(Session &s,uint32_t id){s.playerCommand(id,{{"op","debrief"},{"module",moduleId(nextModule(s.member(id).sheet))}});}
static Session fixture(uint32_t seed=713,int party=2,int maps=1){
  Session s(seed);for(int n=1;n<=party;++n){s.add(unsigned(n),"Pilot "+std::to_string(n));finishCreation(s.member(unsigned(n)).sheet);}
  GameSettings settings;settings.extent=500;settings.maps=maps;s.command(1,{{"op","settings"},{"settings",settings}});return s;
}
static void deploy(Session &s,int slot){
  s.playerCommand(s.host,missionSelectionCommand(slot));
  s.playerCommand(s.host,{{"op","sortie"},{"value",true}});
  for(const auto &m:s.members)if(m.id!=s.host)s.playerCommand(m.id,{{"op","sortie"},{"value",true}});
  for(const auto &m:s.members)s.playerCommand(m.id,{{"op","ready"},{"value",true}});
  assert(s.phase==Phase::Briefing);s.playerCommand(s.host,{{"op","deploy"}});
}
// Drive the real objective state machine, without an AI pilot or renderer.
static void clearSector(Session &s){
  s.mission.openerTime=100;for(size_t i=0;i<s.enemies.size();++i){s.enemies[i].hp=0;s.world.targets[i].health=0;s.world.targets[i].active=false;}
  auto put=[&](Vec3 p){for(auto &m:s.members){m.body.position=p;m.sheet.hp=m.sheet.maxHp;m.ejected=false;}};
  auto tick=[&](){s.step(.05f);};int objective=s.mission.recipe.objective;
  if(objective==1||objective==7||objective==8)s.mission.progress=1000;
  if(objective==2){
    for(int i=0;i<3600&&s.phase==Phase::Combat&&!s.mission.exitOpen&&!s.mission.recoveryOpen;++i){
      put(s.world.targets[s.mission.assets.front()].origin);
      s.step(.1f);
    }
  }
  if(objective==3||objective==4){for(int index:s.mission.assets){put(s.world.targets[index].origin);tick();}put(s.world.missionEntry);}
  if(objective==5)for(int index:s.mission.assets)s.world.targets[index].health=0;
  if(objective==9){s.enemies[0].hp=1;s.enemies[0].disabled=10000;put(s.world.targets[0].origin);tick();put(s.world.missionEntry);}
  tick();
  if(s.mission.recoveryOpen){
    put(s.world.missionEntry);
    for(int frame=0;frame<52&&s.phase==Phase::Combat;++frame)s.step(.1f);
  }
  assert(s.phase==Phase::Complete||s.mission.exitOpen);
}
static void clearMission(Session &s){
  for(int sector=0;sector<4;++sector){clearSector(s);if(s.phase==Phase::Complete)return;
    assert(s.mission.exitOpen);Vec3 exit{s.mission.exit[0],s.mission.exit[1],s.mission.exit[2]};for(auto &m:s.members)m.body.position=exit;
    for(int n=0;n<24&&s.phase==Phase::Combat;++n)s.step(.1f);
    assert(s.phase==Phase::Briefing);s.playerCommand(s.host,{{"op","deploy"}});
  }assert(false);
}
static void checkCareer(){
  auto game=fixture(9107);
  auto host=game.member(1).sheet,guest=game.member(2).sheet;
  const auto employer=campaignMission(game,0).employer;
  assert(employer>=0&&employer<3);
  host.career.standings[employer]=11;host.missionsCompleted=4;
  guest.career.standings[employer]=-10;
  game.command(1,{{"op","configure"},{"profile",host}});
  game.command(2,{{"op","configure"},{"profile",guest}});
  assert(game.campaign.standings==host.career.standings);
  assert(uniformRank(host)==0&&serviceTitle(host)=="ROOKIE");
  deploy(game,0);clearMission(game);
  const auto &winner=game.member(1).sheet;
  const auto visitor=game.member(2).sheet;
  assert(winner.career.standings[employer]==13&&visitor.career.standings[employer]==-8);
  assert(winner.career.heroFactions==(1u<<employer)&&serviceTitle(winner)==factionHonour(employer));
  auto copy=Json(winner).get<Sheet>();
  copy.career.standings[employer]=-20;
  assert(serviceTitle(copy)==factionHonour(employer));
  copy.career.heroFactions=0;
  assert(serviceTitle(copy)=="VETERAN");copy.campaignAwards=1;assert(serviceTitle(copy)=="CAMPAIGN ACE");
  auto lost=game.member(2).sheet.career.standings;
  game.remove(1);assert(game.host==2&&game.campaign.standings==lost);
  // A guest's saved campaign snapshot might be from another host. Their own
  // faction ledger becomes authoritative when they later host that campaign.
  Session solo;solo.add(7,"Returning pilot");
  solo.command(7,{{"op","configure"},{"profile",visitor}});
  assert(solo.campaign.standings==lost);
  auto legacy=Json(host);legacy.erase("career");
  auto old=host.campaign;old.standings[0]=15;legacy["campaign"]=old;legacy["missionsCompleted"]=6;
  Session restored;restored.add(1,"Migrated pilot");
  restored.command(1,{{"op","configure"},{"profile",legacy}});
  assert(restored.member(1).sheet.career.standings==old.standings&&restored.member(1).sheet.career.heroFactions==1);
  auto fresh=fixture(996,1).member(1).sheet;
  restored.command(1,{{"op","configure"},{"profile",fresh},{"resume_campaign",true}});
  assert(restored.campaign.standings==fresh.career.standings&&restored.member(1).sheet.career.heroFactions==0);
}
int main(){
  checkCareer();
  auto s=fixture();auto original=generateMissionBoard(s);std::vector<MissionRecipe> cleared;
  assert(canRegenerateCampaign(s));
  for(int mission=0;mission<9;++mission){
    const int slot=mission;auto before=generateMissionBoard(s);auto revision=s.boardRevision;
    deploy(s,slot);assert(Json(s.mission.recipe)==Json(before[slot]));clearMission(s);
    assert(campaignCompleted(s.campaign)==mission+1&&s.campaign.awaitingAdvance&&!s.campaign.victorious);
    for(auto &m:s.members)assert(m.sheet.level==1&&m.sheet.pendingLevels==0);
    assert(rejects([&]{s.playerCommand(1,{{"op","hangar"}});}));
    accept(s,1);assert(s.boardRevision==revision&&s.member(1).sheet.level==1);
    auto unchanged=Json(s.member(1).sheet);accept(s,1);assert(Json(s.member(1).sheet)==unchanged);
    assert(rejects([&]{s.playerCommand(1,{{"op","hangar"}});}));
    accept(s,2);assert(s.boardRevision==revision+1&&!s.campaign.awaitingAdvance);
    cleared.push_back(before[slot]);auto after=generateMissionBoard(s);
    if(mission<8){
      for(int i=0;i<=mission;++i)assert(Json(after[i])==Json(cleared[i]));
      for(int i=mission+1;i<9;++i){assert(after[i].seed!=before[i].seed);assert(after[i].level==1&&after[i].campaignStage==mission+2);assert(after[i].threatBudget>original[i].threatBudget);}
    }
    Session client;client.readSnapshot(s.snapshot());assert(client.snapshot()==s.snapshot());assert(Json(generateMissionBoard(client))==Json(after));
    auto packet=s.snapshot();for(const auto &pilot:packet.at("players")){assert(pilot.at("shared_campaign")==true&&!pilot.at("sheet").contains("campaign"));}
    s.playerCommand(1,{{"op","hangar"}});assert(!canRegenerateCampaign(s));
    assert(rejects([&]{s.playerCommand(1,{{"op","reroll_missions"}});}));
  }
  assert(campaignFinale(s.campaign));for(auto &m:s.members)assert(m.sheet.level==1&&m.sheet.missionsCompleted==9);
  assert(campaignCards(s).size()==1&&campaignCards(s)[0].title.starts_with("FINALE:"));
  assert(rejects([&]{s.playerCommand(1,missionSelectionCommand(1));}));
  auto finale=campaignMission(s,0);assert(finale.level==1&&finale.finale&&finale.maps==1);
  int hostile=int(std::min_element(s.campaign.standings.begin(),s.campaign.standings.end())-s.campaign.standings.begin());
  assert(s.campaign.standings[hostile]<0
         ?finale.adversary!=voidmawFaction&&s.campaign.standings.at(finale.adversary)==s.campaign.standings[hostile]
         :finale.adversary==voidmawFaction);
  deploy(s,0);clearMission(s);assert(s.campaign.victorious);
  for(auto &m:s.members)assert(m.sheet.level==1&&m.sheet.campaignAwards==0&&m.sheet.pendingLevels==0);
  // Finale clears acquired gear and its tuning, keeping the same pilot and savings.
  std::array<Sheet,2> beforeAward{s.member(1).sheet,s.member(2).sheet};
  for(size_t i=0;i<s.members.size();++i){
    auto &sheet=s.members[i].sheet;
    sheet.credits=800+int(i)*200;
    sheet.inventory[size_t(Gear::Scrap)]=30+int(i);
    sheet.inventory[size_t(Gear::Repair)]=4;
    sheet.oneShotStock[size_t(Module::ChestBeam)]=2;
    sheet.ownedArmor[int(Armor::Heavy)]=true;
    sheet.armor=Armor::Heavy;
    sheet.record="Nine contracts survived";
    sheet.starterModule=Module::NanoShroud;
    sheet.modules={Module::ChestBeam}; // The original starter was lost in combat.
    if(i==1)sheet.campaignAwards=2;
    refinePilot(sheet,PilotRefinement::Hull,i==1);
    refinePilot(sheet,PilotRefinement::Presence);
    recoverWeapon(sheet,Gear::RifleRare,{1,0,-1});
    tuneWeapon(sheet,Gear::RifleRare,0,WeaponStat::Cycle);
    equipGear(sheet,RightHand,Gear::RifleRare,0);
    beforeAward[i]=sheet;
  }
  accept(s,1);
  auto award=Json(s.member(1).sheet);accept(s,1);assert(Json(s.member(1).sheet)==award);
  assert(rejects([&]{s.playerCommand(1,{{"op","hangar"}});}));
  accept(s,2);s.playerCommand(1,{{"op","hangar"}});
  assert(!s.campaign.completed&&!s.campaign.victorious&&canRegenerateCampaign(s));
  for(size_t i=0;i<s.members.size();++i){
    const auto &sheet=s.members[i].sheet,&before=beforeAward[i];
    assert(sheet.created&&sheet.level==1&&sheet.campaignAwards==before.campaignAwards+1&&sheet.missionsCompleted==before.missionsCompleted);
    assert(pilotRefinementCount(sheet)==0&&sheet.maxHp==before.maxHp-1&&sheet.attributes[3]==before.attributes[3]-1);
    assert(sheet.campaignAwardsSpent==before.campaignAwardsSpent&&availableCampaignAwards(sheet)==availableCampaignAwards(before)+1);
    assert(sheet.creationSeed==before.creationSeed&&sheet.chassis==before.chassis&&sheet.pilot==before.pilot&&sheet.record==before.record);
    assert(sheet.credits==before.credits&&sheet.inventory[size_t(Gear::Scrap)]==before.inventory[size_t(Gear::Scrap)]);
    assert(sheet.weaponCopies.empty()&&sheet.owned[size_t(Gear::RifleRare)]==0&&!sheet.ownedArmor[int(Armor::Heavy)]);
    assert((sheet.fittedCopies==std::array<int,2>{-1,-1}));
    assert(sheet.inventory[size_t(Gear::Repair)]==0&&sheet.oneShotStock[size_t(Module::ChestBeam)]==0);
    assert(sheet.modules==std::vector{Module::NanoShroud}&&sheet.equipment[RightHip]==Gear::NanoShroud);
  }
  auto directory=std::filesystem::temp_directory_path()/("mecha-campaign-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  struct Cleanup{std::filesystem::path p;~Cleanup(){std::filesystem::remove_all(p);}}cleanup{directory};ProfileStore store(directory);
  store.save(0,s.member(1).sheet,"Veteran");
  auto loaded=store.load(0);assert(loaded->callsign=="Veteran"&&loaded->sheet.campaignAwards==1&&loaded->sheet.created&&!store.load(1));
  assert(loaded->sheet.credits==beforeAward[0].credits&&loaded->sheet.lastCampaignAward==s.member(1).sheet.lastCampaignAward);
  {
    // Schema 8 stored a fourth reputation in both the shared campaign and local career.
    auto document=Json{{"schema",8},{"callsign","Veteran"},{"sheet",loaded->sheet}};
    auto &sheet=document["sheet"];
    sheet["career"]["standings"]={2,12,-7,20};
    sheet["campaign"]["standings"]={-9,1,4,-20};
    auto path=store.path(2);
    {std::ofstream out(path);out<<document;}
    const auto migrant=store.load(2).value();
    assert((migrant.sheet.career.standings==HumanStandings{2,12,-7}));
    assert((migrant.sheet.campaign.standings==HumanStandings{-9,1,4}));
    store.save(2,migrant.sheet,migrant.callsign);
    std::ifstream in(path);Json saved;in>>saved;
    assert(saved["schema"]==9&&saved["sheet"]["career"]["standings"].size()==3&&
           saved["sheet"]["campaign"]["standings"].size()==3);
    const auto roster=store.lines(2);
    assert(std::none_of(roster.begin(),roster.end(),[](const auto &line){return line.find("VOIDMAW")!=std::string::npos;}));
    Session encounter;encounter.add(1,"Veteran");encounter.member(1).sheet=migrant.sheet;
    encounter.campaign.standings=migrant.sheet.career.standings;
    encounter.mission.recipe.employer=0;encounter.mission.recipe.adversary=voidmawFaction;
    auto lines=campaignDebriefLines(encounter,1);
    assert(std::find(lines.begin(),lines.end(),"Aeonic Primacy 2 / Voidmaw")!=lines.end());
  }
  store.save(1,beforeAward[1],"Refiner");
  const auto restored=store.load(1)->sheet;
  assert(restored.refinements==beforeAward[1].refinements&&restored.maxHp==beforeAward[1].maxHp&&
         restored.attributes==beforeAward[1].attributes&&restored.campaignAwardsSpent==1&&
         availableCampaignAwards(restored)==1);
  // A slot reset must survive the legacy default-profile importer and leave the other pilot intact.
  saveProfile(directory/"default.json",loaded->sheet,"Legacy");
  Dice resetSeed{13};
  store.reset(0,resetSeed);
  ProfileStore reopened(directory);
  auto rookie=reopened.load(0);
  assert(rookie&&rookie->callsign=="Pilot 1"&&!rookie->sheet.created&&!rookie->sheet.campaign.active);
  assert(rookie->sheet.campaignAwards==0&&rookie->sheet.missionsCompleted==0&&
         rookie->sheet.inventory[size_t(Gear::Scrap)]==0&&rookie->sheet.creationSeed!=loaded->sheet.creationSeed);
  assert(Json(reopened.load(1)->sheet)==Json(restored));
  // Saving during party debrief must neither lose the board nor double-level.
  auto pending=fixture(714);deploy(pending,0);clearMission(pending);auto waiting=pending.member(1).sheet;
  saveProfile(directory/"pending.json",waiting,"Returning");auto resumedSheet=loadProfile(directory/"pending.json");
  Session resumed(999);resumed.add(1,"Returning");resumed.playerCommand(1,{{"op","configure"},{"profile",resumedSheet}});
  assert(resumed.seed==714&&resumed.member(1).sheet.level==1&&resumed.campaign.awaitingAdvance);
  accept(resumed,1);assert(resumed.member(1).sheet.level==1&&campaignCompleted(resumed.campaign)==1&&!resumed.campaign.awaitingAdvance);
  accept(pending,1);assert(resumed.member(1).sheet.attributes==pending.member(1).sheet.attributes&&resumed.member(1).sheet.maxHp==pending.member(1).sheet.maxHp);
  // Finished but unclaimed finales survive restarting without duplicating awards.
  auto victory=fixture(715,1);victory.campaign.completed=511;victory.campaign.victorious=true;victory.phase=Phase::Complete;
  auto victor=victory.member(1).sheet;victor.campaign=victory.campaign;victor.campaign.seed=715;
  Session claim(99);claim.add(1,"Champion");claim.playerCommand(1,{{"op","configure"},{"profile",victor}});assert(claim.phase==Phase::Complete);
  Session claimReplica;claimReplica.readSnapshot(claim.snapshot());assert(claimReplica.snapshot()==claim.snapshot());
  accept(claim,1);assert(claim.member(1).sheet.campaignAwards==1);auto once=Json(claim.member(1).sheet);accept(claim,1);assert(Json(claim.member(1).sheet)==once);
  claimReplica.readSnapshot(claim.snapshot());assert(claimReplica.member(1).sheet.campaignAwards==1&&!claimReplica.member(1).sheet.campaign.active);
  // A returning guest who saved their claim cannot be charged an equipment
  // reset or handed an extra award when the host resumes the same debrief.
  Session resumedParty(100);resumedParty.add(1,"Host");resumedParty.add(2,"Guest");
  resumedParty.playerCommand(1,{{"op","configure"},{"profile",victor}});
  const auto guest=claim.member(1).sheet;
  resumedParty.playerCommand(2,{{"op","configure"},{"profile",guest}});
  assert(resumedParty.member(2).campaignAwarded&&resumedParty.member(2).debriefAccepted);
  accept(resumedParty,2);
  assert(Json(resumedParty.member(2).sheet)==Json(guest));
  auto &returning=resumedParty.member(1);
  auto veteran=returning.sheet;
  assert(rejects([&]{resumedParty.playerCommand(1,{{"op","debrief"},{"chassis",999}});}));
  assert(!returning.campaignAwarded&&Json(returning.sheet)==Json(veteran));
  resumedParty.playerCommand(1,{{"op","debrief"},{"chassis",int(Chassis::Colossus)}});
  assert(returning.sheet.chassis==Chassis::Colossus&&returning.sheet.starterModule==chassisStarterModule(Chassis::Colossus));
  assert(returning.sheet.pilot==veteran.pilot&&returning.sheet.creationSeed==veteran.creationSeed&&
         returning.sheet.record==veteran.record&&returning.sheet.credits==veteran.credits&&returning.sheet.created==veteran.created);
  assert(returning.sheet.campaignAwards==veteran.campaignAwards+1&&returning.sheet.maxReactor==chassisInfo(Chassis::Colossus).reactor);
  auto claimed=Json(returning.sheet);
  resumedParty.playerCommand(1,{{"op","debrief"},{"chassis",int(Chassis::Scout)}});
  assert(Json(returning.sheet)==claimed&&Json(resumedParty.member(2).sheet)==Json(guest));
  // Contract choice and faction standing change generated opponents and finale.
  auto a=fixture(713,1),b=a;deploy(a,0);clearMission(a);accept(a,1);a.playerCommand(1,{{"op","hangar"}});
  deploy(b,1);clearMission(b);accept(b,1);b.playerCommand(1,{{"op","hangar"}});
  assert(a.campaign.consequences!=b.campaign.consequences&&Json(generateMissionBoard(a))!=Json(generateMissionBoard(b)));
  b=a;b.campaign.standings={20,-20,0};auto war=generateMissionBoard(b);assert(Json(war)!=Json(generateMissionBoard(a)));
  auto beforeRetry=Json(b.campaign);auto offersBeforeRetry=Json(generateMissionBoard(b));deploy(b,2);b.phase=Phase::Defeat;b.playerCommand(1,{{"op","hangar"}});auto afterRetry=Json(b.campaign);beforeRetry.erase("randomState");afterRetry.erase("randomState");assert(afterRetry==beforeRetry&&Json(generateMissionBoard(b))==offersBeforeRetry);
  // Connected sectors grant a single campaign advance and retain war damage.
  auto stages=fixture(715,1,2);deploy(stages,0);stages.mission.structuresLost=12;clearSector(stages);
  assert(stages.phase==Phase::Combat&&stages.campaign.completed==0&&stages.member(1).sheet.pendingLevels==0);
  Vec3 exit{stages.mission.exit[0],stages.mission.exit[1],stages.mission.exit[2]};stages.member(1).body.position=exit;
  for(int n=0;n<24&&stages.phase==Phase::Combat;++n)stages.step(.1f);assert(stages.phase==Phase::Briefing&&stages.mission.structuresLost==12);
  stages.playerCommand(1,{{"op","deploy"}});clearSector(stages);assert(campaignCompleted(stages.campaign)==1&&stages.campaign.devastation>=12);
  // Every boss and the decisive battle occur; phases and waves remain bounded.
  std::set<int> designs;bool battle=false;
  for(uint32_t seed=1;seed<100&&designs.size()<campaignBosses().size();++seed)for(int faction=0;faction<4;++faction){
    auto final=fixture(seed,1);final.campaign.completed=511;if(faction!=voidmawFaction)final.campaign.standings[faction]=-20;auto r=campaignMission(final,0);
    assert(r.adversary==faction&&r.enemies.size()<=maxInitialEnemies&&r.siteCount==0);
    if(r.finaleBattle){battle=true;continue;}if(!designs.insert(r.boss).second)continue;
    deploy(final,0);final.mission.openerTime=100;auto maximum=final.enemies[0].maxHp;
    final.enemies[0].hp=maximum*2/3;final.step(.05f);assert(final.enemies[0].bossPhase==1);
    auto phaseCount=final.enemies.size();for(auto &e:final.enemies)e.disabled=1000;final.step(.05f);assert(final.enemies.size()==phaseCount);
    final.enemies[0].hp=maximum/3;final.step(.05f);assert(final.enemies[0].bossPhase==2&&final.enemies.size()<=maxEncounterEnemies);
    Session remote;remote.readSnapshot(final.snapshot());assert(remote.enemies[0].boss==r.boss&&remote.enemies[0].bossPhase==2&&remote.enemies[0].maxHp==maximum);
    final.enemies[0].hp=0;final.step(.05f);
    assert(final.phase==Phase::Combat&&!final.mission.recoveryOpen);
    for(size_t i=1;i<final.mission.recipe.enemies.size();++i)final.enemies[i].hp=0;
    final.step(.05f);
    if(final.mission.recoveryOpen){
      final.member(1).body.position=final.world.missionEntry;
      for(int frame=0;frame<52&&final.phase==Phase::Combat;++frame)final.step(.1f);
    }
    assert(final.phase==Phase::Complete&&final.campaign.victorious);
  }
  assert(designs.size()==6&&battle);
  auto support=fixture(716,1);support.campaign.intel=2;support.campaign.supply=2;
  auto supportSettings=support.settings;supportSettings.opener=0;supportSettings.location=2;supportSettings.environment=3;supportSettings.objective=0;
  support.command(1,{{"op","settings"},{"settings",supportSettings}});deploy(support,0);
  for(auto &enemy:support.enemies)enemy.disabled=1000;
  for(int n=0;n<110;++n)support.step(.05f);
  assert(support.phase==Phase::Combat&&support.member(1).sheet.hidden&&support.member(1).hiddenRemaining>1&&support.member(1).shield>0&&support.member(1).shieldRemaining>19);
  auto decisive=fixture(1,4);decisive.campaign.completed=511;decisive.campaign.standings[1]=-20;
  for(unsigned seed=1;!campaignMission(decisive,0).finaleBattle;++seed){decisive.seed=seed;}
  deploy(decisive,0);assert(decisive.mission.recipe.finaleBattle&&decisive.enemies.size()<=maxInitialEnemies);
  for(size_t i=0;i<decisive.enemies.size();++i){const auto &spec=enemySpec(decisive.enemies[i].spec);assert(!(spec.traits&Carrier));if(i<6)assert(spec.hd>=7);}
  clearMission(decisive);assert(decisive.campaign.victorious);for(auto &m:decisive.members){accept(decisive,m.id);assert(m.sheet.campaignAwards==1&&m.sheet.level==1);}
  decisive.command(1,{{"op","next"}});
  assert(decisive.phase==Phase::Hangar&&!decisive.campaign.victorious&&!decisive.campaign.completed);
  auto invalid=s.snapshot();invalid["campaign"]["standings"][0]=-21;assert(rejects([&]{Session c;c.readSnapshot(invalid);}));
  std::cout<<"Nine adaptive victories, level-10 finale, six phased bosses, bounded decisive battle, faction consequences, campaign saves and exactly-once pilot awards passed\n";
}
