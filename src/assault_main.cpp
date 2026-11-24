#include "assault.hpp"
#include "coop.hpp"
#include <iostream>
#include <fstream>
using namespace iron;
using Json=nlohmann::json;
static Json state(const AssaultMatch& match){
    Json pilots=Json::array();for(auto& p:match.pilots)pilots.push_back({{"team",int(p.pilot.team)},{"team_name",teamName(p.pilot.team)},{"colour",Json::array({teamColour(p.pilot.team).x,teamColour(p.pilot.team).y,teamColour(p.pilot.team).z})},{"pilot",coop::pilotWire(p.pilot)},{"respawn_in",p.respawnIn},{"waiting",p.waiting},{"deaths",p.deaths}});
    Json support=Json::array();for(const auto& t:match.world.targets)if(t.team!=Team::Neutral&&t.spawnType>=0)support.push_back({{"type",t.spawnType},{"team",int(t.team)},{"health",t.health},{"active",t.active&&!t.escaped},{"position",Json::array({t.origin.x,t.origin.y,t.origin.z})}});
    return {{"environment",environmentWire(match.world.environment)},{"support",support},{"result",int(match.result)},{"seconds",match.clock},{"respawns",match.respawnsRemaining},{"objective",match.world.objective},{"counters",match.world.counters},{"players",pilots}};
}
int main(int argc,char** argv){
    if(argc!=3&&argc!=4){std::cerr<<"Usage: iron-assault-prototype RETAIL_DIR SCENARIO_INDEX [CONFIG_JSON]\n";for(size_t i=0;i<assaultScenarios.size();++i)std::cerr<<i<<": "<<assaultScenarios[i].name<<'\n';return 2;}
    try {AssaultMatch match;AssaultRules rules;Json config=Json::object();
        if(argc==4){std::ifstream file(argv[3]);if(!file)throw std::runtime_error("Cannot open assault configuration");file>>config;
            if(config.contains("environment"))rules.environment=readEnvironment(config.at("environment"));
            rules.attackerUnits=config.value("attacker_units",std::vector<int>{});rules.defenderUnits=config.value("defender_units",std::vector<int>{});
        }
        match.load(argv[1],unsigned(std::stoul(argv[2])),rules);
        for(const auto& entry:config.value("loadouts",Json::array()))match.setLoadout(entry.at("slot").get<unsigned>(),entry.at("weapons").get<Loadout>());
        std::cout<<state(match)<<std::endl;
        // JSON-lines driver: inputs persist until replaced; simulation advances explicitly.
        std::string line;while(std::getline(std::cin,line)){try{auto j=Json::parse(line);auto op=j.at("op").get<std::string>();
            if(op=="input"){auto slot=j.at("slot").get<unsigned>();if(slot>=8)throw std::invalid_argument("Slot must be 0..7");auto wire=coop::inputWire(Input{});wire.update(j.at("input"));match.pilots[slot].input=coop::inputRead(wire);}
            else if(op=="loadout")match.setLoadout(j.at("slot").get<unsigned>(),j.at("weapons").get<Loadout>());
            else if(op=="catalog"){
                Json units=Json::array(),weapons=Json::array();for(const auto& u:assaultUnitRoster)units.push_back({{"type",u.type},{"name",u.name},{"role",int(u.role)}});
                for(int i=0;i<int(Weapon::Count);++i)weapons.push_back({{"id",i},{"name",weaponInfo(Weapon(i),1).name},{"mounts",weaponInfo(Weapon(i),1).mounts},{"copies",weaponLimit(Weapon(i))}});
                std::cout<<Json{{"units",units},{"weapons",weapons}}<<std::endl;continue;
            }
            else if(op=="tick"){unsigned frames=j.value("frames",1u);if(frames>36000)throw std::invalid_argument("At most 36000 ticks per command");for(unsigned i=0;i<frames;++i)match.step(1.f/60);}
            else if(op!="state")throw std::invalid_argument("Expected input, loadout, catalog, tick or state");
            std::cout<<state(match)<<std::endl;
        }catch(const std::exception& e){std::cout<<Json{{"error",e.what()}}<<std::endl;}}
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
