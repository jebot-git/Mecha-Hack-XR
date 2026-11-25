#include "rpg/network.hpp"
#include "rpg/preparation.hpp"
#include "searchlight.hpp"
#include "rpg/manual_missions.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
using namespace iron;
using namespace iron::rpg;
static void submit(Session &game, uint32_t id, const Input &input) {
  game.command(id, {{"op", "input"},
                    {"epoch", game.epoch},
                    {"input", coop::inputWire(input)},
                    {"systems", input.ping}});
}
static Session deployed(int spec = 3) {
  Session game(42);
  game.add(1, "One");
  GameSettings settings;settings.maps=1;
  settings.enemy = spec;
  settings.environment = 3;
  settings.objective = 0;
  settings.opener = 5;
  settings.tactic = 8;
  settings.enemyCount = 1;
  game.command(1, {{"op", "settings"}, {"settings", settings}});
  game.command(1, {{"op", "ready"}, {"value", true}});
  assert(game.phase == Phase::Briefing);
  game.command(1, {{"op", "deploy"}});
  return game;
}
int main() {
  assert(enemyCatalog().size() == 66 && int(Module::Count) == 34 &&
         int(Quality::Count) == 21);
  assert(environmentCatalog().size() == 8 && enemyThemes().size() == 10 &&
         missionObjectives().size() == 10 && missionLocations().size() == 10 &&
         missionOpeners().size() == 6 && enemyTactics().size() == 10);
  for (size_t i = 0; i < enemyCatalog().size(); ++i) {
    auto &spec = enemySpec(i);
    assert(spec.hd >= 1 && spec.hd <= 10 && !spec.attacks.empty());
    auto game = deployed(int(i));
    assert(game.enemies[0].spec == int(i));
    // New definitions include melee-only predators and distant artillery.
    // Stage a real, clear engagement inside a stat-block weapon envelope.
    auto &pilot=game.member(1);auto &body=game.world.targets[0];bool staged=false;
    std::vector<Vec3> sites{body.origin,game.world.missionEntry};
    sites.insert(sites.end(),game.world.interiorNavNodes.begin(),game.world.interiorNavNodes.end());
    for(auto site:sites){
      site.y=game.world.surfaceHeight(site.x,site.z);body.origin=site;game.world.updateEnemyBounds(body);
      float actorRadius=std::clamp(std::hypot(body.half.x,body.half.z),3.5f,18.f);
      if(!clearPosition(game.world,site,actorRadius,0))continue;
      for(const auto &attack:spec.attacks){
        const float inner=attack.minimum+.1f,outer=attack.range-.1f;
        for(float fraction:{.6f,.8f,.95f,1.f}){
          float radius=std::lerp(inner,outer,fraction);
          for(int angle=0;angle<32&&!staged;++angle){float turn=float(angle)*2*pi/32;
            Vec3 p=site+Vec3{std::sin(turn)*radius,0,-std::cos(turn)*radius};p.y=game.world.surfaceHeight(p.x,p.z);
            const float range=length(p-site);
            if(range<inner||range>outer||!insideArea(p,game.settings.extent*.98f))continue;
            // Keep 3.5 m clearance from terrain and scenery. The moving pilot
            // uses a 2 m torso collision radius against the attacking actor.
            if(!clearPosition(game.world,p,3.5f,0)||!clearPosition(game.world,p,2.f))continue;
            Pilot observer;observer.position=site;
            if(!visibilityInRange(game.world,observer,p+Vec3{0,6,0})||!game.world.enemyLineOfSight(body.center,p+Vec3{0,6,0},0,-1))continue;
            bool reachable=true;auto previous=site;int samples=std::max(1,int(std::ceil(radius/2)));
            for(int n=1;n<=samples&&reachable;++n){auto next=site+(p-site)*(float(n)/samples);next.y=game.world.surfaceHeight(next.x,next.z);
              reachable=clearPosition(game.world,next,3.5f,0)&&std::abs(next.y-previous.y)<=.6f+.6f*std::hypot(next.x-previous.x,next.z-previous.z);previous=next;}
            if(!reachable)continue;
            pilot.body.position=p;staged=true;
          }
          if(staged)break;
        }
        if(staged)break;
      }
      if(staged)break;
    }
    if(!staged)throw std::runtime_error(std::string("No reachable visible firing fixture for ")+spec.id);
    game.enemies[0].fireClock=0;game.enemies[0].decisionClock=100;game.enemies[0].tactic=2;
    game.enemies[0].anchorX=body.origin.x;game.enemies[0].anchorZ=body.origin.z;
    const int durability=pilot.sheet.hp+pilot.sheet.ap;
    bool attacked=false;
    for(int tick=0;tick<1200&&!attacked&&game.phase==Phase::Combat;++tick){
      const auto &pattern=game.enemies[0].heavyAttack;
      const bool sweeping=pattern.pattern==HeavyPattern::Beam;
      const int pulse=pattern.pulse;
      const int beforeHit=pilot.sheet.hp+pilot.sheet.ap+pilot.shield;
      game.step(1.f/120);
      const bool beamHit=sweeping && pattern.pulse!=pulse &&
          pilot.sheet.hp+pilot.sheet.ap+pilot.shield<beforeHit;
      attacked=beamHit||std::any_of(game.ordnance.begin(),game.ordnance.end(),[](const auto &shot){return shot.body.owner==0;})||
          (game.enemies[0].grapplePilot==pilot.id&&pilot.sheet.hp+pilot.sheet.ap<durability)||
          pilot.body.rpgKnockdown.active(game.world.elapsed);
    }
    if(!attacked)throw std::runtime_error(std::string("No attack after 10 seconds for ")+spec.id);

  }
  Dice d{7};
  for (int c = 0; c < 4; ++c)
    for (int p = 0; p < 4; ++p) {
      auto sheet = createSheet(Chassis(c), PilotKind(p), d);
      validate(sheet);
      assert(sheet.level == 1 && sheet.modules.empty());
    }
  auto sheet = createSheet(Chassis::Striker, PilotKind::Engineer, d);
  auto weak = sheet, strong = sheet;
  weak.attributes[0] = 3;
  strong.attributes[0] = 25;
  assert(spreadRadians(weak, Gear::Rifle, 0) >
         spreadRadians(strong, Gear::Rifle, 0));
  assert(spreadRadians(sheet, Gear::Rifle, 1) >
         spreadRadians(sheet, Gear::Rifle, 0));
  for (int i = 0; i < 1000; ++i) {
    auto ray = deviated(d, {0, 0, -1}, .02f);
    assert(std::abs(length(ray) - 1) < .0001f &&
           dot(ray, {0, 0, -1}) >= std::cos(.0201f));
  }
  const auto reactor=sheet.reactor;
  for(int i=0;i<40;++i)sheet.reactorTest(d);
  assert(sheet.overheated&&sheet.reactorStress==1.f&&sheet.reactor==reactor);
  sheet.stepThermal(.6f);assert(sheet.overheated);
  sheet.stepThermal(.9f);assert(!sheet.overheated&&sheet.reactor==reactor);
  // Identical offline character imports into unrelated hosts without rerolling
  // attributes or HP.
  auto directory =
      std::filesystem::temp_directory_path() /
      ("mecha-profile-test-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  auto path = directory / "pilot.json";
  prepare(sheet);
  saveProfile(path, sheet, "Reusable");
  std::string savedName;
  auto loaded = loadProfile(path, &savedName);
  assert(savedName == "Reusable");
  assert(Json(loaded) == Json(sheet));
  {
    std::thread a([&]() { saveProfile(path, loaded, "Alpha"); });
    std::thread b([&]() { saveProfile(path, loaded, "Bravo"); });
    a.join();
    b.join();
    std::string name;
    auto complete = loadProfile(path, &name);
    assert(Json(complete) == Json(loaded) &&
           (name == "Alpha" || name == "Bravo"));
  }
  Session first(1), second(9999);
  first.add(1, "Pilot");
  second.add(7, "Pilot");
  first.command(1, {{"op", "configure"}, {"profile", loaded}});
  second.command(7, {{"op", "configure"}, {"profile", loaded}});
  auto firstPilot=Json(first.member(1).sheet),secondPilot=Json(second.member(7).sheet);firstPilot.erase("campaign");secondPilot.erase("campaign");
  assert(firstPilot==secondPilot&&first.member(1).sheet.campaign.seed==1&&second.member(7).sheet.campaign.seed==9999);
  auto prepared=Json(first.member(1).sheet);
  auto bad = loaded;
  bad.maxHp = 100000;
  bool rejected = false;
  try {
    first.command(1, {{"op", "configure"}, {"profile", bad}});
  } catch (...) {
    rejected = true;
  }
  assert(rejected && Json(first.member(1).sheet) == prepared);
  // Every catalog item is legal in an appropriate setup and supports a bounded
  // activation.
  for (int id = 1; id < int(Gear::Count); ++id) {
    auto game = deployed();
    auto &m = game.member(1);
    auto g = Gear(id);
    if(g==Gear::Comlink||retiredGear(g))continue; // Integrated hardware has no mount action.
    m.sheet = createSheet(Chassis::Striker, PilotKind::Engineer, d);
    m.sheet.equipment.fill(Gear::Empty);
    int slot=mountAllowed(g,RightHand)?RightHand:RightShoulder;
    m.sheet.equipment[slot] = g;
    m.sheet.modules.clear();
    if (gearInfo(g).module != Module::Count)
      m.sheet.modules.push_back(gearInfo(g).module);
    m.sheet.inventory.fill(2);
    normalizeHeavyEquipment(m.sheet);
    prepare(m.sheet);
    m.sheet.hp -= 2;
    m.sheet.ap = 0;
    validate(m.sheet);
    Input input;
    input.handPoses[0].position = {-.4f, -.13f, -.75f};
    input.handPoses[1].position = {.4f, -.13f, -.75f};
    submit(game, 1, input);
    game.step(1.f / 60);
    input.fire[slot] = true;
    submit(game, 1, input);
    game.step(1.f / 60);
    assert(std::isfinite(m.body.position.x) && m.sheet.hp >= 0);
  }
  // One-shot mode equipment keeps its exit control after consuming the item.
  {
    auto game = deployed();
    auto &m = game.member(1);
    m.sheet.equipment[RightShoulder] = Gear::OneShotModule;
    m.sheet.oneShotModule = Module::VehicleMode;
    m.sheet.inventory[size_t(Gear::OneShotModule)] = 1;
    game.enemies[0].disabled = 100;
    game.enemies[0].decisionClock = 100;
    game.enemies[0].fireClock = 100;
    Input in;
    submit(game, 1, in);
    game.step(1.f / 60);
    in.fire[RightShoulder] = true;
    submit(game, 1, in);
    game.step(1.f / 60);
    assert(m.vehicle && m.sheet.inventory[size_t(Gear::OneShotModule)] == 0);
    in.fire[RightShoulder] = false;
    for (int i = 0; i < 250; ++i) {
      submit(game, 1, in);
      game.step(1.f / 60);
    }
    in.fire[RightShoulder] = true;
    submit(game, 1, in);
    game.step(1.f / 60);
    assert(!m.vehicle);
  }
  // A physical contact always resolves damage even with minimum Power and
  // maximum defender Mobility.
  auto hits = deployed();
  auto &shooter = hits.member(1);
  shooter.sheet.attributes[0] = 3;
  hits.enemies[0].hp = hits.enemies[0].maxHp = 100;
  hits.world.targets[0].health = 100;
  hits.enemies[0].fireClock = 100;
  hits.enemies[0].decisionClock = 100;
  hits.enemies[0].disabled = 100;
  Shot shot;
  const auto frontal=direction(hits.world.targets[0].yaw,0);
  shot.body.position = hits.world.targets[0].center + frontal*20;
  shot.body.velocity = frontal*-2400;
  shot.body.damage = 7;
  shot.body.shooter = 1;
  hits.ordnance.push_back(shot);
  hits.step(1.f / 60);
  assert(hits.enemies[0].hp == 94); // recruit plating 1, no hit roll
  auto dodge = deployed();
  auto &victim = dodge.member(1);
  victim.sheet.attributes[1] = 32;
  victim.sheet.ap = 0;
  victim.sheet.hp = victim.sheet.maxHp = 25;
  Shot enemyShot;
  enemyShot.body.position = victim.body.position + Vec3{0, 6, -10};
  enemyShot.body.velocity = {0, 0, 1200};
  enemyShot.body.damage = 5;
  enemyShot.body.owner = 0;
  enemyShot.body.mount = -1;
  dodge.ordnance.push_back(enemyShot);
  dodge.step(1.f / 60);
  assert(victim.sheet.hp == 20); // no post-contact evasion roll
  // Physical cover blocks a shot before it reaches the pilot.
  auto cover = deployed();
  auto &covered = cover.member(1);
  covered.sheet.ap = 0;
  auto wall = cover.world.targets.size() - 8;
  cover.world.targets[wall].active = true;
  cover.world.targets[wall].health = 20;
  cover.world.targets[wall].center = covered.body.position + Vec3{0, 6, -5};
  Shot blocked;
  blocked.body.position = covered.body.position + Vec3{0, 6, -10};
  blocked.body.velocity = {0, 0, 1200};
  blocked.body.damage = 5;
  blocked.body.owner = 0;
  blocked.body.mount = -1;
  int hp = covered.sheet.hp;
  cover.ordnance.push_back(blocked);
  cover.step(1.f / 60);
  assert(covered.sheet.hp == hp && cover.world.targets[wall].health == 15);
  auto movement = deployed();
  auto &moving = movement.member(1);
  for (auto &e : movement.enemies) {
    e.fireClock = 100;
    e.decisionClock = 100;
    e.disabled = 100;
  }
  Input input;
  input.forward = -1;
  auto start = moving.body.position;
  for (int i = 0; i < 600; ++i) {
    submit(movement, 1, input);
    movement.step(1.f / 60);
  }
  assert(length(moving.body.position - start) > 50);
  auto before = moving.body.position;
  for (int i = 0; i < 60; ++i)
    movement.step(1.f / 60);
  auto stale = moving.body.position;
  assert(length(stale - before) < 2.1f);
  for (int i = 0; i < 60; ++i)
    movement.step(1.f / 60);
  assert(length(moving.body.position - stale) < .001f);
  assert(movement.hud(1).empty());
  // The left grip no longer invokes any pilot secondary ability.
  auto passive = deployed();
  auto &pilot = passive.member(1);
  pilot.sheet.pilot = PilotKind::Maverick;
  Input grip;
  submit(passive, 1, grip);
  passive.step(1.f / 60);
  auto ap = pilot.sheet.ap;
  grip.ping[0] = true;
  submit(passive, 1, grip);
  passive.step(1.f / 60);
  assert(pilot.sheet.ap == ap && pilot.sheet.hp > 0);
  // Whole environment geometry and obstacle snapshots reproduce on a client.
  auto environment = deployed();
  assert(environment.world.geometry().terrain.size() > 9000 &&
         environment.world.targets.size() > 40);
  assert(insideArea({900, 0, 900}, 1000) && !insideArea({1100, 0, 0}, 1000));
  Session view;
  view.readSnapshot(environment.snapshot());
  assert(view.snapshot() == environment.snapshot());
  // Completed missions stay explorable, and changing chassis preserves trained
  // attributes.
  auto cleared = deployed();
  for (size_t i = 0; i < cleared.enemies.size(); ++i) {
    cleared.enemies[i].hp = 0;
    cleared.world.targets[i].health = 0;
  }
  cleared.step(1.f / 60);
  assert(cleared.phase == Phase::Complete);
  Input walk;
  walk.forward = -1;
  auto from = cleared.member(1).body.position;
  for (int i = 0; i < 60; ++i) {
    submit(cleared, 1, walk);
    cleared.step(1.f / 60);
  }
  assert(length(cleared.member(1).body.position - from) > 4);
  Preparation prep;
  prep.sheet = loaded;
  prep.selected = 0;
  prep.edit(1);
  Dice design{42};
  const auto reference=createSheet(prep.sheet.chassis,prep.sheet.pilot,design);
  assert(prep.sheet.attributes==reference.attributes&&prep.sheet.maxHp==reference.maxHp);
  Session lobby;
  lobby.add(1, "Setup");
  for (int row = 0; row < prep.rowCount(); ++row) {
    prep.selected = row;
    prep.edit(1);
    validate(prep.sheet);
  }
  assert(!prep.lines(lobby, 1, "").empty());
  // Independent GM table rolls are deterministic and each override is exact.
  std::array<bool, 10> themes{}, objectives{}, places{}, tactics{};
  std::array<bool, 6> openers{};
  for (unsigned seed = 0; seed < 512; ++seed) {
    auto a = generateMission(seed, 0, GameSettings{}, 3),
         b = generateMission(seed, 0, GameSettings{}, 3);
    assert(Json(a) == Json(b));
    themes[a.theme] = objectives[a.objective] = places[a.location] =
        tactics[a.tactic] = true;
    openers[a.opener] = true;
  }
  for (auto seen : themes)
    assert(seen);
  for (auto seen : objectives)
    assert(seen);
  for (auto seen : places)
    assert(seen);
  for (auto seen : tactics)
    assert(seen);
  for (auto seen : openers)
    assert(seen);
  auto missionCase = [&](int objective, int opener = 5, int tactic = 8) {
    Session game(71);
    game.add(1, "Mission");
    GameSettings settings;settings.maps=1;
    settings.enemy = 3;
    settings.enemyCount = 1;
    settings.objective = objective;
    settings.opener = opener;
    settings.tactic = tactic;
    settings.location = 0;
    game.command(1, {{"op", "settings"}, {"settings", settings}});
    game.command(1, {{"op", "ready"}, {"value", true}});
    assert(game.phase == Phase::Briefing);
    auto before = game.snapshot();
    game.step(.1f);
    assert(game.snapshot() == before);
    game.command(1, {{"op", "deploy"}});
    game.enemies[0].disabled = 10000;
    return game;
  };
  // Every objective gets a distinct executable completion condition.
  for (int objective = 0; objective < 10; ++objective) {
    auto game = missionCase(objective);
    auto &player = game.member(1);
    auto visit = [&](Vec3 p) {
      player.body.position = p;
      game.step(.05f);
    };
    if (objective == 0 || objective == 6) {
      game.enemies[0].hp = 0;
      game.world.targets[0].health = 0;
      game.step(.05f);
    }
    if (objective == 1) {
      game.mission.progress = objectiveDuration(game.mission.recipe,90.f)-.01f;
      game.step(.05f);
    }
    if (objective == 2) {
      auto &asset = game.world.targets[game.mission.assets[0]];
      asset.origin = {0, 0, -game.settings.extent * .65f};
      asset.origin.y = game.world.surfaceHeight(asset.origin.x, asset.origin.z);
      visit(asset.origin + Vec3{10, 0, 0});
    }
    if (objective == 3 || objective == 4) {
      auto assets = game.mission.assets;
      for (int i : assets)
        visit(game.world.targets[i].origin);
      if (objective == 4) {
        assert(game.phase == Phase::Combat);
        Vec3 exit{0, 0, game.settings.extent * .65f};
        exit.y = game.world.surfaceHeight(exit.x, exit.z);
        visit(exit);
      }
    }
    if (objective == 5) {
      for (int i : game.mission.assets)
        game.world.targets[i].health = 0;
      game.step(.05f);
    }
    if (objective == 7 || objective == 8) {
      game.mission.progress = objectiveDuration(game.mission.recipe,45.f)-.01f;
      visit(game.world.targets[game.mission.assets[0]].origin + Vec3{10, 0, 0});
    }
    if (objective == 9) {
      game.enemies[0].hp = 1;
      game.world.targets[0].health = 1;
      visit(game.world.targets[0].origin + Vec3{10, 0, 0});
      assert(game.mission.recovered && game.phase == Phase::Combat);
      Vec3 exit{0, 0, game.settings.extent * .65f};
      exit.y = game.world.surfaceHeight(exit.x, exit.z);
      visit(exit);
    }
    assert(game.phase == Phase::Complete);
    Session copy;
    copy.readSnapshot(game.snapshot());
    assert(copy.snapshot() == game.snapshot());
    auto earned = game.salvage;
    for (auto &m : game.members)
      m.sheet.hp = 0;
    game.step(.05f);
    assert(game.phase == Phase::Complete && game.salvage == earned);
  }
  for (int objective : {1, 2, 8}) {
    auto game = missionCase(objective);
    game.world.targets[game.mission.assets[0]].health = 0;
    game.step(.05f);
    assert(game.phase == Phase::Defeat);
  }
  {
    auto game = missionCase(6);
    game.world.targets[0].origin.z = game.settings.extent * .81f;
    game.step(.05f);
    assert(game.phase == Phase::Defeat);
  }
  // Six openers change insertion presentation/state without mutating saved
  // profiles.
  for (int opener = 0; opener < 6; ++opener) {
    auto game = missionCase(0, opener);
    float floor = game.world.surfaceHeight(game.member(1).body.position.x,
                                           game.member(1).body.position.z);
    if (opener == 0)
      assert(std::abs(game.member(1).body.position.y-floor-dropEntryAltitude)<.01f);
    if (opener == 3)
      assert(std::abs(game.member(1).body.position.y-floor)<.01f);
    if (opener == 1)
      assert(game.world.night && game.member(1).sheet.hidden);
    if (opener == 4)
      assert(game.member(1).sheet.hidden);
    Session copy;
    copy.readSnapshot(game.snapshot());
    assert(copy.snapshot() == game.snapshot());
    for (int frame = 0; frame < int(std::ceil(dropInsertionDuration*60))+2; ++frame)
      game.step(1.f / 60);
    if (opener == 0 || opener == 3)
      assert(std::abs(game.member(1).body.position.y - floor) < .01f);
  }
  auto cautious =
      tacticalPlan(0, {0, 0, 0}, {0, 0, 20}, 30, 1, 0, 0, {0, 0, 0}, false);
  auto charge =
      tacticalPlan(1, {0, 0, 0}, {0, 0, 20}, 30, 1, 0, 0, {0, 0, 0}, false);
  assert(cautious.direction.z < 0 && charge.direction.z > 0 &&
         charge.preferMelee);
  assert(!tacticalPlan(9, {0, 0, 0}, {0, 0, 80}, 30, 0, 0, 0, {0, 0, 0}, false)
              .fire);
  assert(tacticalPlan(8, {0, 0, 0}, {0, 0, 30}, 30, 0, 0, 0, {0, 0, 0}, true)
             .spread < 1);
  for (int tactic = 0; tactic < 10; ++tactic) {
    auto game = missionCase(0, 5, tactic);
    game.enemies[0].disabled = 0;
    game.enemies[0].fireClock = 100;
    game.enemies[0].decisionClock = 100;
    auto &body = game.world.targets[0];
    float floor=game.world.surfaceHeight(0,0);
    body.origin = {0, floor, 0};
    game.world.updateEnemyBounds(body);
    game.enemies[0].anchorX = game.enemies[0].anchorZ = 0;
    for (size_t i = 1; i < game.world.targets.size(); ++i)
      game.world.targets[i].active = false;
    game.member(1).body.position = {0,game.world.surfaceHeight(0,20),20};
    game.step(.1f);
    if (tactic == 0)
      assert(body.origin.z < 0);
    if (tactic == 1)
      assert(body.origin.z > 0);
    if (tactic == 2)
      assert(length(body.origin-Vec3{0,floor,0}) < .01f);
    if (tactic == 3 || tactic == 4 || tactic == 5)
      assert(std::abs(body.origin.x) > .01f);
  }
  // Ready barrier, profile import, roster ownership and real loopback
  // transport.
  Server server("127.0.0.1", 0, 77);
  Client one("127.0.0.1:" + std::to_string(server.port()), "Alpha"),
      two("127.0.0.1:" + std::to_string(server.port()), "Bravo");
  auto pump = [&](int count) {
    for (int i = 0; i < count; ++i) {
      one.update(1.f / 60);
      two.update(1.f / 60);
      server.step();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  pump(60);
  assert(one.id && two.id && one.id != two.id);
  auto hostClient = server.session.host == one.id ? &one : &two;
  auto nonHost = server.session.host == one.id ? &two : &one;
  auto readyProfile=loaded;
  if(!readyProfile.created)finishCreation(readyProfile);
  one.command({{"op","configure"},{"profile",readyProfile}});
  two.command({{"op","configure"},{"profile",readyProfile}});
  pump(20);
  nonHost->command({{"op","sortie"},{"value",true}});
  nonHost->command({{"op","ready"},{"value",true}});
  pump(12);
  assert(server.session.phase==Phase::Hangar&&!server.session.member(nonHost->id).inSortie&&!nonHost->error.empty());
  nonHost->error.clear();
  hostClient->command({{"op","sortie"},{"value",true}});
  hostClient->command({{"op","ready"},{"value",true}});
  hostClient->command({{"op","deploy"}});
  pump(16);
  assert(server.session.phase==Phase::Hangar&&server.session.member(hostClient->id).ready&&!hostClient->error.empty());
  hostClient->error.clear();
  nonHost->command({{"op","sortie"},{"value",true}});
  nonHost->command({{"op","ready"},{"value",true}});
  pump(30);
  assert(server.session.phase==Phase::Briefing&&one.session.epoch==server.session.epoch);
  assert(one.session.member(two.id).inSortie&&one.session.member(two.id).ready);
  nonHost->command({{"op","ready"},{"value",false}});
  pump(15);
  assert(server.session.phase==Phase::Hangar&&server.session.member(hostClient->id).ready);
  assert(server.session.member(nonHost->id).inSortie&&!server.session.member(nonHost->id).ready);
  nonHost->command({{"op","ready"},{"value",true}});
  pump(20);
  nonHost->command({{"op","deploy"}});
  pump(10);
  assert(server.session.phase==Phase::Briefing&&!nonHost->error.empty());
  hostClient->command({{"op","deploy"}});
  pump(10);
  assert(server.session.phase == Phase::Combat &&
         one.session.mission.recipe.seed == two.session.mission.recipe.seed);
  nonHost->command({{"op", "settings"}, {"settings", GameSettings{}}});
  pump(10);
  assert(!nonHost->error.empty());
  std::filesystem::remove_all(directory);
  std::cout << "Physical accuracy/contact, full catalog, offline reuse, setup, "
               "GM objectives, openers, tactics and ENet checks passed\n";
}
