#include "coop_snapshot.hpp"
#include <utility>
#include <limits>
#include <stdexcept>
namespace iron::coop {using Json=nlohmann::json;namespace {
template<class T> T integer(const Json& j){
    if(j.is_number_unsigned()){auto n=j.get<uint64_t>();if(std::in_range<T>(n))return T(n);}
    else if(j.is_number_integer()){auto n=j.get<int64_t>();if(std::in_range<T>(n))return T(n);}
    throw std::runtime_error("invalid compact integer");
}
Json vec(Vec3 v){return Json::array({v.x,v.y,v.z});}
Vec3 vector(const Json& j){if(!j.is_array()||j.size()!=3)throw std::runtime_error("invalid compact vector");Vec3 v{j[0].get<float>(),j[1].get<float>(),j[2].get<float>()};if(!std::isfinite(length(v)))throw std::runtime_error("nonfinite compact vector");return v;}
Json target(const Target& t){return Json::array({vec(t.center),vec(t.half),vec(t.origin),t.yaw,t.health,t.active,t.escaped,t.ai.bank,t.ai.pitch,t.ai.weaponYaw,t.ai.weaponPitch,t.ai.muzzleFlash,t.ai.gaitPhase,vec(t.ai.velocity)});}
float number(const Json&);
void readTarget(const Json& j,Target& t){if(!j.is_array()||j.size()!=14)throw std::runtime_error("invalid compact target");t.center=vector(j[0]);t.half=vector(j[1]);t.origin=vector(j[2]);t.yaw=number(j[3]);t.health=number(j[4]);t.active=j[5];t.escaped=j[6];t.ai.bank=number(j[7]);t.ai.pitch=number(j[8]);t.ai.weaponYaw=number(j[9]);t.ai.weaponPitch=number(j[10]);t.ai.muzzleFlash=number(j[11]);t.ai.gaitPhase=number(j[12]);t.ai.velocity=vector(j[13]);}
uint64_t baselineHash(const Json& targets){uint64_t hash=14695981039346656037ull;for(uint8_t b:Json::to_cbor(targets)){hash^=b;hash*=1099511628211ull;}return hash;}
float number(const Json& j){float value=j.get<float>();if(!std::isfinite(value))throw std::runtime_error("nonfinite snapshot scalar");return value;}
void copyTarget(const Target& from,Target& to){to.center=from.center;to.half=from.half;to.origin=from.origin;to.yaw=from.yaw;to.health=from.health;to.active=from.active;to.escaped=from.escaped;to.ai.bank=from.ai.bank;to.ai.pitch=from.ai.pitch;to.ai.weaponYaw=from.ai.weaponYaw;to.ai.weaponPitch=from.ai.weaponPitch;to.ai.muzzleFlash=from.ai.muzzleFlash;to.ai.gaitPhase=from.ai.gaitPhase;to.ai.velocity=from.ai.velocity;}
void arity(const Json& j,size_t n){if(!j.is_array()||j.size()!=n)throw std::runtime_error("invalid compact record length");}
}
Json SnapshotBaseline::wire()const{return {{"version",1},{"hash",hash},{"targets",targets}};}
SnapshotBaseline captureBaseline(const World& w){SnapshotBaseline base;for(const auto& t:w.targets)base.targets.push_back(target(t));base.hash=baselineHash(base.targets);return base;}
SnapshotBaseline readBaseline(const Json& j){
    if(!j.is_object()||integer<unsigned>(j.at("version"))!=1||!j.at("targets").is_array()||j.at("targets").size()>8192)throw std::runtime_error("Invalid snapshot baseline");
    SnapshotBaseline base;base.targets=j.at("targets");base.hash=integer<uint64_t>(j.at("hash"));
    if(base.hash!=baselineHash(base.targets))throw std::runtime_error("Snapshot baseline hash mismatch");
    for(const auto& row:base.targets){Target value;readTarget(row,value);}return base;
}
Json compactWorld(const World& w,const SnapshotBaseline* base){
    bool sparse=base&&base->targets.size()==w.targets.size();Json targets=Json::array(),shots=Json::array(),effects=Json::array(),pickups=Json::array(),sounds=Json::array();
    for(size_t i=0;i<w.targets.size();++i){auto row=target(w.targets[i]);if(!sparse){targets.push_back(std::move(row));continue;}auto& original=base->targets[i];if(row==original)continue;unsigned mask=0;for(size_t k=0;k<14;++k)if(row[k]!=original[k])mask|=1u<<k;Json update=Json::array({i,mask});for(size_t k=0;k<14;++k)if(mask&(1u<<k))update.push_back(std::move(row[k]));targets.push_back(std::move(update));}
    for(auto& p:w.projectiles){
        auto row=Json::array({vec(p.position),vec(p.velocity),p.life,p.damage,p.mount,int(p.kind),p.owner,p.victim,p.shooter,p.radius,int(p.weapon),p.homingTarget,p.hull,p.intercepted});
        if(p.visual!=ProjectileVisual::Standard)row.push_back(int(p.visual));
        shots.push_back(std::move(row));
    }
    for(auto& p:w.effects)effects.push_back(Json::array({int(p.kind),vec(p.position),vec(p.direction),int(p.weapon),p.age,p.size,p.model,p.damageRadius,p.damageRate}));
    for(auto& p:w.pickups)pickups.push_back(Json::array({vec(p.position),p.kind,p.collected,p.collectedBy}));
    for(auto& p:w.sounds)sounds.push_back(Json::array({int(p.kind),p.weapon,vec(p.position)}));
    return Json::array({sparse?2:1,w.targets.size(),sparse?base->hash:0,targets,shots,effects,pickups,sounds,w.elapsed,w.missionFlags,w.counters,w.destroyed});
}
void compactRead(const Json& j,World& w,const SnapshotBaseline* base){
    arity(j,12);int version=integer<int>(j[0]);if(version!=1&&version!=2)throw std::runtime_error("unknown compact version");if(integer<size_t>(j[1])!=w.targets.size())throw std::runtime_error("compact mission mismatch");
    if(!j[3].is_array()||j[3].size()>w.targets.size()||!j[4].is_array()||j[4].size()>16384||!j[5].is_array()||j[5].size()>16384||!j[6].is_array()||j[6].size()>4096||!j[7].is_array()||j[7].size()>16384)throw std::runtime_error("compact count limit");
    Json rows;
    if(version==2){if(!base||integer<uint64_t>(j[2])!=base->hash)throw std::runtime_error("compact baseline mismatch");rows=base->targets;std::vector<bool> seen(w.targets.size());for(auto& update:j[3]){if(!update.is_array()||update.size()<2)throw std::runtime_error("short compact update");size_t index=integer<size_t>(update[0]);unsigned mask=integer<unsigned>(update[1]);if(index>=w.targets.size()||seen[index]||mask&~0x3fffu||mask==0)throw std::runtime_error("invalid compact index/mask");seen[index]=true;size_t at=2;for(size_t k=0;k<14;++k)if(mask&(1u<<k))rows[index][k]=update.at(at++);if(at!=update.size())throw std::runtime_error("compact update length");}}
    else{if(j[3].size()!=w.targets.size())throw std::runtime_error("compact target count");rows=j[3];}
    // Decode into scratch state. Bad packets cannot partly overwrite the world.
    std::vector<Target> decoded(w.targets.size());for(size_t i=0;i<decoded.size();++i)readTarget(rows.at(i),decoded[i]);
    std::vector<Projectile> shots;std::vector<WeaponEffect> effects;std::vector<Pickup> pickups;std::vector<SoundEvent> sounds;
    for(auto& row:j[4]){
        if(!row.is_array()||(row.size()!=14&&row.size()!=15))throw std::runtime_error("Invalid projectile record length");
        Projectile p;
        if(row.size()==15){
            const auto tag=integer<unsigned>(row[14]);
            if(tag>unsigned(ProjectileVisual::LoftMissile))throw std::runtime_error("Invalid projectile visual");
            p.visual=ProjectileVisual(tag);
        }
        p.position=vector(row[0]);p.velocity=vector(row[1]);p.life=number(row[2]);p.damage=number(row[3]);p.mount=integer<int>(row[4]);int kind=integer<int>(row[5]),weapon=integer<int>(row[10]);if(kind<0||kind>2||weapon<0||weapon>=int(Weapon::Count))throw std::runtime_error("compact shot enum");p.kind=ProjectileKind(kind);p.owner=integer<int>(row[6]);p.victim=integer<int>(row[7]);p.shooter=integer<int>(row[8]);p.radius=number(row[9]);p.weapon=Weapon(weapon);p.homingTarget=integer<int>(row[11]);p.hull=number(row[12]);p.intercepted=row[13];shots.push_back(p);}
    for(auto& row:j[5]){arity(row,9);WeaponEffect p;int kind=integer<int>(row[0]),weapon=integer<int>(row[3]);if(kind<0||kind>5||weapon<0||weapon>=int(Weapon::Count))throw std::runtime_error("compact effect enum");p.kind=WeaponEffect::Kind(kind);p.position=vector(row[1]);p.direction=vector(row[2]);p.weapon=Weapon(weapon);p.age=number(row[4]);p.size=number(row[5]);p.model=integer<unsigned>(row[6]);p.damageRadius=number(row[7]);p.damageRate=number(row[8]);effects.push_back(p);}
    for(auto& row:j[6]){arity(row,4);Pickup p;p.position=vector(row[0]);p.kind=integer<int>(row[1]);p.collected=row[2];p.collectedBy=integer<unsigned>(row[3]);pickups.push_back(p);}
    for(auto& row:j[7]){arity(row,3);int kind=integer<int>(row[0]);if(kind<0||kind>9)throw std::runtime_error("compact sound enum");sounds.push_back({SoundEvent::Kind(kind),integer<int>(row[1]),vector(row[2])});}
    float elapsed=number(j[8]);uint32_t flags=integer<uint32_t>(j[9]);arity(j[10],3);decltype(w.counters) counters;for(size_t i=0;i<counters.size();++i)counters[i]=integer<int>(j[10][i]);unsigned destroyed=integer<unsigned>(j[11]);
    for(size_t i=0;i<decoded.size();++i)copyTarget(decoded[i],w.targets[i]);w.projectiles=std::move(shots);w.effects=std::move(effects);w.pickups=std::move(pickups);w.sounds.insert(w.sounds.end(),sounds.begin(),sounds.end());w.elapsed=elapsed;w.missionFlags=flags;w.counters=counters;w.destroyed=destroyed;for(auto& p:w.pickups)p.collected=(p.collectedBy&(1u<<w.playerSlot))!=0;
}
}
