#include "simulation.hpp"
#include "searchlight.hpp"
#include <algorithm>
#include <limits>
namespace iron {
namespace {
float planar(Vec3 p){return std::hypot(p.x,p.z);}
float squareRange(Vec3 p){return std::max(std::abs(p.x),std::abs(p.z));}
float bearing(Vec3 p){return std::atan2(-p.x,-p.z);}
float turn(float a,float b,float limit){return wrap(a+std::clamp(wrap(b-a),-limit,limit));}
float segment(Vec3 a,Vec3 b,Vec3 center,Vec3 half){
    float near=0,far=1;auto d=b-a,lo=center-half,hi=center+half;
    float starts[]={a.x,a.y,a.z},rays[]={d.x,d.y,d.z},lows[]={lo.x,lo.y,lo.z},highs[]={hi.x,hi.y,hi.z};
    for(int k=0;k<3;++k){if(std::abs(rays[k])<1e-6f){if(starts[k]<lows[k]||starts[k]>highs[k])return 2;}
        else{float x=(lows[k]-starts[k])/rays[k],y=(highs[k]-starts[k])/rays[k];if(x>y)std::swap(x,y);near=std::max(near,x);far=std::min(far,y);if(near>far)return 2;}}
    return near;
}
void state(Target& t,AIState next,float time=0){if(t.ai.state!=next||time!=0)t.ai.stateTime=time;t.ai.state=next;}
void updateTrackAttitude(const World& world,Target& target,float dt){
    auto &ai=target.ai;
    const float gauge=std::max(.45f,ai.localHalf.x*.8f);
    const float wheelbase=std::max(.6f,ai.localHalf.z*.72f);
    const float sole=ai.localCenter.y-ai.localHalf.y;
    const auto yaw=angles(target.yaw);
    const float clearance=.5f+.65f*std::hypot(gauge,wheelbase);
    auto support=[&](float x,float z){
        return world.walkableHeight(target.origin+rotate(yaw,{x,sole,z}),clearance);
    };
    // Average fore/aft and left/right contact pairs so one track can climb
    // a bank without tilting the chassis toward its turret target.
    const float frontLeft=support(-gauge,-wheelbase),frontRight=support(gauge,-wheelbase);
    const float rearLeft=support(-gauge,wheelbase),rearRight=support(gauge,wheelbase);
    const float pitch=std::atan2(frontLeft+frontRight-rearLeft-rearRight,4*wheelbase);
    const float roll=std::atan2(frontRight+rearRight-frontLeft-rearLeft,4*gauge);
    const float settle=1.f-std::exp(-12.f*dt);
    ai.pitch+=(std::clamp(pitch,-.40f,.40f)-ai.pitch)*settle;
    ai.bank+=(std::clamp(roll,-.36f,.36f)-ai.bank)*settle;
}
}
bool World::triggerReady(unsigned trigger) const {
    unsigned mode=trigger>>14,arg=trigger&0x3fff;
    return mode==0?elapsed>=arg:mode==1?bool(missionFlags&(1u<<(arg&31))):!(missionFlags&(1u<<(arg&31)));
}
void World::updateEnemyBounds(Target& t){
    auto& a=t.ai;
    if(!a.boundsReady){
        if(t.mesh&&!t.mesh->empty()){Vec3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
            for(auto& v:*t.mesh){auto p=v.position;lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}a.localCenter=(lo+hi)*.5f;a.localHalf=(hi-lo)*.5f;
        }else{a.localCenter=rotate(angles(-t.yaw),t.center-t.origin);a.localHalf=t.half;}
        a.boundsReady=true;
    }
    bool proceduralGround=proceduralTerrain&&t.spawnType>=0&&t.spawnType!=1&&t.spawnType!=7&&t.spawnType!=9&&t.spawnType!=12&&t.spawnType!=18;
    if((t.spawnType==0||t.spawnType==3||t.spawnType==14||proceduralGround)&&t.mesh&&!t.mesh->empty()&&!a.airborne&&!a.gravity.active()){
        const float sole=a.localCenter.y-a.localHalf.y;
        const Vec3 feet=t.origin+Vec3{0,sole,0};
        const float floor=a.rpgFallEnabled?pilotSupportHeight(feet):
            rpgPrototype?walkableHeight(feet):surfaceHeight(feet.x,feet.z);
        if(!a.rpgFallEnabled||(a.fallSpeed==0&&feet.y-floor<=.85f))t.origin.y=floor-sole;
    }
    if(a.gravity.active()){
        const auto frame=compose(a.gravity.frame,angles(t.yaw,a.pitch,a.bank));
        t.center=t.origin+rotate(frame,a.localCenter);
        t.half={};
        for(auto axis:{rotate(frame,{a.localHalf.x,0,0}),rotate(frame,{0,a.localHalf.y,0}),rotate(frame,{0,0,a.localHalf.z})})
            t.half=t.half+Vec3{std::abs(axis.x),std::abs(axis.y),std::abs(axis.z)};
        return;
    }
    t.center=t.origin+rotate(angles(t.yaw),a.localCenter);
    float c=std::abs(std::cos(t.yaw)),s=std::abs(std::sin(t.yaw));t.half={c*a.localHalf.x+s*a.localHalf.z,a.localHalf.y,s*a.localHalf.x+c*a.localHalf.z};
}
void World::resetEnemy(Target& t){
    auto& a=t.ai;t.origin=a.spawn;t.yaw=a.spawnYaw;t.health=a.maxHealth;t.processed=false;t.escaped=false;t.waypoint=0;t.attackClock=0;
    a.state=AIState::Dormant;a.playerTarget=-1;a.retarget=0;a.stateTime=0;a.alert=0;a.lastHealth=t.health;a.weaponYaw=t.yaw;a.weaponPitch=0;a.velocity={};a.gaitPhase=0;a.bank=a.pitch=a.blocked=a.secondClock=0;
    a.burst=0;a.secondBurst=0;a.rocketUsed=false;a.registered=false;a.passes=t.spawnType==7?a.words[3]:0;a.spawnDelay=t.spawnType==9?float(a.words[3]):0;updateEnemyBounds(t);
}
void World::retireEnemy(Target& t,bool escaped){
    if(!assault&&!game&&t.active&&t.spawnType==3&&mission==1)counters[0]=std::max(0,counters[0]-1);
    t.active=false;t.escaped=escaped;t.ai.state=AIState::Finished;
    if(t.ai.identity>0&&t.ai.identity<32&&targetSlots[t.ai.identity]>=0&& &targets[targetSlots[t.ai.identity]]==&t){targetSlots[t.ai.identity]=-1;missionFlags&=~(1u<<t.ai.identity);}
    // $68FC refunds a generated actor removed offscreen, but never a killed actor.
    if(t.generated&&t.ai.generator>=0&&t.health>0)++generators.at(t.ai.generator).remaining;
    if(!t.generated&&!escaped){t.spawnFlags&=~1u;resetEnemy(t);}
}
Vec3 World::enemyDestination(const Target& t,int& victim) const {
    // Assault supports choose opposing pilots, supports or defended scenery.
    // Campaign target-slot directives must not make converted allies attack allies.
    if(assault&&t.team!=Team::Neutral){
        victim=-1;float best=1e9f;Vec3 destination=t.center;
        for(size_t i=0;i<teammates.size();++i){auto p=teammates[i];if(p->team==t.team||p->lifeSpent||p->health<=0)continue;
            Pilot observer;observer.position=t.center-Vec3{0,6,0};if(!visibilityInRange(*this,observer,p->position+Vec3{0,6,0},searchlightActive(*p)))continue;
            float d=length(p->position-t.center);if(d<best){best=d;victim=-2-int(i);destination=p->position+Vec3{0,6,0};}}
        for(size_t i=0;i<targets.size();++i){auto& other=targets[i];if(other.team==Team::Neutral||other.team==t.team||!other.active||other.health<=0||other.escaped)continue;
            if(other.spawnType>=0&&visibilityRules()&&length(other.center-t.center)>visibilityRanges(*this).base)continue;
            float d=length(other.center-t.center);if(d<best){best=d;victim=int(i);destination=other.center;}}
        return destination;
    }
    victim=-1;unsigned slot=unsigned(t.ai.targetSlot);
    if(slot>0&&slot<targetSlots.size()){int i=targetSlots[slot];if(i>=0&&size_t(i)<targets.size()){auto& target=targets[i];if(target.active&&target.health>0&&!target.escaped){victim=i;return target.center;}}}
    if(cooperative&&t.ai.playerTarget>=0&&size_t(t.ai.playerTarget)<teammates.size()){auto p=teammates[t.ai.playerTarget];if(!p->lifeSpent&&p->health>0){victim=-2-t.ai.playerTarget;return p->position+Vec3{0,6,0};}}
    return position+Vec3{0,6,0};
}
bool World::enemyLineOfSight(Vec3 from,Vec3 to,int owner,int victim) const {
    if(visibilityRules()&&groundContact(from,to)<.999f)return false;
    for(size_t i=0;i<targets.size();++i){auto& t=targets[i];if(int(i)==owner||int(i)==victim||!t.active||t.health<=0||t.escaped)continue;
        if(t.spawnType>=0&&t.spawnType!=8&&t.spawnType!=11)continue;
        if(segment(from,to,t.center,t.half)<.999f)return false;
    }return true;
}
bool World::moveEnemy(size_t index,Vec3 displacement,float dt,bool flying,bool crushing,bool tracked){
    auto& t=targets[index];
    if(!flying&&maxWadeDepth>0)displacement=displacement*waterMovementScale(t.origin,std::min(t.half.x,t.half.z)*.65f);
    Vec3 next=t.origin+displacement;bool clear=true;
    if(proceduralTerrain&&!flying&&t.spawnType!=9){
        int steps=std::max(1,int(std::ceil(planar(displacement))));float radius=std::max(.5f,std::min(t.half.x,t.half.z)*.65f);
        float wadeLimit=std::min(maxWadeDepth,t.ai.boundsReady?t.ai.localHalf.y:t.half.y);
        const float footOffset=t.ai.localHalf.y-t.ai.localCenter.y;
        auto supportAt=[&](Vec3 feet){return t.ai.rpgFallEnabled?pilotSupportHeight(feet):walkableHeight(feet);};
        float floor=supportAt(t.origin-Vec3{0,footOffset,0});
        for(int n=1;n<=steps;++n){auto sample=t.origin+displacement*(float(n)/steps);
            sample.y=floor;
            const float support=supportAt(sample);
            // Keep the feet at the lip while crossing a drop. The session's
            // vertical integrator takes over once the hull clears its support.
            if(t.ai.rpgFallEnabled&&floor-support>1.f){
                if(sample.x-radius<movementBounds[0]||sample.z-radius<movementBounds[1]||
                   sample.x+radius>movementBounds[2]||sample.z+radius>movementBounds[3]){clear=false;break;}
                continue;
            }
            sample.y=support;
            if((!interiorDecks.empty()&&(std::abs(support-floor)>1.f||
               deckContact(sample+Vec3{0,.15f,0},sample+Vec3{0,2*t.half.y,0})<=1.f))||
               !groundPassable(sample,radius,t.ai.rpgFallEnabled)||(maxWadeDepth>0&&wadingDepthAt(sample,radius)>(waterTraversalLimitAt(sample,radius)>maxWadeDepth&&t.ai.localHalf.y>=3?waterTraversalLimitAt(sample,radius):wadeLimit)+.001f)){clear=false;break;}
            floor=support;
        }
        next.y=floor+footOffset;displacement=next-t.origin;
    }
    auto half=t.half;half.x=std::max(.3f,half.x*.65f);half.z=std::max(.3f,half.z*.65f);
    const auto end=t.center+displacement;
    const float minX=std::min(t.center.x,end.x)-half.x,maxX=std::max(t.center.x,end.x)+half.x;
    const float minZ=std::min(t.center.z,end.z)-half.z,maxZ=std::max(t.center.z,end.z)+half.z;
    for(size_t i=0;i<targets.size();++i){auto& obstacle=targets[i];if(i==index||!obstacle.active||obstacle.health<=0||obstacle.escaped||obstacle.ai.parent==int(index)||t.ai.parent==int(i))continue;
        if(obstacle.center.x+obstacle.half.x<minX-.001f||
           obstacle.center.x-obstacle.half.x>maxX+.001f||
           obstacle.center.z+obstacle.half.z<minZ-.001f||
           obstacle.center.z-obstacle.half.z>maxZ+.001f)continue;
        if(crushing&&!flying&&obstacle.rpgDecoration&&crushable(obstacle))continue;
        if(t.spawnType==9&&obstacle.spawnType!=9)continue; // ships occupy authored water lanes
        Vec3 inflated=obstacle.half+half;
        // An actor initially touching scenery may move out; never tunnel into another building.
        if(segment(t.center,t.center,obstacle.center,inflated)==0)continue;
        if(segment(t.center,end,obstacle.center,inflated)<=1){clear=false;break;}
    }
    if((!flying||rpgPrototype)&&t.spawnType!=9){
        auto block=[&](Pilot* p){
            // Low-hover RPG enemies share the standing mecha's space; aircraft
            // whose bounds clear its head may still fly overhead.
            if((flying||!interiorDecks.empty())&&(t.center.y+displacement.y-t.half.y>=p->position.y+12.f||
                        t.center.y+displacement.y+t.half.y<=p->position.y))return;
            if(!p->lifeSpent&&p->health>0&&planar(next-p->position)<2.5f&&planar(t.origin-p->position)>=2.5f)clear=false;
        };
        if(cooperative)for(auto p:teammates)block(p);else block(this);
    }
    if(clear){
        t.origin=next;t.ai.velocity=displacement*(1/std::max(dt,.00001f));
        if(!flying){
            const float travel=planar(displacement),sign=dot(displacement,direction(t.yaw,0))>=0?1.f:-1.f;
            t.ai.gaitPhase=std::fmod(std::fmod(t.ai.gaitPhase+sign*travel*(100.f/96),18.f)+18.f,18.f);
            if(tracked)updateTrackAttitude(*this,t,dt);
        }
        t.ai.blocked=0;updateEnemyBounds(t);
    }
    else{t.ai.blocked+=dt;t.ai.velocity={};}
    return clear;
}
void World::enemyFire(size_t index,Vec3 destination,int victim,ProjectileKind kind,float speed,float damage,float radius,Vec3 offset){
    if(projectiles.size()>=256)return;
    auto& t=targets[index];Vec3 source=t.center+rotate(angles(t.yaw),offset),delta=destination-source;float distance=length(delta);if(distance<.01f)return;
    if(mission>=0){
        // $E534 scales the $E5DC projectile damage pair. Heavy tank uses 4x second value.
        if(kind==ProjectileKind::Bullet&&t.spawnType==3)damage=difficultyValues[0];
        else if(kind==ProjectileKind::Bullet&&(t.spawnType==0||t.spawnType==1))damage=difficultyValues[1]*((t.spawnType==0&&(t.parameters&64))?4.f:1.f);
        else damage*=difficultyRatio; // native fallback for not-yet-mapped weapon routines
    }
    Projectile p{source,delta*(speed/distance),std::max(4.f,distance/std::max(1.f,speed)+2),damage,-1};
    p.kind=kind;p.weapon=kind==ProjectileKind::Bullet?Weapon::Rifle:kind==ProjectileKind::Rocket?Weapon::Rocket:Weapon::Grenade;p.owner=int(index);p.victim=victim;p.radius=radius;
    if(kind==ProjectileKind::Bomb){p.position=t.origin+Vec3{0,-.9f,0};p.velocity=t.ai.velocity;}
    projectiles.push_back(p);t.ai.muzzleFlash=.08f;
    sounds.push_back({SoundEvent::Shot,int(kind==ProjectileKind::Bullet?Weapon::Rifle:kind==ProjectileKind::Rocket?Weapon::Rocket:Weapon::Grenade),source});
}
void World::stepEnemies(float dt){
    const int game=combatGame();
    selectPlayerTargets(dt);
    for(size_t i=0;i<targets.size();++i){auto& t=targets[i];auto& a=t.ai;if(t.spawnType<0||t.health<=0||t.escaped)continue;
        if(!a.airborne)a.velocity={};
        if(t.ai.parent>=0){
            if(a.parent<0||size_t(a.parent)>=targets.size())continue;
            auto& parent=targets[a.parent];
            if(parent.health<=0||parent.escaped){t.active=false;t.escaped=true;continue;}t.active=parent.active;
            if(t.active){Vec3 delta=parent.origin-t.origin;float spacing=a.words[0]*.01f,d=planar(delta);if(d>spacing){t.yaw=turn(t.yaw,bearing(delta),dt*1.8f);t.origin=parent.origin-direction(t.yaw,0)*spacing;updateEnemyBounds(t);}}
            continue;
        }
        if(a.attachment>=0&&!t.active){auto& parent=targets.at(a.attachment);if(parent.health<=0){t.spawnFlags|=2;continue;}if(!parent.active)continue;
            t.origin=parent.origin+rotate(angles(parent.yaw),a.attachmentOffset);a.spawn=t.origin;updateEnemyBounds(t);}
        if(!t.active){if(t.generated||(t.spawnFlags&7)||!triggerReady(t.trigger)||nearestPlayerRange(t.origin)>t.activationRange)continue;
            t.active=true;a.attachment=-1;if(!assault&&!game&&mission==1&&t.spawnType==3)++counters[0];}
        if(!a.registered){a.registered=true;a.lastHealth=t.health;if(a.maxHealth==0)a.maxHealth=t.health;
            if(a.identity>0&&a.identity<32){targetSlots[a.identity]=int(i);missionFlags|=1u<<a.identity;}
            state(t,AIState::Patrol);}
        if(t.spawnType==21&&game){pickups.push_back({t.origin,int(t.parameters>>16)});retireEnemy(t,true);continue;}
        if(!a.airborne&&a.respawnRange>0&&nearestPlayerRange(t.origin)>a.respawnRange&&nearestPlayerRange(a.spawn)>t.activationRange){retireEnemy(t);continue;}
        if(a.airborne){a.velocity.y-=9.8f*dt;t.origin.y+=a.velocity.y*dt;if(t.origin.y<=a.spawn.y){t.origin.y=a.spawn.y;a.airborne=false;a.velocity={};}updateEnemyBounds(t);continue;}
        bool hit=t.health<a.lastHealth&&!(t.spawnType==0&&(t.parameters&32));a.lastHealth=t.health;a.alert=hit?4:std::max(0.f,a.alert-dt);a.stateTime-=dt;t.attackClock-=dt;a.secondClock-=dt;a.muzzleFlash=std::max(0.f,a.muzzleFlash-dt);
        int victim;Vec3 destination=enemyDestination(t,victim),delta=destination-t.center;float distance=planar(delta);
        bool visible=enemyLineOfSight(t.center,destination,int(i),victim);
        const Pilot* observed=victim<=-2&&size_t(-2-victim)<teammates.size()?teammates[size_t(-2-victim)]:victim==-1&&!assault?static_cast<const Pilot*>(this):nullptr;
        if(visibilityRules()){
            Pilot observer;observer.position=t.center-Vec3{0,6,0};
            visible=visible&&visibilityInRange(*this,observer,destination,observed&&searchlightActive(*observed));
            if(assault&&victim==-1)visible=false;
        }
        auto& w=a.words;float detect=(w[0]?w[0]:16000)*.01f;
        if(visibilityRules())detect=observed&&searchlightActive(*observed)?visibilityRanges(*this).lit:std::min(detect,visibilityRanges(*this).base);
        bool seen=(squareRange(delta)<=detect&&visible)||a.alert>0;
        if(squareRange(delta)<=detect&&visible)a.alert=4;
        auto aim=[&](float rate){a.weaponYaw=turn(a.weaponYaw,bearing(delta),rate*dt);float pitch=std::atan2(delta.y,std::max(.001f,distance));a.weaponPitch+=std::clamp(pitch-a.weaponPitch,-rate*dt,rate*dt);return std::abs(wrap(bearing(delta)-a.weaponYaw))<.10f&&std::abs(pitch-a.weaponPitch)<.10f;};
        auto walk=[&](Vec3 goal,float speed,bool flying=false){Vec3 d=goal-t.origin;float flat=planar(d);if(flat<.01f)return true;
            float desired=bearing(d);t.yaw=turn(t.yaw,desired,(t.spawnType==3?pi/2:pi/3)*dt);
            float alignment=std::max(flying?.35f:0.f,std::cos(wrap(desired-t.yaw)));
            Vec3 step=direction(t.yaw,0)*std::min(flat,speed*dt*alignment);if(flying)step.y=std::clamp(d.y,-speed*.5f*dt,speed*.5f*dt);
            if(moveEnemy(i,step,dt,flying))return true;
            // Preserve route/target while executing a bounded collision detour, then reacquire it.
            float side=((i+unsigned(a.side))&1)?1.f:-1.f;t.yaw=wrap(t.yaw+side*dt*1.8f);
            if(a.blocked>1.5f){++a.side;a.blocked=0;}
            return moveEnemy(i,direction(t.yaw+side*pi/3,0)*(speed*.5f*dt),dt,flying);
        };
        auto patrol=[&](bool terminal){
            if(t.route.empty()){a.velocity={};return;}
            auto goal=t.route[t.waypoint];if(planar(goal-t.origin)<2.5f){
                if(t.waypoint+1<t.route.size())++t.waypoint;
                else if(terminal){arrivedEnemy(t);retireEnemy(t,true);return;}
                else t.waypoint=0;
                goal=t.route[t.waypoint];
            }
            if(t.spawnType==1||t.spawnType==12||t.spawnType==18)goal.y=std::max(17.9f,a.spawn.y);
            walk(goal,t.speed,t.spawnType==1||t.spawnType==12||t.spawnType==18);
        };
        auto shootBurst=[&](unsigned count,float interval,float pause,ProjectileKind kind,float speed,float damage,float radius=0.f,Vec3 offset=Vec3{}){
            bool aligned=aim(t.spawnType==0?.8f:pi/2);
            if(!visible||t.attackClock>0||!aligned)return false;
            if(!a.burst)a.burst=std::max(1u,count);
            enemyFire(i,destination,victim,kind,speed,damage,radius,offset);
            t.attackClock=--a.burst?std::max(.04f,interval):std::max(.04f,pause);return a.burst==0;
        };
        switch(t.spawnType){
        case 0:{ // Tank patrol, engage/chase flags, close-range retreat, heavy variant and emergency missile.
            if(!seen){state(t,AIState::Patrol);patrol(false);aim(.8f);break;}
            if(!(t.parameters&4)&&(distance<8||a.state==AIState::Retreat)){if(a.state!=AIState::Retreat)state(t,AIState::Retreat,4);if(a.stateTime>0){walk(t.origin-delta, t.speed);break;}}
            if(!(t.parameters&4)&&(t.parameters&2)&&distance>std::max(8.f,w[3]*.01f)){state(t,AIState::Pursue);walk(destination,t.speed);}
            else {state(t,AIState::Attack);a.velocity={};}
            bool heavy=t.parameters&64;
            if(t.health<=8&&!a.rocketUsed&&distance>40&&visible&&aim(.8f)){enemyFire(i,destination,victim,ProjectileKind::Rocket,heavy?140:70,20,5);a.rocketUsed=true;t.attackClock=1;}
            else shootBurst(w[2],w[1]/256.f,2,ProjectileKind::Bullet,heavy?128:64,heavy?16:4);
            break;
        }
        case 1:{ // Helicopter approach, aimed burst or rocket/bomb attack, then five-second breakaway.
            bool retreat=a.state==AIState::Retreat&&a.stateTime>0;
            if(retreat){Vec3 away=t.origin-direction(bearing(delta)+(i&1?1.f:-1.f),0)*40;away.y=std::max(18.f,a.spawn.y);walk(away,t.speed,true);break;}
            if(!seen){state(t,AIState::Patrol);patrol(false);break;}
            float attackRange=(w[1]?w[1]:7000)*.01f;
            if(distance>attackRange){state(t,AIState::Pursue);auto goal=destination;goal.y=std::max(16.f,a.spawn.y);walk(goal,t.speed,true);}
            else {state(t,AIState::Attack);auto goal=t.origin;goal.y=std::max(12.f,destination.y+8);if(proceduralLayout)goal.y=std::max(goal.y,a.spawn.y);moveEnemy(i,{0,std::clamp(goal.y-t.origin.y,-dt*3,dt*3),0},dt,true);
                auto kind=w[4]==2?ProjectileKind::Bomb:w[4]==1?ProjectileKind::Rocket:ProjectileKind::Bullet;
                if(shootBurst(w[3],w[2]/256.f,5,kind,kind==ProjectileKind::Bullet?64:70,kind==ProjectileKind::Bullet?4:20,kind==ProjectileKind::Bullet?0:5))state(t,AIState::Retreat,5);
            }
            break;
        }
        case 2:case 5:case 10:{
            if(!seen){state(t,AIState::Patrol);break;}state(t,AIState::Attack);
            bool rocket=(t.parameters&1)&&w[2]>0;
            shootBurst((t.parameters&1)?1:w[2],w[1]/256.f,(t.parameters&1)?w[1]/256.f:w[3]/256.f,rocket?ProjectileKind::Rocket:ProjectileKind::Bullet,rocket?70:64,rocket?20:4,rocket?5:0);
            // The loaded assembly has no separated runtime turret pivot yet.
            t.yaw=a.weaponYaw;updateEnemyBounds(t);break;
        }
        case 3:{
            float engage=detect;seen=(squareRange(delta)<engage&&visible)||hit||a.alert>0;
            if(a.state==AIState::Patrol&&!seen){patrol(false);break;}
            if(distance<4){state(t,AIState::Melee);if(t.attackClock<=0&&visible){if(victim<=-2)damagePilot(*teammates.at(-2-victim),12,t.center-teammates.at(-2-victim)->bodyPoint(6));else if(victim<0)hurt(12,t.center-bodyPoint(6));else{targets[victim].health-=12;}t.attackClock=8;sounds.push_back({SoundEvent::Punch,0,t.center});}break;}
            if(distance>engage+20){state(t,AIState::Pursue);walk(destination,t.speed);break;}
            if(a.state==AIState::Attack&&!visible&&a.stateTime<-3){++a.side;state(t,AIState::Evade,2);}
            if(a.state==AIState::Evade&&a.stateTime>0){float sign=a.side&1?1.f:-1.f;walk(t.origin+direction(bearing(delta)+sign*pi/2,0)*20,t.speed);break;}
            if(a.state==AIState::Attack){
                bool rocket=game?(w[3]&1):(mission==4||mission==11||mission==15);
                if(shootBurst(w[2]?w[2]:4,rocket?.375f:.0625f,3,rocket?ProjectileKind::Rocket:ProjectileKind::Bullet,rocket?70:64,rocket?20:5,rocket?5:0,{a.side&1?1.7f:-1.7f,1.8f,0})){++a.side;state(t,AIState::Retreat,3);}
            }else if(a.state==AIState::Retreat&&a.stateTime>0){float sign=a.side&1?1.f:-1.f;walk(t.origin+direction(bearing(delta)+sign*pi/2,0)*20,t.speed);}
            else {state(t,AIState::Attack);a.burst=0;}
            break;
        }
        case 6:case 8:state(t,AIState::Patrol);patrol(true);break;
        case 7:{ // $7D3A: acquire -> approach -> bomb salvo -> exit, finite repeat passes.
            if(a.state==AIState::Patrol){if(squareRange(delta)<300||elapsed>6)state(t,AIState::Pursue);}
            if(a.state==AIState::Pursue){auto goal=destination;goal.y=std::max(20.f,a.spawn.y);walk(goal,t.speed,true);if(distance<detect&&(!visibilityRules()||visible)){state(t,AIState::BombRun);a.burst=std::max(1u,w[2]);t.attackClock=0;}}
            else if(a.state==AIState::BombRun){moveEnemy(i,direction(t.yaw,0)*(t.speed*dt),dt,true);
                if((hit&&(t.parameters&2))||(visibilityRules()&&!visible)){a.burst=0;}else if(t.attackClock<=0&&a.burst){enemyFire(i,destination,victim,ProjectileKind::Bomb,t.speed,40,10);--a.burst;t.attackClock=std::max(.04f,w[1]/256.f);}
                if(!a.burst){if(a.passes)--a.passes;state(t,AIState::Egress,3);}}
            else if(a.state==AIState::Egress){moveEnemy(i,direction(t.yaw,0)*(t.speed*dt),dt,true);if(distance>300&&a.stateTime<=0){if((t.parameters&1)&&a.passes)state(t,AIState::Pursue);else retireEnemy(t,true);}}
            a.bank=std::clamp(wrap(bearing(delta)-t.yaw)*-.4f,-.6f,.6f);break;
        }
        case 9:{
            state(t,AIState::Patrol);a.spawnDelay-=dt;if((t.parameters&1)&&a.spawnDelay<=0)moveEnemy(i,direction(t.yaw,0)*(t.speed*dt),dt,true);
            // Two independent original ship batteries, each with its own burst and range words.
            for(int mount=0;mount<2;++mount){unsigned enabled=mount?16:4;if(!(t.parameters&enabled))continue;
                float& clock=mount?a.secondClock:t.attackClock;unsigned& remaining=mount?a.secondBurst:a.burst;
                unsigned base=mount?9:6;float range=w[12+mount]*.01f;if(squareRange(delta)>range||clock>0||!visible)continue;
                bool rocket=t.parameters&(mount?32:8);Vec3 offset{0,2.24f,mount?26.72f:-24.8f};
                enemyFire(i,destination,victim,rocket?ProjectileKind::Rocket:ProjectileKind::Bullet,rocket?70:64,rocket?20:4,rocket?6:0,offset);
                if(!remaining)remaining=std::max(1u,w[base+1]);
                clock=(--remaining?w[base]:w[base+2])/256.f;clock=std::max(.04f,clock);
            }break;
        }
        case 11:case 14:case 19:{ // IS2 mobile missile battery, Satyr, Cold Iron Gunner.
            if(!game)break;
            if(!seen&&t.spawnType!=19){state(t,AIState::Patrol);patrol(false);break;}
            float range=t.spawnType==11?65:t.spawnType==14?45:100;
            if(distance>range||!visible){state(t,AIState::Pursue);walk(destination,t.speed);}
            else state(t,AIState::Attack);
            if(t.spawnType==14&&distance<5){state(t,AIState::Melee);if(t.attackClock<=0&&visible){if(victim<=-2)damagePilot(*teammates.at(-2-victim),15,t.center-teammates.at(-2-victim)->bodyPoint(6));else if(victim<0)hurt(15,t.center-bodyPoint(6));else targets[victim].health-=15;t.attackClock=2;sounds.push_back({SoundEvent::Punch,0,t.center});}break;}
            bool boss=t.spawnType==19;
            shootBurst(t.spawnType==11?1:boss?6:4,boss?.12f:.16f,boss?1.2f:2.5f,t.spawnType==11?ProjectileKind::Rocket:ProjectileKind::Bullet,t.spawnType==11?70:100,boss?8:5,t.spawnType==11?6:0,{a.side&1?2.f:-2.f,2,0});
            if(t.spawnType!=11&&a.secondClock<=0&&visible&&distance>12){enemyFire(i,destination,victim,ProjectileKind::Rocket,80,boss?25:18,6,{a.side&1?-2.f:2.f,3,0});a.secondClock=boss?(t.health<a.maxHealth*.5f?2.f:4.f):6.f;++a.side;}
            break;
        }
        case 12:{ // $D6A8: route leg, finite salvo, release interval and repeat count.
            if(!game)break;
            auto flight=std::find_if(cargoFlights.begin(),cargoFlights.end(),[&](auto& f){return f.carrier==i;});
            if(flight==cargoFlights.end()){patrol(true);break;}
            if(a.state==AIState::BombRun){
                moveEnemy(i,direction(t.yaw,0)*(t.speed*dt),dt,true);
                if(a.secondClock<=0&&a.burst){dropCargo(i);--a.burst;a.secondClock=flight->interval;}
                if(!a.burst){if(flight->passes)--flight->passes;state(t,AIState::Egress,4);}
            }else if(a.state==AIState::Egress){moveEnemy(i,direction(t.yaw,0)*(t.speed*dt),dt,true);if(a.stateTime<=0){if(!flight->passes)retireEnemy(t,true);else state(t,AIState::Patrol);}}
            else {state(t,AIState::Patrol);patrol(false);if(flight->passes&&t.waypoint>=flight->release){a.burst=flight->salvo;a.secondClock=flight->interval;state(t,AIState::BombRun);}}
            break;
        }
        case 13:case 16:case 20:if(game){state(t,AIState::Patrol);a.velocity={};}break;
        case 15:case 17:if(game){state(t,AIState::Patrol);patrol(true);}break;
        case 18:{
            if(!game)break;
            if(!t.route.empty()){state(t,AIState::Patrol);patrol(false);}
            else {auto goal=destination;goal.y=std::max(20.f,a.spawn.y);if(a.state==AIState::Egress&&a.stateTime>0)moveEnemy(i,direction(t.yaw,0)*(t.speed*dt),dt,true);else{state(t,AIState::Pursue);walk(goal,t.speed,true);if(distance<18)state(t,AIState::Egress,3);}}
            if(distance<160)shootBurst(4,.10f,2,ProjectileKind::Bullet,130,5);
            a.bank=std::clamp(wrap(bearing(delta)-t.yaw)*-.4f,-.6f,.6f);break;
        }
        case 22:if(game){state(t,AIState::Patrol);patrol(false);if(seen)shootBurst(3,.15f,2,ProjectileKind::Bullet,85,4);}break;
        default:break;
        }
    }
}
void World::selectPlayerTargets(float dt){
    if(!cooperative||assault)return;
    auto random=[&](){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return double(randomState)/4294967296.;};
    for(auto& t:targets){if(t.spawnType<0||!t.active||t.health<=0)continue;auto& a=t.ai;a.retarget-=dt;
        if(a.retarget>0&&a.playerTarget>=0&&size_t(a.playerTarget)<teammates.size()&&!teammates[a.playerTarget]->lifeSpent&&teammates[a.playerTarget]->health>0)continue;
        std::vector<double> weights(teammates.size());double sum=0;
        for(size_t i=0;i<teammates.size();++i)if(!teammates[i]->lifeSpent&&teammates[i]->health>0){double d=length(teammates[i]->position-t.center);sum+=weights[i]=1./(25.+d*d);}
        double choice=random()*sum;a.playerTarget=-1;
        for(size_t i=0;i<teammates.size();++i)if(weights[i]>0){a.playerTarget=int(i);choice-=weights[i];if(choice<=0)break;}
        a.retarget=2+float(random()*3);
    }
}
float World::nearestPlayerRange(Vec3 p)const{
    auto range=[&](Vec3 a){return std::max(std::abs(a.x-p.x),std::abs(a.z-p.z));};
    if(!cooperative)return range(position);
    float distance=1e9f;for(auto pilot:teammates)if(!pilot->lifeSpent&&pilot->health>0)distance=std::min(distance,range(pilot->position));return distance;
}
}
