#include "simulation.hpp"
#include "procedural_terrain.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace iron {
namespace {
using Json=nlohmann::json;
using Rect=std::array<float,4>;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(std::string("Invalid procedural terrain: ")+message);}
bool number(const Json& j){return j.is_number()&&std::isfinite(j.get<double>());}
bool integer(const Json& j,int a,int b){return j.is_number_integer()&&j.get<double>()>=a&&j.get<double>()<=b;}
bool rectangle(const Json& j){return j.is_array()&&j.size()==4&&std::all_of(j.begin(),j.end(),number)&&j[0].get<double>()<j[2].get<double>()&&j[1].get<double>()<j[3].get<double>();}
bool overlap(Rect a,Rect b){return a[0]<b[2]&&b[0]<a[2]&&a[1]<b[3]&&b[1]<a[3];}
bool contains(Rect r,float x,float z){return x>=r[0]&&x<=r[2]&&z>=r[1]&&z<=r[3];}
}
float proceduralWaterEdge(const ProceduralWater &w,float x,float z){
    if(w.shore.empty())return std::min({x-w.bounds[0],w.bounds[2]-x,z-w.bounds[1],w.bounds[3]-z});
    bool inside=false;float nearest=1e20f;
    for(size_t i=0,j=w.shore.size()-1;i<w.shore.size();j=i++){
        auto a=w.shore[j],b=w.shore[i];float dx=b[0]-a[0],dz=b[1]-a[1];
        float t=std::clamp(((x-a[0])*dx+(z-a[1])*dz)/std::max(.001f,dx*dx+dz*dz),0.f,1.f);
        nearest=std::min(nearest,std::hypot(x-a[0]-dx*t,z-a[1]-dz*t));
        if((a[1]>z)!=(b[1]>z)&&x<(b[0]-a[0])*(z-a[1])/(b[1]-a[1])+a[0])inside=!inside;
    }
    return inside?nearest:-nearest;
}
float proceduralRelief(const Json& terrain,float x,float z){
    float value=0;if(!terrain.contains("depressions"))return value;
    for(const auto& feature:terrain.at("depressions")){
        auto center=feature.at("center").get<std::array<float,2>>();float radius=feature.at("radius");
        float weight=std::max(0.f,1-((x-center[0])*(x-center[0])+(z-center[1])*(z-center[1]))/(radius*radius));
        value-=feature.at("depth").get<float>()*weight*weight;
    }
    return value;
}
void validateProceduralTerrain(const Json& recipe){
    require(recipe.contains("terrain")&&recipe.at("terrain").is_object(),"missing physical terrain");auto& terrain=recipe.at("terrain");
    require(terrain.contains("biome")&&(terrain.at("biome")=="urban"||terrain.at("biome")=="grassland"||terrain.at("biome")=="desert"||terrain.at("biome")=="hills"),"unknown biome");
    require(terrain.contains("amplitude")&&number(terrain.at("amplitude"))&&terrain.at("amplitude").get<double>()>=0&&terrain.at("amplitude").get<double>()<=24,"invalid elevation amplitude");
    require(terrain.contains("water")&&terrain.at("water").is_array()&&terrain.at("water").size()<=8,"water budget exceeded");
    if(terrain.contains("depressions")){
        require(terrain.at("depressions").is_array()&&terrain.at("depressions").size()<=64,"depression budget exceeded");
        for(const auto& f:terrain.at("depressions")){
            require(f.contains("center")&&f.at("center").is_array()&&f.at("center").size()==2&&number(f.at("center")[0])&&number(f.at("center")[1]),"invalid depression center");
            require(f.contains("radius")&&number(f.at("radius"))&&f.at("radius").get<float>()>=8&&f.at("radius").get<float>()<=200,"invalid depression radius");
            require(f.contains("depth")&&number(f.at("depth"))&&f.at("depth").get<float>()>=0&&f.at("depth").get<float>()<=24,"invalid depression depth");
        }
    }
    auto bounds=recipe.at("bounds").get<Rect>();std::vector<Rect> waters;
    for(auto& body:terrain.at("water")){
        require(body.is_object()&&body.contains("bounds")&&rectangle(body.at("bounds")),"invalid water rectangle");auto r=body.at("bounds").get<Rect>();
        require(r[0]>=bounds[0]&&r[1]>=bounds[1]&&r[2]<=bounds[2]&&r[3]<=bounds[3],"water outside map");
        require(body.contains("level")&&number(body.at("level"))&&body.at("level").get<double>()>=-4&&body.at("level").get<double>()<=0,"invalid water level");
        require(body.contains("depth")&&number(body.at("depth"))&&body.at("depth").get<double>()>=2&&body.at("depth").get<double>()<=16,"invalid water depth");
        require(!body.contains("bank_width")||(number(body.at("bank_width"))&&body.at("bank_width").get<double>()>=0&&body.at("bank_width").get<double>()<=96),"invalid graded bank width");
        if(body.contains("shore")){
            const auto &shore=body.at("shore");require(recipe.at("version")==4&&shore.is_array()&&shore.size()>=8&&shore.size()<=32,"invalid shoreline budget");
            float previous=-4;const float cx=(r[0]+r[2])/2,cz=(r[1]+r[3])/2;
            for(const auto &p:shore){require(p.is_array()&&p.size()==2&&number(p[0])&&number(p[1]),"invalid shoreline point");
                float x=p[0],z=p[1],angle=std::atan2(z-cz,x-cx);
                require(contains(r,x,z)&&angle>previous+.001f&&std::hypot(x-cx,z-cz)>1,"shore must wind once around its centre");previous=angle;}
            require(previous>0,"incomplete shoreline");
        }
        for(auto other:waters)require(!overlap(r,other),"overlapping water bodies");waters.push_back(r);
    }
    size_t buildings=0;
    for(auto& plot:recipe.at("plots")){
        require(plot.contains("buildings")&&integer(plot.at("buildings"),0,16),"invalid parcel density");int count=plot.at("buildings");buildings+=size_t(count);
        if(count){auto r=plot.at("bounds").get<Rect>();for(auto water:waters)require(!overlap(r,water),"building parcel overlaps water");}
    }
    require(buildings<=size_t(recipe.at("version")==4?2048:768),"scenery budget exceeded");
    if(recipe.at("mission").at("kind")=="strike")for(auto& objective:recipe.at("objectives"))require(recipe.at("plots").at(objective.at("plot").get<size_t>()).at("buildings").get<int>()>0,"strike target parcel is empty");
    require(recipe.contains("encounters")&&recipe.at("encounters").is_object(),"missing encounter controls");auto& encounters=recipe.at("encounters");
    require(encounters.contains("ground_per_zone")&&integer(encounters.at("ground_per_zone"),1,12)&&encounters.at("ground_per_zone").get<size_t>()*recipe.at("objectives").size()<=64,"ground encounter budget exceeded");
    require(encounters.contains("air_units")&&integer(encounters.at("air_units"),0,8),"air encounter budget exceeded");
}

