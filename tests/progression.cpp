#include "frontend.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
using namespace iron;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(int argc,char** argv){
    Progression p;
    if(argc>1)p.rules.load(argv[1]);
    check(p.rules.rewards==std::vector<int>{5,-1,3,4,2,-1,6,-1,7,-1,-1,8,-1,-1,-1,-1},"ROM rewards");
    check(p.rules.available(0)==15&&p.rules.weapons(0)==3,"initial availability");
    check(!p.complete(4)&&!p.complete(-1)&&!p.complete(16),"locked/invalid mission");
    for(int i:{3,1,2})check(p.complete(i),"out of order group");
    check(!p.missionAvailable(4),"whole group required");
    check(p.complete(0)&&p.missionAvailable(7)&&!p.missionAvailable(8),"group two");
    check(p.weaponAvailable(Weapon::Grenade)&&p.weaponAvailable(Weapon::Cutter)&&p.weaponAvailable(Weapon::Gatling),"first group weapons");
    for(int i=4;i<8;++i)p.complete(i);
    check(p.rules.available(p.completed)==4095,"group three");
    for(int i=8;i<12;++i)p.complete(i);
    check(p.rules.available(p.completed)==8191,"only mission thirteen");
    for(int i=12;i<16;++i){check(p.nextMission()==i,"final sequential");check(p.complete(i),"final victory");}
    check(p.finished()&&p.rules.weapons(p.completed)==511&&!p.complete(15),"campaign complete and replay");
    Frontend f;f.screen=Screen::Missions;Input input;f.mission=12;f.update(input,.02f,true,false);
    check(f.screen==Screen::Missions,"locked briefing");
    f.screen=Screen::Loadout;f.confirmHeld=false;check(!f.update(input,.02f,true,false),"locked deploy");
    f.weapon=int(Weapon::Cruise);check(!f.equip(LeftShoulder),"locked equipment");
    f.mission=0;f.confirmHeld=false;f.loadout[LeftShoulder]=Weapon::Cruise;check(!f.update(input,.02f,true,false),"injected locked loadout");
    f.loadout[LeftShoulder]=Weapon::Empty;
    World w;w.mission=0;w.health=100;
    for(auto status:{MissionStatus::Range,MissionStatus::Active,MissionStatus::Ejecting,MissionStatus::Failed}){w.missionStatus=status;check(!f.recordResult(w),"non victory cannot advance");}
    w.missionStatus=MissionStatus::Complete;w.lifeSpent=true;check(!f.recordResult(w),"death cannot advance");w.lifeSpent=false;
    f.sandbox=true;check(!f.recordResult(w),"sandbox cannot advance");f.sandbox=false;
    check(f.recordResult(w)&&!f.recordResult(w)&&f.campaign.weaponAvailable(Weapon::Grenade),"victory once");
    f.acceptResult();check(f.screen==Screen::Missions&&f.mission==1,"next available selection");
    char temp[]="/tmp/iron-progression-XXXXXX";check(mkdtemp(temp),"temp directory");auto path=std::filesystem::path(temp)/"campaign.json";
    if(argc>1)for(int game=0;game<2;++game){
        Progression campaign;campaign.rules.load(argv[1],game);
        check(campaign.weaponAvailable(Weapon::Rifle),"both campaigns start with rifle");
        check(campaign.weaponAvailable(Weapon::Cutter)==bool(game)&&campaign.weaponAvailable(Weapon::Grenade)==bool(game),"IS2 starts with cutter and hand grenades; IS1 earns them");
        for(int mission=0;mission<campaign.rules.count();++mission){
            auto before=campaign.rules.weapons(campaign.completed);check(campaign.complete(mission),"campaign mission can be completed in order");
            auto reward=campaign.rules.rewards[mission];auto expected=before|(reward>=0?1u<<reward:0u);
            check(campaign.rules.weapons(campaign.completed)==expected,"only the completed mission reward unlocks new equipment");
            if(game&&mission==5)check((expected&511)==511,"IS2's eight shared weapons are obtainable by mission six");
        }
        for(int weapon=1;weapon<int(Weapon::Count);++weapon)check(campaign.weaponAvailable(Weapon(weapon))==(game||weapon<9),"IS1 unlocks eight original weapons; IS2 unlocks all twelve");
        campaign.loadout={Weapon::Cutter,Weapon::Rifle,Weapon::Rocket,Weapon::Rail,Weapon::Grenade,Weapon::Gatling};
        auto unlocked=std::filesystem::path(temp)/("all-weapons-"+std::to_string(game)+".json");campaign.save(unlocked);
        Progression loaded;loaded.rules.load(argv[1],game);loaded.load(unlocked);check(loaded.loadout==campaign.loadout&&loaded.rules.weapons(loaded.completed)==campaignWeaponMask(game),"campaign-derived unlocks and mixed hand equipment survive save reload");
        Frontend arming;arming.campaign=loaded;arming.game=game;arming.restoreCampaign();arming.screen=Screen::Loadout;
        check(arming.weaponAvailable(Weapon::Cutter)&&arming.weaponAvailable(Weapon::Grenade),"frontend receives campaign starting and reward unlocks");
    }
    Frontend victory;victory.screen=Screen::Playing;World completed;completed.mission=0;completed.missionStatus=MissionStatus::Complete;Input heldMenu;heldMenu.fire[RightHand]=true;
    check(victory.recordResult(completed),"victory awards before debrief");victory.showDebrief(0,heldMenu);
    auto awardPath=std::filesystem::path(temp)/"award.json";victory.campaign.save(awardPath);Progression loadedAward;loadedAward.load(awardPath);
    check(loadedAward.completed==1&&loadedAward.weaponAvailable(Weapon::Grenade),"debrief award survives save and reload");
    victory.update(heldMenu,.02f,true,false);check(victory.screen==Screen::Debrief,"held completion/menu button cannot skip debrief or open pause");
    heldMenu.fire.fill(false);victory.update(heldMenu,.02f,false,false);victory.update(heldMenu,.02f,true,false);
    check(victory.screen==Screen::Missions&&victory.mission==1&&victory.weaponAvailable(Weapon::Grenade),"debrief advances to next mission with award retained");
    Frontend guest;guest.cooperative=true;guest.showDebrief(0,Input{});guest.update(Input{},.02f,false,false);guest.update(Input{},.02f,true,false);
    check(guest.screen==Screen::Standby,"co-op guest leaves debrief for host selection, not pause");

    Progression a,b;a.complete(0);a.selectedMission=2;a.loadout[RightHip]=Weapon::Grenade;a.save(path);
    Progression restored;restored.load(path);check(restored.completed==1&&restored.selectedMission==2&&restored.loadout[RightHip]==Weapon::Grenade,"round trip");
    b.complete(2);b.save(path);restored.load(path);check(restored.completed==5,"stale writer merges victories");
    for(int hand=LeftHand;hand<=RightHand;++hand){
        Progression pair;pair.completed=65535;pair.loadout.fill(Weapon::Empty);
        pair.loadout[hand]=Weapon::Rifle;pair.loadout[1-hand]=Weapon::Cutter;
        auto pairPath=std::filesystem::path(temp)/("hands-"+std::to_string(hand)+".json");pair.save(pairPath);
        Progression reloaded;reloaded.load(pairPath);check(reloaded.loadout==pair.loadout&&reloaded.loadout[1-hand]==Weapon::Cutter,"mixed hand loadout survives save sanitation and reload in either arrangement");
        pair.loadout[hand]=Weapon::Cutter;pair.sanitizeLoadout();
        check(std::count(pair.loadout.begin(),pair.loadout.end(),Weapon::Cutter)==1,"save sanitation still enforces one cutter copy");
    }
    nlohmann::json valid;{std::ifstream in(path);in>>valid;}
    auto locked=valid;locked["loadout"][2]=8;{std::ofstream out(path);out<<locked;}restored.load(path);check(restored.loadout[2]==Weapon::Empty,"scrub unavailable weapon");
    for(auto bad: {nlohmann::json{{"schema",2}},nlohmann::json{{"completed",4096}},nlohmann::json{{"completed",1.5}},nlohmann::json{{"loadout",{0,1,int(Weapon::Count),0,0,0}}}}){
        auto j=valid; j.update(bad);{std::ofstream out(path);out<<j;}
        bool rejected=false;try{restored.load(path);}catch(...){rejected=true;}
        check(rejected&&restored.completed==5,"invalid save rejected atomically");
        rejected=false;try{a.save(path);}catch(...){rejected=true;}check(rejected,"do not overwrite corrupt save");
    }
    for(auto& e:std::filesystem::directory_iterator(temp))check(e.path().filename().string().find(".tmp.")==std::string::npos,"no temporary leftovers");
    std::filesystem::remove_all(temp);std::cout<<"Campaign progression checks passed\n";
}
