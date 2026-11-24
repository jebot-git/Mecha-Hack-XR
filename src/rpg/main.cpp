#include "render.hpp"
#include "audio.hpp"
#include "rpg/score.hpp"
#include "net/voice.hpp"
#include "cockpit_tracking.hpp"
#include "rpg/network.hpp"
#include "rpg/preparation.hpp"
#include "rpg/menu.hpp"
#include "config.hpp"
#include "rpg/profiles.hpp"
#include "rpg/mission_board.hpp"
#include "rpg/mission_intro.hpp"
#include "rpg/manual_missions.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/insertion.hpp"
#include "xr.hpp"
#include <SDL.h>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
using namespace iron;
using namespace iron::rpg;
using namespace avp;
static float yaw(const XrPosef &p) {
  auto q = p.orientation;
  return std::atan2(2 * (q.w * q.y + q.x * q.z),
                    1 - 2 * (q.x * q.x + q.y * q.y));
}
static float pitch(const XrPosef &p) {
  auto q = p.orientation;
  return std::asin(std::clamp(2 * (q.w * q.x - q.y * q.z), -1.f, 1.f));
}
static unsigned number(const std::string &s) {
  unsigned value = 0;
  auto parsed = std::from_chars(s.data(), s.data() + s.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size())
    throw std::runtime_error("Expected unsigned integer: " + s);
  return value;
}
static void screenshot(const std::string &path, int width, int height) {
  std::vector<unsigned char> pixels(size_t(width) * height * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
  std::ofstream out(path, std::ios::binary);
  if (!out)
    throw std::runtime_error("Cannot write screenshot");
  out << "P6\n" << width << ' ' << height << "\n255\n";
  for (int y = height - 1; y >= 0; --y)
    out.write(reinterpret_cast<const char *>(pixels.data() + y * width * 3),
              width * 3);
}
int main(int argc, char **argv) {
  SDL_Window *window = nullptr;
  SDL_GLContext context = nullptr;
  try {
#ifdef __ANDROID__
    const char *storage=SDL_AndroidGetInternalStoragePath();
    if(!storage)throw std::runtime_error("Android private storage is unavailable");
    setenv("XDG_DATA_HOME",storage,1);
    setenv("XDG_CONFIG_HOME",storage,1);
#endif
    bool useXR = true, hosting = false, autoReady = false, prepareOnly = false,
         explicitName = false, previewBriefing = false,explicitProfile=false,generationPreview=false;
    unsigned port = publicLobbyPort, seed = std::random_device{}(), frameLimit = 0;
    int selectedMission = 0;
    float ejectionPreview = -1, openerPreview = -1;
    float healthPreview = -1;
    std::string initialScreen = "missions";
    int chassis = -1, pilot = -1;
    auto profilePath = defaultProfilePath();
    std::filesystem::path profilesDirectory;
    GameSettings requestedSettings;
    std::string endpoint, name = "Pilot", capture;
    Json loadout;
    std::filesystem::path retail = originalAssetRoot();
#ifndef __ANDROID__
    if (char *base = SDL_GetBasePath()) {
      auto packaged = std::filesystem::path(base) / "assets" / "mecha";
      SDL_free(base);
      if (std::filesystem::is_regular_file(packaged / "runtime-models.json")) retail = packaged;
    }
#endif
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      auto value = [&]() {
        if (++i >= argc)
          throw std::runtime_error("Missing " + a);
        return std::string(argv[i]);
      };
      if (a == "--xr")
        useXR = true;
      else if (a == "--desktop")
        useXR = false;
      else if (a == "--host")
        hosting = true;
      else if (a == "--join")
        endpoint = value();
      else if (a == "--port")
        port = number(value());
      else if (a == "--voidmaw-combat-preview")continue; // Accepted by older launch scripts.
      else if (a == "--seed"){seed = number(value());generationPreview=true;}
      else if (a == "--name") {
        name = value();
        explicitName = true;
      } else if (a == "--frames")
        frameLimit = number(value());
      else if (a == "--screenshot")
        capture = value();
      else if (a == "--opener-preview") {
        openerPreview = std::stof(value());
        if (!std::isfinite(openerPreview) || openerPreview < 0 || openerPreview > 30)
          throw std::runtime_error("Opener preview must be 0..30 seconds");
        initialScreen = "playing";
        autoReady = true;
      }
      else if (a == "--ejection-preview") {
        ejectionPreview = std::stof(value());
        if (!std::isfinite(ejectionPreview) || ejectionPreview < 0 || ejectionPreview > 7)
          throw std::runtime_error("Ejection preview must be 0..7 seconds");
        initialScreen = "playing";
        autoReady = true;
      }
      else if (a == "--health-preview") {
        healthPreview = std::stof(value());
        if (!std::isfinite(healthPreview) || healthPreview < 0 || healthPreview > 100)
          throw std::runtime_error("Health preview must be 0..100 percent");
        initialScreen = "playing";
        autoReady = true;
      }
      else if (a == "--assets" || a == "--retail")
        retail = value();
      else if(a=="--profiles")profilesDirectory=value();
      else if (a == "--profile"){profilePath=value();explicitProfile=true;}
      else if(a=="--maps"){requestedSettings.maps=int(number(value()));generationPreview=true;}
      else if (a == "--world") {
        generationPreview=true;
        auto id=value();
        if(id=="aeon_prime")requestedSettings.world=0;
        else if(id=="slipstar")requestedSettings.world=1;
        else if(id=="hangars"||id=="fortress_husqvarna")requestedSettings.world=2;
        else if(id=="tunnels"||id=="verilian_tunnels")requestedSettings.world=3;
        else throw std::runtime_error("Unknown world; choose aeon_prime, slipstar, hangars or tunnels");
      }
      else if (a == "--mission") {
        selectedMission = int(number(value())) - 1;
      } else if (a.starts_with("--mission") && a.size()==10) {
        selectedMission = int(number(a.substr(9))) - 1;
      } else if (a == "--screen") {
        initialScreen = value();
        if(initialScreen != "missions" && initialScreen != "briefing" && initialScreen != "playing" && initialScreen != "preparation"&&initialScreen!="profiles"&&initialScreen!="creation")
          throw std::runtime_error("Screen must be profiles, creation, missions, preparation, briefing or playing");
        autoReady = initialScreen == "briefing" || initialScreen == "playing";
        previewBriefing = initialScreen == "briefing";
      } else if (a == "--prepare")
        prepareOnly = true;
      else if (a == "--briefing") {
        previewBriefing = true;
        autoReady = true;
        initialScreen = "briefing";
      } else if (a == "--enemy") {
        generationPreview=true;
        auto id = value();
        bool found = false;
        for (size_t k = 0; k < enemyCatalog().size(); ++k)
          if (id == enemySpec(k).id) {
            requestedSettings.enemy = int(k);
            found = true;
          }
        if (!found)
          throw std::runtime_error("Unknown enemy");
      } else if (a == "--auto-ready")
        autoReady = true;
      else if (a == "--chassis") {
        auto c = value();
        chassis=-1;
        for(int i=0;i<int(Chassis::Count);++i)if(c==chassisId(Chassis(i)))chassis=i;
        if(chassis<0)
          throw std::runtime_error("Unknown chassis");
      } else if (a == "--loadout") {
        std::ifstream file(value());
        if (!file)
          throw std::runtime_error("Cannot open RPG loadout");
        file >> loadout;
      } else if (a == "--pilot") {
        auto p = value();
        pilot=-1;
        for(int i=0;i<int(PilotKind::Count);++i)if(p==pilotId(PilotKind(i)))pilot=i;
        if(pilot<0)
          throw std::runtime_error("Unknown pilot");
      } else if (a == "--help") {
        std::cout
            << "mecha-hack-xr [--desktop|--xr] [--host|--join HOST:PORT] "
               "[--port 29597] [--name NAME]\n  [--chassis "
               "brawler|scout|striker|titan|auxiliary|colossus|hybrid|vanguard] [--pilot "
               "commander|engineer|maverick|quipster|bionic|merc|vet|pariah]\n  [--loadout FILE.json] "
               "[--assets PATH] [--profiles DIRECTORY] [--mission 1..9] "
               "\nVR controls: left stick moves or navigates; right stick turns torso; "
               "triggers use hands; Y/B shoulders; X/A hips; controller swing punches.\n"
               "Menu or left grip + Y opens pause; A/RT selects; B/LT goes back.\n"
               "Menus: left stick up/down selects a row, left/right changes its value.\n"
               "Equipment: LT/RT arms, Y/B shoulders, X/A hips; grip plus mount removes.\n"
               "Click and tilt the left stick to evade. Right stick click lifts. Combine them for a hotter, longer boost.\n"
               "Right stick down: headlight. Right stick up: rifle mode. Press the scanner mount button to scan.\n"
               "Hangar: missions, equipment, trade, pilots and options. Host can regenerate all nine missions before campaign progression begins.\n"
               "XR is the default; --desktop is a passive preview without keyboard or mouse controls.\n";
        return 0;
      } else
        throw std::runtime_error("Unknown argument " + a);
    }
    if(generationPreview&&(useXR||!frameLimit||hosting||!endpoint.empty()))throw std::runtime_error("Generation overrides are restricted to offline desktop captures");
    if (selectedMission < 0 || selectedMission >= missionBoardSize)
      throw std::runtime_error("Mission must be 1..9");
    if (port > 65535 || port == 0 || (hosting && !endpoint.empty()))
      throw std::runtime_error("Choose host or join and port 1..65535");
    if ((ejectionPreview >= 0 || healthPreview >= 0 || openerPreview >= 0) && (hosting || !endpoint.empty()))
      throw std::runtime_error("Visual previews are offline only");
    if(openerPreview>=0 && requestedSettings.opener<0)requestedSettings.opener=0;
    std::unique_ptr<ProfileStore> profiles;
    if((!explicitProfile||!profilesDirectory.empty())&&openerPreview<0){profiles=std::make_unique<ProfileStore>(profilesDirectory.empty()?defaultProfilePath().parent_path():profilesDirectory);profilePath=profiles->path(profiles->active());}
    bool profilePicker=bool(profiles)&&!autoReady&&!prepareOnly&&(initialScreen=="missions"||initialScreen=="profiles");
    int profileCursor=profiles?profiles->active():0;
    bool confirmProfileReset=false;
    int nextCampaignChassis=-1;
    Preparation preparation;
    preparation.randomState=std::random_device{}();
    preparation.settings = requestedSettings;
    if (std::filesystem::exists(profilePath)) {
      std::string savedName;
      preparation.sheet = loadProfile(profilePath, &savedName);
      if (!explicitName)
        name = savedName;
    } else {
      Dice d{std::random_device{}()};
      preparation.sheet = createSheet(Chassis::Striker, PilotKind::Engineer, d);
    }
    if(chassis>=0&&preparation.sheet.created&&int(preparation.sheet.chassis)!=chassis)throw std::runtime_error("Existing pilot chassis is locked; use a new profile");
    if (chassis >= 0&&!preparation.sheet.created) {
      preparation.selected = 0;
      while (int(preparation.sheet.chassis) != chassis)
        preparation.edit(1);
    }
    if(pilot>=0&&preparation.sheet.created&&int(preparation.sheet.pilot)!=pilot)throw std::runtime_error("Existing pilot archetype is locked; use a new profile");
    if (pilot >= 0&&!preparation.sheet.created) {
      preparation.selected = 1;
      while (int(preparation.sheet.pilot) != pilot)
        preparation.edit(1);
    }
    preparation.selected = 0;
    if (!loadout.is_null())
      customize(preparation.sheet, loadout);
    preparation.callsign=name;
    if(!preparation.sheet.created&&(autoReady||prepareOnly))finishCreation(preparation.sheet);
    if(openerPreview<0&&!profilePicker)saveProfile(profilePath, preparation.sheet, name);
    if (prepareOnly) {
      std::cout << "Saved offline profile: " << profilePath << "\n";
      return 0;
    }
    retail = gameAssetPath(retail);
    setOriginalAssetRoot(retail);
    Session local(seed);
    std::unique_ptr<Client> client;
    local.add(1,name);
    if(hosting)endpoint=publicLobbyEndpoint(port);
    if(!endpoint.empty())client=std::make_unique<Client>(endpoint,name);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
      throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
#ifdef __ANDROID__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
#ifndef __ANDROID__
    // SDL still supplies the OpenGL context used by OpenXR.
    if (useXR)
      windowFlags |= SDL_WINDOW_HIDDEN;
#endif
    window =
        SDL_CreateWindow("Mecha Hack XR", SDL_WINDOWPOS_CENTERED,
                         SDL_WINDOWPOS_CENTERED, 1280, 800,
                         windowFlags);
    if (!window)
      throw std::runtime_error(SDL_GetError());
    context = SDL_GL_CreateContext(window);
    if (!context)
      throw std::runtime_error(SDL_GetError());
#ifdef _WIN32
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
      throw std::runtime_error("GLEW initialization failed");
    while (glGetError() != GL_NO_ERROR) {
    }
#endif
    if (!useXR)
      SDL_GL_SetSwapInterval(1);
    {
      Renderer renderer(retail / "cockpit");
      renderer.prepareWorldMaterials(local.world);
      Audio audio(retail,true);
      std::unique_ptr<avp::voice::Chat> voice;
      bool voiceConfigured=false;
      auto startVoice=[&]{
        voice=std::make_unique<avp::voice::Chat>();
        if(audio.available())voice->shareOutput({[&](){return audio.voiceBytes();},[&](std::span<const float> pcm){audio.queueVoice(pcm);},[&](){audio.clearVoice();}});
      };
      if(client)startVoice();
      bool switchLobby=false;
      ClientConfig audioConfig;auto audioPath=defaultProfilePath().parent_path().parent_path()/"audio.json";
      try{audioConfig.load(audioPath);}catch(const std::exception &e){std::cerr<<"Audio preferences: "<<e.what()<<"\n";}
      PlayerMenu playerMenu;playerMenu.profilesAvailable=bool(profiles);MenuNavigation menuNavigation;
      if(!frameLimit&&!autoReady&&!profilePicker&&preparation.sheet.created)playerMenu.open(MenuPage::Hangar);
      Frontend frontend;frontend.soundVolume=audioConfig.soundVolume;frontend.musicVolume=audioConfig.musicVolume;
      frontend.applicationTitle = "MECHA HACK XR";
      frontend.screen = (initialScreen == "preparation"||initialScreen=="creation") ? Screen::Loadout : Screen::Missions;
      frontend.mission = selectedMission;
      frontend.missionCards = missionCards(generateMissionBoard(seed, preparation.settings));
      if(profilePicker)frontend.screen=Screen::Title;else if(!preparation.sheet.created)frontend.screen=Screen::Loadout;
      std::unique_ptr<XR> xr;
      if (useXR) {
        xr = std::make_unique<XR>(true);
        xr->initialize(window);
        std::cout << xr->description() << std::endl;
      }
      bool running=true,paused=false,focused=true,anchored=false,configured=false;
      float headYaw = 0, headPitch = 0, referenceYaw = 0, accumulator = 0;
      XrVector3f anchor{};
      const std::array<Vec3,2> hands{{{-.4f,-.13f,-.75f},{.4f,-.13f,-.75f}}};
      std::string status;
      unsigned frames = 0;
      bool pauseHeld=true,awaitCombatRelease=false,headlightDown=false,stickModeUp=false,rightStickArmed=false;
      int fieldRow=0;
      bool insertionPreviewPrimed=false,inventoryOpen=false,sortieView=false;
      std::array<bool,MountCount> equipHeld{};
      struct PendingPurchase {
        int credits,scrap,awardsSpent;
        std::string success;
        bool settled(const Sheet &sheet)const{
          return sheet.credits!=credits||sheet.inventory[size_t(Gear::Scrap)]!=scrap||
              sheet.campaignAwardsSpent!=awardsSpent;
        }
      };
      std::optional<PendingPurchase> pendingPurchase;
      auto last = std::chrono::steady_clock::now();
      uint64_t lastEpoch = 0, autoDeployedEpoch = 0, lastAudioEpoch = ~uint64_t(0);
      float lastAudioTime = -1;
      MissionBriefingHeader header;
      struct HeaderSelection {
        uint64_t epoch;
        uint32_t seed, revision;
        int slot, sector;
        bool operator==(const HeaderSelection &) const = default;
      };
      std::optional<HeaderSelection> headerSelection;
      std::chrono::steady_clock::time_point headerStarted;
      Phase previousPhase = Phase::Hangar;
      auto session = [&]() -> Session & {
        return client ? client->session : local;
      };
      auto id = [&]() { return client ? client->id : 1u; };
      auto command = [&](Json j) {
        try {
          if (client)
            client->command(j);
          else
            local.playerCommand(1, j);
        } catch (const std::exception &e) {
          status = e.what();
        }
      };
      uint32_t configuration=0;
      auto configure = [&](bool resume=false) {
        for(int slot=0;slot<MountCount;++slot)if(!playerEquipment(preparation.sheet.equipment[slot]))equipGear(preparation.sheet,slot,Gear::Empty);
        name=preparation.callsign;
        if(openerPreview<0)saveProfile(profilePath, preparation.sheet, name);
        configuration=std::max(configuration,session().member(id()).configuration)+1;
        command({{"op", "configure"}, {"profile", preparation.sheet},{"name",name},{"resume_campaign",resume},{"configuration",configuration}});
      };
      while (running && (!frameLimit || frames < frameLimit)) {
        bool confirm=false,rebuild=false,menuSelect=false,menuBack=false,pauseDown=false;
        int setupRow = 0, setupDelta = 0;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
          if (event.type == SDL_QUIT)
            running = false;
          if (event.type == SDL_WINDOWEVENT &&
              event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            focused = false;
          if (event.type == SDL_WINDOWEVENT &&
              event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
            focused = true;
        }
        if (!running)
          break;
        Input input;
        if (xr) {
          if (!xr->beginFrame())
            break;
          const auto &t = xr->tracking();
          input.focused = xr->focused() && !paused;
          input.headValid = t.headValid;
          const bool combat=session().phase==Phase::Combat&&!paused;
          const bool grip=t.squeezeHeld[0]||t.squeezeHeld[1];
          const bool mounts=inventoryOpen||(!profilePicker&&playerMenu.page==MenuPage::None&&
              session().phase==Phase::Hangar&&frontend.screen==Screen::Loadout&&
              preparation.sheet.created&&!preparation.trading&&!preparation.inventory&&!sortieView);
          input.thrust=combat&&t.recenter;
          headlightDown=headlightDown?t.turn.y<-.40f:
              t.turn.y<-.70f&&-t.turn.y>std::abs(t.turn.x);
          stickModeUp=stickModeUp?t.turn.y>.40f:t.turn.y>.70f&&t.turn.y>std::abs(t.turn.x);
          if(!combat||!input.focused||!t.headValid)rightStickArmed=false;
          else if(!headlightDown&&!stickModeUp)rightStickArmed=true;
          input.searchlight=rightStickArmed&&headlightDown;
          input.ascend=combat&&t.inventory;
          input.fireMode=rightStickArmed&&stickModeUp;
          const bool triggerPause=t.squeezeHeld[0]&&t.squeezeHeld[1]&&t.triggerHeld[0];
          pauseDown=combat&&(t.menu||(t.squeezeHeld[0]&&t.faceSecondary[0])||triggerPause);
          menuSelect=!combat&&!grip&&(t.facePrimary[1]||(!mounts&&t.triggerHeld[1]));
          menuBack=!combat&&!grip&&(t.faceSecondary[1]||(!mounts&&t.triggerHeld[0]));
          input.fire=mountButtons(t.triggerHeld[0]&&!(combat&&triggerPause),t.triggerHeld[1],
              t.facePrimary[1]&&(!mounts||grip),t.faceSecondary[1]&&(!mounts||grip),
              t.facePrimary[0],t.faceSecondary[0]&&!pauseDown);
          if(combat&&awaitCombatRelease){
            awaitCombatRelease=std::any_of(input.fire.begin(),input.fire.end(),[](bool pressed){return pressed;});
            input.fire.fill(false);
          }
          if (t.headValid) {
            auto head = xr->headPose();
            if (!anchored) {
              anchor = head.position;
              referenceYaw = yaw(head);
              anchored = true;
            }
            input.headYaw = wrap(yaw(head) - referenceYaw);
            input.headPitch = pitch(head);
            input.steer = t.move.x;
            input.forward = t.move.y;
            input.turn = t.turn.x;
            input.ping = {t.squeezeHeld[0], t.squeezeHeld[1]};

            for (int h = 0; h < 2; ++h) {
              auto &grip = t.grips[h];
              auto &aim = t.hands[h];
              auto q = angles(-referenceYaw);
              input.handValid[h] = t.gripValid[h];
              input.handPoses[h] = {
                  rotate(q, {grip.position.x - anchor.x,
                             grip.position.y - anchor.y,
                             grip.position.z - anchor.z}),
                  compose(q, {grip.orientation.x, grip.orientation.y,
                              grip.orientation.z, grip.orientation.w}),
                  compose(q, {aim.orientation.x, aim.orientation.y,
                              aim.orientation.z, aim.orientation.w}),
                  t.gripValid[h] && t.valid[h]};
              input.hands[h] = input.handPoses[h].position;
            }
          }
        } else {
          // Passive desktop rendering and scripted captures have no controls.
          input.focused=focused&&!paused;
          for(int hand=0;hand<2;++hand)input.handPoses[hand]={hands[hand],{}, {},true};
        }
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        dt = std::clamp(dt, 0.f, .1f);
        const bool pausePress=input.focused&&input.headValid&&pauseDown&&!pauseHeld;
        pauseHeld=!(xr?xr->focused():focused)||!input.headValid||pauseDown;
        if(pausePress)input.focused=false;
        if(switchLobby||(client&&client->disconnected())){
          bool connect=switchLobby&&!client;
          switchLobby=false;
          voice.reset();voiceConfigured=false;client.reset();audio.clearVoice();
          configured=false;configuration=0;pendingPurchase.reset();
          lastEpoch=autoDeployedEpoch=0;previousPhase=Phase::Hangar;
          lastAudioEpoch=~uint64_t(0);lastAudioTime=-1;
          paused=inventoryOpen=sortieView=false;anchored=false;
          header.stop();headerSelection.reset();
          playerMenu.open(MenuPage::Hangar);menuNavigation.blockButtons();
          if(connect){
            try{client=std::make_unique<Client>(publicLobbyEndpoint(),preparation.callsign);startVoice();status.clear();}
            catch(const std::exception &e){voice.reset();client.reset();status=e.what();}
          }
        }
        if (client) {
          client->update(dt, &input);
          if (!client->error.empty()) {
            status = client->error;
            client->error.clear();
            pendingPurchase.reset();
          }
        }
        auto &current = session();
        uint32_t player = id();
        bool present =
            player &&
            std::any_of(current.members.begin(), current.members.end(),
                        [&](auto &m) { return m.id == player; });
        bool authoritativeProfile=present&&configured&&current.member(player).configuration==configuration;
        bool ejectionActive = present && current.member(player).body.lifeSpent && current.member(player).body.ejectionTime < 7;
        if(ejectionActive)paused=false;
        bool uiFocus=(xr?xr->focused():focused)&&input.headValid;
        auto menuStep=menuNavigation.update(input.steer,input.forward,menuSelect,menuBack,uiFocus);
        confirm=menuStep.select;rebuild=menuStep.back;setupRow=menuStep.row;setupDelta=menuStep.change;
        if(present&&current.phase==Phase::Combat&&!ejectionActive&&!paused&&pausePress){paused=true;playerMenu.open(MenuPage::Pause);menuNavigation.blockButtons();confirm=false;rebuild=false;menuStep={};}
        if(present&&(current.phase==Phase::Complete||current.phase==Phase::Defeat||(current.campaign.awaitingAdvance&&!current.member(player).debriefAccepted)))playerMenu.open(MenuPage::None);
        if(present&&!profilePicker&&playerMenu.page!=MenuPage::None){
          float oldSound=frontend.soundVolume,oldMusic=frontend.musicVolume;
          auto action=playerMenu.update(menuStep,current.host==player&&canRegenerateCampaign(current),bool(client),frontend.soundVolume,frontend.musicVolume,frontend.micEnabled);
          if(frontend.soundVolume!=oldSound||frontend.musicVolume!=oldMusic){audioConfig.soundVolume=frontend.soundVolume;audioConfig.musicVolume=frontend.musicVolume;try{audioConfig.saveAudio(audioPath);status.clear();}catch(const std::exception&){status="AUDIO PREFERENCES COULD NOT BE SAVED";}}
          if(action==PlayerAction::Quit)running=false;
          else if(action==PlayerAction::Resume||action==PlayerAction::Recenter){
            if(action==PlayerAction::Recenter)anchored=false;
            awaitCombatRelease=true;
            input.fire.fill(false);
            paused=false;
            inventoryOpen=false;
            playerMenu.open(MenuPage::None);
          }
          else if(action==PlayerAction::NewCampaign){command({{"op","reroll_missions"}});frontend.mission=0;status="NEW CAMPAIGN READY";}
          else if(action==PlayerAction::Lobby){switchLobby=true;menuNavigation.blockButtons();}
          else if(action==PlayerAction::Pilots&&profiles){profilePicker=true;frontend.screen=Screen::Title;preparation.trading=preparation.inventory=false;command({{"op","sortie"},{"value",false}});playerMenu.open(MenuPage::None);}
          else if(action==PlayerAction::Missions){frontend.screen=preparation.sheet.created?Screen::Missions:Screen::Loadout;playerMenu.open(MenuPage::None);}
          else if(action==PlayerAction::Sortie){
            sortieView=true;frontend.screen=Screen::Pause;status.clear();playerMenu.open(MenuPage::None);
          }
          else if(action==PlayerAction::Equipment||action==PlayerAction::Inventory||action==PlayerAction::Trade){
            preparation.selected=0;preparation.trading=false;preparation.inventory=action==PlayerAction::Inventory;
            if(action==PlayerAction::Trade)preparation.openTrader();
            frontend.screen=Screen::Loadout;inventoryOpen=current.phase==Phase::Combat;fieldRow=0;equipHeld=input.fire;playerMenu.open(MenuPage::None);
          }
          confirm=rebuild=false;setupRow=setupDelta=0;
        }
        input.focused=(xr?xr->focused():focused)&&!paused;
        if(!present&&(confirm||rebuild))running=false;
        if(!running)break;
        if(profilePicker&&present){
          bool choose=confirm;
          if(confirmProfileReset){
            if(rebuild){confirmProfileReset=false;choose=false;}
            else if(choose){
              Dice dice{std::random_device{}()};
              profiles->reset(profileCursor,dice);
              confirmProfileReset=false;
            }
          }else{
            profileCursor=(profileCursor+setupRow+pilotProfileLimit)%pilotProfileLimit;
            if(rebuild){profilePicker=false;playerMenu.open(MenuPage::Hangar);choose=false;}
            else if(setupDelta>0&&profiles->load(profileCursor)){
              confirmProfileReset=true;choose=false;menuNavigation.blockButtons();
            }
          }
          if(choose){
            profiles->select(profileCursor);profilePath=profiles->path(profileCursor);
            auto saved=profiles->load(profileCursor);
            if(saved){preparation.sheet=saved->sheet;name=saved->callsign;}
            else{Dice d{std::random_device{}()};preparation.sheet=createSheet(Chassis::Striker,PilotKind::Engineer,d);name="Pilot "+std::to_string(profileCursor+1);}
            preparation.callsign=name;preparation.selected=0;preparation.trading=preparation.inventory=false;profilePicker=false;configured=false;authoritativeProfile=false;
            frontend.screen=preparation.sheet.created?Screen::Pause:Screen::Loadout;
            if(preparation.sheet.created)playerMenu.open(MenuPage::Hangar);
            frontend.held=input.fire;frontend.confirmHeld=true;equipHeld=input.fire;
          }
          confirm=false;rebuild=false;setupRow=setupDelta=0;
        }
        if (present&&!profilePicker) {
          if(current.phase==Phase::Hangar&&authoritativeProfile){
            const auto &live=current.member(player).sheet;
            if(pendingPurchase&&pendingPurchase->settled(live)){
              status=pendingPurchase->success;
              pendingPurchase.reset();
            }
            if(live.inventory!=preparation.sheet.inventory||live.owned!=preparation.sheet.owned||
               live.weaponCopies!=preparation.sheet.weaponCopies||live.credits!=preparation.sheet.credits||
               live.armor!=preparation.sheet.armor||live.ownedArmor!=preparation.sheet.ownedArmor||
               live.refinements!=preparation.sheet.refinements||live.campaignAwardsSpent!=preparation.sheet.campaignAwardsSpent){
              preparation.sheet=live;saveProfile(profilePath,live,name);
            }
          }
          if(pendingPurchase){
            if(current.phase!=Phase::Hangar)pendingPurchase.reset();
            else{
              confirm=rebuild=false;
              setupRow=setupDelta=0;
              equipHeld=input.fire;
            }
          }

          if (authoritativeProfile&&current.phase != previousPhase &&
              (current.phase == Phase::Complete ||
               current.phase == Phase::Hangar)) {
            if(current.phase==Phase::Complete)paused=true;
            preparation.sheet = current.member(player).sheet;
            prepare(preparation.sheet);
            if(openerPreview<0)saveProfile(profilePath, preparation.sheet, name);
          }
          if (current.phase == Phase::Hangar && previousPhase != Phase::Hangar){bool creation=!current.member(player).sheet.created;frontend.screen=creation?Screen::Loadout:Screen::Pause;paused=false;inventoryOpen=false;frontend.mission=current.selectedMission;preparation.selected=0;preparation.trading=preparation.inventory=false;playerMenu.open(creation||sortieView?MenuPage::None:MenuPage::Hangar);}
          previousPhase = current.phase;
          if((current.phase==Phase::Complete||current.phase==Phase::Hangar)&&authoritativeProfile&&
             (preparation.sheet.career.standings!=current.member(player).sheet.career.standings||preparation.sheet.career.heroFactions!=current.member(player).sheet.career.heroFactions||preparation.sheet.campaignAwards!=current.member(player).sheet.campaignAwards||Json(preparation.sheet.campaign)!=Json(current.member(player).sheet.campaign))){
            preparation.sheet=current.member(player).sheet;if(openerPreview<0)saveProfile(profilePath,preparation.sheet,name);
          }
          if (!configured) {
            configure(true);
            configured = true;
            if(generationPreview&&!client)local.command(1,{{"op","settings"},{"settings",preparation.settings}});
            if (current.host == player&&current.phase==Phase::Hangar)
              command(missionSelectionCommand(autoReady?selectedMission:current.selectedMission));
            if(autoReady&&current.phase==Phase::Hangar){
              command({{"op","sortie"},{"value",true}});
              command({{"op","ready"},{"value",true}});
            }
          }
          if(autoReady&&!previewBriefing&&current.phase==Phase::Briefing&&current.host==player&&autoDeployedEpoch!=current.epoch){
            autoDeployedEpoch=current.epoch;
            command({{"op","deploy"}});
          }
          if(current.phase==Phase::Combat){
            if(!paused)inventoryOpen=false;
            if(inventoryOpen){
              fieldRow=(fieldRow+setupRow+MountCount)%MountCount;
              if(setupDelta){preparation.selected=0;preparation.edit(setupDelta);}
              for(int mount=0;mount<MountCount;++mount)if(uiFocus&&input.fire[mount]&&!equipHeld[mount]){
                status.clear();command(preparation.equipmentCommand(mount,input.ping[0]||input.ping[1]));
              }
              equipHeld=input.fire;
              if(confirm){
                status.clear();
                command(preparation.equipmentCommand(equipmentPanelOrder[fieldRow]));
              }
              if(rebuild){inventoryOpen=false;playerMenu.open(MenuPage::Pause);menuNavigation.blockButtons();}
              confirm=rebuild=false;setupDelta=setupRow=0;
            }
          }
          if(current.phase==Phase::Hangar&&sortieView&&!current.campaign.awaitingAdvance){
            const auto &pilot=current.member(player);
            if(rebuild){
              if(pilot.inSortie)command({{"op","sortie"},{"value",false}});
              sortieView=false;status.clear();playerMenu.open(MenuPage::Hangar);
            }else if(confirm){
              status.clear();
              if(!pilot.inSortie)command({{"op","sortie"},{"value",true}});
              else command({{"op","ready"},{"value",!pilot.ready}});
            }
            confirm=rebuild=false;
          }
          if(current.phase==Phase::Hangar&&!sortieView&&!current.campaign.awaitingAdvance&&playerMenu.page==MenuPage::None){
            frontend.missionCards=campaignCards(current);frontend.mission=std::clamp(frontend.mission,0,int(frontend.missionCards.size())-1);
            if(frontend.screen==Screen::Missions||frontend.screen==Screen::Briefing){
              Input navigation=input;navigation.fire.fill(false);navigation.steer=float(setupDelta);navigation.forward=-float(setupRow);
              const auto before=frontend.screen;
              if(rebuild&&before==Screen::Missions){playerMenu.open(MenuPage::Hangar);rebuild=false;}
              if(before==Screen::Briefing&&confirm){
                sortieView=true;frontend.screen=Screen::Pause;status.clear();
              }else frontend.update(navigation,dt,confirm,rebuild);
              if(before==Screen::Missions&&frontend.screen==Screen::Briefing&&current.host==player)
                command(missionSelectionCommand(frontend.mission));
              confirm=rebuild=false;setupRow=setupDelta=0;
            }
            if(frontend.screen==Screen::Loadout&&rebuild){
              preparation.trading=preparation.inventory=false;playerMenu.open(MenuPage::Hangar);rebuild=confirm=false;
            }
          }
          if(current.phase==Phase::Hangar&&!current.campaign.awaitingAdvance&&frontend.screen==Screen::Loadout&&playerMenu.page==MenuPage::None){
            const bool host=current.host==player;
            if(setupRow||setupDelta)status.clear();
            if(setupRow)preparation.selected=(preparation.selected+setupRow+preparation.rowCount(host))%preparation.rowCount(host);
            bool changed=false;
            try{
              if(setupDelta)changed=preparation.edit(setupDelta,host);
              if(preparation.sheet.created&&!preparation.trading&&!preparation.inventory){
                for(int mount=0;mount<MountCount;++mount)if(uiFocus&&input.fire[mount]&&!equipHeld[mount]){
                  preparation.mount(mount,input.ping[0]||input.ping[1]);changed=true;
                }
              }
              equipHeld=input.fire;
              if(changed){configure();status.clear();}
            }catch(const std::exception &e){status=e.what();}
          }
          const bool menuFocus=uiFocus;
          if(current.phase==Phase::Complete && current.campaign.victorious && !current.member(player).debriefAccepted){
            if(nextCampaignChassis<0)nextCampaignChassis=int(current.member(player).sheet.chassis);
            nextCampaignChassis=(nextCampaignChassis+setupDelta+int(Chassis::Count))%int(Chassis::Count);
          }else nextCampaignChassis=-1;
          if (confirm &&
              (input.focused ||
               (paused && menuFocus && current.phase == Phase::Complete))) {
            if (current.phase == Phase::Briefing) {
              if (current.host == player)
                command({{"op", "deploy"}});
            } else if (current.phase == Phase::Hangar && !current.campaign.awaitingAdvance && frontend.screen == Screen::Loadout){
              try{bool before=preparation.sheet.created;
                auto transaction=preparation.tradeCommand();bool host=current.host==player;
                auto refill=preparation.refillCommand();
                const bool frame=transaction.is_object()&&transaction.at("action")=="frame_upgrade";
                const bool refining=transaction.is_object()&&
                    (transaction.at("action")=="refine_pilot"||transaction.at("action")=="tune");
                if(!refill.is_null()||frame||refining){
                  status.clear();
                  const auto &sheet=current.member(player).sheet;
                  PendingPurchase purchase{sheet.credits,sheet.inventory[size_t(Gear::Scrap)],sheet.campaignAwardsSpent,
                      refining?"REFINEMENT APPLIED":frame?"FRAME UPGRADED":"REFILLED +1"};
                  command(refill.is_null()?transaction:refill);
                  if(client&&client->connected){
                    pendingPurchase=std::move(purchase);
                    status=refining?"REFINING":frame?"UPGRADING":"REFILLING";
                  }
                  else if(!client&&status.empty()){
                    preparation.sheet=current.member(player).sheet;
                    saveProfile(profilePath,preparation.sheet,name);
                    status=purchase.success;
                  }
                }else if(!transaction.is_null()){command(transaction);if(!client){preparation.sheet=current.member(player).sheet;saveProfile(profilePath,preparation.sheet,name);}}
                else if(preparation.trading){
                  preparation.confirm(host);
                  if(!preparation.trading&&!preparation.inventory)playerMenu.open(MenuPage::Hangar);
                }else if(preparation.inventory){preparation.confirm(host);}
                else{
                  preparation.confirm(host);configure();
                  if(!before&&preparation.sheet.created){frontend.screen=Screen::Pause;playerMenu.open(MenuPage::Hangar);}
                }
              }catch(const std::exception &e){status=e.what();}
            } else if (current.phase == Phase::Complete||(current.phase==Phase::Hangar&&(current.campaign.awaitingAdvance&&!current.member(player).debriefAccepted))) {
              auto &member=current.member(player);
              if(!member.debriefAccepted){
                Json claim={{"op","debrief"}};
                if(nextCampaignChassis>=0)claim["chassis"]=nextCampaignChassis;
                command(std::move(claim));
              }
              else if(current.phase==Phase::Complete&&current.host==player){command({{"op","hangar"}});paused=false;}
            } else if (current.phase == Phase::Defeat && !ejectionActive && current.host==player)
              command({{"op", "hangar"}});
          }
          if(rebuild&&menuFocus&&current.phase==Phase::Briefing&&current.mission.mapIndex==0){
            sortieView=true;command({{"op","ready"},{"value",false}});
          }
          if(current.phase==Phase::Combat)sortieView=false;
          if (current.epoch != lastEpoch) {
            lastEpoch = current.epoch;
            renderer.resetCockpit();
            insertionPreviewPrimed=false;
            anchored = false;
            if(current.phase==Phase::Briefing){paused=false;inventoryOpen=false;playerMenu.open(MenuPage::None);}
            headYaw = headPitch = 0;
          }
        }
        accumulator += dt;
        int steps = 0;
        std::vector<SoundEvent> frameSounds;
        while (accumulator >= 1.f / 60 && steps++ < 6) {
          if (!client) {
            local.command(1, {{"op", "input"},
                              {"epoch", local.epoch},
                              {"input", coop::inputWire(input)},
                              {"systems", input.ping}});
            if (!paused || ejectionActive) {
              local.step(1.f / 60);
              const auto count = std::min(local.world.sounds.size(), size_t(128) - frameSounds.size());
              frameSounds.insert(frameSounds.end(), local.world.sounds.begin(), local.world.sounds.begin() + count);
            }
          }
          accumulator -= 1.f / 60;
        }
        // Checkpoint persistent field changes without writing every combat tick.
        // Profiles cannot switch after deployment, so these are the selected
        // pilot's authoritative stock, equipment and mission rewards.
        if(authoritativeProfile&&current.phase!=Phase::Hangar&&openerPreview<0){
          const auto &saved=preparation.sheet;const auto &live=current.member(player).sheet;
          if(saved.inventory!=live.inventory||saved.oneShotStock!=live.oneShotStock||saved.oneShotModule!=live.oneShotModule||saved.equipment!=live.equipment||saved.fittedCopies!=live.fittedCopies||saved.moduleCopies!=live.moduleCopies||saved.moduleCopies!=live.moduleCopies||saved.owned!=live.owned||saved.weaponCopies!=live.weaponCopies||saved.modules!=live.modules||saved.missionsCompleted!=live.missionsCompleted||saved.credits!=live.credits||saved.campaignAwards!=live.campaignAwards||Json(saved.campaign)!=Json(live.campaign)){
            preparation.sheet=live;saveProfile(profilePath,live,name);
          }
        }
        const float poseLead=current.phase!=Phase::Combat?0.f:
            client?client->poseLead():paused?0.f:std::min(accumulator,1.f/60);
        World world =
            present ? current.view(player, renderer.armRig(),poseLead) : World{};
        if(present && current.phase==Phase::Briefing)renderer.stageWorld(world);
        if (openerPreview >= 0 && current.phase == Phase::Combat) {
          // Offline capture samples a copied view only: never advance or damage
          // the authoritative mission, pilot profile, equipment or network state.
          world.rpgInsertionTime=openerPreview;
          world.rpgInsertionGround=world.surfaceHeight(world.position.x,world.position.z);
          bool enclosed=world.indoors({world.position.x,world.rpgInsertionGround+12,world.position.z});
          float entryWaterDepth=world.waterDepthAt(world.position);
          world.rpgInsertion=int(insertionKindForOpener(current.mission.recipe.opener,enclosed,entryWaterDepth));
          float shownY=world.rpgInsertionGround+sampleInsertion(InsertionKind(world.rpgInsertion),openerPreview,entryWaterDepth).altitude;
          float shiftY=shownY-world.position.y;
          world.position.y=shownY;
          for(auto& arm:world.arms){arm.shoulder.y+=shiftY;arm.elbow.y+=shiftY;arm.wrist.y+=shiftY;arm.muzzle.y+=shiftY;}
        }
        if (healthPreview >= 0 && current.phase == Phase::Combat)
          world.rpgArmor = world.health = healthPreview;
        // Capture-only presentation override; never destroys the saved pilot.
        if (ejectionPreview >= 0 && current.phase == Phase::Combat) {
          world.lifeSpent = true;
          world.health = 0;
          world.ejectionTime = ejectionPreview;
          world.missionStatus = MissionStatus::Ejecting;
        }
        if (!client) world.sounds = std::move(frameSounds);
        if(voice&&client->connected){
          if(!voiceConfigured&&!client->voiceCredentials.empty()){
            voice->configure(client->voiceHost,client->voiceCredentials);voiceConfigured=true;
          }
          if(voiceConfigured){
            auto participants=client->participants();avp::voice::Controls controls;
            controls.channel=avp::voice::Channel::Radio;
            controls.captureAllowed=(xr?xr->focused():focused)&&input.headValid;
            controls.malfunction=world.injured(MechaInjury::Comms);
            voice->update(current.epoch,client->id,participants,world.eye(),world.facing(),true,
                          frontend.micEnabled,controls,1);
            frontend.voiceMessage=!frontend.micEnabled?"MICROPHONE MUTED":controls.malfunction?"COMMS FAULT / SIGNAL DEGRADED":voice->transmitting()?"VOICE TRANSMITTING":voice->status();
          }
        }else if(voice){voice->close();voiceConfigured=false;}
        frontend.cooperative=bool(client);
        frontend.host=present&&current.host==player;
        frontend.remotePilots = world.mapPilots;
        audio.setVolumes(frontend.soundVolume,frontend.musicVolume);
        if(client && current.world.elapsed == lastAudioTime && current.epoch == lastAudioEpoch)world.sounds.clear();
        lastAudioTime=current.world.elapsed;lastAudioEpoch=current.epoch;
        const auto &recipe=current.mission.recipe;
        ScoreCue score=ScoreCue::Intermission;
        if(!present||profilePicker||!configured)score=ScoreCue::Main;
        else if(current.phase==Phase::Complete)score=current.campaign.victorious?ScoreCue::Clear:ScoreCue::Victory;
        else if(current.phase==Phase::Defeat)score=ScoreCue::Defeat;
        else if(current.phase==Phase::Briefing)score=factionScore(recipe.adversary,true);
        else if(current.phase==Phase::Combat&&!paused)
          score=recipe.boss>=0?ScoreCue::Boss:recipe.finaleBattle?ScoreCue::Finale:factionScore(recipe.adversary,false);
        float cockpitDt=dt;
        if(openerPreview>=0&&!insertionPreviewPrimed&&current.phase==Phase::Combat){
          cockpitDt=openerPreview;
          insertionPreviewPrimed=true;
        }
        renderer.updateCockpit(world,cockpitDt,(current.phase == Phase::Combat || current.phase == Phase::Complete || ejectionActive) && !paused);
        auto pilotArt=[&](const Sheet &sheet){
          static constexpr std::array<int,8> helmets{0,1,2,3,1,2,0,3};
          static_assert(helmets.size()==size_t(PilotKind::Count));
          frontend.pilotPortrait=helmets.at(size_t(sheet.pilot));
          frontend.serviceRank=uniformRank(sheet);
          frontend.pilotHonours=sheet.career.heroFactions;
        };
        pilotArt(present&&current.phase!=Phase::Hangar?current.member(player).sheet:preparation.sheet);
        frontend.interfaceArtSeed=current.seed^(current.boardRevision*0x9e3779b9u);
        auto portraitChassis=present&&current.phase!=Phase::Hangar?current.member(player).sheet.chassis:preparation.sheet.chassis;
        const std::string portraitName(chassisId(portraitChassis));
        uint32_t appearance=preparation.sheet.creationSeed;
        for(unsigned char c:preparation.callsign)appearance=(appearance^c)*16777619u;
        bool portraitAlt=present&&current.phase!=Phase::Hangar?world.rpgHullVariant:
          originalVariant(std::string("chassis_")+portraitName,appearance).ends_with("_alt");
        frontend.chassisPortrait=std::string("portrait-")+portraitName+(portraitAlt?"-1":"-0");
        frontend.interfaceArt=profilePicker?"profile":present&&preparation.trading?"trader":
          current.phase==Phase::Complete?"debrief":
          present&&!current.member(player).sheet.created?"creation":current.phase==Phase::Defeat?"defeat":"hangar";
        frontend.generatedFinale=campaignFinale(current.campaign);
        frontend.generatedCompleted=frontend.generatedFinale?0:current.campaign.completed;
        frontend.menuLines.clear();
        frontend.message.clear();
        frontend.equipmentModel.clear();
        frontend.equipmentPanel.reset();
        frontend.briefingHeader=false;
        if(!present){frontend.screen=Screen::Connecting;frontend.menuLines={"CONNECTING TO HOST",status.empty()?"WAITING FOR CONNECTION":status};frontend.message="A OR B: EXIT";}
        else if(profilePicker){
          frontend.screen=Screen::Title;
          if(confirmProfileReset){
            const auto pilotToReset=profiles->load(profileCursor);
            if(pilotToReset)pilotArt(pilotToReset->sheet);else frontend.pilotPortrait=-1;
            frontend.menuLines={"RESET PILOT "+std::to_string(profileCursor+1)+(pilotToReset?" / "+pilotToReset->callsign:""),
              "Erase this pilot's career, standings, awards and equipment?"};
            frontend.message="A / RT: ERASE PILOT   B / LT: CANCEL";
          }else{
            std::optional<PilotProfile> inspected;
            frontend.menuLines=profiles->lines(profileCursor,&inspected);
            if(inspected)pilotArt(inspected->sheet);else frontend.pilotPortrait=-1;
            frontend.message="UP/DOWN: PILOT   A / RT: SELECT   RIGHT: RESET   B: BACK";
          }
        }
        else if(present&&playerMenu.page!=MenuPage::None){frontend.screen=Screen::Pause;frontend.menuLines=playerMenu.lines(current,player,frontend.soundVolume,frontend.musicVolume,frontend.micEnabled,bool(client));frontend.message=menuHint;if(!status.empty())frontend.menuLines.push_back(status);}
        else if(present&&(current.phase==Phase::Complete||(current.phase==Phase::Hangar&&(current.campaign.awaitingAdvance&&!current.member(player).debriefAccepted)))){
          frontend.screen=Screen::Debrief;auto &member=current.member(player);
          frontend.menuLines=campaignDebriefLines(current,player);
          int accepted=int(std::count_if(current.members.begin(),current.members.end(),[](const auto &p){return p.debriefAccepted;}));frontend.menuLines.push_back("PARTY DEBRIEF: "+std::to_string(accepted)+" / "+std::to_string(current.members.size())+" ACCEPTED");
          frontend.message=member.debriefAccepted?(current.host==player?"A: RETURN TO HANGAR":"WAITING FOR HOST"):current.campaign.victorious?
              std::string("NEXT MECHA < ")+chassisInfo(nextCampaignChassis>=0?Chassis(nextCampaignChassis):member.sheet.chassis).name+" >   A: CLAIM AWARD":"A: ACCEPT";
          if(!status.empty())frontend.menuLines.push_back(status);
        }
        else if(inventoryOpen&&present){
          frontend.screen=Screen::Loadout;frontend.equipmentModel=preparation.previewModel(&frontend.equipmentHandling);
          const int fieldSlot=equipmentPanelOrder[fieldRow];
          frontend.equipmentPanel=preparation.loadoutPanel(current.member(player).sheet,fieldSlot,status);
          frontend.message="STICK: MOVE / ITEM   GRIP + MOUNT: REMOVE   B: BACK";
        }
        else if(current.phase==Phase::Hangar&&sortieView){
          frontend.screen=Screen::Pause;
          const auto &pilot=current.member(player);
          const bool opened=current.member(current.host).inSortie;
          frontend.menuLines={"SORTIE ROOM",manualMissionName(campaignMission(current,current.selectedMission))};
          for(const auto &other:current.members)frontend.menuLines.push_back(other.name+
              (other.id==current.host?"  /  HOST":"")+(other.ready?"  /  READY":other.inSortie?"  /  PRESENT":"  /  HANGAR"));
          if(!status.empty())frontend.menuLines.push_back(status);
          frontend.message=pilot.inSortie?(pilot.ready?"A: UNREADY   B: LEAVE":"A: READY   B: LEAVE"):
              opened?"A: ENTER   B: BACK":current.host==player?"A: OPEN   B: BACK":"WAITING FOR HOST   B: BACK";
        }
        else if (current.phase == Phase::Briefing) {
          frontend.screen = Screen::Briefing;
          frontend.mission = current.selectedMission;
          frontend.missionCards = campaignCards(current);frontend.mission=std::clamp(frontend.mission,0,int(frontend.missionCards.size())-1);
          auto &card=frontend.missionCards.at(frontend.mission);
          card.title=manualMissionName(current.mission.recipe);
          card.briefing=briefing(current.mission.recipe,&current.environmentCycle);
          card.art=std::string("briefing-")+missionLocations()[current.mission.recipe.location].id+"-"+std::to_string(current.mission.recipe.seed&1u);
          frontend.menuLines=card.briefing;
          if(current.mission.mapCount>1)frontend.menuLines.push_back(
              "SECTOR "+std::to_string(current.mission.mapIndex+1)+" / "+std::to_string(current.mission.mapCount));
          frontend.message=current.host==player?"A: DEPLOY":"READY / WAITING FOR HOST";
          if(current.mission.mapIndex==0)frontend.message+="   B: UNREADY";
        } else if (current.phase == Phase::Hangar && present) {
          if(frontend.screen==Screen::Debrief&&!current.campaign.awaitingAdvance)frontend.screen=Screen::Missions;
          if (previousPhase == Phase::Hangar && frontend.screen == Screen::Playing)
            frontend.screen = Screen::Missions;
          if(frontend.screen == Screen::Loadout) {
            frontend.equipmentModel=preparation.previewModel(&frontend.equipmentHandling);
            if(preparation.sheet.created&&!preparation.trading&&!preparation.inventory){
              frontend.equipmentPanel=preparation.loadoutPanel(preparation.sheet,-1,status);
              frontend.message="STICK: MOVE / CHANGE   GRIP + MOUNT: REMOVE   B: BACK";
            }else{
              frontend.menuLines=preparation.lines(current,player,status);
              frontend.message=preparation.inventory?"STICK: BROWSE   B: BACK":menuHint;
            }
          } else frontend.message=frontend.screen==Screen::Briefing?"A: SORTIE ROOM   B: MISSIONS":menuHint;
        } else if (current.phase == Phase::Complete && paused) {
          frontend.screen = Screen::Pause;
          frontend.menuLines = {"MISSION COMPLETE",
                 "A: DEBRIEF",
                 "RETURN TO HANGAR AFTER DEBRIEF"};
        } else if (current.phase == Phase::Defeat && !ejectionActive) {
          frontend.screen = Screen::Pause;
          frontend.menuLines = {"FIRETEAM DISABLED",current.host==player?"A: RETURN TO HANGAR":"WAITING FOR HOST TO RETURN TO HANGAR"};
        } else if (!present) {
          frontend.screen = Screen::Connecting;
          frontend.menuLines = {"CONNECTING TO HOST"};
        } else if(paused) {
          frontend.screen = Screen::Pause;
          playerMenu.open(MenuPage::Pause);frontend.menuLines=playerMenu.lines(current,player,frontend.soundVolume,frontend.musicVolume,frontend.micEnabled,bool(client));frontend.message=menuHint;
        } else frontend.screen = Screen::Playing;
        if(present&&frontend.screen==Screen::Briefing){
          const bool offer=current.phase==Phase::Hangar;
          // Negative sector distinguishes an offer being read on the board from a
          // committed first-sector briefing, including for non-host pilots.
          const HeaderSelection selected{current.epoch,current.seed,current.boardRevision,
              frontend.mission,offer?-1:current.mission.mapIndex};
          const auto now=std::chrono::steady_clock::now();
          if(headerSelection!=selected){
            headerSelection=selected;
            headerStarted=now;
            if(offer)header.open(retail/"briefing-headers",campaignMission(current,selected.slot),0);
            else header.open(retail/"briefing-headers",current.mission.recipe,selected.sector);
          }
          if(header.update(std::chrono::duration<double>(now-headerStarted).count()))
            renderer.uploadBriefingHeader(MissionBriefingHeader::width,MissionBriefingHeader::height,header.pixels());
          frontend.briefingHeader=header.active();
        }else if(headerSelection){
          header.stop();
          headerSelection.reset();
        }
        if (!status.empty() && current.phase == Phase::Combat&&!inventoryOpen)
          std::cerr << status << "\n", status.clear();
        const auto eye=world.lifeSpent?world.pilotPosition():world.eye();
        const auto frame=world.lifeSpent?world.pilotOrientation():
            world.rpgDroneView?angles(world.rpgDroneYaw,world.rpgDronePitch):world.cockpitOrientation();
        const auto bodyFrame=poseMatrix({{frame.x,frame.y,frame.z,frame.w},{eye.x,eye.y,eye.z}});
        const auto transform=xr?multiply(bodyFrame,multiply(rotation(-referenceYaw),
            translation(-anchor.x,-anchor.y,-anchor.z))):bodyFrame;
        const auto head=multiply(transform,xr?poseMatrix(xr->headPose()):rotation(headYaw,headPitch));
        const auto listener=AudioListener::fromPose(head);
        if(!input.focused||inventoryOpen)world.rpgAudioWeapons.fill(Weapon::Empty);
        audio.update(world,(current.phase==Phase::Combat||current.phase==Phase::Complete||ejectionActive)&&!paused,
                     false,&listener,int(score));
        current.world.sounds.clear();
        if (xr) {
          xr->endFrame([&](const XrView &view, int eyeIndex, int width, int height) {
            renderer.draw(world, projection(view.fov, .05f, 3000),
                             multiply(transform, poseMatrix(view.pose)), head,
                             paused, &frontend);
            if (eyeIndex == 0 && !capture.empty() && frameLimit && frames + 1 == frameLimit)
              screenshot(capture, width, height);
          }, false);
          glBindFramebuffer(GL_FRAMEBUFFER, 0);
        } else {
          int width, height;
          SDL_GL_GetDrawableSize(window, &width, &height);
          if (width > 0 && height > 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, width, height);
            float angle = std::atan(std::tan(.65f) * float(width) / height);
            renderer.draw(
                world, projection({-angle, angle, .65f, -.65f}, .05f, 3000),
                head, head, paused, &frontend);
          }
        }
        ++frames;
        if (!xr && !capture.empty() && frameLimit && frames == frameLimit) {
          int width, height;
          SDL_GL_GetDrawableSize(window, &width, &height);
          screenshot(capture, width, height);
        }
        if (glGetError() != GL_NO_ERROR)
          throw std::runtime_error("RPG OpenGL frame error");
        if (!xr)
          SDL_GL_SwapWindow(window);
      }
      std::cout << "RPG frames=" << frames << " epoch=" << session().epoch
                << " combat_seconds=" << session().clock << std::endl;
    }
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Mecha Hack XR: " << e.what() << '\n';
    if (context)
      SDL_GL_DeleteContext(context);
    if (window)
      SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
}
