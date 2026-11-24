#include "rpg/world_generation.hpp"
#include "rpg/generation.hpp"
#include "rpg/session.hpp"
#include "rpg/original_assets.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <string_view>
namespace iron::rpg {
namespace {
// A cluster is a purpose, not a fixed stamp. Its parts occupy independently
// validated sites and its orientation follows a nearby route where appropriate.
using Cluster=std::vector<const char *>;
using Palette=std::vector<Cluster>;
const Palette &exteriorPalette(int location) {
  static const std::array<Palette,10> palettes={{
    {{"is_warehouse","cargo_container","service_crate","city_sign"},
     {"is_office","city_barrier","is_relay","service_console"},
     {"industrial","is_water_tower","oxygen_tank","service_crate"},
     {"mecha_wreck","ship_debris","scrap_heap","city_barrier"},
     {"habitat","solar_array","city_sign","cargo_container"},
     {"building","gun_wreck","battle_barricade","crater"}},
    {{"broadleaf","tree","fallen_log","rock"},
     {"rock","spire","crystal","fallen_log"},
     {"tree","broadleaf","tree","fallen_log"},
     {"mining_rig","cargo_container","service_crate","is_relay"},
     {"mecha_wreck","scrap_heap","fallen_log","rock"},
     {"habitat","oxygen_tank","solar_array","service_console"}},
    {{"gun_wreck","battle_barricade","crater","scrap_heap"},
     {"mecha_wreck","ship_debris","city_barrier","crater"},
     {"cargo_container","service_crate","oxygen_tank","battle_barricade"},
     {"ship_wreck","scrap_heap","gun_wreck","ship_debris"},
     {"is_relay","solar_array","cargo_container","service_console"},
     {"industrial","is_water_tower","crater","city_barrier"}},
    {{"rock","cactus","spire","rock"},
     {"spire","crystal","rock","vent"},
     {"mining_rig","service_crate","cargo_container","is_relay"},
     {"habitat","oxygen_tank","solar_array","rock"},
     {"mecha_wreck","scrap_heap","gun_wreck","cactus"},
     {"vent","crater","crystal","rock"}},
    {{"dock_crane","cargo_container","service_crate","oxygen_tank"},
     {"is_warehouse","cargo_container","city_sign","service_crate"},
     {"is_water_tower","industrial","oxygen_tank","is_relay"},
     {"ship_wreck","ship_debris","scrap_heap","cargo_container"},
     {"habitat","solar_array","service_console","city_barrier"},
     {"dock_crane","mining_rig","service_crate","cargo_container"}},
    {{"mecha_wreck","scrap_heap","ship_debris","gun_wreck"},
     {"ship_wreck","ship_hull","ship_debris","scrap_heap"},
     {"mining_rig","hangar_crane","cargo_container","service_crate"},
     {"industrial","oxygen_tank","is_relay","solar_array"},
     {"gun_wreck","battle_barricade","crater","mecha_wreck"},
     {"crystal","rock","vent","scrap_heap"}},
    {{"habitat","solar_array","oxygen_tank","is_relay"},
     {"mining_rig","cargo_container","service_crate","crystal"},
     {"crater","rock","asteroid","spire"},
     {"is_relay","solar_array","service_console","oxygen_tank"},
     {"ship_wreck","ship_debris","scrap_heap","cargo_container"},
     {"industrial","is_water_tower","habitat","service_crate"}},
    {{"ship_wreck","ship_hull","ship_debris","oxygen_tank"},
     {"battlecruiser","ship_debris","cargo_container","scrap_heap"},
     {"ship_bridge","is_relay","solar_array","ship_hull"},
     {"ship_reactor","oxygen_tank","service_console","ship_debris"},
     {"cargo_container","service_crate","hangar_crane","ship_hull"},
     {"mecha_wreck","gun_wreck","scrap_heap","ship_debris"}},
    {{"reef_fan","coral","kelp","rock"},
     {"coral","reef_fan","kelp","crystal"},
     {"ship_wreck","ship_debris","coral","kelp"},
     {"vent","rock","crystal","reef_fan"},
     {"habitat","oxygen_tank","service_console","cargo_container"},
     {"mining_rig","ship_debris","service_crate","coral"}},
    {{"asteroid","crystal","rock","spire"},
     {"mining_rig","cargo_container","service_crate","is_relay"},
     {"habitat","oxygen_tank","solar_array","rock"},
     {"ship_wreck","ship_debris","scrap_heap","asteroid"},
     {"crater","rock","crystal","asteroid"},
     {"is_relay","solar_array","service_console","oxygen_tank"}}
  }};
  return palettes.at(size_t(location));
}
const Palette &interiorPalette(bool ship,bool underground,bool wet) {
  static const Palette shipRooms={
    {"ship_reactor","service_console","oxygen_tank","service_crate"},
    {"ship_bridge","is_relay","service_console","oxygen_tank"},
    {"cargo_container","service_crate","hangar_crane","oxygen_tank"},
    {"mecha_wreck","hangar_crane","service_console","scrap_heap"},
    {"ship_hull","ship_debris","scrap_heap","service_crate"},
    {"oxygen_tank","service_console","service_crate","ship_debris"}};
  static const Palette mines={
    {"mining_rig","service_console","service_crate","oxygen_tank"},
    {"crystal","rock","spire","service_crate"},
    {"cargo_container","hangar_crane","service_crate","mining_rig"},
    {"mecha_wreck","scrap_heap","ship_debris","rock"},
    {"is_relay","oxygen_tank","service_console","solar_array"},
    {"vent","rock","crystal","oxygen_tank"}};
  static const Palette hangars={
    {"mecha_wreck","hangar_crane","service_console","service_crate"},
    {"cargo_container","service_crate","oxygen_tank","hangar_crane"},
    {"is_relay","service_console","oxygen_tank","solar_array"},
    {"industrial","is_water_tower","cargo_container","service_crate"},
    {"gun_wreck","scrap_heap","battle_barricade","service_console"},
    {"habitat","service_console","service_crate","oxygen_tank"}};
  static const Palette flooded={
    {"ship_wreck","ship_debris","coral","oxygen_tank"},
    {"reef_fan","coral","kelp","rock"},
    {"mining_rig","service_console","oxygen_tank","service_crate"},
    {"habitat","cargo_container","service_console","oxygen_tank"},
    {"crystal","rock","vent","coral"}};
  return wet?flooded:ship?shipRooms:underground?mines:hangars;
}
float facingRoute(const World &world,Vec3 p) {
  Vec3 facing=p+Vec3{0,0,-1};float nearest=1e20f;
  for(auto edge:world.interiorNavLinks){
    Vec3 a=world.interiorNavNodes[edge[0]],b=world.interiorNavNodes[edge[1]],delta=b-a;delta.y=0;
    Vec3 offset=p-a;offset.y=0;float t=std::clamp(dot(offset,delta)/std::max(.001f,dot(delta,delta)),0.f,1.f);
    Vec3 point=a+delta*t;float distance=std::hypot(point.x-p.x,point.z-p.z);
    if(distance<nearest){nearest=distance;facing=point;}
  }
  return std::atan2(p.x-facing.x,p.z-facing.z);
}
bool natural(std::string_view model) {
  return model=="tree"||model=="broadleaf"||model=="fallen_log"||model=="rock"||
    model=="spire"||model=="coral"||model=="reef_fan"||model=="kelp"||
    model=="cactus"||model=="crystal"||model=="ice"||model=="vent"||model=="asteroid"||model=="crater"||model.starts_with("cover_rock_");
}
const char *coverVariant(const char *model,Dice &random) {
  const std::string_view id=model;
  if(id=="rock"||id=="spire"||id=="asteroid")
    return random.next()%3?((random.next()&1)?"cover_rock_fan":"cover_rock_needle"):model;
  if(id=="cargo_container")return (random.next()&1)?"cover_cargo_armored":"cover_cargo_bulk";
  if(id=="city_barrier"||id=="battle_barricade")return random.next()%3?"cover_retaining_wall":model;
  if(id=="ship_wreck"||id=="ship_hull")return random.next()%3?((random.next()&1)?"cover_wreck_keel":"cover_wreck_bow"):model;
  if(id=="ship_reactor"||id=="oxygen_tank")return random.next()%3==0?"cover_pump":model;
  if(id=="hangar_crane"||id=="mining_rig")return random.next()%3==0?"cover_site_frame":model;
  if(id=="industrial"||id=="is_warehouse")return random.next()%3==0?"cover_foundry":model;
  if(id=="building"||id=="is_office")return random.next()%3==0?"cover_facade_block":model;
  return model;
}
struct Site {Vec3 anchor;std::array<float,4> bounds;const Cluster *cluster=nullptr;float yaw=0;int count=0;uint32_t salt=0;bool wet=false;};
bool place(World &world,const Site &site,const char *model,int part,std::map<std::string,int> &used,float health) {
  if(std::string_view(model)=="battlecruiser"&&used[model])return false;
  Dice random{site.salt^uint32_t(part+1)*0x9e3779b9u};
  model=coverVariant(model,random);
  float radius=std::min(site.bounds[2]-site.bounds[0],site.bounds[3]-site.bounds[1])*.32f;
  radius=std::clamp(radius,10.f,52.f);
  float angle=float(random.next()%6284)/1000.f;
  float turn=natural(model)?float(random.next()%6284)/1000.f:site.yaw+float(random.next()%4)*pi/2;
  Target dimensions;applyOriginalModel(dimensions,model);
  float span=2*std::max({dimensions.half.x,dimensions.half.y,dimensions.half.z,.5f});
  std::string_view id=model;
  bool small=id=="service_crate"||id=="ship_debris"||id=="scrap_heap"||id=="city_sign"||
             id=="fallen_log"||id=="oxygen_tank"||id=="cactus"||id=="coral"||id=="kelp";
  bool landmark=id=="is_office"||id=="is_warehouse"||id=="building"||id=="industrial"||
                id=="dock_crane"||id=="is_water_tower"||id=="rock"||id=="spire"||
                id=="asteroid"||id=="broadleaf"||id=="tree"||id=="habitat"||id=="ship_hull"||
                id.starts_with("cover_rock_")||id.starts_with("cover_wreck_")||id=="cover_facade_block"||id=="cover_foundry";
  bool large=part==0&&landmark&&!world.indoors(site.anchor)&&
             site.bounds[2]-site.bounds[0]>110&&site.bounds[3]-site.bounds[1]>110&&random.next()%3!=0;
  float desired=large?48+float(random.next()%65):small?2.5f+float(random.next()%46)/10:10+float(random.next()%121)/10;
  float scale=std::clamp(desired/span,.35f,5.f);
  if(id=="battlecruiser")scale=.9f+float(random.next()%41)/100;
  // Check several positions rather than repeatedly dropping every kit at the
  // same fixed offsets. The utility validates route, water, roof and colliders.
  for(int attempt=0;attempt<12;++attempt){
    float r=part==0&&attempt==0?0:radius*(.4f+.6f*float(random.next()%1001)/1000);
    Vec3 p=site.anchor+Vec3{std::sin(angle+attempt*2.39996f)*r,0,std::cos(angle+attempt*2.39996f)*r};
    p.x=std::clamp(p.x,site.bounds[0]+5,site.bounds[2]-5);
    p.z=std::clamp(p.z,site.bounds[1]+5,site.bounds[3]-5);
    // Oversized landmarks get the first placement attempts. A small parcel
    // can still host a peer-sized workshop when its large silhouette cannot fit.
    float candidateScale=attempt<8?scale:large?std::min(scale,18.f/span):scale;
    if(addMissionProp(world,model,p,turn,health,site.wet,candidateScale,true)){++used[model];return true;}
  }
  return false;
}
void populate(World &world,std::vector<Site> &sites,int maximum,float health) {
  std::map<std::string,int> used;
  for(int part=0;part<maximum;++part)for(const auto &site:sites){
    if(world.targets.size()>=missionSceneryTargetLimit)return;
    if(part>=site.count)continue;
    const auto &cluster=*site.cluster;
    const char *model=cluster[size_t(part)%cluster.size()];
    // A map-wide repeated model cap preserves room for other site purposes.
    if(used[model]>14&&part>0)model=cluster[(size_t(part)+1)%cluster.size()];
    place(world,site,model,part,used,health);
  }
}
}
void dressMissionInterior(World &world,int location,int region) {
  if(world.interiorRegions.empty())return;
  Dice random{world.prototypeSeed^0x45c1a8d3u};
  auto plan=generationPlan(world.prototypeSeed,0,location,region);
  std::vector<Site> sites;
  for(size_t i=0;i<world.interiorRegions.size();++i){
    const auto &room=world.interiorRegions[i];
    float width=room.bounds[2]-room.bounds[0],depth=room.bounds[3]-room.bounds[1];
    if(width<70||depth<70)continue; // Doorway strips are circulation, not rooms.
    Vec3 centre{(room.bounds[0]+room.bounds[2])/2,0,(room.bounds[1]+room.bounds[3])/2};
    centre.y=world.surfaceHeight(centre.x,centre.z);
    bool wet=world.waterDepthAt(centre)>0;
    const auto &palette=interiorPalette(location==7,region==3||centre.y<-8,wet);
    size_t role=(size_t(plan.palette)+i+random.next()%3)%palette.size();
    // Furnish different corners of different rooms, keeping their connecting
    // passages open; adjacent rooms can be workshops, storage, power or wrecks.
    float sideX=(random.next()&1)?1.f:-1.f,sideZ=(random.next()&1)?1.f:-1.f;
    Vec3 anchor=centre+Vec3{width*.28f*sideX,0,depth*.28f*sideZ};
    sites.push_back({anchor,room.bounds,&palette[role],facingRoute(world,anchor),2+int(random.next()%3),random.next(),wet});
  }
  populate(world,sites,4,80);
}
static void dressCityFrontages(World &world,const Json &recipe,Dice &random) {
  std::vector<const char *> skyline(skylineCatalog().begin(),skylineCatalog().end());
  for(size_t n=skyline.size();n>1;--n)std::swap(skyline[n-1],skyline[random.next()%n]);
  size_t nextTower=0;
  for(const auto &plot:recipe.at("plots")){
    const auto b=plot.at("bounds").get<std::array<float,4>>();
    if(b[2]-b[0]<58||b[3]-b[1]<58)continue;
    const Vec3 middle{(b[0]+b[2])*.5f,0,(b[1]+b[3])*.5f};
    if(world.indoors(middle+Vec3{0,12,0})||world.waterDepthAt(middle,25)>0)continue;
    const unsigned style=unsigned(plot.value("purpose",0));
    const char *tower=skyline[nextTower%skyline.size()];
    const char *yard=style%3==0?"cover_foundry":style%3==1?"is_warehouse":"cover_facade_block";
    bool acrossX=b[2]-b[0]>=b[3]-b[1];
    const float offset=std::min(26.f,(acrossX?b[2]-b[0]:b[3]-b[1])*.22f);
    const auto separation=acrossX?Vec3{offset,0,0}:Vec3{0,0,offset};
    const float facing=acrossX?0.f:pi/2;
    if(addMissionProp(world,tower,middle-separation,facing,115,false,.86f,true)||
       addMissionProp(world,tower,middle,facing,115,false,.86f,true))++nextTower;
    addMissionProp(world,yard,middle+separation,facing,100,false,.9f,true);
  }
}
void dressMissionExterior(World &world,const Json &recipe,int biome,int location) {
  Dice random{world.prototypeSeed^0xb4d36219u};
  auto plan=generationPlan(world.prototypeSeed,biome,location,0);
  const auto &palette=exteriorPalette(location);
  if(recipe.value("city_frontages",false))dressCityFrontages(world,recipe,random);
  std::vector<Site> sites;
  for(const auto &plot:recipe.at("plots")){
    auto bounds=plot.at("bounds").get<std::array<float,4>>();
    if(bounds[2]-bounds[0]<16||bounds[3]-bounds[1]<16)continue;
    Vec3 anchor{(bounds[0]+bounds[2])/2,0,(bounds[1]+bounds[3])/2};
    anchor.y=world.surfaceHeight(anchor.x,anchor.z);
    if(world.indoors(anchor+Vec3{0,8,0}))continue;
    // Nearby lots share a favored purpose, with occasional secondary sites.
    size_t role=size_t(plot.value("purpose",plan.palette))%palette.size();
    if(random.next()%3==0)role=random.next()%palette.size();
    bool wet=world.waterDepthAt(anchor)>0||location==8||biome==2;
    const auto *cluster=&palette[role];
    if(biome==2)cluster=&interiorPalette(false,false,true)[role%interiorPalette(false,false,true).size()];
    int count=3+int(random.next()%4);
    // Dense natural groves contrast with solitary geology or abandoned sites.
    if(location==1&&(role==0||role==2))count+=2;
    sites.push_back({anchor,bounds,cluster,facingRoute(world,anchor),count,random.next(),wet});
  }
  // Distribute all landmarks before filling their details. Hitting the scene
  // budget cannot turn the first few parcels into the entire populated map.
  for(size_t i=sites.size();i>1;--i)std::swap(sites[i-1],sites[random.next()%i]);
  populate(world,sites,7,location==0?35.f:100.f);
  // A biome accent is sparse and contextual rather than stamped on every lot.
  const char *accent=biome==6?"ice":biome==4?"vent":biome==7?"crater":biome==1?"crystal":nullptr;
  if(accent){std::map<std::string,int> used;for(size_t i=0;i<sites.size();i+=5)place(world,sites[i],accent,8,used,100);}
}
std::span<const char *const> skylineCatalog(){
  static constexpr const char *models[]={"city_tower_is_original","city_tower_is_stepped","city_tower_is_octagonal",
    "city_tower_is_buttressed","city_tower_is_office","city_tower_is_slender","city_tower_setback","city_tower_wedge",
    "city_tower_twinned","city_tower_terraced","city_tower_citadel","city_tower_industrial","city_tower_spire","city_tower_arcology"};
  return models;
}
int minimumMissionSkyscrapers(int biome,int location,int region,float extent){
  if(region>=2||location==7||biome==2)return 0;
  int base=location==0?16:location==4||location==6?10:location==5?6:0;
  return base?base+(extent>=750?4:0):0;
}
MissionDensity missionDensity(const World &world){
  MissionDensity result;
  for(const auto &t:world.targets)if(t.active&&t.health>0&&t.rpgDecoration){
    ++result.decorations;
    if(2*std::max(t.half.x,t.half.z)>=16||2*t.ai.localHalf.y>=24)++result.largeStructures;
    if(t.generatorAsset.starts_with("city_tower_"))++result.skyscrapers;
  }
  return result;
}
void ensureMissionDensity(World &world,int biome,int location,int region,float extent){
  const auto plan=generationPlan(world.prototypeSeed,biome,location,region);
  Dice dice{world.prototypeSeed^0x64656e73u};
  const int targetDecorations=minimumMissionDecorations+(extent>=750?224:64)+(plan.city?55:0)+int(dice.next()%65);
  const int targetLarge=minimumMissionLargeStructures+(extent>=750?44:20)+(plan.city?18:0)+int(dice.next()%13);
  const int targetSkyline=minimumMissionSkyscrapers(biome,location,region,extent);
  std::vector<const char *> towers(skylineCatalog().begin(),skylineCatalog().end());
  for(size_t n=towers.size();n>1;--n)std::swap(towers[n-1],towers[dice.next()%n]);
  std::stable_partition(towers.begin(),towers.end(),[&](const char *id){
    return std::none_of(world.targets.begin(),world.targets.end(),[&](const auto &t){return t.generatorAsset==id;});
  });
  size_t nextTower=0;
  const std::array<const char *,4> urban{"building","is_warehouse","industrial","habitat"},
    woodland{"broadleaf","rock","spire","tree"},
    desert{"rock","spire","mining_rig","habitat"},
    wet{"rock","reef_fan","ship_wreck","habitat"},
    enclosed{"ship_reactor","cargo_container","hangar_crane","industrial"},
    lunar{"asteroid","rock","mining_rig","habitat"};
  const auto &largePool=plan.fullInterior?enclosed:biome==2||location==8?wet:
    location==1?woodland:location==3?desert:location==6||location==9?lunar:urban;
  const auto &palette=exteriorPalette(location);
  auto density=missionDensity(world);float phaseX=float(dice.next()%10000)/10000,phaseZ=float(dice.next()%10000)/10000;
  // Low-discrepancy proposals cover gaps between authored clusters. Large cover
  // is placed first so small details cannot consume its required footprints.
  for(int attempt=0;attempt<32000&&world.targets.size()<missionSceneryTargetLimit&&
      (density.decorations<targetDecorations||density.largeStructures<targetLarge||density.skyscrapers<targetSkyline);++attempt){
    auto fract=[](double n){return float(n-std::floor(n));};
    Vec3 p{extent*(fract(phaseX+double(attempt)*.754877666)*1.86f-.93f),0,
           extent*(fract(phaseZ+double(attempt)*.569840296)*1.86f-.93f)};
    p.y=world.surfaceHeight(p.x,p.z);
    if(plan.fullInterior&&!world.indoors(p+Vec3{0,12,0}))continue;
    if(std::hypot(p.x-world.missionEntry.x,p.z-world.missionEntry.z)<100||missionRouteDistance(world,p)<(plan.city?23.f:35.f))continue;
    bool skyline=density.skyscrapers<targetSkyline;
    if(skyline&&world.indoors(p+Vec3{0,12,0}))continue;
    bool large=skyline||density.largeStructures<targetLarge;
    const char *model=nullptr;
    if(skyline)model=towers[nextTower%towers.size()];
    else if(large)model=largePool[dice.next()%largePool.size()];
    else if(world.indoors(p)){const auto &pool=interiorPalette(location==7,plan.underground,world.waterDepthAt(p)>0);
      const auto &cluster=pool[dice.next()%pool.size()];model=cluster[dice.next()%cluster.size()];}
    else {const auto &cluster=palette[dice.next()%palette.size()];model=cluster[dice.next()%cluster.size()];}
    if(!skyline)model=coverVariant(model,dice);
    Target shape;applyOriginalModel(shape,model);float span=2*std::max({shape.half.x,shape.half.y,shape.half.z,.5f});
    // A fallback uses tall, narrow equipment instead of reducing the large
    // structure floor on crowded interior maps.
    if(large&&attempt>16000&&plan.fullInterior){model="ship_reactor";applyOriginalModel(shape,model);span=2*std::max({shape.half.x,shape.half.y,shape.half.z,.5f});}
    float desired=large?28.f+float(dice.next()%9):3.f+float(dice.next()%61)/10;
    // Keep the authored skyline scale; the ordinary filler normalization would
    // compress a 100m tower into a 30m generic prop.
    float scale=skyline?.90f+float(dice.next()%21)/100:std::clamp(desired/span,.1f,8.f);float yaw=float(dice.next()%4)*pi/2;
    const float setback=plan.city&&!skyline?20.f:-1.f;
    if(addMissionProp(world,model,p,yaw,100,!skyline,scale,true,setback)){
      const auto &prop=world.targets.back();
      ++density.decorations;
      if(2*std::max(prop.half.x,prop.half.z)>=16||2*prop.ai.localHalf.y>=24)++density.largeStructures;
      if(prop.generatorAsset.starts_with("city_tower_")){++density.skyscrapers;++nextTower;}
    }
  }
  if(density.decorations<minimumMissionDecorations||density.largeStructures<minimumMissionLargeStructures||density.skyscrapers<targetSkyline)
    throw std::runtime_error("Mission density floor not met: seed="+std::to_string(world.prototypeSeed)+
      " location="+std::to_string(location)+" decorations="+std::to_string(density.decorations)+" large="+std::to_string(density.largeStructures)+" skyscrapers="+std::to_string(density.skyscrapers));
}
}
