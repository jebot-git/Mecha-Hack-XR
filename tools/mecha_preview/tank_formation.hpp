// Record the host simulation so the same run can be reviewed in world and plan views.
static void captureTankFormation(Renderer &renderer,auto &mapView,Json &frames) {
    Session demo(64);
    demo.add(1,"Test Pilot");
    demo.phase=Phase::Combat;
    demo.mission.recipe.opener=5;
    demo.mission.openerTime=100;
    demo.world.mission=0;
    demo.world.rpgPrototype=true;
    demo.world.groundColor={.17f,.20f,.23f};
    auto &pilot=demo.member(1);
    pilot.body.position={0,0,-42};
    pilot.sheet.hp=pilot.sheet.maxHp=10000;
    const auto entries=enemyCatalog();
    const std::array ids={"anti_mecha_hovertank","heavy_hover_tank","anti_mecha_strike_team","anti_mecha_destruction_squad"};
    const std::array<Vec3,4> starts={Vec3{-6,0,0},{6,0,0},{-7,0,9},{7,0,9}};
    demo.mission.groups.emplace_back().route.push_back({0,0,0});
    for(size_t i=0;i<ids.size();++i){
        auto found=std::find_if(entries.begin(),entries.end(),[&](const EnemySpec &spec){return std::string_view(spec.id)==ids[i];});
        if(found==entries.end())throw std::runtime_error("Missing formation actor");
        Enemy enemy;
        enemy.spec=int(found-entries.begin());
        enemy.hp=enemy.maxHp=30;
        enemy.group=0;
        demo.mission.groups.front().actors.push_back(int(i));
        demo.enemies.push_back(enemy);
        demo.mission.recipe.enemies.push_back(enemy.spec);
        Target actor;
        actor.origin=starts[i];
        actor.spawnType=3;
        applyOriginalEnemy(actor,enemy.spec);
        demo.world.updateEnemyBounds(actor);
        demo.world.targets.push_back(actor);
    }
    for(int tick=0;tick<=1440;++tick){
        if(tick%30==0){
            auto scene=demo.view(1,renderer.armRig());
            Vec3 centre{};
            for(size_t i=0;i<ids.size();++i)centre=centre+scene.targets.at(i).origin;
            centre=centre*(1.f/float(ids.size()));
            float radius=10;
            for(size_t i=0;i<ids.size();++i)radius=std::max(radius,length(scene.targets[i].origin-centre));
            const Vec3 offset=unit(Vec3{.64f,.68f,.72f})*(radius*1.75f+5);
            mapView(scene,"formation-"+std::to_string(tick),centre+offset,Vec3{0,1,0}-offset,false);
            auto &sample=frames.back();
            sample["time"]=tick/60.f;
            sample["pilot"]={{"x",pilot.body.position.x},{"z",pilot.body.position.z}};
            for(size_t i=0;i<ids.size();++i){
                const auto &actor=scene.targets[i];
                sample["actors"].push_back({{"id",ids[i]},{"x",actor.origin.x},{"z",actor.origin.z},
                    {"hull",actor.yaw},{"turret",actor.ai.weaponYaw},{"phase",int(demo.enemies[i].vehiclePass.phase)}});
            }
        }
        if(tick<1440)demo.step(1.f/60);
    }
}
