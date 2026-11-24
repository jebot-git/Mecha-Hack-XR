#include "simulation.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
using namespace iron;using Json=nlohmann::json;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static Json recipe(){return Json::parse(R"({
"schema":"iron-procedural-layout","version":4,"seed":42,"units":"metres","plane":"xz","ground_y":0,"pilot_radius":3.5,
"bounds":[-2200,-100,2200,100],"nodes":[[-2100,0],[-1900,0],[1900,0],[2100,0]],
"roads":[{"nodes":[0,1],"width":40,"segment":0},{"nodes":[1,2],"width":40,"connection":0},{"nodes":[2,3],"width":40,"segment":1}],
"districts":[{"id":0,"role":"storage","sources":["is1"]}],
"plots":[{"id":0,"bounds":[-2080,40,-2000,80],"district":0,"role":"storage","sources":["is1"],"buildings":1,"segment":0},
         {"id":1,"bounds":[2000,40,2080,80],"district":0,"role":"storage","sources":["is1"],"buildings":1,"segment":1}],
"spawn":{"node":0,"radius":8},"objectives":[{"node":1,"radius":8,"kind":"recon","segment":0},{"node":2,"radius":8,"kind":"recon","segment":1}],
"extraction":{"node":3,"radius":8},"mission":{"kind":"recon","hold_seconds":1,"extraction":true,"ordered_segments":true},
"terrain":{"biome":"hills","amplitude":6,"water":[{"bounds":[-50,-60,50,60],"level":-1,"depth":6}]},"encounters":{"ground_per_zone":1,"air_units":0},
"segments":[{"id":0,"bounds":[-2200,-100,-1800,100],"node_range":[0,2],"plot_range":[0,1],"objectives":[0]},
            {"id":1,"bounds":[1800,-100,2200,100],"node_range":[2,2],"plot_range":[1,1],"objectives":[1]}],
"connections":[{"from_segment":0,"to_segment":1,"nodes":[1,2]}]
})");}
int main(){try{
    auto j=recipe();validateProceduralRecipe(j);
    for(int mode=0;mode<7;++mode){auto bad=j;
        if(mode==0)bad["segments"][1]["node_range"]={0,2};
        if(mode==1)bad["connections"][0]["nodes"]={0,2};
        if(mode==2)bad["roads"][0]["connection"]=0;
        if(mode==3)bad["objectives"][1]["segment"]=0;
        if(mode==4)bad["extraction"]["node"]=0;
        if(mode==5)bad["segments"][0]["plot_range"]={0,999};
        if(mode==6)bad["version"]=3;
        bool rejected=false;try{validateProceduralRecipe(bad);}catch(const std::exception&){rejected=true;}check(rejected,"Malformed sector ownership/bounds accepted");
    }
    World w;w.targets.clear();w.missionStatus=MissionStatus::Active;w.mission=0;w.prototypeSeed=42;w.proceduralLayout=true;w.movementBounds=j.at("bounds").get<std::array<float,4>>();buildProceduralTerrain(w,j);
    check(w.geometry().terrainPatches.size()>1&&w.geometry().terrain.size()%3==0,"Missing tiled mesh");
    double area=0;size_t next=0;
    for(auto& p:w.geometry().terrainPatches){check(p.first==next&&p.count%3==0,"Overlapping/missing patch ranges");next+=p.count;}
    check(next==w.geometry().terrain.size(),"Patch range coverage");
    for(size_t n=0;n<w.geometry().terrain.size();n+=3){auto a=w.geometry().terrain[n].position,b=w.geometry().terrain[n+1].position,c=w.geometry().terrain[n+2].position;double twice=double(b.x-a.x)*(c.z-a.z)-double(b.z-a.z)*(c.x-a.x);check(twice>0,"Degenerate/reversed terrain triangle");area+=twice*.5;}
    check(std::abs(area-4400*200)<1,"Terrain area has holes/overlaps");
    w.position={-2100,w.surfaceHeight(-2100,0),0};
    for(int x=-2099;x<=2100;++x){w.movePilot({float(x),0,0});check(std::abs(w.position.x-x)<.01f,"Pilot blocked by old world limit or road/water seam");}
    w.sounds.clear();w.procedural.segmentCount=2;w.procedural.orderedSegments=true;w.procedural.requireExtraction=true;w.procedural.extraction=w.position;
    for(float x:{-1900.f,1900.f}){ProceduralObjective o;o.segment=x<0?0:1;o.kind=ProceduralGoalKind::Recon;o.position={x,w.surfaceHeight(x,0),0};o.holdSeconds=1;w.procedural.objectives.push_back(o);}
    w.position=w.procedural.objectives[1].position;for(int n=0;n<12;++n)updateProceduralMission(w,.1f);check(w.procedural.objectives[1].progress==0,"Later sector bypassed ordered progression");
    for(auto& o:w.procedural.objectives){w.position=o.position;for(int n=0;n<12;++n)updateProceduralMission(w,.1f);check(o.complete,"Sector recon did not finish");}
    check(w.procedural.extracting&&w.missionStatus==MissionStatus::Active,"Extraction was not gated");w.position=w.procedural.extraction;updateProceduralMission(w,0);check(w.missionStatus==MissionStatus::Complete,"Final-sector extraction did not finish");
    w.missionStatus=MissionStatus::Active;w.procedural.orderedSegments=false;for(auto& o:w.procedural.objectives){o.complete=false;o.progress=0;}
    w.position=w.procedural.objectives[1].position;for(int n=0;n<12;++n)updateProceduralMission(w,.1f);check(w.procedural.objectives[1].complete&&!w.procedural.objectives[0].complete,"Free sector order did not work");
    std::cout<<"Segmented terrain, movement, validation, staged and free-order objectives passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
