#include "rpg/session.hpp"
#include "rpg/story_world.hpp"
#include "rpg/campaign_story.hpp"
#include "rpg/original_assets.hpp"
#include "skirmish.hpp"
#include "rpg/world_generation.hpp"
#include "rpg/generation.hpp"
#include "rpg/manual_missions.hpp"
#include <algorithm>
#include <stdexcept>
namespace iron::rpg {
Climate missionClimate(int biome, int location) {
  return biome == 0 ? Climate::Desert
       : location == 4 ? Climate::Coastal : Climate::Temperate;
}
bool missionWeatherAllowed(const MissionRecipe &r,Weather weather){
  const int biome=r.environment;
  const bool vacuum=biome==1||biome==7||r.location==7;
  const bool water=biome==2;
  switch(weather){
    case Weather::Snowstorm:return !vacuum&&biome==6;
    case Weather::IonStorm:return !water&&(vacuum||biome!=0);
    case Weather::CorruptionSporeBurst:return !vacuum&&biome==5;
    case Weather::FireStorm:
    case Weather::AshCloud:return !vacuum&&biome==4;
    case Weather::MeteorShower:return !water&&(vacuum||biome==0);
    case Weather::MagneticBurst:return vacuum||water||biome==3||biome==6;
    case Weather::RadioactiveWind:
      return !vacuum&&!water&&(biome==4||((biome==0||biome==3)&&(r.location==2||r.location==5)));
    default:
      if(vacuum||water)return weather==Weather::Clear;
      if(biome==6&&(weather==Weather::Rain||weather==Weather::Storm))return false;
      return weatherAllowed(missionClimate(biome,r.location),weather);
  }
}
EnvironmentSample missionEnvironment(const MissionRecipe &recipe,
                                      const EnvironmentCycle &cycle) {
  auto sample = cycle.sample(missionClimate(recipe.environment, recipe.location));
  sample.settings.authoredSky = recipe.environment == 1 || recipe.environment == 2 ||
                                 recipe.environment == 7 || recipe.location == 7;
  if(recipe.environment==6&&(sample.settings.weather==Weather::Rain||sample.settings.weather==Weather::Storm))
    sample.settings.weather=Weather::Snowstorm;
  if(!missionWeatherAllowed(recipe,sample.settings.weather))
    sample.settings.weather=Weather::Clear;
  Weather scripted=Weather::Clear;
  if(recipe.manualMission>=0){
    if(recipe.planetoidHazard==1)scripted=Weather::MagneticBurst;
    if(recipe.planetoidHazard==3)scripted=Weather::IonStorm;
    if(recipe.planetoidHazard==9)scripted=Weather::MeteorShower;
  }
  if(scripted!=Weather::Clear&&missionWeatherAllowed(recipe,scripted)){
    sample.settings.weather=scripted;
    return sample;
  }
  // A separate region-specific choice keeps the ordinary weather and time
  // stream stable. Every client derives the same hazard from the front seed.
  const float frontRoll=weatherNoise(sample.settings.seed+recipe.seed*0x85ebca6bu,829);
  if(frontRoll<.23f){
    std::array<Weather,weatherNames.size()> eligible{};
    size_t count=0;
    for(int i=int(Weather::IonStorm);i<int(weatherNames.size());++i){
      const auto candidate=Weather(i);
      const auto *hazard=weatherHazard(candidate);
      if(missionWeatherAllowed(recipe,candidate)&&
         (!hazard||hazard->density<.008f||frontRoll<.085f))eligible[count++]=candidate;
    }
    if(count)sample.settings.weather=eligible[std::min(count-1,size_t(weatherNoise(sample.settings.seed^0x68617a64u,17)*count))];
  }
  if((sample.settings.weather==Weather::Fog||sample.settings.weather==Weather::Sandstorm||
      sample.settings.weather==Weather::Storm||sample.settings.weather==Weather::Snowstorm)&&
     weatherNoise(sample.settings.seed+recipe.seed*0x27d4eb2du,719)>.35f)
    sample.settings.weather=Weather::Clear;
  return sample;
}
bool insideArea(Vec3 p, float radius) {
  return std::abs(p.x) <= radius && std::abs(p.z) <= radius;
}
bool clearPosition(const World &world, Vec3 p, float radius, int ignore) {
  if (world.proceduralTerrain && !world.groundPassable(p, radius))
    return false;
  for (size_t i = 0; i < world.targets.size(); ++i) {
    auto &t = world.targets[i];
    if (int(i) == ignore || !t.active || t.health <= 0)
      continue;
    if (std::abs(p.x - t.center.x) < t.half.x + radius &&
        std::abs(p.z - t.center.z) < t.half.z + radius &&
        p.y + 12 > t.center.y - t.half.y && p.y < t.center.y + t.half.y)
      return false;
  }
  return true;
}
void buildMissionEnvironment(World &world, uint32_t seed, int biome,
                             float radius, std::span<const int> enemyIds,
                             int location, int objective, int region, int opener,int landform,int dungeonCount,std::vector<DungeonSite> *sites,const MissionRecipe *story) {
  if (biome < 0 || biome >= int(environmentCatalog().size()) ||
      !std::isfinite(radius) || radius < 500 || radius > 1000)
    throw std::invalid_argument("Invalid RPG environment");
  Dice random{seed};
  world = World{};
  // The authoritative arm solver must use the same original weapon rig as
  // the renderer, including on dedicated servers without a renderer.
  world.armRig=originalArmRig();
  world.targets.clear();
  world.pickups.clear();
  world.rpgPrototype = true;
  constexpr SkyKind skies[]={SkyKind::Desert,SkyKind::Space,SkyKind::Underwater,SkyKind::Urban,
    SkyKind::Volcanic,SkyKind::Wilderness,SkyKind::Polar,SkyKind::Lunar};
  world.skyKind = skies[biome];
  world.prototypeSeed = seed;
  world.missionStatus = MissionStatus::Range;
  world.climate = missionClimate(biome, location);
  world.movementBounds = {-radius, -radius, radius, radius};
  world.maxWadeDepth = 6;
  world.missionEntry = {0,0,radius*.65f};world.hasMissionEntry=true;
  bool interior = location == 7 || region == 2 || region == 3;
  // Reuse the local engine's validated connected road and parcel generator,
  // without loading retail models or replacing the RPG target bindings.
  SkirmishSettings layout;
  layout.seed = seed;
  layout.cells = radius>=750?8:7;
  layout.density = 4;
  layout.groundPerZone = 1;
  layout.airUnits = 0;
  layout.reinforcementsPerZone = 0;
  layout.terrain = biome == 0 ? "desert" : biome == 3 ? "urban"
                   : biome == 4 || biome == 7 || biome == 1 ? "hills"
                                                          : "grassland";
  layout.water = "none";
  layout.density = location == 1 ? 12 : location == 5 ? 10 : 8;
  auto recipe = generateSkirmishRecipe(layout);
  float factor = radius / recipe.at("bounds").at(2).get<float>();
  auto scale = [&](Json &rect) { for (auto &v : rect) v = v.get<float>() * factor; };
  scale(recipe["bounds"]);
  for (auto &node : recipe["nodes"]) scale(node);
  for (auto &plot : recipe["plots"]) {scale(plot["bounds"]);plot["scenery_count"]=plot["buildings"];}
  constexpr float biomeRelief[]={5,14,3,1,16,5,3,10};
  constexpr float locationRelief[]={2,5,6,8,0,3,8,0,2,18};
  recipe["terrain"]["amplitude"] = interior ? 0 : std::max(biomeRelief[biome],locationRelief[location]);
  if(location==4)recipe["terrain"]["amplitude"]=0;
  const auto plan=generationPlan(seed,biome,location,region);
  Json water=Json::array();
  auto basin=[&](std::array<float,4> bounds,float depth,float bank) {
    water.push_back({{"bounds",bounds},{"level",-1},{"depth",depth},{"bank_width",bank}});
  };
  if(!interior && biome!=1 && biome!=7 && biome!=4) {
    if(location==4) {
      basin({radius*.32f,-radius*.88f,radius*.74f,radius*.82f},6,48);
      basin({radius*.74f,-radius*.88f,radius*.98f,radius*.82f},12,48);
      // Wide dry wharf fingers connect back to the road network.
      size_t originalNodes=recipe["nodes"].size();
      for(float z:{-radius*.45f,0.f,radius*.45f}) {
        size_t nearest=0;float best=1e20f;
        for(size_t i=0;i<originalNodes;++i){auto p=recipe["nodes"][i].get<std::array<float,2>>();float distance=std::hypot(p[0],p[1]-z);if(distance<best){best=distance;nearest=i;}}
        size_t a=recipe["nodes"].size();recipe["nodes"].push_back({0,z});
        size_t b=recipe["nodes"].size();recipe["nodes"].push_back({radius*.70f,z});
        recipe["roads"].push_back({{"nodes",{nearest,a}},{"width",32},{"class","local"}});
        recipe["roads"].push_back({{"nodes",{a,b}},{"width",32},{"class","arterial"}});
      }
    } else if(location==8 || biome==2) {
      basin({-radius*.78f,-radius*.80f,radius*.78f,radius*.35f},6,64);
    } else if(location==0) {
      // A continuous shallow utility canal can be waded, with the street
      // crossings supplying intermittent dry routes across the district.
      const float x=((seed&1)&&!plan.underground?-1.f:1.f)*radius*.55f;
      basin({x-radius*.066f,-radius*.72f,x+radius*.066f,radius*.26f},5,24);
    } else if(location==1 || biome==5 || biome==6) {
      float side=(seed&1)?1.f:-1.f;
      float center=side*radius*.55f;
      basin({center-radius*.17f,-radius*.42f,center+radius*.17f,-radius*.08f},biome==6?4.f:6.f,40);
    }
  }
  if(plan.underground&&!plan.fullInterior){
    // A dry excavation drains incidental surface ponds instead of inheriting
    // their surface water level tens of metres above its basement floor.
    Json drained=Json::array();std::array<float,4> dry{-.70f*radius-160,-.42f*radius-160,-.26f*radius+160,.06f*radius+160};
    for(const auto &body:water){auto b=body.at("bounds").get<std::array<float,4>>();
      if(b[0]<dry[2]&&dry[0]<b[2]&&b[1]<dry[3]&&dry[1]<b[3])continue;
      drained.push_back(body);}
    water=std::move(drained);
  }
  recipe["terrain"]["water"]=water;
  shapeMissionShores(recipe,plan);
  // Wet parcels retain scenery but never receive a raised construction pad.
  for(auto &plot:recipe["plots"]) {
    auto b=plot.at("bounds").get<std::array<float,4>>();
    for(const auto &body:water) {
      auto w=body.at("bounds").get<std::array<float,4>>();
      if(b[0]<w[2]&&w[0]<b[2]&&b[1]<w[3]&&w[1]<b[3])plot["buildings"]=0;
    }
    if(interior)plot["buildings"]=0;
  }
  Json depressions=Json::array();
  int craterCount=interior?0:location==2?16:location==9?14:location==6||biome==7?10:biome==4?8:location==0?5:0;
  for(int i=0;i<craterCount;++i) {
    float x=float(int(random.next()%1501)-750)*radius/1000;
    float z=float(int(random.next()%1501)-750)*radius/1000;
    if(std::hypot(x,z-radius*.65f)<85 || (objective==2&&std::abs(x)<45))continue;
    depressions.push_back({{"center",{x,z}},{"radius",location==9?75:location==2?38:52},
                           {"depth",location==9?10:location==2?4:5}});
  }
  recipe["terrain"]["depressions"]=depressions;
  optimiseMissionLayout(recipe,plan,radius);
  recipe["terrain"]["art_layer"] = biome;
  recipe["terrain"]["facility_floor"] = plan.fullInterior;
  buildProceduralTerrain(world, recipe);
  buildMissionElevation(world,location,region,radius,biome,landform);
  if(plan.submerged){
    // Pressure-sealed mechs can traverse the flooded facility and its approach,
    // while ordinary offshore water retains the normal six-metre wading limit.
    std::array<float,4> b{(-.48f-.22f)*radius-60,(-.18f-.24f)*radius-60,
                         (-.48f+.22f)*radius+60,(-.18f+.24f)*radius+60};
    world.waterRegions.push_back({b,0,24,100});
    world.submergedRoutes.push_back({b[0]-64,b[1]-64,b[2]+64,b[3]+64});
    std::vector<std::array<float,4>> crossings;
    for(auto r:world.waterCrossings){
      if(r[2]<=b[0]||r[0]>=b[2]||r[3]<=b[1]||r[1]>=b[3]){crossings.push_back(r);continue;}
      auto keep=[&](std::array<float,4> q){if(q[2]>q[0]&&q[3]>q[1])crossings.push_back(q);};
      keep({r[0],r[1],std::min(r[2],b[0]),r[3]});keep({std::max(r[0],b[2]),r[1],r[2],r[3]});
      float l=std::max(r[0],b[0]),h=std::min(r[2],b[2]);
      keep({l,r[1],h,std::min(r[3],b[1])});keep({l,std::max(r[1],b[3]),h,r[3]});
    }
    world.waterCrossings=std::move(crossings);
    Vec3 a{b[0],0,b[1]},c{b[2],0,b[3]},d{b[0],0,b[3]},f{b[2],0,b[1]};
    for(auto p:{a,c,f,a,d,c})world.editGeometry().waterMesh.push_back({p,{.035f,.24f,.42f}});
  }
  buildMissionCanalEntry(world,biome,location,region,opener);
  // Keep biome material identity while retaining the physical terrain and roads.
  Vec3 tint = environmentCatalog()[biome].color;
  if (biome != 0 && biome != 3 && biome != 5)
    for (auto &v : world.editGeometry().terrain) v.color = tint * (.85f + .1f * std::sin(v.position.x * .04f));
  world.groundColor = tint;
  world.skyColor = location == 7 ? Vec3{.012f,.016f,.021f}
                   : biome == 2 ? Vec3{.025f,.10f,.16f}
                   : biome == 1 || biome == 7 ? Vec3{.002f,.003f,.009f}
                                             : Vec3{.055f,.095f,.13f};
  auto grounded = [&](Vec3 p) { p.y=world.surfaceHeight(p.x,p.z); return p; };
  world.targets.resize(enemyIds.size());
  for(auto &target:world.targets){target.active=false;target.health=0;}
  for(const auto &point:recipe.at("nodes"))world.interiorNavNodes.push_back(grounded({point[0].get<float>(),0,point[1].get<float>()}));
  for(const auto &road:recipe.at("roads"))world.interiorNavLinks.push_back(road.at("nodes").get<std::array<unsigned,2>>());
  buildMissionInterior(world,location,region,radius,biome);
  buildMissionCoverRoutes(world,location,region,radius,biome);
  // Work, supply and power alcoves share a clear room centre and face their
  // corridor. Exterior lots use small location-specific narrative clusters.
  auto dungeonSites=buildDungeonSites(world,recipe,biome,location,region,dungeonCount);
  if(sites)*sites=std::move(dungeonSites);
  buildMissionCrossing(world,biome,location,region,radius);
  dressMissionInterior(world,location,region);
  dressMissionExterior(world,recipe,biome,location);
  ensureMissionDensity(world,biome,location,region,radius);
  // RPG enemies remain at indices [0,enemyIds.size()) regardless of structure count.
  for(size_t i=0;i<enemyIds.size();++i) {
    auto &spec=enemySpec(enemyIds[i]);Target target;
    auto appearance=manualAppearance(seed,unsigned(i),spec.kind==EnemyKind::SmallKaiju||spec.kind==EnemyKind::LargeKaiju);
    applyOriginalModel(target,storyEnemyModel(story,enemyIds[i],originalVariant(enemyModelId(enemyIds[i]),seed^uint32_t(i*0x9e3779b9u))));
    auto painted=std::make_shared<WorldMesh>(*target.mesh);
    for(auto &v:*painted)v.color={v.color.x*.35f+appearance.tint.x*.65f,v.color.y*.35f+appearance.tint.y*.65f,v.color.z*.35f+appearance.tint.z*.65f};
    target.mesh=painted;
    Vec3 desired=i==0?Vec3{0,0,radius*.65f-80}:
        Vec3{std::sin(float(i)*2.39996f)*radius*.65f,0,std::cos(float(i)*2.39996f)*radius*.5f};
    if((spec.traits&Submersible)&&biome==2)desired={-.48f*radius,0,-.18f*radius};
    float wading=(spec.traits&(Flying|Hover|Submersible))||(plan.submerged&&spec.kind!=EnemyKind::Conventional)?-1.f:std::min(world.maxWadeDepth,target.ai.localHalf.y);
    target.origin=missionNavigablePosition(world,desired,std::max(8.f,std::min(18.f,std::hypot(target.half.x,target.half.z))),-1,wading);
    target.health=float(spec.hd*8);target.ai.maxHealth=target.health;target.spawnType=3;
    target.ai.airborne=(spec.traits&(Flying|Hover))!=0;target.active=true;
    world.updateEnemyBounds(target);world.targets[i]=std::move(target);
  }
  if(location==4&&!interior)for(float z:{-radius*.45f,0.f,radius*.45f}) {
    Target pier;applyOriginalModel(pier,"dock_pier");
    pier.origin={radius*.55f,-pier.ai.localCenter.y-pier.ai.localHalf.y,z};
    pier.spawnType=-1;pier.health=100000;pier.invulnerable=true;
    world.updateEnemyBounds(pier);
    if(missionRouteDistance(world,pier.center)<std::hypot(pier.half.x,pier.half.z)+20)continue;
    world.targets.push_back(std::move(pier));
  }
  if(story&&story->storyArc>=0)dressCampaignStory(world,*story,radius);
  // Stable tail slots belong to four pilots' deployed barriers and turrets.
  for(int i=0;i<8;++i){
    Target target;
    applyOriginalModel(target,i%2?"turret":"shield");
    target.active=false;
    target.health=0;
    world.targets.push_back(std::move(target));
  }
}
} // namespace iron::rpg
