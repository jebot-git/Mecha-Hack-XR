#include "rpg/session.hpp"
#include "rpg/world_generation.hpp"
#include "rpg/original_assets.hpp"
#include <iostream>
#include <stdexcept>
using namespace iron;
using namespace iron::rpg;
namespace {
void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
void walk(World &world,Vec3 from,Vec3 to){
    world.position=from;
    auto d=to-from;d.y=0;
    const int steps=int(std::ceil(length(d)));
    for(int i=0;i<steps;++i)world.movePilot(world.position+d*(1.f/steps));
    require(length(world.position-to)<1.f,"A mech cannot walk the complete crossing");
}
bool crossing(World &w){
    if(w.interiorDecks.size()!=3)return false;
    const auto &a=w.interiorDecks[0],&b=w.interiorDecks[1],&c=w.interiorDecks[2];
    return a.end.y>a.start.y+25&&std::abs(b.start.y-b.end.y)<.01f&&c.start.y>c.end.y+25;
}
void check(World &w,bool wet){
    const auto &enter=w.interiorDecks[0],&span=w.interiorDecks[1],&exit=w.interiorDecks[2];
    require(length(enter.end-span.start)<.001f&&length(span.end-exit.start)<.001f,"Disconnected road deck");
    auto centre=(span.start+span.end)*.5f;
    auto floor=centre;floor.y=w.surfaceHeight(floor.x,floor.z);
    require((w.waterDepthAt(floor)>1)==wet,"Wrong crossing context");
    if(wet) {
        require(w.waterMovementScale(centre,3.5f)==1.f,"Dry bridge deck incurred wading penalty");
        require(w.waterMovementScale(floor,3.5f)<1.f,"Canal beneath the bridge lost water resistance");
    }
    require(w.interiorCeilingAt(floor)-floor.y>=32,"Crossing has insufficient headroom");
    require(w.deckContact(centre+Vec3{0,4,0},centre-Vec3{0,4,0})<1,"Falling projectiles pass through the road");
    require(w.deckContact(centre-Vec3{0,4,0},centre+Vec3{0,4,0})<1,"Rising projectiles pass through the road");
    auto direction=unit(span.end-span.start);Vec3 across{-direction.z,0,direction.x};
    auto low=floor-across*32,far=floor+across*32;
    low.y=w.surfaceHeight(low.x,low.z);far.y=w.surfaceHeight(far.x,far.z);
    walk(w,low,far);
    for(const auto &deck:w.interiorDecks)walk(w,deck.start,deck.end);
    for(auto it=w.interiorDecks.rbegin();it!=w.interiorDecks.rend();++it)walk(w,it->end,it->start);
    // Both interfaces to the at-grade network must be usable after dressing.
    int connectedApproaches=0;
    for(auto [a,b]:w.interiorNavLinks){
        const auto &p=w.interiorNavNodes[a],&q=w.interiorNavNodes[b];
        if(length(q-enter.start)<.01f||length(p-exit.end)<.01f){
            walk(w,p,q);++connectedApproaches;
        }
    }
    require(connectedApproaches==2,"Missing crossing network links");
}
}
int main(){try{
    setOriginalAssetRoot(MECHA_ASSETS);
    for(auto [seed,location]:{std::pair{0u,0},std::pair{3u,6}}) {
        World w,repeat;
        buildMissionEnvironment(w,seed,3,1000,{},location,0,0,5,-1,2);
        buildMissionEnvironment(repeat,seed,3,1000,{},location,0,0,5,-1,2);
        require(crossing(w)&&crossing(repeat),"Missing contextual bridge or overpass");
        for(size_t i=0;i<w.interiorDecks.size();++i) {
            const auto &a=w.interiorDecks[i],&b=repeat.interiorDecks[i];
            require(a.bounds==b.bounds&&length(a.start-b.start)<.001f&&
                    length(a.end-b.end)<.001f,"Peers derived different crossing geometry");
        }
        check(w,location==0);
    }
    std::cout<<"Canal and road crossings support connected approaches, two-way mech travel, lower passage and projectile contact\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
