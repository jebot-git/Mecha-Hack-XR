#include "simulation.hpp"
#include "presentation.hpp"
#include <limits>
#include <iostream>
#include <cstdlib>
using namespace iron;
static int count=0;
static void check(bool b,const char* what){++count;if(!b){std::cerr<<what<<'\n';std::exit(1);}}
static bool near(Vec3 a,Vec3 b,float e=.001f){return length(a-b)<e;}
static World armed(){World w;w.targets.clear();w.configure({Weapon::Rifle,Weapon::Rifle,Weapon::Rocket,Weapon::Rail,Weapon::Grenade,Weapon::Empty});return w;}
int main(){
    Input a,b;a.fire[0]=b.fire[0]=true;b.headYaw=1.2f;b.headPitch=.6f;
    World wa=armed(),wb=armed();wa.step(a,.01f);wb.step(b,.01f);
    check(near(wa.arms[0].wrist,wb.arms[0].wrist),"head rotation cannot drag wrist");
    check(near(wa.projectiles[0].velocity,wb.projectiles[0].velocity),"head rotation cannot aim hand gun");
    check(near(wa.projectiles[0].position,wa.arms[0].muzzle+wa.projectiles[0].velocity*.01f),"bullet originates at rendered muzzle");
    b.handPoses[0].aim=b.handPoses[0].grip=angles(.7f,.3f,.4f);wb=armed();wb.step(b,.01f);
    check(near(unit(wb.projectiles[0].velocity),direction(.7f,.3f)),"controller yaw and pitch aim gun");
    check(!near(rotate(wb.arms[0].aim,{0,1,0}),rotate(angles(.7f,.3f),{0,1,0})),"controller roll retained");
    auto old=wb.arms[0].wrist;b.handPoses[0].position.x+=.1f;b.handPoses[0].position.y+=.1f;b.handPoses[0].position.z+=.2f;wb=armed();wb.step(b,.01f);
    check(length(wb.arms[0].wrist-old)>.1f,"tracked XYZ translation moves wrist");
    check(std::abs(length(wb.arms[0].elbow-wb.arms[0].shoulder)-wb.armRig.upperLength)<.001f,"IK upper length constant");
    check(std::abs(length(wb.arms[0].wrist-wb.arms[0].elbow)-wb.armRig.foreLength)<.001f,"IK forearm length constant");
    b.handPoses[0].position={-1,0,-2};wb=armed();wb.step(b,.01f);
    check(length(wb.arms[0].wrist-wb.arms[0].shoulder)<=wb.armRig.upperLength+wb.armRig.foreLength,"reach clamps without stretching");
    b=Input{};b.fire[0]=b.fire[1]=true;b.handPoses[0].valid=false;wb=armed();wb.step(b,.01f);
    check(!wb.arms[0].tracked&&wb.shots[0]==0&&wb.shots[1]==1,"tracking loss disables only affected hand");
    b.handPoses[0].valid=true;b.handPoses[0].aim={0,0,0,0};wb=armed();wb.step(b,.01f);check(wb.shots[0]==0,"invalid quaternion cannot fire");
    b.handPoses[0].aim={};b.handPoses[0].position.x=std::numeric_limits<float>::quiet_NaN();wb=armed();wb.step(b,.01f);check(!wb.arms[0].tracked,"NaN hand position rejected");
    b=Input{};b.fire[2]=true;b.headYaw=.8f;b.headPitch=.4f;wb=armed();wb.step(b,.01f);
    Vec3 origin=wb.projectiles[0].position-wb.projectiles[0].velocity*.01f;
    check(near(unit(wb.projectiles[0].velocity),unit(attackDirection(wb.eye()+direction(attackYaw(.8f),.4f)*80-origin,wb.facing()))),"shoulder keeps convergence inside its torso-relative yaw limit");
    World torso=armed();torso.torsoYaw=.8f;Input in;in.fire[0]=true;torso.step(in,.01f);
    check(near(unit(torso.projectiles[0].velocity),direction(.8f,0)),"torso transform applied once to controller aim");
    World missile=armed();missile.configure({Weapon::Empty,Weapon::Rifle,Weapon::Cruise,Weapon::Empty,Weapon::Empty,Weapon::Empty});
    missile.step(Input{},.01f);
    missile.torsoYaw=.4f;in=Input{};in.headYaw=1;in.headPitch=.5f;in.fire[2]=true;missile.step(in,.01f);
    check(missile.missile.active&&std::abs(missile.missile.yaw-.4f)<.001f&&std::abs(missile.missile.pitch)<.001f,"cruise ignores launch head angle");
    in.headYaw=1.3f;in.headPitch=.7f;missile.step(in,.05f);check(missile.missile.yaw>.4f&&missile.missile.pitch>0,"post-launch head movement steers cruise");
    check(missile.arms[0].tracked&&missile.arms[1].tracked,"cruise retains tracked hands for swing melee");
    in.punch[1]=true;auto guidedPunches=missile.punches[1];missile.step(in,.02f);
    check(missile.punches[1]==guidedPunches+1&&missile.missile.active,"melee remains available during cruise guidance");
    in.punch[1]=false;in.headYaw=missile.missile.headYaw+pi;
    for(int n=0;n<30;++n)missile.step(in,.05f);
    check(missile.missile.active&&std::abs(wrap(missile.missile.yaw-missile.missile.launchYaw))<=headYawLimit+.0001f,"head-guided cruise cannot steer behind the stationary torso");
    World punch=armed();in=Input{};punch.step(in,.01f);Vec3 contact=punch.arms[0].wrist+punch.arms[0].forward*1.7f;
    punch.targets={{{contact.x,contact.y,contact.z},{.1f,.1f,.1f},100}};in.headYaw=2;in.punch[0]=true;punch.step(in,.01f);
    check(punch.targets[0].health<100,"punch contacts hand position, not gaze");
    in.focused=false;punch.step(in,.01f);check(!punch.arms[0].tracked&&!punch.arms[1].tracked,"focus loss invalidates visible arms");
    for(int hand=0;hand<2;++hand)for(auto weapon:{Weapon::Empty,Weapon::Rifle,Weapon::Cutter,Weapon::Shotgun,Weapon::HeavyMachinegun}){
        World swing=armed();swing.weapons[hand]=weapon;Input pose;pose.handValid[hand]=true;
        swing.step(pose,.02f);swing.fistFlash[hand]=.4f; // even an active cutter effect cannot block a swing
        auto ammo=swing.ammo[hand];auto shots=swing.shots[hand];auto punches=swing.punches[hand];
        pose.hands[hand].z-=.06f;pose.fire[hand]=true;swing.step(pose,.02f);
        check(swing.punches[hand]==punches+1,"equipped hand swing always punches");
        check(swing.shots[hand]==shots&&swing.ammo[hand]==ammo,"swing takes priority over held trigger without ammo use");
        check(swing.sounds.back().kind==SoundEvent::Punch,"swing produces punch audio, not weapon firing audio");
        swing.step(pose,.02f);check(swing.shots[hand]==shots,"trigger suppressed during punch recovery");
    }
    World aim=armed();Target farTarget;farTarget.center={0,12,-30};farTarget.half={2,2,2};Target nearTarget=farTarget;nearTarget.center.z=-10;
    aim.targets={farTarget,nearTarget};auto ray=aim.traceAim({0,12,0},{0,0,-1});
    check(ray.target==1&&near(ray.position,{0,12,-8}),"reticle ray hits nearest surface regardless of target order");
    aim.targets[1].health=0;check(aim.traceAim({0,12,0},{0,0,-1}).target==0,"reticle ignores destroyed objects");
    ray=aim.traceAim({10,12,0},{0,-1,0});check(ray.target==-1&&near(ray.position,{10,0,0}),"reticle ray intersects terrain");
    aim.cooperative=true;aim.missionStatus=MissionStatus::Active;aim.elapsed=1;aim.position={0,0,0};aim.pingMap(0);
    check(aim.selfPingUntil==7,"self highlight lasts six seconds");aim.elapsed=1.1f;aim.pingMap(0);check(aim.selfPingUntil==7,"ping rate limited");
    aim.pingMap(1);check(aim.pingTarget==0,"head ping selects aimed target");aim.targets[0].center.x=5;
    check(near(aim.mapPingPosition(aim),aim.targets[0].center),"target ping follows moving target");
    World local;local.targets.clear();local.missionStatus=MissionStatus::Active;local.elapsed=1;Input ping;ping.ping[0]=true;local.step(ping,.02f);
    check(local.selfPingUntil>local.elapsed,"single-player grip/input reaches the local radar marker");
    auto pilots=radarPilots(local);check(pilots.size()==1&&pilots[0].selfPingUntil==local.selfPingUntil,"local marker exists without network snapshots");
    Target scenery,enemy;enemy.spawnType=0;auto sceneryColor=radarColor(scenery,0),enemyColor=radarColor(enemy,0);check(length(sceneryColor-enemyColor)>.5f,"hostile radar color differs from scenery");
    auto nearSight=sightPlacement({1,0,-2}),farSight=sightPlacement({10,0,-80});check(nearSight.position.z==-8&&farSight.position.z==-80,"sights use distant optical depth and actual far impact depth");
    check(std::abs(nearSight.position.x/-nearSight.position.z-.5f)<.0001f,"moving sights farther preserves angular aim alignment");
    aim.lifeSpent=true;aim.elapsed=8;aim.pingMap(0);check(aim.selfPingUntil==7,"dead pilot cannot highlight self");
    for(float facing:{-3.f,-.4f,0.f,2.8f})for(float heading:{-3.f,-1.f,-.6f,0.f,.6f,1.f,3.f}){
        World constrained;constrained.targets.clear();constrained.chassisYaw=facing;constrained.torsoYaw=.2f;
        constrained.configure({Weapon::Rifle,Weapon::Rifle,Weapon::Rocket,Weapon::Rail,Weapon::Grenade,Weapon::Rocket});
        Input pose;pose.headYaw=heading;pose.headPitch=.6f;pose.fire.fill(true);
        for(int h=0;h<2;++h)pose.handPoses[h].aim=pose.handPoses[h].grip=angles(heading,.6f,.3f);
        constrained.step(pose,.01f);check(constrained.projectiles.size()==6,"all six mounts fire within their cone");
        check(std::abs(wrap(constrained.aimYaw-constrained.facing()))<=headYawLimit+.0001f&&std::abs(constrained.aimPitch-.6f)<.0001f,"head attack yaw constrained while vertical aim is retained");
        for(auto& shot:constrained.projectiles){float yaw=std::atan2(-shot.velocity.x,-shot.velocity.z),limit=shot.mount<2?handYawLimit:headYawLimit;check(std::abs(wrap(yaw-constrained.facing()))<=limit+.0001f,"actual projectile yaw respects 90-degree hand / 45-degree body limits including mount parallax");}
        for(int hand=0;hand<2;++hand)check(near(unit(constrained.projectiles[hand].velocity),direction(constrained.facing()+attackYaw(heading,handYawLimit),.6f)),"handheld weapons can reach the full sideways 90-degree stop");
        for(auto& arm:constrained.arms)check(std::abs(arm.forward.y-std::sin(.6f))<.0001f,"hand pitch retained when yaw meets mechanical stop");
    }
    World rear=armed();Input behind;behind.punch[0]=true;behind.handPoses[0].position={-.3f,0,1.4f};behind.handPoses[0].aim=behind.handPoses[0].grip=angles(pi);
    Target victim;victim.center=rear.eye()+Vec3{0,0,5};victim.half={.5f,.5f,.5f};victim.health=100;rear.targets={victim};rear.step(behind,.01f);
    check(rear.arms[0].tracked&&dot(rear.arms[0].wrist-rear.eye(),direction(rear.facing(),0))>=-.0001f&&rear.targets[0].health==100,"controller behind player cannot create rear melee contacts");
    std::cout<<count<<" tracked-arm checks passed\n";
}
