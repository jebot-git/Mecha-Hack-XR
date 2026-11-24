#include "procedural.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <cmath>
#include <set>
#include <map>
#include <algorithm>
#include <stdexcept>
namespace iron {
namespace {
using Json=nlohmann::json;using Rect=std::array<double,4>;
void require(bool value,const char* message){if(!value)throw std::runtime_error(std::string("Invalid segmented layout: ")+message);}
bool index(const Json& j,size_t size){return j.is_number_integer()&&j.get<double>()>=0&&j.get<double>()<double(size);}
bool inside(double x,double z,Rect r){return x>=r[0]&&z>=r[1]&&x<=r[2]&&z<=r[3];}
}
// Called after the common recipe validator has checked roads, parcels and zones.
void validateProceduralSegments(const Json& recipe){
    require(recipe.contains("segments")&&recipe.at("segments").is_array()&&!recipe.at("segments").empty()&&recipe.at("segments").size()<=8,"segment count");
    require(recipe.contains("connections")&&recipe.at("connections").is_array()&&recipe.at("connections").size()<=16,"connection count");
    require(recipe.at("mission").contains("ordered_segments")&&recipe.at("mission").at("ordered_segments").is_boolean(),"ordered policy");
    const auto& segments=recipe.at("segments");const auto& nodes=recipe.at("nodes");const auto& plots=recipe.at("plots");const auto& objectives=recipe.at("objectives");
    auto bounds=recipe.at("bounds").get<Rect>();std::vector<Rect> rectangles;
    std::vector<int> owners(nodes.size(),-1),plotOwners(plots.size(),-1),goalOwners(objectives.size(),-1);
    for(size_t i=0;i<segments.size();++i){const auto& s=segments[i];
        require(s.is_object()&&s.contains("id")&&index(s.at("id"),segments.size())&&s.at("id").get<size_t>()==i,"segment id");
        require(s.contains("bounds")&&s.at("bounds").is_array()&&s.at("bounds").size()==4,"segment bounds");
        for(auto& v:s.at("bounds"))require(v.is_number()&&std::isfinite(v.get<double>()),"nonfinite bounds");
        auto b=s.at("bounds").get<Rect>();
        require(b[0]<b[2]&&b[1]<b[3]&&inside(b[0],b[1],bounds)&&inside(b[2],b[3],bounds),"sector outside world");
        for(auto p:rectangles)require(!(b[0]<p[2]&&p[0]<b[2]&&b[1]<p[3]&&p[1]<b[3]),"overlapping sectors");
        rectangles.push_back(b);
        for(bool parcel:{false,true}){const char* field=parcel?"plot_range":"node_range";auto& ownership=parcel?plotOwners:owners;
            require(s.contains(field)&&s.at(field).is_array()&&s.at(field).size()==2,"missing ownership range");const auto& r=s.at(field);
            require(index(r[0],ownership.size())&&r[1].is_number_integer()&&r[1].get<double>()>=1&&r[1].get<double>()<=(parcel?64:81)&&r[0].get<double>()+r[1].get<double>()<=double(ownership.size()),"range outside budget");
            size_t start=r[0],count=r[1];for(size_t n=start;n<start+count;++n){require(ownership[n]==-1,"duplicate ownership");ownership[n]=int(i);
                if(parcel){auto p=plots[n].at("bounds").get<Rect>();require(inside(p[0],p[1],b)&&inside(p[2],p[3],b)&&plots[n].contains("segment")&&index(plots[n].at("segment"),segments.size())&&plots[n].at("segment").get<size_t>()==i,"parcel outside sector");}
                else require(inside(nodes[n][0],nodes[n][1],b),"node outside sector");
            }
        }
        require(s.contains("objectives")&&s.at("objectives").is_array()&&!s.at("objectives").empty(),"sector has no objective");
        for(const auto& value:s.at("objectives")){require(index(value,objectives.size()),"objective index");size_t n=value;require(goalOwners[n]==-1,"duplicate objective owner");goalOwners[n]=int(i);
            require(objectives[n].contains("segment")&&index(objectives[n].at("segment"),segments.size())&&objectives[n].at("segment").get<size_t>()==i&&owners.at(objectives[n].at("node").get<size_t>())==int(i),"objective outside sector");
            if(objectives[n].contains("plot"))require(plotOwners.at(objectives[n].at("plot").get<size_t>())==int(i),"strike parcel outside sector");}
    }
    for(int owner:plotOwners)require(owner>=0,"unowned plot");
    for(int owner:goalOwners)require(owner>=0,"unowned objective");
    require(owners.at(recipe.at("spawn").at("node").get<size_t>())==0,"insertion outside first sector");
    if(recipe.at("mission").at("extraction").get<bool>())require(owners.at(recipe.at("extraction").at("node").get<size_t>())==int(segments.size()-1),"extraction outside final sector");
    if(recipe.at("mission").at("ordered_segments").get<bool>())for(size_t n=1;n<goalOwners.size();++n)require(goalOwners[n]>=goalOwners[n-1],"objectives out of sector order");
    std::set<std::pair<int,int>> pairs,expectedEdges;std::set<int> via;std::vector<std::set<int>> graph(segments.size());
    auto edge=[](int a,int b){return std::pair<int,int>{std::min(a,b),std::max(a,b)};};
    std::map<std::pair<int,int>,const Json*> roads;
    for(const auto& r:recipe.at("roads"))roads[edge(r.at("nodes")[0],r.at("nodes")[1])]=&r;
    const auto& connections=recipe.at("connections");
    for(size_t i=0;i<connections.size();++i){const auto& c=connections[i];
        require(c.is_object()&&c.contains("from_segment")&&c.contains("to_segment")&&index(c.at("from_segment"),segments.size())&&index(c.at("to_segment"),segments.size()),"connection sectors");int a=c.at("from_segment"),b=c.at("to_segment");
        require(a!=b&&pairs.insert(edge(a,b)).second,"duplicate sector link");graph[a].insert(b);graph[b].insert(a);
        require(c.contains("nodes")&&c.at("nodes").is_array()&&c.at("nodes").size()>=2&&c.at("nodes").size()<=4,"connection path");
        const auto& path=c.at("nodes");for(auto& n:path)require(index(n,nodes.size()),"connection node");
        require(owners[path.front().get<size_t>()]==a&&owners[path.back().get<size_t>()]==b,"connection endpoints");
        for(size_t n=1;n+1<path.size();++n){int id=path[n];require(owners[id]==-1,"connector crosses sector");via.insert(id);}
        for(size_t n=1;n<path.size();++n){auto e=edge(path[n-1],path[n]);require(expectedEdges.insert(e).second&&roads.contains(e),"missing/duplicate connector road");auto& r=*roads.at(e);
            require(r.contains("connection")&&index(r.at("connection"),connections.size())&&r.at("connection").get<size_t>()==i&&!r.contains("segment"),"connector road ownership");}
    }
    for(size_t n=0;n<owners.size();++n)require(owners[n]>=0||via.contains(int(n)),"unowned node");
    for(const auto& [e,p]:roads){const auto& r=*p;
        if(r.contains("connection"))require(expectedEdges.contains(e),"extra connector road");
        else require(owners[e.first]>=0&&owners[e.first]==owners[e.second]&&r.contains("segment")&&index(r.at("segment"),segments.size())&&r.at("segment").get<int>()==owners[e.first],"road crosses sector without connector");
    }
    std::set<int> reached{0};std::vector<int> pending{0};while(!pending.empty()){int n=pending.back();pending.pop_back();for(int other:graph[n])if(reached.insert(other).second)pending.push_back(other);}
    require(reached.size()==segments.size(),"disconnected sector graph");
}
}
