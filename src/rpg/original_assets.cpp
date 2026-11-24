#include "rpg/original_assets.hpp"
#include "rpg/catalog.hpp"
#include "rpg/destruction.hpp"
#include "rpg/model_appearance.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <mutex>
#include <map>
#include <set>
#include <stdexcept>
namespace iron::rpg {
namespace {
#ifdef MECHA_GAME_ASSETS
std::filesystem::path assetRoot=MECHA_GAME_ASSETS;
#else
std::filesystem::path assetRoot="assets/mecha";
#endif
struct Asset {std::shared_ptr<WorldMesh> mesh,wreck;Vec3 center,half;};
std::map<std::string,Asset> cache;
std::map<std::string,bool> variants;
std::mutex cacheMutex;
std::map<std::string,size_t> registeredBudgets,approvedMechaBudgets;
bool budgetsLoaded=false;
nlohmann::json appearance;
const std::map<std::string,size_t> &mechaBaselineBudgets(){
    // Explicit source identities, not the broad EnemyKind::Labor category:
    // Salvager Gunship is aircraft and Abomination is a kaiju/mecha hybrid.
    static const auto limits=[] {
        std::map<std::string,size_t> out;
        auto pair=[&](const std::string &id,size_t cap){out.emplace(id,cap);out.emplace(id+"_alt",cap*2);};
        for(const char *name:{"brawler","scout","striker","titan"})pair(std::string("chassis_")+name,376);
        for(const char *id:{"jump_pod","labor_bot","mecha_recruit","mecha_sniper","frontier_patrol_mecha",
             "salvager_berserker","heavy_construction_bot","mecha_ace","aeon_mecha","jump_pod_heavy",
             "salvager_vandal","frontier_patrol_sergeant","jump_pod_field_commander","salvager_thunderer",
             "siege_mecha","rogue_commander","salvager_hydra","experimental_mecha_zero"})pair(id,376);
        for(const char *id:{"mechwalker","mechwalker_disabler"})pair(id,266);
        for(const char *id:{"swarm_drone","aeon_diode","neo_knight","terran_grunt","veriform_mecha",
             "verilian_guardian","funnel_drone_mecha","thunder_cannon_mecha","verilian_goliath","combiner_mecha",
             "aeon_primarch","neo_paladin","terran_mammoth","aegis_mecha","gladius_mecha","verilian_gargantuan"})out.emplace(id,752);
        out.emplace("combiner_mecha_combined",502);
        return out;
    }();
    return limits;
}
const std::map<std::string,std::pair<size_t,size_t>> &enemyRedesignBudgets(){
    static const auto limits=[] {
        std::map<std::string,std::pair<size_t,size_t>> out;
        auto pair=[&](const std::string &id,size_t previous,bool kaiju){out.emplace(id,std::pair{previous,previous*(kaiju?2:1)});out.emplace(id+"_alt",std::pair{previous*2,previous*(kaiju?4:2)});};
        for(const char *id:{"abomination","kaiju_baron","kaiju_drone","kaiju_obliterator","kaiju_spawnling","kaiju_stalker","kaiju_tyrant"})pair(id,376,true);
        for(const char *id:{"anti_mecha_destruction_squad","anti_mecha_strike_team"})pair(id,376,false);
        pair("anti_mecha_hovertank",124,false);pair("fast_attack_hovercraft",52,false);
        pair("ion_disruptor_installation",208,false);pair("ion_emplacement",150,false);pair("salvager_gunship",168,false);
        for(const char *id:{"abyssal_kaiju","kaiju_broodqueen","kaiju_dismantler","kaiju_flea","kaiju_tunnel_wurm"})out.emplace(id,std::pair{752,1504});
        for(const char *id:{"bomber","interceptor","cyclone_drone","deepdiver_submarine","heavy_hover_tank"})out.emplace(id,std::pair{248,248});
        for(const char *id:{"carrier","cruiser","dreadnought","gunboat"})out.emplace(id,std::pair{752,752});
        for(const char *id:{"sentry_gun","artillery_battery"})out.emplace(id,std::pair{300,300});
        return out;
    }();
    return limits;
}
void loadBudgetPolicy(){
    if(budgetsLoaded)return;
    std::map<std::string,size_t> registered,approved;
    for(const char *name:{"original-manifest.json","expansion-manifest.json","manual-enemy-assets.json","manual-item-assets.json","architecture-manifest.json","cover-manifest.json","arsenal-manifest.json"}){
        std::ifstream file(assetRoot/name);if(!file)continue;
        nlohmann::json manifest;file>>manifest;
        if(manifest.at("schema")!=1||!manifest.at("assets").is_array())throw std::runtime_error("Invalid native asset budget registry");
        for(const auto &row:manifest.at("assets")){
            auto id=row.at("id").get<std::string>();auto cap=row.at("budget").get<size_t>();
            if(!cap||!registered.emplace(id,cap).second)throw std::runtime_error("Duplicate or empty native asset budget: "+id);
        }
    }
    std::ifstream file(assetRoot/"mecha-redesign-budgets.json");
    if(file){
        nlohmann::json manifest;file>>manifest;const auto &scope=mechaBaselineBudgets();
        if(manifest.at("schema")!=1||manifest.at("units")!="triangles"||manifest.at("assets").size()!=scope.size())throw std::runtime_error("Invalid mecha redesign budget scope");
        for(const auto &row:manifest.at("assets")){
            auto id=row.at("id").get<std::string>();auto known=scope.find(id);
            auto hash=row.at("baseline_sha256").get<std::string>();
            if(known==scope.end()||row.at("baseline_budget").get<size_t>()!=known->second||row.at("budget").get<size_t>()!=known->second*2||
               row.at("file")!="meshes/"+id+".json"||row.at("baseline_file")!="provenance/mecha-redesign-baseline/meshes/"+id+".json"||
               hash.size()!=64||!std::all_of(hash.begin(),hash.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})||
               !approved.emplace(id,known==scope.end()?0:known->second*2).second)throw std::runtime_error("Unapproved mecha redesign budget: "+id);
        }
    }
    std::ifstream enemyFile(assetRoot/"enemy-redesign-budgets.json");
    if(enemyFile){
        nlohmann::json manifest;enemyFile>>manifest;const auto &scope=enemyRedesignBudgets();
        if(manifest.at("schema")!=1||manifest.at("units")!="triangles"||!manifest.at("assets").is_array()||manifest.at("assets").size()!=scope.size())throw std::runtime_error("Invalid remaining enemy redesign budget scope");
        for(const auto &row:manifest.at("assets")){
            auto id=row.at("id").get<std::string>();auto known=scope.find(id);auto hash=row.at("baseline_sha256").get<std::string>();
            if(known==scope.end()||row.at("baseline_budget").get<size_t>()!=known->second.first||row.at("budget").get<size_t>()!=known->second.second||
               row.at("budget_multiplier").get<size_t>()!=known->second.second/known->second.first||row.at("file")!="meshes/"+id+".json"||
               row.at("baseline_file")!="provenance/enemy-redesign-baseline/meshes/"+id+".json"||hash.size()!=64||
               !std::all_of(hash.begin(),hash.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})||
               !approved.emplace(id,known==scope.end()?0:known->second.second).second)throw std::runtime_error("Unapproved remaining enemy redesign budget: "+id);
        }
    }
    std::ifstream storyFile(assetRoot/"story-assets.json");
    if(storyFile){
        nlohmann::json story;storyFile>>story;
        static const std::map<std::string,std::string> derived={{"story_requisition_enforcer","mecha_recruit"},{"story_arena_collector","mecha_ace"},{"story_archive_sentinel","aeon_diode"},{"story_brood_guardian","kaiju_drone"}};
        static const std::set<std::string> props={"story_manifest_relay","story_air_plant","story_broadcast_pylon","story_docking_clamp","story_ice_cradle","story_oath_beacon","story_aeon_archive","story_rift_anchor"};
        if(story.at("schema")!=1||story.at("assets").size()!=12)throw std::runtime_error("Invalid story asset registry");
        std::set<std::string> seen;
        for(const auto &row:story.at("assets")){
            auto id=row.at("id").get<std::string>();auto cap=row.at("budget").get<size_t>();
            if(row.at("file")!="meshes/"+id+".json"||!seen.insert(id).second||!cap)throw std::runtime_error("Invalid story asset entry");
            if(auto source=derived.find(id);source!=derived.end()){
                auto allowed=approved.find(source->second);std::ifstream sourceFile(assetRoot/"meshes"/(source->second+".json"));nlohmann::json native;sourceFile>>native;
                if(row.at("source_id")!=source->second||allowed==approved.end()||cap>allowed->second||cap>native.at("budget").get<size_t>()||row.at("source_budget")!=native.at("budget")||row.at("source_mesh_sha256").get<std::string>().size()!=64)throw std::runtime_error("Invalid derived story enemy budget");
                approved.emplace(id,cap);
            }else if(!props.contains(id)||cap>256)throw std::runtime_error("Unknown story asset budget");
            if(!registered.emplace(id,cap).second)throw std::runtime_error("Duplicate story budget");
        }
    }
    registeredBudgets=std::move(registered);approvedMechaBudgets=std::move(approved);budgetsLoaded=true;
}
Asset &load(std::string_view id) {
    std::string key(id);
    if(auto i=cache.find(key);i!=cache.end())return i->second;
    if(key.empty()||key.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid original asset ID");
    std::ifstream f(assetRoot/"meshes"/(key+".json"));
    if(!f)throw std::runtime_error("Missing original Mecha Hack mesh: "+key);
    nlohmann::json j;f>>j;
    auto base=key.ends_with("_alt")?key.substr(0,key.size()-4):key;
    bool actor=base.starts_with("chassis_")||std::any_of(enemyCatalog().begin(),enemyCatalog().end(),[&](const auto &e){return base==e.id;});
    loadBudgetPolicy();
    size_t faceLimit=actor?1004:502;
    if(auto registered=registeredBudgets.find(key);registered!=registeredBudgets.end())faceLimit=std::min(faceLimit,registered->second);
    if(auto approved=approvedMechaBudgets.find(key);approved!=approvedMechaBudgets.end()){faceLimit=approved->second;actor=true;}
    if(j.at("schema")!=1||j.at("units")!="metres"||j.at("vertices").size()>(actor?8192u:4096u)||j.at("faces").size()>faceLimit||j.at("budget").get<size_t>()>faceLimit)throw std::runtime_error("Invalid Mecha Hack mesh budget: "+key);
    if(appearance.is_null())appearance=loadModelAppearance(assetRoot/"models");
    applyModelAppearance(j,appearance,key,1.f);
    std::vector<Vec3> points;
    Vec3 low{1e9f,1e9f,1e9f},high{-1e9f,-1e9f,-1e9f};
    for(auto &value:j.at("vertices")) {
        auto p=value.get<std::array<float,3>>();
        for(float c:p)if(!std::isfinite(c)||std::abs(c)>500)throw std::runtime_error("Invalid original mesh vertex");
        Vec3 v{p[0],p[1],p[2]};points.push_back(v);
        low={std::min(low.x,v.x),std::min(low.y,v.y),std::min(low.z,v.z)};
        high={std::max(high.x,v.x),std::max(high.y,v.y),std::max(high.z,v.z)};
    }
    auto mesh=std::make_shared<WorldMesh>();
    for(auto &face:j.at("faces")) {
        auto indices=face.at("indices").get<std::array<unsigned,3>>();
        auto rgb=face.at("color").get<std::array<float,3>>();
        for(float c:rgb)if(!std::isfinite(c)||c<0||c>255)throw std::runtime_error("Invalid original mesh color");
        Vec3 color{rgb[0]/255.f,rgb[1]/255.f,rgb[2]/255.f};
        std::array<std::array<float,2>,3> uv{};
        bool textured=face.contains("texture");
        if(textured){
            if(face.at("texture")!="psx")throw std::runtime_error("Unknown original texture");
            uv=face.at("uv").get<decltype(uv)>();
            for(auto p:uv)for(float v:p)if(!std::isfinite(v)||v<0||v>1)throw std::runtime_error("Invalid original mesh UV");
        }
        float textureMode=textured?2.f:0.f,tileCode=0;
        if(face.contains("repeat")){
            const auto &value=face.at("repeat").at("tile");
            if(!textured||!value.is_number_integer()||value.get<double>()<0||value.get<double>()>=64)throw std::runtime_error("Invalid repeated facade tile");
            int tile=value;
            uv=face.at("repeat").at("uv").get<decltype(uv)>();
            for(auto p:uv)for(float value:p)if(!std::isfinite(value)||std::abs(value)>512)throw std::runtime_error("Invalid repeated facade UV");
            textureMode=3;tileCode=float(tile%8+8*(7-tile/8));
        }
        for(size_t k=0;k<indices.size();++k){auto n=indices[k];if(n>=points.size())throw std::runtime_error("Original mesh index out of range");mesh->push_back({points[n],color,uv[k][0],uv[k][1],textureMode,tileCode});}
    }
    if(mesh->empty()||mesh->size()/3>j.at("budget").get<size_t>())throw std::runtime_error("Original mesh exceeds declared budget");
    auto wreck=destructionWreck(*mesh,key,j.at("budget").get<size_t>());
    return cache.emplace(key,Asset{mesh,wreck,(low+high)*.5f,(high-low)*.5f}).first->second;
}
}
std::filesystem::path gameAssetPath(const std::filesystem::path &path) {
#if defined(MECHA_GAME_ASSETS) && defined(MECHA_ASSETS)
    std::error_code error;
    if (std::filesystem::equivalent(path, MECHA_ASSETS, error))
        return MECHA_GAME_ASSETS;
#endif
    return path;
}
std::filesystem::path originalAssetRoot(){std::scoped_lock lock(cacheMutex);return assetRoot;}
void setOriginalAssetRoot(const std::filesystem::path &root){std::scoped_lock lock(cacheMutex);assetRoot=root;cache.clear();variants.clear();registeredBudgets.clear();approvedMechaBudgets.clear();budgetsLoaded=false;appearance=nullptr;}
std::string originalVariant(std::string_view base,uint32_t choice){
    std::string id(base);if(!(choice&1)||id.ends_with("_alt"))return id;
    std::scoped_lock lock(cacheMutex);auto found=variants.find(id);
    if(found==variants.end())found=variants.emplace(id,std::filesystem::exists(assetRoot/"meshes"/(id+"_alt.json"))).first;
    return found->second?id+"_alt":id;
}
ArmRig originalArmRig(){
    std::scoped_lock lock(cacheMutex);
    std::ifstream file(assetRoot/"models/rig.json");nlohmann::json data;file>>data;
    ArmRig rig;
    if(data.at("schema")!=1||data.at("held").size()!=rig.muzzle.size())throw std::runtime_error("Invalid original arm rig");
    rig.trackingScale=data.at("tracking_scale");rig.upperLength=data.at("upper_length");rig.foreLength=data.at("fore_length");
    for(float value:{rig.trackingScale,rig.upperLength,rig.foreLength})
        if(!std::isfinite(value)||value<.1f||value>10)throw std::runtime_error("Invalid original arm scale");
    for(size_t i=0;i<rig.muzzle.size();++i){
        auto value=data.at("held")[i].at("muzzle").get<std::array<float,3>>();
        rig.muzzle[i]={value[0],value[1],value[2]};
        if(!std::isfinite(length(rig.muzzle[i]))||length(rig.muzzle[i])>10)throw std::runtime_error("Invalid original muzzle");
    }
    return rig;
}
std::shared_ptr<WorldMesh> originalMesh(std::string_view id){std::scoped_lock lock(cacheMutex);return load(id).mesh;}
std::shared_ptr<WorldMesh> originalEnemyMesh(int index){return originalMesh(enemyModelId(size_t(index)));}
void applyOriginalModel(Target &t,std::string_view id){
    std::scoped_lock lock(cacheMutex);auto &a=load(id);t.generatorAsset=std::string(id);t.mesh=a.mesh;t.wreck=a.wreck;
    t.ai.localCenter=a.center;t.ai.localHalf=a.half;t.ai.boundsReady=true;
    auto offset=rotate(angles(t.yaw),a.center);t.center=t.origin+offset;
    float c=std::abs(std::cos(t.yaw)),s=std::abs(std::sin(t.yaw));
    t.half={a.half.x*c+a.half.z*s,a.half.y,a.half.z*c+a.half.x*s};
}
void applyOriginalEnemy(Target &t,int index){applyOriginalModel(t,originalVariant(enemyModelId(size_t(index)),t.generatorAsset.ends_with("_alt")?1:0));}
}
