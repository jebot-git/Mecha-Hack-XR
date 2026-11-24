#include "rpg/enemy_navigation.hpp"
#include <algorithm>
#include <limits>
#include <queue>
namespace iron::rpg {
namespace {
float planar(Vec3 p){return std::hypot(p.x,p.z);}
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
bool intersects(Vec3 a,Vec3 b,Vec3 c,Vec3 h){
  float entryFraction=0,exitFraction=1;auto d=b-a;
  float start[]={a.x,a.y,a.z},delta[]={d.x,d.y,d.z},center[]={c.x,c.y,c.z},half[]={h.x,h.y,h.z};
  for(int k=0;k<3;++k){if(std::abs(delta[k])<1e-6f){if(std::abs(start[k]-center[k])>half[k])return false;}
    else {float x=(center[k]-half[k]-start[k])/delta[k],y=(center[k]+half[k]-start[k])/delta[k];
      if(x>y)std::swap(x,y);
      entryFraction=std::max(entryFraction,x);exitFraction=std::min(exitFraction,y);if(entryFraction>exitFraction)return false;}}
  return true;
}
Vec3 grounded(const World &w,const Target &t,Vec3 p){p.y=w.walkableHeight(p)+t.ai.localHalf.y-t.ai.localCenter.y;return p;}
void beginRoute(const World &w,size_t actor,Vec3 destination,bool airborne,EnemyNavigationState &state){
  const auto &body=w.targets[actor];auto from=body.origin;
  if(!airborne)destination=grounded(w,body,destination);else destination.y=from.y;
  state.searchNodes={from,destination};auto &nodes=state.searchNodes;
  // Existing floor/door/ramp nodes preserve connectivity in multilevel spaces.
  // Nearest-to-corridor order keeps this graph bounded on very large maps.
  auto candidates=w.interiorNavNodes;
  std::stable_sort(candidates.begin(),candidates.end(),[&](Vec3 a,Vec3 b){return length(a-from)+length(a-destination)<length(b-from)+length(b-destination);});
  for(auto p:candidates){if(nodes.size()>=98)break;if(!airborne)p=grounded(w,body,p);else p.y=from.y;nodes.push_back(p);}
  std::vector<size_t> obstacles;
  for(size_t i=0;i<w.targets.size();++i){const auto &t=w.targets[i];if(i==actor||!t.active||t.health<=0||t.escaped||t.spawnType>=0)continue;
    auto expansion=Vec3{std::max(.3f,body.half.x*.65f)+12,body.half.y,std::max(.3f,body.half.z*.65f)+12};
    if(intersects(from+(body.center-body.origin),destination+(body.center-body.origin),t.center,t.half+expansion))obstacles.push_back(i);}
  std::stable_sort(obstacles.begin(),obstacles.end(),[&](size_t a,size_t b){return length(w.targets[a].center-from)<length(w.targets[b].center-from);});
  // Corners provide an exterior route even where no authored nav graph exists.
  for(size_t n=0;n<std::min(size_t(16),obstacles.size());++n){const auto &t=w.targets[obstacles[n]];
    float x=t.half.x+std::max(.3f,body.half.x*.65f)+1,z=t.half.z+std::max(.3f,body.half.z*.65f)+1;
    for(int a:{-1,1})for(int b:{-1,1}){Vec3 p{t.center.x+a*x,from.y,t.center.z+b*z};if(!airborne)p=grounded(w,body,p);nodes.push_back(p);}}
  const size_t count=nodes.size();state.searchCosts.assign(count,std::numeric_limits<float>::infinity());
  state.searchPrevious.assign(count,-1);state.searchClosed.assign(count,false);
  state.searchOpen={{length(destination-from),0}};state.searchCosts[0]=0;
  state.searchNeighbors.clear();state.searchCurrent=-1;state.searchEdgeSample=0;state.searchCursor=0;state.searching=true;state.searchFallback=false;
}
void advanceRoute(const World &w,size_t actor,bool airborne,EnemyNavigationState &s,EnemyNavigationBudget &budget){
  auto &nodes=s.searchNodes;int checks=0,samplesUsed=0;
  auto finish=[&]{s.searching=false;s.remaining=2.5f+.025f*float(actor%11);};
  // Four resumed edge chunks and 64 floor samples per actor, also constrained by
  // the host's shared quota. No wall-clock timing influences path decisions.
  while(s.searching&&checks<4&&budget.edgeChecks>0){
    if(s.searchCurrent<0){
      while(!s.searchOpen.empty()&&s.searchClosed[s.searchOpen.front().second]){
        std::pop_heap(s.searchOpen.begin(),s.searchOpen.end(),std::greater<>());s.searchOpen.pop_back();}
      if(s.searchOpen.empty()){
        if(s.searchFallback){finish();break;}
        // A sparse first pass is responsive; retry full connectivity only when
        // those corridors fail, still under exactly the same work quotas.
        s.searchFallback=true;s.searchCosts.assign(nodes.size(),std::numeric_limits<float>::infinity());s.searchCosts[0]=0;
        s.searchPrevious.assign(nodes.size(),-1);s.searchClosed.assign(nodes.size(),false);
        s.searchOpen={{length(nodes[1]-nodes[0]),0}};s.searchCurrent=-1;s.searchEdgeSample=0;continue;
      }
      size_t here=s.searchOpen.front().second;std::pop_heap(s.searchOpen.begin(),s.searchOpen.end(),std::greater<>());s.searchOpen.pop_back();
      s.searchClosed[here]=true;
      if(here==1){
        std::vector<Vec3> path;for(int at=1;at>0;at=s.searchPrevious[size_t(at)])path.push_back(nodes[size_t(at)]);
        std::reverse(path.begin(),path.end());s.path=std::move(path);s.cursor=0;finish();break;
      }
      s.searchCurrent=int(here);s.searchNeighbors.clear();
      for(size_t n=1;n<nodes.size();++n)if(n!=here&&!s.searchClosed[n])s.searchNeighbors.push_back(n);
      std::stable_sort(s.searchNeighbors.begin(),s.searchNeighbors.end(),[&](size_t a,size_t b){return length(nodes[a]-nodes[here])<length(nodes[b]-nodes[here]);});
      if(!s.searchFallback&&s.searchNeighbors.size()>12)s.searchNeighbors.resize(12);
      auto append=[&](size_t n){if(n!=here&&!s.searchClosed[n]&&std::find(s.searchNeighbors.begin(),s.searchNeighbors.end(),n)==s.searchNeighbors.end())s.searchNeighbors.push_back(n);};
      // Preserve authored door/ramp links even when crowded nearest neighbors
      // would otherwise omit their farther endpoint. All links are validated.
      for(auto edge:w.interiorNavLinks){if(edge[0]>=w.interiorNavNodes.size()||edge[1]>=w.interiorNavNodes.size())continue;
        auto a=w.interiorNavNodes[edge[0]],b=w.interiorNavNodes[edge[1]];Vec3 other{};bool linked=false;
        if(length(a-nodes[here])<1.f){other=b;linked=true;}else if(length(b-nodes[here])<1.f){other=a;linked=true;}
        if(linked)for(size_t n=1;n<nodes.size();++n)if(length(nodes[n]-other)<1.f)append(n);
      }
      // An unobstructed goal should complete immediately, before inspecting
      // irrelevant nearby corners. Long goal edges resume in bounded chunks.
      if(!s.searchClosed[1]){auto goal=std::find(s.searchNeighbors.begin(),s.searchNeighbors.end(),size_t(1));if(goal!=s.searchNeighbors.end())s.searchNeighbors.erase(goal);s.searchNeighbors.insert(s.searchNeighbors.begin(),1);}
      s.searchCursor=0;s.searchEdgeSample=0;
    }
    if(s.searchCursor>=s.searchNeighbors.size()){s.searchCurrent=-1;continue;}
    size_t here=size_t(s.searchCurrent),next=s.searchNeighbors[s.searchCursor];
    float step=length(nodes[next]-nodes[here]);
    if(s.searchClosed[next]||s.searchCosts[here]+step>=s.searchCosts[next]){++s.searchCursor;s.searchEdgeSample=0;continue;}
    // Conservative 2.5m accounting guarantees a <=3m passage query never uses
    // more terrain samples than charged, even with floating-point rounding.
    int total=std::max(1,int(std::ceil(planar(nodes[next]-nodes[here])/(w.interiorDecks.empty()?2.5f:1.95f))));
    int take=std::min({64-samplesUsed,budget.terrainSamples,total-s.searchEdgeSample});
    if(take<=0)break;
    auto delta=nodes[next]-nodes[here];
    Vec3 from=nodes[here]+delta*(float(s.searchEdgeSample)/total),to=nodes[here]+delta*(float(s.searchEdgeSample+take)/total);
    if(!airborne)from=grounded(w,w.targets[actor],from);
    ++checks;--budget.edgeChecks;samplesUsed+=take;budget.terrainSamples-=take;
    if(!enemyNavigationPassage(w,actor,from,to,airborne)){++s.searchCursor;s.searchEdgeSample=0;continue;}
    s.searchEdgeSample+=take;
    if(s.searchEdgeSample<total)continue;
    ++s.searchCursor;s.searchEdgeSample=0;
    s.searchCosts[next]=s.searchCosts[here]+step;s.searchPrevious[next]=int(here);
    s.searchOpen.emplace_back(s.searchCosts[next]+length(nodes[next]-nodes[1]),next);
    std::push_heap(s.searchOpen.begin(),s.searchOpen.end(),std::greater<>());
  }
}
}
bool enemyNavigationPassage(const World &w,size_t actor,Vec3 from,Vec3 to,bool airborne,bool dynamicActors){
  if(actor>=w.targets.size()||!finite(from)||!finite(to))return false;
  const auto &body=w.targets[actor];Vec3 half{std::max(.3f,body.half.x*.65f),body.half.y,std::max(.3f,body.half.z*.65f)};
  auto offset=body.center-body.origin;float radius=std::max(.5f,std::min(body.half.x,body.half.z)*.65f);
  std::vector<size_t> obstacles;
  const auto start=from+offset,end=to+offset;
  const float minX=std::min(start.x,end.x)-half.x,maxX=std::max(start.x,end.x)+half.x;
  const float minZ=std::min(start.z,end.z)-half.z,maxZ=std::max(start.z,end.z)+half.z;
  for(size_t k=0;k<w.targets.size();++k){
    const auto &t=w.targets[k];
    if(k==actor||!t.active||t.health<=0||t.escaped||t.ai.parent==int(actor)||body.ai.parent==int(k)||(!dynamicActors&&t.spawnType>=0))continue;
    // A cheap horizontal broad phase removes distant scenery before testing
    // the swept corridor. The slab test still decides diagonal edge cases.
    if(t.center.x+t.half.x<minX-.001f||t.center.x-t.half.x>maxX+.001f||
       t.center.z+t.half.z<minZ-.001f||t.center.z-t.half.z>maxZ+.001f)continue;
    if(!airborne&&body.ai.rpgCrushing&&t.rpgDecoration&&w.crushable(t))continue;
    Vec3 broad=t.half+half;broad.y=1e9f;
    if(intersects(start,end,t.center,broad))obstacles.push_back(k);
  }
  int samples=std::max(1,int(std::ceil(planar(to-from)/(w.interiorDecks.empty()?3.f:2.f))));float oldHeight=w.walkableHeight(from);
  Vec3 previous=from;
  for(int n=1;n<=samples;++n){auto p=from+(to-from)*(float(n)/samples);
    if(p.x-half.x<w.movementBounds[0]||p.x+half.x>w.movementBounds[2]||p.z-half.z<w.movementBounds[1]||p.z+half.z>w.movementBounds[3])return false;
    if(!airborne){
      p.y=oldHeight;
      const float floor=w.walkableHeight(p);
      p.y=floor;
      if(!w.groundPassable(p,radius))return false;
      if(std::abs(floor-oldHeight)>.6f+.6f*planar(p-previous))return false;
      oldHeight=floor;p=grounded(w,body,p);
      if(n==samples&&!w.interiorDecks.empty()&&std::abs(w.walkableHeight(to)-floor)>1.2f)return false;
      if(w.maxWadeDepth>0){float limit=std::min(w.maxWadeDepth,body.ai.boundsReady?body.ai.localHalf.y:body.half.y);
        if(w.waterTraversalLimitAt(p,radius)>w.maxWadeDepth&&body.ai.localHalf.y>=3)limit=w.waterTraversalLimitAt(p,radius);
        if(w.wadingDepthAt({p.x,floor,p.z},radius)>limit+.001f)return false;}
    }
    if(p.y+offset.y+half.y>w.interiorCeilingAt(p,radius)-.1f)return false;
    for(size_t k:obstacles){const auto &t=w.targets[k];
      Vec3 inflated=t.half+half;
      // Match moveEnemy's overlap escape rule at the true start of a sweep.
      if(intersects(from+offset,from+offset,t.center,inflated))continue;
      if(intersects(previous+offset,p+offset,t.center,inflated))return false;}
    previous=p;
  }
  return true;
}
Vec3 enemyNavigationDirection(const World &w,size_t actor,Vec3 desired,Vec3 destination,float speed,float dt,bool airborne,EnemyNavigationState &state,EnemyNavigationBudget *sharedBudget){
  if(actor>=w.targets.size()||!finite(desired)||!finite(destination)||!std::isfinite(speed)||speed<=0||!std::isfinite(dt)||dt<=0)return {};
  desired.y=0;if(planar(desired)<.0001f){state.path.clear();state.searching=false;return {};}
  const auto &body=w.targets[actor];auto from=body.origin;state.remaining-=dt;state.recoveryClock-=dt;
  bool changed=length(destination-state.destination)>8;
  const bool floorChange=!w.interiorDecks.empty()&&!airborne&&std::abs(destination.y-from.y)>5.f;
  bool blocked=body.ai.blocked>.25f&&state.recoveryClock<=0;
  // Doctrines own free-space motion. Build obstacle routes only when their
  // short look-ahead actually meets static geometry, not for ordinary strafing.
  auto ahead=[&](Vec3 heading,float distance){
    auto p=from+heading*distance;
    if(!airborne&&!w.interiorDecks.empty())p.y=w.walkableHeight(p,.35f*distance+.5f);
    return p;
  };
  bool direct=enemyNavigationPassage(w,actor,from,ahead(unit(desired),5.f),airborne,false);
  if(changed){state.path.clear();state.cursor=0;state.searching=false;state.destination=destination;state.remaining=0;}
  if((!direct||floorChange)&&!state.searching&&(state.remaining<=0||blocked)){
    if(blocked)state.recoveryClock=1;
    state.destination=destination;state.remaining=2.5f+.025f*float(actor%11);
    beginRoute(w,actor,destination,airborne,state);
  }
  EnemyNavigationBudget localBudget;
  if(state.searching)advanceRoute(w,actor,airborne,state,sharedBudget?*sharedBudget:localBudget);
  while(state.cursor<state.path.size()&&length(state.path[state.cursor]-from)<1.f)++state.cursor;
  // Revalidate cached next edge; destroyed/new cover must not leave stale paths.
  if(state.cursor+1<state.path.size()){
    auto waypoint=state.path[state.cursor];state.waypoint=waypoint;
    auto toward=unit(Vec3{waypoint.x-from.x,0,waypoint.z-from.z});
    if(enemyNavigationPassage(w,actor,from,ahead(toward,std::min(5.f,planar(waypoint-from))),airborne,false)){desired=toward;}
    else {state.remaining=0;state.path.clear();}
  }
  desired=unit(desired);float travel=std::max(.5f,speed*std::min(dt,.25f));
  // Deterministic reciprocal side preference prevents every unit turning into
  // the same jamb. Keep a chosen side during recovery to avoid frame oscillation.
  if(!state.side)state.side=actor%2?1:-1;
  // The aligned candidate has the maximum possible score. Most actors are in
  // open lanes, so avoid constructing eleven lower-scoring sweeps every tick.
  if(enemyNavigationPassage(w,actor,from,ahead(desired,std::max(travel,4.f)),airborne,true))return desired;
  const float angles[]={0,.35f,-.35f,.7f,-.7f,1.15f,-1.15f,1.57f,-1.57f,2.2f,-2.2f,pi};
  Vec3 best{};float bestScore=-1e9f;
  for(float angle:angles){angle*=float(state.side);float c=std::cos(angle),s=std::sin(angle);Vec3 d{desired.x*c-desired.z*s,0,desired.x*s+desired.z*c};
    if(!enemyNavigationPassage(w,actor,from,ahead(d,travel),airborne,true))continue;
    float score=dot(d,desired)*3.f;
    // Prefer clearance farther along an otherwise legal immediate sweep.
    if(enemyNavigationPassage(w,actor,from,ahead(d,std::max(travel,4.f)),airborne,true))score+=1;
    score-=std::abs(angle)*.025f;if(score>bestScore){bestScore=score;best=d;}}
  return best;
}
}
