// Mission Manual v1.2, printed pp.86-96. Mechanical values only; authored
// dedicated authored meshes and rigs never copy book illustration pixels.
// Ranges use the existing XR 10/30/90/180 metre bands; 8 denotes all targets.
auto supplement=[&](const char *id,const char *name,int hd,EnemyKind kind,
                    float optimal,int plating,int shield,int acid,unsigned traits,
                    std::initializer_list<EnemyAttack> attacks,const char *model,
                    int shieldDice=1,int deathDice=0,int deathSides=6) {
  data.push_back({id,name,hd,kind,optimal,plating,shield,acid,traits,
                  std::vector<EnemyAttack>(attacks),shieldDice,deathDice,deathSides,model});
};
supplement("bomber","Bomber",1,EnemyKind::Conventional,30,0,4,0,Flying|Starship,
 {{"Machine Guns",1,4,1,0,30},
  {"Proton Bombs",1,6,8,0,30,5,AttackEffect::Damage,false,1,3}},"bomber");
supplement("cyclone_drone","Cyclone Drone",1,EnemyKind::Conventional,10,0,0,0,Submersible,
 {{"Ray Emitters",1,4,1,0,30},
  {"Cyclone Generator",1,6,8,0,10,5,AttackEffect::Immobilize,false,3}},"cyclone_drone");
supplement("interceptor","Interceptor",1,EnemyKind::Conventional,30,0,4,0,Flying|Starship,
 {{"Machine Guns",1,4,1,0,30},
  {"Heat-Seeker Missiles",1,8,1,0,30,5,AttackEffect::Damage,false,1,2}},"interceptor");
supplement("kaiju_flea","Kaiju Flea",1,EnemyKind::SmallKaiju,10,0,0,0,Swarm|Ambush,
 {{"Chittering Mandibles",1,4,1,0,10,0,AttackEffect::Damage,false,3},
  {"Miniature Maw Beam",1,6,1,0,30,0,AttackEffect::Damage,false,3}},"kaiju_flea");
supplement("sentry_gun","Sentry Gun",1,EnemyKind::Installation,30,0,0,0,0,
 {{"Point-Defense Turret",1,6,1,0,30},
  {"Rocket Salvo",1,8,2,10,90,5}},"sentry_gun");
supplement("swarm_drone","Swarm Drone",1,EnemyKind::SmallMecha,10,0,0,0,Swarm,
 {{"Cutting Claws",1,4,1,0,10,0,AttackEffect::Damage,false,3},
  {"Beam Spitters",1,6,1,0,30},
  {"Disruptor Pulse",1,4,1,0,10,5,AttackEffect::Reactor,false,2}},"swarm_drone");
supplement("aeon_diode","Aeon Diode",2,EnemyKind::SmallMecha,30,0,4,0,HiveMind,
 {{"Plasma Repeater",1,6,1,10,90},
  {"Beam Sword",1,8,1,0,10}},"aeon_diode");
supplement("neo_knight","Neo Knight",2,EnemyKind::SmallMecha,10,0,0,0,ShieldWall,
 {{"Lodium Broadsword",1,8,1,0,10,0,AttackEffect::Damage,false,1,0},
  {"Bolt-Thrower",1,6,1,10,90}},"neo_knight");
supplement("terran_grunt","Terran Grunt",2,EnemyKind::SmallMecha,30,2,0,0,0,
 {{"Thermal Hatchet",1,6,1,0,30},
  {"Rotary Cannon",1,6,2,10,90},
  {"Frag Launcher",1,10,2,10,30,5}},"terran_grunt");
supplement("deepdiver_submarine","Deepdiver Submarine",3,EnemyKind::Conventional,30,0,0,0,Submersible|Ambush,
 {{"Slipstream Torpedoes",1,8,2,0,30,0,AttackEffect::Damage,false,2},
  {"Sonar Pulse",0,4,8,0,30,5,AttackEffect::DisruptMobility,false,2}},"deepdiver_submarine");
