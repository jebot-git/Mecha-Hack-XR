#pragma once
#include "rpg/menu.hpp"
#include "rpg/preparation.hpp"
#include "rpg/profiles.hpp"
#include "rpg/mission_board.hpp"
namespace iron::rpg {
inline void previewMenus(Renderer &renderer,SDL_Window *window,const std::filesystem::path &out,nlohmann::json &frames){
 Session session(713);session.add(1,"Atlas Echo");session.add(2,"Beacon Prime");for(auto &m:session.members)finishCreation(m.sheet);
 Frontend frontend;frontend.applicationTitle="MECHA HACK XR";frontend.missionCards=missionCards(generateMissionBoard(session.seed,session.settings));frontend.host=true;frontend.cooperative=true;
 auto captureMenu=[&](const std::string &name){
  SDL_Event event;while(SDL_PollEvent(&event)){}int width,height;SDL_GL_GetDrawableSize(window,&width,&height);
  World world=session.view(1,renderer.armRig());world.rpgPrototype=true;world.health=100;world.environment={Lighting::Day,Weather::Clear,7};
  auto camera=multiply(translation(world.position.x,world.position.y+12,world.position.z),rotation(world.facing(),0));float vertical=.65f,horizontal=std::atan(std::tan(vertical)*float(width)/height);auto lens=projection({-horizontal,horizontal,vertical,-vertical},.05f,3000);
  renderer.updateCockpit(world,1.f/60,false);auto clock=renderer.cockpitEffects().clock;
  for(float offset:{-.032f,.032f}){glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width,height);renderer.draw(world,lens,multiply(camera,translation(offset,0,0)),camera,false,&frontend);if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Menu renderer GL error: "+name);}
  if(renderer.cockpitEffects().clock!=clock)throw std::runtime_error("Menu eye advanced animation");
  std::vector<unsigned char> pixels(size_t(width)*height*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());size_t amberPixels=0;for(size_t i=0;i<pixels.size();i+=3)if(pixels[i]>170&&pixels[i+1]>100&&pixels[i+2]<110)++amberPixels;if(amberPixels<40)throw std::runtime_error("Menu capture contains no readable highlights: "+name);
  capture(out/(name+".ppm"),width,height);SDL_GL_SwapWindow(window);
  frames.push_back({{"id",name},{"amber_pixels",amberPixels},{"width",width},{"height",height},{"stereo_views",2},{"lines",frontend.menuLines},{"controls",frontend.message}});
 };
 PlayerMenu menu;menu.open(MenuPage::Hangar);frontend.screen=Screen::Pause;frontend.interfaceArt="hangar";frontend.message=menuHint;
 frontend.menuLines=menu.lines(session,1,.7f,.5f,true,true);captureMenu("hangar-host");
 frontend.menuLines=menu.lines(session,2,.7f,.5f,true,true);captureMenu("hangar-guest");
 menu.open(MenuPage::Options);frontend.menuLines=menu.lines(session,1,.7f,.5f,true,true);captureMenu("options-online");
 frontend.menuLines=menu.lines(session,1,.7f,.5f,true,false);captureMenu("options-offline");
 menu.open(MenuPage::Pause);frontend.menuLines=menu.lines(session,1,.7f,.5f,true,true);captureMenu("pause");
 ProfileStore profiles(out/"pilot-fixture");profiles.save(0,session.member(1).sheet,"Atlas Echo");profiles.save(2,session.member(2).sheet,"Beacon Prime");frontend.interfaceArt="profile";frontend.screen=Screen::Title;frontend.menuLines=profiles.lines(0);captureMenu("profiles");
 Dice dice{712};Preparation prep;prep.sheet=createSheet(Chassis::Striker,PilotKind::Engineer,dice);frontend.interfaceArt="creation";frontend.screen=Screen::Loadout;frontend.menuLines=prep.lines(session,1,"");captureMenu("creation");
 prep.selected=1;frontend.menuLines=prep.lines(session,1,"");captureMenu("creation-pilot-role");
 prep.selected=0;finishCreation(prep.sheet);frontend.interfaceArt="hangar";frontend.menuLines=prep.lines(session,1,"");frontend.equipmentModel=prep.previewModel();captureMenu("equipment-first");
 prep.selected=prep.rowCount()-1;frontend.menuLines=prep.lines(session,1,"");captureMenu("equipment-ready");
 prep.trading=true;prep.selected=1;prep.sheet.credits=200;prep.sheet.inventory[size_t(Gear::Scrap)]=10;frontend.interfaceArt="trader";frontend.menuLines=prep.lines(session,1,"");captureMenu("trade");
 recoverWeapon(prep.sheet,Gear::Rifle,{1,-1,1});
 tuneWeapon(prep.sheet,Gear::Rifle,1,WeaponStat::Heat);
 prep.selected=prep.rowCount()-2;prep.selectedCopy=1;prep.tuneStat=WeaponStat::Cycle;
 frontend.menuLines=prep.lines(session,1,"");frontend.equipmentModel=prep.previewModel();captureMenu("trade-weapon-tuning");
 frontend.equipmentModel.clear();frontend.screen=Screen::Missions;frontend.menuLines.clear();frontend.mission=1;frontend.generatedCompleted=1;captureMenu("missions");
 frontend.generatedCompleted=0;frontend.screen=Screen::Briefing;frontend.interfaceArt="briefing";frontend.menuLines=frontend.missionCards[0].briefing;frontend.message="MENU: EQUIPMENT  R CLICK: MISSIONS";captureMenu("briefing");
 session.campaign.completed=7;session.campaign.standings={-6,4,2};session.campaign.intel=2;session.campaign.supply=4;session.campaign.consequences=0xb12d045au;session.boardRevision=3;
 Dice levels{913};for(auto &m:session.members){m.sheet.credits+=modulePurchaseCost;if(!m.sheet.has(Module::JumpJets))installModule(m.sheet,Module::JumpJets);}
 frontend.screen=Screen::Pause;frontend.interfaceArt="hangar";frontend.message=menuHint;menu.open(MenuPage::Hangar);frontend.menuLines=menu.lines(session,1,.7f,.5f,true,true);captureMenu("campaign-hangar");
 frontend.screen=Screen::Missions;frontend.missionCards=campaignCards(session);frontend.menuLines.clear();frontend.generatedCompleted=session.campaign.completed;frontend.mission=3;captureMenu("campaign-offers");
 session.mission.recipe=campaignMission(session,3);session.mission.contractReward=session.mission.recipe.rewardCredits;
 frontend.screen=Screen::Debrief;frontend.interfaceArt="debrief";frontend.menuLines=campaignDebriefLines(session,1);frontend.message="A: ACCEPT";captureMenu("campaign-debrief");
 session.campaign.completed=511;session.boardRevision=9;session.selectedMission=0;
 frontend.screen=Screen::Missions;frontend.generatedFinale=true;frontend.generatedCompleted=0;frontend.missionCards=campaignCards(session);frontend.mission=0;frontend.menuLines.clear();frontend.message=menuHint;captureMenu("finale-board");
 frontend.screen=Screen::Briefing;frontend.interfaceArt="briefing";frontend.menuLines=frontend.missionCards[0].briefing;frontend.message="MENU: EQUIPMENT  R CLICK: BACK";captureMenu("finale-briefing");
 session.member(1).sheet.credits=875;session.member(1).sheet.inventory[size_t(Gear::Scrap)]=16;
 session.campaign.victorious=true;frontend.screen=Screen::Debrief;frontend.interfaceArt="debrief";frontend.menuLines=campaignDebriefLines(session,1);frontend.message="A: CLAIM CAMPAIGN AWARD";captureMenu("campaign-victory");
 session.member(1).sheet=claimCampaignAward(session.member(1).sheet,session.campaign);session.member(1).campaignAwarded=true;frontend.menuLines=campaignDebriefLines(session,1);frontend.message="A: RETURN TO HANGAR";captureMenu("campaign-award");
 prep.sheet=session.member(1).sheet;prep.selected=0;prep.trading=false;frontend.screen=Screen::Loadout;frontend.interfaceArt="hangar";frontend.menuLines=prep.lines(session,1,"");frontend.message=menuHint;captureMenu("campaign-return-equipment");
}
}
