#include "rpg/encounters.hpp"
#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/world_generation.hpp"
#include "rpg/generation.hpp"
#include <algorithm>
#include <numeric>
#include <queue>
namespace iron::rpg {
namespace {
Vec3 unpack(std::array<float,3> p){return {p[0],p[1],p[2]};}
std::array<float,3> pack(Vec3 p){return {p.x,p.y,p.z};}
unsigned node(World &w,Vec3 p){p.y=w.surfaceHeight(p.x,p.z);w.interiorNavNodes.push_back(p);return unsigned(w.interiorNavNodes.size()-1);}
void link(World &w,unsigned a,unsigned b){if(a!=b)w.interiorNavLinks.push_back({a,b});}
bool within(std::array<float,4> b,Vec3 p,float margin=0){return p.x>b[0]-margin&&p.x<b[2]+margin&&p.z>b[1]-margin&&p.z<b[3]+margin;}
bool clearRoute(const World &w,Vec3 a,Vec3 b,float radius=18){
  const int steps=std::max(1,int(std::ceil(length(a-b)/(w.interiorDecks.empty()?5.f:2.f))));
  float floor=w.walkableHeight(a);
  // The entire segment shares a conservative blocker list. Heights and
  // authored hull clearances are still checked at every original sample.
  std::vector<const Target*> blockers;
  for(const auto& t:w.targets){
    if(t.active&&t.health>0&&t.spawnType<0&&t.invulnerable&&
       t.center.x+t.half.x+radius>=std::min(a.x,b.x)&&t.center.x-t.half.x-radius<=std::max(a.x,b.x)&&
       t.center.z+t.half.z+radius>=std::min(a.z,b.z)&&t.center.z-t.half.z-radius<=std::max(a.z,b.z))
      blockers.push_back(&t);
  }
  for(int i=0;i<=steps;++i){
    auto p=a+(b-a)*(float(i)/steps);p.y=floor;
    p.y=w.walkableHeight(p);
    if(!w.interiorDecks.empty()&&(std::abs(p.y-floor)>1.2f||
       w.deckContact(p+Vec3{0,.15f,0},p+Vec3{0,28.f,0})<=1.f))return false;
    floor=p.y;
    if(!w.groundPassable(p,3.5f)||w.waterDepthAt(p,3.5f)>w.waterTraversalLimitAt(p,3.5f))return false;
    for(const auto* t:blockers)if(std::abs(p.x-t->center.x)<t->half.x+radius&&std::abs(p.z-t->center.z)<t->half.z+radius&&
      p.y+28>t->center.y-t->half.y&&p.y<t->center.y+t->half.y)return false;
  }
  return w.interiorDecks.empty()||std::abs(floor-w.walkableHeight(b))<1.f;
}

using HeightChanges=std::vector<std::pair<size_t,float>>;
void refresh(World &w,const std::array<float,4>& b){
  auto& map=w.editGeometry();
  for(auto& patch:map.terrainPatches){
    if(patch.high.x<b[0]||patch.low.x>b[2]||patch.high.z<b[1]||patch.low.z>b[3])continue;
    patch.low.y=1e9f;patch.high.y=-1e9f;
    for(size_t i=patch.first;i<patch.first+patch.count;++i){
      float y=map.terrain[i].position.y;
      patch.low.y=std::min(patch.low.y,y);patch.high.y=std::max(patch.high.y,y);
    }
  }
  // A vertical edit preserves tile membership and barycentric denominators.
  // A triangle touching any changed vertex is indexed in that vertex's tile.
  for(int x=int(std::floor(b[0]/64));x<=int(std::floor(b[2]/64));++x)
    for(int z=int(std::floor(b[1]/64));z<=int(std::floor(b[3]/64));++z){
      auto tile=map.groundTiles.find({x,z});if(tile==map.groundTiles.end())continue;
      for(auto i:tile->second){
        auto& tri=map.groundTriangles[i];auto first=tri.firstVertex;
        tri.a.y=map.terrain[first].position.y;tri.b.y=map.terrain[first+1].position.y;tri.c.y=map.terrain[first+2].position.y;
      }
    }
  for(size_t i=0;i<w.interiorNavNodes.size();++i){
    if(std::find(w.interiorTierNodes.begin(),w.interiorTierNodes.end(),i)!=w.interiorTierNodes.end())continue;
    auto &p=w.interiorNavNodes[i];p.y=w.surfaceHeight(p.x,p.z);
  }
}
HeightChanges stamp(World &w,std::array<float,4> b,float floor,const std::array<float,4>& dirty){
  HeightChanges changes;
  auto& map=w.editGeometry();
  for(size_t i=0;i<map.terrain.size();++i){
      auto &p=map.terrain[i].position;
      if(p.x<dirty[0]||p.z<dirty[1]||p.x>dirty[2]||p.z>dirty[3])continue;
      float dx=std::max({b[0]-p.x,p.x-b[2],0.f}),dz=std::max({b[1]-p.z,p.z-b[3],0.f});
      float weight=std::clamp((48-std::hypot(dx,dz))/24,0.f,1.f);
      if(weight<=0)continue;
      changes.emplace_back(i,p.y);p.y=p.y*(1-weight)+floor*weight;
  }
  refresh(w,dirty);
  return changes;
}
bool stampPreservesRoutes(const World &w,std::array<float,4> shell){
  for(auto edge:w.interiorNavLinks){
    const auto a=w.interiorNavNodes[edge[0]],b=w.interiorNavNodes[edge[1]];
    const int steps=std::max(1,int(std::ceil(length(a-b)/4)));
    for(int n=0;n<=steps;++n){
      const auto p=a+(b-a)*(float(n)/steps);
      if(within(shell,p,70)&&!w.groundPassable(p,3.5f))return false;
    }
  }
  return true;
}
void wall(World &w,const char *material,bool horizontal,float boundary,float first,float last,float floor,bool door){
  constexpr float height=40,halfDoor=20;
  auto piece=[&](float a,float b){if(b-a<.01f)return;
    addStructuralPiece(w,material,horizontal?Vec3{(a+b)/2,floor+height/2,boundary}:Vec3{boundary,floor+height/2,(a+b)/2},
      horizontal?Vec3{(b-a)/2,height/2,1}:Vec3{1,height/2,(b-a)/2});};
  if(!door){piece(first,last);return;}
  float middle=(first+last)/2;piece(first,middle-halfDoor);piece(middle+halfDoor,last);
  addStructuralPiece(w,"door_header",horizontal?Vec3{middle,floor+36,boundary}:Vec3{boundary,floor+36,middle},
    horizontal?Vec3{halfDoor,4,1}:Vec3{1,4,halfDoor});
}

void core(World &w,DungeonSite &site){
  auto room=std::find_if(site.rooms.begin(),site.rooms.end(),[](const auto &r){return r.kind==4;});
  auto p=unpack((room==site.rooms.end()?site.rooms.back():*room).position);
  const auto shifted=p+Vec3{14,0,14};
  if(std::abs(w.walkableHeight(shifted)-p.y)<2.f)p=shifted;
  p.y=w.walkableHeight(p);
  Target t;t.origin=p;applyOriginalModel(t,"ship_reactor");t.spawnType=-1;t.health=24;
  site.core=int(w.targets.size());w.targets.push_back(std::move(t));
}
void addTierSite(World &w,std::vector<DungeonSite> &sites,int count,bool underground){
  if(w.interiorTierNodes.empty()||int(sites.size())>=count)return;
  const auto foot=w.interiorTierNodes.front();
  const auto approach=w.interiorNavNodes[foot-1];
  DungeonSite site;
  site.style=underground?4:0;
  site.underground=underground;
  site.entrance=pack(approach);
  site.bounds={approach.x,approach.z,approach.x,approach.z};
  for(const auto &deck:w.interiorDecks){
    const bool onRoute=std::any_of(w.interiorTierNodes.begin(),w.interiorTierNodes.end(),[&](unsigned node){
      const auto p=w.interiorNavNodes[node];
      return p.x>=deck.bounds[0] && p.x<=deck.bounds[2] && p.z>=deck.bounds[1] && p.z<=deck.bounds[3] &&
             std::abs(p.y-deck.height(p.x,p.z))<.1f;
    });
    if(!onRoute)continue;
    site.bounds={std::min(deck.bounds[0],site.bounds[0]),std::min(deck.bounds[1],site.bounds[1]),
                 std::max(deck.bounds[2],site.bounds[2]),std::max(deck.bounds[3],site.bounds[3])};
  }
  site.rooms.push_back({pack(approach),0,false});
  const auto cache=w.interiorTierNodes[w.interiorTierNodes.size()==3?1:2];
  for(const auto node:w.interiorTierNodes){
    const int next=int(site.rooms.size());
    const int kind=node==w.interiorTierNodes.back()?4:node==cache?3:1;
    site.rooms.push_back({pack(w.interiorNavNodes[node]),kind,false});
    site.links.push_back({next-1,next});
  }
  core(w,site);
  sites.push_back(std::move(site));
}

}
std::vector<DungeonSite> buildDungeonSites(World &w,const nlohmann::json &layout,int biome,int location,int region,int count){
  std::vector<DungeonSite> result;if(count<=0)return result;count=std::clamp(count,0,3);
  Dice dice{w.prototypeSeed^0x73697465u};
  // Existing hulls and tunnels already have authored traversable compartments.
  // Key those physical rooms instead of placing another shell inside the hull.
  if(region>=2||location==7){
    const auto plan=generationPlan(w.prototypeSeed,biome,location,region);
    const int rowCount=plan.sectionStyle==1?2:plan.sectionStyle==2?4:3;
    const size_t roomCount=size_t(rowCount*3);
    if(w.interiorNavNodes.size()<roomCount)return result;
    // The first navigation nodes are the main bays; the next nodes are the
    // authored doorway/alcove anchors. Follow the doorway graph from entry.
    std::vector<std::vector<unsigned>> graph(w.interiorNavNodes.size());
    for(auto edge:w.interiorNavLinks){graph[edge[0]].push_back(edge[1]);graph[edge[1]].push_back(edge[0]);}
    const unsigned first=unsigned(roomCount-2);
    std::vector<int> keyed(roomCount,-1);
    std::queue<unsigned> pending;pending.push(first);keyed[first]=0;
    int assigned=1;
    constexpr int keyLimit=6;
    DungeonSite site;site.style=4;site.underground=region==3;
    const float extent=w.movementBounds[2];
    site.bounds={-.82f*extent,-.82f*extent,.82f*extent,.40f*extent};
    site.entrance=pack({0,w.surfaceHeight(0,site.bounds[3]),site.bounds[3]});
    while(!pending.empty()){
      const unsigned here=pending.front();pending.pop();
      site.rooms.push_back({pack(w.interiorNavNodes[here]),here==first?0:1,false});
      for(unsigned portal:graph[here])for(unsigned next:graph[portal]){
        if(next>=roomCount||next==here)continue;
        if(keyed[next]<0&&assigned<keyLimit){keyed[next]=assigned++;pending.push(next);}
        if(keyed[next]>keyed[here])site.links.push_back({keyed[here],keyed[next]});
      }
    }
    if(site.rooms.size()>=3){
      const size_t cache=site.rooms.size()-2;
      site.rooms[cache].kind=3;site.rooms.back().kind=4;
      core(w,site);result.push_back(std::move(site));
    }
    addTierSite(w,result,count,region==3);
    return result;
  }
  const float extent=w.movementBounds[2];auto plan=generationPlan(w.prototypeSeed,biome,location,region);
  Vec3 excavation{-.48f*extent,0,-.18f*extent};excavation.y=w.surfaceHeight(excavation.x,excavation.z);
  if(plan.underground&&excavation.y<-48&&w.indoors(excavation+Vec3{0,12,0})){
    // The reserved excavation already has a below-grade floor, a roof and two
    // graded approaches. Key that physical facility before selecting surface
    // building parcels, so both site types can coexist on the same map.
    DungeonSite site;site.style=4;site.underground=true;
    site.bounds={-.70f*extent,-.42f*extent,-.26f*extent,.06f*extent};
    size_t existing=w.interiorNavNodes.size();std::vector<unsigned> anchors;
    for(int row=0;row<2;++row)for(int col=0;col<3;++col){
      Vec3 p{site.bounds[0]+(site.bounds[2]-site.bounds[0])*(float(col)+.5f)/3,0,
        site.bounds[1]+(site.bounds[3]-site.bounds[1])*(float(row)+.5f)/2};p.y=w.surfaceHeight(p.x,p.z);
      int nearest=-1;float best=1e20f;
      for(size_t n=0;n<existing;++n){auto q=w.interiorNavNodes[n];
        if(within(site.bounds,q)&&length(q-p)<best&&clearRoute(w,p,q)){nearest=int(n);best=length(q-p);}}
      if(nearest<0)continue;
      auto a=node(w,p);link(w,a,unsigned(nearest));anchors.push_back(a);site.rooms.push_back({pack(p),1,false});
    }
    if(site.rooms.size()>=3){
      site.rooms.front().kind=0;
      for(size_t n=1;n<site.rooms.size();++n){
        for(size_t i=0;i<n;++i){
          if(!clearRoute(w,w.interiorNavNodes[anchors[i]],w.interiorNavNodes[anchors[n]]))continue;
          site.links.push_back({int(i),int(n)});
          link(w,anchors[i],anchors[n]);
        }
      }
      Vec3 entrance{(site.bounds[0]+site.bounds[2])/2,0,site.bounds[3]};entrance.y=w.surfaceHeight(entrance.x,entrance.z);site.entrance=pack(entrance);
      site.rooms[site.rooms.size()-2].kind=3;site.rooms.back().kind=4;
      core(w,site);result.push_back(std::move(site));
    }
  }
  addTierSite(w,result,count,plan.underground);
  struct Candidate {Vec3 center;int rows;};std::vector<Candidate> candidates;
  const size_t roadCount=w.interiorNavNodes.size();
  for(const auto &plot:layout.at("plots")){auto b=plot.at("bounds").get<std::array<float,4>>();
    if(b[2]-b[0]<100||b[3]-b[1]<100)continue;
    int rows=b[3]-b[1]>=160?3:2;Vec3 p{(b[0]+b[2])/2,0,(b[1]+b[3])/2};
    p.y=w.surfaceHeight(p.x,p.z);if(length(p-w.missionEntry)<190||w.waterDepthAt(p,70)>0)continue;
    std::array<float,4> shell{p.x-48,p.z-float(rows)*24,p.x+48,p.z+float(rows)*24};bool clear=true;
    for(const auto &t:w.targets)if(t.active&&t.health>0&&t.spawnType<0&&
      std::abs(p.x-t.center.x)<48+t.half.x+25&&std::abs(p.z-t.center.z)<float(rows)*24+t.half.z+25)clear=false;
    for(auto edge:w.interiorNavLinks){auto a=w.interiorNavNodes[edge[0]],z=w.interiorNavNodes[edge[1]];int steps=std::max(1,int(std::ceil(length(a-z)/8)));
      for(int n=0;n<=steps;++n)if(within(shell,a+(z-a)*(float(n)/steps),22))clear=false;}
    if(clear)candidates.push_back({p,rows});
  }
  for(size_t i=candidates.size();i>1;--i)std::swap(candidates[i-1],candidates[dice.next()%i]);
  for(const auto &candidate:candidates){if(int(result.size())>=count)break;
    auto center=candidate.center;int rows=candidate.rows;DungeonSite site;site.style=biome==0?1:biome==2?3:location==1?2:0;
    site.bounds={center.x-48,center.z-rows*24,center.x+48,center.z+rows*24};bool overlap=false;
    for(const auto &prior:result)if(std::abs(center.x-(prior.bounds[0]+prior.bounds[2])/2)<140&&std::abs(center.z-(prior.bounds[1]+prior.bounds[3])/2)<180)overlap=true;
    if(overlap)continue;
    const auto oldTargets=w.targets.size(),oldNodes=w.interiorNavNodes.size(),oldLinks=w.interiorNavLinks.size(),oldRegions=w.interiorRegions.size();
    const auto& b=site.bounds;
    const std::array<float,4> dirty={b[0]-48,b[1]-48,b[2]+48,b[3]+48};
    auto oldHeights=stamp(w,b,center.y,dirty);
    auto rollback=[&]{
      auto& terrain=w.editGeometry().terrain;
      for(auto [i,y]:oldHeights)terrain[i].position.y=y;
      refresh(w,dirty);
    };
    float floor=w.surfaceHeight(center.x,center.z);
    if(!stampPreservesRoutes(w,site.bounds)){
      rollback();
      continue;
    }
    const char *material=site.style==2?"tunnel_wall":site.style==3?"ship_bulkhead":"hangar_wall";
    // Repeated bay widths give a deliberate architectural rhythm; a missing
    // corner produces a courtyard/L footprint instead of one stamped rectangle.
    int omitted=rows==3&&dice.next()%3?((dice.next()&1)?0:rows*2-1):-1;
    float split=center.x+float(int(dice.next()%9)-4);
    std::vector<float> xs{site.bounds[0],split,site.bounds[2]};
    std::vector<int> cells(size_t(rows*2),-1);std::vector<unsigned> roomNodes;
    for(int row=0;row<rows;++row)for(int col=0;col<2;++col){int key=row*2+col;if(key==omitted)continue;
      Vec3 p{(xs[col]+xs[col+1])/2,floor,site.bounds[1]+24+row*48};
      cells[size_t(key)]=int(site.rooms.size());site.rooms.push_back({pack(p),1,false});roomNodes.push_back(node(w,p));}
    int rooms=int(site.rooms.size());std::vector<int> parent(size_t(rooms),0);std::iota(parent.begin(),parent.end(),0);
    std::vector<std::array<int,2>> edges;
    for(int row=0;row<rows;++row)for(int col=0;col<2;++col){int key=row*2+col,a=cells[size_t(key)];if(a<0)continue;
      if(col==0&&cells[size_t(key+1)]>=0)edges.push_back({a,cells[size_t(key+1)]});
      if(row+1<rows&&cells[size_t(key+2)]>=0)edges.push_back({a,cells[size_t(key+2)]});}
    for(size_t n=edges.size();n>1;--n)std::swap(edges[n-1],edges[dice.next()%n]);
    auto root=[&](int n){while(parent[size_t(n)]!=n)n=parent[size_t(n)];return n;};
    for(auto edge:edges){int a=root(edge[0]),b=root(edge[1]);if(a!=b){parent[size_t(a)]=b;site.links.push_back(edge);}else if(dice.next()%2)site.links.push_back(edge);}
    auto connected=[&](int a,int b){return std::any_of(site.links.begin(),site.links.end(),[&](auto e){return (e[0]==a&&e[1]==b)||(e[0]==b&&e[1]==a);});};
    for(int row=0;row<rows;++row)for(int col=0;col<2;++col){int key=row*2+col,a=cells[size_t(key)];if(a<0)continue;
      float z0=site.bounds[1]+row*48,z1=z0+48,x0=xs[col],x1=xs[col+1];
      int left=col?cells[size_t(key-1)]:-1,right=col==0?cells[size_t(key+1)]:-1;
      int north=row?cells[size_t(key-2)]:-1,south=row+1<rows?cells[size_t(key+2)]:-1;
      wall(w,material,false,x0,z0,z1,floor,left>=0&&connected(a,left));
      if(right<0)wall(w,material,false,x1,z0,z1,floor,false);
      wall(w,material,true,z0,x0,x1,floor,(north>=0&&connected(a,north))||(row==0&&col==1));
      if(south<0)wall(w,material,true,z1,x0,x1,floor,row==rows-1&&col==0);
      addStructuralPiece(w,"roof_tile",{(x0+x1)/2,floor+41,(z0+z1)/2},{(x1-x0)/2,1,24});
      w.interiorRegions.push_back({{x0,z0,x1,z1},floor,floor+40});
    }
    for(auto edge:site.links){auto a=unpack(site.rooms[size_t(edge[0])].position),b=unpack(site.rooms[size_t(edge[1])].position);
      Vec3 doorway=(a+b)*.5f;if(std::abs(a.x-b.x)>1)doorway.x=split;auto portal=node(w,doorway);
      link(w,roomNodes[size_t(edge[0])],portal);link(w,portal,roomNodes[size_t(edge[1])]);}
    bool attached=true;
    for(int exit=0;exit<2;++exit){int room=cells[size_t(exit?1:(rows-1)*2)];auto door=unpack(site.rooms[size_t(room)].position);door.z=exit?site.bounds[1]:site.bounds[3];
      auto approach=door+Vec3{0,0,exit?-28.f:28.f};unsigned portal=node(w,door),outside=node(w,approach);
      link(w,roomNodes[size_t(room)],portal);link(w,portal,outside);
      float best=1e20f;int nearest=-1;
      for(size_t n=0;n<roadCount;++n){auto p=w.interiorNavNodes[n];if(exit?p.z>=site.bounds[1]:p.z<=site.bounds[3])continue;
        float distance=length(p-approach);if(distance<best&&clearRoute(w,approach,p)){best=distance;nearest=int(n);}}
      if(nearest<0){attached=false;break;}link(w,outside,unsigned(nearest));if(!exit)site.entrance=pack(door);
    }
    if(!attached){w.targets.resize(oldTargets);w.interiorNavNodes.resize(oldNodes);w.interiorNavLinks.resize(oldLinks);w.interiorRegions.resize(oldRegions);
      rollback();continue;}
    int entry=cells[size_t((rows-1)*2)];std::vector<int> steps(site.rooms.size(),-1);steps[size_t(entry)]=0;
    std::queue<int> pending;pending.push(entry);
    while(!pending.empty()){int here=pending.front();pending.pop();for(auto e:site.links){int next=e[0]==here?e[1]:e[1]==here?e[0]:-1;
      if(next>=0&&steps[size_t(next)]<0){steps[size_t(next)]=steps[size_t(here)]+1;pending.push(next);}}}
    int deepest=int(std::max_element(steps.begin(),steps.end())-steps.begin()),cache=-1;
    for(int i=0;i<rooms;++i)if(i!=entry&&i!=deepest&&(cache<0||steps[size_t(i)]>steps[size_t(cache)]))cache=i;
    site.rooms[size_t(entry)].kind=0;site.rooms[size_t(cache)].kind=3;site.rooms[size_t(deepest)].kind=4;
    core(w,site);result.push_back(std::move(site));
  }
  return result;
}
}
