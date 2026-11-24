#include "frontend.hpp"
#include <algorithm>
namespace iron {
const WeaponInfo& weaponInfo(Weapon weapon,int game) {
    // IS1 $A40E uses 16.16 intervals; IS2 $10694 uses 8.8 intervals.
    // Ballistic payload raw damage converts to native health by division by ten.
    static constexpr std::array<WeaponInfo,size_t(Weapon::Count)> info{{
        {"REMOVE WEAPON","EMPTY",63,0,.45f,0,7,1},
        {"CASSY ASSAULT RIFLE","RIFLE",3,400,.125f,110,2,3},
        {"WOLFPACK ROCKET LAUNCHER","ROCKET",60,12,0,100,20.1f,1},
        {"PARKER CHAIN CUTTER","CUTTER",3,0,0,0,1.8f},
        {"RACHELS GATLING GUN","GATLING",60,600,4369.f/65536,100,2.5f},
        {"BADGER GRENADE RACK","GRENADE",48,8,0,19.2f,50,1},
        {"CHARLOTTE RAIL CANNON","RAIL",12,40,1,220,20,1},
        {"SLATE HEAVY SHIELD","SHIELD",12,0,0,0,0,1},
        {"SABLE CRUISE MISSILE","CRUISE",12,1,0,0,1000,1},
        {"SHERRY 00 GAUGE SHOTGUN","SHOTGUN",3,50,200.f/256,95,8,1},
        {"FERRET HOMING MISSILES","HOMING",12,6,0,50,40.1f,1},
        {"CARLISLE 75MM MACHINE GUN","HEAVY MG",3,400,42.f/256,150,4},
        {"CORINNE AUTO GRENADE LAUNCHER","AUTO GREN",48,100,51.f/256,70,50,5}}};
    static const auto is2=[](){auto values=info;values[int(Weapon::Rifle)].cooldown=36.f/256;values[int(Weapon::Gatling)].cooldown=23.f/256;values[int(Weapon::Rail)].cooldown=2;values[int(Weapon::Grenade)].cooldown=128.f/256;return values;}();
    int i=int(weapon);return (game?is2:info)[i>=0&&i<int(Weapon::Count)?i:0];
}
bool compatible(Weapon w,int mount){return mount>=0&&mount<MountCount&&(weaponInfo(w).mounts&(1u<<mount));}
const char* weaponModel(Weapon w){
    static const char* names[]={"question-mark","rifle","rocket","cutter","gatling","grenade","rail","shield","cruise","shotgun","homing","heavy-machinegun","auto-grenade"};
    int n=int(w);return names[n>=0&&n<int(Weapon::Count)?n:0];
}
std::array<std::string,skirmishRowCount> Frontend::skirmishRows() const {
    const char* goals[]={"ELIMINATE","STRIKE","RECON"};
    return {std::string("OBJECTIVE: ")+goals[std::clamp(int(skirmishSettings.kind),0,2)],
        "TERRAIN: "+skirmishSettings.terrain,"WATER: "+skirmishSettings.water,
        "MAP: "+std::to_string(skirmishSettings.cells)+" X "+std::to_string(skirmishSettings.cells)+" BLOCKS",
        "SCENERY DENSITY: "+std::to_string(skirmishSettings.density),
        "GROUND / ZONE: "+std::to_string(skirmishSettings.groundPerZone),
        "AIR UNITS: "+std::to_string(skirmishSettings.airUnits),
        std::string("EXTRACTION: ")+(skirmishSettings.extraction?"YES":"NO"),
        "SEED: "+std::to_string(skirmishSettings.seed)+" / REROLL",
        "LIGHTING: "+(skirmishSettings.environmentCycle?std::string("SERVER CYCLE"):std::string(lightingNames.at(size_t(skirmishSettings.environment.lighting)))),
        "WEATHER: "+(skirmishSettings.environmentCycle?std::string("MAP / SERVER CYCLE"):std::string(weatherNames.at(size_t(restrictEnvironment(skirmishSettings.environment,skirmishClimate(skirmishSettings)).weather)))),
        "RESERVES / ZONE: "+std::to_string(skirmishSettings.reinforcementsPerZone),
        "REINFORCEMENT DELAY: "+std::to_string(int(skirmishSettings.reinforcementInterval))+" S","START SKIRMISH","BACK"};
}
void World::configure(const Loadout& selected,const ArmRig& rig) {
    armRig=rig;arms={};
    weapons=selected;homingTarget=-1;ammoRemainder.fill(0);cooldown.fill(0);shots.fill(0);burstCount.fill(0);wasFire.fill(true);projectiles.clear();effects.clear();missile={};resetTracking();
    for(int i=0;i<MountCount;++i)ammo[i]=weaponInfo(weapons[i]).capacity;
}
bool Frontend::equip(int mount,bool remove) {
    Weapon selected=remove?Weapon::Empty:Weapon(weapon);
    if(!weaponAvailable(selected)){message="WEAPON LOCKED - COMPLETE ITS REWARD MISSION";return false;}
    if(!compatible(selected,mount)){message="WEAPON DOES NOT FIT THIS MOUNT";return false;}
    if(mount>=LeftHip){
        int other=mount==LeftHip?RightHip:LeftHip;
        if(selected==Weapon::Empty&&loadout[other]==Weapon::AutoGrenade)loadout[other]=Weapon::Empty;
        if(selected!=Weapon::Empty&&loadout[other]==Weapon::AutoGrenade){message="REMOVE AUTO GRENADE LAUNCHER - BOTH HIPS REQUIRED";return false;}
        if(selected==Weapon::AutoGrenade&&loadout[other]!=Weapon::Empty){message="FREE BOTH HIPS FOR AUTO GRENADE LAUNCHER";return false;}
    }
    unsigned copies=0;for(int m=0;m<MountCount;++m)if(m!=mount&&loadout[m]==selected)++copies;
    if(selected!=Weapon::Empty&&copies>=weaponLimit(selected)){message="ALL AVAILABLE COPIES OF THIS WEAPON ARE MOUNTED";return false;}
    loadout[mount]=selected;
    static const char* mounts[]={"LEFT HAND","RIGHT HAND","LEFT SHOULDER","RIGHT SHOULDER","LEFT HIP","RIGHT HIP"};
    message=std::string(mounts[mount])+": "+weaponInfo(selected).shortName;return true;
}
bool Frontend::update(const Input& in,float dt,bool confirm,bool back) {
    if(!std::isfinite(dt)||dt<0)dt=0;
    dt=std::min(dt,.1f);
    clock+=dt;
    rotation+=wrap(-mission*pi/2-rotation)*std::min(1.f,dt*10);
    bool nav=std::abs(in.steer)>.55f||std::abs(in.forward)>.55f;
    bool accept=confirm&&!confirmHeld,cancel=back&&!backHeld;
    auto previous=held;held=in.fire;confirmHeld=confirm;backHeld=back;
    if(screen==Screen::Title||screen==Screen::Campaign||screen==Screen::Cooperative||screen==Screen::Join){skirmish=false;prototypeBriefing.clear();}
    if(!in.focused||!in.headValid){navHeld=nav;return false;}
    if(screen==Screen::Playing){navHeld=nav;return false;}
    bool select=accept||(in.fire[RightHand]&&!previous[RightHand]);
    if(screen==Screen::Debrief){navHeld=nav;if(select||cancel)acceptResult();return false;}
    if(screen==Screen::Campaign){
        int count=pendingAction==MenuAction::Singleplayer?3:2;option=std::clamp(option,0,count-1);
        if(nav&&!navHeld)option=(option+(in.forward>0||in.steer<0?-1:1)+count)%count;
        navHeld=nav;
        if(cancel){skirmish=false;prototypeBriefing.clear();screen=pendingAction==MenuAction::Host?Screen::Cooperative:Screen::Title;option=0;}
        else if(select){
            if(pendingAction==MenuAction::Singleplayer&&option==2){skirmish=true;cooperative=host=false;action=MenuAction::Skirmish;screen=Screen::Skirmish;option=0;}
            else {skirmish=false;prototypeBriefing.clear();game=option;action=pendingAction;option=0;}
        }
        return false;
    }
    if(screen==Screen::Skirmish){
        skirmish=true;cooperative=host=false;option=std::clamp(option,0,skirmishBackRow);
        if(cancel||(select&&option==skirmishBackRow)){skirmish=false;prototypeBriefing.clear();screen=Screen::Campaign;pendingAction=MenuAction::Singleplayer;option=2;navHeld=nav;return false;}
        bool horizontal=std::abs(in.steer)>.55f&&std::abs(in.steer)>=std::abs(in.forward);
        bool change=(nav&&!navHeld&&horizontal)||(select&&option<skirmishStartRow);
        if(nav&&!navHeld&&!horizontal)option=(option+(in.forward>0?-1:1)+skirmishRowCount)%skirmishRowCount;
        int delta=horizontal&&in.steer<0?-1:1;
        if(change){
            auto cycle=[&](std::string& value,const auto& choices){auto it=std::find(choices.begin(),choices.end(),value);int current=it==choices.end()?0:int(it-choices.begin());value=choices[size_t((current+delta+int(choices.size()))%int(choices.size()))];};
            if(option==0)skirmishSettings.kind=ProceduralGoalKind((std::clamp(int(skirmishSettings.kind),0,2)+delta+3)%3);
            else if(option==1)cycle(skirmishSettings.terrain,skirmishTerrainOptions);
            else if(option==2)cycle(skirmishSettings.water,skirmishWaterOptions);
            else if(option==3)skirmishSettings.cells=std::clamp(skirmishSettings.cells+delta,skirmishMinCells,skirmishMaxCells);
            else if(option==4)skirmishSettings.density=std::clamp(skirmishSettings.density+delta,skirmishMinDensity,skirmishMaxDensity);
            else if(option==5)skirmishSettings.groundPerZone=std::clamp(skirmishSettings.groundPerZone+delta,skirmishMinGroundPerZone,skirmishMaxGroundPerZone);
            else if(option==6)skirmishSettings.airUnits=std::clamp(skirmishSettings.airUnits+delta,skirmishMinAirUnits,skirmishMaxAirUnits);
            else if(option==7)skirmishSettings.extraction=!skirmishSettings.extraction;
            else if(option==8){uint32_t old=skirmishSettings.seed,value=old+0x9e3779b9u;value=(value^(value>>16))*0x85ebca6bu;value=(value^(value>>13))*0xc2b2ae35u;skirmishSettings.seed=value^(value>>16);if(skirmishSettings.seed==old)++skirmishSettings.seed;}
            if(option==9){skirmishSettings.environment.lighting=Lighting((int(skirmishSettings.environment.lighting)+delta+3)%3);skirmishSettings.environmentCycle=skirmishSettings.environment.lighting==Lighting::Authored;}
            if(option==10){skirmishSettings.environmentCycle=false;do{skirmishSettings.environment.weather=Weather((int(skirmishSettings.environment.weather)+delta+5)%5);}while(!weatherAllowed(skirmishClimate(skirmishSettings),skirmishSettings.environment.weather));}
            if(option==11)skirmishSettings.reinforcementsPerZone=std::clamp(skirmishSettings.reinforcementsPerZone+delta,0,12);
            if(option==12)skirmishSettings.reinforcementInterval=std::clamp(skirmishSettings.reinforcementInterval+delta*5,5.f,120.f);
        }
        if(select&&option==skirmishStartRow)action=MenuAction::GenerateSkirmish;
        navHeld=nav;
        return false;
    }
    if(screen==Screen::Title||screen==Screen::Cooperative||screen==Screen::Pause){
        int count=screen==Screen::Title?4:screen==Screen::Cooperative?3:9;
        bool volume=screen==Screen::Pause&&(option==5||option==6)&&std::abs(in.steer)>.55f&&std::abs(in.steer)>=std::abs(in.forward);
        if(nav&&!navHeld){if(volume){float& value=option==5?soundVolume:musicVolume;value=std::clamp((std::round(value*20)+(in.steer>0?1:-1))/20.f,0.f,1.f);}else option=(option+(in.forward>0||in.steer<0?-1:1)+count)%count;}
        navHeld=nav;
        if(cancel){if(screen==Screen::Pause)screen=Screen::Playing;else screen=Screen::Title;option=0;return false;}
        if(select){
            if(screen==Screen::Title){skirmish=false;prototypeBriefing.clear();if(option==0){pendingAction=MenuAction::Singleplayer;screen=Screen::Campaign;option=game;}else if(option==1){screen=Screen::Cooperative;option=0;}else if(option==2){pendingAction=MenuAction::Load;screen=Screen::Campaign;option=game;}else if(option==3)action=MenuAction::Exit;}
            else if(screen==Screen::Cooperative){skirmish=false;prototypeBriefing.clear();if(option==0){pendingAction=MenuAction::Host;screen=Screen::Campaign;}else if(option==1){screen=Screen::Join;codeCursor=0;}else screen=Screen::Title;option=0;}
            else {if(option==0)screen=Screen::Playing;else if(option==1||option==2){if(!cooperative||host)action=option==1?MenuAction::Restart:MenuAction::Abort;else networkMessage="ONLY HOST CAN RESTART OR ABORT";}else if(option==3)action=MenuAction::Destroy;else if(option==4)micEnabled=!micEnabled;else if(option==7)action=MenuAction::Recenter;else if(option==8)action=MenuAction::Leave;}
        }return false;
    }
    if(screen==Screen::Join){
        constexpr std::string_view alphabet="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        if(nav&&!navHeld){if(std::abs(in.steer)>.55f)codeCursor=(codeCursor+(in.steer>0?1:5))%6;else{auto index=alphabet.find(joinCode[codeCursor]);joinCode[codeCursor]=alphabet[(index+(in.forward>0?1:31))%32];}}
        navHeld=nav;if(cancel)screen=Screen::Cooperative;else if(select)action=MenuAction::Join;return false;
    }
    if(screen==Screen::Connecting||screen==Screen::Disconnected||screen==Screen::Standby||screen==Screen::Ready){navHeld=nav;if(cancel)action=MenuAction::Leave;else if(screen==Screen::Standby&&missionChosen&&select)screen=Screen::Briefing;else if(screen==Screen::Ready&&select){ready=false;action=MenuAction::Ready;screen=Screen::Loadout;}return false;}

    if(cancel&&screen==Screen::Missions&&generatedMissions()){navHeld=nav;return false;}
    if(cancel&&screen==Screen::Missions){if(cooperative)action=MenuAction::Leave;else{screen=Screen::Title;option=0;}navHeld=nav;return false;}
    if(cancel){screen=screen==Screen::Loadout?Screen::Briefing:skirmish?Screen::Skirmish:cooperative&&!host?Screen::Standby:Screen::Missions;if(skirmish&&screen==Screen::Skirmish)option=skirmishStartRow;navHeld=nav;return false;}
    if(nav&&!navHeld) {
        int verticalStep=screen==Screen::Missions&&generatedMissions()?1:2;
        int delta=std::abs(in.steer)>.55f?(in.steer>0?1:-1):(in.forward>0?-verticalStep:verticalStep);
        if(screen==Screen::Missions)mission=(mission+delta+missionCount())%missionCount();
        if(screen==Screen::Loadout)weapon=(weapon+delta+weaponCount())%weaponCount();
    }
    navHeld=nav;
    if(screen==Screen::Missions && (accept||(in.fire[RightHand]&&!previous[RightHand]))){if(missionAvailable(mission)){screen=Screen::Briefing;message=generatedMissions()?"MENU: EQUIPMENT / R CLICK: MISSIONS":"SELECT WEAPON THEN PRESS ITS MOUNT BUTTON";}else message=generatedMissions()?"MISSION CLEARED - SELECT ANOTHER":"MISSION LOCKED - COMPLETE THE PREVIOUS GROUP";}
    else if(screen==Screen::Briefing && (accept||(in.fire[RightHand]&&!previous[RightHand]))){if(missionAvailable(mission))screen=Screen::Loadout;else screen=Screen::Missions;}
    else if(screen==Screen::Loadout && !generatedMissions()) {
        for(int m=0;m<MountCount;++m)if(in.fire[m]&&!previous[m])equip(m,in.ping[0]||in.ping[1]);
        if(accept){
            if(!missionAvailable(mission)){message="MISSION LOCKED";return false;}
            if((loadout[LeftHip]==Weapon::AutoGrenade&&loadout[RightHip]!=Weapon::Empty)||(loadout[RightHip]==Weapon::AutoGrenade&&loadout[LeftHip]!=Weapon::Empty)){message="AUTO GRENADE LAUNCHER REQUIRES BOTH HIPS";return false;}
            for(int m=0;m<MountCount;++m)if(!weaponAvailable(loadout[m])||!compatible(loadout[m],m)||std::count(loadout.begin(),loadout.end(),loadout[m])>weaponLimit(loadout[m])){message="REMOVE LOCKED OR INVALID EQUIPMENT";return false;}
            if(cooperative){ready=true;action=MenuAction::Ready;screen=Screen::Ready;return false;}
            resultMessage.clear();screen=Screen::Playing;return true;
        }
    }
    return false;
}
void Frontend::restoreCampaign(){if(skirmish)return;mission=campaign.selectedMission;loadout=campaign.loadout;rotation=-mission*pi/2;}
void Frontend::setLobbyHost(bool value){host=value;if(cooperative&&host&&screen==Screen::Standby)screen=missionChosen?Screen::Briefing:Screen::Missions;}
void Frontend::connectionLost(){screen=Screen::Disconnected;missionChosen=false;spectating=false;ready=false;}
bool Frontend::recordResult(const World& w){
    if(skirmish||sandbox||w.missionPrototype||w.missionStatus!=MissionStatus::Complete||w.lifeSpent||w.health<=0)return false;
    unsigned before=campaign.rules.available(campaign.completed);
    if(!campaign.complete(w.mission))return false;
    int reward=campaign.rules.rewards[w.mission];
    resultMessage=campaign.finished()?"CAMPAIGN COMPLETE":reward>=0?std::string(weaponInfo(Weapon(reward)).shortName)+" UNLOCKED":"MISSION RECORDED";
    if(before!=campaign.rules.available(campaign.completed))resultMessage+=" / NEW MISSIONS";
    campaign.selectedMission=campaign.nextMission();campaign.loadout=loadout;return true;
}
bool Frontend::handleFailure(const World& world,const Input& input){
    if(cooperative||screen!=Screen::Playing||world.missionStatus!=MissionStatus::Failed)return false;
    screen=skirmish?Screen::Skirmish:Screen::Missions;if(skirmish)option=skirmishStartRow;resultMessage="MISSION FAILED";ready=false;spectating=false;missionChosen=false;
    held=input.fire;confirmHeld=true;backHeld=true;navHeld=true;return true;
}
void Frontend::showDebrief(int completedMission,const Input& input){
    resultMission=completedMission;screen=Screen::Debrief;ready=false;
    held=input.fire;confirmHeld=true;backHeld=true;navHeld=true;
}
void Frontend::acceptResult(){
    if(skirmish){screen=Screen::Skirmish;option=skirmishStartRow;}
    else {mission=sandbox?(mission+1)%campaign.rules.count():campaign.nextMission();screen=cooperative&&!host?Screen::Standby:Screen::Missions;}
    missionChosen=false;confirmHeld=true;backHeld=true;navHeld=true;
}
}
