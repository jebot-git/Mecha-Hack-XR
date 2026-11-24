#include "rpg/enemy_animation.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace iron;using namespace iron::rpg;
namespace {
void check(bool pass,const char *message){if(!pass)throw std::runtime_error(message);}
Vec3 front(const PartPose &p){return rotate(p.orientation,{0,0,-1});}
Vec3 up(const PartPose &p){return rotate(p.orientation,{0,1,0});}
bool same(const PartPose &a,const PartPose &b){return length(a.offset-b.offset)<.0001f&&length(front(a)-front(b))<.0001f&&length(up(a)-up(b))<.0001f;}
EnemyPart part(EnemyJoint role,int parent=-1,float side=0){EnemyPart p;p.joint=role;p.parent=parent;p.side=side;p.low={-.1f,0,-.1f};p.high={.1f,.2f,.1f};p.pivot={side*.2f,.1f,0};return p;}
void finite(const WalkerPose &pose){for(const auto &p:pose.parts)check(valid(p.orientation)&&std::isfinite(length(p.offset)),"Motion profile produced a nonfinite transform");}
}
int main(){try{
 // Aircraft structural hull banks and pitches as an airframe. Aim yaw cannot
 // turn it like a walking torso, and small Wing groups represent control trim.
 EnemyRig jet;jet.motionProfile="flight";jet.parts={part(EnemyJoint::Torso),part(EnemyJoint::Wing,0,-1),part(EnemyJoint::Wing,0,1)};
 auto bank=poseEnemy(jet,4.5f,4,.9f,.4f,0,0),otherElevation=poseEnemy(jet,4.5f,4,.9f,-.4f,0,0);
 check(std::abs(front(bank.parts[0]).x)<.0001f,"Jet hull turns toward weapon yaw");
 check(std::abs(up(bank.parts[0]).x)>.1f,"Jet does not bank with steering");
 check(same(bank.parts[0],otherElevation.parts[0]),"Jet hull follows weapon aim elevation");
 for(int sample=0;sample<=120;++sample){auto pose=poseEnemy(jet,0,4,0,0,0,sample/12.f);
  for(size_t i=1;i<pose.parts.size();++i)check(length(up(pose.parts[i])-up(pose.parts[0]))<.03f,"Jet control surfaces flap like creature wings");
 }
 // An emplacement keeps its foundation immobile while turret/barrel tracking
 // consumes the same aim and recoil inputs. Movement phase cannot rock its base.
 EnemyRig emplacement;emplacement.motionProfile="stationary";
 emplacement.parts={part(EnemyJoint::Torso),part(EnemyJoint::Turret,0),part(EnemyJoint::Barrel,1)};
 const auto rest=poseEnemy(emplacement,0,0,0,0,0,0);
 auto firing=poseEnemy(emplacement,4.5f,8,1.f,.4f,.12f,1.3f);
 check(same(rest.parts[0],firing.parts[0]),"Stationary emplacement foundation moves under combat inputs");
 check(!same(rest.parts[1],firing.parts[1])&&!same(rest.parts[2],firing.parts[2]),"Stationary profile froze turret or barrel");
 // Naval turret tracking leaves hull orientation unchanged at a shared time.
 // Gentle heave still provides an observable afloat idle rather than rigidity.
 EnemyRig vessel;vessel.motionProfile="naval";vessel.parts={part(EnemyJoint::Torso),part(EnemyJoint::Turret,0)};
 const auto seaA=poseEnemy(vessel,0,0,0,0,0,2),seaB=poseEnemy(vessel,4.5f,8,1,.4f,.12f,2),seaRest=poseEnemy(vessel,0,0,0,0,0,0);
 check(same(seaA.parts[0],seaB.parts[0]),"Naval hull follows turret aiming or walking phase");
 check(!same(seaA.parts[1],seaB.parts[1]),"Naval turret does not track independently");
 check(!same(seaA.parts[0],seaRest.parts[0]),"Naval profile has no gentle hull motion");
 check(length(up(seaA.parts[0])-Vec3{0,1,0})<.025f,"Naval hull uses oversized walking/flight roll");
 // Hover chassis keeps weapon yaw separate from its hull and limits its bank.
 EnemyRig hover;hover.motionProfile="hover";hover.parts={part(EnemyJoint::Torso)};
 const auto hovering=poseEnemy(hover,4.5f,8,1.5f,.4f,.12f,1);
 check(std::abs(front(hovering.parts[0]).x)<.0001f,"Hover hull yaws like a biped torso");
 check(std::abs(up(hovering.parts[0]).x)<.081f,"Hover chassis banks beyond its stable hull limit");
 // Paired mandibles hinge out in opposite horizontal directions. A legacy jaw
 // remains a vertical hinge even if authored with a side marker.
 EnemyRig insect;insect.motionProfile="insect";insect.parts={part(EnemyJoint::Rigid),part(EnemyJoint::Jaw,0,-1),part(EnemyJoint::Jaw,0,1)};
 const auto bite=poseEnemy(insect,0,0,0,0,.12f,0);const auto left=front(bite.parts[1]),right=front(bite.parts[2]);
 check(left.x>.15f&&right.x<-.15f&&std::abs(left.x+right.x)<.0001f&&std::abs(left.y-right.y)<.0001f,"Insect mandibles fail to mirror horizontally");
 auto legacyJaw=insect;legacyJaw.motionProfile="legacy";const auto oldBite=poseEnemy(legacyJaw,0,0,0,0,.12f,0);
 check(std::abs(front(oldBite.parts[1]).x)<.0001f&&front(oldBite.parts[1]).y>.1f,"Legacy jaw no longer uses its original vertical hinge");
 // Stern propellers rotate about the shaft, while hover fans retain their
 // vertical default. Aircraft control surfaces pitch about their span.
 EnemyRig propeller;propeller.motionProfile="naval";propeller.parts={part(EnemyJoint::Rotor)};
 propeller.parts[0].rotationAxis='z';
 auto propSpin=poseEnemy(propeller,0,0,0,0,0,.1f);
 check(length(rotate(propSpin.parts[0].orientation,{0,0,1})-Vec3{0,0,1})<.0001f&&std::abs(up(propSpin.parts[0]).x)>.5f,"Stern propeller leaves its authored shaft axis");
 propeller.parts[0].rotationAxis=0;auto hoverSpin=poseEnemy(propeller,0,0,0,0,0,.1f);
 check(length(up(hoverSpin.parts[0])-Vec3{0,1,0})<.0001f&&std::abs(front(hoverSpin.parts[0]).x)>.5f,"Default hover rotor lost vertical shaft axis");
 EnemyRig trim;trim.motionProfile="flight";trim.parts={part(EnemyJoint::Wing,-1,1)};
 auto trimPose=poseEnemy(trim,0,4,0,0,0,1);
 check(length(rotate(trimPose.parts[0].orientation,{1,0,0})-Vec3{1,0,0})<.0001f&&front(trimPose.parts[0]).y>.01f,"Aircraft trim does not hinge about the span");
 trim.parts[0].rotationAxis='z';auto foldPose=poseEnemy(trim,0,4,0,0,0,1);
 check(length(front(foldPose.parts[0])-Vec3{0,0,-1})<.0001f&&std::abs(up(foldPose.parts[0]).x)>.01f,"Authored wing fold axis is ignored");
 EnemyRig tail;tail.motionProfile="serpent";tail.parts={part(EnemyJoint::Tail)};tail.parts[0].rotationAxis='x';
 auto tailPose=poseEnemy(tail,4.5f,4,0,0,0,0);
 check(std::abs(front(tailPose.parts[0]).x)<.0001f&&front(tailPose.parts[0]).y>.05f,"Vertical tail hinge uses a horizontal swing");
 EnemyRig splayed;
 splayed.archetype="crawler";splayed.motionProfile="insect";
 splayed.parts={part(EnemyJoint::Rigid),part(EnemyJoint::Hip,0,1),part(EnemyJoint::Knee,1,1),part(EnemyJoint::Foot,2,1)};
 splayed.parts[1].pivot={.5f,1,0};splayed.parts[2].pivot={1.4f,1.5f,.2f};splayed.parts[3].pivot={2.2f,.2f,.5f};
 const auto forwardStep=poseEnemy(splayed,3,3,0,0,0,1,{},0,{0,0,-3});
 const auto lateralStep=poseEnemy(splayed,3,3,0,0,0,1,{},0,{3,0,0});
 const auto foot=splayed.parts[3].pivot;
 check(std::abs(animatedPoint(forwardStep.parts[3],foot).x-foot.x)<.0001f,
       "Splayed foot drifts laterally during a forward stance");
 check(std::abs(animatedPoint(lateralStep.parts[3],foot).x-foot.x)>.08f,
       "Splayed foot does not track lateral travel");
 for(const auto &pose:{forwardStep,lateralStep}){
  const auto hip=animatedPoint(pose.parts[1],splayed.parts[1].pivot);
  const auto knee=animatedPoint(pose.parts[2],splayed.parts[2].pivot);
  const auto ankle=animatedPoint(pose.parts[3],foot);
  check(std::abs(length(ankle-knee)-length(foot-splayed.parts[2].pivot))<.0001f&&
        std::abs(length(knee-hip)-length(splayed.parts[2].pivot-splayed.parts[1].pivot))<.0001f,
        "Splayed gait changes the limb length");
 }
 EnemyRig swimmer;swimmer.motionProfile="serpent";
 swimmer.parts={part(EnemyJoint::Torso),part(EnemyJoint::Wing,0,1),
                part(EnemyJoint::Tail,0),part(EnemyJoint::Tail,2)};
 swimmer.parts[3].phase=1.2f;
 const auto strokeA=poseEnemy(swimmer,0,4,0,0,0,0);
 const auto strokeB=poseEnemy(swimmer,0,4,0,0,0,.65f);
 check(!same(strokeA.parts[1],strokeB.parts[1])&&!same(strokeA.parts[3],strokeB.parts[3]),
       "Swimmer stalls when the host walking clock is stopped");
 // A default-constructed rig retains original torso/head aiming proportions.
 EnemyRig legacy;legacy.parts={part(EnemyJoint::Torso),part(EnemyJoint::Head,0)};
 check(legacy.motionProfile=="legacy","Historical rigs changed their default motion profile");
 const auto aimed=poseEnemy(legacy,0,0,.5f,.4f,0,0);
 const auto torsoForward=front(aimed.parts[0]),headForward=front(aimed.parts[1]);
 check(std::abs(std::atan2(-torsoForward.x,-torsoForward.z)-.3f)<.0001f,"Legacy torso aim fraction changed");
 check(std::abs(std::atan2(-headForward.x,-headForward.z)-.5f)<.0001f&&std::abs(headForward.y-std::sin(.18f))<.0001f,"Legacy head parenting/aim fractions changed");
 // An articulated chassis can already contribute to aim before the turret
 // rotates. The barrel must complete the requested aim, not repeat it.
 EnemyRig armed;armed.archetype="crawler";
 armed.parts={part(EnemyJoint::Torso),part(EnemyJoint::Turret,0),part(EnemyJoint::Barrel,1)};
 const auto tracking=poseEnemy(armed,0,0,.8f,.4f,0,0);
 check(length(front(tracking.parts[2])-rotate(angles(.8f,.4f),{0,0,-1}))<.0001f,"Turret or barrel double-counts inherited aim");
 const auto kicking=poseEnemy(armed,0,0,.8f,.4f,.12f,0);
 const auto settling=poseEnemy(armed,0,0,.8f,.4f,.03f,0);
 check(length(kicking.parts[2].offset-tracking.parts[2].offset)>2*length(settling.parts[2].offset-tracking.parts[2].offset),"Mechanical recoil does not settle after its initial impulse");
 // Exercise every joint in each profile with nonfinite and extreme finite
 // network clocks/inputs, including oscillators used only by rotors/scanners.
 size_t samples=0;
 for(const char *profile:{"legacy","flight","hover","naval","stationary","organic","insect","serpent","infantry"}){
  EnemyRig all;all.motionProfile=profile;
  for(int joint=0;joint<=int(EnemyJoint::Scanner);++joint)all.parts.push_back(part(EnemyJoint(joint),-1,joint%2?1:-1));
  for(float time:{0.f,std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
   finite(poseEnemy(all,std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),time));++samples;
  }
 }
 std::cout<<"Enemy motion profiles: aircraft control trim/bank, fixed emplacement foundation, independent naval turret, stable hover, mirrored mandibles, legacy aiming and "<<samples<<" extreme-clock joint samples verified\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
