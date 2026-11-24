#include "rpg/generation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace iron::rpg {
namespace {
using Point=std::array<float,2>;
float squared(Point a,Point b){return (a[0]-b[0])*(a[0]-b[0])+(a[1]-b[1])*(a[1]-b[1]);}
using Polygon=std::vector<Point>;
Polygon cell(const std::vector<Point> &sites,size_t index,float extent){
  Polygon result{{-extent,-extent},{extent,-extent},{extent,extent},{-extent,extent}};
  for(size_t j=0;j<sites.size();++j)if(j!=index){
    auto a=sites[index],b=sites[j];Point normal{b[0]-a[0],b[1]-a[1]};
    float threshold=(b[0]*b[0]+b[1]*b[1]-a[0]*a[0]-a[1]*a[1])*.5f;
    auto side=[&](Point p){return p[0]*normal[0]+p[1]*normal[1]-threshold;};
    Polygon next;
    for(size_t n=0;n<result.size();++n){auto p=result[n],q=result[(n+1)%result.size()];float u=side(p),v=side(q);
      if(u<=0)next.push_back(p);
      if((u<=0)!=(v<=0)){float t=u/(u-v);next.push_back({p[0]+t*(q[0]-p[0]),p[1]+t*(q[1]-p[1])});}}
    result=std::move(next);
  }
  return result;
}
Point centroid(const Polygon &p,Point fallback){
  double area=0,x=0,z=0;
  for(size_t i=0;i<p.size();++i){auto a=p[i],b=p[(i+1)%p.size()];double cross=double(a[0])*b[1]-double(b[0])*a[1];area+=cross;x+=(a[0]+b[0])*cross;z+=(a[1]+b[1])*cross;}
  return std::abs(area)>.001?Point{float(x/(3*area)),float(z/(3*area))}:fallback;
}
}
// Independent implementations inspired by Arcana's public design descriptions:
// shape geography first, then coherent wards/groves and road-frontage parcels.
// No Watabou source, generated artwork or online service is embedded.
void shapeMissionShores(Json &recipe,const GenerationPlan &plan){
  if(plan.fullInterior||plan.submerged)return;
  Dice dice{plan.seed^0x73686f72u};
  for(auto &body:recipe["terrain"]["water"]){
    auto b=body.at("bounds").get<std::array<float,4>>();
    // Long engineered dock basins keep straight quays; incidental ponds and
    // natural offshore shelves acquire a bounded, star-shaped contour.
    if(!plan.organic&&(b[2]-b[0])/(b[3]-b[1])<.7f)continue;
    float cx=(b[0]+b[2])/2,cz=(b[1]+b[3])/2,hx=(b[2]-b[0])/2,hz=(b[3]-b[1])/2;
    float phase=float(dice.next()%6284)/1000;Json shore=Json::array();
    for(int i=0;i<24;++i){float angle=-pi+.0001f+float(i)*2*pi/24;
      float radius=.86f+.075f*std::sin(angle*3+phase)+.035f*std::cos(angle*5-phase);
      shore.push_back({cx+hx*radius*std::cos(angle),cz+hz*radius*std::sin(angle)});}
    body["shore"]=std::move(shore);
  }
}
void districtMissionPlots(Json &recipe,const GenerationPlan &plan,float extent){
  Dice dice{plan.seed^0x77617264u};std::vector<Point> seeds;
  for(int attempt=0;attempt<100&&seeds.size()<7;++attempt){Point p{extent*(float(dice.next()%1401)/1000-.7f),extent*(float(dice.next()%1401)/1000-.7f)};
    if(std::all_of(seeds.begin(),seeds.end(),[&](Point q){return squared(p,q)>extent*extent*.09f;}))seeds.push_back(p);}
  for(int iteration=0;iteration<2;++iteration){auto next=seeds;
    for(size_t i=0;i<seeds.size();++i)next[i]=centroid(cell(seeds,i,extent),seeds[i]);
    seeds=std::move(next);}
  Json districts=Json::array();
  for(size_t i=0;i<seeds.size();++i)districts.push_back({{"id",i},{"centre",seeds[i]},{"boundary",cell(seeds,i,extent)},
    {"purpose",int((i+size_t(plan.palette))%6)},{"natural",plan.organic}});
  for(auto &plot:recipe["plots"]){auto b=plot.at("bounds").get<std::array<float,4>>();Point p{(b[0]+b[2])/2,(b[1]+b[3])/2};
    size_t nearest=0;float best=std::numeric_limits<float>::infinity();
    for(size_t i=0;i<seeds.size();++i)if(float d=squared(p,seeds[i]);d<best){best=d;nearest=i;}
    plot["district"]=nearest;plot["purpose"]=districts[nearest]["purpose"];
  }
  recipe["districts"]=std::move(districts);
}
}
