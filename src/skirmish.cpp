#include "skirmish.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <stdexcept>
#include <vector>
namespace iron {
namespace {
using Json=nlohmann::json;
struct Random {
    uint32_t state;
    explicit Random(uint32_t seed):state(seed?seed:0x49535852u){}
    size_t below(size_t bound){if(!bound)throw std::runtime_error("No safe Skirmish site");uint32_t x=state;x^=x<<13;x^=x>>17;x^=x<<5;state=x;return x%bound;}
};
void require(bool ok,const char* message){if(!ok)throw std::invalid_argument(std::string("Invalid Skirmish settings: ")+message);}
std::string kindName(ProceduralGoalKind kind){switch(kind){case ProceduralGoalKind::Eliminate:return "eliminate";case ProceduralGoalKind::Strike:return "strike";case ProceduralGoalKind::Recon:return "recon";}throw std::invalid_argument("Invalid Skirmish objective");}
std::vector<int> distances(const std::vector<std::vector<int>>& adjacency,int start){
    std::vector<int> result(adjacency.size(),-1);result[start]=0;std::queue<int> queue;queue.push(start);
    while(!queue.empty()){int a=queue.front();queue.pop();for(int b:adjacency[a])if(result[b]<0){result[b]=result[a]+1;queue.push(b);}}
    return result;
}
}
Climate skirmishClimate(const SkirmishSettings& settings){
    auto biome=settings.terrain;if(biome=="auto"){constexpr std::array<const char*,4> biomes{"urban","grassland","desert","hills"};biome=biomes[(settings.seed>>1)%4];}
    return climateForBiome(biome);
}
Json generateSkirmishRecipe(const SkirmishSettings& settings){
    require(settings.cells>=skirmishMinCells&&settings.cells<=skirmishMaxCells,"map cells must be 2..8");
    require(settings.density>=skirmishMinDensity&&settings.density<=skirmishMaxDensity,"density must be 1..16");
    require(settings.groundPerZone>=skirmishMinGroundPerZone&&settings.groundPerZone<=skirmishMaxGroundPerZone,"ground units must be 1..12 per zone");
    require(settings.airUnits>=skirmishMinAirUnits&&settings.airUnits<=skirmishMaxAirUnits,"air units must be 0..8");
    require(std::find(skirmishTerrainOptions.begin(),skirmishTerrainOptions.end(),settings.terrain)!=skirmishTerrainOptions.end(),"unknown terrain");
    require(std::find(skirmishWaterOptions.begin(),skirmishWaterOptions.end(),settings.water)!=skirmishWaterOptions.end(),"unknown water layout");
    require(settings.reinforcementsPerZone>=0&&settings.reinforcementsPerZone<=12&&std::isfinite(settings.reinforcementInterval)&&settings.reinforcementInterval>=5&&settings.reinforcementInterval<=120,"reinforcements must be 0..12 per zone with interval 5..120 seconds");
    auto kind=kindName(settings.kind);const int cells=settings.cells,side=cells+1,nodeCount=side*side,goalCount=std::min(3,cells),spacing=120;const float half=cells*spacing/2.f;
    Random random(settings.seed);std::array<std::vector<float>,2> axes;
    for(auto& axis:axes){axis.push_back(-half);for(int i=1;i<cells;++i)axis.push_back(i*spacing-half+float(random.below(21))-10);axis.push_back(half);}
    std::vector<std::array<float,2>> nodes;nodes.reserve(nodeCount);for(auto z:axes[1])for(auto x:axes[0])nodes.push_back({x,z});
    std::vector<std::vector<int>> neighbors(nodeCount);
    for(int a=0;a<nodeCount;++a)for(int b:{a%side<cells?a+1:-1,a+side})if(b>=0&&b<nodeCount){neighbors[a].push_back(b);neighbors[b].push_back(a);}
    std::set<std::array<int,2>> edges;std::vector<bool> visited(nodeCount);visited[0]=true;std::vector<int> stack{0};
    while(!stack.empty()){
        int a=stack.back();std::vector<int> options;for(int b:neighbors[a])if(!visited[b])options.push_back(b);
        if(options.empty()){stack.pop_back();continue;}int b=options[random.below(options.size())];edges.insert({std::min(a,b),std::max(a,b)});visited[b]=true;stack.push_back(b);
    }
    const int middle=cells/2;auto arterial=[&](int a,int b){return (a/side==middle&&b/side==middle)||(a%side==middle&&b%side==middle);};int loopChance=20+int(random.below(26));
    for(int a=0;a<nodeCount;++a)for(int b:neighbors[a])if(a<b&&(arterial(a,b)||int(random.below(100))<loopChance))edges.insert({a,b});
    if(edges.size()==size_t(nodeCount-1)){
        std::vector<std::array<int,2>> extra;for(int a=0;a<nodeCount;++a)for(int b:neighbors[a])if(a<b&&!edges.contains({a,b}))extra.push_back({a,b});edges.insert(extra[random.below(extra.size())]);
    }
    Json roads=Json::array();std::vector<std::vector<int>> adjacency(nodeCount);
    for(auto edge:edges){int a=edge[0],b=edge[1];bool major=arterial(a,b);roads.push_back({{"nodes",edge},{"width",major?28:20},{"class",major?"arterial":"local"}});adjacency[a].push_back(b);adjacency[b].push_back(a);}
    constexpr std::array<const char*,3> roles{"industrial","storage","urban"};int rotation=int(random.below(3));Json districts=Json::array(),plots=Json::array();
    for(int i=0;i<4;++i){Json sources=i==1?Json::array({"is1"}):i==2?Json::array({"is2"}):Json::array({"is1","is2"});districts.push_back({{"id",i},{"role",roles[(i+rotation)%3]},{"sources",sources}});}
    for(int z=0;z<cells;++z)for(int x=0;x<cells;++x){
        int district=int(x>=cells/2.f)+2*int(z>=cells/2.f);auto& preference=districts[district];float cx=(axes[0][x]+axes[0][x+1])/2,cz=(axes[1][z]+axes[1][z+1])/2;
        float hx=(axes[0][x+1]-axes[0][x]-40)/2,hz=(axes[1][z+1]-axes[1][z]-40)/2;
        hx-=float(random.below(std::min(9,int(hx-4)+1)));hz-=float(random.below(std::min(9,int(hz-4)+1)));
        plots.push_back({{"id",plots.size()},{"bounds",{cx-hx,cz-hz,cx+hx,cz+hz}},{"district",district},{"sources",preference.at("sources")},{"role",preference.at("role")}});
    }
    auto fromSpawn=distances(adjacency,0);std::vector<int> candidates;for(int i=0;i<nodeCount;++i)if(fromSpawn[i]>=3)candidates.push_back(i);
    std::vector<int> selected;std::vector<std::vector<int>> fromSelected;std::set<int> usedPlots;Json objectives=Json::array();
    struct Choice {int score,node;std::vector<int> plots;};
    while(objectives.size()<size_t(goalCount)){
        std::vector<Choice> options;
        for(int node:candidates){
            if(std::find(selected.begin(),selected.end(),node)!=selected.end())continue;int nx=node%side,nz=node/side;std::vector<int> adjacent;
            for(int z:{nz-1,nz})for(int x:{nx-1,nx})if(x>=0&&z>=0&&x<cells&&z<cells&&!usedPlots.contains(z*cells+x))adjacent.push_back(z*cells+x);
            if(kind=="strike"&&adjacent.empty())continue;
            int separation=fromSpawn[node];if(!fromSelected.empty()){separation=nodeCount;for(auto& distance:fromSelected)separation=std::min(separation,distance[node]);}
            options.push_back({separation*3+fromSpawn[node],node,std::move(adjacent)});
        }
        require(!options.empty(),"not enough reachable objective sites");std::sort(options.begin(),options.end(),[](const Choice& a,const Choice& b){return a.score!=b.score?a.score>b.score:a.node<b.node;});
        auto chosen=options[random.below(std::min(size_t(3),options.size()))];selected.push_back(chosen.node);fromSelected.push_back(distances(adjacency,chosen.node));Json zone={{"node",chosen.node},{"radius",8},{"kind",kind=="eliminate"?"encounter":kind}};
        if(kind=="strike"){
            std::sort(chosen.plots.begin(),chosen.plots.end(),[&](int a,int b){bool urbanA=plots[a].at("role")=="urban",urbanB=plots[b].at("role")=="urban";return urbanA!=urbanB?!urbanA:a<b;});
            int target=chosen.plots[random.below(std::min(size_t(2),chosen.plots.size()))];zone["plot"]=target;usedPlots.insert(target);
        }
        objectives.push_back(std::move(zone));
    }
    auto biome=settings.terrain;auto water=settings.water;
    if(biome=="auto"){constexpr std::array<const char*,4> biomes{"urban","grassland","desert","hills"};biome=biomes[(settings.seed>>1)%4];}
    if(water=="auto"){constexpr std::array<const char*,3> layouts{"canal","lake","none"};water=layouts[settings.seed%3];}
    Json waters=Json::array();std::array<float,4> bounds{-half-24,-half-24,half+24,half+24};
    if(water=="canal"){
        int column=1+int((settings.seed/7)%uint32_t(cells-1));float x=axes[0][column];waters.push_back({{"bounds",{x+15,bounds[1]+8,x+29,bounds[3]-8}},{"level",-1},{"depth",6}});
        for(size_t i=0;i<plots.size();++i)if(int(i)%cells==column){auto b=plots[i].at("bounds").get<std::array<float,4>>();float next=axes[0][column+1],extra=std::max(0.f,next-x-60),left=std::min(std::max(0.f,b[0]-x-20),extra),right=std::min(std::max(0.f,next-20-b[2]),extra-left);b[0]=x+32+left;b[2]=next-20-right;plots[i]["bounds"]=b;}
    }
    if(water=="lake"){
        std::vector<size_t> options;for(size_t i=0;i<plots.size();++i)if(!usedPlots.contains(int(i)))options.push_back(i);require(!options.empty(),"no safe reservoir parcel");size_t chosen=options[(settings.seed/11)%options.size()];auto b=plots[chosen].at("bounds").get<std::array<float,4>>();float inset=std::min(2.f,(std::min(b[2]-b[0],b[3]-b[1])-4)/2);
        waters.push_back({{"bounds",{b[0]+inset,b[1]+inset,b[2]-inset,b[3]-inset}},{"level",-1},{"depth",6}});plots[chosen]["buildings"]=0;
    }
    for(auto& plot:plots)if(!plot.contains("buildings")){auto b=plot.at("bounds").get<std::array<float,4>>();int capacity=std::max(1,int((b[2]-b[0])/18))*std::max(1,int((b[3]-b[1])/18));plot["buildings"]=std::min({settings.density,capacity,768/int(plots.size())});}
    int amplitude=biome=="urban"?0:biome=="grassland"?3:biome=="desert"?5:18;
    Json recipe={{"schema","iron-procedural-layout"},{"version",3},{"generator","xorshift32-v3"},{"seed",settings.seed},{"units","metres"},{"plane","xz"},{"ground_y",0},{"pilot_radius",3.5},{"bounds",bounds},{"nodes",nodes},{"roads",roads},{"districts",districts},{"plots",plots},{"spawn",{{"node",0},{"radius",8}}},{"mission",{{"kind",kind},{"extraction",settings.extraction},{"hold_seconds",4}}},{"objectives",objectives},{"terrain",{{"biome",biome},{"amplitude",amplitude},{"water",waters},{"water_layout",water}}},{"encounters",{{"ground_per_zone",settings.groundPerZone},{"air_units",settings.airUnits}}}};
    recipe["environment"]=environmentWire(restrictEnvironment(settings.environment,climateForBiome(biome)));
    recipe["reinforcements"]={{"per_zone",settings.reinforcementsPerZone},{"interval",settings.reinforcementInterval}};
    if(settings.extraction)recipe["extraction"]={{"node",0},{"radius",8}};
    validateProceduralRecipe(recipe);return recipe;
}
}
