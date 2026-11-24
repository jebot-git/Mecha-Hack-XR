#include "render.hpp"
#include "assault.hpp"
#include "xr.hpp"
#include "audio.hpp"
#include "coop.hpp"
#include "config.hpp"
#include "cockpit_tracking.hpp"
#include "net/voice.hpp"
#include <SDL.h>
#include <SDL_system.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <charconv>
#include <optional>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace iron;
using namespace avp;
static float yaw(XrPosef pose) { auto m=poseMatrix(pose); return std::atan2(m[8],m[10]); }
static float pitch(XrPosef pose) { return std::asin(std::clamp(-poseMatrix(pose)[9],-1.f,1.f)); }
static void screenshot(const std::string& path,int width,int height) {
    std::vector<unsigned char> pixels(width*height*3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    std::ofstream out(path,std::ios::binary);if(!out)throw std::runtime_error("Cannot write screenshot");
    out<<"P6\n"<<width<<" "<<height<<"\n255\n";
    for(int y=height-1;y>=0;--y)out.write(reinterpret_cast<const char*>(pixels.data()+y*width*3),width*3);
}
static std::string loadCartridge(const std::filesystem::path& root) {
    std::ifstream file(root/"manifest.json");
    if(!file)throw std::runtime_error("Import the local cart with tools/import_cartridge.py first, or use --fixture to skip cartridge validation");
    nlohmann::json j;file>>j;
    if(j.at("schema")!=1 || j.at("sha256")!="bfbe264c82e5d4e6cc9d0ec51f917e41b0f2bf7fef52316f64b12b30899d94c4")
        throw std::runtime_error("Unsupported cartridge manifest");
    std::ifstream cart(root/"cartridge.bin",std::ios::binary|std::ios::ate);
    if(!cart || cart.tellg()!=2097152)throw std::runtime_error("Missing or truncated imported cartridge");
    cart.seekg(0x2048);std::string module(0x32,'\0');cart.read(module.data(),module.size());
    if(module.find("IRON SOLDIER ROM_MODULE  Version 1.04 29.10.1994")==std::string::npos)
        throw std::runtime_error("Imported cartridge module marker is damaged");
    std::cout<<j.at("module").get<std::string>()<<"\n"<<j.at("status").get<std::string>()<<"\n";
    return "Iron Soldier XR - native missions / local cartridge";
}
static std::filesystem::path defaultRetail() {
    if(char* path=SDL_GetBasePath()){
        std::filesystem::path base(path);SDL_free(path);
        for(auto root:{base/"assets/retail",base/"../assets/retail"})
            if(std::filesystem::is_regular_file(root/"manifest.json"))return root;
    }
    return IRON_ASSETS;
}
static uint32_t parseSeed(std::string_view value){
    uint32_t seed=0;auto result=std::from_chars(value.data(),value.data()+value.size(),seed);
    if(result.ec!=std::errc{}||result.ptr!=value.data()+value.size())throw std::runtime_error("Seed must be an unsigned 32-bit integer");
    return seed;
}
static uint32_t newSkirmishSeed(){
    auto tick=uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
    return uint32_t(tick)^uint32_t(tick>>32);
}
int main(int argc,char** argv) {
    SDL_Window* window=nullptr;SDL_GLContext context=nullptr;
    try {
#ifdef __ANDROID__
        const char *storage=SDL_AndroidGetInternalStoragePath();
        if(!storage)throw std::runtime_error("Android private storage is unavailable");
        setenv("XDG_DATA_HOME",storage,1);
        setenv("XDG_CONFIG_HOME",storage,1);
#endif
        bool ejectionDemo=false;float demoClock=0;float healthPreview=-1;float ejectionPreview=-1;bool useXR=false,fixture=false;int frameLimit=0;Frontend menu;std::string capture;
#ifdef __ANDROID__
        useXR=true;
#endif
        std::filesystem::path retail=defaultRetail(),savePath;bool missionSpecified=false,freshCampaign=false;
        ClientConfig config;auto configPath=ClientConfig::defaultPath();bool explicitConfig=false;
        for(int i=1;i<argc;++i)if(std::string(argv[i])=="--config"){if(++i>=argc)throw std::runtime_error("Missing --config path");configPath=argv[i];explicitConfig=true;}
        config.load(configPath,explicitConfig);config.environment();
        menu.soundVolume=config.soundVolume;menu.musicVolume=config.musicVolume;
        std::string serverAddress=config.server,playerName=config.playerName,joinCode;
        std::filesystem::path proceduralRecipe;std::optional<uint32_t> prototypeSeed,skirmishSeed;std::optional<unsigned> assaultScenario;unsigned assaultSlot=0;std::filesystem::path assaultConfig;
        std::optional<nlohmann::json> skirmishRecipe;bool screenSpecified=false,campaignSpecified=false,environmentSpecified=false;
        bool hostLobby=false,difficultySpecified=false,networkSearchlights=false;World::Difficulty requestedDifficulty=World::Difficulty::Normal;
        for(int i=1;i<argc;++i) {
            std::string a=argv[i];
            if(a=="--ejection-preview" && i+1<argc)ejectionPreview=std::stof(argv[++i]);
            else if(a=="--health-preview" && i+1<argc){healthPreview=std::stof(argv[++i]);if(!std::isfinite(healthPreview)||healthPreview<0||healthPreview>100)throw std::runtime_error("Health preview must be 0..100");}
            else if(a=="--ejection-demo")ejectionDemo=true;
            else if(a=="--xr")useXR=true;
            else if(a=="--desktop")useXR=false;
            else if(a=="--fixture")fixture=true;
            else if(a=="--campaign" && i+1<argc){campaignSpecified=true;std::string c=argv[++i];if(c!="is1"&&c!="is2"&&c!="skirmish")throw std::runtime_error("Campaign must be is1, is2 or skirmish");menu.skirmish=c=="skirmish";if(!menu.skirmish)menu.game=c=="is2";}
            else if(a=="--searchlight-preview")networkSearchlights=true;
            else if(a=="--assault-scenario"&&i+1<argc)assaultScenario=unsigned(std::stoul(argv[++i]));
            else if(a=="--assault-slot"&&i+1<argc)assaultSlot=unsigned(std::stoul(argv[++i]));
            else if(a=="--assault-config"&&i+1<argc)assaultConfig=argv[++i];
            else if(a=="--lighting"&&i+1<argc){environmentSpecified=true;auto j=environmentWire(menu.skirmishSettings.environment);j["lighting"]=argv[++i];menu.skirmishSettings.environment=readEnvironment(j);}
            else if(a=="--weather"&&i+1<argc){environmentSpecified=true;auto j=environmentWire(menu.skirmishSettings.environment);j["weather"]=argv[++i];menu.skirmishSettings.environment=readEnvironment(j);}
            else if(a=="--skirmish-seed" && i+1<argc)skirmishSeed=parseSeed(argv[++i]);
            else if(a=="--mission-prototype" && i+1<argc){
                prototypeSeed=parseSeed(argv[++i]);
            }
            else if(a=="--procedural-layout" && i+1<argc)proceduralRecipe=argv[++i];
            else if(a=="--sandbox")menu.sandbox=true;
            else if(a=="--config" && i+1<argc)++i;
            else if(a=="--name" && i+1<argc)playerName=validatePlayerName(argv[++i]);
            else if(a=="--server" && i+1<argc)serverAddress=argv[++i];
            else if(a=="--host")hostLobby=true;
            else if(a=="--join" && i+1<argc)joinCode=argv[++i];
            else if(a=="--difficulty" && i+1<argc){std::string d=argv[++i];if(d!="normal"&&d!="hard")throw std::runtime_error("Difficulty must be normal or hard");menu.difficulty=d=="hard"?World::Difficulty::Hard:World::Difficulty::Normal;requestedDifficulty=menu.difficulty;difficultySpecified=true;}
            else if(a=="--save" && i+1<argc)savePath=argv[++i];
            else if(a=="--screen" && i+1<argc){screenSpecified=true;std::string s=argv[++i];if(s=="campaign")menu.screen=Screen::Campaign;else if(s=="skirmish")menu.screen=Screen::Skirmish;else if(s=="title")menu.screen=Screen::Title;else if(s=="missions")menu.screen=Screen::Missions;else if(s=="briefing")menu.screen=Screen::Briefing;else if(s=="loadout")menu.screen=Screen::Loadout;else if(s=="playing")menu.screen=Screen::Playing;else throw std::runtime_error("Invalid --screen");}
            else if(a=="--mission" && i+1<argc){missionSpecified=true;menu.mission=std::stoi(argv[++i])-1;if(menu.mission<0||menu.mission>19)throw std::runtime_error("Mission must be 1..20");}
            else if(a=="--retail" && i+1<argc)retail=argv[++i];
            else if(a=="--frames" && i+1<argc) {frameLimit=std::stoi(argv[++i]);if(frameLimit<=0)throw std::runtime_error("--frames must be positive");}
            else if(a=="--screenshot" && i+1<argc)capture=argv[++i];
            else if(a=="--help") {
                std::cout<<"iron-soldier-xr [--desktop|--xr] [--fixture] [--sandbox] [--save PATH] [--retail PATH] [--frames N] [--screenshot FILE.ppm] [--screen title|campaign|skirmish|missions|briefing|loadout|playing] [--campaign is1|is2|skirmish] [--assault-scenario 0..6 --assault-slot 0..7 --assault-config FILE] [--skirmish-seed N] [--lighting authored|day|night] [--weather clear|fog|rain|storm|sandstorm] [--mission N] [--ejection-preview SECONDS] [--ejection-demo] [--config FILE] [--name NAME] [--server HOST:PORT] [--host|--join CODE] [--difficulty normal|hard] [--mission-prototype SEED | --procedural-layout FILE]\n"
                         <<"W/S move, A/D steer chassis; arrows turn torso (+/-90 degrees); mouse aims.\n"
                         <<"Searchlight: right joystick click / L toggles (Skirmish / Assault). Left joystick click resets cockpit height and position.\nMap pings: G identifies self, H marks head-aimed target (XR left/right grip).\nLeft/right mouse: hands. 1/2: shoulders (Y/B). 3/4: hips (X/A, equipped hip weapon).\n"
                         <<"Menus: arrows/stick browse, Enter/right trigger select, mount buttons equip, Enter/menu deploy.\nEscape returns to menus, Q/E punch, R recenter, Ctrl+W quit.\nHold Z/C + mouse to aim left/right arm; Shift translates, Alt rolls, wheel adjusts depth.\n";
                return 0;
            } else throw std::runtime_error("Unknown or incomplete option: "+a);
        }
        if(assaultScenario){
            if(*assaultScenario>=assaultScenarios.size()||assaultSlot>=8||menu.skirmish||skirmishSeed||fixture||hostLobby||!joinCode.empty()||prototypeSeed||!proceduralRecipe.empty()||ejectionDemo||ejectionPreview>=0||healthPreview>=0)throw std::runtime_error("Invalid local assault preview options");
            menu.sandbox=true;menu.screen=Screen::Playing;menu.game=assaultScenarios[*assaultScenario].game;menu.mission=assaultScenarios[*assaultScenario].mission;
        }else if(!assaultConfig.empty()||assaultSlot)throw std::runtime_error("Assault config/slot requires --assault-scenario");
        if(skirmishSeed){if(campaignSpecified&&!menu.skirmish)throw std::runtime_error("Skirmish seed cannot select an IS1 or IS2 campaign");menu.skirmish=true;}
        if(networkSearchlights)throw std::runtime_error("Weather and searchlights are reserved for Skirmish and Assault; --searchlight-preview campaign rooms are retired");
        if(environmentSpecified)menu.skirmishSettings.environmentCycle=false;
        if(environmentSpecified&&!menu.skirmish&&!assaultScenario)throw std::runtime_error("Lighting/weather options require Skirmish or Assault");
        if(!proceduralRecipe.empty()){if(prototypeSeed)throw std::runtime_error("Choose --mission-prototype or --procedural-layout");std::error_code error;auto bytes=std::filesystem::file_size(proceduralRecipe,error);if(error||bytes>1024*1024)throw std::runtime_error("Missing or oversized procedural layout");std::ifstream f(proceduralRecipe);if(!f)throw std::runtime_error("Missing procedural layout: "+proceduralRecipe.string());nlohmann::json j;f>>j;validateProceduralRecipe(j);prototypeSeed=j.at("seed").get<uint32_t>();}
        if(prototypeSeed){
            if(menu.skirmish||fixture||hostLobby||!joinCode.empty()||missionSpecified||ejectionDemo||ejectionPreview>=0||healthPreview>=0)throw std::runtime_error("Mission prototype cannot combine with skirmish, fixture, co-op, mission selection or preview options");
            menu.sandbox=true;menu.game=1;menu.mission=0;menu.screen=Screen::Playing;
            menu.prototypeBriefing="SEED "+std::to_string(*prototypeSeed)+" / DESTROY ALL MARKED ENEMIES";
            menu.loadout={Weapon::Empty,Weapon::Rifle,Weapon::Rail,Weapon::Shield,Weapon::Rocket,Weapon::Grenade};
        }
        if(menu.skirmish){
            if(fixture||hostLobby||!joinCode.empty()||missionSpecified||ejectionDemo||ejectionPreview>=0||healthPreview>=0)throw std::runtime_error("Skirmish cannot combine with campaign mission selection, co-op or preview options");
            menu.skirmishSettings.seed=skirmishSeed.value_or(newSkirmishSeed());menu.mission=0;
            if(!screenSpecified)menu.screen=Screen::Skirmish;
            if(menu.screen!=Screen::Skirmish&&menu.screen!=Screen::Briefing&&menu.screen!=Screen::Loadout&&menu.screen!=Screen::Playing)throw std::runtime_error("Skirmish screen must be setup, briefing, loadout or playing");
            menu.loadout={Weapon::Empty,Weapon::Rifle,Weapon::Rail,Weapon::Shield,Weapon::Rocket,Weapon::Grenade};
        }else if(menu.screen==Screen::Skirmish)throw std::runtime_error("Skirmish setup requires --campaign skirmish");
        if(ejectionDemo||ejectionPreview>=0)menu.screen=Screen::Playing;
        std::string title=fixture?"Iron Soldier XR - native test range":loadCartridge(retail);
        menu.sandbox|=healthPreview>=0||fixture||ejectionDemo||ejectionPreview>=0;
        auto campaignSave=[&](){auto path=savePath;if(menu.game)path.replace_filename(path.stem().string()+"-is2"+path.extension().string());return path;};
        if(!fixture)menu.campaign.rules.load(retail,menu.game);
        if(menu.mission>=menu.campaign.rules.count())throw std::runtime_error("Mission outside selected campaign");
        if(!menu.sandbox&&savePath.empty())savePath=Progression::defaultPath();
        if(!menu.sandbox&&!menu.skirmish){
            int requested=menu.mission;menu.campaign.load(campaignSave());menu.restoreCampaign();if(!difficultySpecified)menu.difficulty=menu.campaign.difficulty;
            if(missionSpecified)menu.mission=requested;
            if(!menu.missionAvailable(menu.mission))menu.screen=Screen::Missions;
        }
        auto saveCampaign=[&](){
            if(menu.sandbox||menu.skirmish)return;
            try{menu.campaign.difficulty=menu.difficulty;menu.campaign.save(campaignSave(),!freshCampaign);freshCampaign=false;menu.saveMessage="PROGRESS SAVED";}
            catch(const std::exception& e){menu.saveMessage="PROGRESS NOT SAVED";std::cerr<<"Campaign save: "<<e.what()<<"\n";}
        };
        if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0)throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
#ifdef __ANDROID__
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
#else
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
#endif
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
        window=SDL_CreateWindow(title.c_str(),SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1280,800,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
        if(!window)throw std::runtime_error(SDL_GetError());
        context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());
#ifdef _WIN32
        glewExperimental=GL_TRUE;
        auto glewResult=glewInit();
        if(glewResult!=GLEW_OK)throw std::runtime_error(reinterpret_cast<const char*>(glewGetErrorString(glewResult)));
        if(!GLEW_VERSION_3_3)throw std::runtime_error("OpenGL 3.3 is required");
        while(glGetError()!=GL_NO_ERROR){}
#endif
        SDL_GL_SetSwapInterval(useXR?0:1);
        {
            Renderer renderer(retail/"cockpit");Audio audio(retail);audio.setVolumes(menu.soundVolume,menu.musicVolume);
            std::unique_ptr<XR> xr;
            if(useXR){xr=std::make_unique<XR>();xr->initialize(window);std::cout<<xr->description()<<"\n";}
            else if(!frameLimit && menu.screen==Screen::Playing) SDL_SetRelativeMouseMode(SDL_TRUE);
            World world;std::unique_ptr<AssaultMatch> assault;
            std::unique_ptr<coop::EnvironmentClient> environmentClient;
            auto applyEnvironment=[&](World& target){
                if(!menu.skirmish&&!assaultScenario){environmentClient.reset();menu.environmentMessage.clear();return;}
                if(!environmentClient)environmentClient=std::make_unique<coop::EnvironmentClient>(serverAddress);
                auto climate=menu.skirmish&&menu.screen==Screen::Skirmish?skirmishClimate(menu.skirmishSettings):target.climate;
                auto sample=environmentClient->sample(climate);
                if(menu.skirmishSettings.environmentCycle){
                    if(target.environmentEnabled()){target.environment=sample.settings;target.environmentSeconds=sample.weatherSeconds;}
                    menu.environmentMessage=environmentDescription(sample,environmentClient->synchronized());
                }else{
                    auto settings=restrictEnvironment(menu.skirmishSettings.environment,climate);
                    if(target.environmentEnabled()){target.environment=settings;target.environmentSeconds=-1;}
                    menu.environmentMessage="MANUAL PREVIEW / "+std::string(lightingNames[size_t(settings.lighting)])+" / "+std::string(weatherNames[size_t(settings.weather)]);
                }
            };
            auto loadSelectedMission=[&](World& next){
                if(assaultScenario){
                    assault=std::make_unique<AssaultMatch>();AssaultRules rules;rules.environment=menu.skirmishSettings.environment;nlohmann::json config=nlohmann::json::object();
                    if(!assaultConfig.empty()){std::ifstream file(assaultConfig);if(!file)throw std::runtime_error("Missing assault configuration");file>>config;
                        if(config.contains("environment")){rules.environment=readEnvironment(config.at("environment"));menu.skirmishSettings.environment=rules.environment;menu.skirmishSettings.environmentCycle=false;}
                        rules.attackerUnits=config.value("attacker_units",std::vector<int>{});rules.defenderUnits=config.value("defender_units",std::vector<int>{});}
                    assault->load(retail,*assaultScenario,rules);
                    for(const auto& entry:config.value("loadouts",nlohmann::json::array()))assault->setLoadout(entry.at("slot").get<unsigned>(),entry.at("weapons").get<Loadout>());
                    for(auto& p:assault->pilots)p.pilot.armRig=renderer.armRig();
                    next=std::move(assault->world);static_cast<Pilot&>(next)=assault->pilots[assaultSlot].pilot;
                    menu.remotePilots.clear();next.mapPilots.clear();for(size_t i=0;i<8;++i){next.mapPilots.push_back(assault->pilots[i].pilot);if(i!=assaultSlot)menu.remotePilots.push_back(assault->pilots[i].pilot);}
                    menu.prototypeBriefing="LOCAL 4V4 PREVIEW / "+std::string(teamName(next.team));
                }else if(menu.skirmish){
                    if(!skirmishRecipe)skirmishRecipe=generateSkirmishRecipe(menu.skirmishSettings);
                    next.loadProceduralRecipe(retail,*skirmishRecipe);
                    auto kind=skirmishRecipe->at("mission").at("kind").get<std::string>();
                    auto biome=skirmishRecipe->at("terrain").at("biome").get<std::string>();
                    auto water=skirmishRecipe->at("terrain").at("water_layout").get<std::string>();
                    menu.prototypeBriefing=biome+" / "+water+" / ";
                    menu.prototypeBriefing+=kind=="recon"?"HOLD EACH MARKED ZONE FOR 4 SECONDS.":kind=="strike"?"DESTROY THE MARKED STRUCTURES.":"DESTROY EVERY MARKED HOSTILE GROUP.";
                    if(menu.skirmishSettings.extraction)menu.prototypeBriefing+=" RETURN TO EX FOR EXTRACTION.";
                    menu.prototypeBriefing+=" / "+next.objective;
                    std::cout<<"Skirmish seed="<<menu.skirmishSettings.seed<<" objective="<<kind<<" terrain="<<biome<<"\n";
                }else if(!proceduralRecipe.empty())next.loadProceduralPrototype(retail,proceduralRecipe);
                else if(prototypeSeed)next.loadMissionPrototype(retail,*prototypeSeed);
                else if(!fixture)next.loadMission(retail,menu.mission,menu.game);
                if(prototypeSeed)menu.prototypeBriefing="SEED "+std::to_string(*prototypeSeed)+" / "+next.objective;
                applyEnvironment(next);
                if(next.environmentEnabled())menu.environmentIntro=8;
                if(next.environmentEnabled())std::cout<<menu.environmentMessage<<"\n";
            };
            world.difficulty=menu.difficulty;world.configure(menu.loadout,renderer.armRig());
            if(menu.skirmish&&menu.screen==Screen::Skirmish)world.loadMission(retail,0,menu.game);else loadSelectedMission(world);
            renderer.resetCockpit();
            std::array<bool,MountCount> suppressFire{};std::array<bool,2> suppressPing{};bool suppressSearchlight=true;bool recordedCompletion=false; bool running=true,paused=false,desktopFocus=true,lastMenu=false,anchored=false;
            if(ejectionPreview>=0){menu.screen=Screen::Playing;world.hurt(10000);for(float t=0;t< std::min(7.f,ejectionPreview);t+=.01f)world.stepEjection(.01f);}
            float headYaw=0,headPitch=0,referenceYaw=0;
            XrVector3f anchor{};
            RecenterPress recenterPress;
            std::array<Vec3,2> desktopHands{{{-.4f,-.13f,-.75f},{.4f,-.13f,-.75f}}};
            std::array<Vec3,2> desktopHandAngles{};
            std::unique_ptr<coop::Client> network;std::unique_ptr<avp::voice::Chat> voice;
            uint64_t seenRound=0,seenRevision=0,seenSelection=0;int seenPhase=-1,seenMission=-1;bool voiceConfigured=false;
            auto connectLobby=[&](bool host,const std::string& code){
                try{if(menu.skirmish)throw std::runtime_error("Skirmish is singleplayer only");if(menu.sandbox)throw std::runtime_error("Cooperative requires campaign mode; remove --sandbox/--fixture");
                    network=std::make_unique<coop::Client>(serverAddress,host,menu.campaign,code,playerName,true);voice=std::make_unique<avp::voice::Chat>();
                    if(audio.available())voice->shareOutput({[&](){return audio.voiceBytes();},[&](std::span<const float> pcm){audio.queueVoice(pcm);},[&](){audio.clearVoice();}});
                    voiceConfigured=false;
                    menu.cooperative=true;menu.host=host;menu.screen=Screen::Connecting;menu.networkMessage.clear();seenRound=seenRevision=seenSelection=0;seenPhase=seenMission=-1;
                    menu.difficulty=World::Difficulty::Hard;
                }catch(const std::exception& e){menu.networkMessage=e.what();menu.screen=Screen::Cooperative;network.reset();voice.reset();}
            };
            auto disconnectLobby=[&](){menu.skirmish=false;skirmishRecipe.reset();menu.prototypeBriefing.clear();voice.reset();audio.clearVoice();network.reset();menu.cooperative=false;menu.host=false;menu.spectating=false;menu.teamResult=0;menu.remotePilots.clear();menu.roster.clear();menu.lobbyCode.clear();menu.voiceMessage.clear();menu.screen=Screen::Title;menu.option=0;
                menu.campaign=Progression{};if(!fixture)menu.campaign.rules.load(retail,menu.game);if(!savePath.empty())menu.campaign.load(campaignSave());menu.restoreCampaign();menu.difficulty=World::Difficulty::Normal;};
            auto deploy=[&](){
                World next;next.difficulty=menu.difficulty;next.configure(menu.loadout,renderer.armRig());loadSelectedMission(next);world=std::move(next);
                renderer.resetCockpit();
                recordedCompletion=false;headYaw=headPitch=0;menu.screen=Screen::Playing;menu.resultMessage.clear();menu.saveMessage.clear();
            };
            auto missionMenu=[&](){if(menu.screen==Screen::Playing&&(world.missionStatus!=MissionStatus::Ejecting||assault)){if(menu.skirmish||prototypeSeed)menu.prototypeBriefing=std::string(menu.skirmish?"SKIRMISH / SEED ":"SEED ")+std::to_string(menu.skirmish?menu.skirmishSettings.seed:*prototypeSeed)+" / "+world.objective;menu.screen=Screen::Pause;menu.option=0;menu.confirmHeld=true;world.resetTracking();if(!xr)SDL_SetRelativeMouseMode(SDL_FALSE);headYaw=headPitch=0;}else if(menu.screen==Screen::Pause){menu.screen=Screen::Playing;menu.confirmHeld=true;if(!xr&&!frameLimit)SDL_SetRelativeMouseMode(SDL_TRUE);}};
            if(hostLobby||!joinCode.empty())connectLobby(hostLobby,joinCode);
#ifndef __ANDROID__
            SDL_StartTextInput();
#endif
            auto last=std::chrono::steady_clock::now();int frames=0;
            while(running && (!frameLimit || frames<frameLimit)) {
#ifdef __ANDROID__
                // Opening Android's keyboard at startup steals immersive focus.
                if(menu.screen==Screen::Join){if(!SDL_IsTextInputActive())SDL_StartTextInput();}
                else if(SDL_IsTextInputActive())SDL_StopTextInput();
#endif
                bool confirm=false,back=false;
                SDL_Event e;
                while(SDL_PollEvent(&e)) {
                    if(e.type==SDL_QUIT)running=false;
                    if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {desktopFocus=false;world.resetTracking();}
                    if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_GAINED)desktopFocus=true;
                    if(e.type==SDL_MOUSEMOTION && menu.screen==Screen::Playing && !paused && !xr && SDL_GetRelativeMouseMode()) {
                        const auto* keys=SDL_GetKeyboardState(nullptr);
                        bool editing=keys[SDL_SCANCODE_Z]||keys[SDL_SCANCODE_C];
                        if(!editing){headYaw=wrap(headYaw-e.motion.xrel*.0025f);headPitch=std::clamp(headPitch-e.motion.yrel*.0025f,-1.45f,1.45f);}
                        for(int h=0;h<2;++h)if(keys[h?SDL_SCANCODE_C:SDL_SCANCODE_Z]){
                            if(SDL_GetModState()&KMOD_SHIFT){desktopHands[h].x+=e.motion.xrel*.002f;desktopHands[h].y-=e.motion.yrel*.002f;}
                            else if(SDL_GetModState()&KMOD_ALT)desktopHandAngles[h].z-=e.motion.xrel*.005f;
                            else {desktopHandAngles[h].x=wrap(desktopHandAngles[h].x-e.motion.xrel*.0025f);desktopHandAngles[h].y=std::clamp(desktopHandAngles[h].y-e.motion.yrel*.0025f,-1.5f,1.5f);}
                        }
                    }
                    if(e.type==SDL_MOUSEWHEEL && !xr && menu.screen==Screen::Playing) {
                        const auto* keys=SDL_GetKeyboardState(nullptr);
                        for(int h=0;h<2;++h)if(keys[h?SDL_SCANCODE_C:SDL_SCANCODE_Z])desktopHands[h].z=std::clamp(desktopHands[h].z-e.wheel.y*.05f,-1.2f,.3f);
                    }
                    if(e.type==SDL_TEXTINPUT&&menu.screen==Screen::Join){for(char c:std::string(e.text.text)){c=char(std::toupper(static_cast<unsigned char>(c)));if(std::string_view("ABCDEFGHJKLMNPQRSTUVWXYZ23456789").find(c)!=std::string_view::npos){menu.joinCode[menu.codeCursor]=c;menu.codeCursor=(menu.codeCursor+1)%6;}}}
                    if(e.type==SDL_KEYDOWN && !e.key.repeat) {
                        if(e.key.keysym.sym==SDLK_TAB&&menu.spectating)++menu.spectator;
                        if(e.key.keysym.sym==SDLK_m&&menu.cooperative&&menu.screen!=Screen::Join)menu.micEnabled=!menu.micEnabled;
                        if(e.key.keysym.sym==SDLK_w && (e.key.keysym.mod&KMOD_CTRL))running=false;
                        if(e.key.keysym.sym==SDLK_RETURN)confirm=true;
                        if(e.key.keysym.sym==SDLK_DELETE && menu.screen==Screen::Loadout)menu.weapon=0;
                        if(e.key.keysym.sym==SDLK_ESCAPE) {
                            if(menu.screen==Screen::Playing||menu.screen==Screen::Pause)missionMenu();
                            else back=true;
                        }
                        if(e.key.keysym.sym==SDLK_r&&world.missionStatus!=MissionStatus::Ejecting){desktopHands={{{-.4f,-.13f,-.75f},{.4f,-.13f,-.75f}}};desktopHandAngles={};anchored=false;headYaw=headPitch=0;world.missile.active=false;world.resetTracking();}
                    }
                }
                if(!running)break;
                Input input;
                if(xr) {
                    if(!xr->beginFrame())break;
                    const auto& t=xr->tracking();
                    if(t.menu&&!lastMenu&&(menu.screen==Screen::Playing||menu.screen==Screen::Pause))missionMenu();
                    confirm=(menu.screen==Screen::Loadout||menu.screen==Screen::Ready||menu.screen==Screen::Debrief)&&t.menu;back=((menu.screen==Screen::Briefing||menu.screen==Screen::Disconnected||menu.screen==Screen::Skirmish||menu.screen==Screen::Campaign)&&t.menu)||t.turn.y<-.7f;lastMenu=t.menu;
                    input.focused=xr->focused()&&!paused;input.headValid=t.headValid;
                    bool reset=recenterPress.update(t.recenter,input.focused&&t.headValid,
                        menu.screen==Screen::Playing&&!menu.spectating&&!world.lifeSpent&&!world.missile.active&&world.missionStatus!=MissionStatus::Ejecting);
                    if(reset){anchored=false;world.resetTracking();suppressFire.fill(true);suppressPing.fill(true);suppressSearchlight=true;}
                    if(t.headValid) {
                        auto head=xr->headPose();
                        if(!anchored){auto reference=cockpitTrackingAnchor(poseMatrix(head));anchor={reference[12],reference[13],reference[14]};referenceYaw=std::atan2(reference[8],reference[10]);anchored=true;}
                        input.headYaw=wrap(yaw(head)-referenceYaw);input.headPitch=pitch(head);
                        input.steer=t.move.x;input.forward=t.move.y;input.turn=t.turn.x;
                        input.ping={t.squeezeHeld[0],t.squeezeHeld[1]};input.searchlight=t.inventory;
                        input.fire=mountButtons(t.triggerHeld[0],t.triggerHeld[1],t.facePrimary[1],t.faceSecondary[1],t.facePrimary[0],t.faceSecondary[0]);
                        for(int h=0;h<2;++h) {
                            input.handValid[h]=t.gripValid[h];
                            const auto& grip=t.grips[h];const auto& aim=t.hands[h];
                            Quaternion recenter=angles(-referenceYaw);
                            input.handPoses[h]={rotate(recenter,{grip.position.x-anchor.x,grip.position.y-anchor.y,grip.position.z-anchor.z}),
                                compose(recenter,{grip.orientation.x,grip.orientation.y,grip.orientation.z,grip.orientation.w}),
                                compose(recenter,{aim.orientation.x,aim.orientation.y,aim.orientation.z,aim.orientation.w}),
                                t.gripValid[h]&&t.valid[h]};
                            input.hands[h]=input.handPoses[h].position;
                        }
                    }
                } else {
                    auto keys=SDL_GetKeyboardState(nullptr);auto mouse=SDL_GetMouseState(nullptr,nullptr);
                    input.focused=desktopFocus&&!paused;input.headYaw=headYaw;input.headPitch=headPitch;
                    input.steer=float(keys[SDL_SCANCODE_D])-keys[SDL_SCANCODE_A];input.forward=float(keys[SDL_SCANCODE_W])-keys[SDL_SCANCODE_S];
                    input.turn=float(keys[SDL_SCANCODE_RIGHT])-keys[SDL_SCANCODE_LEFT];
                    if(menu.screen!=Screen::Playing){input.steer+=input.turn;input.forward+=float(keys[SDL_SCANCODE_UP])-keys[SDL_SCANCODE_DOWN];}
                    if(menu.screen==Screen::Join){input.steer=input.turn;input.forward=float(keys[SDL_SCANCODE_UP])-keys[SDL_SCANCODE_DOWN];}
                    input.fire={bool(mouse&SDL_BUTTON_LMASK),bool(mouse&SDL_BUTTON_RMASK),bool(keys[SDL_SCANCODE_1]),bool(keys[SDL_SCANCODE_2]),bool(keys[SDL_SCANCODE_3]),bool(keys[SDL_SCANCODE_4])};
                    input.ping={bool(keys[SDL_SCANCODE_G]),bool(keys[SDL_SCANCODE_H])};input.searchlight=keys[SDL_SCANCODE_L];
                    input.punch={bool(keys[SDL_SCANCODE_Q]),bool(keys[SDL_SCANCODE_E])};
                    for(int h=0;h<2;++h){auto e=desktopHandAngles[h];auto q=angles(e.x,e.y,e.z);input.handPoses[h]={desktopHands[h],q,q,true};}
                }
                if(!input.searchlight)suppressSearchlight=false;
                if(menu.screen!=Screen::Playing||world.lifeSpent)suppressSearchlight=true;
                if(suppressSearchlight)input.searchlight=false;
                auto now=std::chrono::steady_clock::now();float dt=std::chrono::duration<float>(now-last).count();last=now;
                applyEnvironment(world);
                if(environmentClient){environmentClient->update(dt);applyEnvironment(world);}
                if(menu.screen==Screen::Playing&&input.focused)menu.environmentIntro=std::max(0.f,menu.environmentIntro-dt);
                if(menu.screen==Screen::Playing)for(int m=0;m<MountCount;++m){if(!input.fire[m])suppressFire[m]=false;if(suppressFire[m])input.fire[m]=false;}
                if(menu.screen==Screen::Playing)for(int h=0;h<2;++h){if(!input.ping[h])suppressPing[h]=false;if(suppressPing[h])input.ping[h]=false;}
                if(network){
                    Input outgoing=input;if(menu.screen!=Screen::Playing||world.lifeSpent)outgoing.focused=false;
                    network->update(std::min(dt,.1f),&outgoing);
                    if(!network->error.empty()){menu.networkMessage=network->error;network->error.clear();if(!network->connected)menu.connectionLost();}
                    if(network->connected&&!network->state.empty()){
                        auto& state=network->state;auto phase=network->phase();int selected=state.at("mission");
                        bool promoted=!menu.host&&network->hosting;menu.setLobbyHost(network->hosting);
                        if(menu.host&&seenPhase<0&&difficultySpecified)network->send({{"op","difficulty"},{"value",requestedDifficulty==World::Difficulty::Hard?"hard":"normal"}});
                        menu.lobbyCode=network->code;menu.localPlayer=network->id;menu.difficulty=state.at("difficulty")=="hard"?World::Difficulty::Hard:World::Difficulty::Normal;
                        int game=state.value("campaign",0);if(game!=menu.game){menu.game=game;menu.campaign=Progression{};menu.campaign.rules.load(retail,game);}
                        auto completed=state.at("completed").get<uint32_t>();
                        if(completed!=menu.campaign.completed){menu.campaign.completed=completed;menu.campaign.selectedMission=menu.campaign.nextMission();menu.campaign.loadout=menu.loadout;menu.campaign.sanitizeLoadout();menu.loadout=menu.campaign.loadout;}
                        if(promoted){menu.campaign.selectedMission=menu.campaign.nextMission();menu.campaign.loadout=menu.loadout;saveCampaign();menu.networkMessage="YOU ARE NOW THE LOBBY HOST";}
                        menu.missionChosen=phase!=coop::Phase::Selecting;menu.teamResult=phase==coop::Phase::Complete?1:phase==coop::Phase::Failed?2:0;
                        if(phase==coop::Phase::Complete&&seenPhase!=int(phase)){
                            int reward=menu.campaign.rules.rewards.at(selected);
                            menu.resultMessage=reward>=0?std::string(weaponInfo(Weapon(reward)).shortName)+" UNLOCKED FOR TEAM":"TEAM VICTORY - MISSION RECORDED";
                            if(menu.campaign.finished())menu.resultMessage+=" / CAMPAIGN COMPLETE";
                            menu.campaign.selectedMission=menu.campaign.nextMission();menu.campaign.loadout=menu.loadout;
                            if(menu.host)saveCampaign();else menu.saveMessage="HOST OWNS CAMPAIGN SAVE";
                        }
                        if(phase==coop::Phase::Playing&&network->round!=seenRound)menu.resultMessage.clear();
                        menu.remotePilots.clear();menu.roster.clear();
                        for(auto& member:state.at("players")){Pilot pilot;coop::pilotRead(member.at("pilot"),pilot);if(member.at("id")!=network->id)menu.remotePilots.push_back(pilot);menu.roster.push_back("P"+std::to_string(member.at("slot").get<int>()+1)+" "+member.at("name").get<std::string>()+(member.at("ready").get<bool>()?" READY":" NOT READY"));}
                        if(int(phase)!=seenPhase||selected!=seenMission||network->round!=seenRound||state.at("selection").get<uint64_t>()!=seenSelection){
                            if(phase==coop::Phase::Playing&&(seenPhase!=int(phase)||network->round!=seenRound))renderer.resetCockpit();
                            menu.mission=selected;menu.confirmHeld=true;menu.held=input.fire;menu.spectating=false;
                            if(phase==coop::Phase::Selecting){menu.screen=menu.host?Screen::Missions:Screen::Standby;menu.ready=false;menu.teamResult=0;menu.missionChosen=false;menu.resultMessage=state.value("outcome",std::string{});menu.networkMessage=menu.resultMessage;world.missile.active=false;world.resetTracking();menu.navHeld=menu.backHeld=true;}
                            else if(phase==coop::Phase::Briefing){menu.screen=Screen::Briefing;menu.ready=false;menu.networkMessage.clear();menu.resultMessage.clear();}
                            else if(phase==coop::Phase::Complete)menu.showDebrief(selected,input);
                            else menu.screen=Screen::Playing;
                            seenSelection=state.at("selection");seenPhase=int(phase);seenMission=selected;seenRound=network->round;suppressSearchlight=true;headYaw=headPitch=0;suppressFire=input.fire;suppressPing=input.ping;
                            if(!xr&&!frameLimit)SDL_SetRelativeMouseMode(menu.screen==Screen::Playing?SDL_TRUE:SDL_FALSE);
                        }
                        network->apply(world,retail,renderer.armRig());
                        menu.remotePilots.clear();for(const auto& pilot:world.mapPilots)if(pilot.playerSlot!=world.playerSlot)menu.remotePilots.push_back(pilot);
                        if(world.lifeSpent&&world.ejectionTime>=7&&phase==coop::Phase::Playing)menu.spectating=true;
                        if(menu.spectating&&input.turn>.7f&&!menu.spectatorHeld)++menu.spectator;
                        if(menu.spectating)menu.spectatorHeld=std::abs(input.turn)>.55f;
                        seenRevision=network->revision;
                    }
                    if(voice&&!voiceConfigured&&!network->voiceCredentials.empty()){voice->configure(network->voiceHost,network->voiceCredentials);voiceConfigured=true;}
                    if(voiceConfigured){auto participants=network->participants();avp::voice::Controls controls;controls.channel=avp::voice::Channel::Radio;controls.captureAllowed=input.focused;
                        voice->update(1,network->id,participants,world.eye(),world.facing(),network->connected,menu.micEnabled&&input.focused,controls,1);
                        menu.voiceMessage=menu.micEnabled?(voice->transmitting()?"VOICE TRANSMITTING":voice->status()):"MICROPHONE MUTED";}
                }
                if(menu.screen==Screen::Playing&&input.focused&&input.headValid&&confirm&&(world.missionStatus==MissionStatus::Complete||world.missionStatus==MissionStatus::Failed)){
                    if(prototypeSeed||assault)running=false;else if(network)missionMenu();else {if(world.missionStatus==MissionStatus::Complete)menu.acceptResult();else menu.screen=menu.skirmish?Screen::Skirmish:Screen::Missions;}
                    confirm=false;menu.confirmHeld=true;headYaw=headPitch=0;if(!xr)SDL_SetRelativeMouseMode(SDL_FALSE);
                }
                Screen beforeMenu=menu.screen;
                float previousSound=menu.soundVolume,previousMusic=menu.musicVolume;
                if(menu.update(input,dt,confirm,back)) {
                    if(!menu.skirmish){menu.campaign.selectedMission=menu.mission;menu.campaign.loadout=menu.loadout;saveCampaign();}deploy();world.wasPing=input.ping;suppressFire=input.fire;suppressPing=input.ping;
                    if(!xr&&!frameLimit)SDL_SetRelativeMouseMode(SDL_TRUE);
                }
                if(previousSound!=menu.soundVolume||previousMusic!=menu.musicVolume){
                    audio.setVolumes(menu.soundVolume,menu.musicVolume);config.soundVolume=menu.soundVolume;config.musicVolume=menu.musicVolume;
                    try{config.saveAudio(configPath);menu.audioMessage="AUDIO SETTINGS SAVED";}
                    catch(const std::exception& e){menu.audioMessage="AUDIO SETTINGS NOT SAVED";std::cerr<<"Audio settings: "<<e.what()<<'\n';}
                }
                if(network&&menu.host&&beforeMenu==Screen::Missions&&menu.screen==Screen::Briefing)network->send({{"op","select"},{"mission",menu.mission}});
                auto action=menu.action;menu.action=MenuAction::None;
                if(action==MenuAction::Exit)running=false;
                else if(action==MenuAction::Host||action==MenuAction::Join){
                    if(action==MenuAction::Host){menu.campaign=Progression{};menu.campaign.rules.load(retail,menu.game);if(!savePath.empty())menu.campaign.load(campaignSave());menu.restoreCampaign();}
                    connectLobby(action==MenuAction::Host,menu.joinCode);
                }
                else if(action==MenuAction::Skirmish){
                    if(network||voice)disconnectLobby();
                    menu.skirmish=true;menu.skirmishSettings=SkirmishSettings{};menu.skirmishSettings.seed=newSkirmishSeed();
                    skirmishRecipe.reset();prototypeSeed.reset();proceduralRecipe.clear();menu.prototypeBriefing.clear();
                    menu.mission=0;menu.loadout={Weapon::Empty,Weapon::Rifle,Weapon::Rail,Weapon::Shield,Weapon::Rocket,Weapon::Grenade};
                    menu.screen=Screen::Skirmish;menu.option=0;menu.networkMessage.clear();menu.saveMessage.clear();menu.resultMessage.clear();
                }
                else if(action==MenuAction::GenerateSkirmish){
                    try{skirmishRecipe=generateSkirmishRecipe(menu.skirmishSettings);World next;next.difficulty=menu.difficulty;next.configure(menu.loadout,renderer.armRig());loadSelectedMission(next);world=std::move(next);
                        menu.screen=Screen::Briefing;menu.networkMessage.clear();menu.resultMessage.clear();}
                    catch(const std::exception& error){skirmishRecipe.reset();menu.screen=Screen::Skirmish;menu.networkMessage="CHANGE SEED OR SETTINGS AND TRY AGAIN";std::cerr<<"Skirmish generation: "<<error.what()<<'\n';}
                }
                else if(action==MenuAction::Singleplayer||action==MenuAction::Load){
                    menu.skirmish=false;skirmishRecipe.reset();menu.prototypeBriefing.clear();
                    if(network||voice)disconnectLobby();
                    menu.networkMessage.clear();menu.campaign=Progression{};if(!fixture)menu.campaign.rules.load(retail,menu.game);
                    if(action==MenuAction::Load){if(!savePath.empty()&&std::filesystem::exists(campaignSave()))menu.campaign.load(campaignSave());else menu.networkMessage="NO SAVED GAME";}
                    if(menu.networkMessage!="NO SAVED GAME"){freshCampaign=action==MenuAction::Singleplayer;menu.restoreCampaign();menu.screen=Screen::Missions;menu.networkMessage.clear();if(!difficultySpecified)menu.difficulty=action==MenuAction::Load?menu.campaign.difficulty:World::Difficulty::Normal;}
                }
                else if(action==MenuAction::Ready&&network)network->send({{"op","ready"},{"ready",menu.ready},{"loadout",menu.loadout}});
                else if(action==MenuAction::Restart){if(network)network->send({{"op","restart"}});else deploy();}
                else if(action==MenuAction::Abort){if(prototypeSeed||assault)running=false;else if(network)network->send({{"op","abort"}});else{menu.screen=menu.skirmish?Screen::Skirmish:Screen::Missions;menu.option=0;world.missile.active=false;}}
                else if(action==MenuAction::Destroy){if(network)network->send({{"op","destroy"}});else {world.hurt(100000);if(assault)assault->pilots[assaultSlot].pilot=world;}menu.screen=Screen::Playing;}
                else if(action==MenuAction::Recenter){if(!menu.spectating&&!world.lifeSpent&&!world.missile.active&&world.missionStatus!=MissionStatus::Ejecting){anchored=false;headYaw=headPitch=0;world.resetTracking();suppressFire.fill(true);suppressPing.fill(true);suppressSearchlight=true;}menu.screen=Screen::Playing;menu.confirmHeld=true;}
                else if(action==MenuAction::Leave){if(prototypeSeed||assault)running=false;else disconnectLobby();}
                if(beforeMenu==Screen::Pause&&menu.screen==Screen::Playing){suppressFire=input.fire;if(!xr&&!frameLimit)SDL_SetRelativeMouseMode(SDL_TRUE);}
                if(!network&&menu.screen==Screen::Playing){
                    for(int m=0;m<MountCount;++m){if(!input.fire[m])suppressFire[m]=false;if(suppressFire[m])input.fire[m]=false;}
                    if(assault){
                        assault->pilots[assaultSlot].input=input;std::swap(world,assault->world);
                        if(dt>0&&dt<=.1f)assault->step(dt);
                        std::swap(world,assault->world);
                        static_cast<Pilot&>(world)=assault->pilots[assaultSlot].pilot;world.armRig=renderer.armRig();
                        menu.remotePilots.clear();world.mapPilots.clear();for(size_t i=0;i<8;++i){world.mapPilots.push_back(assault->pilots[i].pilot);if(i!=assaultSlot)menu.remotePilots.push_back(assault->pilots[i].pilot);}
                        if(assault->result!=AssaultResult::Playing){world.objective=assault->result==AssaultResult::AttackersWin?"ATTACKERS WIN":"DEFENDERS WIN";world.missionStatus=MissionStatus::Complete;}
                        else if(world.lifeSpent){world.missionStatus=MissionStatus::Ejecting;
                            world.objective=assault->pilots[assaultSlot].waiting?"RESPAWN IN "+std::to_string(int(std::ceil(assault->pilots[assaultSlot].respawnIn)))+" S":"ELIMINATED / TEAM STILL ACTIVE";}
                    }else world.step(input,dt);
                    if(!assault&&!prototypeSeed&&world.missionStatus==MissionStatus::Complete&&!world.lifeSpent&&!recordedCompletion){
                        bool awarded=menu.recordResult(world);
                        if(menu.skirmish){
                            menu.resultMessage="SKIRMISH COMPLETE";menu.saveMessage="CAMPAIGN PROGRESS UNCHANGED";
                            menu.prototypeBriefing=std::to_string(world.procedural.objectives.size())+" OBJECTIVES COMPLETE";
                            if(world.procedural.requireExtraction)menu.prototypeBriefing+=" / EXTRACTION COMPLETE";
                        }
                        else {
                            if(!awarded)menu.resultMessage=menu.sandbox?"SANDBOX - PROGRESS NOT SAVED":"MISSION ALREADY RECORDED";
                            menu.campaign.selectedMission=menu.campaign.nextMission();menu.campaign.loadout=menu.loadout;
                            if(!menu.sandbox)saveCampaign();
                        }
                        menu.showDebrief(world.mission,input);recordedCompletion=true;world.resetTracking();
                        if(!xr)SDL_SetRelativeMouseMode(SDL_FALSE);
                    }
                }else if(!network)world.resetTracking();
                if(!assault&&!network&&!ejectionDemo&&ejectionPreview<0&&menu.handleFailure(world,input)){
                    if(prototypeSeed)running=false;
                    world.missile.active=false;world.resetTracking();headYaw=headPitch=0;suppressFire=input.fire;
                    if(!xr)SDL_SetRelativeMouseMode(SDL_FALSE);
                }
                if(ejectionDemo&&menu.screen==Screen::Playing&&input.focused&&input.headValid&&dt>0&&dt<=.1f){
                    demoClock+=dt;
                    if(demoClock>=3&&!world.lifeSpent){world.hurt(10000);std::cout<<"Ejection demo: destruction triggered\n"<<std::flush;}
                    if(demoClock>=13){world=World{};world.configure(menu.loadout,renderer.armRig());if(!proceduralRecipe.empty())world.loadProceduralPrototype(retail,proceduralRecipe);else if(prototypeSeed)world.loadMissionPrototype(retail,*prototypeSeed);else if(!fixture)world.loadMission(retail,menu.mission,menu.game);renderer.resetCockpit();demoClock=0;std::cout<<"Ejection demo: new demonstration cycle\n"<<std::flush;}
                }
                if(healthPreview>=0)world.health=healthPreview;
                renderer.updateCockpit(world,dt,menu.screen==Screen::Playing&&!menu.spectating&&(network||input.focused));
                auto spectatorPose=[&](){
                    std::vector<const Pilot*> living;for(auto& p:menu.remotePilots)if(!p.lifeSpent&&p.health>0)living.push_back(&p);
                    if(living.empty())return multiply(translation(world.position.x,world.position.y+35,world.position.z+25),rotation(0,-.4f));
                    auto p=living[size_t(menu.spectator)%living.size()];auto pos=p->position-direction(p->facing(),0)*25+Vec3{0,22,0};return multiply(translation(pos.x,pos.y,pos.z),rotation(p->facing(),-.35f));
                };
                auto pilot=[&](){auto p=world.pilotPosition();auto q=world.pilotOrientation();return poseMatrix({{q.x,q.y,q.z,q.w},{p.x,p.y,p.z}});};
                auto base=[&](XrVector3f origin) {
                    if(menu.spectating&&menu.screen==Screen::Playing)return multiply(spectatorPose(),multiply(rotation(-referenceYaw),translation(-origin.x,-origin.y,-origin.z)));
                    if(world.lifeSpent&&menu.screen==Screen::Playing)return multiply(pilot(),multiply(rotation(-referenceYaw),translation(-origin.x,-origin.y,-origin.z)));
                    if(world.missile.active) {
                        const auto& m=world.missile;
                        return multiply(translation(m.position.x,m.position.y,m.position.z),multiply(rotation(m.yaw,m.pitch),inverseRigid(poseMatrix(xr->headPose()))));
                    }
                    Vec3 eye=world.missile.active?world.missile.position:world.eye();
                    float y=world.missile.active?world.missile.launchYaw-world.missile.headYaw:world.facing();
                    return multiply(translation(eye.x,eye.y,eye.z),multiply(rotation(y-referenceYaw),translation(-origin.x,-origin.y,-origin.z)));
                };
                if(xr) {
                    auto origin=world.missile.active?xr->headPose().position:anchor;
                    auto transform=base(origin);auto head=multiply(transform,poseMatrix(xr->headPose()));
                    auto listener=AudioListener::fromPose(head);
                    audio.update(world,menu.screen==Screen::Playing&&input.focused,menu.screen!=Screen::Playing&&input.focused,&listener);
                    xr->endFrame([&](const XrView& view,int,int,int){
                        renderer.draw(world,projection(view.fov,.05f,3000),multiply(transform,poseMatrix(view.pose)),head,paused||!xr->focused(),&menu);
                    });
                    glBindFramebuffer(GL_FRAMEBUFFER,0);
                    if(!input.headValid)SDL_Delay(8);
                } else {
                    Vec3 eye=world.missile.active?world.missile.position:world.eye();
                    float baseYaw=world.missile.active?world.missile.launchYaw-world.missile.headYaw:world.facing();
                    auto camera=multiply(translation(eye.x,eye.y,eye.z),world.missile.active?rotation(world.missile.yaw,world.missile.pitch):rotation(baseYaw+headYaw,headPitch));
                    if(world.lifeSpent&&menu.screen==Screen::Playing)camera=multiply(pilot(),rotation(headYaw,headPitch));
                    if(menu.spectating&&menu.screen==Screen::Playing)camera=multiply(spectatorPose(),rotation(headYaw,headPitch));
                    auto listener=AudioListener::fromPose(camera);
                    audio.update(world,menu.screen==Screen::Playing&&input.focused,menu.screen!=Screen::Playing&&input.focused,&listener);
                    int width,height;SDL_GL_GetDrawableSize(window,&width,&height);
                    if(width>0 && height>0) {
                        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width,height);
                        float angle=std::atan(std::tan(.65f)*float(width)/height);
                        renderer.draw(world,projection({-angle,angle,.65f,-.65f},.05f,3000),camera,camera,paused||!desktopFocus,&menu);
                    }
                }
                ++frames;
                if(!capture.empty() && frameLimit && frames==frameLimit) {
                    int width,height;SDL_GL_GetDrawableSize(window,&width,&height);screenshot(capture,width,height);
                }
                if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("OpenGL frame error");
                SDL_GL_SwapWindow(window);
            }
            std::cout<<"Frames="<<frames<<" destroyed="<<world.destroyed<<" torso_degrees="<<world.torsoYaw*180/pi<<"\n";
        } // XR swapchains and renderer must be destroyed before the GL context.
        SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return 0;
    } catch(const std::exception& error) {
#ifdef __ANDROID__
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"Iron Soldier XR: %s",error.what());
#else
        std::cerr<<"Iron Soldier XR: "<<error.what()<<"\n";
#endif
        if(context)SDL_GL_DeleteContext(context);if(window)SDL_DestroyWindow(window);SDL_Quit();return 1;
    }
}
