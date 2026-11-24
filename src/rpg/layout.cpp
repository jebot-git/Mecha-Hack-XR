#include "rpg/generation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <vector>
namespace iron::rpg {
namespace {
using Point=std::array<float,2>;
using Rect=std::array<float,4>;
float distance(Point a,Point b){return std::hypot(a[0]-b[0],a[1]-b[1]);}
float segmentDistance(Point p,Point a,Point b){
  float dx=b[0]-a[0],dz=b[1]-a[1];
  float t=std::clamp(((p[0]-a[0])*dx+(p[1]-a[1])*dz)/std::max(.001f,dx*dx+dz*dz),0.f,1.f);
  return distance(p,{a[0]+dx*t,a[1]+dz*t});
}
bool inside(Point p,Rect r){return p[0]>r[0]&&p[0]<r[2]&&p[1]>r[1]&&p[1]<r[3];}
bool intersects(Point a,Point b,Rect r){
  return std::min(a[0],b[0])<r[2]&&std::max(a[0],b[0])>r[0]&&
         std::min(a[1],b[1])<r[3]&&std::max(a[1],b[1])>r[1];
}
}
void shapeMissionLayout(Json &recipe,const GenerationPlan &plan,float extent){
  Dice random{plan.seed^0x79e1c65bu};
  const bool reserve=!plan.fullInterior&&(plan.underground||plan.submerged||plan.surfaceFacility);
  Rect core{-.70f*extent-40,-.42f*extent-40,-.26f*extent+40,.06f*extent+40};
  std::vector<Point> points;std::vector<std::array<unsigned,2>> edges;
  std::vector<float> widths;std::set<std::array<unsigned,2>> used;
  auto node=[&](Point p){
    p[0]=std::clamp(p[0],-.90f*extent,.90f*extent);p[1]=std::clamp(p[1],-.90f*extent,.90f*extent);
    if(reserve&&inside(p,core))p[0]=core[2]+28;
    for(size_t i=0;i<points.size();++i)if(distance(p,points[i])<.001f)return unsigned(i);
    points.push_back(p);return unsigned(points.size()-1);
  };
  auto edge=[&](unsigned a,unsigned b,float width){
    if(a==b)return;
    std::array<unsigned,2> key{std::min(a,b),std::max(a,b)};
    if(used.insert(key).second){edges.push_back({a,b});widths.push_back(width);}
  };
  // Engineered rectangles retain explicit axis-aligned bends. Natural routes
  // use physical ribbons, so their curves do not fill a diagonal's AABB.
  auto connect=[&](unsigned first,unsigned last,float width=28.f){
    auto a=points[first],b=points[last];
    if(plan.organic&&(!reserve||!intersects(a,b,{core[0]-48,core[1]-48,core[2]+48,core[3]+48}))){
      float span=distance(a,b),bend=float(int(random.next()%101)-50)*.0018f;
      Point control{(a[0]+b[0])/2-(b[1]-a[1])*bend,(a[1]+b[1])/2+(b[0]-a[0])*bend};
      int steps=std::max(2,int(std::ceil(span/80)));unsigned previous=first;
      for(int i=1;i<steps;++i){float t=float(i)/steps,u=1-t;
        auto next=node({u*u*a[0]+2*u*t*control[0]+t*t*b[0],u*u*a[1]+2*u*t*control[1]+t*t*b[1]});
        edge(previous,next,std::max(width,36.f));previous=next;}
      edge(previous,last,std::max(width,36.f));return;
    }
    std::vector<std::vector<Point>> paths={{{a[0],b[1]}},{{b[0],a[1]}}};
    if(random.next()&1)std::swap(paths[0],paths[1]);
    for(float x:{core[0]-32,core[2]+32})paths.push_back({{x,a[1]},{x,b[1]}});
    for(float z:{core[1]-32,core[3]+32})paths.push_back({{a[0],z},{b[0],z}});
    float best=std::numeric_limits<float>::infinity();std::vector<Point> chosen;
    for(auto path:paths){
      path.push_back(b);Point previous=a;float cost=0;bool clear=true;
      for(auto p:path){if(reserve&&intersects(previous,p,core)){clear=false;break;}cost+=distance(previous,p);previous=p;}
      if(clear&&cost<best){best=cost;chosen=std::move(path);}
    }
    if(chosen.empty())chosen={Point{a[0],b[1]},b};
    unsigned previous=first;
    for(auto p:chosen){unsigned next=node(p);edge(previous,next,width);previous=next;}
  };
  unsigned entry=node({0,extent*.65f});
  unsigned hub=node({0,-extent*.08f});
  unsigned exit=node({0,-extent*.78f});
  connect(entry,hub,40);connect(hub,exit,40);
  auto ring=[&](float cx,float cz,float hx,float hz){
    std::vector<unsigned> corners;
    if(plan.organic)for(int i=0;i<8;++i){float angle=pi*(-.75f+float(i)*.25f),r=.88f+float(random.next()%101)/1000;
      corners.push_back(node({cx+hx*r*std::cos(angle),cz+hz*r*std::sin(angle)}));}
    else for(Point p:std::array<Point,8>{{{cx-hx,cz-hz},{cx,cz-hz},{cx+hx,cz-hz},{cx+hx,cz},{cx+hx,cz+hz},{cx,cz+hz},{cx-hx,cz+hz},{cx-hx,cz}}})corners.push_back(node(p));
    for(size_t i=0;i<corners.size();++i)connect(corners[i],corners[(i+1)%corners.size()]);
    return corners;
  };
  std::vector<float> cityX,cityZ;
  if(plan.city){
    // Shared intersections and rectangular frontage parcels keep both the
    // street mesh and the navigation graph continuous across each block.
    const int blocks=extent>=750?7:5;
    for(int i=0;i<=blocks;++i){
      const float offset=-.74f+1.48f*float(i)/blocks;
      const float jitter=i>0&&i<blocks?float(int(random.next()%51)-25)*.001f:0;
      cityX.push_back(extent*(offset+jitter));
      cityZ.push_back(extent*(offset-jitter*.72f));
    }
    std::vector<std::vector<int>> junctions(cityZ.size(),std::vector<int>(cityX.size(),-1));
    for(size_t z=0;z<cityZ.size();++z)for(size_t x=0;x<cityX.size();++x){
      const Point point{cityX[x],cityZ[z]};
      if(reserve&&inside(point,core))continue;
      junctions[z][x]=int(node(point));
      if(x&&junctions[z][x-1]>=0)connect(unsigned(junctions[z][x-1]),unsigned(junctions[z][x]),z%3==0?39.f:30.f);
      if(z&&junctions[z-1][x]>=0)connect(unsigned(junctions[z-1][x]),unsigned(junctions[z][x]),x%3==0?39.f:30.f);
    }
    for(unsigned access:{entry,hub,exit}){
      unsigned nearest=access;
      float best=std::numeric_limits<float>::infinity();
      for(const auto &row:junctions)for(int crossing:row)
        if(crossing>=0&&unsigned(crossing)!=access){
          float d=distance(points[access],points[size_t(crossing)]);
          if(d<best){best=d;nearest=unsigned(crossing);}
        }
      connect(access,nearest,38);
    }
  }else switch(plan.layout%5){
  case 0:{ // Several nested perimeter routes, linked at different gates.
    std::vector<unsigned> previous;
    for(int level=0;level<3;++level){
      float radius=extent*(.27f+.22f*level);auto current=ring(0,-extent*.04f,radius,radius*.90f);
      if(previous.empty())connect(hub,current[random.next()%current.size()],36);
      else for(int gate=0;gate<4;++gate){size_t k=size_t(gate*2);connect(previous[k],current[k],gate%2?28:36);}
      previous=std::move(current);
    }
    connect(entry,previous[5],36);connect(exit,previous[1],36);break;
  }
  case 1:{ // Branching spokes terminate at a circumferential supply route.
    auto outside=ring(0,-extent*.04f,extent*.76f,extent*.72f);
    for(size_t i=0;i<outside.size();++i){
      Point p=points[outside[i]],mid{p[0]*.49f,p[1]*.49f};
      mid[0]+=float(int(random.next()%81)-40);unsigned branch=node(mid);
      connect(hub,branch,32);connect(branch,outside[i],28);
      if(i%2==0){auto q=points[branch];q[0]+=i<4?extent*.15f:-extent*.15f;connect(branch,node(q),24);}
    }
    connect(entry,outside[5],36);connect(exit,outside[1],36);break;
  }
  case 2:{ // A changing central spine, alternating work-yard branches and loops.
    std::vector<unsigned> left,right;unsigned previous=entry;
    for(int row=0;row<6;++row){
      float z=extent*(.53f-float(row)*.235f);
      float x=extent*(row%2?.15f:-.11f)+float(int(random.next()%41)-20);
      unsigned here=node({x,z});connect(previous,here,36);previous=here;
      left.push_back(node({-extent*(.60f+.03f*(row%3)),z+extent*.045f}));
      right.push_back(node({extent*(.61f+.025f*(row%3)),z-extent*.055f}));
      connect(here,left.back());connect(here,right.back());
      if(row){connect(left[row-1],left[row],24);if(row%2)connect(right[row-1],right[row],24);}
    }
    connect(previous,exit,36);connect(entry,left.front(),28);connect(exit,right.back(),28);break;
  }
  case 3:{ // Courtyard clusters connected by offset, double-entry arteries.
    std::vector<unsigned> centres;
    for(Point p:std::array<Point,5>{{{-.55f,.37f},{.51f,.34f},{-.52f,-.55f},{.50f,-.54f},{.03f,-.10f}}}){
      p[0]*=extent;p[1]*=extent;auto c=node(p);centres.push_back(c);
      auto boundary=ring(points[c][0],points[c][1],extent*(.14f+float(random.next()%5)*.01f),extent*.14f);
      connect(c,boundary[1],32);connect(c,boundary[5],32);
      connect(hub,c,36);
    }
    for(size_t i=0;i<centres.size();++i)connect(centres[i],centres[(i+1)%centres.size()],28);
    connect(entry,centres[0],36);connect(exit,centres[3],36);break;
  }
  default:{ // Spaced clearings: a tree plus local alternative approaches.
    std::vector<unsigned> sites{hub};
    for(int attempt=0;attempt<400&&sites.size()<25;++attempt){
      Point p{extent*(float(random.next()%1501)/1000-.75f),extent*(float(random.next()%1501)/1000-.75f)};
      if(reserve&&inside(p,core))continue;
      if(std::any_of(sites.begin(),sites.end(),[&](unsigned n){return distance(p,points[n])<extent*.20f;}))continue;
      unsigned here=node(p);unsigned nearest=sites.front();float best=1e20f;
      for(auto other:sites){float d=distance(points[here],points[other]);if(d>1&&d<best){best=d;nearest=other;}}
      connect(nearest,here);sites.push_back(here);
    }
    for(size_t i=1;i<sites.size();++i)for(size_t j=i+1;j<sites.size();++j){
      float d=distance(points[sites[i]],points[sites[j]]);if(d>extent*.58f)continue;bool neighbor=true;
      for(auto k:sites)if(k!=sites[i]&&k!=sites[j]&&std::max(distance(points[k],points[sites[i]]),distance(points[k],points[sites[j]]))<d){neighbor=false;break;}
      if(neighbor)connect(sites[i],sites[j],28);
    }
    for(size_t i=2;i+3<sites.size();i+=6)connect(sites[i],sites[i+3],28);
    connect(entry,sites[sites.size()-3],36);connect(exit,sites[3],36);break;
  }}
  // A geometric crossroads must also be a navigation junction. Split the
  // original edges simultaneously, with stable ordering and shared vertices.
  if(plan.organic||plan.city){
    std::vector<std::vector<std::pair<float,unsigned>>> cuts(edges.size());
    for(size_t i=0;i<edges.size();++i)cuts[i]={{0,edges[i][0]},{1,edges[i][1]}};
    for(size_t i=0;i<edges.size();++i)for(size_t j=i+1;j<edges.size();++j){
      auto e=edges[i],f=edges[j];if(e[0]==f[0]||e[0]==f[1]||e[1]==f[0]||e[1]==f[1])continue;
      auto a=points[e[0]],b=points[e[1]],c=points[f[0]],d=points[f[1]];
      float ux=b[0]-a[0],uz=b[1]-a[1],vx=d[0]-c[0],vz=d[1]-c[1],det=ux*vz-uz*vx;
      if(std::abs(det)<.001f)continue;
      float t=((c[0]-a[0])*vz-(c[1]-a[1])*vx)/det,s=((c[0]-a[0])*uz-(c[1]-a[1])*ux)/det;
      if(t<=.001f||t>=.999f||s<=.001f||s>=.999f)continue;
      auto n=node({a[0]+ux*t,a[1]+uz*t});cuts[i].push_back({t,n});cuts[j].push_back({s,n});
    }
    auto oldWidths=widths;edges.clear();widths.clear();used.clear();
    for(size_t i=0;i<cuts.size();++i){auto &parts=cuts[i];std::sort(parts.begin(),parts.end());
      for(size_t j=1;j<parts.size();++j)edge(parts[j-1].second,parts[j].second,oldWidths[i]);}
  }
  Json roads=Json::array();
  for(size_t i=0;i<edges.size();++i)roads.push_back({{"nodes",edges[i]},{"width",widths[i]},{"class",widths[i]>=36?"arterial":"local"},{"geometry",plan.organic?"ribbon":"rectangle"}});
  Json plots=Json::array();
  if(plan.city)for(size_t z=1;z<cityZ.size();++z)for(size_t x=1;x<cityX.size();++x){
    const Rect block{cityX[x-1]+22,cityZ[z-1]+22,cityX[x]-22,cityZ[z]-22};
    if(block[2]-block[0]<40||block[3]-block[1]<40||
       (reserve&&intersects({block[0],block[1]},{block[2],block[3]},core)))continue;
    const Point centre{(block[0]+block[2])/2,(block[1]+block[3])/2};
    if(distance(centre,points[entry])<115)continue;
    plots.push_back({{"id",plots.size()},{"bounds",block},{"district",0},{"role","urban"},
                     {"sources",Json::array({"is1","is2"})},{"buildings",0},{"scenery_count",6}});
  }
  const int desired=extent>=750?88:62;
  for(int attempt=0;attempt<800&&int(plots.size())<desired;++attempt){
    Point p{extent*(float(random.next()%1701)/1000-.85f),extent*(float(random.next()%1701)/1000-.85f)};
    float half=attempt%6==0?60.f+float(random.next()%25):
               attempt%3==0?24.f+float(random.next()%13):36.f+float(random.next()%21);
    if(attempt%4!=3&&!edges.empty()){
      size_t road=random.next()%edges.size();auto a=points[edges[road][0]],b=points[edges[road][1]];
      float span=distance(a,b),t=float(random.next()%1001)/1000,side=(random.next()&1)?1.f:-1.f;
      float setback=half+widths[road]/2+24;
      p={a[0]+(b[0]-a[0])*t-(b[1]-a[1])/std::max(1.f,span)*setback*side,
         a[1]+(b[1]-a[1])*t+(b[0]-a[0])/std::max(1.f,span)*setback*side};
    }
    if(std::abs(p[0])+half>extent*.94f||std::abs(p[1])+half>extent*.94f)continue;
    if(distance(p,{0,extent*.65f})<110+half)continue;
    if(reserve&&inside(p,{core[0]-half,core[1]-half,core[2]+half,core[3]+half}))continue;
    bool clear=true;
    for(auto e:edges)if(segmentDistance(p,points[e[0]],points[e[1]])<half+34){clear=false;break;}
    if(!clear)continue;
    Rect bounds{p[0]-half,p[1]-half,p[0]+half,p[1]+half};
    for(const auto &plot:plots){auto b=plot.at("bounds").get<Rect>();if(bounds[0]<b[2]+12&&b[0]<bounds[2]+12&&bounds[1]<b[3]+12&&b[1]<bounds[3]+12){clear=false;break;}}
    if(!clear)continue;
    int district=int(p[0]>=0)+2*int(p[1]>=0);
    constexpr const char *roles[]={"industrial","storage","urban"};
    plots.push_back({{"id",plots.size()},{"bounds",bounds},{"district",district},{"role",roles[(district+plan.palette)%3]},
      {"sources",Json::array({"is1","is2"})},{"buildings",0},{"scenery_count",3+int(random.next()%5)}});
  }
  // Distant sites anchor internal recipe references; actual RPG objectives are
  // assigned to safe generated sites after physical scenery and sections exist.
  Json objectives=Json::array();
  std::vector<unsigned> ranked;for(unsigned i=0;i<points.size();++i)ranked.push_back(i);
  std::stable_sort(ranked.begin(),ranked.end(),[&](unsigned a,unsigned b){return distance(points[a],points[entry])>distance(points[b],points[entry]);});
  for(int i=0;i<3;++i)objectives.push_back({{"node",ranked[size_t(i)%ranked.size()]},{"radius",8},{"kind","encounter"}});
  recipe["nodes"]=points;recipe["roads"]=std::move(roads);recipe["plots"]=std::move(plots);recipe["objectives"]=std::move(objectives);
  recipe["spawn"]["node"]=entry;recipe["mission"]["kind"]="eliminate";
  recipe["version"]=4; // Bounded local meshes for irregular district and parcel edges.
  recipe["generator"]="mecha-landscape-v6";
  recipe["city_frontages"]=plan.city;
  districtMissionPlots(recipe,plan,extent);
}
}
