#include "assault.hpp"
#include "coop.hpp"
#include <stdexcept>
namespace iron {
namespace {
bool objectiveTarget(const World& w,const Target& t){
    if(t.spawnType>=0)return false;
    if(!w.game&&w.mission==2)return t.tag==32;
    if(!w.game&&w.mission==10)return t.origin.x>=14.08f&&t.origin.x<=290.56f&&t.origin.z>=-71.68f&&t.origin.z<=102.4f;
    if(w.game&&(w.mission==14||w.mission==17))return t.tag<128;
    if(w.game&&w.mission==1)return t.origin.x>=238.08f&&t.origin.x<=273.92f&&-t.origin.z>=222.72f&&-t.origin.z<=309.76f;
    if(w.game&&w.mission==11)return t.action==0x80020000u;
    if(w.game&&w.mission==0)for(auto r:w.objectiveRegions)if(t.origin.x>=r[0]&&-t.origin.z>=r[1]&&t.origin.x<=r[2]&&-t.origin.z<=r[3])return true;
    return false;
}
const AssaultUnitType& unitType(int type){
    for(const auto& entry:assaultUnitRoster)if(entry.type==type)return entry;
    throw std::invalid_argument("Not an independently deployable IS2 unit type");
}
void validate(const AssaultRules& r){
    for(const auto* roster:{&r.attackerUnits,&r.defenderUnits}){
        if(roster->size()>32)throw std::invalid_argument("At most 32 support units per team");
        for(int type:*roster)unitType(type);
    }
    if(!std::isfinite(r.attackerDelay)||!std::isfinite(r.defenderDelay)||!std::isfinite(r.roundSeconds)||r.attackerDelay<7||r.defenderDelay<=r.attackerDelay||r.roundSeconds<=0||r.attackerRespawns>1000||
       r.attackerAir>16||r.attackerGround>16||r.defenderTurrets>16||r.defenderGround>16||r.attackerAir<=r.attackerGround||r.defenderTurrets+r.defenderGround==0)
        throw std::invalid_argument("Invalid assault rules (delays must allow ejection; defenders wait longer; attackers favor air; support counts <=16)");
}
}
void AssaultMatch::load(const std::filesystem::path& retail,unsigned scenario,const AssaultRules& settings){
    validate(settings);if(scenario>=assaultScenarios.size())throw std::invalid_argument("Unknown assault scenario");
    auto spec=assaultScenarios[scenario];World loaded;loaded.difficulty=World::Difficulty::Hard;loaded.loadMission(retail,spec.mission,spec.game);
    // Always import support from IS2, including when objectives come from IS1.
    std::array<bool,23> found{};
    for(int m=0;m<20;++m){World source;source.difficulty=loaded.difficulty;source.loadMission(retail,m,1);
        for(const auto& t:source.targets)if(t.spawnType>=0&&t.spawnType<23&&t.spawnType!=4&&t.spawnType!=21&&!found[t.spawnType]&&t.mesh&&!t.mesh->empty()){
            Target copy=t;
            if(!spec.game)for(auto mesh:{&copy.mesh,&copy.wreck})if(*mesh){*mesh=std::make_shared<WorldMesh>(**mesh);for(auto& v:**mesh)if(v.textured)v.v=(158.f+v.v*451.f)/609.f;}
            copy.assetGame=1;copy.active=false;copy.generated=true;loaded.targets.push_back(std::move(copy));found[t.spawnType]=true;
        }
    }
    prepare(std::move(loaded),settings);
}
void AssaultMatch::links(size_t current){world.teammates.clear();for(size_t i=0;i<pilots.size();++i)world.teammates.push_back(i==current?static_cast<Pilot*>(&world):&pilots[i].pilot);}
void AssaultMatch::deploy(size_t i){
    auto& p=pilots[i];Vec3 spawn=starts[i];
    if(p.deaths){
        // Recheck scenery, supports and living pilots; retry blocked deployment later.
        world.position=spawn;world.chassisYaw=yaws[i];size_t count=world.targets.size();
        for(size_t k=0;k<8;++k)if(k!=i&&!pilots[k].pilot.lifeSpent&&pilots[k].pilot.health>0){Target body;body.center=pilots[k].pilot.position+Vec3{0,6,0};body.half={3.5f,6,3.5f};world.targets.push_back(body);}
        try{spawn=coop::spawnPositions(world,std::array<unsigned,1>{0})[0];}
        catch(...){world.targets.resize(count);throw;}
        world.targets.resize(count);
    }
    Pilot fresh;fresh.position=spawn;fresh.chassisYaw=yaws[i];fresh.playerSlot=unsigned(i);fresh.team=i<4?Team::Attackers:Team::Defenders;fresh.missionStatus=MissionStatus::Active;
    static_cast<Pilot&>(world)=fresh;
    // configure resets the range's shared effects; deployment must preserve combat.
    auto shots=std::move(world.projectiles);auto effects=std::move(world.effects);
    world.configure(p.loadout,p.pilot.armRig);world.projectiles=std::move(shots);world.effects=std::move(effects);p.pilot=world;p.input={};p.input.focused=false;p.waiting=false;p.deathHandled=false;p.respawnIn=0;
}
void AssaultMatch::prepare(World loaded,const AssaultRules& settings){
    validate(settings);bool supported=false;for(auto s:assaultScenarios)supported|=s.game==loaded.game&&s.mission==loaded.mission;
    if(!supported)throw std::invalid_argument("Assault requires a supported campaign destruction mission");
    world=std::move(loaded);rules=settings;result=AssaultResult::Playing;clock=0;respawnsRemaining=rules.attackerRespawns;
    for(auto scenario:assaultScenarios)if(scenario.game==world.game&&scenario.mission==world.mission)world.climate=scenario.climate;
    world.assault=true;world.cooperative=true;world.environment=restrictEnvironment(rules.environment,world.climate);world.difficulty=World::Difficulty::Hard;
    world.generators.clear();world.cargoFlights.clear();world.targetSlots.fill(-1);
    std::array<Target,23> templates;std::array<bool,23> found{};Vec3 defended{};unsigned goals=0;
    for(auto& t:world.targets){
        if(t.spawnType>=0){if(t.spawnType<23&&(!found[t.spawnType]||t.assetGame==1)){templates[t.spawnType]=t;found[t.spawnType]=true;}t.active=false;t.generated=true;t.escaped=true;t.ai.generator=-1;}
        else if(objectiveTarget(world,t)&&t.health>0){t.team=Team::Defenders;defended=defended+t.origin;++goals;}
    }
    if(!goals)throw std::runtime_error("Campaign has no surviving assault objectives");
    auto attackersRoster=rules.attackerUnits,defendersRoster=rules.defenderUnits;
    auto append=[](std::vector<int>& roster,unsigned count,std::initializer_list<int> types){
        std::vector<int> cycle(types);for(unsigned i=0;i<count;++i)roster.push_back(cycle[i%cycle.size()]);
    };
    if(attackersRoster.empty()){append(attackersRoster,rules.attackerGround,{0,3,11,14,17,19,6,8,15});append(attackersRoster,rules.attackerAir,{1,7,12,18});}
    if(defendersRoster.empty()){append(defendersRoster,rules.defenderGround,{11,14,19,0,3,6,8,15,17});append(defendersRoster,rules.defenderTurrets,{2,5,10});}
    for(const auto* roster:{&attackersRoster,&defendersRoster})for(int type:*roster)
        if(!found[type])throw std::runtime_error("Missing IS2 support template: "+std::to_string(type));
    Vec3 attack=world.position;float yaw=world.chassisYaw;defended=defended*(1.f/goals);
    // Use the existing clearance search for each four-pilot deployment.
    const std::array<unsigned,4> slots{0,1,2,3};
    auto attackers=coop::spawnPositions(world,slots);
    world.position=defended;world.position.y=world.surfaceHeight(defended.x,defended.z);world.chassisYaw=wrap(yaw+pi);
    size_t reservedAttackers=world.targets.size();
    for(auto p:attackers){Target body;body.center=p+Vec3{0,6,0};body.half={3.5f,6,3.5f};world.targets.push_back(body);}
    auto defenders=coop::spawnPositions(world,slots);
    world.targets.resize(reservedAttackers);
    for(size_t i=0;i<8;++i){starts[i]=i<4?attackers[i]:defenders[i-4];yaws[i]=i<4?yaw:wrap(yaw+pi);pilots[i]=AssaultPilot{};pilots[i].loadout={Weapon::Empty,Weapon::Rifle,Weapon::Rocket,Weapon::Empty,Weapon::Grenade,Weapon::Empty};deploy(i);}
    // Reserve the eight pilot bodies while searching finite support placements.
    size_t reserve=world.targets.size();for(auto p:starts){Target body;body.center=p+Vec3{0,6,0};body.half={3.5f,6,3.5f};world.targets.push_back(body);}
    auto support=[&](const std::vector<int>& roster,Team team){for(unsigned n=0;n<roster.size();++n){int type=roster[n];Target actor=templates[type];auto role=unitType(type).role;
        world.position=team==Team::Attackers?attack:defended;world.position.x+=float(int(n%4)-2)*14;world.position.z+=50+float(n/4)*14;world.position.y=world.surfaceHeight(world.position.x,world.position.z);
        Vec3 location;
        if(role==AssaultUnitRole::Naval){
            // Ships/boats require water; never silently place them on land.
            float best=1e9f;bool located=false;
            for(int z=-960;z<=960;z+=20)for(int x=-960;x<=960;x+=20)if(world.hasWater(float(x),float(z))){
                Vec3 candidate{float(x),0,float(z)};bool clear=true;
                for(const auto& t:world.targets)if(t.active&&!t.escaped&&t.health>0&&std::abs(t.center.x-x)<t.half.x+actor.half.x+4&&std::abs(t.center.z-z)<t.half.z+actor.half.z+4)clear=false;
                float d=length(candidate-world.position);if(clear&&d<best){best=d;location=candidate;located=true;}
            }
            if(!located)throw std::runtime_error("No water deployment site for selected IS2 naval unit");
        }else location=coop::spawnPositions(world,std::array<unsigned,1>{0})[0];
        actor.origin=location;if(role==AssaultUnitRole::Air)actor.origin.y+=40;
        actor.team=team;actor.active=true;actor.generated=true;actor.escaped=false;actor.processed=false;actor.protectedTarget=false;actor.trigger=0;actor.spawnFlags=0;actor.deathFlag=0;actor.action=0;actor.route.clear();actor.waypoint=0;
        if(type==20){ // Transport pads receive a map-local destination, not an old mission coordinate.
            actor.route.push_back(team==Team::Attackers?attack:defended);
        }else if(role!=AssaultUnitRole::Turret&&role!=AssaultUnitRole::Utility){
            actor.route.push_back(role==AssaultUnitRole::Naval?location:team==Team::Attackers?defended:location);
        }
        actor.tag=255;actor.ai.identity=-1;actor.ai.targetSlot=0;actor.ai.parent=-1;actor.ai.attachment=-1;actor.ai.generator=-1;actor.ai.respawnRange=0;actor.ai.registered=false;actor.ai.playerTarget=-1;actor.ai.spawn=actor.origin;actor.ai.spawnYaw=actor.yaw;actor.ai.maxHealth=actor.health;world.updateEnemyBounds(actor);world.targets.push_back(std::move(actor));}};
    support(attackersRoster,Team::Attackers);support(defendersRoster,Team::Defenders);
    world.targets.erase(world.targets.begin()+reserve,world.targets.begin()+reserve+8);world.teammates.clear();static_cast<Pilot&>(world)=pilots[0].pilot;
}
bool validAssaultLoadout(const Loadout& loadout){
    for(int mount=0;mount<MountCount;++mount){auto weapon=loadout[mount];
        if(int(weapon)<0||weapon>=Weapon::Count||!compatible(weapon,mount)||std::count(loadout.begin(),loadout.end(),weapon)>weaponLimit(weapon))return false;
    }
    return !(loadout[LeftHip]==Weapon::AutoGrenade&&loadout[RightHip]!=Weapon::Empty)&&!(loadout[RightHip]==Weapon::AutoGrenade&&loadout[LeftHip]!=Weapon::Empty);
}
void AssaultMatch::setLoadout(unsigned slot,const Loadout& loadout){
    if(slot>=pilots.size()||!validAssaultLoadout(loadout))throw std::invalid_argument("Invalid IS2 loadout or pilot slot");
    if(result!=AssaultResult::Playing||(clock>0&&!pilots[slot].waiting))throw std::invalid_argument("Choose equipment before the round or while awaiting respawn");
    pilots[slot].loadout=loadout;if(clock==0)deploy(slot);
}
void AssaultMatch::step(float dt){
    if(!std::isfinite(dt)||dt<=0||dt>.1f)throw std::invalid_argument("Assault tick must be 0 < dt <= 0.1");
    if(result!=AssaultResult::Playing)return;
    world.sounds.clear();clock+=dt;
    for(size_t i=0;i<8;++i){auto& p=pilots[i];
        if(p.waiting){p.respawnIn=std::max(0.f,p.respawnIn-dt);if(p.respawnIn==0){try{deploy(i);}catch(const std::runtime_error&){p.respawnIn=.5f;}}}
        static_cast<Pilot&>(world)=p.pilot;links(i);world.step(p.input,dt,false);p.pilot=world;
    }
    // A neutral controller keeps shared mission simulation running even during a wipe.
    static_cast<Pilot&>(world)=Pilot{};world.missionStatus=MissionStatus::Active;links();world.stepProjectiles(dt);world.stepMission(dt);
    bool attackersPresent=false;
    for(size_t i=0;i<8;++i){auto& p=pilots[i];if((p.pilot.lifeSpent||p.pilot.health<=0)&&!p.waiting&&!p.deathHandled){
        ++p.deaths;p.deathHandled=true; // includes exhausted attackers
        if(i>=4||respawnsRemaining){if(i<4)--respawnsRemaining;p.waiting=true;p.respawnIn=i<4?rules.attackerDelay:rules.defenderDelay;}
    }if(i<4)attackersPresent|=p.waiting||(!p.pilot.lifeSpent&&p.pilot.health>0);}
    if(world.missionStatus==MissionStatus::Complete)result=AssaultResult::AttackersWin;
    else if(world.missionStatus==MissionStatus::Failed||clock>=rules.roundSeconds||!attackersPresent)result=AssaultResult::DefendersWin;
    world.teammates.clear();
}
}
