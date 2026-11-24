#include "rpg/mission.hpp"
#include "rpg/bosses.hpp"
#include "rpg/manual_missions.hpp"
#include "rpg/mission_intro.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <set>
#include <vector>

using namespace iron::rpg;
namespace fs=std::filesystem;

static int frames(const fs::path &file){
    std::ifstream stream(file,std::ios::binary);
    std::array<uint8_t,8> h{};
    assert(stream.read(reinterpret_cast<char*>(h.data()),h.size()));
    assert(h[0]=='M'&&h[1]=='H'&&h[2]=='B'&&h[3]=='1');
    const unsigned n=h[4]|(unsigned(h[5])<<8)|(unsigned(h[6])<<16)|(unsigned(h[7])<<24);
    assert(n>1&&n<=MissionBriefingHeader::maxFrames);
    return int(n);
}

int main(int argc,char **argv){
    assert(argc==2);
    const fs::path root=argv[1];
    MissionBriefingHeader header;
    MissionRecipe mission{};
    std::set<std::string> decoded;
    auto check=[&](int sector){
        const auto selected=briefingHeaderShots(mission,sector);
        int count=0;
        for(const auto &id:selected){
            decoded.insert(id);
            count+=frames(root/(id+".mhr"));
        }
        header.open(root,mission,sector);
        assert(!header.active());
        assert(header.update(0)&&header.active());
        const std::vector<uint16_t> first(header.pixels(),header.pixels()+header.width*header.height);
        for(int i=1;i<count;++i){
            double time=(i+.25)/header.fps;
            assert(header.update(time)&&!header.update(time)&&header.active());
        }
        assert(header.update((count+.25)/header.fps)&&header.active());
        assert(std::equal(first.begin(),first.end(),header.pixels()));
        // A late frame (for instance after XR resume) jumps across loops.
        assert(header.update((count*7+3.25)/header.fps));
        header.stop();
        assert(!header.update(0)&&!header.active());
    };
    const auto catalogue=missionObjectives();
    for(int i=0;i<int(catalogue.size());++i){
        mission.objective=i;
        mission.location=i%int(missionLocations().size());
        assert(briefingHeaderShots(mission,i)[0]==catalogue[i].id);
        assert(briefingHeaderShots(mission,i)[1]==std::string("site-")+missionLocations()[mission.location].id);
        check(i);
    }
    for(const auto &entry:manualMissionCatalog()){
        for(int objective:entry.objectives){
            mission.objective=objective;
            assert(fs::exists(root/(briefingHeaderShots(mission,2)[0]+".mhr")));
        }
    }
    mission.finale=true;
    mission.finaleBattle=true;
    assert(briefingHeaderShots(mission,1)[0]=="finale-battle");
    check(1);
    mission.finaleBattle=false;
    for(int boss=0;boss<int(campaignBosses().size());++boss){
        mission.boss=boss;
        assert(briefingHeaderShots(mission,1)[0]==std::string("boss-")+campaignBoss(boss).spec.id);
        check(1);
    }
    assert(decoded.size()==catalogue.size()+missionLocations().size()+4+1+campaignBosses().size());
    header.open(root/"absent",mission,0);
    assert(!header.update(0)&&!header.active());
    // One damaged or missing cut must not blank the other two cuts or reuse a
    // stale texture from the previous briefing. Exercise a valid directory
    // table whose first compressed frame is damaged.
    const auto temp=fs::temp_directory_path()/("mecha-header-test-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(fs::create_directory(temp));
    const auto ids=briefingHeaderShots(mission,0);
    const auto broken=temp/(ids[0]+".mhr");
    fs::copy_file(root/(ids[0]+".mhr"),broken);
    const auto &site=ids[1];
    fs::copy_file(root/(site+".mhr"),temp/(site+".mhr"));
    const int brokenFrames=frames(broken);
    {
        std::fstream bad(broken,std::ios::binary|std::ios::in|std::ios::out);
        bad.seekp(8+4*brokenFrames);
        bad.put(0);bad.put(0);
        assert(bad);
    }
    header.open(temp,mission,0);
    assert(header.update(0)&&header.active());
    const std::vector<uint16_t> recovered(header.pixels(),header.pixels()+header.width*header.height);
    header.stop();
    fs::remove(broken);
    header.open(temp,mission,0);
    assert(header.update(0)&&std::equal(recovered.begin(),recovered.end(),header.pixels()));
    header.stop();
    fs::remove_all(temp);
    std::cout<<"Verified "<<decoded.size()<<" header shots and cyclic playback\n";
}
