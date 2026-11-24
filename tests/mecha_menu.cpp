#include "rpg/menu.hpp"
#include "rpg/mission_board.hpp"
#include "rpg/preparation.hpp"
#include "rpg/network.hpp"
#include <thread>
#include <chrono>
#include <cassert>
#include <iostream>
#include <set>
using namespace iron;using namespace iron::rpg;
template<class F>bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
int main(){
 MenuNavigation nav;assert(!nav.update(0,0,true,false,false).select);assert(!nav.update(0,0,true,false,true).select);
 nav.update(0,0,false,false,true);assert(nav.update(0,0,true,false,true).select);assert(!nav.update(0,0,true,false,true).select);
 nav.update(0,0,false,false,true);auto both=nav.update(0,0,true,true,true);assert(both.back&&!both.select);
 nav.update(0,0,false,false,true);auto diagonal=nav.update(.7f,.9f,false,false,true);assert(diagonal.row==-1&&!diagonal.change);assert(!nav.update(.7f,.9f,false,false,true).row);
 nav.update(0,0,false,false,true);assert(nav.update(.9f,.7f,false,false,true).change==1);
 nav.update(0,0,false,false,false);nav.update(0,0,true,false,false);assert(!nav.update(0,0,true,false,true).select);
 PlayerMenu menu;menu.open(MenuPage::Hangar);auto host=menu.actions(true,true),guest=menu.actions(false,true);
 assert(host.size()==guest.size()+1&&std::count(host.begin(),host.end(),PlayerAction::NewCampaign)==1&&std::count(guest.begin(),guest.end(),PlayerAction::NewCampaign)==0);
 Session campaign(713);campaign.add(1,"Host");campaign.add(2,"Guest");auto initial=campaign.snapshot();
 for(auto op:{"settings","quality","rest","next"})for(unsigned id:{1u,2u})assert(rejects([&]{campaign.playerCommand(id,{{"op",op}});}));
 assert(campaign.snapshot()==initial);assert(rejects([&]{campaign.playerCommand(2,{{"op","reroll_missions"}});}));
 assert(rejects([&]{campaign.playerCommand(1,{{"op","reroll_missions"},{"seed",42}});}));
 auto before=generateMissionBoard(campaign.seed,campaign.settings);std::set<int> lengths,widths;
 for(const auto &r:before){assert(r.maps>=1&&r.maps<=3&&r.radius>=500&&r.radius<=1000);lengths.insert(r.maps);widths.insert(int(r.radius));}
 assert(lengths.size()>=2&&widths.size()>1);
 campaign.campaign.completed=5;assert(rejects([&]{campaign.playerCommand(1,{{"op","reroll_missions"}});}));campaign.campaign.completed=0;campaign.playerCommand(1,{{"op","reroll_missions"}});assert(campaign.campaign.completed==0&&campaign.selectedMission==0&&campaign.boardRevision==1);
 auto after=generateMissionBoard(campaign.seed,campaign.settings,1,campaign.boardRevision);
 for(int slot=0;slot<9;++slot)assert(before[slot].seed!=after[slot].seed);
 campaign.campaign.completed=1;assert(rejects([&]{campaign.playerCommand(1,missionSelectionCommand(0));}));
 campaign.playerCommand(1,missionSelectionCommand(1));assert(campaign.boardRevision==1);
 campaign.phase=Phase::Complete;campaign.playerCommand(1,{{"op","hangar"}});assert(campaign.boardRevision==1&&campaign.campaign.completed==1&&campaign.selectedMission==1);
 equipQuality(campaign.member(1).sheet,RightHand,Quality::Sniper);campaign.playerCommand(1,{{"op","equip"},{"slot",RightHand},{"gear","rifle"}});assert(campaign.member(1).sheet.qualities[RightHand]==Quality::Sniper);
 Session replica;replica.readSnapshot(campaign.snapshot());assert(replica.snapshot()==campaign.snapshot());
 auto invalid=campaign.snapshot();invalid["completed_missions"]=512u;assert(rejects([&]{Session bad;bad.readSnapshot(invalid);}));
 menu.open(MenuPage::Pause);float sound=.5f,music=.5f;bool mic=true;menu.open(MenuPage::Options);
 for(int n=0;n<20;++n)menu.update({0,-1},true,true,sound,music,mic);assert(sound==0&&music==.5f);
 menu.update({1,0},true,true,sound,music,mic);menu.update({0,1},true,true,sound,music,mic);assert(music==.6f);
 menu.update({0,0,false,true},true,true,sound,music,mic);assert(menu.page==MenuPage::Pause);
 assert(menu.update({0,0,false,true},true,true,sound,music,mic)==PlayerAction::Resume);
 for(auto phase:{Phase::Hangar,Phase::Briefing}){Session ready;ready.add(1,"Host");ready.phase=phase;ready.member(1).ready=true;ready.playerCommand(1,{{"op","ready"},{"value",false}});assert(ready.phase==Phase::Hangar&&!ready.member(1).ready);}
 for(int i=0;i<int(Module::Count);++i){std::string text=moduleDescription(Module(i));for(const char *word:{"roll reactor","dice"," die ","rulebook","1d","2d"})assert(text.find(word)==std::string::npos);}
 Server server("127.0.0.1",0,713);Client client("127.0.0.1:"+std::to_string(server.port()),"Network Host");
 auto poll=[&](){for(int n=0;n<60;++n){server.step();client.update(1.f/60);std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
 poll();assert(client.connected&&client.id==server.session.host);auto settings=Json(server.session.settings);
 client.command({{"op","settings"},{"settings",GameSettings{}}});poll();assert(!client.error.empty()&&Json(server.session.settings)==settings);client.error.clear();
 client.command({{"op","reroll_missions"},{"seed",42}});poll();assert(!client.error.empty()&&server.session.boardRevision==0);client.error.clear();
 client.command({{"op","reroll_missions"}});poll();assert(client.error.empty()&&server.session.boardRevision==1);
 auto profile=server.session.member(client.id).sheet;profile.campaignAwards=2;
 client.command({{"op","configure"},{"profile",profile},{"configuration",77u}});poll();assert(client.error.empty()&&client.session.member(client.id).configuration==77&&client.session.member(client.id).sheet.campaignAwards==2);
 std::cout<<"Unified controls, focus/edge safety, one host campaign action, generated lengths, completion state and options passed\n";
}
