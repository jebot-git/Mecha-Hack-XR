#include "rpg/generation.hpp"
#include <queue>
#include <limits>
namespace iron::rpg {
GenerationPlan generationPlan(uint32_t seed,int biome,int location,int region){
  Dice random{seed^0x735a9e27u};GenerationPlan plan;plan.seed=seed;
  plan.layout=int(random.next()%5);plan.height=int(random.next()%6);
  plan.palette=int(random.next()%5);plan.sectionStyle=int(random.next()%4);
  plan.fullInterior=location==7||region==2||region==3;
  plan.submerged=biome==2;
  plan.underground=region==3||(!plan.fullInterior&&!plan.submerged&&location!=4&&location!=8&&location!=9&&random.next()%3==0);
  plan.surfaceFacility=!plan.fullInterior&&!plan.underground&&!plan.submerged&&(location==0||location==4||location==6)&&plan.sectionStyle!=0;
  plan.organic=!plan.fullInterior&&!plan.submerged&&location!=4&&location!=6&&location!=0;
  plan.city=location==0&&!plan.fullInterior&&!plan.submerged;
  plan.relief=18+float(random.next()%43);
  Dice modifiers{seed^0xae14738du};
  const bool dry=biome==0||biome==3||biome==5||biome==6;
  plan.lowerTier=dry&&plan.underground&&modifiers.next()%3==0;
  plan.upperTier=dry&&plan.surfaceFacility&&modifiers.next()%3==0;
  return plan;
}
float missionLayoutFitness(const Json &r,float extent){
  const auto &nodes=r.at("nodes");if(nodes.size()<3)return -100000;
  std::vector<std::vector<size_t>> links(nodes.size());
  float minX=extent,minZ=extent,maxX=-extent,maxZ=-extent,penalty=0;
  for(const auto &node:nodes){float x=node[0],z=node[1];if(std::abs(x)>extent||std::abs(z)>extent)return -100000;
    minX=std::min(minX,x);maxX=std::max(maxX,x);minZ=std::min(minZ,z);maxZ=std::max(maxZ,z);}
  for(const auto &road:r.at("roads")){auto edge=road.at("nodes").get<std::array<size_t,2>>();
    if(edge[0]>=nodes.size()||edge[1]>=nodes.size())return -100000;
    links[edge[0]].push_back(edge[1]);links[edge[1]].push_back(edge[0]);
    float distance=std::hypot(nodes[edge[0]][0].get<float>()-nodes[edge[1]][0].get<float>(),nodes[edge[0]][1].get<float>()-nodes[edge[1]][1].get<float>());
    penalty+=std::max(0.f,distance/extent-.6f)*4;
  }
  std::vector<bool> visited(nodes.size());std::queue<size_t> queue;visited[0]=true;queue.push(0);size_t reachable=0;
  while(!queue.empty()){auto i=queue.front();queue.pop();++reachable;for(auto j:links[i])if(!visited[j]){visited[j]=true;queue.push(j);}}
  if(reachable!=nodes.size())return -100000;
  float smallest=std::numeric_limits<float>::infinity(),largest=0,total=0;
  for(const auto &plot:r.at("plots")){auto b=plot.at("bounds").get<std::array<float,4>>();float area=(b[2]-b[0])*(b[3]-b[1]);
    if(area<=0)return -100000;
    smallest=std::min(smallest,area);largest=std::max(largest,area);total+=area;}
  float coverage=(maxX-minX)*(maxZ-minZ)/(4*extent*extent);
  float loops=float(r.at("roads").size()+1)-float(nodes.size());
  float scaleVariety=std::isfinite(smallest)?std::log1p(largest/std::max(1.f,smallest)):0;
  return 40*coverage+1.5f*std::min(20.f,loops)+float(std::min<size_t>(r.at("plots").size(),40))+5*scaleVariety+
         25*std::min(.5f,total/(4*extent*extent))-penalty;
}
void optimiseMissionLayout(Json &recipe,const GenerationPlan &plan,float extent){
  Json best;float score=-100001;int selected=0;
  // Keep the selected family; search several seeded spatial arrangements for
  // connected loops, useful coverage and parcels at several silhouette scales.
  for(int attempt=0;attempt<4;++attempt){
    auto trial=recipe;auto candidate=plan;candidate.seed^=uint32_t(attempt)*0x9e3779b9u;
    shapeMissionLayout(trial,candidate,extent);float value=missionLayoutFitness(trial,extent);
    if(value>score){score=value;selected=attempt;best=std::move(trial);}
  }
  if(score<0)throw std::runtime_error("No valid connected mission layout candidate");
  recipe=std::move(best);recipe["generation_fitness"]={{"score",score},{"candidate",selected},{"version",generationVersion}};
}
}
