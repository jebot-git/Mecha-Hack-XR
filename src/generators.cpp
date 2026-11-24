#include "simulation.hpp"
#include <algorithm>
namespace iron {
void World::stepGenerators(float dt){
    stepContinuousProceduralReinforcements(*this,dt);
    for(auto& g:generators){
        if(g.prototype.ai.attachment>=0){auto& parent=targets.at(g.prototype.ai.attachment);if(parent.health<=0)continue;
            g.origin=parent.origin;g.prototype.origin=parent.origin+rotate(angles(parent.yaw),g.prototype.ai.attachmentOffset);
            g.prototype.ai.spawn=g.prototype.origin;updateEnemyBounds(g.prototype);}
        if(!g.remaining)continue;
        if(proceduralLayout&&g.prototype.proceduralZone>=0){
            if(missionStatus!=MissionStatus::Active)continue;
            size_t zone=size_t(g.prototype.proceduralZone);
            if(zone>=procedural.objectives.size()||procedural.objectives[zone].complete){g.remaining=0;continue;}
            auto center=g.prototype.center,half=g.prototype.half;bool blocked=false;
            if(nearestPlayerRange(g.origin)<40)blocked=true;
            for(const auto& t:targets)if(t.active&&t.health>0&&!t.escaped&&std::abs(t.center.x-center.x)<t.half.x+half.x+1&&std::abs(t.center.y-center.y)<t.half.y+half.y+1&&std::abs(t.center.z-center.z)<t.half.z+half.z+1)blocked=true;
            if(!groundPassable(g.origin,std::hypot(half.x,half.z)))blocked=true;
            if(blocked)continue; // no ticket spent, and never materialize inside a pilot/body
        }
        unsigned bit=(g.flags>>16)&31;
        if(game&&(g.flags&8)&&(missionFlags&(1u<<bit)))continue;
        if((g.flags&2)&&(missionFlags&(1u<<bit)))continue;
        if((g.flags&4)&&!(missionFlags&(1u<<bit)))continue;
        if(!triggerReady(g.trigger))continue;
        if(g.range>0&&nearestPlayerRange(g.origin)>g.range)continue;
        auto free=std::find_if(g.slots.begin(),g.slots.end(),[&](size_t i){return !targets[i].active||(targets[i].health<=0&&targets[i].processed);});
        if(free==g.slots.end())continue;
        g.clock-=dt;if(g.clock>0)continue;
        targets[*free]=g.prototype;targets[*free].ai.attachment=-1;for(auto& f:cargoFlights)if(f.carrier==*free)f.passes=g.prototype.ai.words[5];--g.remaining;
        g.clock=std::max(.1f,g.interval);
        if((g.flags&1)&&g.burstSize&&--g.burst==0){g.clock=std::max(.1f,g.burstInterval);g.burst=g.burstSize;}
        if(!game&&mission==1&&g.prototype.spawnType==3)++counters[0];
    }
}
}
