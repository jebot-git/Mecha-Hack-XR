#include "rpg/original_assets.hpp"
#include "rpg/model_appearance.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <set>
#include <tuple>
using namespace iron;
using namespace iron::rpg;
using Json=nlohmann::json;
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int main(){try{
    const std::filesystem::path root=MECHA_ASSETS;
    setOriginalAssetRoot(root);
    const auto appearance=loadModelAppearance(root/"models");
    size_t checked=0,textured=0;
    for(const auto& entry:std::filesystem::directory_iterator(root/"meshes")){
        if(entry.path().extension()!=".json")continue;
        std::ifstream file(entry.path());Json j;file>>j;
        applyModelAppearance(j,appearance,j.at("id").get<std::string>(),1);
        auto mesh=originalMesh(j.at("id").get<std::string>());
        Target target;applyOriginalModel(target,j.at("id").get<std::string>());
        require(mesh->size()==j.at("faces").size()*3,"Native UV loader changed triangle membership");
        for(size_t f=0;f<j.at("faces").size();++f){
            const auto& face=j.at("faces")[f];
            bool has=face.contains("texture"),repeat=face.contains("repeat");
            for(size_t k=0;k<3;++k){
                const auto& vertex=mesh->at(f*3+k);
                require(vertex.textured==(repeat?3.f:has?2.f:0.f),"Texture source selector lost");
                if(has){
                    const auto &uv=repeat?face.at("repeat").at("uv"):face.at("uv");
                    require(vertex.u==uv[k][0].get<float>()&&vertex.v==uv[k][1].get<float>(),"Native UV changed at load");
                    if(repeat){int tile=face.at("repeat").at("tile");require(vertex.oneSided==float(tile%8+8*(7-tile/8)),"Repeated facade addresses the wrong atlas row");}
                    ++textured;
                }
            }
        }
        require(target.wreck->size()/3<=j.at("budget").get<size_t>(),"Wreck exceeds original model triangle budget");
        std::set<std::tuple<float,float,float,float>> surfaces;
        for(const auto &v:*mesh)surfaces.emplace(v.u,v.v,v.textured,v.oneSided);
        for(const auto &v:*target.wreck)require(surfaces.count({v.u,v.v,v.textured,v.oneSided})!=0,"Wreck lost its source material/UV membership");
        ++checked;
    }
    require(textured>0,"No textured production triangles");
    std::ifstream manifestFile(root/"models/manifest.json");Json manifest;manifestFile>>manifest;
    for(const auto& [name,record]:manifest.at("models").items()){
        std::ifstream aliasFile(root/"models"/record.at("file").get<std::string>());
        auto nativePath=root/"meshes"/(record.at("original_id").get<std::string>()+".json");
        // Preserve historical IS material/UV membership independently of the
        // redesigned RPG striker that once shared its original_id alias.
        if(name=="iron-soldier"&&std::filesystem::exists(root/"mecha-redesign-budgets.json")){
            require(record.at("file")=="iron-soldier.json"&&record.at("original_id")=="chassis_striker"&&record.at("triangles")==248&&record.at("budget")==376,"Original IS texture alias routing/count/cap changed");
            nativePath=root/"provenance/mecha-redesign-baseline/meshes/chassis_striker.json";
        }
        std::ifstream nativeFile(nativePath);
        Json alias,native;aliasFile>>alias;nativeFile>>native;
        require(alias.at("faces").size()==native.at("faces").size(),"GPU alias lost triangle membership");
        for(size_t i=0;i<alias.at("faces").size();++i){
            const auto &a=alias.at("faces")[i],&n=native.at("faces")[i];
            require(a.value("texture","")==n.value("texture",""),"GPU alias uses different material");
            if(n.contains("texture"))require(a.at("uv")==n.at("uv"),"GPU alias UV differs from native world mesh");
        }
    }
    // Reject malformed data before it can reach GPU buffers. Use an isolated
    // temporary pack and restore the global cache on every exit.
    auto temp=std::filesystem::temp_directory_path()/("mecha-uv-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp/"meshes");
    struct Cleanup{std::filesystem::path temp,root;~Cleanup(){setOriginalAssetRoot(root);std::filesystem::remove_all(temp);}} cleanup{temp,root};
    Json fixture={{"schema",1},{"units","metres"},{"budget",1},{"vertices",{{0,0,0},{1,0,0},{0,1,0}}},
                  {"faces",{{{"indices",{0,1,2}},{"color",{255,255,255}},{"texture","psx"},{"uv",{{0,0},{1,0},{0,1}}}}}}};
    for(int invalid=0;invalid<9;++invalid){
        auto broken=fixture;
        if(invalid==0)broken["faces"][0]["texture"]="other";
        if(invalid==1)broken["faces"][0]["uv"][0][0]=1.1;
        if(invalid==2)broken["faces"][0]["uv"]=Json::array({{0,0}});
        if(invalid==3)broken["faces"][0].erase("uv");
        if(invalid>=4)broken["faces"][0]["repeat"]={{"tile",56},{"uv",{{0,0},{8,0},{0,20}}}};
        if(invalid==4)broken["faces"][0]["repeat"]["tile"]=64;
        if(invalid==5)broken["faces"][0]["repeat"]["uv"][0][0]=513;
        if(invalid==6)broken["faces"][0]["repeat"]["tile"]=1.5;
        if(invalid==7)broken["faces"][0].erase("texture");
        if(invalid==8)broken["faces"][0]["repeat"]["uv"]={{0,0}};
        std::ofstream(temp/"meshes/bad.json")<<broken;
        setOriginalAssetRoot(temp);bool rejected=false;
        try{originalMesh("bad");}catch(const std::exception&){rejected=true;}
        require(rejected,"Malformed texture metadata accepted");
    }
    std::cout<<checked<<" textured mesh loaders checked, "<<textured<<" UV vertices; malformed metadata rejected\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
