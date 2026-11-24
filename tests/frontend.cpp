#include "frontend.hpp"
#include <cstdlib>
#include <iostream>
using namespace iron;
static int count=0;
static void check(bool value,const char* name){++count;if(!value){std::cerr<<name<<'\n';std::exit(1);}}
int main(){
    Frontend volume;volume.screen=Screen::Pause;volume.option=5;Input adjustment;adjustment.steer=-1;
    volume.update(adjustment,.02f,false,false);check(std::abs(volume.soundVolume-.95f)<.0001f&&volume.option==5,"horizontal stick changes selected sound volume without leaving its row");
    volume.update(adjustment,.02f,false,false);check(std::abs(volume.soundVolume-.95f)<.0001f,"held adjustment is consumed");
    for(int n=0;n<25;++n){volume.update(Input{},.02f,false,false);volume.update(adjustment,.02f,false,false);}check(volume.soundVolume==0,"sound volume clamps at mute");
    volume.update(Input{},.02f,false,false);adjustment.steer=0;adjustment.forward=-1;volume.update(adjustment,.02f,false,false);check(volume.option==6&&volume.musicVolume==1,"vertical stick selects the separate music control");
    volume.update(Input{},.02f,false,false);adjustment.forward=0;adjustment.steer=-1;volume.update(adjustment,.02f,false,false);check(std::abs(volume.musicVolume-.95f)<.0001f&&volume.soundVolume==0,"music adjustment does not change sound volume");
    volume.update(Input{},.02f,false,false);volume.update(Input{},.02f,true,false);check(volume.screen==Screen::Pause&&volume.action==MenuAction::None,"selecting a volume row cannot leave the game");
    volume.option=7;volume.update(Input{},.02f,false,false);volume.update(Input{},.02f,true,false);check(volume.action==MenuAction::Recenter,"pause offers a cockpit reset without leaving the mission");
    volume.action=MenuAction::None;volume.option=8;volume.update(Input{},.02f,false,false);volume.update(Input{},.02f,true,false);check(volume.action==MenuAction::Leave,"leave action remains explicit after volume and reset rows");
    Frontend f;f.screen=Screen::Missions;f.sandbox=true;Input in;
    for(int grip=0;grip<2;++grip)for(int mount=0;mount<MountCount;++mount){
        Frontend removal;removal.screen=Screen::Loadout;removal.sandbox=true;removal.weapon=int(Weapon::Cruise);removal.loadout.fill(Weapon::Rocket);
        Input controls;controls.ping[grip]=true;controls.fire[mount]=true;removal.update(controls,.02f,false,false);
        check(removal.loadout[mount]==Weapon::Empty&&removal.weapon==int(Weapon::Cruise),"either grip plus any mount removes only that slot without changing selected weapon");
        for(int other=0;other<MountCount;++other)if(other!=mount)check(removal.loadout[other]==Weapon::Rocket,"grip removal preserves other mount slots");
        controls.ping[grip]=false;removal.update(controls,.02f,false,false);check(removal.loadout[mount]==Weapon::Empty,"holding mount while releasing grip cannot re-equip");
    }
    check(f.screen==Screen::Missions,"starts in mission selection");
    in.steer=-1;f.update(in,.02f,false,false);check(f.mission==15,"mission wraps backwards");
    f.update(in,.02f,false,false);check(f.mission==15,"held stick does not skip missions");
    in.steer=0;f.update(in,.02f,false,false);in.steer=1;f.update(in,.02f,false,false);check(f.mission==0,"mission wraps forwards");
    in.steer=0;in.fire[RightHand]=true;f.update(in,.02f,false,false);check(f.screen==Screen::Briefing,"right trigger opens briefing");
    f.update(in,.02f,false,false);check(f.screen==Screen::Briefing,"held trigger cannot skip briefing");
    in.fire.fill(false);f.update(in,.02f,false,false);in.fire[RightHand]=true;f.update(in,.02f,false,false);
    check(f.screen==Screen::Loadout,"fresh trigger opens arming");
    f.weapon=int(Weapon::Cruise);f.update(in,.02f,false,false);check(f.loadout[RightHand]==Weapon::Rifle,"entry trigger cannot equip weapon");
    in.fire.fill(false);f.update(in,.02f,false,false);in.fire[LeftShoulder]=true;f.update(in,.02f,false,false);
    check(f.loadout[LeftShoulder]==Weapon::Cruise,"Y fits cruise to left shoulder");
    f.weapon=int(Weapon::Shield);f.update(in,.02f,false,false);check(f.loadout[LeftShoulder]==Weapon::Cruise,"held mount cannot replace equipment");
    in.fire.fill(false);f.update(in,.02f,false,false);f.weapon=int(Weapon::Grenade);in.fire[RightHip]=true;f.update(in,.02f,false,false);check(f.loadout[RightHip]==Weapon::Grenade,"A fits hip grenade rack");
    for(int w=0;w<int(Weapon::Count);++w)for(int m=0;m<MountCount;++m){
        if((w==int(Weapon::Cruise)||w==int(Weapon::Rail)||w==int(Weapon::Shield)))check(compatible(Weapon(w),m)==(m==2||m==3),"shoulder-only restrictions");
    }
    f.weapon=int(Weapon::Cutter);check(f.equip(LeftHand)&&f.loadout[RightHand]==Weapon::Rifle,"left cutter preserves right rifle");
    check(!f.equip(RightHand)&&f.loadout[RightHand]==Weapon::Rifle,"second cutter respects inventory and cannot replace the rifle");
    f.weapon=0;check(f.equip(LeftHand),"remove mode frees only the selected hand");
    f.weapon=int(Weapon::Cutter);check(f.equip(RightHand),"cutter replaces rifle in the same hand");
    f.weapon=int(Weapon::Rifle);check(f.equip(LeftHand)&&f.loadout[RightHand]==Weapon::Cutter,"left rifle preserves right cutter");
    for(int hand=LeftHand;hand<=RightHand;++hand)for(auto first:{Weapon::Rifle,Weapon::Cutter}){
        Frontend pair;pair.sandbox=true;pair.screen=Screen::Loadout;pair.loadout.fill(Weapon::Empty);
        pair.weapon=int(first);Input control;control.fire[hand]=true;pair.update(control,.02f,false,false);
        pair.update(Input{},.02f,false,false);pair.weapon=int(first==Weapon::Rifle?Weapon::Cutter:Weapon::Rifle);
        control.fire.fill(false);control.fire[1-hand]=true;pair.update(control,.02f,false,false);
        check(pair.loadout[hand]==first&&pair.loadout[1-hand]==Weapon(pair.weapon),"both equip orders and hand arrangements preserve the occupied hand");
        pair.update(Input{},.02f,false,false);check(pair.update(Input{},.02f,true,false)&&pair.screen==Screen::Playing,"mixed handheld loadout deploys");
        World mixed;mixed.targets.clear();mixed.configure(pair.loadout);mixed.step(Input{},.02f);
        Input fire;fire.fire[LeftHand]=fire.fire[RightHand]=true;mixed.step(fire,.02f);
        int rifle=first==Weapon::Rifle?hand:1-hand,cutter=1-rifle;
        check(mixed.shots[rifle]==1&&mixed.ammo[rifle]==399&&mixed.punches[cutter]==1,"rifle fires while the opposite cutter attacks");
        check(mixed.arms[LeftHand].tracked&&mixed.arms[RightHand].tracked,"both equipped hands remain tracked");
    }
    in.fire.fill(false);check(f.update(in,.02f,true,false),"confirm deploys");check(f.screen==Screen::Playing,"deployment changes screen");
    World world;world.configure(f.loadout);check(world.ammo[LeftShoulder]==1&&world.ammo[RightHip]==8,"original rack capacities");
    world.step(Input{},.02f);in.fire[RightHip]=true;world.step(in,.02f);check(!world.missile.active&&world.projectiles.size()==1,"A fires equipped grenade rather than hardcoded cruise");
    in.fire.fill(false);world.step(in,.02f);in.fire[LeftShoulder]=true;world.step(in,.02f);check(world.missile.active,"cruise follows shoulder mount");
    Frontend unfocused;unfocused.screen=Screen::Missions;in.focused=false;in.steer=1;in.fire.fill(true);unfocused.update(in,.02f,true,false);check(unfocused.screen==Screen::Missions&&unfocused.mission==0,"unfocused menu cannot navigate or accept");
    Frontend client;client.cooperative=true;client.missionChosen=true;client.screen=Screen::Briefing;Input menuInput;
    client.update(menuInput,.02f,false,true);check(client.screen==Screen::Standby,"client can step back to standby");client.update(menuInput,.02f,false,false);client.update(menuInput,.02f,true,false);check(client.screen==Screen::Briefing,"selected briefing remains accessible");
    Frontend duplicates;duplicates.sandbox=true;duplicates.loadout.fill(Weapon::Empty);
    duplicates.weapon=int(Weapon::Rail);check(duplicates.equip(LeftShoulder),"first rail allowed");check(!duplicates.equip(RightShoulder),"second rail forbidden by cartridge inventory");
    duplicates.weapon=int(Weapon::Cruise);check(duplicates.equip(LeftShoulder)&&duplicates.equip(RightShoulder),"two cruise missiles allowed");
    duplicates.weapon=int(Weapon::Gatling);check(duplicates.equip(LeftShoulder)&&duplicates.equip(RightShoulder),"two gatlings allowed");check(!duplicates.equip(LeftHip),"third gatling rejected");check(duplicates.equip(LeftShoulder),"replacing existing same weapon does not consume copy");
    duplicates.weapon=int(Weapon::Rocket);for(int m=2;m<6;++m)check(duplicates.equip(m),"four rockets allowed");
    check(armorLamp(100,4).y==1&&armorLamp(90,4).x==1&&armorLamp(84,4).y<.02f&&armorLamp(80,4).x<.01f,"last lamp green yellow red then off");
    check(armorLamp(20,0).y==1&&armorLamp(0,0).x<.01f,"first lamp is last to drain");
    Frontend failure;failure.screen=Screen::Playing;World dead;dead.missionStatus=MissionStatus::Ejecting;
    check(!failure.handleFailure(dead,Input{}),"singleplayer ejection completes before menu return");dead.missionStatus=MissionStatus::Failed;Input heldTrigger;heldTrigger.fire[RightHand]=true;
    check(failure.handleFailure(dead,heldTrigger)&&failure.screen==Screen::Missions,"singleplayer failure automatically selects missions");failure.update(heldTrigger,.02f,false,false);check(failure.screen==Screen::Missions,"held trigger cannot skip failure return screen");
    World stalled;stalled.missionStatus=MissionStatus::Ejecting;stalled.lifeSpent=true;Input unfocusedInput;unfocusedInput.focused=false;unfocusedInput.headValid=false;
    for(int i=0;i<14;++i)stalled.step(unfocusedInput,.5f);check(stalled.missionStatus==MissionStatus::Failed,"slow frames and lost tracking cannot strand ejection");
    failure.screen=Screen::Playing;failure.cooperative=true;check(!failure.handleFailure(dead,Input{}),"individual coop death waits for authoritative team result");
    Frontend promoted;promoted.cooperative=true;promoted.screen=Screen::Standby;promoted.setLobbyHost(true);check(promoted.screen==Screen::Missions&&promoted.host,"promoted host immediately leaves standby for selection");
    promoted.screen=Screen::Standby;promoted.missionChosen=true;promoted.setLobbyHost(true);check(promoted.screen==Screen::Briefing,"host never waits for another host on selected mission");
    promoted.screen=Screen::Playing;promoted.setLobbyHost(true);check(promoted.screen==Screen::Playing,"active host handover preserves gameplay");
    promoted.screen=Screen::Ready;promoted.setLobbyHost(true);check(promoted.screen==Screen::Ready,"host handover preserves loadout readiness screen");
    promoted.connectionLost();check(promoted.screen==Screen::Disconnected&&!promoted.missionChosen,"lost connection has explicit recovery screen instead of host standby");
    promoted.update(Input{},.02f,true,false);check(promoted.screen==Screen::Disconnected&&promoted.action==MenuAction::None,"disconnected confirmation cannot launch stale mission");
    promoted.update(Input{},.02f,false,true);check(promoted.action==MenuAction::Leave,"disconnected player can leave with menu button");
    Frontend chooser;chooser.screen=Screen::Campaign;chooser.pendingAction=MenuAction::Singleplayer;chooser.campaign.completed=5;chooser.campaign.selectedMission=3;auto campaignLoadout=chooser.campaign.loadout;
    Input left;left.steer=-1;chooser.update(left,.02f,false,false);check(chooser.option==2,"singleplayer chooser wraps to third skirmish option");
    chooser.update(Input{},.02f,false,false);chooser.update(Input{},.02f,true,false);
    check(chooser.action==MenuAction::Skirmish&&chooser.screen==Screen::Skirmish&&chooser.skirmish&&!chooser.cooperative&&chooser.game==0,"choosing skirmish requests native setup without third campaign index");
    check(chooser.campaign.completed==5&&chooser.campaign.selectedMission==3&&chooser.campaign.loadout==campaignLoadout,"skirmish selection does not mutate campaign state");
    for(auto entry:{MenuAction::Load,MenuAction::Host}){
        Frontend campaigns;campaigns.screen=Screen::Campaign;campaigns.pendingAction=entry;campaigns.skirmish=true;campaigns.prototypeBriefing="OLD SKIRMISH";
        campaigns.update(left,.02f,false,false);check(campaigns.option==1&&!campaigns.skirmish,"load/host chooser contains only the two campaigns");
        campaigns.update(Input{},.02f,false,false);campaigns.update(Input{},.02f,true,false);check(campaigns.action==entry&&campaigns.game==1&&campaigns.prototypeBriefing.empty(),"campaign entry clears generated mission briefing and preserves action");
    }
    Frontend setup;setup.screen=Screen::Skirmish;setup.skirmish=true;setup.skirmishSettings.seed=42;
    auto adjust=[&](int row,int delta){setup.option=row;setup.action=MenuAction::None;setup.update(Input{},.02f,false,false);Input control;control.steer=float(delta);setup.update(control,.02f,false,false);};
    check(setup.skirmishRows().size()==skirmishRowCount&&setup.skirmishRows()[skirmishStartRow]=="START SKIRMISH","native setup exposes settings and explicit start");
    setup.update(Input{},.02f,true,false);check(setup.skirmishSettings.kind==ProceduralGoalKind::Strike,"confirm changes selected objective kind");
    setup.update(Input{},.02f,true,false);check(setup.skirmishSettings.kind==ProceduralGoalKind::Strike,"held confirm changes a setting once");
    adjust(0,1);check(setup.skirmishSettings.kind==ProceduralGoalKind::Recon&&setup.option==0,"horizontal navigation changes objective without changing row");
    adjust(0,-1);check(setup.skirmishSettings.kind==ProceduralGoalKind::Strike,"left changes objective backwards");
    adjust(1,1);check(setup.skirmishSettings.terrain=="urban","terrain cycles through native generator choices");
    adjust(2,1);check(setup.skirmishSettings.water=="none","water choice is independent of terrain");
    setup.skirmishSettings.cells=skirmishMaxCells;adjust(3,1);check(setup.skirmishSettings.cells==skirmishMaxCells,"map size clamps at native maximum");
    adjust(3,-1);check(setup.skirmishSettings.cells==skirmishMaxCells-1,"map size changes by whole blocks");
    setup.skirmishSettings.density=skirmishMinDensity;adjust(4,-1);check(setup.skirmishSettings.density==skirmishMinDensity,"scenery density clamps at native minimum");
    setup.skirmishSettings.groundPerZone=skirmishMaxGroundPerZone;adjust(5,1);check(setup.skirmishSettings.groundPerZone==skirmishMaxGroundPerZone,"ground population clamps at generator budget");
    setup.skirmishSettings.airUnits=0;adjust(6,-1);check(setup.skirmishSettings.airUnits==0,"air units can be disabled");adjust(6,1);check(setup.skirmishSettings.airUnits==1,"air population changes independently");
    adjust(7,-1);check(!setup.skirmishSettings.extraction,"extraction setting toggles");
    Input heldLeft;heldLeft.steer=-1;setup.update(heldLeft,.02f,false,false);check(!setup.skirmishSettings.extraction,"held horizontal control cannot toggle repeatedly");
    setup.option=8;setup.update(Input{},.02f,false,false);auto oldSeed=setup.skirmishSettings.seed;setup.update(Input{},.02f,true,false);auto rolledSeed=setup.skirmishSettings.seed;
    check(rolledSeed!=oldSeed,"seed row rerolls without starting a mission");setup.update(Input{},.02f,true,false);check(setup.skirmishSettings.seed==rolledSeed&&setup.action==MenuAction::None,"held select rerolls seed only once");
    Frontend sameSeed;sameSeed.screen=Screen::Skirmish;sameSeed.skirmish=true;sameSeed.option=8;sameSeed.skirmishSettings.seed=oldSeed;sameSeed.update(Input{},.02f,true,false);check(sameSeed.skirmishSettings.seed==rolledSeed,"setup reroll uses deterministic integer seed transition");
    setup.option=0;setup.update(Input{},.02f,false,false);Input down;down.forward=-1;setup.update(down,.02f,false,false);check(setup.option==1,"vertical stick selects next setup row");setup.update(down,.02f,false,false);check(setup.option==1,"held setup navigation is consumed");
    adjust(9,1);check(setup.skirmishSettings.environment.lighting==Lighting::Day,"lighting row selects day");
    adjust(10,1);check(setup.skirmishSettings.environment.weather==Weather::Fog,"weather row selects fog");
    adjust(11,1);check(setup.skirmishSettings.reinforcementsPerZone==4,"reserve row changes finite quota");
    adjust(12,-1);check(setup.skirmishSettings.reinforcementInterval==15,"reinforcement interval changes independently");
    setup.option=skirmishStartRow;setup.update(Input{},.02f,false,false);setup.update(Input{},.02f,true,false);check(setup.action==MenuAction::GenerateSkirmish&&setup.screen==Screen::Skirmish,"start requests native generation before briefing");
    setup.action=MenuAction::None;setup.update(Input{},.02f,true,false);check(setup.action==MenuAction::None,"held start cannot request another generation");
    Frontend inactive;inactive.screen=Screen::Skirmish;inactive.skirmish=true;inactive.option=skirmishStartRow;Input noFocus;noFocus.focused=false;noFocus.steer=1;noFocus.fire[RightHand]=true;
    inactive.update(noFocus,.02f,true,false);check(inactive.action==MenuAction::None&&inactive.option==skirmishStartRow,"unfocused skirmish setup cannot start or change settings");
    noFocus.focused=true;inactive.update(noFocus,.02f,true,false);check(inactive.action==MenuAction::None,"refocusing with held controls cannot start skirmish");
    inactive.update(Input{},.02f,false,false);inactive.update(Input{},.02f,true,false);check(inactive.action==MenuAction::GenerateSkirmish,"fresh confirmation works after focus returns");
    Frontend skirmish;skirmish.skirmish=true;skirmish.screen=Screen::Briefing;skirmish.mission=15;skirmish.campaign.completed=1;skirmish.campaign.selectedMission=7;auto savedLoadout=skirmish.campaign.loadout;
    for(int w=0;w<int(Weapon::Count);++w)check(skirmish.weaponAvailable(Weapon(w)),"skirmish unlocks every valid weapon independently of campaign");
    check(!skirmish.weaponAvailable(Weapon::Count),"skirmish still rejects invalid weapon identifiers");
    skirmish.update(Input{},.02f,true,false);check(skirmish.screen==Screen::Loadout,"skirmish briefing advances despite locked campaign mission index");
    skirmish.update(Input{},.02f,true,false);check(skirmish.screen==Screen::Loadout,"held briefing confirmation cannot deploy");
    skirmish.loadout.fill(Weapon::Empty);skirmish.weapon=int(Weapon::Rail);check(skirmish.equip(LeftShoulder)&&!skirmish.equip(RightShoulder),"skirmish retains weapon inventory limits");
    skirmish.weapon=int(Weapon::Shield);check(skirmish.equip(RightShoulder),"skirmish can equip a campaign-locked shield");skirmish.weapon=int(Weapon::Rifle);check(skirmish.equip(RightHand),"skirmish keeps ordinary mount compatibility");
    auto armed=skirmish.loadout;skirmish.restoreCampaign();check(skirmish.loadout==armed&&skirmish.mission==15,"campaign restore cannot replace active skirmish equipment");
    skirmish.update(Input{},.02f,false,false);check(skirmish.update(Input{},.02f,true,false)&&skirmish.screen==Screen::Playing,"valid skirmish loadout deploys");
    check(skirmish.campaign.completed==1&&skirmish.campaign.selectedMission==7&&skirmish.campaign.loadout==savedLoadout,"skirmish arming leaves campaign completion selection and equipment intact");
    World victory;victory.missionStatus=MissionStatus::Complete;victory.mission=0;check(!skirmish.recordResult(victory)&&skirmish.campaign.completed==1,"skirmish cannot record campaign awards even without world prototype flag");
    skirmish.showDebrief(0,Input{});skirmish.update(Input{},.02f,true,false);check(skirmish.screen==Screen::Debrief,"held completion confirmation cannot skip skirmish debrief");
    skirmish.update(Input{},.02f,false,false);skirmish.update(Input{},.02f,true,false);check(skirmish.screen==Screen::Skirmish&&skirmish.skirmish&&skirmish.mission==15,"skirmish debrief returns setup without advancing campaign mission");
    skirmish.screen=Screen::Playing;World failed;failed.missionStatus=MissionStatus::Failed;Input fireHeld;fireHeld.fire[RightHand]=true;check(skirmish.handleFailure(failed,fireHeld)&&skirmish.screen==Screen::Skirmish,"skirmish failure returns setup");
    skirmish.action=MenuAction::None;skirmish.update(fireHeld,.02f,false,false);check(skirmish.action==MenuAction::None,"held fire cannot immediately regenerate failed skirmish");
    skirmish.screen=Screen::Loadout;skirmish.update(Input{},.02f,false,false);skirmish.update(Input{},.02f,false,true);check(skirmish.screen==Screen::Briefing,"skirmish loadout back returns briefing");
    skirmish.update(Input{},.02f,false,false);skirmish.update(Input{},.02f,false,true);check(skirmish.screen==Screen::Skirmish&&skirmish.skirmish,"skirmish briefing back returns editable setup");
    skirmish.update(Input{},.02f,false,true);check(skirmish.screen==Screen::Skirmish,"held back cannot skip setup");
    skirmish.update(Input{},.02f,false,false);skirmish.update(Input{},.02f,false,true);check(skirmish.screen==Screen::Campaign&&skirmish.option==2&&!skirmish.skirmish,"setup back returns singleplayer chooser and clears mode");
    for(int row:{1,2,8}){Frontend paused;paused.skirmish=true;paused.screen=Screen::Pause;paused.option=row;paused.update(Input{},.02f,true,false);check(paused.action==(row==1?MenuAction::Restart:row==2?MenuAction::Abort:MenuAction::Leave),"skirmish pause retains restart abort and leave controls");}
    Frontend title;title.skirmish=true;title.prototypeBriefing="STALE";title.screen=Screen::Title;title.update(Input{},.02f,false,false);check(!title.skirmish&&title.prototypeBriefing.empty(),"returning to title clears skirmish briefing and mode");
    std::cout<<count<<" frontend checks passed\n";
}
