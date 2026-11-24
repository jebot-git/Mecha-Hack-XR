#include "rpg/session.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
using namespace iron;
using namespace iron::rpg;
int main(){try{
  Json results=Json::array();
  for(int region:{0,2,3}) {
    Session game(723);game.add(1,"CPU probe");GameSettings settings;
    settings.world=region;settings.location=region?0:7;settings.objective=0;
    settings.opener=5;settings.enemy=3;settings.enemyCount=12;
    game.command(1,{{"op","settings"},{"settings",settings}});
    game.command(1,{{"op","ready"},{"value",true}});game.command(1,{{"op","deploy"}});
    auto &pilot=game.member(1);pilot.sheet.hp=pilot.sheet.maxHp=1000000;
    pilot.body.position={-190,0,-200};
    game.environmentCycle.daySeconds=game.environmentCycle.weatherSeconds=1e9;
    for(unsigned i=0;i<1000&&game.environmentCycle.sample(game.world.climate).settings.weather!=Weather::Clear;++i)
      ++game.environmentCycle.seed;
    for(size_t i=0;i<game.enemies.size();++i) {
      auto &target=game.world.targets[i];target.origin={-350-float(i%3)*20,0,-280+float(i/3)*40};
      target.origin.y=game.world.surfaceHeight(target.origin.x,target.origin.z);
      game.world.updateEnemyBounds(target);
    }
    std::vector<double> samples;double total=0;Vec3 start=game.world.targets[0].origin;
    for(int i=0;i<600;++i) {
      auto before=std::chrono::steady_clock::now();game.step(1.f/60);
      auto after=std::chrono::steady_clock::now();
      double ms=std::chrono::duration<double,std::milli>(after-before).count();
      samples.push_back(ms);total+=ms;
    }
    std::sort(samples.begin(),samples.end());
    results.push_back({{"region",region},{"ticks",samples.size()},{"enemies",game.enemies.size()},
      {"targets",game.world.targets.size()},{"mean_ms",total/samples.size()},
      {"p95_ms",samples[size_t(samples.size()*.95)]},{"worst_ms",samples.back()},
      {"first_enemy_travel",length(game.world.targets[0].origin-start)}});
  }
  std::cout<<Json{{"platform","local desktop CPU; not Quest measurements"},{"results",results}}.dump(2)<<'\n';
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
