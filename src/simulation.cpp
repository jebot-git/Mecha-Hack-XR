#include "simulation.hpp"
#include "rpg/cockpit_arm_rest.hpp"
#include "rpg/enemy_effects.hpp"
#include "searchlight.hpp"
#include <algorithm>
namespace iron {
static float axis(float v) {
    if (!std::isfinite(v)) return 0;
    return std::abs(v)<.18f ? 0 : std::copysign((std::min(std::abs(v),1.f)-.18f)/.82f,v);
}
bool Gesture::sample(Vec3 p,bool tracked,float dt) {
    cooldown=std::max(0.f,cooldown-dt);
    if (!tracked || dt<=0 || dt>.1f || !std::isfinite(length(p))) { reset(); return false; }
    if (!valid) { previous=p; valid=true; return false; }
    float distance=length(p-previous), speed=distance/dt; previous=p;
    if (speed>8) { armed=false; return false; } // discontinuity / recenter, not a punch
    if (speed<.5f) armed=true;
    if (armed && cooldown==0 && speed>1.7f && distance>.018f) {
        armed=false; cooldown=.45f; return true;
    }
    return false;
}
World::World() {
    targetSlots.fill(-1);
    for (int z=0;z<5;++z) for(int x=-3;x<=3;++x) {
        float height=6+((x*x+z*3)%5)*3;
        Target t;t.center={x*22.f,height/2,-z*26.f-20};t.half={5,height/2,5};targets.push_back(t);
    }
}
void World::resetTracking() {
    walkingSpeed=0;
    for(auto &g:gestures) g.reset();
    for(auto &a:arms)a.tracked=false;
}
// Swept segment vs expanded AABB: fast rounds cannot tunnel through a building.
static float hit(Vec3 a,Vec3 b,const Target& t,float radius=0) {
    float low=0,high=1;
    std::array<float,3> start{a.x,a.y,a.z}, end{b.x,b.y,b.z};
    std::array<float,3> center{t.center.x,t.center.y,t.center.z}, half{t.half.x+radius,t.half.y+radius,t.half.z+radius};
    for(int k=0;k<3;++k) {
        float delta=end[k]-start[k];
        if(std::abs(delta)<1e-6f) { if(std::abs(start[k]-center[k])>half[k]) return 2; }
        else {
            float u=(center[k]-half[k]-start[k])/delta,v=(center[k]+half[k]-start[k])/delta;
            if(u>v) std::swap(u,v);
            low=std::max(low,u); high=std::min(high,v);
            if(low>high) return 2;
        }
    }
    return low;
}
World::AimHit World::traceAim(Vec3 origin,Vec3 ray,float range,bool respectVisibility)const{
    ray=unit(ray);
    if(!proceduralTerrain&&!gravity.active()&&ray.y<-.0001f)range=std::min(range,std::max(0.f,-origin.y/ray.y));
    if(respectVisibility&&visibilityRules())range=std::min(range,(searchlightActive(*this)&&dot(ray,searchlightDirection(*this))>=.9063078f)?visibilityRanges(*this).lit:visibilityRanges(*this).base);
    Vec3 end=origin+ray*range;
    const float support=proceduralTerrain&&ray.y<-.0001f?groundContact(origin,end):deckContact(origin,end);
    float first=std::min(1.f,support);int target=-1;
    for(size_t i=0;i<targets.size();++i){const auto& t=targets[i];
        if(!t.active||t.escaped||t.health<=0||t.spawnType==21)continue;
        float f=hit(origin,end,t);if(f<first){first=f;target=int(i);}
    }
    return {origin+ray*(range*first),target};
}
Vec3 World::mapPingPosition(const Pilot& p)const{
    if(p.pingTarget>=0&&size_t(p.pingTarget)<targets.size()){const auto& t=targets[p.pingTarget];if(t.active&&!t.escaped&&t.health>0)return t.center;}
    return p.pingPosition;
}
void World::lockHoming(){
    homingTarget=-1;
    if(std::find(weapons.begin(),weapons.end(),Weapon::Homing)==weapons.end())return;
    Vec3 ray=direction(aimYaw,aimPitch),origin=eye();float best=.985f;
    for(size_t i=0;i<targets.size();++i){const auto& t=targets[i];
        if(!t.active||t.escaped||t.health<=0||t.invulnerable||t.protectedTarget||t.spawnType<0||t.spawnType==21)continue;
        if(visibilityRules()&&!canPerceive(*this,t.center,false,int(i)))continue;
        Vec3 delta=t.center-origin;float distance=length(delta);if(distance<3||distance>800)continue;
        float alignment=dot(unit(delta),ray);if(alignment<=best)continue;
        if(traceAim(origin,delta,distance+1).target!=int(i))continue;
        best=alignment;homingTarget=int(i);
    }
}
void World::pingMap(unsigned kind){
    if(kind>1||lifeSpent||health<=0||missionStatus!=MissionStatus::Active||elapsed<nextPing[kind])return;
    nextPing[kind]=elapsed+.35f;
    if(kind==0){selfPingUntil=elapsed+6;return;}
    auto aim=traceAim(eye(),direction(aimYaw,aimPitch));
    pingTarget=aim.target;pingPosition=aim.position;if(pingTarget<0)pingPosition.y=0;
    aimPingUntil=elapsed+6;
}
static void damage(Target& t,float d,unsigned& count) {
    if(!t.active || t.health<=0) return;
    if(t.invulnerable)return;
    t.health-=d;
    if(t.health<=0) ++count;
}
bool World::crushable(const Target& t)const{
    if(t.invulnerable||t.escaped||!t.active||t.health<=0)return false;
    if(rpgPrototype){
        const auto half=t.ai.boundsReady?t.ai.localHalf:t.half;
        return (t.rpgDecoration||t.spawnType==3)&&half.y<=4.f&&std::max(half.x,half.z)<=5.f;
    }
    if(t.spawnType>=0){
        // IS1 $BB8E / IS2 $12DFA: 10000 raw damage, except internal
        // types $20006 (mech), $20012 (fortress gun), $20013 (support),
        // and tanks whose original model flags contain bit 6.
        return t.spawnType!=3&&t.spawnType!=10&&!(t.spawnType==0&&(t.parameters&64))&&!(combatGame()&&t.spawnType==20);
    }
    // Byte lookup at $36464 / $874DC, including the actual bytes indexed
    // by IS2's expanded static type IDs (the original code has no 34-type guard).
    static constexpr unsigned char scenery[]={0,0,0,0,0,0,1,0,1,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,8,118,198,0,8,118,78,0,8,118,174,0,8,117};
    return t.type>=0&&t.type<(game?49:34)&&scenery[t.type]!=0;
}
bool World::footContact(Vec3 center,const Target& t)const{
    if(t.center.y-t.half.y>center.y+.96f||t.center.y+t.half.y<center.y-.96f)return false;
    float x=std::max(0.f,std::abs(t.center.x-center.x)-t.half.x);
    float z=std::max(0.f,std::abs(t.center.z-center.z)-t.half.z);
    return x*x+z*z<=1.75f*1.75f;
}
void World::stomp(unsigned foot){
    if(foot>1||lifeSpent||health<=0)return;
    // Both cartridges: alternating +/-170 cm, radius 175 cm, +/-96 cm height.
    // The foot event has no return-damage callback; it must not hurt the pilot.
    Vec3 center=position+rotate(compose(gravity.frame,angles(chassisYaw)),{foot? -1.7f:1.7f,0,0});
    if(!rpgPrototype)for(auto& t:targets)if(crushable(t)&&footContact(center,t))damage(t,1000,destroyed);
    sounds.push_back({SoundEvent::Footstep,0,center});
}
void World::movePilot(Vec3 proposed){
    Vec3 previous=position;auto movement=proposed-position;if(length(movement)<.00001f)return;
    if(gravity.active()){
        if(!rpg::moveOnGravity(*this,gravity,position,movement,13.f,3.5f))return;
    }else{
        bool blocked=false;
        if(proceduralTerrain){
            const auto delta=proposed-position;
            const int steps=std::max(1,int(std::ceil(std::hypot(delta.x,delta.z))));
            const float maxStep=.6f+.6f*std::hypot(delta.x,delta.z)/steps;
            const float initialFloor=rpgPrototype?pilotSupportHeight(position):walkableHeight(position);
            bool flying=rpgPrototype&&position.y>initialFloor+.025f;
            float feetY=rpgPrototype?position.y:initialFloor;
            for(int i=1;i<=steps;++i){
                auto sample=position+delta*(float(i)/steps);
                sample.y=feetY;
                float floor=rpgPrototype?pilotSupportHeight(sample):walkableHeight(sample);
                const float gap=feetY-floor;
                // A ledge keeps the feet at their current height until gravity
                // advances them. Gentle gradients remain continuously walkable.
                const bool airborne=rpgPrototype&&(gap>maxStep||(flying&&gap>.025f));
                if(!airborne){
                    const bool roof=rpgPrototype&&floor>walkableHeight(sample)+.02f;
                    if(std::abs(gap)>maxStep||(!roof&&!groundPassable({sample.x,floor,sample.z},3.5f,rpgPrototype))){
                        blocked=true;break;
                    }
                }
                const float desired=feetY+(rpgPrototype?delta.y/steps:0.f);
                feetY=airborne||(rpgPrototype&&delta.y>0.f)?std::max(floor,desired):floor;
                flying=rpgPrototype&&feetY>floor+.025f;
                sample.y=feetY;
                if(!interiorDecks.empty()&&deckContact(sample+Vec3{0,.15f,0},sample+Vec3{0,13,0})<=1.f){
                    blocked=true;break;
                }
            }
            proposed.y=feetY;
        }
        for(const auto& t:targets)if(t.active&&t.health>0&&!t.escaped&&!t.ai.rpgGrapplePilot&&!(combatGame()&&t.spawnType==20)){
            // Let feet reach source-eligible scenery as well as small actors.
            // Inflated native torso bounds otherwise stop movement before a
            // footfall, permanently protecting trees and small crushable buildings.
            // Satyr retains its separate original torso-blocking exception.
            if(crushable(t)&&!(combatGame()&&t.spawnType==14))continue;
            if(hit(position+Vec3{0,5,0},proposed+Vec3{0,5,0},t,2)<=1)blocked=true;
        }
        if(cooperative)for(auto p:teammates)if(p!=static_cast<Pilot*>(this)&&!p->lifeSpent&&p->health>0&&length(proposed-p->position)<4&&length(position-p->position)>=4)blocked=true;
        if(!blocked){position.x=std::clamp(proposed.x,proceduralTerrain?movementBounds[0]:mission<0?-150.f:-1000.f,proceduralTerrain?movementBounds[2]:mission<0?150.f:1000.f);position.z=std::clamp(proposed.z,proceduralTerrain?movementBounds[1]:mission<0?-180.f:-1000.f,proceduralTerrain?movementBounds[3]:mission<0?100.f:1000.f);if(proceduralTerrain||rpgPrototype)position.y=proposed.y;}
    }
    const auto heading=rotate(gravity.frame,direction(chassisYaw,0));
    movement=position-previous;
    const float distance=gravity.active()?length(movement):std::hypot(movement.x,movement.z);
    if(distance<.00001f||(rpgPrototype&&!gravity.active()&&position.y>pilotSupportHeight(position)+.75f))return;
    // Original 18-frame gait emits a latched stomp on frames 3 and 12, not
    // continuous contact damage. Crossing detection preserves events at any FPS.
    // Use a slower native 96 cm stride phase (source arithmetic uses 64 cm).
    // Only actual travel advances the gait, including at obstacles/map bounds.
    float delta=distance*(100.f/96)*(dot(movement,heading)>=0?1.f:-1.f);
    float next=gaitPhase+delta;
    for(int foot=0;foot<2;++foot){float event=(foot?12.f:3.f)+(delta<0?1.f:0.f);
        if(delta>0){while(event<=gaitPhase)event+=18;if(event<=next)stomp(foot);}
        else{while(event>=gaitPhase)event-=18;if(event>=next)stomp(foot);}
    }
    gaitPhase=std::fmod(next+18,18.f);
}
bool World::effect(WeaponEffect::Kind kind,Vec3 p,Vec3 d,Weapon weapon,float size){
    // Prefer evicting cosmetics over active hazards or arrival signals.
    if(effects.size()>=256){
        auto old=std::find_if(effects.begin(),effects.end(),[](const auto& fx){
            return fx.model!=rpg::rpgRadarArrival&&(fx.damageRate<=0||fx.age>=1);
        });
        if(old==effects.end())return false;
        effects.erase(old);
    }
    effects.push_back({kind,p,d,weapon,0,size});
    return true;
}
static void rocketDamage(Projectile&,float);
// Contact effects belong to the struck surface, never the swinging muzzle.
void World::strike(Vec3 p,Vec3 forward,Weapon weapon,float amount,bool sound){
    for(auto& t:targets)if(t.active&&t.health>0){
        Vec3 contact{std::clamp(p.x,t.center.x-t.half.x,t.center.x+t.half.x),std::clamp(p.y,t.center.y-t.half.y,t.center.y+t.half.y),std::clamp(p.z,t.center.z-t.half.z,t.center.z+t.half.z)};
        if(length(contact-p)<2.5f){
            if(sound&&weapon==Weapon::Cutter)effect(WeaponEffect::Sparks,contact,forward,weapon,.4f);
            if(!(!game&&mission==2&&t.tag==32))damage(t,amount*(t.spawnType>=0?(weapon==Weapon::Cutter?50.f/18:15.f/7):1.f),destroyed);
        }
    }
    if(cooperative)for(auto* pilot:teammates)if(pilot!=static_cast<Pilot*>(this)&&!pilot->lifeSpent&&length(pilot->position+Vec3{0,6,0}-p)<5.5f)damagePilot(*pilot,amount,p-pilot->bodyPoint(6));
    for(auto& shot:projectiles)if(shot.kind==ProjectileKind::Rocket&&length(shot.position-p)<3.5f)rocketDamage(shot,amount*(weapon==Weapon::Cutter?50.f/18:15.f/7));
    if(sound)sounds.push_back({weapon==Weapon::Cutter?SoundEvent::CutterMotor:SoundEvent::Punch,int(weapon),p});
}
void World::destruction(Vec3 p,float size,Weapon weapon,float damageRadius,float damageRate){
    if(effect(WeaponEffect::Blast,p,{},weapon,size)){
        effects.back().damageRadius=damageRadius;effects.back().damageRate=damageRate;
    }
    if(effect(WeaponEffect::Smoke,p+Vec3{0,size*.15f,0},{},weapon,size*.8f))effects.back().age=-.35f;
    // Original debris uses 18 m/s² gravity, a 3-second lifetime and half-speed bounces.
    for(int i=0;i<6;++i){float a=i*2*pi/6;effect(WeaponEffect::Debris,p,{std::cos(a)*(6+i*.5f),12+i*.5f,std::sin(a)*(6+i*.5f)},weapon,.65f);}
}
void World::explode(Vec3 p,float radius,float d,bool friendly) {
    for(auto &t:targets) {
        if(!t.active||(!game&&mission==2&&t.tag==32))continue; // bridge supports require a direct grenade hit
        Vec3 closest{std::clamp(p.x,t.center.x-t.half.x,t.center.x+t.half.x),
                     std::clamp(p.y,t.center.y-t.half.y,t.center.y+t.half.y),
                     std::clamp(p.z,t.center.z-t.half.z,t.center.z+t.half.z)};
        if(length(p-closest)<radius) damage(t,d,destroyed);
    }
    if(friendly&&cooperative)for(auto pilot:teammates)if(pilot!=static_cast<Pilot*>(this)&&length(pilot->position+Vec3{0,6,0}-p)<radius+3)damagePilot(*pilot,d,p-pilot->bodyPoint(6));
}
void World::step(const Input& in,float dt,bool shared) {
    if(!std::isfinite(dt)||dt<=0){resetTracking();return;}
    // Forced ejection is an animation, not tracked locomotion: a slow render frame
    // or lost focus must not strand a destroyed pilot before the menu transition.
    if(missionStatus==MissionStatus::Ejecting){searchlightOn=false;wasSearchlight=in.searchlight;stepEjection(std::min(dt,1.f));return;}
    if(dt>.1f){resetTracking();return;}
    if(!in.focused || !in.headValid || !std::isfinite(in.headYaw) || !std::isfinite(in.headPitch)) {
        homingTarget=-1;resetTracking();wasSearchlight=in.searchlight; wasFire=in.fire; wasPunch=in.punch; return;
    }
    if(missionStatus==MissionStatus::Complete||missionStatus==MissionStatus::Failed){walkingSpeed=0;return;}
    if(visibilityRules()&&missionStatus==MissionStatus::Active&&health>0&&in.searchlight&&!wasSearchlight)searchlightOn=!searchlightOn;
    wasSearchlight=in.searchlight;
    walkingSpeed=0;Vec3 startPosition=position;
    for(auto &c:cooldown)c=(rpgPrototype?std::max(0.f,c):c)-dt;
    for(auto &c:fistFlash) c=std::max(0.f,c-dt);
    for(auto &c:punchRecovery) c=std::max(0.f,c-dt);
    transitCooldown=std::max(0.f,transitCooldown-dt);
    if(combatGame()&&!inTransit&&transitCooldown==0)for(auto& t:targets)if(t.active&&t.spawnType==20&&!t.route.empty()&&std::hypot(position.x-t.origin.x,position.z-t.origin.z)<std::max(3.f,std::min(t.half.x,t.half.z))){transitDestination=t.route.front();inTransit=true;transitCooldown=2;break;}
    if(inTransit){auto delta=transitDestination-position;float distance=length(delta);if(distance<=200*dt){position=transitDestination;inTransit=false;transitCooldown=2;}else position=position+delta*(200*dt/distance);}
    else if(!missile.active) {
        torsoYaw=std::clamp(torsoYaw-axis(in.turn)*1.35f*dt,-pi/2,pi/2);
        chassisYaw=wrap(chassisYaw-axis(in.steer)*.8f*dt);
        float z=axis(in.forward)*(rpgPrototype?injuryMovementScale(rpgInjuries)*in.rpgWalkScale:1.f);
        Vec3 proposed=position+rotate(gravity.frame,direction(chassisYaw,0))*(8*dt*z);
        movePilot(proposed);
    }
    walkingSpeed=(gravity.active()?length(position-startPosition):std::hypot(position.x-startPosition.x,position.z-startPosition.z))/dt;
    const auto cockpit=cockpitOrientation();
    aimYaw=wrap(facing()+attackYaw(in.headYaw));aimPitch=std::clamp(in.headPitch,-1.45f,1.45f);
    if(gravity.active()||rpgKnockdown.active(elapsed+rpgPoseLead)){
        const auto headAim=rotate(cockpit,direction(attackYaw(in.headYaw),aimPitch));
        aimYaw=std::atan2(-headAim.x,-headAim.z);
        aimPitch=std::asin(std::clamp(headAim.y,-1.f,1.f));
    }
    if(targetingWorking())lockHoming();else homingTarget=-1;
    if(!cooperative)for(unsigned kind=0;kind<2;++kind){
        if(in.ping[kind]&&!wasPing[kind])pingMap(kind);
        wasPing[kind]=in.ping[kind];
    }
    bool guiding=missile.active;
    for(int h=0;h<2;++h) {
        int held=heldModel(weapons[h]);
        arms[h]=armAvailable(h)?solveArm(in.handPoses[h],h,eye(),0,armRig,held,cockpit,rpg::armRestFor(rpgChassis,h)):Arm{};
    }
    // RPG owns weapon cooldowns, actions and damage; keep native locomotion/IK.
    if(rpgPrototype)return;
    for(int h=0;h<2;++h) {
        bool swing=gestures[h].sample(in.hands[h],in.handValid[h]&&arms[h].tracked,dt);
        if(arms[h].tracked && (swing || (in.punch[h]&&!wasPunch[h]&&punchRecovery[h]==0))) {
            ++punches[h]; fistFlash[h]=.45f;punchRecovery[h]=.3f;
            strike(arms[h].wrist+arms[h].forward*1.7f,arms[h].forward,Weapon::Empty,7);
        }
        wasPunch[h]=in.punch[h];
    }
    for(int mount=0;mount<MountCount;++mount) {
        Weapon weapon=weapons[mount];const auto& spec=weaponInfo(weapon,combatGame());
        bool fire=in.fire[mount]||(weapon==Weapon::AutoGrenade&&in.fire[mount==LeftHip?RightHip:LeftHip]);
        if(!fire){burstCount[mount]=0;cooldown[mount]=std::max(0.f,cooldown[mount]);if(!combatGame()&&weapon!=Weapon::Rail)cooldown[mount]=0;}
        if(fire&&!wasFire[mount])cooldown[mount]=std::max(0.f,cooldown[mount]);
        bool allowed=fire&&!guiding&&weapon!=Weapon::Shield&&(mount>=2||(arms[mount].tracked&&punchRecovery[mount]==0));
        if(!allowed){cooldown[mount]=std::max(0.f,cooldown[mount]);wasFire[mount]=fire;continue;}
        if(mount<2&&weapon==Weapon::Cutter){
            // Original continuous-contact callbacks return 18/50 raw per update.
            // Normalize contact integration to the cartridge's initial 30-Hz step.
            bool motor=cooldown[mount]<=.000001f;
            strike(arms[mount].muzzle,arms[mount].forward,weapon,spec.damage*(30*dt),motor);
            if(motor){++punches[mount];fistFlash[mount]=.45f;cooldown[mount]+=.18f;}
        }else if(mount<2&&weapon==Weapon::Empty){
            if(!wasFire[mount]&&cooldown[mount]<=.000001f){strike(arms[mount].wrist+arms[mount].forward*1.7f,arms[mount].forward,weapon,spec.damage);++punches[mount];fistFlash[mount]=.45f;cooldown[mount]=spec.cooldown;}
        }else if(weapon==Weapon::Cruise){
            if(!wasFire[mount]&&ammo[mount]>0){
                Vec3 launch=eye()+rotate(angles(facing()),{mount%2?2.5f:-2.5f,-.5f,-2});
                missile={true,launch,facing(),0,8,facing(),in.headYaw,in.headPitch};
                ++shots[mount];--ammo[mount];guiding=true;sounds.push_back({SoundEvent::Shot,int(weapon),launch});
            }
        }else if(weapon!=Weapon::Homing||homingTarget>=0){
            for(int catchup=0;catchup<8&&cooldown[mount]<=.000001f&&ammo[mount]>0&&(!spec.burst||burstCount[mount]<spec.burst);++catchup){
                Vec3 right{std::cos(facing()),0,-std::sin(facing())};
                Vec3 origin=eye()+right*((mount%2)?2.f:-2.f)+Vec3{0,mount>=LeftHip?-6.f:mount>=LeftShoulder?1.f:-2.f,0};
                // Hand origin/direction match the rendered tracked weapon muzzle. Body mounts use gaze.
                Vec3 ray;
                if(mount<2){origin=arms[mount].muzzle;ray=arms[mount].forward;}
                else ray=eye()+direction(aimYaw,aimPitch)*80-origin;
                ray=attackDirection(ray,facing(),mount<2?handYawLimit:headYawLimit); // Include body-mount parallax in the limit.
                ray=unit(ray);Vec3 spreadRight=unit(cross(ray,{0,1,0}),{1,0,0}),up=unit(cross(spreadRight,ray));
                int pellets=weapon==Weapon::Shotgun?8:1;
                for(int pellet=0;pellet<pellets;++pellet){
                    Vec3 spread=ray;
                    if(pellets>1){float angle=pellet*2*pi/8,radius=pellet%2?.055f:.025f;spread=attackDirection(unit(ray+spreadRight*(std::cos(angle)*radius)+up*(std::sin(angle)*radius)),facing(),handYawLimit);}
                    projectiles.push_back({origin,spread*spec.speed,weapon==Weapon::Shotgun?1.2f:weapon==Weapon::Homing?10.f:weapon==Weapon::Rail?5.f:4.f,spec.damage,mount});
                    auto& shot=projectiles.back();shot.weapon=weapon;shot.shooter=int(playerSlot);
                    if(weapon==Weapon::Rocket||weapon==Weapon::Homing){shot.kind=ProjectileKind::Rocket;shot.radius=weapon==Weapon::Homing?0:5;shot.homingTarget=weapon==Weapon::Homing?homingTarget:-1;}
                    if(weapon==Weapon::Grenade||weapon==Weapon::AutoGrenade){shot.kind=ProjectileKind::Bomb;shot.radius=weapon==Weapon::Grenade?15:0;shot.velocity.y+=weapon==Weapon::Grenade?30:12;}
                }
                ++shots[mount];++burstCount[mount];--ammo[mount];
                sounds.push_back({SoundEvent::Shot,int(weapon),origin});
                cooldown[mount]+=spec.cooldown;
                if(spec.cooldown<=0)break;
            }
            if(!ammo[mount]||(spec.burst&&burstCount[mount]>=spec.burst))cooldown[mount]=std::max(0.f,cooldown[mount]);
        }else cooldown[mount]=std::max(0.f,cooldown[mount]);
        wasFire[mount]=fire;
    }
    if(missile.active) {
        float targetYaw=wrap(missile.launchYaw+attackYaw(in.headYaw-missile.headYaw));
        float targetPitch=std::clamp(in.headPitch-missile.headPitch,-1.3f,1.3f);
        missile.yaw=wrap(missile.yaw+std::clamp(wrap(targetYaw-missile.yaw),-1.8f*dt,1.8f*dt));
        missile.pitch+=std::clamp(targetPitch-missile.pitch,-1.8f*dt,1.8f*dt);
        Vec3 previous=missile.position,next=previous+direction(missile.yaw,missile.pitch)*(32*dt);
        float first=2;Target* target=nullptr;Pilot* struck=nullptr;
        for(auto& t:targets)if(t.active&&!t.escaped&&t.health>0){float f=hit(previous,next,t);if(f<first){first=f;target=&t;}}
        float floor=groundContact(previous,next);if(floor<first){first=floor;target=nullptr;}
        if(cooperative)for(auto* pilot:teammates)if(pilot!=static_cast<Pilot*>(this)&&!pilot->lifeSpent&&pilot->health>0){Target body;body.center=pilot->position+Vec3{0,6,0};body.half={2,6,2};float f=hit(previous,next,body);if(f<first){first=f;target=nullptr;struck=pilot;}}
        missile.position=previous+(next-previous)*std::min(first,1.f);
        missile.life-=dt;
        if(first<=1 || missile.position.y<surfaceHeight(missile.position.x,missile.position.z) || missile.life<=0) {
            if(first<=1){
                float amount=1000;
                if(target){
                    if(!combatGame()&&target->spawnType==3)amount=130;
                    if((combatGame()&&(target->spawnType==3||target->spawnType==8||target->spawnType==18))||(game&&mission==16&&target->spawnType<0&&target->type==48))amount=0;
                    if(!(!game&&mission==2&&target->tag==32))damage(*target,amount,destroyed);
                }
                if(struck)damagePilot(*struck,amount,previous-missile.position);
            }
            destruction(missile.position,30,Weapon::Cruise,20,combatGame()?125.1f:125.f);
            sounds.push_back({SoundEvent::Explosion,int(Weapon::Cruise),missile.position});missile.active=false;resetTracking();
        }
    }
    if(shared){stepProjectiles(dt);stepMission(dt);}
    else if(missionStatus==MissionStatus::Active)collectPickups();
}
// Original missile damage callbacks survive zero health (BPL); only a
// strictly negative hull destroys Wolfpack/Ferret. Grenades and cruise are immune.
static void rocketDamage(Projectile& p,float amount){
    if(p.kind!=ProjectileKind::Rocket||p.intercepted)return;
    if(p.hull<0)p.hull=p.weapon==Weapon::Homing?10.f:2.f;
    p.hull-=amount;
    if(p.hull<-.00001f){p.intercepted=true;p.life=0;}
    else p.hull=std::max(0.f,p.hull); // Float roundoff at zero must not reinitialize hull.
}
static float directDamage(const Projectile& p,const Target* target,int game,int mission){
    if(p.mount<0)return p.damage; // Unmapped enemy routines retain their native tuning.
    if(p.weapon==Weapon::Rocket)return target&&target->spawnType<0?30.f:20.1f;
    if(p.weapon==Weapon::AutoGrenade&&target){
        if(target->spawnType==3)return 5;
        if(game&&mission==16&&target->spawnType<0&&target->type==48)return 15;
    }
    return p.damage;
}
// Relative swept sphere: a fast bullet and a moving rocket can cross entirely
// between two frames. Test against the world-hit limits as well as each other.
static float crossHit(Vec3 start,Vec3 delta,float radius){
    float c=dot(start,start)-radius*radius;if(c<=0)return 0;
    float a=dot(delta,delta),b=dot(start,delta),discriminant=b*b-a*c;
    if(a<1e-10f||discriminant<0)return 2;
    float t=(-b-std::sqrt(discriminant))/a;return t>=0&&t<=1?t:2;
}
void World::stepProjectiles(float dt){
    if(!std::isfinite(dt)||dt<=0)return;
    for(auto& p:projectiles)if(p.kind==ProjectileKind::Rocket&&!p.intercepted&&p.hull<0)p.hull=p.weapon==Weapon::Homing?10.f:2.f;
    for(auto& fx:effects){
        // $9194 multiplies the damage rate by elapsed 16.16 seconds. Ten
        // 10-Hz animation frames give a one-second damage volume, not one hit.
        float active=std::max(0.f,std::min(1.f,fx.age+dt)-std::max(0.f,fx.age));
        if(fx.damageRate>0&&active>0){
            float amount=fx.damageRate*active;
            explode(fx.position,fx.damageRadius,amount);
            auto splash=[&](Pilot* pilot){if(length(pilot->position+Vec3{0,6,0}-fx.position)<fx.damageRadius+3)damagePilot(*pilot,amount,fx.position-pilot->bodyPoint(6));};
            if(cooperative)for(auto* pilot:teammates)splash(pilot);else splash(this);
            for(auto& shot:projectiles)if(length(shot.position-fx.position)<fx.damageRadius+1)rocketDamage(shot,amount);
        }
        fx.age+=dt;
        if(fx.kind==WeaponEffect::Debris){fx.direction.y-=18*dt;fx.position=fx.position+fx.direction*dt;float floor=debrisFloor(fx);if(fx.position.y<floor){fx.position.y=floor;if(fx.direction.y<0)fx.direction.y=-fx.direction.y*.5f;}}
    }
    std::erase_if(effects,[](const auto& fx){return fx.age>(fx.kind==WeaponEffect::Debris?3.f:(fx.kind==WeaponEffect::Blast||fx.kind==WeaponEffect::Smoke)?1.f:fx.kind==WeaponEffect::Muzzle?.09f:.3f);});
    struct Sweep{Vec3 next;float first=2;Target* target=nullptr;Pilot* pilot=nullptr;bool consumed=false;};
    std::vector<Sweep> sweeps(projectiles.size());
    for(size_t i=0;i<projectiles.size();++i){auto& p=projectiles[i];auto& s=sweeps[i];
        if(p.weapon==Weapon::Homing&&p.homingTarget>=0){
            if(size_t(p.homingTarget)>=targets.size()||!targets[p.homingTarget].active||targets[p.homingTarget].escaped||targets[p.homingTarget].health<=0)p.homingTarget=-1;
            else {Vec3 desired=unit(targets[p.homingTarget].center-p.position),current=unit(p.velocity);float angle=std::acos(std::clamp(dot(desired,current),-1.f,1.f));float blend=angle>.0001f?std::min(1.f,1.5f*dt/angle):1.f;p.velocity=unit(current*(1-blend)+desired*blend)*length(p.velocity);}
        }
        float travel=std::clamp(p.life,0.f,dt);
        if(p.kind==ProjectileKind::Bomb)p.velocity.y-=18.f*travel;
        s.next=p.position+p.velocity*travel;
        for(auto& t:targets)if(t.active&&!t.escaped&&t.health>0&&(&t-targets.data()!=p.owner)&&(p.owner<0||t.ai.parent!=p.owner)){
            float f=hit(p.position,s.next,t);if(f<s.first){s.first=f;s.target=&t;}
        }
        float floor=groundContact(p.position,s.next);if(floor<s.first){s.first=floor;s.target=nullptr;}
        if(p.mount<0||cooperative){
            auto test=[&](Pilot* pilot){if(pilot->lifeSpent||pilot->health<=0||(p.mount>=0&&int(pilot->playerSlot)==p.shooter))return;Target body;body.center=pilot->position+Vec3{0,6,0};body.half={2,6,2};float f=hit(p.position,s.next,body);if(f<=1&&f<s.first){s.first=f;s.pilot=pilot;s.target=nullptr;}};
            if(cooperative)for(auto* pilot:teammates)test(pilot);else test(this);
        }
    }
    struct Interception{float time;size_t bullet,rocket;};std::vector<Interception> contacts;
    for(size_t b=0;b<projectiles.size();++b)if(projectiles[b].kind==ProjectileKind::Bullet&&projectiles[b].life>0)
        for(size_t r=0;r<projectiles.size();++r)if(projectiles[r].kind==ProjectileKind::Rocket&&!projectiles[r].intercepted&&projectiles[r].life>0){
            const auto& bullet=projectiles[b];const auto& rocket=projectiles[r];
            Vec3 relative=bullet.position-rocket.position;
            // Simultaneously emitted ordnance must not intercept at its muzzle.
            if(bullet.shooter==rocket.shooter&&bullet.owner==rocket.owner&&length(relative)<1.01f)continue;
            float t=crossHit(relative,(sweeps[b].next-bullet.position)-(sweeps[r].next-rocket.position),1);
            if(t<=1&&t<sweeps[b].first&&t<sweeps[r].first)contacts.push_back({t,b,r});
        }
    std::sort(contacts.begin(),contacts.end(),[](const auto& a,const auto& b){return a.time<b.time;});
    for(const auto& c:contacts){auto& bullet=projectiles[c.bullet];auto& rocket=projectiles[c.rocket];auto& bs=sweeps[c.bullet];auto& rs=sweeps[c.rocket];
        if(bs.consumed||rocket.intercepted)continue;
        bs.consumed=true;bs.first=c.time;bs.target=nullptr;bs.pilot=nullptr;bullet.life=0;
        rocketDamage(rocket,bullet.damage);
        if(rocket.intercepted){rs.first=c.time;rs.target=nullptr;rs.pilot=nullptr;}
    }
    for(size_t i=0;i<projectiles.size();++i){auto& p=projectiles[i];auto& s=sweeps[i];
        p.position=p.position+(s.next-p.position)*std::min(s.first,1.f);
        if(!p.intercepted&&!s.consumed&&s.first<=1){
            if(s.target){bool bridge=!game&&mission==2&&s.target->tag==32;
                if((p.mount>=0||p.radius==0)&&(!bridge||(p.mount>=0&&p.weapon==Weapon::Grenade)))damage(*s.target,directDamage(p,s.target,game,mission),destroyed);
            }
            if(s.pilot&&(p.mount>=0||p.radius==0))damagePilot(*s.pilot,directDamage(p,nullptr,game,mission),p.velocity*-1.f);
            p.life=0;
        }
        p.life-=dt;
        if(p.life>0)continue;
        if(p.intercepted){destruction(p.position,3,p.weapon);sounds.push_back({SoundEvent::Explosion,int(p.weapon),p.position});}
        else if(p.kind!=ProjectileKind::Bullet){
            if(p.mount<0){ // Preserve enemy fallback profiles until their routines are mapped.
                explode(p.position,p.radius,p.damage);
                auto splash=[&](Pilot* pilot){if(length(pilot->position+Vec3{0,6,0}-p.position)<p.radius+3)damagePilot(*pilot,p.damage,p.position-pilot->bodyPoint(6));};
                if(p.radius>0){if(cooperative)for(auto* pilot:teammates)splash(pilot);else splash(this);}
                destruction(p.position,p.kind==ProjectileKind::Bomb?20.f:10.f,p.weapon);
            }else{
                float size=10,radius=0,rate=0;Vec3 center=p.position;
                if(p.weapon==Weapon::Grenade){size=20;radius=15;rate=125;center.y=std::max(center.y,surfaceHeight(center.x,center.z)+6);}
                else if(p.weapon==Weapon::AutoGrenade){size=4;center.y=std::max(center.y,surfaceHeight(center.x,center.z)+1.5f);}
                else if(p.weapon==Weapon::Rocket){
                    if(s.target){radius=5;rate=s.target->spawnType<0?25.f:s.target->spawnType==8?0.f:35.f;}
                    else if(s.pilot){radius=5;rate=35;}
                    else size=4;
                }
                destruction(center,size,p.weapon,radius,rate);
            }
            sounds.push_back({SoundEvent::Explosion,int(p.weapon),p.position});
        }else if(s.first<=1)effect(WeaponEffect::Impact,p.position,p.velocity,p.weapon,p.weapon==Weapon::Rail?2.f:.7f);
    }
    if(missionStatus==MissionStatus::Range)for(auto& t:targets)if(t.active&&t.health<=0&&!t.processed)targetDestroyed(t);
    std::erase_if(projectiles,[](const auto& p){return p.life<=0;});
}
}
