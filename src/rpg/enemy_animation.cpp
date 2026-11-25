#include "rpg/enemy_animation.hpp"
#include "rpg/original_assets.hpp"
#include "walker_gait.hpp"
#include "simulation.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
namespace iron::rpg {
namespace {
using RigLibrary=std::map<std::string,EnemyRig>;
std::map<std::filesystem::path,RigLibrary> libraries;
constexpr const char *rigFiles[]={"voidmaw-proportion-rigs.json","story-enemy-rigs.json","enemy-reference-rigs.json","enemy-redesign-rigs.json","mecha-redesign-enemy-rigs.json","enemy-animation-rigs.json","manual-animation-rigs.json"};
std::mutex rigMutex;
constexpr const char *roles[]={"rigid","torso","head","hip","knee","foot","arm","tail","wing","rotor","turret","barrel","suspension","jaw","scanner"};
constexpr float maxGunElevation=.6f;
float finite(float value){return std::isfinite(value)?value:0;}

Quaternion alignLimb(Vec3 from,Vec3 to){
  from=unit(from);
  to=unit(to);
  const float cosine=dot(from,to);
  if(cosine<-.9999f){
    const Vec3 axis=unit(cross(from,std::abs(from.y)<.9f?Vec3{0,1,0}:Vec3{1,0,0}));
    return {axis.x,axis.y,axis.z,0};
  }
  const Vec3 axis=cross(from,to);
  const float scale=std::sqrt(2*(1+cosine));
  return {axis.x/scale,axis.y/scale,axis.z/scale,scale*.5f};
}

void reachSplayedLeg(Vec3 upper,Vec3 lower,Vec3 travel,Quaternion &hip,Quaternion &knee){
  const float femur=length(upper),shin=length(lower);
  if(std::min(femur,shin)<.001f)return;
  const Vec3 bindAnkle=upper+lower;
  const Vec3 axis=unit(bindAnkle+travel,unit(bindAnkle));
  const Vec3 bindAxis=unit(bindAnkle);
  const Vec3 bindBend=unit(upper-bindAxis*dot(upper,bindAxis),
                         unit(cross(bindAxis,{0,0,1}),{0,1,0}));
  const Vec3 bend=rotate(alignLimb(bindAxis,axis),bindBend);
  const float reach=std::clamp(length(bindAnkle+travel),std::abs(femur-shin)+.0001f,femur+shin-.0001f);
  const float along=(femur*femur-shin*shin+reach*reach)/(2*reach);
  const Vec3 joint=axis*along+bend*std::sqrt(std::max(0.f,femur*femur-along*along));
  hip=alignLimb(upper,joint);
  const Vec3 shinDirection=axis*reach-joint;
  const Quaternion inverse{-hip.x,-hip.y,-hip.z,hip.w};
  knee=alignLimb(lower,rotate(inverse,shinDirection));
}

// Separate the weight on the feet from the silhouette of the upper body.
// The AI moves the enemy through space; these curves only articulate its rig.
struct VoidmawAction {
  bool active=false;
  float crouch=0,bank=0,dart=0,reach=0,grasp=0,inhale=0,expel=0,heave=0;
  float hang=0,open=0,bite=0;
};

float smoothUnit(float x) {
  x=std::clamp(x,0.f,1.f);
  return x*x*(3.f-2.f*x);
}

struct MechaActionPose {
  Vec3 pelvis{},torso{},weaponArm{},freeArm{}; // Angular channels are yaw, pitch, roll.
  float elbow=0,offElbow=0,stance=0,step=0,weight=0,rack=0;
};

MechaActionPose mechaActionPose(MechaTechnique move,EnemyAction action,float t,float side,
                               float strafe,float kick){
  MechaActionPose p;
  const bool wind=action==EnemyAction::MeleeWindup, strike=action==EnemyAction::Strike;
  const bool firing=action==EnemyAction::Fire, warning=action==EnemyAction::RangedWindup;
  if(move==MechaTechnique::Rush&&wind){
    const float drop=smoothUnit(t/.16f);
    p.weight=drop;
    p.pelvis={0,-.40f,-.20f};p.torso={side*.27f,-.40f,-side*.11f};
    p.weaponArm={side*.20f,-.34f,side*.25f};
    p.freeArm={side*.24f,.90f,-side*.30f};
    p.elbow=.88f;p.offElbow=.44f;p.step=.11f;
  }else if((move==MechaTechnique::Skirt||move==MechaTechnique::MissileRack)&&(warning||firing)){
    const float load=warning?smoothUnit(t/.53f):1.f;
    p.weight=load*(firing?1-smoothUnit((t-.90f)/.10f):1.f);
    if(move==MechaTechnique::Skirt){
      // The hips lead the lateral slide. Chest and sights stay on target.
      p.pelvis={strafe*.27f,-.31f,.12f};p.torso={-strafe*.17f,-.11f,-strafe*.23f};
      // Native gun barrels are authored level in the forearm. Bring the
      // shoulder forward and counter-rotate the elbow to keep bores on target.
      p.weaponArm={.05f,.56f+.10f*kick,.05f};
      p.freeArm={-.05f,.48f+.10f*kick,-.05f};
      p.elbow=-.56f;p.offElbow=-.48f;p.stance=.09f;
    }else{
      p.pelvis={0,-.56f-.12f*kick,.19f};p.torso={0,.21f+.11f*kick,0};
      p.weaponArm={side*.12f,.44f+.12f*kick,side*.38f};
      p.freeArm={-side*.12f,.44f+.12f*kick,-side*.38f};
      p.elbow=p.offElbow=.47f;p.stance=.16f;p.rack=.34f+.08f*kick;
    }
  }else if(move==MechaTechnique::LeftCut||move==MechaTechnique::RightCut||move==MechaTechnique::Overhead){
    const float direction=move==MechaTechnique::LeftCut?1.f:move==MechaTechnique::RightCut?-1.f:side;
    if(wind||strike){
      // Chamber and hold the tell. The center of the cut crosses forward
      // 70–90 ms after launch, when the authoritative slash reaches the pilot.
      p.weight=wind?smoothUnit(t/.56f):1-smoothUnit((t-.49f)/.51f);
      const float sweep=strike?smoothUnit(t/(move==MechaTechnique::Overhead?.38f:.44f)):0;
      const bool high=move==MechaTechnique::Overhead;
      p.pelvis={direction*(.29f-.52f*sweep),-.36f-.19f*sweep,.29f-.78f*sweep};
      p.torso={direction*(high?-.17f+.42f*sweep:-.64f+1.33f*sweep),
               high?.21f-.63f*sweep:.06f-.36f*sweep,direction*(.13f-.28f*sweep)};
      p.weaponArm={direction*(high?-.12f+.29f*sweep:-.94f+1.88f*sweep),
                   high?2.37f-1.56f*sweep:1.43f+.16f*sweep,
                   high?side*.10f:direction*(.45f-.87f*sweep)};
      p.freeArm={side*.27f,.90f-.69f*sweep,-side*(.21f+.44f*sweep)};
      p.elbow=high?.64f-.47f*sweep:.62f-.50f*sweep;
      p.offElbow=.49f;p.stance=.08f;p.step=.10f+.13f*sweep;
    }else if(action==EnemyAction::Guard||action==EnemyAction::Parry){
      const bool counter=action==EnemyAction::Parry;
      p.weight=counter?1-smoothUnit((t-.22f)/.78f):smoothUnit(t/.18f)*(1-smoothUnit((t-.82f)/.18f));
      p.pelvis={-.19f*side,-.33f,.30f};p.torso={-.27f*side,.17f,side*.14f};
      p.weaponArm={side*(counter?.98f:.68f),1.51f,side*.20f};
      p.freeArm={-side*.39f,.97f,-side*.12f};p.elbow=.74f;p.offElbow=.48f;p.stance=.12f;
    }else if(action==EnemyAction::Stagger){
      p.weight=1-smoothUnit((t-.18f)/.82f);
      p.pelvis={-direction*.27f,-.30f,.46f};p.torso={direction*.53f,.42f,-direction*.28f};
      p.weaponArm={-side*.76f,.35f,side*.81f};
      p.freeArm={side*.36f,.65f,-side*.33f};p.stance=.14f;
    }
  }
  return p;
}

VoidmawAction sampleVoidmaw(const VoidmawPoseCue &cue,const EnemyCombatMotion &motion,float now,float gait) {
  VoidmawAction a;
  if(cue.phase==VoidmawPose::Default||!std::isfinite(cue.started)||!std::isfinite(cue.duration))return a;
  const double age=double(now)-cue.started;
  if(age<0||cue.duration<0||(cue.duration>0&&age>cue.duration))return a;
  const float t=cue.duration>0?float(std::clamp(age/cue.duration,0.,1.)):0.f;
  const float enter=smoothUnit(float(age/.11));
  const float side=cue.side<0?-1.f:1.f;
  a.active=true;
  switch(cue.phase){
  case VoidmawPose::Pursuit:
    a.dart=enter*(.50f+.14f*std::cos(2*gait));
    a.bank=enter*.16f*std::sin(gait);
    break;
  case VoidmawPose::Dodge:{
    const float push=smoothUnit(t/.23f)*(1-smoothUnit((t-.58f)/.42f));
    a.bank=side*push;
    a.dart=.35f*push;
    a.crouch=.38f*push;
    break;
  }
  case VoidmawPose::Coil:
    a.crouch=smoothUnit(t/.7f);
    a.grasp=-.42f*a.crouch;
    break;
  case VoidmawPose::Tackle:{
    const float launch=smoothUnit(t/.28f);
    a.crouch=1-launch;
    a.dart=launch;
    a.reach=launch;
    a.grasp=-.42f*(1-launch);
    break;
  }
  case VoidmawPose::Cling:{
    a.hang=smoothUnit(float(age/.34));
    a.dart=1-a.hang;
    a.reach=1-.48f*a.hang;
    a.grasp=.82f*a.hang;
    a.crouch=.23f*a.hang;
    if(motion.active(now)){
      const float progress=motion.phase(now);
      if(motion.action==EnemyAction::MeleeWindup){
        // The neck pulls away first. The last fifth of the tell snaps
        // the jaw closed so the damage tick coincides with tooth contact.
        a.bite=smoothUnit((progress-.78f)/.22f);
        a.open=smoothUnit(progress/.55f)*(1-a.bite);
      }else if(motion.action==EnemyAction::Strike){
        a.bite=1-smoothUnit((progress-.20f)/.80f);
      }
    }
    break;
  }
  case VoidmawPose::Recover:
    a.dart=.65f*(1-smoothUnit(t));
    a.reach=.50f*(1-smoothUnit(t/.55f));
    a.crouch=.55f*smoothUnit(t/.22f)*(1-smoothUnit((t-.25f)/.75f));
    break;
  case VoidmawPose::Lumber:
    a.heave=enter*std::sin(gait);
    a.crouch=enter*(.17f+.07f*std::cos(gait*2));
    break;
  case VoidmawPose::Brace:
    a.inhale=smoothUnit(t/.84f);
    a.crouch=.55f*a.inhale;
    break;
  case VoidmawPose::Discharge:{
    const float release=smoothUnit(t/.13f);
    a.inhale=1-release;
    a.expel=release*(.85f+.15f*std::cos(age*12));
    a.crouch=.55f+.12f*release;
    break;
  }
  case VoidmawPose::SiegeRecover:
    a.expel=.85f*(1-smoothUnit(t/.65f));
    a.crouch=.67f*(1-smoothUnit(t));
    break;
  default:break;
  }
  return a;
}

}
static const RigLibrary* loadRigLibrary(const std::filesystem::path& filename){
  auto library=libraries.find(filename);
  if(library==libraries.end()){
    std::ifstream file(filename);if(!file)return nullptr;
    nlohmann::json data;file>>data;
    if(data.at("schema")!=1||data.at("rigs").size()>256)throw std::runtime_error("Invalid enemy animation library");
    std::map<std::string,EnemyRig> parsed;
    for(const auto &[id,row]:data.at("rigs").items()){
      EnemyRig rig;rig.archetype=row.at("archetype");rig.floor=row.at("floor");rig.vertices=row.at("mesh_vertices");
      rig.motionProfile=row.value("motion_profile",std::string("legacy"));
      const std::string profiles[]={"legacy","flight","hover","naval","stationary","organic","insect","serpent","infantry","tracked"};
      if(std::find(std::begin(profiles),std::end(profiles),rig.motionProfile)==std::end(profiles))throw std::runtime_error("Invalid enemy motion profile");
      if(!std::isfinite(rig.floor)||rig.vertices>12288||rig.vertices%3||row.at("parts").empty()||row.at("parts").size()>128)throw std::runtime_error("Invalid enemy animation budget");
      std::set<size_t> covered;size_t offset=0;
      for(const auto &p:row.at("parts")){
        EnemyPart part;part.name=p.at("name");part.parent=p.at("parent");part.side=p.value("side",0.f);part.phase=p.value("phase",0.f);part.ground=p.value("ground",false);
        auto role=p.at("role").get<std::string>();auto found=std::find(std::begin(roles),std::end(roles),role);
        if(found==std::end(roles)||part.parent>=int(rig.parts.size())||part.parent< -1||!std::isfinite(part.side)||!std::isfinite(part.phase))throw std::runtime_error("Invalid enemy animation hierarchy");
        part.joint=EnemyJoint(found-std::begin(roles));
        if(p.contains("rotation_axis")){
          auto axis=p.at("rotation_axis").get<std::string>();
          if(axis.size()!=1||axis.find_first_not_of("xyz")!=std::string::npos||
             (part.joint!=EnemyJoint::Rotor&&part.joint!=EnemyJoint::Wing&&part.joint!=EnemyJoint::Jaw&&part.joint!=EnemyJoint::Tail))throw std::runtime_error("Invalid enemy hinge axis");
          part.rotationAxis=axis[0];
        }
        auto vec=[&](const char *key){auto a=p.at(key).get<std::array<float,3>>();Vec3 v{a[0],a[1],a[2]};if(!std::isfinite(length(v))||length(v)>1000)throw std::runtime_error("Invalid enemy animation pivot");return v;};
        part.pivot=vec("pivot");part.low=vec("low");part.high=vec("high");size_t count=0;
        for(const auto &range:p.at("ranges")){
          auto a=range.get<std::array<size_t,2>>();
          if(a[0]%3||a[1]%3||a[0]+a[1]>rig.vertices||!a[1])throw std::runtime_error("Invalid enemy animation face range");
          for(size_t i=a[0];i<a[0]+a[1];i+=3)if(!covered.insert(i).second)throw std::runtime_error("Overlapping enemy animation parts");
          part.ranges.emplace_back(a[0],a[1]);count+=a[1];
        }
        if(!count)throw std::runtime_error("Empty enemy animation part");
        if(rig.turretBarrel<0&&(rig.motionProfile=="tracked"||rig.motionProfile=="hover")&&
           part.joint==EnemyJoint::Barrel&&part.parent>=0&&rig.parts[size_t(part.parent)].joint==EnemyJoint::Turret)
          rig.turretBarrel=int(rig.parts.size());
        rig.parts.push_back(std::move(part));rig.drawRanges.emplace_back(offset,count);offset+=count;
      }
      if(offset!=rig.vertices||covered.size()!=rig.vertices/3)throw std::runtime_error("Enemy animation omits geometry");
      // Forearm and blade are often one rigid piece. Use the weapon-bearing
      // arm for both the forehand and backhand, and the other arm to balance.
      if(rig.motionProfile=="legacy"&&(rig.archetype=="biped"||rig.archetype=="crawler")){
        std::array<float,2> reach{};
        for(size_t i=0;i<rig.parts.size();++i){
          const auto &piece=rig.parts[i];
          int parent=int(i);
          while(parent>=0&&rig.parts[size_t(parent)].joint!=EnemyJoint::Arm)parent=rig.parts[size_t(parent)].parent;
          if(parent<0)continue;
          const auto &arm=rig.parts[size_t(parent)];
          const int hand=arm.pivot.x<0?0:1;
          reach[hand]=std::max(reach[hand],arm.pivot.y-piece.low.y);
        }
        if(std::abs(reach[0]-reach[1])>.14f*std::max(reach[0],reach[1]))
          rig.bladeSide=reach[0]>reach[1]?-1:1;
      }
      parsed.emplace(id,std::move(rig));
    }
    library=libraries.emplace(filename,std::move(parsed)).first;
  }
  return &library->second;
}
void preloadEnemyAnimationRigs(const std::filesystem::path& root){
  std::scoped_lock lock(rigMutex);
  for(auto name:rigFiles)loadRigLibrary(root/name);
}
const EnemyRig *enemyAnimationRig(const std::filesystem::path &root,const std::string &asset,size_t vertices){
  if(asset.empty())return nullptr;
  std::scoped_lock lock(rigMutex);
  for(auto name:rigFiles){
    const auto* library=loadRigLibrary(root/name);
    if(!library)continue;
    auto found=library->find(asset);
    if(found==library->end())continue;
    if(found->second.vertices!=vertices)throw std::runtime_error("Enemy animation mesh does not match its authored ranges: "+asset);
    return &found->second;
  }
  return nullptr;
}
WalkerPose poseEnemy(const EnemyRig &rig,float phase,float speed,float aimYaw,float aimPitch,float recoil,float seconds,
                     const EnemyCombatMotion &motion,unsigned missingArms,Vec3 localVelocity,const VoidmawPoseCue &voidmaw){
  WalkerPose out;out.parts.resize(rig.parts.size());
  phase=finite(phase);speed=finite(speed);seconds=finite(seconds);
  // Keep finite extreme timestamps from overflowing oscillator multipliers.
  const bool clinging=voidmaw.phase==VoidmawPose::Cling;
  const double animationTime=clinging?0:seconds;
  aimYaw=wrap(finite(aimYaw));
  if(rig.turretBarrel<0)aimYaw=std::clamp(aimYaw,-pi/2,pi/2);
  aimPitch=std::clamp(finite(aimPitch),-maxGunElevation,maxGunElevation);
  recoil=std::pow(std::clamp(finite(recoil)/.12f,0.f,1.f),1.6f);
  float moving=std::clamp(std::abs(speed)/2,0.f,1.f),cycle=std::remainder(phase,18.f)*2*pi/18;
  if(clinging){moving=0;cycle=0;}
  const bool flight=rig.motionProfile=="flight",hover=rig.motionProfile=="hover",naval=rig.motionProfile=="naval";
  const bool tracked=rig.motionProfile=="tracked";
  const bool walker=rig.archetype=="biped"||rig.archetype=="crawler"||rig.archetype=="kaiju"||rig.archetype=="infantry";
  const bool wideStance=rig.archetype=="crawler"||rig.archetype=="infantry";
  const bool reptile=rig.motionProfile=="organic",arthropod=rig.motionProfile=="insect";
  const bool serpent=rig.motionProfile=="serpent";
  const bool mechanical=rig.motionProfile=="legacy"&&(rig.archetype=="biped"||rig.archetype=="crawler");
  const auto legs=arthropod?std::count_if(rig.parts.begin(),rig.parts.end(),[](const auto &p){return p.joint==EnemyJoint::Hip;}):0;
  const bool arachnid=legs>=8;
  const bool swimmer=serpent&&std::any_of(rig.parts.begin(),rig.parts.end(),[](const auto &p){return p.joint==EnemyJoint::Wing;});
  if(swimmer)cycle=float(std::remainder(animationTime*2.7,double(2*pi)));
  const Vec3 planarVelocity{finite(localVelocity.x),0,finite(localVelocity.z)};
  const Vec3 travel=unit(planarVelocity);
  const float dash=mechanical?std::clamp((std::hypot(planarVelocity.x,planarVelocity.z)-12.f)/8.f,0.f,1.f):0.f;
  const Vec3 slide=travel*dash;
  const auto creature=sampleVoidmaw(voidmaw,motion,seconds,cycle);
  const auto action=(!creature.active||motion.action==EnemyAction::Stagger)&&motion.active(seconds)?
      motion.action:EnemyAction::None;
  const float progress=motion.phase(seconds);
  auto ease=[](float t){t=std::clamp(t,0.f,1.f);return t*t*(3-2*t);};
  const float aim=std::clamp(finite(motion.aim),0.f,1.f);
  const float retreat=std::clamp(finite(motion.retreat),0.f,1.f);
  // The strike starts in the held anticipation pose, hits early, then
  // resolves to the locomotion pose after a readable follow-through.
  const float release=action==EnemyAction::Strike?ease(progress/(mechanical?.25f:.19f)):0.f;
  const float follow=action==EnemyAction::Strike?1-ease((progress-.36f)/.64f):0.f;
  const float wind=action==EnemyAction::MeleeWindup?ease(progress/.70f):(1-release)*follow;
  const float slash=release*follow;
  const float followThrough=action==EnemyAction::Strike?
      ease((progress-.22f)/.27f)*(1-ease((progress-.55f)/.45f)):0.f;
  const float rangedWind=action==EnemyAction::RangedWindup?ease(progress*1.3f):0;
  const float fire=action==EnemyAction::Fire?1-ease(progress):0;
  const float guard=action==EnemyAction::Guard?ease(progress*5.f)*(1-ease(progress*5.f-4.f)):0;
  const float parry=action==EnemyAction::Parry?(1-ease(progress))*(.6f+.4f*ease(progress*5.f)):0;
  const float stagger=action==EnemyAction::Stagger?1-ease(progress):0;
  const float guardPose=std::max(guard,parry);
  const auto technique=mechanical?motion.technique:MechaTechnique::None;
  float swingSide=1.f;
  if(technique!=MechaTechnique::None){
    swingSide=rig.bladeSide!=0?float(rig.bladeSide):technique==MechaTechnique::RightCut?-1.f:1.f;
  }
  if(missingArms&1u)swingSide=1.f;
  if(missingArms&2u)swingSide=-1.f;
  const auto mech=mechaActionPose(technique,action,progress,swingSide,
      std::clamp(planarVelocity.x/4.f,-1.f,1.f),recoil);
  const float twist=swingSide*(wind*.33f-slash*.82f);
  const float brace=aim*.08f+rangedWind*.09f+guardPose*.08f;
  const float lunge=wind*-.10f+slash*.25f;
  const float kick=fire*.12f+stagger*.22f;
  const float contact=arachnid?.67f:arthropod?.58f:wideStance?.7f:.62f;
  // A rush drives the feet through the stride until the torso commits to a cut.
  const bool rushing=technique==MechaTechnique::Rush&&action==EnemyAction::MeleeWindup;
  moving*=rushing?1-dash*.48f:(1-dash*.94f)*(1-std::max(wind,slash)*.72f);
  const float weight=moving*(.06f+.04f*std::cos(2*cycle));
  const float mechanicalTwist=swingSide*(-wind*.42f+slash*.68f+followThrough*.22f);
  const Vec3 baseShift=mechanical?Vec3{
      swingSide*(wind*.16f-slash*.23f)+std::sin(cycle)*moving*.095f,
      -(.27f*wind+.21f*slash+.28f*dash+.075f*retreat+weight),
      wind*.30f-slash*.38f+slide.z*.28f}:Vec3{};
  const Vec3 pelvisShift=baseShift*(1-mech.weight)+mech.pelvis*mech.weight;
  // Articulated legs share the hip's timing, regardless of how the knee and
  // foot phase fields were authored. Simple one-piece legs use hinge motion.
  std::array<Quaternion,128> legPose{};
  std::array<float,128> toePitch{},parentYaw{},parentPitch{};
  std::array<bool,128> pelvis{};
  if(rig.parts.size()>legPose.size())throw std::runtime_error("Invalid enemy animation budget");
  // The simulation already reverses the gait clock for backward motion.
  const Vec3 strideAxis=travel.z>0?travel*(-1):travel;
  for(size_t i=0;i<rig.parts.size();++i){
    const auto &part=rig.parts[i];
    if(part.parent>=int(i)||part.parent< -1)throw std::runtime_error("Invalid enemy animation hierarchy");
    if(part.joint==EnemyJoint::Hip){
      const auto step=sampleStep(cycle+part.phase,contact);
      if(mechanical&&part.parent>=0&&rig.parts[size_t(part.parent)].joint==EnemyJoint::Rigid)
        pelvis[size_t(part.parent)]=true;
      const float lead=part.side==swingSide?1.f:-.48f;
      legPose[i]=angles(0,-step.stride*.24f*moving+(mechanical?lead*(wind*.10f-slash*.29f):0));
    }
    if(part.joint!=EnemyJoint::Foot||part.parent<0)continue;
    const size_t knee=size_t(part.parent);
    const auto &shin=rig.parts[knee];
    if(shin.joint!=EnemyJoint::Knee||shin.parent<0)continue;
    const size_t hip=size_t(shin.parent);
    const auto &thigh=rig.parts[hip];
    if(thigh.joint!=EnemyJoint::Hip)continue;
    const Vec3 upper=shin.pivot-thigh.pivot,lower=part.pivot-shin.pivot;
    const float span=std::abs(part.pivot.y-thigh.pivot.y);
    const float staggeredPhase=arachnid?thigh.pivot.z*.20f:0;
    const auto step=sampleStep(cycle+thigh.phase+staggeredPhase,contact);
    if(creature.active){
      const float reach=length(upper+lower);
      const bool fore=thigh.pivot.z<rig.parts.front().pivot.z;
      // Forelegs come forward to catch the cockpit, hind legs push off.
      // Compress both chains when coiling so floor planting lowers the body.
      const float stride=step.stride*reach*.23f*moving;
      const Vec3 stance{
        thigh.side*(creature.crouch*.09f-creature.grasp*.12f)-creature.bank*.13f+thigh.side*creature.hang*(fore?.34f:.14f),
        creature.crouch*.25f+step.lift*moving*.17f+creature.reach*(fore?.15f:.04f)+creature.hang*(fore?.59f:.21f),
        creature.reach*(fore?-.33f:.26f)+creature.grasp*(fore?-.17f:.10f)-creature.hang*(fore?.54f:.34f)};
      reachSplayedLeg(upper,lower,strideAxis*(-stride)+stance*reach,legPose[hip],legPose[knee]);
    }else if(arthropod&&moving>0){
      const float reach=length(upper+lower);
      const float stride=reach*(arachnid?.18f:.22f)*moving;
      const float swing=reach*(arachnid?.14f:.20f)*step.lift*moving;
      const Vec3 target=strideAxis*(-step.stride*stride)+Vec3{thigh.side*swing*.43f,swing,0};
      reachSplayedLeg(upper,lower,target,legPose[hip],legPose[knee]);
    }else if(mechanical){
      const float stride=step.stride*span*(wideStance?.23f:.29f)*moving;
      const float lead=thigh.side==swingSide?1.f:-.55f;
      const float combatStride=lead*(wind*.07f-slash*.20f)*span;
      const float stepLift=step.lift*span*(wideStance?.14f:.19f)*moving;
      const float plant=mech.weight;
      const Vec3 target=strideAxis*(-stride)+Vec3{
          (slide.x*.16f+plant*mech.stance*thigh.side)*span,stepLift,
          combatStride*(1-plant)+(slide.z*.17f-plant*mech.step*lead)*span}-pelvisShift;
      if(length(target)>.0001f)
        reachSplayedLeg(upper,lower,target,legPose[hip],legPose[knee]);
      else legPose[hip]={};
    }else{
      const float strideDirection=reptile?std::abs(travel.z):1.f;
      const auto solution=moving>0?reachAnkle(upper,lower,step.stride*span*(wideStance?.20f:.24f)*moving*strideDirection+slide.z*span*.16f,
                                             step.lift*span*(reptile?.20f:.17f)*moving+weight+dash*span*.10f):LegAngles{};
      legPose[hip]=angles(0,solution.hip);
      legPose[knee]=angles(0,solution.knee);
    }
    toePitch[i]=-.10f*step.lift*step.stride*moving;
  }
  for(size_t i=0;i<rig.parts.size();++i){
    const auto &p=rig.parts[i];
    float wave=std::sin(cycle+p.phase);
    float idle=std::sin(animationTime*1.7f+p.phase);Quaternion q{};Vec3 shift{};
    const float inheritedYaw=p.parent>=0?parentYaw[size_t(p.parent)]:0;
    const float inheritedPitch=p.parent>=0?parentPitch[size_t(p.parent)]:0;
    float yaw=inheritedYaw,pitch=inheritedPitch;
    auto hinge=[&](float angle,char fallback){
      char axis=p.rotationAxis?p.rotationAxis:fallback;
      if(axis=='x')return angles(0,angle,0);
      if(axis=='z')return angles(0,0,angle);
      return angles(angle);
    };
    switch(p.joint){
    case EnemyJoint::Rigid:
      if(pelvis[i])shift=pelvisShift;
      else if(p.name=="backpack"&&mech.rack>0)q=angles(0,mech.rack*mech.weight);
      else if(mechanical&&p.parent>=0&&rig.parts[size_t(p.parent)].joint==EnemyJoint::Arm){
        const float side=p.side!=0?std::copysign(1.f,p.side):std::copysign(1.f,p.pivot.x);
        const float lead=side==swingSide?1.f:.30f;
        // Exported forearms share the shoulder hierarchy but have their own
        // elbow pivots. Fold for anticipation and extend into the attack.
        const float original=lead*(wind*.51f-slash*.32f)+guardPose*.26f+aim*.14f+dash*.28f;
        q=angles(0,std::lerp(original,side==swingSide?mech.elbow:mech.offElbow,mech.weight));
      }
      break;
    case EnemyJoint::Torso:
      if(flight){q=angles(0,-moving*.025f+retreat*.19f-brace*.38f+kick*.65f+wind*.16f-slash*.24f,
                         std::clamp(aimYaw*.25f,-.22f,.22f)+retreat*.12f+twist*.19f);shift.y=std::sin(animationTime*.8f)*.025f;}
      else if(tracked){q={};}
      else if(hover&&rig.turretBarrel>=0){shift.y=std::sin(animationTime*1.2f)*.065f;}
      else if(hover){q=angles(0,-moving*.018f+retreat*.15f-brace*.30f+kick*.55f,std::clamp(aimYaw*.1f,-.08f,.08f));shift.y=std::sin(animationTime*1.2f)*.035f;}
      else if(naval){q=angles(0,std::sin(animationTime*.45f)*.008f,std::sin(animationTime*.6f)*.012f);shift.y=std::sin(animationTime*.5f)*.025f;}
      else if(rig.motionProfile=="stationary"){q={};}
      else if(mechanical){
        yaw+=aimYaw*.54f+mechanicalTwist-std::sin(cycle)*moving*.08f;
        pitch+=wind*.17f-slash*.31f+kick*.8f-brace-moving*.065f+retreat*.16f+slide.z*.22f;
        float roll=std::sin(cycle)*moving*.055f+wind*swingSide*.10f-slash*swingSide*.16f+slide.x*.24f;
        yaw=std::lerp(yaw,aimYaw*.40f+mech.torso.x,mech.weight);
        pitch=std::lerp(pitch,mech.torso.y,mech.weight);roll=std::lerp(roll,mech.torso.z,mech.weight);
        q=angles(yaw-inheritedYaw,pitch-inheritedPitch,roll);
        shift.z=(recoil*.10f+stagger*.24f-slash*.14f)*(1-mech.weight);
        if(p.parent<0)shift=shift+pelvisShift;
      }else if(arthropod){
        q=angles(aimYaw*.40f+std::sin(cycle)*.042f*moving,
                 wind*.13f-slash*.30f+kick*.65f-brace,
                 -travel.x*.085f*moving+idle*.012f-twist*.14f);
        shift.z=wind*.24f-slash*.48f;
        yaw=aimYaw*.40f;
      }else if(swimmer){
        q=angles(aimYaw*.3f+std::sin(cycle)*.11f*moving,
                 aimPitch*.38f+wind*.20f-slash*.39f,idle*.045f+twist*.16f);
        shift.z=wind*.31f-slash*.65f;
      }
      else{
        const float lean=walker?moving*(.035f-retreat*.17f):0;
        yaw+=aimYaw*.6f+twist+(reptile?std::sin(cycle)*.17f*moving:0);
        pitch+=recoil*.055f-lean-brace-lunge+kick+slide.z*.18f;
        q=angles(yaw-inheritedYaw,pitch-inheritedPitch,std::sin(cycle)*.028f*moving-parry*swingSide*.13f-slide.x*.18f);
        shift.x=walker?std::sin(cycle)*.075f*moving:0;
        shift.z=recoil*.065f+stagger*.28f-slash*.22f;
        if(walker)shift.y=-retreat*.16f-dash*.48f;
      }
      break;
    case EnemyJoint::Head:
      if(arthropod)q=angles(aimYaw*.5f+std::sin(animationTime*3.2)*.038f*(1-aim),
                           aimPitch*.55f+wind*.17f-slash*.23f+stagger*.09f);
      else if(serpent)q=angles(aimYaw*.57f+std::sin(cycle)*.075f*moving,
                              aimPitch*.55f+wind*.24f-slash*.40f+idle*.045f);
      else q=angles(aimYaw*.4f-(mechanical?mechanicalTwist*.65f:twist*.7f)+idle*.025f*(1-aim)-(reptile?std::sin(cycle)*.12f*moving:0),
                    aimPitch*.45f-recoil*.035f+lunge*.55f-stagger*.12f);
      if(mech.weight>0)q=compose(q,angles(-.56f*mech.torso.x*mech.weight,-.35f*mech.torso.y*mech.weight));
      break;
    case EnemyJoint::Hip:
      if(mechanical&&(p.parent<0||rig.parts[size_t(p.parent)].joint!=EnemyJoint::Rigid))shift=pelvisShift;
      q=legPose[i];break;
    case EnemyJoint::Knee:q=legPose[i];break;
    case EnemyJoint::Foot:
      if(p.parent>=0){
        const auto parent=out.parts[size_t(p.parent)].orientation;
        const Vec3 facing=rotate(parent,{0,0,-1});
        const auto flat=angles(std::atan2(-facing.x,-facing.z),toePitch[i]);
        q=compose(Quaternion{-parent.x,-parent.y,-parent.z,parent.w},flat);
      }
      break;
    case EnemyJoint::Arm:
      {
        const float side=p.side!=0?std::copysign(1.f,p.side):p.pivot.x<0?-1.f:1.f;
        if(arthropod){
          const float reach=wind*.34f+slash*.88f;
          q=angles(side*(.13f*wave*moving+wind*.42f-slash*.54f+guardPose*.28f),
                   aimPitch*.44f+reach-kick*.44f,side*(.09f+guardPose*.18f));
          break;
        }
        const float lead=side==swingSide?1.f:.32f;
        if(mechanical){
          const float rangedAim=1-std::max(wind,slash)*.86f;
          const float armYaw=lead*swingSide*(-wind*.62f+slash*.88f+followThrough*.32f-parry*.84f)+
              side*(guardPose*.43f+rangedAim*aim*.12f)+(1-lead)*swingSide*slash*.34f;
          const float elevation=rangedAim*(aimPitch*.65f+aim*(.43f+lead*.62f))+rangedWind*.23f+
              guardPose*.86f+lead*(wind*.70f+slash*1.34f-followThrough*.37f)-dash*.24f-
              stagger*.26f-wave*.26f*moving*(1-aim);
          Vec3 desired=side==swingSide?mech.weaponArm:mech.freeArm;
          if(technique==MechaTechnique::Skirt){
            desired.x+=aimYaw*.60f-mech.torso.x;
            desired.y+=aimPitch-mech.torso.y;
          }
          q=angles(std::lerp(armYaw,desired.x,mech.weight),
                   std::lerp(elevation-inheritedPitch-recoil*.14f-fire*.22f,desired.y,mech.weight),
                   std::lerp(side*(guardPose*.23f-stagger*.12f-wind*lead*.42f+slash*.09f),desired.z,mech.weight));
        }else{
          pitch=aimPitch*.65f+aim*(.38f+lead*.64f)+rangedWind*.28f+guardPose*.85f
              +lead*(wind*.82f+slash*1.22f)-stagger*.32f;
          const float armYaw=lead*swingSide*(wind*.45f-slash*1.28f-parry*.95f)+side*(guardPose*.55f+aim*.14f);
          q=angles(armYaw,pitch-inheritedPitch-wave*.12f*moving*(1-aim)-recoil*.11f-fire*.16f,
                   side*(guardPose*.26f+stagger*.18f));
        }
        shift.z=(1-mech.weight)*(recoil*.045f+fire*.09f+lead*(wind*.12f-slash*.28f));
        break;
      }
    case EnemyJoint::Tail:
      if(p.rotationAxis)q=hinge(wave*(serpent?.12f:.09f)*moving+idle*.025f,'y');
      else if(serpent){
        const float wave=std::sin(cycle-p.phase),swell=std::cos(cycle-p.phase);
        q=angles(wave*moving*(swimmer?.21f:.10f)+idle*.035f,
                 swell*moving*(swimmer?.12f:.052f)+std::sin(p.phase+.4f)*wind*.07f);
      }else if(reptile){
        q=angles(std::sin(cycle-p.phase)*.21f*moving+idle*.031f-twist*.13f,
                 -(.06f+.054f*std::cos(cycle-p.phase))*moving-wind*.075f-slash*.03f);
      }else if(arthropod){
        q=angles(-travel.x*.12f*moving+idle*.025f,wind*.20f-slash*.28f+wave*.052f*moving);
      }else q=hinge(wave*.09f*moving+idle*.025f,'y');
      break;
    case EnemyJoint::Wing:
      if(flight)q=hinge(p.side*(aimYaw*.07f+moving*.018f+std::sin(animationTime*1.1f+p.phase)*.004f),'x');
      else if(swimmer&&!p.rotationAxis)q=angles(p.side*std::sin(cycle-p.phase)*.23f*moving,
                               0,p.side*(std::cos(cycle-p.phase)*.21f*moving+idle*.045f));
      else if(reptile&&!p.rotationAxis){
        const double stroke=animationTime*5.8;
        const float flap=float(std::sin(stroke)+.20*std::sin(2*stroke));
        q=angles(p.side*(brace*.38f+wind*.25f),
                 p.side*aimYaw*.08f,p.side*(flap*(.41f-.18f*moving)+.26f*slash));
      }
      else q=hinge(p.side*(std::sin(animationTime*4+p.phase)*(arthropod?.18f:.075f)+recoil*.025f),'z');
      break;
    case EnemyJoint::Rotor:q=hinge(std::remainder(animationTime*18+p.phase,double(2*pi)),'y');break;
    case EnemyJoint::Turret:
      if(rig.turretBarrel>=0){
        yaw=aimYaw;
        q=angles(yaw-inheritedYaw);
        break;
      }
      yaw=aimYaw;pitch=aimPitch*.2f;
      q=angles(yaw-inheritedYaw+idle*.02f,pitch-inheritedPitch-recoil*.045f-kick*.5f-rangedWind*.06f+
               aim*.035f+wind*.11f-slash*.19f);
      break;
    case EnemyJoint::Barrel:
      if(int(i)==rig.turretBarrel){
        pitch=aimPitch;
        q=angles(0,pitch-inheritedPitch);
        shift=rotate(q,{0,0,recoil*std::clamp((p.high.z-p.low.z)*.12f,.08f,.62f)});
        break;
      }
      if(!(mechanical&&p.parent>=0&&rig.parts[size_t(p.parent)].joint==EnemyJoint::Arm&&std::max({wind,slash,guardPose})>0)){
        yaw=aimYaw;pitch=aimPitch;
        q=angles(yaw-inheritedYaw,pitch-inheritedPitch-recoil*.075f-kick*.6f);
      }
      shift=rotate(q,{0,0,std::max({recoil,fire,slash*.58f})*std::clamp((p.high.z-p.low.z)*.09f,.03f,.48f)});
      break;
    case EnemyJoint::Suspension:if(!tracked)shift.y=idle*.035f+wave*.018f*moving;break;
    case EnemyJoint::Jaw:
      if(arthropod&&p.side!=0)q=hinge(p.side*(recoil*.24f+idle*.025f+wind*.56f-slash*.60f),'y');
      else q=hinge(recoil*.15f+idle*.025f+wind*.60f-slash*.65f,'x');
      break;
    case EnemyJoint::Scanner:
      if(arthropod)q=angles(p.side*(.20f*std::sin(animationTime*4.3+p.phase)+wind*.26f),
                            .11f*std::cos(animationTime*5.1+p.phase)-slash*.27f);
      else q=angles(std::sin(animationTime*.65f+p.phase)*.20f);
      break;
    default:break;
    }
    if(creature.active){
      const auto &a=creature;
      switch(p.joint){
      case EnemyJoint::Rigid:
        // The tunnel wurm carries its neck and body segments on a single root.
        if(serpent&&p.parent<0)
          q=compose(q,angles(-.16f*a.bank,.24f*a.crouch-.31f*a.dart,-.24f*a.bank));
        break;
      case EnemyJoint::Torso:
        q=compose(q,angles(-.10f*a.bank,.18f*a.crouch-.34f*a.dart+.27f*a.inhale-.17f*a.expel,
                           -.34f*a.bank+.11f*a.heave));
        break;
      case EnemyJoint::Head:
        q=compose(q,angles(.18f*a.bank,-.16f*a.crouch+.17f*a.dart-.18f*a.inhale+.12f*a.expel+
                           .20f*a.open-.12f*a.bite,.20f*a.bank-.06f*a.heave));
        if(a.hang>0){
          const float neck=p.high.z-p.low.z;
          shift.z+=neck*(.13f*(1-a.bite)+.13f*a.open)*a.hang;
        }
        break;
      case EnemyJoint::Arm:
        q=compose(q,angles(p.side*(-.44f*a.grasp+.16f*a.crouch-.64f*a.hang),
                           .86f*a.reach+.25f*a.grasp-.30f*a.crouch+.20f*a.expel,
                           p.side*(.17f*a.crouch+.22f*a.inhale)));
        break;
      case EnemyJoint::Foot:
        if(a.hang>0&&p.parent>=0){
          // Soles curl around the cowling with their limbs once off the ground.
          q=angles(0,-.43f*a.hang,-p.side*.19f*a.hang);
        }
        break;
      case EnemyJoint::Jaw:{
        const float open=.35f*a.crouch+.57f*a.reach-.65f*a.grasp+.34f*a.inhale+.65f*a.expel+
                         a.hang*(.41f+.68f*a.open-.24f*a.bite);
        const bool paired=arthropod&&p.side!=0;
        q=compose(q,hinge(paired?-p.side*open:-open,paired?'y':'x'));
        break;
      }
      case EnemyJoint::Tail:
        if(swimmer&&a.hang>0&&p.parent>=0&&rig.parts[size_t(p.parent)].joint==EnemyJoint::Torso&&
           p.pivot.z<rig.parts[size_t(p.parent)].pivot.z){
          // Spread the forward tentacles out around the canopy rim.
          q=compose(q,angles(-std::copysign(.98f,p.pivot.x)*a.hang));
        }
        q=compose(q,angles(.12f*a.bank-.06f*a.heave+
                           (serpent?(.16f*a.crouch+.13f*a.hang)*std::cos(p.phase):0.f),
                           .085f*a.dart-.07f*a.inhale+.05f*a.expel+.075f*a.hang));
        break;
      case EnemyJoint::Wing:
        q=compose(q,hinge(p.side*(.33f*a.inhale+.18f*a.expel-.18f*a.dart),'z'));
        break;
      default:break;
      }
    }
    parentYaw[i]=yaw;parentPitch[i]=pitch;
    PartPose local{q,p.pivot-rotate(q,p.pivot)+shift};
    if(p.parent<0)out.parts[i]=local;
    else{const auto &parent=out.parts[size_t(p.parent)];out.parts[i]={compose(parent.orientation,q),animatedPoint(parent,local.offset)};}
  }
  // Plant the lowest sole. Counter-rotation keeps stance feet level while
  // joint motion advances the swing foot; no whole-body gait approximation.
  if(voidmaw.phase==VoidmawPose::Cling&&creature.active)return out;
  float bottom=1e9f;
  for(size_t i=0;i<rig.parts.size();++i){const auto &p=rig.parts[i];if(p.joint!=EnemyJoint::Foot&&!p.ground)continue;
    const auto &pose=out.parts[i];const auto q=pose.orientation;
    const auto up=rotate(Quaternion{-q.x,-q.y,-q.z,q.w},{0,1,0});
    const Vec3 lowest{up.x<0?p.high.x:p.low.x,up.y<0?p.high.y:p.low.y,up.z<0?p.high.z:p.low.z};
    bottom=std::min(bottom,dot(up,lowest)+pose.offset.y);
  }
  if(bottom<1e8f){out.vertical=rig.floor-bottom;for(auto &p:out.parts)p.offset.y+=out.vertical;}
  return out;
}

std::optional<EnemySocket> voidmawBiteSocket(const EnemyRig &rig,const Target &body,float side){
  if(!body.mesh||body.mesh->size()!=rig.vertices)return {};
  const auto facing=angles(pi+side*voidmawHangYaw,voidmawHangPitch,side*voidmawHangRoll);
  const auto contact=voidmawCanopyContact(side);
  const float dzdx=(cockpitFrontGlass(contact.x+.01f,contact.y).z-
                    cockpitFrontGlass(contact.x-.01f,contact.y).z)/.02f;
  const float dzdy=(cockpitFrontGlass(contact.x,contact.y+.01f).z-
                    cockpitFrontGlass(contact.x,contact.y-.01f).z)/.02f;
  const Vec3 towardGlass=rotate(gravityInverse(facing),{-dzdx,-dzdy,1});
  const auto bite=poseEnemy(rig,0,0,0,0,0,1,
      {EnemyAction::Strike,1,voidmawBiteFollow},{},{},{VoidmawPose::Cling,0,0,side});
  std::optional<EnemySocket> socket;
  float nearest=-1e9f;
  for(size_t i=0;i<rig.parts.size();++i){
    const auto &part=rig.parts[i];
    const bool mouth=part.joint==EnemyJoint::Jaw&&part.parent>=0&&
                     rig.parts[size_t(part.parent)].joint==EnemyJoint::Head;
    if(!mouth&&part.joint!=EnemyJoint::Head)continue;
    for(const auto &[first,count]:part.ranges)for(size_t v=first;v<first+count;++v){
      const auto &vertex=(*body.mesh)[v];
      // The accepted rigs assign upper teeth to the skull. Its tooth_ridges
      // tile is column 4, row 4 of the shared 8×8 PSX atlas.
      if(part.joint==EnemyJoint::Head&&
         !(vertex.textured&&int(vertex.u*8)==4&&int(vertex.v*8)==4))continue;
      const auto point=vertex.position;
      const auto atImpact=animatedPoint(bite.parts[i],point);
      const float front=dot(atImpact,towardGlass);
      if(front>nearest){socket=EnemySocket{i,point,atImpact};nearest=front;}
    }
  }
  return socket;
}

bool traverseTankGun(Target &body,Vec3 target,float dt){
  const auto &gravity=body.ai.gravity;
  const auto center=body.center+gravity.up()*(body.ai.localHalf.y*.2f);
  const auto ray=rotate(gravityInverse(gravity.frame),target-center);
  const float yaw=std::atan2(-ray.x,-ray.z),pitch=std::atan2(ray.y,std::hypot(ray.x,ray.z));
  const float turn=wrap(yaw-body.ai.weaponYaw);
  body.ai.weaponYaw=wrap(body.ai.weaponYaw+std::clamp(turn,-2.7f*dt,2.7f*dt));
  body.ai.weaponPitch+=std::clamp(pitch-body.ai.weaponPitch,-1.6f*dt,1.6f*dt);
  const auto local=rotate(gravityInverse(angles(body.yaw,body.ai.pitch,body.ai.bank)),ray);
  const float elevation=std::atan2(local.y,std::hypot(local.x,local.z));
  return std::abs(elevation)<=maxGunElevation&&std::abs(wrap(yaw-body.ai.weaponYaw))<.035f&&
         std::abs(pitch-body.ai.weaponPitch)<.035f;
}

std::optional<GunSocket> tankGunSocket(const Target &body,float seconds){
  if(!body.mesh)return {};
  const auto *rig=enemyAnimationRig(originalAssetRoot()/"models",body.generatorAsset,body.mesh->size());
  if(!rig||rig->turretBarrel<0)return {};
  const auto orientation=compose(body.ai.gravity.frame,angles(body.yaw,body.ai.pitch,body.ai.bank));
  const auto pose=poseLiveEnemy(*rig,body,orientation,seconds);
  const size_t index=size_t(rig->turretBarrel);
  const auto &barrel=rig->parts[index];
  const auto &joint=pose.parts[index];
  const Vec3 tip{barrel.pivot.x,barrel.pivot.y,barrel.low.z};
  return GunSocket{body.origin+rotate(orientation,animatedPoint(joint,tip)),
                   rotate(compose(orientation,joint.orientation),{0,0,-1})};
}

WalkerPose poseLiveEnemy(const EnemyRig &rig,const Target &body,Quaternion orientation,float seconds){
  const auto inverse=gravityInverse(orientation);
  const auto &ai=body.ai;
  float yaw=wrap(ai.weaponYaw-body.yaw),pitch=ai.weaponPitch;
  if(rig.motionProfile=="tracked"||rig.turretBarrel>=0){
    // Suspension follows the ground, while the gun retains its world-space aim.
    const auto aim=rotate(inverse,rotate(ai.gravity.frame,direction(ai.weaponYaw,ai.weaponPitch)));
    yaw=std::atan2(-aim.x,-aim.z);
    pitch=std::atan2(aim.y,std::hypot(aim.x,aim.z));
  }
  const float speed=ai.gravity.active()?length(ai.velocity):std::hypot(ai.velocity.x,ai.velocity.z);
  return poseEnemy(rig,ai.gaitPhase,speed,yaw,pitch,ai.muzzleFlash,seconds,ai.combatMotion,
                   body.rpgEnemyMissingArms,rotate(inverse,ai.velocity),ai.voidmawPose);
}
}
