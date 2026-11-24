#include "simulation.hpp"
#include "skirmish.hpp"
#include <nlohmann/json.hpp>
#include <cassert>
#include <iostream>
using namespace iron;
World fixture(){World w;w.targets.clear();w.generators.clear();w.missionStatus=MissionStatus::Active;w.missionPrototype=true;w.proceduralLayout=true;w.position={-150,0,0};
 Target actor;actor.spawnType=0;actor.center={100,2,0};actor.origin={100,0,0};actor.half={2,2,2};actor.proceduralZone=0;actor.prototypeObjective=true;actor.ai.maxHealth=100;
 w.targets.push_back(actor);ProceduralObjective goal;goal.position={120,0,0};goal.targets={0};w.procedural.objectives={goal};return w;}
void wait(World& w){for(int frame=0;frame<52;++frame)w.stepGenerators(.1f);}
int main(){
 auto w=fixture();prepareProceduralReinforcements(w,3,5);assert(w.generators.size()==1&&w.targets.size()==2);auto slot=w.generators[0].slots[0];w.procedural.objectives[0].targets.push_back(slot);
 wait(w);assert(w.generators[0].remaining==3&&!w.targets[slot].active); // occupied source
 w.targets[0].health=0;w.targets[0].processed=true;w.position={100,0,0};wait(w);assert(w.generators[0].remaining==3); // nearby pilot
 w.position={-150,0,0};updateProceduralMission(w,0);assert(w.missionStatus==MissionStatus::Active&&w.objective.find("RESERVES 3")!=std::string::npos); // reserve prevents early victory
 for(unsigned remaining=2;;--remaining){wait(w);assert(w.targets[slot].active&&w.targets[slot].health==100&&w.generators[0].remaining==remaining);
  wait(w);assert(w.generators[0].remaining==remaining); // occupied pool slot cannot generate more
  w.targets[slot].health=0;w.targetDestroyed(w.targets[slot]);if(remaining==0)break;
 }
 w.effects.clear();updateProceduralMission(w,0);assert(w.missionStatus==MissionStatus::Complete&&w.targets.size()==2);wait(w);assert(w.generators[0].remaining==0);
 auto recon=fixture();recon.procedural.objectives[0].kind=ProceduralGoalKind::Recon;prepareProceduralReinforcements(recon,2,5);recon.procedural.objectives[0].complete=true;recon.stepGenerators(.1f);assert(recon.generators[0].remaining==0);
 auto strike=fixture();strike.procedural.objectives[0].kind=ProceduralGoalKind::Strike;prepareProceduralReinforcements(strike,3,5);strike.targets[0].health=0;strike.targets[0].processed=true;
 updateProceduralMission(strike,0);assert(strike.procedural.objectives[0].complete&&strike.generators[0].remaining==0&&strike.missionStatus==MissionStatus::Complete);
 // Exercise real encounter placement and pooled-slot reservation without retail meshes.
 SkirmishSettings settings;settings.airUnits=0;settings.groundPerZone=3;auto recipe=generateSkirmishRecipe(settings);
 World placed;placed.targets.clear();placed.proceduralLayout=placed.missionPrototype=true;placed.game=1;placed.position={-240,0,-240};placed.prototypeSeed=settings.seed;
 for(const auto& zone:recipe.at("objectives")){ProceduralObjective goal;auto node=recipe.at("nodes").at(zone.at("node").get<size_t>());goal.position={node[0].get<float>(),0,node[1].get<float>()};placed.procedural.objectives.push_back(goal);}
 std::vector<Target> templates;for(int type:{0,2,14}){Target t;t.spawnType=type;t.mesh=std::make_shared<WorldMesh>();t.mesh->push_back({{-1,0,-1},{1,1,1}});t.mesh->push_back({{1,4,1},{1,1,1}});templates.push_back(t);}
 populateProceduralEncounters(placed,recipe,templates);assert(placed.generators.size()==3&&placed.targets.size()<=64);
 for(const auto& g:placed.generators){assert(g.remaining==3&&g.slots.size()==1);assert(!placed.targets[g.slots[0]].active&&placed.targets[g.slots[0]].generated);}
 bool rejected=false;try{prepareProceduralReinforcements(recon,13,5);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 std::cout<<"Finite reinforcement pools, proximity/occupancy gates, delayed elimination and recon shutdown passed\n";
}
