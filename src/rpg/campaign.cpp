#include "rpg/mission_board.hpp"
#include "rpg/manual_missions.hpp"
#include "rpg/campaign_story.hpp"
#include <numeric>
#include <bit>
#include <stdexcept>
namespace iron::rpg {
int hostilityPressure(const HumanStandings &standings,int opponent){
  return opponent==voidmawFaction?0:std::max(0,-standings.at(size_t(opponent)));
}
int campaignCompleted(const CampaignState &c){return std::popcount(c.completed);}
int campaignLevel(const CampaignState &c){return std::min(10,1+campaignCompleted(c));}
bool campaignFinale(const CampaignState &c){return c.completed==campaignMissionMask;}
void validateCampaign(const CampaignState &c){
  if(c.completed>campaignMissionMask||c.intel<0||c.intel>18||c.supply<0||c.supply>18||c.devastation<0||c.devastation>9000||
     (c.victorious&&!campaignFinale(c))||(c.awaitingAdvance&&!c.completed))throw std::runtime_error("Invalid campaign progression");
  auto storyBounds=[](int arc,int choice,int collateral,const std::array<int,3> &tracks,int previous,int ending,int scars){
    if(arc< -1||arc>=campaignStoryCount||choice< -1||choice>2||collateral<0||collateral>9||previous< -1||previous>=campaignStoryCount||ending< -1||ending>2||scars<0||scars>9||(previous<0)!=(ending<0))return false;
    for(int value:tracks)if(value<0||value>9)return false;
    return true;
  };
  if(!storyBounds(c.storyArc,c.storyLastApproach,c.storyCollateral,c.storyTracks,c.legacyArc,c.legacyEnding,c.legacyScars)||
     (c.storyArc>=0&&((campaignCompleted(c)>0&&c.storyLastApproach<0)||std::accumulate(c.storyTracks.begin(),c.storyTracks.end(),0)!=campaignCompleted(c)||c.storyCollateral>campaignCompleted(c)))||
     (c.storyArc<0&&(c.storyLastApproach!=-1||c.storyCollateral||std::accumulate(c.storyTracks.begin(),c.storyTracks.end(),0))))throw std::runtime_error("Invalid campaign narrative state");
  for(int standing:c.standings)if(standing< -20||standing>20)throw std::runtime_error("Invalid faction standing");
  for(size_t i=0;i<c.history.size();++i){const auto &r=c.history[i];const auto &x=r.context;
    if(x.storyFront<0||x.storyFront>2)throw std::runtime_error("Invalid story front");
    if(!storyBounds(x.storyArc,x.storyApproach,x.storyCollateral,x.storyTracks,x.legacyArc,x.legacyEnding,x.legacyScars)||
       (x.storyArc>=0&&(x.storyApproach<0||std::accumulate(x.storyTracks.begin(),x.storyTracks.end(),0)!=x.stage-1)))throw std::runtime_error("Invalid story history");
    if(r.complete&&c.storyArc>=0&&(x.storyArc!=c.storyArc||x.stage>campaignCompleted(c)))throw std::runtime_error("Campaign story history changed framework");
    if((r.complete&&!(c.completed&(1u<<i)))||r.level<1||r.level>10||r.partySize<1||r.partySize>4||r.structuresLost<0||r.structuresLost>4000||
       x.stage<1||x.stage>9||x.employer< -1||x.employer>=voidmawFaction||x.adversary< -1||x.adversary>3||x.pressure<0||x.pressure>20||x.intel<0||x.intel>18||x.supply<0||x.supply>18)
      throw std::runtime_error("Invalid campaign history");
  }
}
bool canRegenerateCampaign(const Session &s){return s.phase==Phase::Hangar&&!s.campaign.completed&&!s.campaign.awaitingAdvance;}
namespace {
uint32_t offerSeed(const Session &s){return missionBoardSeed(s.seed,s.boardRevision)^s.campaign.consequences;}
int weightedFaction(Dice &d,const HumanStandings &standings,bool employer,int excluded=-1){
  std::array<int,4> weights{};
  for(size_t i=0;i<standings.size();++i){
    if(int(i)!=excluded)weights[i]=std::max(1,12+(employer?standings[i]:-standings[i]));
  }
  // Wild encounters retain their base weight independently of diplomacy.
  if(!employer&&excluded!=voidmawFaction)weights[voidmawFaction]=12;
  const int total=std::accumulate(weights.begin(),weights.end(),0);
  int value=int(d.next()%unsigned(total));for(int i=0;i<4;++i)if((value-=weights[i])<0)return i;
  return 0;
}
std::vector<int> factionRoster(int faction,int threatTier,int environment,bool decisive=false){
  std::vector<int> pool;
  for(size_t i=0;i<enemyCatalog().size();++i){const auto &e=enemySpec(i);
    if(enemyFaction(i)!=faction||e.hd>threatTier||(decisive&&(e.traits&Carrier)))continue;
    if((e.traits&Submersible)&&environment!=2)continue;
    if(environment==2&&!(e.traits&(Submersible|Hover|Flying))&&e.kind==EnemyKind::Conventional)continue;
    pool.push_back(int(i));
  }
  if(pool.empty())pool.push_back(faction==3?1:3);
  return pool;
}
CampaignContext context(const Session &s,const MissionRecipe &base){
  CampaignContext x;x.stage=1+campaignCompleted(s.campaign);x.employer=base.employer;x.adversary=base.adversary;
  if(s.campaign.completed){Dice d{base.seed^s.campaign.consequences};x.adversary=weightedFaction(d,s.campaign.standings,false);x.employer=weightedFaction(d,s.campaign.standings,true,x.adversary);}
  x.pressure=hostilityPressure(s.campaign.standings,x.adversary);x.intel=s.campaign.intel;x.supply=s.campaign.supply;return x;
}
void applyContext(MissionRecipe &r,const CampaignContext &x,const GameSettings &settings){
  r.campaignStage=x.stage;r.employer=x.employer;r.adversary=x.adversary;r.factionPressure=x.pressure;r.campaignIntel=x.intel;r.campaignSupply=x.supply;
  if(x.stage==1&&x.storyArc<0)return;
  static constexpr int factionThemes[]={9,2,8,7};r.theme=factionThemes[x.adversary];
  r.threatBudget+=3*(x.stage-1)+x.pressure;
  if(settings.enemy<0){
    auto pool=factionRoster(x.adversary,encounterThreatLimit(r.difficulty,x.stage),r.environment);Dice d{r.seed^0x71e9b35du};
    for(size_t n=pool.size();n>1;--n)std::swap(pool[n-1],pool[d.next()%n]);
    int count=int(r.enemies.size());
    if(!settings.enemyCount){float mean=0;for(int id:pool)mean+=enemySpec(id).hd;mean/=pool.size();
      count=std::clamp(int(std::lround(float(r.threatBudget)/std::max(1.f,mean)*(1.f-.01f*std::min(15,x.intel)))),3,maxInitialEnemies);}
    r.enemies.clear();r.tactics.clear();
    for(int i=0;i<count;++i){int id=pool[size_t(i)%pool.size()];r.enemies.push_back(id);r.tactics.push_back(enemySpec(id).kind==EnemyKind::Installation?2:i%3?4:r.tactic);}
  }
}
MissionRecipe finaleRecipe(const Session &s){
  GameSettings settings=s.settings;
  // A final battle needs an open, traversable arena for its largest combatant.
  // There are no dungeon/interior or submersible-only boss combinations.
  settings.maps=1;settings.objective=0;settings.location=2;settings.environment=3;settings.opener=0;
  auto r=generateMission(offerSeed(s)^0xf1a1e10u,0,settings,10,int(s.members.size()));
  auto x=context(s,r);x.stage=10;
  // In the absence of a hostile human power, the finale is a Voidmaw incursion.
  int rival=voidmawFaction;
  int lowest=0;
  for(size_t n=0;n<s.campaign.standings.size();++n){
    const int faction=int((r.seed+n)%s.campaign.standings.size());
    if(s.campaign.standings[faction]<lowest){lowest=s.campaign.standings[faction];rival=faction;}
  }
  x.adversary=rival;x.employer=(rival+1)%3;for(int i=0;i<3;++i)if(i!=rival&&s.campaign.standings[i]>s.campaign.standings[x.employer])x.employer=i;
  x.pressure=hostilityPressure(s.campaign.standings,rival);
  r.campaignStage=10;r.employer=x.employer;r.adversary=x.adversary;r.factionPressure=x.pressure;r.campaignIntel=x.intel;r.campaignSupply=x.supply;
  x=storyContext(s,0,x);x.stage=10;
  // Final adversary still follows the established faction consequences.
  x.adversary=rival;x.employer=r.employer;x.pressure=hostilityPressure(s.campaign.standings,rival);
  if(x.storyArc>=0){x.storyApproach=storyDominant(s.campaign.storyTracks,s.campaign.storyLastApproach);
    int employer=-1;for(int i=0;i<3;++i)if(i!=rival&&(employer<0||s.campaign.standings[i]>s.campaign.standings[employer]))employer=i;
    x.employer=r.employer=employer;
  }
  r.finale=true;r.finaleBattle=x.storyArc>=0?campaignStoryCatalog().at("campaigns").at(size_t(x.storyArc)).at("finales").at(size_t(x.storyApproach)).at("battle").get<bool>():((s.campaign.consequences^s.seed)%3)==0;
  applyStoryMission(r,x,s.settings.extent);
  static constexpr int factionThemes[]={9,2,8,7};r.theme=factionThemes[x.adversary];
  r.maps=1;r.siteCount=0;r.manualMission=-1;r.objective=0;r.objectiveCount=0;r.planetoidHazard=-1;r.planetoidFeature=6;
  r.complication=r.opportunity=-1;r.countdownSeconds=r.rechargeSeconds=0;r.difficulty=2;
  r.enemies.clear();r.tactics.clear();
  Dice d{r.seed};auto pool=factionRoster(x.adversary,10,r.environment,true);
  std::vector<int> bosses;for(size_t i=0;i<campaignBosses().size();++i)if(campaignBoss(int(i)).faction==x.adversary)bosses.push_back(int(i));
  int count=std::clamp((r.finaleBattle?24:8)+(r.partySize-1)*(r.finaleBattle?8:4)+x.pressure/4-x.intel/6,6,maxInitialEnemies);
  if(!r.finaleBattle){r.boss=bosses[d.next()%bosses.size()];r.enemies.push_back(bossBaseEnemy(r.boss));r.tactics.push_back(campaignBoss(r.boss).tactic);}
  std::vector<int> elites;for(int id:pool)if(enemySpec(id).hd>=7)elites.push_back(id);if(elites.empty())elites=pool;
  for(int n=int(r.enemies.size());n<count;++n){const auto &roster=r.finaleBattle&&n<2+r.partySize?elites:pool;int id=roster[d.next()%roster.size()];r.enemies.push_back(id);r.tactics.push_back(enemySpec(id).kind==EnemyKind::Installation?2:n%3?4:6);}
  r.threatBudget=0;for(int id:r.enemies)r.threatBudget+=enemySpec(id).hd;
  r.rewardCredits=0;fitMissionPacing(r,s.settings);return r;
}
}
MissionRecipe campaignMission(const Session &s,int slot,int sector){
  if(slot<0||slot>=9||sector<0||sector>3)throw std::runtime_error("Invalid campaign mission");
  if(campaignFinale(s.campaign))return finaleRecipe(s);
  const auto &record=s.campaign.history[size_t(slot)];
  uint32_t seed=record.complete?record.boardSeed:offerSeed(s);
  int party=record.complete?record.partySize:int(s.members.size());
  auto first=generateMission(seed,unsigned(slot),s.settings,1,party);
  auto x=record.complete?record.context:storyContext(s,slot,context(s,first));
  auto effective=storySettings(s.settings,x);
  auto r=generateMission(seed,unsigned(slot+sector*9),effective,1,party);
  applyStoryMission(r,x,s.settings.extent);
  fitMissionPacing(r,s.settings);
  applyContext(r,x,s.settings);
  if(x.storyArc>=0&&s.settings.tactic<0)for(size_t i=0;i<r.tactics.size();++i){
    const auto &spec=enemySpec(size_t(r.enemies[i]));
    bool closeOnly=std::all_of(spec.attacks.begin(),spec.attacks.end(),[](const auto &attack){return attack.range<=12;});
    r.tactics[i]=spec.kind==EnemyKind::Installation?2:closeOnly?(r.tactic==3?6:1):r.tactic;
  }
  preferStoryRoster(r);
  r.rewardCredits=contractCredits(r);
  return r;
}
MissionBoard generateMissionBoard(const Session &s){MissionBoard board;for(int slot=0;slot<9;++slot)board[slot]=campaignMission(s,slot);return board;}
std::vector<MissionCard> campaignCards(const Session &s){
  auto cards=missionCards(generateMissionBoard(s));if(campaignFinale(s.campaign))cards.resize(1);return cards;
}
void Session::checkpointCampaign(){
  campaign.active=true;campaign.seed=seed;campaign.revision=boardRevision;campaign.randomState=dice.state;validateCampaign(campaign);
  for(auto &m:members)if(!m.campaignAwarded)m.sheet.campaign=campaign;
}
void Session::advanceCampaign(){
  if(!campaign.awaitingAdvance||campaign.victorious||members.empty()||std::any_of(members.begin(),members.end(),[](const auto &m){return !m.debriefAccepted;}))return;
  campaign.awaitingAdvance=false;++boardRevision;if(campaignFinale(campaign))selectedMission=0;
  checkpointCampaign();log(campaignFinale(campaign)?"FINAL OPERATION UNLOCKED":"CAMPAIGN ADVANCED / REMAINING MISSIONS REGENERATED");
}
void Session::recordCampaignVictory(){
  const auto &r=mission.recipe;
  if(r.finale?campaign.victorious:bool(campaign.completed&(1u<<selectedMission)))return;
  const int collateral=std::min(3,mission.structuresLost/8);
  const int oppositionLosses=r.adversary>=0?mission.factionKills[r.adversary]:0;
  for(size_t f=0;f<campaign.standings.size();++f){
    const unsigned bit=1u<<f;
    const int kills=mission.factionKills[f];
    int delta=-(kills+2)/3;
    if(int(f)==r.adversary)delta-=r.objective==9?1:2;
    if(int(f)==r.employer)delta+=2-collateral;
    if(int(f)!=r.adversary&&int(f)!=r.employer&&
       !(bit&(mission.offendedNeutral|mission.withdrawnAllies))&&oppositionLosses>0&&
       (mission.supportedFactions&bit))++delta;
    if(mission.offendedNeutral&bit)delta-=2;
    if(mission.withdrawnAllies&bit)delta-=3;
    if(mission.lostAllies&bit)delta-=3;
    const int old=campaign.standings[f];
    campaign.standings[f]=std::clamp(old+delta,-20,20);
    for(auto &member:members){
      int &standing=member.sheet.career.standings[f];
      standing=member.id==host?campaign.standings[f]:std::clamp(standing+delta,-20,20);
    }
    const int change=campaign.standings[f]-old;
    if(change)log(std::string(factions[f])+" / STANDING "+(change>0?"+":"")+std::to_string(change));
  }
  for(auto &member:members)awardServiceHonours(member.sheet);
  if(r.finale){campaign.victorious=true;campaign.awaitingAdvance=false;checkpointCampaign();log("CAMPAIGN VICTORY / ACCEPT YOUR AWARD IN DEBRIEF");return;}
  auto &record=campaign.history[size_t(selectedMission)];
  record.complete=true;record.boardSeed=offerSeed(*this);record.level=r.level;record.partySize=r.partySize;
  record.structuresLost=mission.structuresLost;
  record.context={r.campaignStage,r.employer,r.adversary,r.factionPressure,r.campaignIntel,r.campaignSupply};
  record.context.storyFront=r.storyFront;record.context.storyArc=r.storyArc;record.context.storyApproach=r.storyApproach;record.context.storyTracks=r.storyTracks;record.context.storyCollateral=r.storyCollateral;
  record.context.legacyArc=r.legacyArc;record.context.legacyEnding=r.legacyEnding;record.context.legacyScars=r.legacyScars;
  recordStoryVictory(campaign,r,mission.structuresLost);
  campaign.completed|=1u<<selectedMission;campaign.awaitingAdvance=true;
  if(r.objective==3||r.objective==9)campaign.intel=std::min(18,campaign.intel+2);
  if(r.objective==1||r.objective==2||r.objective==4||r.objective==8)campaign.supply=std::min(18,campaign.supply+2);
  if(r.objective==5)campaign.intel=std::min(18,campaign.intel+1);
  campaign.devastation=std::min(9000,campaign.devastation+mission.structuresLost);
  uint32_t outcome=r.seed^unsigned(r.storyArc+1)*0x27d4eb2du^unsigned(r.storyApproach+1)*0x165667b1u^unsigned(r.objective+1)*0x85ebca6bu^unsigned(mission.structuresLost)*0xc2b2ae35u;
  for(int standing:campaign.standings)outcome=missionBoardSeed(outcome,unsigned(standing+20));
  campaign.consequences=missionBoardSeed(campaign.consequences^outcome,unsigned(selectedMission+1));
  checkpointCampaign();log("CAMPAIGN CONSEQUENCES RECORDED / "+std::to_string(campaignCompleted(campaign))+" OF 9");
}
CampaignAwardKey campaignAwardKey(const CampaignState &campaign){
  return {1,campaign.seed,campaign.revision,campaign.consequences};
}
Sheet claimCampaignAward(const Sheet &old,const CampaignState &victory,std::optional<Chassis> nextChassis){
  if(!victory.victorious)throw std::runtime_error("Campaign victory is required");
  const auto award=campaignAwardKey(victory);
  if(old.lastCampaignAward==award)return old;

  Sheet next=old;
  if(nextChassis && *nextChassis!=old.chassis){
    if(*nextChassis<Chassis::Brawler||*nextChassis>=Chassis::Count)
      throw std::runtime_error("Invalid next campaign chassis");
    next.chassis=*nextChassis;
    next.starterModule=chassisStarterModule(next.chassis);
  }
  if(next.starterModule==Module::Count)
    next.starterModule=chassisStarterModule(next.chassis);
  next.modules={next.starterModule};
  next.refinements={};
  normalizeCombatProfile(next);
  next.oneShotModule=Module::ChestBeam;
  next.created=false; // Use the same baseline kit as pilot creation.
  startingEquipment(next,false);
  next.created=old.created;
  next.startingCredits=old.startingCredits;
  next.credits=old.credits;
  next.inventory[size_t(Gear::Scrap)]=old.inventory[size_t(Gear::Scrap)];
  next.campaignAwards=std::min(1000000,old.campaignAwards+1);
  next.lastCampaignAward=award;
  next.campaign=restartCampaign(victory);
  next.campaign.standings=next.career.standings;
  if(victory.storyArc>=0){
    next.campaign.legacyArc=victory.storyArc;
    next.campaign.legacyEnding=storyDominant(victory.storyTracks,victory.storyLastApproach);
    next.campaign.legacyScars=std::min(9,victory.storyCollateral+victory.legacyScars);
  }
  validate(next);
  return next;
}
}