supplement("veriform_mecha","Veriform Mecha",3,EnemyKind::SmallMecha,30,0,0,0,Veriform,
 {{"Wingblade",1,8,1,0,10},
  {"Swarm Missiles",1,4,8,10,90,5,AttackEffect::Damage,false,2}},"veriform_mecha");
supplement("verilian_guardian","Verilian Guardian",3,EnemyKind::SmallMecha,10,2,0,0,UnstableReactor,
 {{"Guardian Blade",1,8,1,0,10},
  {"Molten Cannon",1,10,1,10,90,5}},"verilian_guardian",1,1,6);
supplement("funnel_drone_mecha","Funnel Drone Mecha",4,EnemyKind::LargeMecha,30,0,0,0,Ace,
 {{"Beam Sword",1,8,1,0,10},
  {"Particle Cannon",1,10,1,10,90,0,AttackEffect::Damage,false,3},
  {"Funnel Drones",1,6,8,0,30,5,AttackEffect::Damage,false,2},
  {"Focused Funnel Drones",2,6,1,0,30,5,AttackEffect::Damage,false,2}},"funnel_drone_mecha");
supplement("gunboat","Gunboat",4,EnemyKind::Conventional,30,0,6,0,Flying|Starship,
 {{"Point-Defense Turrets",1,8,2,0,30},
  {"Swarm Missiles",1,6,8,10,90,5,AttackEffect::Damage,false,2}},"gunboat");
supplement("kaiju_dismantler","Kaiju Dismantler",4,EnemyKind::LargeKaiju,10,0,0,2,Dismantler,
 {{"Rending Mandibles",1,10,1,0,10,0,AttackEffect::Damage,false,0},
  {"Caustic Web",1,6,2,10,90,0,AttackEffect::Immobilize}},"kaiju_dismantler");
supplement("abyssal_kaiju","Abyssal Kaiju",5,EnemyKind::LargeKaiju,10,0,0,2,Submersible,
 {{"Coiling Tentacles",1,10,2,0,30,0,AttackEffect::Immobilize,false,0},
  {"Maw Beam",1,12,1,10,90},
  {"Screeching Sonar",0,4,8,0,30,5,AttackEffect::DisruptMobility,false,2}},"abyssal_kaiju");
supplement("heavy_hover_tank","Heavy Hover Tank",5,EnemyKind::Conventional,30,3,0,0,0,
 {{"Point-Defense Turrets",1,8,3,0,30},
  {"Thunder Cannon",1,12,1,10,90},
  {"Frag Mines",1,8,8,0,30,5,AttackEffect::Damage,false,2}},"heavy_hover_tank");
supplement("carrier","Carrier",5,EnemyKind::Conventional,30,0,8,0,Flying|Starship|Carrier,
 {{"Point-Defense Turrets",1,8,8,0,30}},"carrier");
supplement("artillery_battery","Artillery Battery",6,EnemyKind::Installation,90,0,8,0,Barricaded,
 {{"Point-Defense Turrets",1,8,8,0,30},
  {"Artillery Bombardment",2,6,8,30,180,5,AttackEffect::Damage,false,2}},"artillery_battery");
supplement("thunder_cannon_mecha","Thunder Cannon Mecha",6,EnemyKind::LargeMecha,30,3,0,0,0,
 {{"Vulcan Cannons",1,10,2,0,30},
  {"Thunder Cannon",2,6,1,10,90}},"thunder_cannon_mecha");
supplement("verilian_goliath","Verilian Goliath",6,EnemyKind::LargeMecha,10,2,0,0,UnstableReactor,
 {{"Fists",1,10,2,0,10},
  {"Molten Cannon",2,10,1,10,90},
  {"Graviton Beam",0,4,1,10,30,5,AttackEffect::Pull,false,0}},"verilian_goliath",1,1,8);
supplement("combiner_mecha","Combiner Mecha",7,EnemyKind::LargeMecha,30,0,0,0,Combiner,
 {{"Thermal Axe",1,10,1,0,10},
  {"Particle Cannon",2,8,1,10,90,0,AttackEffect::Damage,false,3}},"combiner_mecha");