void buildProceduralTerrain(World& world,const Json& recipe){
    validateProceduralTerrain(recipe);
    if(recipe.at("version")==4){buildSegmentedTerrain(world,recipe);return;}
    world.editGeometry().terrainPatches.clear();auto& terrain=recipe.at("terrain");auto bounds=recipe.at("bounds").get<Rect>();
    world.proceduralTerrain=true;world.waterRegions.clear();world.waterCrossings.clear();world.editGeometry().waterMesh.clear();world.editGeometry().terrain.clear();
    std::string biome=terrain.at("biome");float amplitude=terrain.at("amplitude");
    Vec3 color=biome=="desert"?Vec3{.55f,.42f,.21f}:biome=="grassland"?Vec3{.22f,.35f,.14f}:biome=="hills"?Vec3{.27f,.33f,.17f}:Vec3{.16f,.20f,.17f};world.groundColor=color;
    float phase=float(world.prototypeSeed%360)*pi/180,second=float((world.prototypeSeed/361)%360)*pi/180;
    auto base=[&](float x,float z){
        float sx=biome=="desert"?80.f:150.f,sz=biome=="hills"?180.f:120.f;
        return amplitude*(.5f+.25f*std::sin(x/sx+phase)+.25f*std::cos(z/sz+second))+proceduralRelief(terrain,x,z);
    };
    struct Road {Rect bounds;Vec3 color;};std::vector<Road> roads;
    for(auto& road:recipe.at("roads")){
        auto ids=road.at("nodes").get<std::array<size_t,2>>();auto a=recipe.at("nodes")[ids[0]].get<std::array<float,2>>(),b=recipe.at("nodes")[ids[1]].get<std::array<float,2>>();float half=road.at("width").get<float>()/2;
        Rect r{std::min(a[0],b[0])-half,std::min(a[1],b[1])-half,std::max(a[0],b[0])+half,std::max(a[1],b[1])+half};world.waterCrossings.push_back(r);
        Vec3 roadColor=biome=="desert"?Vec3{.38f,.31f,.21f}:road.value("class",std::string("local"))=="arterial"?Vec3{.25f,.27f,.28f}:Vec3{.18f,.21f,.22f};roads.push_back({r,roadColor});
    }
    struct Pad {Rect bounds;float height;};std::vector<Pad> pads;
    for(auto& plot:recipe.at("plots"))if(plot.at("buildings").get<int>()>0){auto r=plot.at("bounds").get<Rect>();pads.push_back({r,base((r[0]+r[2])/2,(r[1]+r[3])/2)});}
    for(auto& body:terrain.at("water"))world.waterRegions.push_back({body.at("bounds").get<Rect>(),body.at("level"),body.at("depth"),body.value("bank_width",0.f)});
    auto height=[&](float x,float z){
        float value=base(x,z);
        for(auto& road:roads)if(contains(road.bounds,x,z))return value;
        for(auto water:world.waterRegions)if(contains(water.bounds,x,z)){
            float edge=std::min({x-water.bounds[0],water.bounds[2]-x,z-water.bounds[1],water.bounds[3]-z});
            float blend=water.bankWidth>0?std::clamp(edge/water.bankWidth,0.f,1.f):1.f;
            return value*(1-blend)+(water.level-water.depth)*blend;
        }
        float weight=0,padHeight=value;
        for(auto pad:pads){float dx=std::max({pad.bounds[0]-x,x-pad.bounds[2],0.f}),dz=std::max({pad.bounds[1]-z,z-pad.bounds[3],0.f});float blend=std::max(0.f,1-std::hypot(dx,dz)/8);
            if(blend>weight){weight=blend;padHeight=pad.height;}}
        return value*(1-weight)+padHeight*weight;
    };
    std::array<std::vector<float>,2> axes;
    auto span=[](std::vector<float>& axis,float a,float b,float step){int count=std::max(1,int(std::ceil((b-a)/step)));for(int n=0;n<=count;++n)axis.push_back(a+(b-a)*float(n)/count);};
    for(int axis=0;axis<2;++axis){span(axes[axis],-1000,bounds[axis],128);span(axes[axis],bounds[axis],bounds[axis+2],12);span(axes[axis],bounds[axis+2],1000,128);
        for(auto road:roads){axes[axis].push_back(road.bounds[axis]);axes[axis].push_back(road.bounds[axis+2]);}
        for(auto pad:pads){axes[axis].push_back(pad.bounds[axis]);axes[axis].push_back(pad.bounds[axis+2]);}
        for(auto water:world.waterRegions){axes[axis].push_back(water.bounds[axis]);axes[axis].push_back(water.bounds[axis+2]);
            if(water.bankWidth>0){axes[axis].push_back(std::min(water.bounds[axis]+water.bankWidth,(water.bounds[axis]+water.bounds[axis+2])/2));axes[axis].push_back(std::max(water.bounds[axis+2]-water.bankWidth,(water.bounds[axis]+water.bounds[axis+2])/2));}}
        std::sort(axes[axis].begin(),axes[axis].end());axes[axis].erase(std::unique(axes[axis].begin(),axes[axis].end(),[](float a,float b){return std::abs(a-b)<.001f;}),axes[axis].end());
        require(axes[axis].size()<=512,"sample axis budget exceeded");
    }
    auto& xs=axes[0];auto& zs=axes[1];require((xs.size()-1)*(zs.size()-1)<=200000,"height mesh budget exceeded");std::vector<float> samples(xs.size()*zs.size());
    for(size_t z=0;z<zs.size();++z)for(size_t x=0;x<xs.size();++x)samples[z*xs.size()+x]=height(xs[x],zs[z]);
    world.editGeometry().terrain.reserve((xs.size()-1)*(zs.size()-1)*6);
    for(size_t z=0;z+1<zs.size();++z)for(size_t x=0;x+1<xs.size();++x){
        float cx=(xs[x]+xs[x+1])/2,cz=(zs[z]+zs[z+1])/2;Vec3 tint=color;bool roadCell=false;
        for(auto road:roads)if(contains(road.bounds,cx,cz)){tint=road.color;roadCell=true;break;}
        if(!roadCell){float variation=.85f+.2f*(amplitude>0?base(cx,cz)/amplitude:.5f);tint=tint*variation;for(auto water:world.waterRegions)if(contains(water.bounds,cx,cz)){tint={.11f,.15f,.12f};break;}}
        Vec3 a{xs[x],samples[z*xs.size()+x],zs[z]},b{xs[x+1],samples[z*xs.size()+x+1],zs[z]},c{xs[x+1],samples[(z+1)*xs.size()+x+1],zs[z+1]},d{xs[x],samples[(z+1)*xs.size()+x],zs[z+1]};
        for(auto p:{a,b,c,a,c,d})world.editGeometry().terrain.push_back({p,tint});
    }
    for(auto water:world.waterRegions){auto r=water.bounds;Vec3 a{r[0],water.level,r[1]},b{r[2],water.level,r[1]},c{r[2],water.level,r[3]},d{r[0],water.level,r[3]};for(auto p:{a,c,b,a,d,c})world.editGeometry().waterMesh.push_back({p,{.035f,.24f,.42f}});}
    world.indexTerrain();
}
}
