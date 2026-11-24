#include "simulation.hpp"
#include "terrain_art.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace iron {
namespace {
using Json=nlohmann::json;using Rect=std::array<float,4>;
constexpr float tileSize=256,spacing=32;
struct Feature {Rect bounds;Vec3 color{};float height=0;std::array<float,2> a{},b{};float radius=0;};
struct Tile {Rect bounds;std::array<std::vector<float>,2> axes;std::vector<size_t> roads,pads,water;};
bool contains(Rect r,float x,float z){return r[0]<=x&&x<=r[2]&&r[1]<=z&&z<=r[3];}
bool contains(const Feature &f,float x,float z){
    if(!contains(f.bounds,x,z))return false;
    if(f.radius<=0)return true;
    float dx=f.b[0]-f.a[0],dz=f.b[1]-f.a[1];
    float t=std::clamp(((x-f.a[0])*dx+(z-f.a[1])*dz)/std::max(.001f,dx*dx+dz*dz),0.f,1.f);
    return std::hypot(x-f.a[0]-dx*t,z-f.a[1]-dz*t)<=f.radius;
}
void tidy(std::vector<float>& v){std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end(),[](float a,float b){return std::abs(a-b)<.0001f;}),v.end());}
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(std::string("Segmented terrain: ")+message);}
}
// Local axes capture parcel/road edges without a world-wide Cartesian product.
// Boundary fans insert the union of both neighbours' edge vertices, avoiding
// T-junction cracks while keeping every interior tile independent.
void buildSegmentedTerrain(World& world,const Json& recipe){
    auto bounds=recipe.at("bounds").get<Rect>();const auto& terrain=recipe.at("terrain");
    int nx=int(std::ceil((bounds[2]-bounds[0])/tileSize)),nz=int(std::ceil((bounds[3]-bounds[1])/tileSize));
    require(nx>0&&nz>0&&nx*nz<=4096,"tile budget exceeded");
    world.proceduralTerrain=true;world.waterRegions.clear();world.waterCrossings.clear();world.editGeometry().waterMesh.clear();world.editGeometry().terrain.clear();world.editGeometry().terrainPatches.clear();
    std::string biome=terrain.at("biome");float amplitude=terrain.at("amplitude");
    Vec3 color=biome=="desert"?Vec3{.55f,.42f,.21f}:biome=="grassland"?Vec3{.22f,.35f,.14f}:biome=="hills"?Vec3{.27f,.33f,.17f}:Vec3{.16f,.20f,.17f};world.groundColor=color;
    float phase=float(world.prototypeSeed%360)*pi/180,second=float((world.prototypeSeed/361)%360)*pi/180;
    auto base=[&](float x,float z){return amplitude*(.5f+.25f*std::sin(x/(biome=="desert"?80.f:150.f)+phase)+.25f*std::cos(z/(biome=="hills"?180.f:120.f)+second))+proceduralRelief(terrain,x,z);};
    std::vector<Tile> tiles(size_t(nx*nz));
    for(int z=0;z<nz;++z)for(int x=0;x<nx;++x){auto& t=tiles[size_t(z*nx+x)];
        t.bounds={bounds[0]+x*tileSize,bounds[1]+z*tileSize,std::min(bounds[2],bounds[0]+(x+1)*tileSize),std::min(bounds[3],bounds[1]+(z+1)*tileSize)};
        for(int a=0;a<2;++a){t.axes[a].push_back(t.bounds[a]);t.axes[a].push_back(t.bounds[a+2]);for(int n=1;n<8;++n){float v=t.bounds[a]+n*spacing;if(v<t.bounds[a+2])t.axes[a].push_back(v);}}
    }
    auto tileIndex=[&](float x,float z){return size_t(std::clamp(int(std::floor((z-bounds[1])/tileSize)),0,nz-1)*nx+std::clamp(int(std::floor((x-bounds[0])/tileSize)),0,nx-1));};
    std::vector<Feature> roads,pads;
    struct RoadSurface {float acrossX, acrossZ, alongX, alongZ, width, length; int layer;};
    std::vector<RoadSurface> roadSurfaces;
    const int groundLayer=std::clamp(terrain.value("art_layer",0),0,int(terrainArt::Lunar));
    const bool facilityFloor=terrain.value("facility_floor",false);
    auto add=[&](Rect r,size_t id,int type,float feather,bool cutAxes=true){
        Rect extended{r[0]-feather,r[1]-feather,r[2]+feather,r[3]+feather};
        int x0=std::clamp(int(std::floor((extended[0]-bounds[0])/tileSize)),0,nx-1),x1=std::clamp(int(std::floor((extended[2]-bounds[0])/tileSize)),0,nx-1);
        int z0=std::clamp(int(std::floor((extended[1]-bounds[1])/tileSize)),0,nz-1),z1=std::clamp(int(std::floor((extended[3]-bounds[1])/tileSize)),0,nz-1);
        // Include the tile immediately before a feature exactly on its edge.
        if(extended[0]==bounds[0]+x0*tileSize&&x0>0)--x0;
        if(extended[1]==bounds[1]+z0*tileSize&&z0>0)--z0;
        for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){auto& t=tiles[size_t(z*nx+x)];(type==0?t.roads:type==1?t.pads:t.water).push_back(id);
            if(cutAxes)for(int a=0;a<2;++a)for(float v:{r[a],r[a+2],extended[a],extended[a+2]})if(v>t.bounds[a]&&v<t.bounds[a+2])t.axes[a].push_back(v);
        }
    };
    for(const auto& road:recipe.at("roads")){auto ends=road.at("nodes").get<std::array<size_t,2>>();auto p=recipe.at("nodes")[ends[0]].get<std::array<float,2>>(),q=recipe.at("nodes")[ends[1]].get<std::array<float,2>>();float h=road.at("width").get<float>()/2;
        Rect r{std::min(p[0],q[0])-h,std::min(p[1],q[1])-h,std::max(p[0],q[0])+h,std::max(p[1],q[1])+h};
        Vec3 tint=biome=="desert"?Vec3{.38f,.31f,.21f}:road.value("class",std::string("local"))=="arterial"?Vec3{.25f,.27f,.28f}:Vec3{.18f,.21f,.22f};
        bool ribbon=road.value("geometry",std::string{})=="ribbon";add(r,roads.size(),0,0,!ribbon);
        roads.push_back({r,tint,0,p,q,ribbon?h:0});
        const float length=std::max(.001f,std::hypot(q[0]-p[0],q[1]-p[1]));
        const float alongX=(q[0]-p[0])/length,alongZ=(q[1]-p[1])/length;
        roadSurfaces.push_back({-alongZ,alongX,alongX,alongZ,2*h,length,
            road.value("class",std::string{})=="arterial"?terrainArt::Arterial:terrainArt::ServiceRoad});
        if(!ribbon)world.waterCrossings.push_back(r);
        else {
            // Short overlapping squares stay inside the ribbon. A diagonal's
            // entire AABB would incorrectly drain the lake beside the road.
            int steps=std::max(1,int(std::ceil(std::hypot(q[0]-p[0],q[1]-p[1])/4)));
            float half=std::max(1.f,(h-3)*.70710678f);
            for(int i=0;i<=steps;++i){float t=float(i)/steps,x=p[0]+(q[0]-p[0])*t,z=p[1]+(q[1]-p[1])*t;
                bool wet=false;for(const auto &water:terrain.at("water")){auto b=water.at("bounds").get<Rect>();
                    if(x+half>=b[0]&&x-half<=b[2]&&z+half>=b[1]&&z-half<=b[3]){wet=true;break;}}
                if(wet)world.waterCrossings.push_back({x-half,z-half,x+half,z+half});}
        }
    }
    // A ribbon's AABB is not its boundary. Cutting every such bound into both
    // axes creates a quadratic sample explosion without following the curve.
    // Refine only occupied tiles on a bounded 16m lattice instead.
    for(auto &tile:tiles)if(std::any_of(tile.roads.begin(),tile.roads.end(),[&](size_t id){return roads[id].radius>0;}))
        for(int axis=0;axis<2;++axis)for(float v=tile.bounds[axis]+16;v<tile.bounds[axis+2];v+=16)tile.axes[axis].push_back(v);
    for(const auto& p:recipe.at("plots"))if(p.at("buildings").get<int>()>0){auto r=p.at("bounds").get<Rect>();add(r,pads.size(),1,8);pads.push_back({r,{},base((r[0]+r[2])/2,(r[1]+r[3])/2)});}
    for(const auto& body:terrain.at("water")){
        auto r=body.at("bounds").get<Rect>();size_t id=world.waterRegions.size();
        add(r,id,2,0);float bank=body.value("bank_width",0.f);
        world.waterRegions.push_back({r,body.at("level"),body.at("depth"),bank,body.value("shore",std::vector<std::array<float,2>>{})});
        // Preserve the exact bank-to-bed break in every local tile. A coarse
        // 32m sample straddling it otherwise shallows a flat waist-depth bed.
        if(bank>0)for(auto& tile:tiles)if(std::find(tile.water.begin(),tile.water.end(),id)!=tile.water.end())
            for(int axis=0;axis<2;++axis)for(float v:{std::min(r[axis]+bank,(r[axis]+r[axis+2])/2),std::max(r[axis+2]-bank,(r[axis]+r[axis+2])/2)})
                if(v>tile.bounds[axis]&&v<tile.bounds[axis+2])tile.axes[axis].push_back(v);
    }
    for(auto& tile:tiles)for(auto& axis:tile.axes)tidy(axis);
    auto height=[&](float x,float z){const auto& t=tiles[tileIndex(x,z)];float value=base(x,z);
        for(auto id:t.roads)if(contains(roads[id],x,z))return value;
        for(auto id:t.water){const auto& w=world.waterRegions[id];if(contains(w.bounds,x,z)){
            float edge=proceduralWaterEdge(w,x,z);if(edge<0)continue;
            float blend=w.bankWidth>0?std::clamp(edge/w.bankWidth,0.f,1.f):1.f;
            return value*(1-blend)+(w.level-w.depth)*blend;
        }}
        float weight=0,padHeight=value;
        for(auto id:t.pads){auto p=pads[id];float dx=std::max({p.bounds[0]-x,x-p.bounds[2],0.f}),dz=std::max({p.bounds[1]-z,z-p.bounds[3],0.f});float blend=std::max(0.f,1-std::hypot(dx,dz)/8);if(blend>weight){weight=blend;padHeight=p.height;}}
        return value*(1-weight)+padHeight*weight;
    };
    struct Surface {Vec3 color;int layer;const RoadSurface *road=nullptr;std::array<float,2> origin{};bool pavedJunction=false;};
    auto surfaceAt=[&](const Tile& t,float x,float z,float halfX,float halfZ) -> Surface {
        const bool deck=facilityFloor&&std::abs(x)<=bounds[2]*.82f&&
            z>=bounds[1]*.82f&&z<=bounds[3]*.4f;
        int substrate=deck?terrainArt::InteriorDeck:groundLayer;
        Vec3 previous{};
        for(auto id:t.water)if(proceduralWaterEdge(world.waterRegions[id],x,z)>=0){
            substrate=terrainArt::Silt;previous={.11f,.15f,.12f};break;
        }

        // Keep the original vertex-colour fallback. For the material pass also
        // include cells that merely graze a road, then cut its actual edge in
        // the fragment shader. This avoids a 16m stair-step along a diagonal.
        bool originalRoad=false;
        const RoadSurface* chosen=nullptr;
        std::array<float,2> start{};
        const float cellRadius=std::hypot(halfX,halfZ);
        float nearest=cellRadius;
        unsigned overlapping=0;
        for(auto id:t.roads){
            const auto& feature=roads[id];
            if(!originalRoad&&contains(feature,x,z)){previous=feature.color;originalRoad=true;}
            if(!world.rpgPrototype||deck)continue;
            const auto& road=roadSurfaces[id];
            const float dx=x-feature.a[0],dz=z-feature.a[1];
            const float along=dx*road.alongX+dz*road.alongZ;
            const float across=dx*road.acrossX+dz*road.acrossZ;
            const float edge=std::max({std::abs(across),-along,along-road.length})-road.width*.5f;
            if(edge<cellRadius)++overlapping;
            if(edge<nearest){nearest=edge;chosen=&road;start=feature.a;}
        }
        if(!originalRoad&&substrate!=terrainArt::Silt)
            previous=color*(.85f+.2f*(amplitude>0?base(x,z)/amplitude:.5f));
        bool pavedJunction=chosen&&overlapping>1&&originalRoad;
        if(pavedJunction){
            // A cell straddling two connected segments can be entirely asphalt
            // even though neither segment alone covers its whole area.
            for(float xx:{x-halfX,x,x+halfX})for(float zz:{z-halfZ,z,z+halfZ}){
                if(pavedJunction&&!std::any_of(t.roads.begin(),t.roads.end(),[&](size_t id){
                    return contains(roads[id],xx,zz);
                }))pavedJunction=false;
            }
        }
        return {previous,substrate,chosen,start,pavedJunction};
    };
    auto point=[&](float x,float z){return Vec3{x,height(x,z),z};};
    world.editGeometry().terrain.reserve(tiles.size()*500);
    for(int z=0;z<nz;++z)for(int x=0;x<nx;++x){const auto& t=tiles[size_t(z*nx+x)];auto& xs=t.axes[0];auto& zs=t.axes[1];
        std::array<std::vector<float>,4> edges{xs,zs,xs,zs}; // bottom, right, top, left
        auto merge=[&](int edge,int xx,int zz,int axis){if(xx<0||xx>=nx||zz<0||zz>=nz)return;auto& v=tiles[size_t(zz*nx+xx)].axes[axis];edges[edge].insert(edges[edge].end(),v.begin(),v.end());tidy(edges[edge]);};
        merge(0,x,z-1,0);merge(1,x+1,z,1);merge(2,x,z+1,0);merge(3,x-1,z,1);
        merge(0,x,z-1,0);merge(1,x+1,z,1);merge(2,x,z+1,0);merge(3,x-1,z,1);
        World::TerrainPatch patch;patch.first=world.geometry().terrain.size();patch.low={t.bounds[0],1e9f,t.bounds[1]};patch.high={t.bounds[2],-1e9f,t.bounds[3]};
        auto vertex=[&](Vec3 p,const Surface& surface){
            patch.low.y=std::min(patch.low.y,p.y);
            patch.high.y=std::max(patch.high.y,p.y);
            WorldVertex vertex{p,surface.color};
            if(world.rpgPrototype){
                vertex.textured=terrainArt::textureMode;
                vertex.oneSided=float(surface.layer);
                if(surface.road){
                    // One carriageway across its actual width, repeat every 24m along its axis.
                    const auto &road=*surface.road;
                    vertex.textured=terrainArt::roadMode(surface.layer,road.layer);
                    vertex.oneSided=(surface.pavedJunction?-1.f:1.f)*(road.length+road.width)/24.f;
                    float x=p.x-surface.origin[0],z=p.z-surface.origin[1];
                    vertex.u=.5f+(x*road.acrossX+z*road.acrossZ)/road.width;
                    vertex.v=(x*road.alongX+z*road.alongZ+road.width*.5f)/24.f;
                }else{
                    vertex.u=p.x/32.f;
                    vertex.v=p.z/32.f;
                }
            }
            world.editGeometry().terrain.push_back(vertex);
        };
        for(size_t iz=0;iz+1<zs.size();++iz)for(size_t ix=0;ix+1<xs.size();++ix){
            float x0=xs[ix],x1=xs[ix+1],z0=zs[iz],z1=zs[iz+1];const auto c=surfaceAt(t,(x0+x1)/2,(z0+z1)/2,.5f*(x1-x0),.5f*(z1-z0));
            std::vector<Vec3> ring{point(x0,z0)};
            if(iz==0)for(float v:edges[0])if(v>x0&&v<x1)ring.push_back(point(v,z0));
            ring.push_back(point(x1,z0));
            if(ix+2==xs.size())for(float v:edges[1])if(v>z0&&v<z1)ring.push_back(point(x1,v));
            ring.push_back(point(x1,z1));
            if(iz+2==zs.size())for(auto it=edges[2].rbegin();it!=edges[2].rend();++it)if(*it>x0&&*it<x1)ring.push_back(point(*it,z1));
            ring.push_back(point(x0,z1));
            if(ix==0)for(auto it=edges[3].rbegin();it!=edges[3].rend();++it)if(*it>z0&&*it<z1)ring.push_back(point(x0,*it));
            if(ring.size()==4){for(size_t n:{size_t(0),size_t(1),size_t(2),size_t(0),size_t(2),size_t(3)})vertex(ring[n],c);}
            else {auto centre=point((x0+x1)/2,(z0+z1)/2);for(size_t n=0;n<ring.size();++n){vertex(centre,c);vertex(ring[n],c);vertex(ring[(n+1)%ring.size()],c);}}
        }
        patch.count=world.geometry().terrain.size()-patch.first;world.editGeometry().terrainPatches.push_back(patch);
        require(world.geometry().terrain.size()<=1800000,"600000 triangle budget exceeded");
    }
    for(const auto &w:world.waterRegions){auto r=w.bounds;Vec3 a{r[0],w.level,r[1]},b{r[2],w.level,r[1]},c{r[2],w.level,r[3]},d{r[0],w.level,r[3]};
        if(w.shore.empty())for(auto p:{a,c,b,a,d,c})world.editGeometry().waterMesh.push_back({p,{.035f,.24f,.42f}});
        else {Vec3 centre{(r[0]+r[2])/2,w.level,(r[1]+r[3])/2};
            for(size_t i=0;i<w.shore.size();++i){auto p=w.shore[i],q=w.shore[(i+1)%w.shore.size()];
                for(auto v:{centre,Vec3{q[0],w.level,q[1]},Vec3{p[0],w.level,p[1]}})world.editGeometry().waterMesh.push_back({v,{.035f,.24f,.42f}});}}
    }
    world.indexTerrain();
}
}
