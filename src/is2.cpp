#include "simulation.hpp"
#include <algorithm>
namespace iron {
namespace {
bool region(const Target& t,const std::vector<std::array<float,4>>& regions){
    for(auto r:regions)if(t.origin.x>=r[0]&&-t.origin.z>=r[1]&&t.origin.x<=r[2]&&-t.origin.z<=r[3])return true;
    return false;
}
}
void World::initIS2(){
    counters={};actorDeaths.fill(0);objectiveStage=0;
    static const int initial[]={0,5,0,0,3,0,0,4,3,10,0,4,0,0,0,0,19,0,3,0};
    counters[0]=initial[mission];
    for(size_t i=0;i<targets.size();++i){auto& t=targets[i];if(t.spawnType>=0)continue;
        if(mission==0&&region(t,objectiveRegions))++counters[2];
        if((mission==2||mission==12)&&t.tag==22)++counters[0];
        if(mission==7&&t.tag==33)++counters[1];
        if(mission==13&&t.tag==32)++counters[0];
        if((mission==14||mission==17)&&t.tag<128)++counters[0];
        if((mission==3&&t.tag>=32&&t.tag<=36)||((mission==6||mission==13)&&t.tag==32))t.protectedTarget=true;
    }
    // $2741C moves target slot 1 to the next surviving protected structure.
    if(mission==3||mission==6||mission==13){int selected=-1;for(size_t i=0;i<targets.size();++i)if(targets[i].protectedTarget&&targets[i].spawnType<0&&(selected<0||targets[i].tag<targets[selected].tag))selected=int(i);targetSlots[1]=selected;}
    static const char* goals[]={"DESTROY DESERT FACTORIES","DESTROY ISLAND BUILDINGS","COLLECT SUPPLIES","PROTECT SKYSCRAPERS / 3 SATYRS","ESCORT TRUCKS","STOP 9 CAR BOMBS / MAX 3 ESCAPES","PROTECT CITY / DESTROY SHIPS","AIRBASE + 5 FIGHTERS","ESCORT AT LEAST 2 TRUCKS","STOP ALL 10 TRUCKS","STOP AIR DROP / CLEAR GROUND FORCES","DESTROY 4 POWER PLANTS","COLLECT WAREHOUSE SUPPLIES","PROTECT UR / DESTROY 2 MECHS","DESTROY MILITARY BASE","DESTROY 6 SHIPS / 12 MIN","FORTRESS DEFENSES + SUPPLIES","DESTROY ISLAND FACTORIES","ESCORT AT LEAST 3 TRUCKS","DESTROY COLD IRON GUNNER"};
    objective=goals[mission];
}
void World::destroyedIS2(Target& t){
    if(t.spawnType>=0){
        if(t.spawnType<int(actorDeaths.size()))++actorDeaths[t.spawnType];
        if(t.spawnType==8)--counters[0];
        if(t.spawnType==17)++counters[1];
        if(t.spawnType==9)missionFlags|=1u<<t.deathFlag;
        return;
    }
    applyMissionAction(t.action);
    if(mission==0&&region(t,objectiveRegions))--counters[2];
    if(mission==1&&t.origin.x>=238.08f&&t.origin.x<=273.92f&&-t.origin.z>=222.72f&&-t.origin.z<=309.76f)--counters[0];
    if((mission==14||mission==17)&&t.tag<128)--counters[0];
    if(t.tag<32)pickups.push_back({{t.origin.x,1.5f,t.origin.z},t.tag});
    if(t.protectedTarget){
        int replacement=-1;
        for(size_t i=0;i<targets.size();++i){auto& candidate=targets[i];if(&candidate!=&t&&candidate.protectedTarget&&candidate.spawnType<0&&!candidate.processed&&candidate.health>0&&(replacement<0||candidate.tag<targets[replacement].tag)){replacement=int(i);}}
        targetSlots[1]=replacement;
        if(replacement<0)missionFlags|=1u<<24;
    }
}
void World::arrivedEnemy(Target& t){
    if(assault){if(t.spawnType==17){explode(t.center,10,40,true);t.health=0;}return;}
    if(t.spawnType==8){
        // Route terminal callbacks $82E6 (IS1) / $D28C (IS2).
        if((!game&&mission==6)||(game&&mission==9))missionFlags|=1u<<30;
        if(t.protectedTarget){
            bool all=true;for(auto& other:targets)if(&other!=&t&&other.spawnType==8&&other.health>0&&!other.escaped&&!(other.spawnFlags&4))all=false;
            if(all)missionFlags|=1u<<30;
        }
    }
    if(game&&t.spawnType==17){++counters[0];destruction(t.center,12,Weapon::Empty);sounds.push_back({SoundEvent::Explosion,0,t.center});}
}
void World::dropCargo(size_t carrier){
    for(auto& f:cargoFlights)if(f.carrier==carrier&&!f.choices.empty()){
        randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;
        Target payload=f.choices[randomState%f.choices.size()];auto slot=std::find_if(f.slots.begin(),f.slots.end(),[&](size_t i){return targets[i].mesh==payload.mesh&&(!targets[i].active||(targets[i].health<=0&&targets[i].processed));});if(slot==f.slots.end())return;
        auto& plane=targets[carrier];payload.active=true;payload.processed=false;payload.escaped=false;payload.ai.registered=false;payload.ai.airborne=true;payload.ai.velocity={};payload.origin=plane.origin-direction(plane.yaw,0)*4;payload.ai.spawn={payload.origin.x,0,payload.origin.z};payload.yaw=plane.yaw;updateEnemyBounds(payload);targets[*slot]=std::move(payload);return;
    }
}
void World::evaluateIS2(){
    if(missionStatus!=MissionStatus::Active)return;
    auto remaining=[&](int type){unsigned n=0;for(auto& t:targets)if(t.spawnType==type&&t.ai.parent<0&&t.health>0&&!t.processed&&!t.escaped&&(t.active||(!t.generated&&!(t.spawnFlags&6))))++n;for(auto& g:generators)if(g.prototype.spawnType==type)n+=g.remaining;return n;};
    if(mission==10){std::array<unsigned,3> live{};for(auto& t:targets)if(t.active&&t.health>0&&!t.escaped&&!t.ai.airborne&&t.ai.cargoGroup>=0&&t.ai.cargoGroup<3)++live[t.ai.cargoGroup];for(unsigned i=0;i<3;++i){missionFlags&=~(1u<<i);if(live[i]>=6)missionFlags|=1u<<i;}}
    bool success=false,fail=false;bool arrived=missionFlags&(1u<<30);
    switch(mission){
    case 0:{if(missionBuildingCount>0){int stage=(7^(std::max(0,counters[2])*8/(missionBuildingCount+1)))-1;if(stage>=0&&stage<7)missionFlags|=1u<<stage;}success=counters[2]==0;break;}
    case 1:case 2:case 11:case 12:case 14:success=counters[0]==0;break;
    case 3:fail=missionFlags&(1u<<24);success=actorDeaths[14]>=3;break;
    case 4:case 8:case 18:fail=counters[0]<=0;success=arrived;
        if(mission==18&&actorDeaths[14]>0)for(unsigned n=0;n<std::min(8u,actorDeaths[14]);++n)missionFlags|=1u<<n;
        break;
    case 5:fail=counters[0]>=4;success=counters[1]>=9;break;
    case 6:fail=missionFlags&(1u<<24);success=remaining(9)==0;break;
    case 7:success=counters[0]==0&&counters[1]==0&&actorDeaths[18]>=5;break;
    case 9:fail=arrived;success=counters[0]==0;break;
    case 10:success=elapsed>=300&&remaining(11)==0&&remaining(0)==0&&remaining(14)==0;break;
    case 13:fail=counters[0]<=0;success=actorDeaths[3]>=2;break;
    case 15:fail=elapsed>720;success=actorDeaths[9]>=6;break;
    case 16:case 17:success=counters[0]<=0;break;
    case 19:success=actorDeaths[19]>0&&remaining(19)==0;break;
    }
    if(fail)missionStatus=MissionStatus::Failed;else if(success&&!destructionPending())missionStatus=MissionStatus::Complete;
}
}