supplement("cruiser","Cruiser",7,EnemyKind::Conventional,60,0,10,0,Flying|Starship,
 {{"Point-Defense Turrets",1,12,8,0,10},
  {"Swarm Missiles",1,8,8,10,90,0,AttackEffect::Damage,false,2},
  {"Hypermatter Cannon",2,10,1,10,90,5,AttackEffect::Damage,false,3}},"cruiser");
supplement("kaiju_broodqueen","Kaiju Broodqueen",7,EnemyKind::LargeKaiju,10,0,0,2,Carrier,
 {{"Rending Mandibles",1,12,1,0,10,0,AttackEffect::Damage,false,0},
  {"Caustic Spit",1,10,1,10,90},
  {"Piercing Screech",1,10,8,0,30,0,AttackEffect::Reactor,false,3}},"kaiju_broodqueen");
supplement("aeon_primarch","Aeon Primarch",8,EnemyKind::LargeMecha,30,0,8,0,Ace,
 {{"Beam Sword",2,6,1,0,10},
  {"Beam Rifle",2,8,1,10,90},
  {"Disruption Field",0,4,8,0,30,5,AttackEffect::DisruptSystems,false,2}},"aeon_primarch");
supplement("neo_paladin","Neo Paladin",8,EnemyKind::LargeMecha,10,0,0,0,Ace,
 {{"Lodium Greataxe",2,6,1,0,10,0,AttackEffect::Damage,false,1,0},
  {"Bolt-Repeater",1,10,3,10,90},
  {"Charge",2,10,1,10,90,5,AttackEffect::Push,false,3}},"neo_paladin");
supplement("terran_mammoth","Terran Mammoth",8,EnemyKind::LargeMecha,30,4,0,0,Ace,
 {{"Thermal Hatchets",1,10,2,0,30},
  {"Frag-Round Chaingun",2,8,2,10,90,0,AttackEffect::Ongoing},
  {"Swarm Missiles",1,10,8,10,90,5,AttackEffect::Damage,false,2}},"terran_mammoth");
supplement("aegis_mecha","Aegis Mecha",9,EnemyKind::LargeMecha,30,0,0,0,Ace|PairAegis,
 {{"Plasma Shield",1,10,1,0,30},
  {"Beam Pistol",2,8,2,10,90},
  {"Shield Overload",1,10,8,10,90,5,AttackEffect::Reactor,false,2}},"aegis_mecha");
supplement("gladius_mecha","Gladius Mecha",9,EnemyKind::LargeMecha,10,0,0,0,Ace|PairGladius,
 {{"Thermal Gladius",2,10,1,0,10},
  {"Beam Pistol",2,8,2,10,90}},"gladius_mecha");
supplement("kaiju_tunnel_wurm","Kaiju Tunnel Wurm",9,EnemyKind::LargeKaiju,10,0,0,4,Ambush,
 {{"Rending Maw",2,12,8,0,10},
  {"Consume",0,4,1,0,10,5,AttackEffect::Consume,false,0}},"kaiju_tunnel_wurm");
supplement("dreadnought","Dreadnought",10,EnemyKind::Conventional,60,0,10,0,Flying|Starship|Elite|Carrier,
 {{"Point-Defense Turrets",2,10,8,0,30},
  {"Swarm Missiles",1,12,8,10,90,0,AttackEffect::Damage,false,2},
  {"CK-90 Cannon",3,10,8,10,90,5,AttackEffect::Damage,false,3}},"dreadnought",2);
supplement("verilian_gargantuan","Verilian Gargantuan",10,EnemyKind::LargeMecha,30,8,0,0,Elite|UnstableReactor,
 {{"Fists",1,12,2,0,10},
  {"Molten Cannons",2,8,2,10,90},
  {"Graviton Surge",2,10,8,0,30,5,AttackEffect::Reactor,false,0}},"verilian_gargantuan",1,2,10);
