#include "rpg/voidmaw_siege.hpp"
#include "rpg/mission.hpp"
#include "rpg/enemy_animation.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/voidmaw_pose.hpp"

namespace iron::rpg {

SiegeAttack siegeAttack(const EnemySpec &spec, const EnemyAttack &attack) {
  if (spec.kind != EnemyKind::LargeKaiju || !attack.name) return {};
  const std::string_view name = attack.name;
  // Keep all sweep rays inside the three stripes of the native breath tell.
  if (name == "Maw Beam") {
    if (spec.traits & Submersible)
      return {SiegeStyle::Axial, 1.65f, .16f, 2.6f, .34f, 5, 1};
    if (spec.traits & Flying)
      return {SiegeStyle::Sweep, 1.14f, .15f, 1.95f, .39f, 5, 1};
    return {SiegeStyle::Sweep, 1.75f, .17f, 2.85f, .32f, 7, 1};
  }
  // Spitters throw visible, finite-speed shots. A web carries the catalog's
  // immobilization on one shot, so multiple rays cannot reroll the debuff.
  if (name == "Caustic Web")
    return {SiegeStyle::Web, 1.05f, .25f, 2.6f, .72f, 1, 1, 34.f};
  if (name == "Caustic Spines")
    return {SiegeStyle::Spines, 1.55f, .52f, 2.65f, .38f, 2, 3, 46.f};
  if (name == "Caustic Spit")
    return {SiegeStyle::Spit, 1.3f, .42f, 2.35f, .55f, 2, 2, 36.f};
  if (name == "Rocket Elbow")
    return {SiegeStyle::Elbow, 1.1f, .30f, 1.9f, .7f, 2, 1, 44.f};
  return {};
}

Vec3 siegeEmitter(const Target &body, const SiegeAttack &attack, float now, Vec3 fallback) {
  if (!body.mesh) return fallback;
  const auto *rig = enemyAnimationRig(originalAssetRoot()/"models", body.generatorAsset, body.mesh->size());
  if (!rig) return fallback;
  const auto joint = attack.style == SiegeStyle::Elbow ? EnemyJoint::Arm : EnemyJoint::Jaw;
  auto part = std::find_if(rig->parts.begin(),rig->parts.end(),
                           [joint](const auto &p){ return p.joint == joint; });
  if (part == rig->parts.end()) return fallback;
  const auto orientation = compose(body.ai.gravity.frame,angles(body.yaw,body.ai.pitch,body.ai.bank));
  const auto pose = poseLiveEnemy(*rig,body,orientation,now);
  const Vec3 lip{(part->low.x+part->high.x)*.5f,
                 joint==EnemyJoint::Jaw ? part->high.y : (part->low.y+part->high.y)*.5f,
                 part->low.z-.25f};
  return body.origin + rotate(orientation,animatedPoint(pose.parts[size_t(part-rig->parts.begin())],lip));
}

float siegeLaneOffset(const SiegeAttack &pattern, int pulse, int lane, bool reverse) {
  const float side = reverse ? -1.f : 1.f;
  switch (pattern.style) {
  case SiegeStyle::Sweep: {
    const float t = float(pulse) / float(pattern.pulses - 1);
    return side * .11f * (2.f * (t*t*(3.f-2.f*t)) - 1.f);
  }
  case SiegeStyle::Spines:
    return side * ((float(lane)-1.f)*.19f + (pulse ? .07f : 0.f));
  case SiegeStyle::Spit:
    return side * (pulse ? (lane ? .13f : -.035f) : (lane ? 0.f : -.18f));
  case SiegeStyle::Elbow:
    return side * (pulse ? .07f : 0.f);
  default:
    return 0;
  }
}

void steerSiegeVoidmaw(const EnemySpec &spec, Target &body, Vec3 pilot, float now,
                       size_t index, TacticalPlan &plan, Vec3 &destination) {
  if (spec.kind != EnemyKind::LargeKaiju) return;
  const std::string_view id = spec.id;
  if (id == "kaiju_tunnel_wurm") return;
  auto &pose = body.ai.voidmawPose;
  if (pose.phase == VoidmawPose::SiegeRecover && now < pose.started + pose.duration) {
    plan.speed = 0;
    plan.direction = {};
    plan.fire = false;
    destination = body.origin;
    return;
  }
  auto gap = pilot - body.origin;
  gap.y = 0;
  const float distance = length(gap);
  if (distance < 12.f) {
    pose = {};
    return;  // The creature can still bite or grapple at contact.
  }
  const Vec3 forward = gap * (1.f / distance);
  const float side = index % 2 ? -1.f : 1.f;
  const Vec3 flank = cross(forward, {0,1,0}) * side;
  const bool flier = spec.traits & Flying;
  const bool hunter = spec.traits & (Camo | Dismantler);
  const bool brawler = id == "abomination";
  const float range = brawler ? 19.f : hunter ? 29.f : flier ? 40.f : 47.f;
  // Elbows and pouncing ambushers can breach the firing distance and use
  // their close attacks. Slow artillery holds a broadside and reloads planted.
  const float radial = std::clamp((distance-range)/12.f, -.75f, 1.f);
  Vec3 course = forward * (hunter && distance < 22.f ? 1.f : radial);
  const float orbit = flier ? .72f : hunter ? .46f : .24f;
  course = course + flank * orbit;
  const float stride = .5f + .5f * std::cos(now * 3.f + float(index%7));
  plan.speed = (flier ? 1.04f : hunter ? 1.15f : .8f) *
               (flier ? 1.f : .60f + .40f*stride) * std::min(1.f,length(course));
  plan.direction = unit(course);
  destination = body.origin + plan.direction * 18.f;
  plan.preferMelee = false;
  if (pose.phase != VoidmawPose::Lumber)
    pose = {VoidmawPose::Lumber, now, 0.f, side};
}

} // namespace iron::rpg
