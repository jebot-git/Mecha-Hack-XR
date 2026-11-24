#include "rpg/destruction.hpp"
#include "rpg/catalog.hpp"
#include <algorithm>
#include <array>
namespace iron::rpg {
namespace {
float ease(float a,float b,float x){float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);}
bool named(std::string_view name,std::initializer_list<std::string_view> values){return std::find(values.begin(),values.end(),name)!=values.end();}
}
DestructionMaterial destructionMaterial(std::string_view id){
  if(id.ends_with("_alt"))id.remove_suffix(4);
  for(const auto &e:enemyCatalog())if(id==e.id)return e.kind==EnemyKind::SmallKaiju||e.kind==EnemyKind::LargeKaiju?DestructionMaterial::Organic:DestructionMaterial::Metal;
  if(named(id,{"tree","broadleaf","fallen_log","cactus"}))return DestructionMaterial::Wood;
  if(named(id,{"coral","reef_fan","kelp"}))return DestructionMaterial::Organic;
  if(named(id,{"rock","spire","asteroid","crater","crystal","ice"}))return DestructionMaterial::Rock;
  if(id.starts_with("city_tower_")||named(id,{"building","industrial","is_office","is_warehouse","habitat"}))return DestructionMaterial::Concrete;
  return DestructionMaterial::Metal;
}
bool volatileScenery(std::string_view id){
  if(id.ends_with("_alt"))id.remove_suffix(4);
  return named(id,{"oxygen_tank","ship_reactor","industrial","mining_rig","is_relay"});
}
std::shared_ptr<WorldMesh> destructionWreck(const WorldMesh &source,std::string_view id,size_t budget){
  auto material=destructionMaterial(id);auto wreck=std::make_shared<WorldMesh>();
  if(budget<8||source.empty()){*wreck=source;for(auto &v:*wreck)v.color=v.color*.4f;return wreck;}
  if(material==DestructionMaterial::Wood){
    *wreck=source;float bottom=1e9f;auto q=angles(0,pi/2);
    for(auto &v:*wreck){v.position=rotate(q,v.position);bottom=std::min(bottom,v.position.y);v.color=v.color*.45f;}
    for(auto &v:*wreck)v.position.y-=bottom;
    return wreck;
  }
  bool actor=std::any_of(enemyCatalog().begin(),enemyCatalog().end(),[&](const auto &e){return id==e.id||id==std::string(e.id)+"_alt";});
  if(actor||material==DestructionMaterial::Wood||material==DestructionMaterial::Organic){
    *wreck=source;
    Vec3 low{1e9f,1e9f,1e9f},high{-1e9f,-1e9f,-1e9f};for(const auto &v:source){low={std::min(low.x,v.position.x),std::min(low.y,v.position.y),std::min(low.z,v.position.z)};high={std::max(high.x,v.position.x),std::max(high.y,v.position.y),std::max(high.z,v.position.z)};}
    for(auto &v:*wreck){auto p=v.position;
      v.position={p.x*.9f,(p.z-low.z)*.65f,(p.y-low.y)*.85f-(high.y-low.y)*.35f};
      v.color=v.color*(material==DestructionMaterial::Organic?.60f:.44f);}
    return wreck;
  }
  Vec3 low{1e9f,1e9f,1e9f},high{-1e9f,-1e9f,-1e9f};
  for(const auto &v:source){low={std::min(low.x,v.position.x),std::min(low.y,v.position.y),std::min(low.z,v.position.z)};high={std::max(high.x,v.position.x),std::max(high.y,v.position.y),std::max(high.z,v.position.z)};}
  float width=std::max(1.f,high.x-low.x),depth=std::max(1.f,high.z-low.z),height=std::clamp((high.y-low.y)*.10f,.4f,8.f);
  unsigned seed=2166136261u;for(char c:id){seed^=unsigned(c);seed*=16777619u;}
  int chunks=std::max(1,std::min(8,int(budget/8)));
  for(int n=0;n<chunks;++n){float angle=float(n)*2.39996f+float(seed%1000)*.001f;
    float radius=n?(.2f+.1f*float(n%3)):0;
    float x=std::sin(angle)*width*radius,z=std::cos(angle)*depth*radius;
    float w=width*(n?.18f:.45f),d=depth*(n?.2f:.45f),h=height*(n?.35f+.13f*float(n%4):1.f);
    std::array<Vec3,6> p{{{x-w,0,z-d},{x+w,0,z-d},{x+w,0,z+d},{x-w,0,z+d},{x-w*.55f,h,z-d*.35f},{x+w*.55f,h*.73f,z+d*.4f}}};
    constexpr unsigned faces[][3]={{0,2,1},{0,3,2},{0,1,4},{1,5,4},{1,2,5},{2,3,5},{3,4,5},{3,0,4}};
    for(const auto &f:faces){const auto &style=source[size_t(n*3)%source.size()];Vec3 tint=style.color*(.32f+.07f*float(n%3));
      for(unsigned k=0;k<3;++k){auto v=style;v.position=p[f[k]];v.color=tint;v.u=source[size_t(n*3+k)%source.size()].u;v.v=source[size_t(n*3+k)%source.size()].v;wreck->push_back(v);}}
  }
  return wreck;
}
WalkerPose poseEnemyDeath(const EnemyRig &rig,float phase,float aimYaw,float aimPitch,float age,unsigned seed){
  auto pose=poseEnemy(rig,phase,0,aimYaw,aimPitch,0,0);float t=ease(.06f,1.6f,age),side=(seed&1)?1.f:-1.f;
  bool organic=rig.motionProfile=="organic"||rig.motionProfile=="insect"||rig.motionProfile=="serpent";
  bool flying=rig.motionProfile=="flight";
  auto root=angles(0,(organic?-.18f:.10f)*t,side*(organic?.7f:flying?.45f:1.5f)*t);
  for(size_t i=0;i<rig.parts.size();++i){const auto &part=rig.parts[i];Quaternion q{};
    switch(part.joint){
    case EnemyJoint::Head:q=angles(.18f*side*t,(organic?-.55f:.32f)*t);break;
    case EnemyJoint::Hip:q=angles(0,-(organic?1.1f:.45f)*t,part.side*.18f*t);break;
    case EnemyJoint::Knee:q=angles(0,(organic?-1.2f:1.0f)*t);break;
    case EnemyJoint::Arm:q=angles(part.side*.20f*t,.55f*t,part.side*.35f*t);break;
    case EnemyJoint::Wing:q=angles(0,0,part.side*.75f*t);break;
    case EnemyJoint::Tail:q=angles(.65f*side*t,0);break;
    case EnemyJoint::Jaw:q=angles(0,.18f*t);break;
    case EnemyJoint::Turret:case EnemyJoint::Barrel:q=angles(0,.4f*t);break;
    case EnemyJoint::Rotor:q=angles(std::min(std::max(age,0.f),1.f)*8*(1-t));break;
    default:break;
    }
    PartPose fold{q,part.pivot-rotate(q,part.pivot)};
    if(part.parent>=0){const auto &parent=pose.parts[size_t(part.parent)];fold={compose(parent.orientation,q),animatedPoint(parent,fold.offset)};}
    pose.parts[i]=fold;
  }
  for(auto &p:pose.parts){p.orientation=compose(root,p.orientation);p.offset=rotate(root,p.offset);}
  return pose;
}
CollapsePose collapsePose(float age,unsigned seed){
  float t=ease(.08f,1.6f,age);CollapsePose p;p.intact=age<1.6f;
  p.scale={1+.06f*t,std::max(.075f,(1-t)*(1-t)),1+.06f*t};p.rotation=angles(0,0,((seed&1)?1.f:-1.f)*.025f*t*(1-t));return p;
}
}
