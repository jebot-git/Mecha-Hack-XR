#include "simulation.hpp"
#include "destruction_visuals.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
namespace iron {
namespace {
using Json=nlohmann::json;
Json read(const std::filesystem::path& p){std::ifstream f(p);if(!f)throw std::runtime_error("Missing decoration asset: "+p.string());Json j;f>>j;return j;}
}
void World::loadDecorations(const std::filesystem::path& root){
    auto shared=root/"is2/shared";
    auto catalog=read(std::filesystem::exists(shared/"gameplay/missions.json")?shared/"gameplay/missions.json":root/"gameplay/missions.json");
    auto manifest=read(root/"models/manifest.json");pickupTypes=manifest.at("pickup_types").get<decltype(pickupTypes)>();
    auto source=[&](const std::string& name){auto file=name+".json";return read(std::filesystem::exists(shared/"models"/file)?shared/"models"/file:root/"models"/file);};
    debrisVertices.clear();
    for(auto& [name,record]:manifest.at("models").items())if(name=="effect-debris"||name.starts_with("fragment-")){
        unsigned id=name=="effect-debris"?0:unsigned(std::stoul(name.substr(9)));auto j=source(name);
        for(auto& v:j.at("vertices"))debrisVertices[id].push_back({v[0].get<float>()*.01f,v[1].get<float>()*.01f,v[2].get<float>()*.01f});
    }
    auto missionCatalog=game?read(root/"is2/gameplay/missions.json"):catalog;
    auto light=missionCatalog.at("missions").at(mission).at("light").get<std::array<float,3>>();float ll=std::hypot(light[0],light[1],light[2]);
    std::array<int,3> lighting{};for(int k=0;k<3;++k)lighting[k]=int(light[k]*256/ll);
    for(unsigned kind=0;kind<5;++kind){auto j=source("pickup-"+std::to_string(kind));auto& mesh=pickupMeshes[kind];mesh.clear();size_t faceIndex=0;
        std::map<std::array<int,4>,unsigned> layers;std::vector<std::pair<Vec3,float>> shells;
        for(auto& face:j.at("faces")){
            auto ids=face.at("indices").get<std::vector<unsigned>>();int group=face.at("group"),material=face.at("material");
            auto n=j.at("normals").at(faceIndex++).get<std::array<int,4>>();
            int illumination=(n[0]*lighting[0]+n[1]*lighting[1]+n[2]*lighting[2])>>3;
            bool back=illumination<0;illumination=std::abs(illumination);if(back)illumination>>=1;
            int shade=face.at("shade"),bias=face.at("bias");if(night){shade>>=1;bias>>=1;}
            int intensity=std::clamp(((illumination*shade)>>12)+bias,0,255);
            int c=(material>>12)&15,r=(material>>8)&15;auto& table=catalog.at("cry_table");
            Vec3 color{float((table["red"][c][r].get<int>()*intensity)>>8)/255,float((table["green"][c][r].get<int>()*intensity)>>8)/255,float((table["blue"][c][r].get<int>()*intensity)>>8)/255};
            // Retail crate labels are painter overlays, often coplanar with their shell.
            Vec3 normal=unit(Vec3{float(n[0]),float(n[1]),-float(n[2])});
            auto raw=[&](size_t id){auto p=j.at("vertices").at(id).get<std::array<float,3>>();return Vec3{p[0]*.01f,p[1]*.01f,-p[2]*.01f};};
            float plane=dot(normal,raw(ids.front()));
            std::array<int,4> key{int(std::round(normal.x*1000)),int(std::round(normal.y*1000)),int(std::round(normal.z*1000)),int(std::round(plane*1000))};
            float offset=.008f*layers[key]++;
            if(group==2)for(auto [base,distance]:shells){float alignment=dot(normal,base);if(alignment<.995f)continue;
                float low=1e9f;for(auto id:ids)low=std::min(low,dot(base,raw(id)));float gap=distance-low;
                if(gap>=-.12f&&gap<=.12f)offset=std::max(offset,(gap+.008f)/alignment);
            }
            if(group==0)shells.push_back({normal,plane+offset});
            std::vector<WorldVertex> vertices;
            for(size_t k=0;k<ids.size();++k){auto& p=j.at("vertices").at(ids[k]);WorldVertex v{{p[0].get<float>()*.01f,p[1].get<float>()*.01f,-p[2].get<float>()*.01f},color};
                if(group==2){auto uv=catalog.at("texture_uv").at(material).at(k);v.u=uv[0];v.v=uv[1];v.color={1,1,1};v.textured=1;}v.position=v.position+normal*offset;vertices.push_back(v);
            }
            for(size_t k=1;k+1<vertices.size();++k)for(size_t i:{size_t(0),k,k+1})mesh.push_back(vertices[i]);
        }
    }
}
void World::indexTerrain(){
    auto &map=editGeometry();
    map.groundTriangles.clear();map.groundTiles.clear();
    for(size_t i=0;i+2<map.terrain.size();i+=3){
        auto a=map.terrain[i].position,b=map.terrain[i+1].position,c=map.terrain[i+2].position;
        float denominator=(b.z-c.z)*(a.x-c.x)+(c.x-b.x)*(a.z-c.z);
        if(std::abs(denominator)<.000001f)continue;
        size_t index=map.groundTriangles.size();map.groundTriangles.push_back({a,b,c,denominator,i});
        int x0=int(std::floor(std::min({a.x,b.x,c.x})/64)),x1=int(std::floor(std::max({a.x,b.x,c.x})/64));
        int z0=int(std::floor(std::min({a.z,b.z,c.z})/64)),z1=int(std::floor(std::max({a.z,b.z,c.z})/64));
        for(int x=x0;x<=x1;++x)for(int z=z0;z<=z1;++z)map.groundTiles[{x,z}].push_back(index);
    }
}
float World::surfaceHeight(float x,float z)const{
    auto cell=geometry().groundTiles.find({int(std::floor(x/64)),int(std::floor(z/64))});
    if(cell==geometry().groundTiles.end())return 0;
    float height=-1e9f;bool found=false;
    for(auto index:cell->second){auto& t=geometry().groundTriangles[index];
        float a=((t.b.z-t.c.z)*(x-t.c.x)+(t.c.x-t.b.x)*(z-t.c.z))/t.denominator;
        float b=((t.c.z-t.a.z)*(x-t.c.x)+(t.a.x-t.c.x)*(z-t.c.z))/t.denominator;
        if(a>=-.00001f&&b>=-.00001f&&a+b<=1.00001f){height=std::max(height,a*t.a.y+b*t.b.y+(1-a-b)*t.c.y);found=true;}
    }
    return found?height:0;
}
bool World::hasWater(float x,float z,float radius)const{
    if(!std::isfinite(x)||!std::isfinite(z)||!std::isfinite(radius)||radius<0)return true;
    for(const auto& water:waterRegions){const auto& b=water.bounds;
        if(x+radius<=b[0]||x-radius>=b[2]||z+radius<=b[1]||z-radius>=b[3])continue;
        if(!water.shore.empty()&&proceduralWaterEdge(water,x,z)<-radius*1.414214f)continue;
        // Only the footprint over water needs bridge support. Requiring the
        // dry bank to fit on the bridge creates an impassable lip at its ends.
        float lowX=std::max(x-radius,b[0]),highX=std::min(x+radius,b[2]);
        float lowZ=std::max(z-radius,b[1]),highZ=std::min(z+radius,b[3]);
        bool crossing=false;for(const auto& road:waterCrossings)if(lowX>=road[0]&&highX<=road[2]&&lowZ>=road[1]&&highZ<=road[3]){crossing=true;break;}
        if(!crossing)return true;
    }
    return false;
}
bool World::groundPassable(Vec3 position,float radius,bool allowDrop)const{
    if(!std::isfinite(position.x)||!std::isfinite(position.z)||!std::isfinite(radius)||radius<0||position.x-radius<movementBounds[0]||position.z-radius<movementBounds[1]||position.x+radius>movementBounds[2]||position.z+radius>movementBounds[3]||(hasWater(position.x,position.z,radius)&&(waterTraversalLimitAt(position,radius)<=0||wadingDepthAt(position,radius)>waterTraversalLimitAt(position,radius)+.001f)))return false;
    float center=walkableHeight(position),span=std::max(.5f,radius);
    for(auto offset:std::array<Vec3,8>{{{span,0,0},{-span,0,0},{0,0,span},{0,0,-span},{span,0,span},{span,0,-span},{-span,0,span},{-span,0,-span}}})
        {
            const float rise=walkableHeight({position.x+offset.x,center,position.z+offset.z},.65f*std::hypot(offset.x,offset.z)+.5f)-center;
            if((allowDrop?rise:std::abs(rise))>.5f+.5f*std::hypot(offset.x,offset.z))return false;
        }
    return true;
}
float World::groundContact(Vec3 from,Vec3 to)const{
    // Retail overlays remain the original flat physical plane.
    if(!proceduralTerrain&&waterRegions.empty()){if(from.y<0)return 0;return to.y<0?std::clamp(from.y/(from.y-to.y),0.f,1.f):2;}
    auto gap=[&](Vec3 p){float floor=surfaceHeight(p.x,p.z);if(hasWater(p.x,p.z)&&waterTraversalLimitAt(p)<=maxWadeDepth)for(const auto& water:waterRegions)if(proceduralWaterEdge(water,p.x,p.z)>=0)floor=std::max(floor,water.level);return p.y-floor;};
    if(gap(from)<-.00001f)return 0;
    const float deck=deckContact(from,to);
    auto delta=to-from;int samples=std::clamp(int(std::ceil(std::hypot(delta.x,delta.z))),1,512);
    float previous=0;
    for(int sample=1;sample<=samples;++sample){float current=float(sample)/samples;if(gap(from+delta*current)<-.00001f){float low=previous,high=current;for(int n=0;n<12;++n){float middle=(low+high)*.5f;if(gap(from+delta*middle)>=0)low=middle;else high=middle;}return std::min(deck,high);}previous=current;}
    return deck;
}
float World::debrisFloor(const WeaponEffect& fx)const{
    if(rpgDebris(fx.model)){
        auto half=rpgFragmentHalf(fx.model);float spin=destructionSpin(fx.age,fx.model);
        auto orientation=angles(spin*700*2*pi/2048,spin*1600*2*pi/2048);float floor=-1e9f;
        for(float x:{-half.x,half.x})for(float y:{-half.y,half.y})for(float z:{-half.z,half.z}){
            auto p=rotate(orientation,Vec3{x,y,z}*fx.size);floor=std::max(floor,walkableHeight(fx.position+Vec3{p.x,0,p.z})-p.y);}
        return floor;
    }
    auto found=debrisVertices.find(fx.model);
    if(found==debrisVertices.end())return walkableHeight(fx.position)+.5f*fx.size;
    auto orientation=angles(fx.age*700*2*pi/2048,fx.age*1600*2*pi/2048);
    float floor=-1e9f;
    for(auto v:found->second){auto p=rotate(orientation,v*fx.size);floor=std::max(floor,walkableHeight(fx.position+Vec3{p.x,0,p.z})-p.y);}
    return floor;
}
Vec3 World::pickupPosition(const Pickup& p)const{
    unsigned kind=pickupTypes[unsigned(p.kind)&31];float low=0;
    for(const auto& v:pickupMeshes.at(kind))low=std::min(low,v.position.y);
    return {p.position.x,walkableHeight(p.position)-low,p.position.z};
}
Vec3 World::wreckPosition(const Target& target)const{
    if(rpgPrototype&&target.rpgDeathAt>=0)return {target.origin.x,target.rpgRestY,target.origin.z};
    if(!target.wreck||target.wreck->empty())return target.origin;
    auto orientation=angles(target.yaw,target.ai.pitch,target.ai.bank);float support=-1e9f;
    for(const auto& v:*target.wreck){auto p=rotate(orientation,v.position);
        support=std::max(support,walkableHeight(target.origin+Vec3{p.x,0,p.z})-p.y);
    }
    return {target.origin.x,support,target.origin.z};
}
}
