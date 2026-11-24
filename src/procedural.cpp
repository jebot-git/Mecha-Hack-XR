#include "simulation.hpp"
#include "procedural_terrain.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>

namespace iron {
namespace {
using Json=nlohmann::json;
using Rect=std::array<double,4>;
void require(bool value,const char* message){if(!value)throw std::runtime_error(std::string("Invalid procedural recipe: ")+message);}
bool number(const Json& value){return value.is_number()&&std::isfinite(value.get<double>());}
bool integer(const Json& value,int64_t minimum,int64_t maximum){return value.is_number_integer()&&value.get<double>()>=double(minimum)&&value.get<double>()<=double(maximum);}
bool numbers(const Json& value,size_t count){return value.is_array()&&value.size()==count&&std::all_of(value.begin(),value.end(),number);}
bool rect(const Json& value){return numbers(value,4)&&value[0].get<double>()<value[2].get<double>()&&value[1].get<double>()<value[3].get<double>();}
bool overlap(Rect a,Rect b){return a[0]<b[2]&&b[0]<a[2]&&a[1]<b[3]&&b[1]<a[3];}
bool role(const Json& value){return value=="industrial"||value=="storage"||value=="urban";}
bool sources(const Json& value){if(!value.is_array()||value.empty()||value.size()>2)return false;std::set<std::string> seen;for(auto& source:value){if(source!="is1"&&source!="is2")return false;if(!seen.insert(source.get<std::string>()).second)return false;}return true;}
}
void validateProceduralRecipe(const Json& j){
    require(j.is_object(),"expected an object");
    require(j.contains("schema")&&j.at("schema")=="iron-procedural-layout","unsupported schema");
    require(j.contains("version")&&integer(j.at("version"),1,4),"unsupported version");int version=j.at("version");double limit=version==4?8192:1000;
    require(j.contains("seed")&&integer(j.at("seed"),0,UINT32_MAX),"seed must be uint32");
    require(j.contains("units")&&j.at("units")=="metres"&&j.contains("plane")&&j.at("plane")=="xz"&&j.contains("ground_y")&&number(j.at("ground_y"))&&j.at("ground_y")==0,"expected flat native metres");
    require(j.contains("pilot_radius")&&number(j.at("pilot_radius"))&&j.at("pilot_radius")==3.5,"unexpected pilot clearance");
    require(j.contains("bounds")&&rect(j.at("bounds")),"invalid world bounds");auto bounds=j.at("bounds").get<Rect>();
    require(*std::min_element(bounds.begin(),bounds.end())>=-limit&&*std::max_element(bounds.begin(),bounds.end())<=limit,"world bounds exceed movement limits");
    auto inside=[&](Rect r){return bounds[0]<=r[0]&&bounds[1]<=r[1]&&r[2]<=bounds[2]&&r[3]<=bounds[3];};
    require(j.contains("nodes")&&j.at("nodes").is_array()&&j.at("nodes").size()>=2&&j.at("nodes").size()<=size_t(version==4?768:81),"node budget exceeded");
    auto& nodes=j.at("nodes");std::set<std::array<double,2>> distinct;
    for(auto& n:nodes){require(numbers(n,2),"invalid node coordinate");auto p=n.get<std::array<double,2>>();require(std::abs(p[0])<=(version==4?limit-32:950)&&std::abs(p[1])<=(version==4?limit-32:950),"node outside runtime bounds");require(distinct.insert(p).second,"duplicate node");}
    auto nodeIndex=[&](const Json& n){return integer(n,0,int64_t(nodes.size())-1);};
    require(j.contains("roads")&&j.at("roads").is_array()&&!j.at("roads").empty()&&j.at("roads").size()<=size_t(version==4?1536:144),"road budget exceeded");
    require(j.contains("plots")&&j.at("plots").is_array()&&j.at("plots").size()<=size_t(version==4?512:64),"plot budget exceeded");
    std::vector<std::set<int>> adjacency(nodes.size());std::vector<Rect> roads,plots;
    for(auto& road:j.at("roads")){
        require(road.is_object()&&road.contains("nodes")&&road.at("nodes").is_array()&&road.at("nodes").size()==2&&nodeIndex(road.at("nodes")[0])&&nodeIndex(road.at("nodes")[1]),"invalid road endpoints");
        int a=road.at("nodes")[0],b=road.at("nodes")[1];require(a!=b&&!adjacency[a].contains(b),"duplicate or zero-length road");
        require(road.contains("width")&&number(road.at("width")),"invalid road width");double width=road.at("width");require(width>=16&&width<=60,"road width outside runtime limits");
        auto pa=nodes[a].get<std::array<double,2>>(),pb=nodes[b].get<std::array<double,2>>();require(pa[0]==pb[0]||pa[1]==pb[1],"roads must be orthogonal");
        Rect box{std::min(pa[0],pb[0])-width/2,std::min(pa[1],pb[1])-width/2,std::max(pa[0],pb[0])+width/2,std::max(pa[1],pb[1])+width/2};require(inside(box),"road outside world bounds");
        if(road.contains("class"))require(road.at("class")=="arterial"||road.at("class")=="local","invalid road class");
        roads.push_back(box);adjacency[a].insert(b);adjacency[b].insert(a);
    }
    std::set<int> districts;std::map<int,Json> districtDefinitions;
    if(j.contains("districts")){
        require(j.at("districts").is_array()&&!j.at("districts").empty()&&j.at("districts").size()<=64,"invalid districts");
        for(auto& district:j.at("districts")){require(district.is_object()&&district.contains("id")&&integer(district.at("id"),0,63)&&district.contains("role")&&role(district.at("role"))&&district.contains("sources")&&sources(district.at("sources")),"invalid district");int id=district.at("id");require(districts.insert(id).second,"duplicate district");districtDefinitions[id]=district;}
    }
    for(size_t i=0;i<j.at("plots").size();++i){auto& plot=j.at("plots")[i];
        require(plot.is_object()&&plot.contains("bounds")&&rect(plot.at("bounds")),"invalid plot bounds");auto box=plot.at("bounds").get<Rect>();
        require(box[2]-box[0]>=8&&box[3]-box[1]>=8&&inside(box)&&*std::min_element(box.begin(),box.end())>=-(version==4?limit-8:980)&&*std::max_element(box.begin(),box.end())<=(version==4?limit-8:980),"plot outside bounds or too small");
        for(auto road:roads)require(!overlap(box,road),"plot overlaps a road");
        for(auto other:plots)require(!overlap(box,other),"plots overlap");
        plots.push_back(box);
        if(version>=2||plot.contains("id"))require(plot.contains("id")&&integer(plot.at("id"),int64_t(i),int64_t(i)),"plot id must match its index");
        if(version>=2||plot.contains("sources"))require(plot.contains("sources")&&sources(plot.at("sources")),"invalid plot sources");
        if(version>=2||plot.contains("role"))require(plot.contains("role")&&role(plot.at("role")),"invalid plot role");
        if(plot.contains("district")){
            require(integer(plot.at("district"),0,63)&&districts.contains(plot.at("district").get<int>()),"invalid plot district");
            auto& district=districtDefinitions.at(plot.at("district").get<int>());
            require(plot.contains("role")&&plot.at("role")==district.at("role")&&plot.contains("sources"),"plot differs from district preferences");
            std::set<std::string> expected,actual;for(auto& source:district.at("sources"))expected.insert(source.get<std::string>());for(auto& source:plot.at("sources"))actual.insert(source.get<std::string>());
            require(expected==actual,"plot differs from district sources");
        }
    }
    std::string kind="eliminate";bool extraction=false;
    if(version>=2){
        require(j.contains("mission")&&j.at("mission").is_object(),"missing mission specification");auto& mission=j.at("mission");
        require(mission.contains("kind")&&(mission.at("kind")=="eliminate"||mission.at("kind")=="strike"||mission.at("kind")=="recon"),"invalid mission kind");kind=mission.at("kind");
        require(mission.contains("extraction")&&mission.at("extraction").is_boolean(),"invalid extraction policy");extraction=mission.at("extraction");
        require(mission.contains("hold_seconds")&&number(mission.at("hold_seconds"))&&mission.at("hold_seconds").get<double>()>=1&&mission.at("hold_seconds").get<double>()<=30,"invalid recon hold duration");
        require(j.contains("extraction")==extraction,"extraction zone and policy disagree");
    }
    require(j.contains("spawn")&&j.at("spawn").is_object()&&j.contains("objectives")&&j.at("objectives").is_array()&&!j.at("objectives").empty()&&j.at("objectives").size()<=8,"invalid mission zones");
    std::set<int> zoneNodes,strikePlots;std::vector<std::array<double,3>> discs;
    auto zone=[&](const Json& value,bool isExtraction){
        require(value.is_object()&&value.contains("node")&&nodeIndex(value.at("node"))&&value.contains("radius")&&number(value.at("radius")),"invalid zone");
        int n=value.at("node");double radius=value.at("radius");require(radius>=3.5&&radius<=30,"zone radius outside runtime limits");
        require(isExtraction||zoneNodes.insert(n).second,"mission zones share a node");auto p=nodes[n].get<std::array<double,2>>();
        for(size_t n=isExtraction?1:0;n<discs.size();++n){auto other=discs[n];require(std::hypot(p[0]-other[0],p[1]-other[1])>=radius+other[2],"mission zones overlap");}
        Rect box{p[0]-radius,p[1]-radius,p[0]+radius,p[1]+radius};require(inside(box),"zone outside bounds");for(auto plot:plots)require(!overlap(box,plot),"zone blocked by a plot");
        require(std::any_of(roads.begin(),roads.end(),[&](Rect road){return road[0]<=box[0]&&road[1]<=box[1]&&box[2]<=road[2]&&box[3]<=road[3];}),"zone outside a road corridor");
        if(!isExtraction)discs.push_back({p[0],p[1],radius});
    };
    zone(j.at("spawn"),false);
    for(auto& objective:j.at("objectives")){
        zone(objective,false);std::string expected=kind=="eliminate"?"encounter":kind;
        require(objective.contains("kind")&&objective.at("kind")==expected,"objective does not match mission kind");
        if(kind=="strike"){
            require(objective.contains("plot")&&integer(objective.at("plot"),0,int64_t(plots.size())-1),"invalid strike plot");int plot=objective.at("plot");require(strikePlots.insert(plot).second,"strike objectives share a plot");
            auto point=nodes[objective.at("node").get<int>()].get<std::array<double,2>>();auto box=plots[plot];
            double dx=std::max({box[0]-point[0],point[0]-box[2],0.}),dz=std::max({box[1]-point[1],point[1]-box[3],0.});
            require(dx>0&&dx<=(version==4?80:40)&&dz>0&&dz<=(version==4?80:40),"strike plot is not adjacent to its objective node");
        }
    }
    if(extraction){zone(j.at("extraction"),true);require(version==4||j.at("extraction").at("node")==j.at("spawn").at("node"),"extraction must use the spawn node");}
    std::vector<bool> reached(nodes.size());std::queue<int> frontier;int start=j.at("spawn").at("node");reached[start]=true;frontier.push(start);size_t count=1;
    while(!frontier.empty()){int n=frontier.front();frontier.pop();for(int other:adjacency[n])if(!reached[other]){reached[other]=true;frontier.push(other);++count;}}
    require(count==nodes.size(),"road network is disconnected");
    if(j.contains("environment"))readEnvironment(j.at("environment"));
    validateProceduralReinforcements(j);
    if(version>=3)validateProceduralTerrain(j);
    if(version==4)validateProceduralSegments(j);
}

void updateProceduralMission(World& world,float dt){
    if(!world.proceduralLayout||world.missionStatus!=MissionStatus::Active||world.lifeSpent||world.health<=0)return;
    auto& mission=world.procedural;if(mission.objectives.empty())return;
    if(!std::isfinite(dt)||dt<0)dt=0;
    dt=std::min(dt,.1f);
    int activeSegment=mission.segmentCount;
    for(const auto& o:mission.objectives)if(!o.complete)activeSegment=std::min(activeSegment,o.segment);
    size_t completed=0;ProceduralObjective* next=nullptr;
    for(auto& objective:mission.objectives){
        if(!objective.complete&&(!mission.orderedSegments||objective.segment==activeSegment)){
            if(objective.kind==ProceduralGoalKind::Recon){
                float distance=std::hypot(world.position.x-objective.position.x,world.position.z-objective.position.z);
                if(distance<=objective.radius&&std::abs(world.position.y-objective.position.y)<2)objective.progress=std::min(objective.holdSeconds,objective.progress+dt);else objective.progress=0;
                objective.complete=objective.progress>=objective.holdSeconds;
            }else if(objective.kind==ProceduralGoalKind::Eliminate&&objective.killQuota)objective.complete=objective.kills>=objective.killQuota;
            else if(!objective.targets.empty())objective.complete=(objective.kind!=ProceduralGoalKind::Eliminate||std::none_of(world.generators.begin(),world.generators.end(),[&](const auto& g){return g.prototype.proceduralZone==int(&objective-mission.objectives.data())&&g.remaining>0;}))&&std::all_of(objective.targets.begin(),objective.targets.end(),[&](size_t index){return index<world.targets.size()&&world.targets[index].health<=0;});
        }
        if(objective.complete){++completed;for(auto& g:world.generators)if(g.prototype.proceduralZone==int(&objective-mission.objectives.data()))g.remaining=0;}
        else if(!next)next=&objective;
    }
    mission.extracting=completed==mission.objectives.size()&&mission.requireExtraction;
    if(completed==mission.objectives.size()){
        bool inExtraction=!mission.requireExtraction||(std::hypot(world.position.x-mission.extraction.x,world.position.z-mission.extraction.z)<=mission.extractionRadius&&std::abs(world.position.y-mission.extraction.y)<2);
        if(inExtraction&&!world.destructionPending()){world.objective="PROCEDURAL / MISSION COMPLETE";world.missionStatus=MissionStatus::Complete;}
        else if(mission.requireExtraction)world.objective="EXTRACT / "+std::to_string(int(std::hypot(world.position.x-mission.extraction.x,world.position.z-mission.extraction.z)))+" M";
        else world.objective="OBJECTIVES COMPLETE / WAIT FOR DEBRIS";
        return;
    }
    if(!next)return;
    auto distance=std::to_string(int(std::hypot(world.position.x-next->position.x,world.position.z-next->position.z)));
    std::string progress=std::to_string(completed)+"/"+std::to_string(mission.objectives.size());
    if(next->kind==ProceduralGoalKind::Recon)world.objective="RECON "+progress+" / HOLD "+std::to_string(int(next->progress))+"/"+std::to_string(int(next->holdSeconds))+" S / "+distance+" M";
    else {size_t remaining=0;for(auto id:next->targets)if(id<world.targets.size()&&world.targets[id].health>0)++remaining;
        world.objective=std::string(next->kind==ProceduralGoalKind::Strike?"STRIKE ":"ELIMINATE ")+progress+" / "+std::to_string(remaining)+(next->kind==ProceduralGoalKind::Strike?" STRUCTURES / ":" HOSTILES / ")+distance+" M";
        if(next->kind==ProceduralGoalKind::Eliminate&&next->killQuota)world.objective="ELIMINATE "+progress+" / KILLS "+std::to_string(next->kills)+"/"+std::to_string(next->killQuota)+" / "+distance+" M / REINFORCEMENTS";
        if(next->kind==ProceduralGoalKind::Eliminate&&!next->killQuota){unsigned reserves=0;for(const auto& g:world.generators)if(g.prototype.proceduralZone==int(next-mission.objectives.data()))reserves+=g.remaining;
            if(reserves){world.objective+=" / RESERVES "+std::to_string(reserves);if(!remaining)world.objective+=" / CLEAR R POINT";}}
    }
    if(mission.segmentCount>1)world.objective="SECTOR "+std::to_string(next->segment+1)+"/"+std::to_string(mission.segmentCount)+" / "+world.objective;
}
}
