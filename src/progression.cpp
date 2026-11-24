#include "progression.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#include <atomic>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace iron {
using Json=nlohmann::json;
void CampaignRules::load(const std::filesystem::path& base,int campaign){
    if(campaign<0||campaign>1)throw std::runtime_error("Invalid campaign");game=campaign;auto root=game?base/"is2":base;
    std::ifstream f(root/"art/frontend.json");Json j;f>>j;auto& p=j.at("progression");
    unsigned expected=1u<<int(Weapon::Rifle);
    if(game)expected|=(1u<<int(Weapon::Cutter))|(1u<<int(Weapon::Grenade));
    if(p.at("initial_missions")!=15||p.at("initial_weapons")!=expected)throw std::runtime_error("Unsupported campaign rules - regenerate frontend exports");
    auto decoded=p.at("mission_rewards").get<std::vector<int>>();
    for(int w:decoded)if(w!=-1&&(w<1||w>=int(Weapon::Count)))throw std::runtime_error("Invalid campaign reward");
    if(decoded.size()!=size_t(game?20:16))throw std::runtime_error("Invalid campaign length");
    unsigned starting=p.at("initial_weapons").get<unsigned>(),obtainable=starting|1u;
    for(int w:decoded)if(w>=0)obtainable|=1u<<w;
    if(obtainable!=campaignWeaponMask(game))throw std::runtime_error("Campaign leaves implemented weapons unobtainable");
    initialWeapons=starting;rewards=decoded;
}
uint32_t CampaignRules::available(uint32_t done) const {
    unsigned mask=initialMissions;
    if(game){for(unsigned boundary=4;boundary<20;boundary+=4)if((done&((1u<<boundary)-1))==((1u<<boundary)-1))mask|=15u<<boundary;return mask;}
    if((done&15)==15)mask|=0xf0;
    if((done&255)==255)mask|=0xf00;
    if((done&4095)==4095)mask|=0x1000;
    if((done&8191)==8191)mask|=0x2000;
    if((done&16383)==16383)mask|=0x4000;
    if((done&32767)==32767)mask|=0x8000;
    return mask;
}
unsigned CampaignRules::weapons(uint32_t done) const {
    unsigned mask=initialWeapons|1u; // Empty is always selectable.
    for(int i=0;i<count();++i)if((done&(1u<<i))&&rewards[i]>=0)mask|=1u<<rewards[i];
    return mask;
}
bool Progression::missionAvailable(int i) const{return i>=0&&i<rules.count()&&(rules.available(completed)&(1u<<i));}
bool Progression::weaponAvailable(Weapon w) const {int i=int(w);return i>=0&&i<int(Weapon::Count)&&(rules.weapons(completed)&(1u<<i));}
bool Progression::complete(int i){if(!missionAvailable(i)||(completed&(1u<<i)))return false;completed|=1u<<i;return true;}
int Progression::nextMission() const {for(int i=0;i<rules.count();++i)if(missionAvailable(i)&&!(completed&(1u<<i)))return i;return rules.count()-1;}
void Progression::sanitizeLoadout(){
    for(int m=0;m<MountCount;++m)if(!weaponAvailable(loadout[m])||!compatible(loadout[m],m))loadout[m]=Weapon::Empty;
    std::array<unsigned,size_t(Weapon::Count)> copies{};for(auto& w:loadout)if(++copies[unsigned(w)]>weaponLimit(w))w=Weapon::Empty;
    if(loadout[LeftHip]==Weapon::AutoGrenade)loadout[RightHip]=Weapon::Empty;
    else if(loadout[RightHip]==Weapon::AutoGrenade)loadout[LeftHip]=Weapon::Empty;
}
void Progression::load(const std::filesystem::path& path){
    if(!std::filesystem::exists(path))return;
    if(std::filesystem::file_size(path)>16384)throw std::runtime_error("Campaign save exceeds size limit: "+path.string());
    std::ifstream f(path);Json j;f>>j;
    if(j.at("schema")!=1||j.at("game")!="iron-soldier-xr")throw std::runtime_error("Unsupported campaign save: "+path.string());
    if(j.value("campaign",0)!=rules.game)throw std::runtime_error("Save belongs to a different campaign");
    auto integer=[&](const char* key,int limit){auto& v=j.at(key);if(!v.is_number_integer()||v.get<int64_t>()<0||v.get<int64_t>()>limit)throw std::runtime_error("Invalid campaign field: "+std::string(key));return v.get<int>();};
    int done=integer("completed",int(rules.fullMask())),selected=integer("selected_mission",rules.count()-1);
    if(unsigned(done)&~rules.available(uint32_t(done)))throw std::runtime_error("Campaign save violates mission prerequisites");
    auto& items=j.at("loadout");if(!items.is_array()||items.size()!=MountCount)throw std::runtime_error("Invalid saved loadout");
    Loadout equipment{};for(int m=0;m<MountCount;++m){if(!items[m].is_number_integer())throw std::runtime_error("Invalid saved weapon");auto v=items[m].get<int64_t>();if(v<0||v>=int(Weapon::Count))throw std::runtime_error("Invalid saved weapon");equipment[m]=Weapon(v);}
    std::string savedDifficulty=j.value("difficulty",std::string("normal"));if(savedDifficulty!="normal"&&savedDifficulty!="hard")throw std::runtime_error("Invalid saved difficulty");
    difficulty=savedDifficulty=="hard"?World::Difficulty::Hard:World::Difficulty::Normal;
    completed=uint32_t(done);selectedMission=selected;loadout=equipment;sanitizeLoadout();if(!missionAvailable(selectedMission))selectedMission=nextMission();
}
void Progression::save(const std::filesystem::path& path,bool merge){
    auto parent=path.parent_path();if(parent.empty())parent=".";std::filesystem::create_directories(parent);
#ifdef _WIN32
    struct Handle {HANDLE value=INVALID_HANDLE_VALUE;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
    auto failure=[](const char* what){return std::system_error(GetLastError(),std::system_category(),what);};
    auto lockPath=path;lockPath+=L".lock";
    Handle lock{CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
    if(lock.value==INVALID_HANDLE_VALUE)throw failure("Open campaign lock");
    OVERLAPPED overlapped{};
    if(!LockFileEx(lock.value,LOCKFILE_EXCLUSIVE_LOCK,0,MAXDWORD,MAXDWORD,&overlapped))throw failure("Lock campaign save");
#else
    struct FD {int value=-1;~FD(){if(value>=0)::close(value);}};
    FD lock{::open((path.string()+".lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600)};
    if(lock.value<0||::flock(lock.value,LOCK_EX)!=0)throw std::system_error(errno,std::generic_category(),"Lock campaign save");
#endif
    Progression previous;previous.rules=rules;if(merge){previous.load(path);completed|=previous.completed;}
    sanitizeLoadout();if(!missionAvailable(selectedMission))selectedMission=nextMission();
    std::array<int,MountCount> equipment{};for(int i=0;i<MountCount;++i)equipment[i]=int(loadout[i]);
    std::string data=Json{{"schema",1},{"game","iron-soldier-xr"},{"campaign",rules.game},{"completed",completed},{"selected_mission",selectedMission},{"difficulty",difficulty==World::Difficulty::Hard?"hard":"normal"},{"loadout",equipment}}.dump(2)+"\n";
#ifdef _WIN32
    static std::atomic<unsigned> serial{0};
    std::filesystem::path temporary;
    Handle output;
    for(unsigned attempt=0;attempt<100;++attempt){
        temporary=path;temporary+=L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(serial++);
        output.value=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(output.value!=INVALID_HANDLE_VALUE)break;
        if(GetLastError()!=ERROR_FILE_EXISTS)throw failure("Create campaign save");
    }
    if(output.value==INVALID_HANDLE_VALUE)throw failure("Create campaign save");
    try{
        size_t cursor=0;
        while(cursor<data.size()){
            DWORD written=0;
            if(!WriteFile(output.value,data.data()+cursor,DWORD(data.size()-cursor),&written,nullptr)||!written)throw failure("Write campaign save");
            cursor+=written;
        }
        if(!FlushFileBuffers(output.value))throw failure("Flush campaign save");
        CloseHandle(output.value);output.value=INVALID_HANDLE_VALUE;
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw failure("Replace campaign save");
    }catch(...){
        if(output.value!=INVALID_HANDLE_VALUE){CloseHandle(output.value);output.value=INVALID_HANDLE_VALUE;}
        DeleteFileW(temporary.c_str());throw;
    }
#else
    std::string pattern=path.string()+".tmp.XXXXXX";std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);FD output{::mkstemp(name.data())};
    if(output.value<0)throw std::system_error(errno,std::generic_category(),"Create campaign save");
    try {
        size_t cursor=0;while(cursor<data.size()){ssize_t n=::write(output.value,data.data()+cursor,data.size()-cursor);if(n<0&&errno==EINTR)continue;if(n<=0)throw std::system_error(errno,std::generic_category(),"Write campaign save");cursor+=size_t(n);}
        if(::fsync(output.value)!=0)throw std::system_error(errno,std::generic_category(),"Flush campaign save");
        std::filesystem::rename(name.data(),path);
        FD directory{::open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC)};if(directory.value>=0)::fsync(directory.value);
    }catch(...){::unlink(name.data());throw;}
#endif
}
std::filesystem::path Progression::defaultPath(){
#ifdef _WIN32
    if(auto p=_wgetenv(L"LOCALAPPDATA");p&&*p)return std::filesystem::path(p)/"iron-soldier-xr/campaign.json";
#endif
    const char* data=std::getenv("XDG_DATA_HOME");if(data&&*data&&std::filesystem::path(data).is_absolute())return std::filesystem::path(data)/"iron-soldier-xr/campaign.json";
    const char* home=std::getenv("HOME");if(home&&*home)return std::filesystem::path(home)/".local/share/iron-soldier-xr/campaign.json";
    throw std::runtime_error("No campaign data directory; pass --save PATH");
}
}
