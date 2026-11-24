#include "rpg/profiles.hpp"
#include <fstream>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#endif
namespace iron::rpg {
ProfileStore::ProfileStore(std::filesystem::path directory):root(std::move(directory)){
    std::filesystem::create_directories(root);
    auto index=root/"index.json";
    if(std::filesystem::exists(index)){
        std::ifstream file(index);Json j;file>>j;
        if(j.at("schema")!=1)throw std::runtime_error("Unsupported pilot index");
        selected=j.at("active");if(selected<0||selected>=pilotProfileLimit)throw std::runtime_error("Invalid pilot slot");
    }
    // Import the old one-profile prototype without removing or rewriting it.
    if(!std::filesystem::exists(path(0))&&std::filesystem::exists(root/"default.json")){
        std::string name;auto sheet=loadProfile(root/"default.json",&name);saveProfile(path(0),sheet,name);
    }
}
std::filesystem::path ProfileStore::path(int slot)const{
    if(slot<0||slot>=pilotProfileLimit)throw std::runtime_error("Pilot slot must be 1..4");
    return root/("pilot-"+std::to_string(slot+1)+".json");
}
std::optional<PilotProfile> ProfileStore::load(int slot)const{
    auto file=path(slot);if(!std::filesystem::exists(file))return {};
    PilotProfile profile;profile.sheet=loadProfile(file,&profile.callsign);return profile;
}
void ProfileStore::writeIndex()const{
    auto temp=root/("index.tmp."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {std::ofstream file(temp);file<<Json{{"schema",1},{"active",selected}}.dump(2)<<'\n';file.flush();if(!file)throw std::runtime_error("Cannot save active pilot");}
#ifdef _WIN32
    auto destination=root/"index.json";
    if(!MoveFileExW(temp.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace active pilot index");
#else
    std::filesystem::rename(temp,root/"index.json");
#endif
}
void ProfileStore::select(int slot){path(slot);selected=slot;writeIndex();}
void ProfileStore::save(int slot,const Sheet &sheet,const std::string &callsign){saveProfile(path(slot),sheet,callsign);}
void ProfileStore::reset(int slot,Dice &dice){
    // Replace atomically so startup cannot reimport a legacy default.json for slot one.
    save(slot,createSheet(Chassis::Striker,PilotKind::Engineer,dice),"Pilot "+std::to_string(slot+1));
}
std::vector<std::string> ProfileStore::lines(int cursor,std::optional<PilotProfile> *highlighted)const{
    std::vector<std::string> result{"PILOTS"};
    std::optional<PilotProfile> selectedPilot;
    for(int slot=0;slot<pilotProfileLimit;++slot){
        auto pilot=load(slot);
        std::string text=std::to_string(slot+1)+". ";
        if(pilot)text+=pilot->callsign+" / "+(pilot->sheet.created?serviceTitle(pilot->sheet):"CREATION");
        else text+="NEW PILOT";
        result.push_back(std::string(cursor==slot?"> ":"  ")+text);
        if(cursor==slot)selectedPilot=std::move(pilot);
    }
    if(selectedPilot){
        const auto &s=selectedPilot->sheet;
        result.push_back(std::to_string(s.missionsCompleted)+" CONTRACTS / "+"CAMPAIGN WINS "+std::to_string(s.campaignAwards));
        const char *names[]={"PRIMACY","TERRAN","DOMINION"};
        for(size_t f=0;f<s.career.standings.size();++f){
            const int value=s.career.standings[f];
            std::string standing=std::string(names[f])+" "+(value>0?"+":"")+std::to_string(value);
            result.push_back(standing);
            if(s.career.heroFactions&(1u<<f))result.push_back(factionHonour(f));
        }
        if(s.missionsCompleted<5)result.push_back("VETERAN AT 5 CONTRACTS");
        else if(s.career.heroFactions!=7)result.push_back("FACTION HONOURS AT +12");
    }
    if(highlighted)*highlighted=std::move(selectedPilot);
    return result;
}
}
