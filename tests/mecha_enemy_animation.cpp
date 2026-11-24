#include "rpg/enemy_animation.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/catalog.hpp"
#include "coop.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <set>
#include <string_view>
using namespace iron;using namespace iron::rpg;
static bool different(const WalkerPose &a,const WalkerPose &b){
  for(size_t i=0;i<a.parts.size();++i)if(length(a.parts[i].offset-b.parts[i].offset)>.0001f||length(rotate(a.parts[i].orientation,{0,0,-1})-rotate(b.parts[i].orientation,{0,0,-1}))>.0001f)return true;
  return false;
}
static void combatSilhouettes(const std::filesystem::path &root){
  const auto mesh=originalMesh("gladius_mecha");
  const auto *rig=enemyAnimationRig(root/"models","gladius_mecha",mesh->size());
  assert(rig&&rig->bladeSide==-1);
  auto partIndex=[&](std::string_view name){
    const auto found=std::find_if(rig->parts.begin(),rig->parts.end(),[&](const EnemyPart &p){return p.name==name;});
    assert(found!=rig->parts.end());
    return size_t(found-rig->parts.begin());
  };
  const size_t blade=partIndex("left_forearm"),other=partIndex("right_upper_arm");
  const size_t torso=partIndex("torso"),rack=partIndex("backpack");
  Vec3 tip=rig->parts[blade].pivot;
  for(auto [first,count]:rig->parts[blade].ranges)for(size_t k=first;k<first+count;++k)
    if((*mesh)[k].position.y<tip.y)tip=(*mesh)[k].position;
  auto pose=[&](MechaTechnique move,EnemyAction action,float phase,Vec3 velocity={},float kick=0,unsigned lost=0){
    const EnemyCombatMotion motion{action,1-phase,1,0,0,move};
    return poseEnemy(*rig,0,length(velocity),0,0,kick,1,motion,lost,velocity);
  };
  auto bladeTip=[&](MechaTechnique move,EnemyAction action,float phase){
    return animatedPoint(pose(move,action,phase).parts[blade],tip);
  };
  for(auto cut:{MechaTechnique::LeftCut,MechaTechnique::RightCut,MechaTechnique::Overhead}){
    assert(!different(pose(cut,EnemyAction::MeleeWindup,.99f),pose(cut,EnemyAction::Strike,0)));
    const auto tipAtHit=bladeTip(cut,EnemyAction::Strike,.22f);
    assert(tipAtHit.z< -6&&std::abs(tipAtHit.x)<4);
    for(int frame=0;frame<40;++frame){
      const auto strike=pose(cut,EnemyAction::Strike,frame/40.f);
      for(size_t i=0;i<rig->parts.size();++i){
        assert(valid(strike.parts[i].orientation));
        const auto &part=rig->parts[i];
        if(part.joint!=EnemyJoint::Foot)continue;
        for(auto [first,count]:part.ranges)for(size_t k=first;k<first+count;++k)
          assert(animatedPoint(strike.parts[i],(*mesh)[k].position).y>=rig->floor-.0001f);
      }
    }
  }
  for(auto cut:{MechaTechnique::LeftCut,MechaTechnique::RightCut}){
    const float direction=cut==MechaTechnique::LeftCut?1.f:-1.f;
    assert(bladeTip(cut,EnemyAction::MeleeWindup,.8f).x*direction>4);
    assert(bladeTip(cut,EnemyAction::Strike,.46f).x*direction< -4);
  }
  assert(bladeTip(MechaTechnique::Overhead,EnemyAction::MeleeWindup,.8f).y>
         rig->parts[partIndex("head")].high.y+1);
  assert(bladeTip(MechaTechnique::Overhead,EnemyAction::Strike,.46f).y<2);
  const auto full=pose(MechaTechnique::LeftCut,EnemyAction::MeleeWindup,.8f);
  const auto lost=pose(MechaTechnique::LeftCut,EnemyAction::MeleeWindup,.8f,{},0,1);
  assert(length(rotate(full.parts[other].orientation,{0,-1,0})-
                rotate(lost.parts[other].orientation,{0,-1,0}))>.45f);
  const auto left=pose(MechaTechnique::Skirt,EnemyAction::Fire,.45f,{-5,0,0});
  const auto right=pose(MechaTechnique::Skirt,EnemyAction::Fire,.45f,{5,0,0});
  assert(length(rotate(left.parts[torso].orientation,{0,1,0})-rotate(right.parts[torso].orientation,{0,1,0}))>.28f);
  const auto rifleMesh=originalMesh("mecha_sniper");
  const auto *sniper=enemyAnimationRig(root/"models","mecha_sniper",rifleMesh->size());
  assert(sniper);
  bool checkedRifle=false;
  // The native sniper barrel points down local -Z at bind. Its sights
  // must stay on the target while the chassis leans into the lateral step.
  for(size_t i=0;i<sniper->parts.size();++i){
    if(sniper->parts[i].name!="left_forearm")continue;
    for(float lateral:{-5.f,5.f})for(float elevation:{-.35f,.35f}){
      const auto sight=poseEnemy(*sniper,0,5,.18f,elevation,0,1,
          {EnemyAction::Fire,.55f,1,1,0,MechaTechnique::Skirt},0,{lateral,0,0});
      assert(dot(rotate(sight.parts[i].orientation,{0,0,-1}),direction(.18f,elevation))>.96f);
    }
    checkedRifle=true;
  }
  assert(checkedRifle);
  const auto hold=pose(MechaTechnique::MissileRack,EnemyAction::RangedWindup,.9f);
  const auto launch=pose(MechaTechnique::MissileRack,EnemyAction::Fire,.1f,{},.12f);
  assert(length(rotate(hold.parts[rack].orientation,{0,0,-1})-rotate(launch.parts[rack].orientation,{0,0,-1}))>.10f);
}
int main(){try{
  const auto root=originalAssetRoot();size_t checked=0;std::set<std::string> archetypes;
  for(const auto &enemy:enemyCatalog().first(34))for(const std::string suffix:{"","_alt"}){
    auto id=std::string(enemy.id)+suffix;auto mesh=originalMesh(id);
    auto rig=enemyAnimationRig(root/"models",id,mesh->size());
    if(!rig)throw std::runtime_error("Enemy model has no authored animation: "+id);
    archetypes.insert(rig->archetype);assert(rig->vertices==mesh->size());
    std::set<size_t> covered;size_t hips=0,feet=0,turrets=0;bool contacts=false;
    for(const auto &p:rig->parts){
      hips+=p.joint==EnemyJoint::Hip;feet+=p.joint==EnemyJoint::Foot;turrets+=p.joint==EnemyJoint::Turret;contacts|=p.ground;
      for(auto [first,count]:p.ranges)for(size_t k=first;k<first+count;++k)assert(covered.insert(k).second);
    }
    assert(covered.size()==mesh->size());
    if(rig->archetype=="biped"||rig->archetype=="kaiju")assert(hips>=2&&feet>=2);
    if(rig->archetype=="crawler")assert(hips>=4&&contacts);
    if(rig->archetype=="infantry")assert(hips>=6&&contacts);
    if(rig->archetype=="installation"||rig->archetype=="hovercraft")assert(turrets>=1);
    auto still=poseEnemy(*rig,0,0,0,0,0,0),idle=poseEnemy(*rig,0,0,0,0,0,1.3f);
    auto attack=poseEnemy(*rig,0,0,0,0,.12f,0);
    assert(different(still,idle)&&different(still,attack));
    if(hips){auto forward=poseEnemy(*rig,4.5f,4,0,0,0,0),opposite=poseEnemy(*rig,13.5f,4,0,0,0,0);assert(different(forward,opposite));}
    if(std::string_view(enemy.id)=="jump_pod"||std::string_view(enemy.id)=="mecha_ace"){
      // Some two-part authored legs disagree between the hip and knee/foot
      // phase. Each ankle must inherit the cycle of the actual hip above it.
      for(size_t foot=0;foot<rig->parts.size();++foot){
        const auto &shoe=rig->parts[foot];if(shoe.joint!=EnemyJoint::Foot)continue;
        const size_t knee=size_t(shoe.parent),hip=size_t(rig->parts[knee].parent);
        const float origin=-rig->parts[hip].phase*18/(2*pi);
        const auto front=poseEnemy(*rig,origin+1.8f,4,0,0,0,0);
        const auto rear=poseEnemy(*rig,origin+8.1f,4,0,0,0,0);
        const auto swing=poseEnemy(*rig,origin+14.58f,4,0,0,0,0);
        assert(length(rotate(front.parts[foot].orientation,{0,1,0})-Vec3{0,1,0})<.0001f);
        assert(length(rotate(rear.parts[foot].orientation,{0,1,0})-Vec3{0,1,0})<.0001f);
        assert(animatedPoint(rear.parts[foot],shoe.pivot).z-animatedPoint(front.parts[foot],shoe.pivot).z>.25f);
        assert(animatedPoint(swing.parts[foot],shoe.pivot).y-animatedPoint(front.parts[foot],shoe.pivot).y>.2f);
        const Vec3 hinge=rig->parts[knee].pivot;
        assert(length(animatedPoint(swing.parts[hip],hinge)-animatedPoint(swing.parts[knee],hinge))<.0001f);
      }
    }
    for(int step=0;step<=72;++step){
      auto pose=poseEnemy(*rig,step*.25f,4,.35f,.2f,.06f,step/30.f);float bottom=1e9f;
      for(size_t part=0;part<rig->parts.size();++part){
        assert(valid(pose.parts[part].orientation)&&std::isfinite(length(pose.parts[part].offset)));
        const auto &p=rig->parts[part];if(p.joint!=EnemyJoint::Foot&&!p.ground)continue;
        for(auto [first,count]:p.ranges)for(size_t k=first;k<first+count;++k)bottom=std::min(bottom,animatedPoint(pose.parts[part],(*mesh)[k].position).y);
      }
      if(contacts||feet)assert(bottom>=rig->floor-.0001f);
    }
    auto start=poseEnemy(*rig,0,4,0,0,0,0),end=poseEnemy(*rig,18,4,0,0,0,0);assert(!different(start,end));
    auto clean=poseEnemy(*rig,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),0,0,0,0);
    for(const auto &p:clean.parts)assert(valid(p.orientation)&&std::isfinite(length(p.offset)));
    // New posing consumes only the values included in authoritative
    // world snapshots: no client-only random phase or animation clock.
    World host;host.rpgPrototype=true;host.targets.clear();Target target;applyOriginalModel(target,id);target.ai.gaitPhase=4.25f;
    target.ai.velocity={3,0,1};target.ai.weaponPitch=.2f;target.ai.weaponYaw=.4f;target.ai.muzzleFlash=.08f;
    target.ai.combatMotion={EnemyAction::Fire,2.2f,.3f,.8f,.2f};
    host.targets.push_back(target);host.elapsed=2.3f;World guest=host;guest.targets[0].ai={};coop::worldRead(coop::worldWire(host),guest);
    auto sample=[&](const World &w){const auto &t=w.targets[0];return poseEnemy(*rig,t.ai.gaitPhase,length(t.ai.velocity),t.ai.weaponYaw-t.yaw,t.ai.weaponPitch,t.ai.muzzleFlash,w.elapsed,t.ai.combatMotion);};
    assert(!different(sample(host),sample(guest)));
    assert(guest.targets[0].ai.combatMotion.action==EnemyAction::Fire);
    auto older=coop::worldWire(host);older["targets"][0].erase(14);coop::worldRead(older,guest);
    assert(guest.targets[0].ai.combatMotion.action==EnemyAction::None);
    if(std::string_view(enemy.id)=="mecha_ace"){
      auto pose=[&](EnemyAction action,float phase,float aim=0,float retreat=0){
        return poseEnemy(*rig,4.5f,retreat>0?4.f:0.f,0,0,0,10,{action,10-phase,1,aim,retreat});
      };
      auto relaxed=pose(EnemyAction::None,0),ready=pose(EnemyAction::None,0,1);
      auto windup=pose(EnemyAction::MeleeWindup,.75f),slash=pose(EnemyAction::Strike,.16f);
      auto guard=pose(EnemyAction::Guard,.5f),parry=pose(EnemyAction::Parry,.11f);
      assert(different(ready,relaxed)&&different(pose(EnemyAction::Fire,.1f,1),ready));
      assert(different(windup,relaxed)&&different(slash,windup)&&different(guard,parry));
      assert(!different(pose(EnemyAction::MeleeWindup,.95f),pose(EnemyAction::Strike,0)));
      assert(different(pose(EnemyAction::Strike,.25f),pose(EnemyAction::Strike,.63f)));
      assert(!different(relaxed,pose(EnemyAction::Strike,1.f)));
      const auto forward=poseEnemy(*rig,4.5f,4,0,0,0,10);
      assert(different(forward,pose(EnemyAction::None,0,0,1)));
      const auto leftBoost=poseEnemy(*rig,4.5f,21,0,0,0,10,{},0,{-21,0,0});
      const auto rightBoost=poseEnemy(*rig,4.5f,21,0,0,0,10,{},0,{21,0,0});
      assert(different(leftBoost,rightBoost));
      for(const auto &motion:{windup,slash,leftBoost,rightBoost})
        for(size_t i=0;i<rig->parts.size();++i){
          const auto &part=rig->parts[i];
          assert(valid(motion.parts[i].orientation));
          if(part.joint!=EnemyJoint::Foot)continue;
          for(auto [first,count]:part.ranges)for(size_t vertex=first;vertex<first+count;++vertex)
            assert(animatedPoint(motion.parts[i],(*mesh)[vertex].position).y>=rig->floor-.0001f);
        }
    }
    ++checked;
  }
  assert(checked==68&&archetypes.size()==7);
  combatSilhouettes(root);
  std::cout<<"68 enemy variants: articulated locomotion, idle, attack, contact floors, cycle seams and replicated poses verified\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
