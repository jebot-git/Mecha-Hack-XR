// Native renderer QA only. No network, pilot profile or gameplay state is saved.
#include "render.hpp"
#include "rpg/catalog.hpp"
#include "rpg/enemy_effects.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/session.hpp"
#include "rpg/generation.hpp"
#include "rpg/world_generation.hpp"
#include "openers.hpp"
#include "destruction.hpp"
#include <SDL.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace iron;
using namespace iron::rpg;
using namespace avp;
using Json = nlohmann::json;

static void capture(const std::filesystem::path& path,int width,int height) {
    std::vector<unsigned char> data(size_t(width)*height*3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,data.data());
    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Native gallery GL error");
    std::ofstream file(path,std::ios::binary);
    file<<"P6\n"<<width<<' '<<height<<"\n255\n";
    for(int y=height-1;y>=0;--y)file.write(reinterpret_cast<char*>(data.data()+y*width*3),width*3);
    if(!file)throw std::runtime_error("Native gallery capture failed");
}
#include "menus.hpp"
#include "cockpit_interiors.hpp"
#include "weapon_workings.hpp"
#include "terrain_gallery.hpp"
#include "crossings.hpp"
#include "weather_fall.hpp"
#include "tank_formation.hpp"
int main(int argc,char** argv) {try {
    std::filesystem::path assets=originalAssetRoot(),out="artifacts/validation/native-gallery";
    std::filesystem::path environmentCatalogPath;
    bool crossings=false,weatherFall=false,cockpitFamilies=false,liveInteriors=false,weaponWorkings=false;
    bool terrainStudy=false,voidmaw=false,surfaceGravity=false,coverKit=false,arsenal=false,tankFormation=false,vehicleAttacks=false;
    bool motionEffects=false,encounterReactor=false,combatSky=false,enemyEffects=false,menus=false,referenceRefinement=false,destruction=false,skyline=false,landscapes=false,encounters=false,enemyRedesign=false,redesign=false,manualAssets=false,expansion=false,maps=false,openers=false,materials=false,injuries=false,vehicles=false,reticles=false;
    for(int i=1;i<argc;++i) {
        std::string flag=argv[i];
        if(flag=="--weapon-workings"){weaponWorkings=true;continue;}
        if(flag=="--cockpit-interiors"){cockpitFamilies=true;liveInteriors=true;continue;}
        if(flag=="--cockpit-families"){cockpitFamilies=true;continue;}
        if(flag=="--weather-fall"){weatherFall=true;continue;}
        if(flag=="--crossings"){crossings=true;continue;}
        if(flag=="--terrain-art"){terrainStudy=true;continue;}
        if(flag=="--motion-effects"){motionEffects=true;continue;}
        if(flag=="--encounter-reactor"){encounterReactor=true;continue;}
        if(flag=="--combat-sky"){combatSky=true;continue;}
        if(flag=="--enemy-effects"){enemyEffects=true;continue;}
        if(flag=="--menus"){menus=true;continue;}
        if(flag=="--reference-refinement"){referenceRefinement=true;continue;}
        if(flag=="--destruction"){destruction=true;continue;}
        if(flag=="--skyline"){skyline=true;continue;}
        if(flag=="--landscapes"){landscapes=true;continue;}
        if(flag=="--encounters"){encounters=true;continue;}
        if(flag=="--enemy-redesign"){enemyRedesign=true;continue;}
        if(flag=="--voidmaw"){voidmaw=true;continue;}
        if(flag=="--surface-gravity"){surfaceGravity=true;continue;}
        if(flag=="--tank-formation"){tankFormation=vehicleAttacks=true;continue;}
        if(flag=="--vehicle-attacks"){vehicleAttacks=true;continue;}
        if(flag=="--arsenal"){arsenal=true;continue;}
        if(flag=="--cover-kit"){coverKit=true;continue;}
        if(flag=="--mecha-redesign"){redesign=true;continue;}
        if(flag=="--manual-assets"){manualAssets=true;continue;}
        if(flag=="--expansion"){expansion=true;continue;}
        if(flag=="--vehicles"){vehicles=true;continue;}
        if(flag=="--reticles"){reticles=true;continue;}
        if(flag=="--injuries"){injuries=true;continue;}
        if(flag=="--materials"){materials=true;continue;}
        if(flag=="--maps"){maps=true;continue;}
        if(flag=="--openers"){openers=true;continue;}
        if(++i>=argc)throw std::runtime_error("Missing gallery option value");
        if(flag=="--environment-catalog")environmentCatalogPath=argv[i];
        else if(flag=="--assets")assets=argv[i];
        else if(flag=="--out")out=argv[i];
        else throw std::runtime_error("Unknown gallery option");
    }
    std::filesystem::create_directories(out);setOriginalAssetRoot(assets);
    if(SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window=SDL_CreateWindow("Mecha Hack native asset QA",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,720,SDL_WINDOW_OPENGL);
    if(!window)throw std::runtime_error(SDL_GetError());
    auto context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());
    SDL_GL_SetSwapInterval(0);
    Json frames=Json::array();
    {
        Renderer renderer(assets/"cockpit");
        if(openers)previewOpeners(renderer,window,out,frames);
        auto mapView=[&](World world,const std::string& id,Vec3 position,Vec3 direction,bool cockpit,EnvironmentSettings settings={Lighting::Day,Weather::Clear,7},float headYaw=0,Vec3 lean={}) {
            SDL_Event event;while(SDL_PollEvent(&event)){}
            int width,height;SDL_GL_GetDrawableSize(window,&width,&height);
            world.position=position;world.environment=settings;
            if(landscapes||skyline)world.environment.authoredSky=true;
            world.missionStatus=world.lifeSpent?MissionStatus::Ejecting:MissionStatus::Active;world.rpgInsertion=0;
            auto ray=unit(direction);float yaw=std::atan2(-ray.x,-ray.z),pitch=std::asin(ray.y);
            world.chassisYaw=yaw;world.torsoYaw=0;
            Vec3 eye=cockpit?world.eye()+rotate(world.cockpitOrientation(),lean):position;
            const auto q=cockpit?compose(world.cockpitOrientation(),angles(headYaw,pitch)):angles(yaw,pitch);
            auto camera=poseMatrix({{q.x,q.y,q.z,q.w},{eye.x,eye.y,eye.z}});
            float vertical=.65f,horizontal=std::atan(std::tan(vertical)*float(width)/height);
            auto lens=projection({-horizontal,horizontal,vertical,-vertical},.05f,3000);
            Frontend menu;menu.screen=Screen::Playing;menu.spectating=!cockpit;menu.renderWorldOnly=!cockpit;
            menu.remotePilots=world.mapPilots;
            renderer.resetCockpit();renderer.updateCockpit(world,6,true);
            auto clock=renderer.cockpitEffects().clock;
            for(float offset:{-.032f,.032f}) {
                glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width,height);
                renderer.draw(world,lens,multiply(camera,translation(offset,0,0)),camera,false,&menu);
                if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Map renderer GL error: "+id);
                if(liveInteriors && offset<0 && id.starts_with("lean-"))capture(out/("left-"+id+".ppm"),width,height);
            }
            if(renderer.cockpitEffects().clock!=clock)throw std::runtime_error("Map draw advanced cockpit");
            capture(out/(id+".ppm"),width,height);SDL_GL_SwapWindow(window);
            frames.push_back({{"id",id},{"width",width},{"height",height},{"stereo_views",2},
                              {"indoors",world.indoors(world.eye())},{"targets",world.targets.size()},
                              {"aim_mask",world.rpgAimMask},{"vehicle_blend",world.rpgVehicleBlend},{"injuries",world.rpgInjuries},{"missing_arms",world.missingArms()},
                              {"sensors_working",world.sensorsWorking()},{"targeting_working",world.targetingWorking()},
                              {"cockpit_armor",renderer.cockpitEffects().armor},{"fracture_stage",renderer.cockpitEffects().fractureStage()}});
        };
        if(weatherFall)captureWeatherAndFalls(mapView,frames);
        if(crossings)captureCrossings(mapView,frames);
        if(terrainStudy)captureTerrainArt(mapView,frames);
        auto render=[&](World& world,Vec3 center,float span,const std::string& id) {
            SDL_Event event;while(SDL_PollEvent(&event)){}
            int width,height;SDL_GL_GetDrawableSize(window,&width,&height);
            if(width<1||height<1)throw std::runtime_error("No gallery framebuffer");
            world.rpgPrototype=true;world.mission=0;world.skyColor={.09f,.13f,.17f};world.groundColor={.16f,.18f,.20f};
            world.environment={Lighting::Day,Weather::Clear,7};world.missionStatus=MissionStatus::Active;
            Frontend menu;menu.screen=Screen::Playing;menu.spectating=true;menu.renderWorldOnly=true;
            menu.remotePilots=world.mapPilots;
            float distance=std::max(2.f,span)*1.05f;
            // Front three-quarter view; the asset forward direction is -Z.
            Vec3 eye=center+Vec3{-distance*.68f,distance*.36f,-distance};
            Vec3 delta=unit(center-eye);
            auto camera=multiply(translation(eye.x,eye.y,eye.z),rotation(std::atan2(-delta.x,-delta.z),std::asin(delta.y)));
            float vertical=.56f,horizontal=std::atan(std::tan(vertical)*float(width)/height);
            auto lens=projection({-horizontal,horizontal,vertical,-vertical},.05f,3000);
            renderer.updateCockpit(world,1.f/60,false);
            auto before=renderer.cockpitEffects().clock;
            for(float offset:{-.032f,.032f}) {
                glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width,height);
                renderer.draw(world,lens,multiply(camera,translation(offset,0,0)),camera,false,&menu);
                if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Native stereo gallery GL error: "+id);
            }
            if(renderer.cockpitEffects().clock!=before)throw std::runtime_error("Per-eye effect advancement");
            capture(out/(id+".ppm"),width,height);SDL_GL_SwapWindow(window);
            frames.push_back({{"id",id},{"width",width},{"height",height},{"stereo_views",2}});
        };
        if(weaponWorkings)captureWeaponWorkings(renderer,mapView);
        if(liveInteriors)captureCockpitInteriors(renderer,assets,mapView);
        else if(cockpitFamilies){
            World world;
            buildMissionEnvironment(world,4271,0,600,{},0,3,0,5,-1,0);
            world.rpgPrototype=true;world.mission=0;
            world.rpgAp=world.rpgMaxAp=5;world.rpgHeat=.45f;
            world.rpgReadouts={"20","48","--","--","--","--"};
            const auto seat=world.missionEntry;
            auto captureSeat=[&](const std::string& id){
                mapView(world,"seat-"+id,seat,{0,0,-1},true);
                mapView(world,"look-"+id,seat,{0,.10f,-1},true,{Lighting::Day,Weather::Clear,7},.46f);
            };
            captureSeat("baseline");
            for(size_t i=0;i<chassisIds.size();++i){
                const auto id=std::string(chassisIds[i]);
                if(!std::filesystem::is_regular_file(assets/"cockpit"/"variants"/(id+".mesh")))
                    throw std::runtime_error("Missing cockpit trim: "+id);
                world.rpgChassis=int(i);captureSeat(id);
                if(i>=size_t(Chassis::Auxiliary)){
                    if(!std::filesystem::is_regular_file(assets/"models"/("chassis_"+id+".json")))
                        throw std::runtime_error("Missing player exterior: "+id);
                    World stage;Pilot body;
                    body.rpgChassis=int(i);body.health=100;body.position={};
                    body.weapons.fill(Weapon::Empty);stage.mapPilots.push_back(body);
                    render(stage,{0,6,0},15,"exterior-"+id);
                }
            }
        }
        if(combatSky){
            struct PoseFrame {const char *id;EnemyAction action;float progress,aim,retreat;};
            constexpr PoseFrame poses[]={{"idle",EnemyAction::None,0,0,0},{"aim",EnemyAction::None,0,1,0},
                {"windup",EnemyAction::MeleeWindup,.72f,0,0},{"strike",EnemyAction::Strike,.17f,0,0},
                {"guard",EnemyAction::Guard,.45f,0,0},{"parry",EnemyAction::Parry,.18f,0,0},
                {"fire",EnemyAction::Fire,.08f,1,0},{"retreat",EnemyAction::None,0,.6f,1}};
            for(const auto &frame:poses){
                World world;Target target;applyOriginalModel(target,"mecha_ace");
                target.health=100;target.spawnType=3;world.updateEnemyBounds(target);
                target.ai.combatMotion={frame.action,1.f,1.f,frame.aim,frame.retreat};
                if(frame.retreat>0){target.ai.gaitPhase=4.5f;target.ai.velocity={0,0,3};}
                world.elapsed=1.f+frame.progress;world.targets.push_back(target);
                render(world,target.center,std::max({target.half.x,target.half.y,target.half.z})*2,"motion-"+std::string(frame.id));
            }
            for(size_t biome=0;biome<environmentCatalog().size();++biome){
                World world;buildMissionEnvironment(world,4271,int(biome),500,{},0,3,0,5,-1,0);
                const std::string id=environmentCatalog()[biome].id;
                for(auto lighting:{Lighting::Day,Lighting::Night})
                    mapView(world,"sky-"+id+(lighting==Lighting::Day?"-day":"-night"),
                            {0,32,320},{0,.38f,-1},false,{lighting,Weather::Clear,7});
                if(biome==0||biome==3||biome==5)
                    mapView(world,"sky-"+id+"-weather",{0,32,320},{0,.38f,-1},false,
                            {Lighting::Day,biome==0?Weather::Sandstorm:biome==3?Weather::Rain:Weather::Storm,7});
                if(biome==5){
                    const EnvironmentSettings storm{Lighting::Night,Weather::Storm,7};
                    mapView(world,"sky-storm-night",{0,32,320},{0,.38f,-1},false,storm);
                    world.environmentSeconds=17.f-weatherNoise(storm.seed,0)*10.f+.05f;
                    mapView(world,"sky-storm-flash",{0,32,320},{0,.38f,-1},false,storm);
                }
            }
            struct HazardPreview {Weather weather;int biome,location;unsigned effect;};
            constexpr HazardPreview hazards[]={
                {Weather::IonStorm,3,0,rpgWeatherPulse},
                {Weather::CorruptionSporeBurst,5,1,rpgWeatherSpore},
                {Weather::FireStorm,4,2,rpgWeatherFire},
                {Weather::MeteorShower,0,3,rpgWeatherMeteor},
                {Weather::MagneticBurst,6,0,rpgWeatherPulse},
                {Weather::AshCloud,4,2,rpgWeatherWarning},
                {Weather::RadioactiveWind,3,5,rpgWeatherWarning}
            };
            for(const auto &sample:hazards){
                World world;buildMissionEnvironment(world,4271,sample.biome,500,{},sample.location,0,0,5,-1,0);
                world.environmentSeconds=6.f;
                const auto p=world.missionEntry;
                auto effect=p+Vec3{5,0,-30};
                effect.y=world.surfaceHeight(effect.x,effect.z);
                const auto &hazard=*weatherHazard(sample.weather);
                const bool local=hazard.radius>0;
                const auto weapon=local?Weapon::Grenade:Weapon::Rail;
                if(!local)effect.y+=5.f;
                world.effects.push_back({WeaponEffect::Sparks,effect,{0,1,0},weapon,
                                        sample.effect==rpgWeatherMeteor?2.18f:sample.effect==rpgWeatherPulse?.17f:1.16f,
                                        local?hazard.radius:5.f,sample.effect});
                mapView(world,"weather-"+std::string(weatherNames[size_t(sample.weather)]),
                        p,{.12f,sample.weather==Weather::MeteorShower?.27f:-.06f,-1},true,
                        {Lighting::Day,sample.weather,717u});
            }
            World hangar;
            buildMissionEnvironment(hangar,4271,1,500,{},7,3,2,5,-1,0);
            const auto &room=hangar.interiorRegions.at(0);
            const auto &bounds=room.bounds;
            const Vec3 eye{(bounds[0]+bounds[2])*.5f,(room.floor+room.ceiling)*.5f,(bounds[1]+bounds[3])*.5f};
            mapView(hangar,"sky-hangar",eye,{0,.38f,-1},false);
        }
        if(encounterReactor){
            Session session(4271);session.add(1,"Reactor preview");finishCreation(session.member(1).sheet);
            GameSettings settings;settings.environment=3;settings.location=0;settings.opener=5;settings.enemyCount=1;
            session.command(1,{{"op","settings"},{"settings",settings}});
            session.command(1,{{"op","ready"},{"value",true}});
            session.command(1,{{"op","deploy"}});
            auto world=session.view(1,renderer.armRig());
            for(int i=0;i<3;++i){
                world.rpgArmor=100.f-i*38.f;
                world.rpgHeat=i==0?.25f:i==1?.86f:.96f;
                world.rpgOverheated=i==2;
                mapView(world,i==0?"reactor-cool":i==1?"reactor-hot":"reactor-venting",
                        world.position,{0,.16f,-1},true);
            }
            world.rpgHeat=0;world.rpgOverheated=false;
            mapView(world,"reactor-armour",world.position,{0,-.48f,-1},true);
            for(int bearing:{359,1,90}){
                auto ray=direction(-bearing*pi/180,.0f);
                ray.y=-.31f;
                mapView(world,"compass-"+std::to_string(bearing),world.position,ray,true);
            }
            constexpr std::array arrivals={rpgArrivalDropship,rpgArrivalWarp,rpgArrivalBurrow,rpgArrivalEdge};
            constexpr std::array names={"dropship","warp","burrow","edge"};
            for(size_t i=0;i<arrivals.size();++i){
                World stage;stage.rpgPrototype=true;stage.mission=0;stage.skyKind=SkyKind::Urban;
                stage.effects.push_back({WeaponEffect::Sparks,{0,0,0},{0,0,1},Weapon::Empty,
                                         enemyEffectLife(arrivals[i])*.34f,4.f,arrivals[i]});
                mapView(stage,"arrival-"+std::string(names[i]),{0,11,40},{0,.15f,-1},false);
            }
        }
        if(motionEffects){
            struct MotionFrame {const char *id;float speed,phase,landing,recoil;Vec3 thrust;};
            constexpr MotionFrame motion[]={
                {"walk",4,4.2f,0,0,{}},{"brake",0,4.2f,.92f,0,{}},
                {"recoil",0,0,0,1,{}},{"boost",4,8,0,0,{.85f,0,-.50f}}};
            for(int chassis=0;chassis<int(chassisIds.size());++chassis)for(const auto &sample:motion){
                World view;
                Pilot pilot;pilot.position={};pilot.rpgChassis=chassis;pilot.health=100;
                pilot.walkingSpeed=sample.speed;pilot.gaitPhase=sample.phase;
                pilot.rpgLanding=sample.landing;pilot.rpgBodyRecoil=sample.recoil;pilot.rpgThrust=sample.thrust;
                pilot.chassisYaw=pi;pilot.weapons.fill(Weapon::Empty);
                view.mapPilots.push_back(pilot);
                render(view,{0,6,0},15,"chassis-"+std::to_string(chassis)+"-"+sample.id);
            }
            for(float speed:{5.f,21.f}){
                World view;Target actor;applyOriginalModel(actor,"mecha_ace");
                actor.origin={};actor.yaw=pi*.66f;actor.spawnType=3;actor.health=100;
                actor.ai.gaitPhase=6;actor.ai.velocity=rotate(angles(actor.yaw),{speed*.5f,0,-speed*.866f});
                view.updateEnemyBounds(actor);view.targets.push_back(actor);
                render(view,actor.center,13,speed>10?"enemy-slide":"enemy-stride");
            }
            struct EffectFrame {const char *id;unsigned model;Weapon weapon;float size;};
            constexpr EffectFrame effects[]={
                {"rifle",rpgWeaponMuzzle,Weapon::Rifle,1.2f},{"cannon",rpgWeaponHeavy,Weapon::HeavyMachinegun,1.8f},
                {"energy",rpgWeaponHeavy,Weapon::Rail,1.8f},{"rocket",rpgRocketIgnition,Weapon::Rocket,1.5f},
                {"strike",rpgEnemyMelee,Weapon::Cutter,2.1f},{"contact",rpgEnemyHit,Weapon::Rifle,2.f},
                {"explosion",rpgWeaponBlast,Weapon::Grenade,4.f}};
            for(const auto &sample:effects)for(float phase:{.16f,.52f,.78f}){
                World view;view.effects.push_back({WeaponEffect::Sparks,{0,3,0},{.85f,0,-.50f},
                    sample.weapon,enemyEffectLife(sample.model)*phase,sample.size,sample.model});
                render(view,{0,3.8f,0},11,std::string(sample.id)+"-"+std::to_string(int(phase*100)));
            }
        }
        if(enemyEffects){
            constexpr Vec3 center{0,4,0};
            constexpr Vec3 aim{-.55f,0,-.84f};
            constexpr float span=10;
            World control;
            control.targets.clear();
            render(control,center,span,"enemy-effects-control");
            const std::pair<unsigned,const char*> effects[]={
                {rpgEnemyCharge,"enemy-charge"},
                {rpgEnemyBossCharge,"enemy-boss-charge"},
                {rpgEnemyBoost,"enemy-boost"},
                {rpgEnemyKinetic,"enemy-kinetic"},
                {rpgEnemyEnergy,"enemy-energy"},
                {rpgEnemyDisruption,"enemy-disruption"},
                {rpgEnemyMelee,"enemy-melee"},
                {rpgEnemyHit,"enemy-hit"},
                {rpgEnemyShieldHit,"enemy-shield-hit"},
                {rpgWeaponMuzzle,"weapon-muzzle"},
                {rpgWeaponHeavy,"weapon-heavy"},
                {rpgWeaponArc,"weapon-arc"},
                {rpgWeaponPulse,"weapon-pulse"},
                {rpgWeaponBarrier,"weapon-barrier"},
                {rpgWeaponBlast,"weapon-blast"},
                {rpgArrivalWarp,"encounter-human-warp"},
                {rpgArrivalKaijuRift,"encounter-voidmaw-rift"},
            };
            for (auto [id, name] : effects) {
                World world=control;
                world.effects.push_back({WeaponEffect::Sparks,center,aim,Weapon::Rifle,enemyEffectLife(id)*.27f,3.f,id});
                const bool portal=id==rpgArrivalKaijuRift||id==rpgArrivalWarp;
                render(world,portal?center+Vec3{0,4.5f,0}:center,portal?16.f:span,name);
            }
            // Capture the actual in-engine rigid poses at the key beats of a strike.
            struct StrikeSample {const char *name; EnemyAction action;float phase;};
            constexpr StrikeSample strikeSamples[]={
                {"anticipation",EnemyAction::MeleeWindup,.83f},
                {"contact",EnemyAction::Strike,.19f},
                {"follow-through",EnemyAction::Strike,.52f},
                {"recovery",EnemyAction::Strike,.84f},
            };
            for(const char *id:{"mecha_ace","mechwalker","kaiju_dismantler"}){
                for(const auto &sample:strikeSamples){
                    World stage=control;
                    Target actor;applyOriginalModel(actor,id);
                    actor.health=actor.ai.maxHealth=100;actor.spawnType=3;actor.active=true;
                    actor.ai.combatMotion.play(sample.action,1.f,.48f);
                    actor.ai.combatMotion.aim=.65f;
                    stage.elapsed=1.f+sample.phase*.48f;
                    stage.updateEnemyBounds(actor);
                    stage.targets.push_back(actor);
                    render(stage,actor.center,std::max({actor.half.x,actor.half.y,actor.half.z})*2.15f,
                           std::string("npc-melee-")+id+"-"+sample.name);
                }
            }
            for(const char *id:{"jump_pod_field_commander","kaiju_obliterator"}){
                Session demo(745);
                demo.add(1,"Pattern preview");
                demo.phase=Phase::Combat;
                demo.mission.recipe.opener=5;demo.mission.openerTime=100;
                demo.world.mission=0;demo.world.rpgPrototype=true;
                demo.world.groundColor={.18f,.20f,.22f};
                demo.member(1).body.position={0,0,-46};
                demo.member(1).sheet.hp=demo.member(1).sheet.maxHp=100;
                const auto entries=enemyCatalog();
                const auto spec=std::find_if(entries.begin(),entries.end(),[&](const auto &entry){
                    return std::string_view(entry.id)==id;
                });
                if(spec==entries.end())throw std::runtime_error("Missing heavy attack preview enemy");
                Enemy enemy;enemy.spec=int(spec-entries.begin());enemy.hp=enemy.maxHp=100;
                enemy.tactic=8;enemy.fireClock=0;
                demo.enemies={enemy};demo.mission.recipe.enemies={enemy.spec};
                Target actor;actor.spawnType=3;
                applyOriginalEnemy(actor,enemy.spec);demo.world.targets={actor};
                auto stage=[&](const char *name){
                    mapView(demo.view(1,renderer.armRig()),std::string(id)+"-"+name,
                            {54,23,-30},{-1,-.25f,.17f},false);
                };
                for(int tick=0;tick<45&&demo.enemies[0].heavyAttack.pattern==HeavyPattern::None;++tick)
                    demo.step(.02f);
                if(demo.enemies[0].heavyAttack.pattern==HeavyPattern::None)
                    throw std::runtime_error(std::string(id)+" did not start its attack");
                stage("warning");
                while(demo.enemies[0].heavyAttack.pulse==0&&demo.enemies[0].heavyAttack.pattern!=HeavyPattern::None)
                    demo.step(.02f);
                if(demo.enemies[0].heavyAttack.pulse==0)
                    throw std::runtime_error(std::string(id)+" did not release its attack");
                for(int tick=0;tick<22;++tick)demo.step(.02f);
                stage("release");
            }
            World shots=control;
            Projectile tracer;
            tracer.position=center+Vec3{-1,-.6f,0};
            tracer.velocity=aim*70.f;tracer.owner=0;tracer.mount=-1;tracer.weapon=Weapon::Rifle;
            shots.projectiles.push_back(tracer);
            tracer.position=center+Vec3{2,1,0};tracer.weapon=Weapon::Rail;
            shots.projectiles.push_back(tracer);
            render(shots,center,span,"enemy-tracers");
        }
        if(environmentCatalogPath.empty()&&!weaponWorkings&&!cockpitFamilies&&!weatherFall&&!crossings&&!terrainStudy&&!vehicleAttacks&&!arsenal&&!coverKit&&!surfaceGravity&&!voidmaw&&!motionEffects&&!encounterReactor&&!combatSky&&!enemyEffects&&!menus&&!referenceRefinement&&!destruction&&!skyline&&!landscapes&&!enemyRedesign&&!redesign&&!manualAssets&&!expansion&&!maps&&!openers&&!materials&&!injuries&&!vehicles&&!reticles&&!encounters)for(int index=0;index<int(enemyCatalog().size());++index) {
            World world;Target target;applyOriginalEnemy(target,index);
            target.origin={};target.health=100;target.active=true;target.spawnType=3;
            world.targets.push_back(target);
            render(world,target.center,std::max({target.half.x,target.half.y,target.half.z})*2,enemySpec(index).id);
        }
        if(environmentCatalogPath.empty()&&!weaponWorkings&&!cockpitFamilies&&!weatherFall&&!crossings&&!terrainStudy&&!vehicleAttacks&&!arsenal&&!coverKit&&!surfaceGravity&&!voidmaw&&!motionEffects&&!encounterReactor&&!combatSky&&!enemyEffects&&!menus&&!referenceRefinement&&!destruction&&!skyline&&!landscapes&&!enemyRedesign&&!redesign&&!manualAssets&&!expansion&&!maps&&!openers&&!materials&&!injuries&&!vehicles&&!reticles&&!encounters)for(const char* id:{"building","industrial"}) {
            World world;Target target;applyOriginalModel(target,id);
            target.origin={};target.health=100;target.active=true;target.spawnType=-1;
            world.targets.push_back(target);
            render(world,target.center,std::max({target.half.x,target.half.y,target.half.z})*2,id);
        }
        if(menus){
            previewMenus(renderer,window,out,frames);
            for(size_t i=0;i<campaignBosses().size();++i)for(bool charge:{false,true}){
                const auto &boss=campaignBoss(int(i));Target body;applyOriginalEnemy(body,bossBaseEnemy(int(i)));body.origin={};body.health=160;body.ai.maxHealth=160;body.spawnType=3;
                body.rpgTint=charge?Vec3{1,.28f,.12f}:boss.faction==3?Vec3{.72f,.38f,.62f}:Vec3{1,.7f,.38f};
                World world;world.targets.push_back(body);render(world,body.center,std::max({body.half.x,body.half.y,body.half.z})*2,std::string(boss.spec.id)+(charge?"-charge":"-boss"));
                frames.back()["boss"]=boss.spec.name;frames.back()["base_rig"]=boss.base;frames.back()["triangles"]=body.mesh->size()/3;frames.back()["charging"]=charge;
            }
        }
        if(referenceRefinement){
            std::vector<std::string> ids;for(const char *base:{"jump_pod","jump_pod_heavy","jump_pod_field_commander","mechwalker","mechwalker_disabler","mecha_ace","siege_mecha"})for(const char *suffix:{"","_alt"})ids.push_back(std::string(base)+suffix);
            for(const char *id:{"heavy_hover_tank","cyclone_drone"})ids.push_back(id);
            for(const auto &id:ids)for(int state=0;state<4;++state){World world;world.rpgPrototype=true;world.prototypeSeed=722;
                Target target;applyOriginalModel(target,id);target.origin={};target.health=state==3?0:100;target.rpgInitialHealth=100;target.active=true;target.spawnType=3;
                target.ai.gaitPhase=state==1?4.5f:0;target.ai.velocity=state==1?Vec3{0,0,-4}:Vec3{};
                target.ai.weaponYaw=state==2?.4f:0;target.ai.weaponPitch=state==2?.18f:0;target.ai.muzzleFlash=state==2?.12f:0;
                if(state==3){target.rpgDeathAt=0;target.rpgRestY=0;}world.elapsed=state==3?2.5f:.4f;world.targets.push_back(target);
                render(world,target.center,2*std::max({target.half.x,target.half.y,target.half.z}),id+std::array<const char*,4>{"-bind","-walk","-aim","-corpse"}[state]);
                frames.back()["triangles"]=target.mesh->size()/3;
            }
        }
        if(destruction)previewDestruction(assets,out,render);
        if(!environmentCatalogPath.empty()){
            std::ifstream input(environmentCatalogPath);
            const auto catalog=Json::parse(input);
            for(const auto &[id,family]:catalog.at("assets").items()){
                World world;
                Target target;
                applyOriginalModel(target,id);
                target.spawnType=-1;
                target.health=100;
                world.targets={target};
                const float span=2*std::max({target.half.x,target.half.y,target.half.z});
                render(world,target.center,span,id);
                frames.back()["family"]=family;
                frames.back()["triangles"]=target.mesh->size()/3;
            }
        }
        if(skyline){
            for(const auto *id:skylineCatalog()){
                World world;world.targets.clear();Target target;applyOriginalModel(target,id);target.spawnType=-1;target.health=100;
                world.targets.push_back(target);render(world,target.center,2*std::max({target.half.x,target.half.y,target.half.z}),id);
                frames.back()["triangles"]=target.mesh->size()/3;frames.back()["height_m"]=target.half.y*2;
            }
            for(int location:{0,4,6}){
                World world;std::array<int,0> empty;buildMissionEnvironment(world,4271,3,500,empty,location,3,0,5,-1,2);world.mission=0;
                auto density=missionDensity(world);std::string id="skyline-location-"+std::to_string(location);
                mapView(world,id+"-overview",{600,500,700},{-.6f,-.5f,-.7f},false);
                frames.back()["skyscrapers"]=density.skyscrapers;frames.back()["decorations"]=density.decorations;frames.back()["large_structures"]=density.largeStructures;
                mapView(world,id+"-cockpit",{0,world.surfaceHeight(0,325),325},{0,0,-1},true);
            }
        }
        if(landscapes)for(auto setting:std::vector<std::array<int,2>>{{0,3},{1,5},{3,0},{4,3},{8,2}}){
            World world;std::array<int,0> empty;
            uint32_t seed=4271;
            if(setting[0]==1||setting[0]==3){int family=setting[0]==1?4:3;
                for(seed=1;generationPlan(seed,setting[1],setting[0],0).layout!=family;++seed){}}
            std::vector<DungeonSite> sites;
            buildMissionEnvironment(world,seed,setting[1],500,empty,setting[0],3,0,5,-1,2,&sites);
            world.mission=0;
            std::string id="landscape-"+std::to_string(setting[0]);
            mapView(world,id+"-overview",{0,850,650},{0,-1,-.7f},false);
            auto density=missionDensity(world);frames.back()["decorations"]=density.decorations;frames.back()["large_structures"]=density.largeStructures;
            mapView(world,id+"-ground",{0,world.surfaceHeight(0,325),325},{.1f,0,-1},true);
            frames.back()["seed"]=seed;
        }
        if(landscapes){
            uint32_t seed=0;for(;!generationPlan(seed,3,0,0).underground;++seed){}
            World world;std::array<int,0> empty;std::vector<DungeonSite> sites;
            buildMissionEnvironment(world,seed,3,500,empty,0,3,0,5,-1,2,&sites);world.mission=0;
            auto site=std::find_if(sites.begin(),sites.end(),[](const auto &s){return s.underground;});
            if(site==sites.end())throw std::runtime_error("No underground dungeon in reserved excavation");
            Vec3 entry{site->entrance[0],site->entrance[1],site->entrance[2]};
            Vec3 observer=entry+Vec3{0,0,70};observer.y=world.surfaceHeight(observer.x,observer.z);
            mapView(world,"underground-ramp",observer,entry-(observer+Vec3{0,12,0}),true);
            auto p=site->rooms.front().position;Vec3 room{p[0],p[1],p[2]};
            mapView(world,"underground-room",room,world.targets[size_t(site->core)].center-(room+Vec3{0,12,0}),true);
            frames.back()["underground"]=true;frames.back()["floor"]=room.y;
        }
        if(landscapes){
            struct Sample {const char *name;unsigned seed;int biome,location,region;};
            for(auto sample:std::array{Sample{"excavation",4,3,0,0},Sample{"tunnels",4,5,1,3},
                                       Sample{"megastructure",5,3,0,0}}){
                World world;std::vector<DungeonSite> sites;
                buildMissionEnvironment(world,sample.seed,sample.biome,750,{},sample.location,0,sample.region,5,-1,2,&sites);
                if(world.interiorTierNodes.size()<3)throw std::runtime_error("Missing preview storey");
                const auto foot=world.interiorNavNodes[world.interiorTierNodes.front()];
                const auto landing=world.interiorNavNodes[world.interiorTierNodes[2]];
                const auto axis=unit(Vec3{landing.x-foot.x,0,landing.z-foot.z});
                const auto side=cross(axis,Vec3{0,1,0});
                world.mission=0;
                const auto look=foot+(landing-foot)*.45f;
                auto eye=look+side*40.f-axis*42.f+Vec3{0,20.f,0};
                if(!world.indoors(eye)||!clearPosition(world,eye,2.f))eye=eye-side*80.f;
                if(!world.indoors(eye)||!clearPosition(world,eye,2.f))eye=look-axis*58.f+Vec3{0,17.f,0};
                mapView(world,std::string(sample.name)+"-section",eye,look-eye,false);
                mapView(world,std::string(sample.name)+"-upper-floor",landing-axis*12.f,
                        axis*.25f+side*.9f+Vec3{0,-.22f,0},true);
                frames.back()["upper_floor_m"]=landing.y;
                if(world.interiorTierNodes.size()>3){
                    const auto top=world.interiorNavNodes[world.interiorTierNodes.back()];
                    const auto turn=world.interiorNavNodes[world.interiorTierNodes[3]];
                    mapView(world,std::string(sample.name)+"-switchback",top+Vec3{0,12,0},turn-top,false);
                    frames.back()["terminal_floor_m"]=top.y;
                }
            }
        }
        if(encounters)for(int location:{0,3,7}) {
            Session game(4271);game.add(1,"Encounter preview");finishCreation(game.member(1).sheet);
            GameSettings settings;settings.location=location;settings.environment=location==3?0:location==7?1:3;
            settings.world=location==7?2:0;settings.theme=2;settings.objective=3;settings.opener=5;
            settings.enemyCount=24;
            game.command(1,{{"op","settings"},{"settings",settings}});
            game.command(1,{{"op","ready"},{"value",true}});game.command(1,{{"op","deploy"}});
            if(game.mission.sites.empty())throw std::runtime_error("No enterable encounter preview site");
            const auto &site=game.mission.sites.front();auto world=game.view(1,renderer.armRig());
            Vec3 entry{site.entrance[0],site.entrance[1],site.entrance[2]};
            std::string name="encounter-"+std::to_string(location);
            mapView(world,name+"-entrance",entry+Vec3{0,0,52},{0,0,-1},true);
            Vec3 room{site.rooms.front().position[0],site.rooms.front().position[1],site.rooms.front().position[2]};
            auto core=world.targets[size_t(site.core)].center;
            mapView(world,name+"-interior",room,core-(room+Vec3{0,12,0}),true);
            auto cutaway=world;
            for(auto &t:cutaway.targets)if(t.invulnerable&&t.center.y-t.half.y>=entry.y+38)t.active=false;
            Vec3 middle{(site.bounds[0]+site.bounds[2])/2,entry.y+8,(site.bounds[1]+site.bounds[3])/2};
            mapView(cutaway,name+"-cutaway",middle+Vec3{90,130,170},{-90,-130,-170},false);
            mapView(world,name+"-world",{0,700,850},{0,-.7f,-1},false);
        }
        if(expansion)for(int index=0;index<int(enemyCatalog().size());++index)for(int variant=0;variant<2;++variant)for(int phase=0;phase<2;++phase) {
            World world;Target target;std::string id=originalVariant(enemyModelId(index),uint32_t(variant));
            applyOriginalModel(target,id);target.origin={};target.health=100;target.active=true;target.spawnType=3;
            target.ai.velocity={0,0,-4};target.ai.gaitPhase=phase?4.5f:0;
            target.ai.weaponYaw=.25f;target.ai.weaponPitch=.15f;target.ai.muzzleFlash=phase?.15f:0;
            world.elapsed=phase?.3f:0;world.updateEnemyBounds(target);world.targets.push_back(target);
            render(world,target.center,std::max({target.half.x,target.half.y,target.half.z})*2,id+"-pose"+std::to_string(phase));
        }
        if(manualAssets){
            for(const char *registry:{"manual-enemy-assets.json","manual-item-assets.json"}){
                std::ifstream file(assets/registry);Json pack;file>>pack;
                for(const auto &row:pack.at("assets")){
                    const auto id=row.at("id").get<std::string>();bool actor=std::string(registry)=="manual-enemy-assets.json";
                    for(int phase=0;phase<(actor?3:1);++phase){
                        World world;Target target;applyOriginalModel(target,id);
                        target.health=100;target.active=true;target.spawnType=actor?3:-1;
                        target.ai.velocity=phase==1?Vec3{0,0,-4}:Vec3{};
                        target.ai.gaitPhase=phase==1?4.5f:0;
                        target.ai.weaponYaw=phase==2?.4f:0;target.ai.weaponPitch=phase==2?.15f:0;
                        target.ai.muzzleFlash=phase==2?.15f:0;world.elapsed=.4f;
                        world.updateEnemyBounds(target);world.targets.push_back(target);
                        render(world,target.center,std::max({target.half.x,target.half.y,target.half.z})*2,
                               id+(actor?std::array<const char*,3>{"-idle","-move","-attack"}[phase]:"-item"));
                    }
                }
            }
        }
        if(redesign){
            std::ifstream file(assets/"mecha-redesign-budgets.json");
            if(!file)throw std::runtime_error("Missing audited mecha redesign scope");
            Json scope;file>>scope;
            if(scope.at("schema")!=1||scope.at("assets").size()!=65)throw std::runtime_error("Redesign gallery requires exactly 65 audited mesh IDs");
            for(const auto &row:scope.at("assets")){
                const auto id=row.at("id").get<std::string>();
                const bool player=row.at("category")=="player";
                Target bound;applyOriginalModel(bound,id);
                const float span=std::max({bound.half.x,bound.half.y,bound.half.z})*2;
                const std::array<const char*,4> poses={"bind","walk-turn","aim-attack","lost-arm"};
                for(int state=0;state<(player?4:3);++state){
                    World world;
                    if(player){
                        const std::array<const char*,4> chassis={"brawler","scout","striker","titan"};
                        int kind=-1;for(size_t n=0;n<chassis.size();++n)
                            if(id==std::string("chassis_")+chassis[n]||id==std::string("chassis_")+chassis[n]+"_alt")kind=int(n);
                        if(kind<0)throw std::runtime_error("Unknown redesigned player chassis: "+id);
                        Pilot body;body.position={};body.health=100;body.playerSlot=1;
                        body.rpgChassis=kind;body.rpgHullVariant=id.ends_with("_alt");body.weapons.fill(Weapon::Empty);
                        body.walkingSpeed=state?2.8f:0;body.gaitPhase=state==1?4.5f:state>=2?13.5f:0;
                        body.torsoYaw=state==1?.7f:state>=2?-.65f:0;
                        body.aimYaw=state==1?-.35f:state>=2?.4f:0;body.aimPitch=state>=2?.22f:0;
                        if(state>=2){
                            body.weapons[LeftHand]=Weapon::Rifle;body.weapons[RightHand]=Weapon::Cutter;
                            body.rpgModelNames[LeftHand]="weapon_rifle";body.rpgModelNames[RightHand]="weapon_blade";
                            HandPose left;left.position={-.40f,-.13f,-.7f};left.aim=left.grip=angles(-.65f,.12f);
                            HandPose right;right.position={.45f,-.10f,-.65f};right.aim=right.grip=angles(.45f,-.12f);
                            body.arms[LeftHand]=solveArm(left,LeftHand,{0,12,0},body.facing(),renderer.armRig(),1);
                            body.arms[RightHand]=solveArm(right,RightHand,{0,12,0},body.facing(),renderer.armRig(),2);
                            if(state==3)body.rpgMissingArms=1u<<LeftHand;
                        }
                        world.mapPilots.push_back(body);
                    }else{
                        Target actor;applyOriginalModel(actor,id);actor.active=true;actor.health=100;actor.spawnType=3;
                        actor.ai.velocity=state==1?Vec3{0,0,-4}:Vec3{};actor.ai.gaitPhase=state==1?4.5f:0;
                        actor.ai.weaponYaw=state==2?.55f:0;actor.ai.weaponPitch=state==2?.22f:0;
                        actor.ai.muzzleFlash=state==2?.12f:0;world.elapsed=state==0?0:state==1?1.3f:2.3f;
                        world.updateEnemyBounds(actor);world.targets.push_back(actor);
                    }
                    render(world,bound.center,span,id+"-"+poses[state]);
                    frames.back()["asset"]=id;frames.back()["category"]=row.at("category");frames.back()["pose"]=poses[state];
                    frames.back()["budget"]=row.at("budget");frames.back()["tracked_arms"]=player&&state>=2;
                    frames.back()["independent_aim_inputs"]=player&&state>0;
                }
            }
        }
        if(enemyRedesign){
            std::ifstream file(assets/"enemy-redesign-budgets.json");
            if(!file)throw std::runtime_error("Missing audited remaining enemy scope");
            Json scope;file>>scope;
            if(scope.at("schema")!=1||scope.at("assets").size()!=44)throw std::runtime_error("Enemy redesign gallery requires exactly44 audited models");
            for(const auto &row:scope.at("assets")){
                const auto id=row.at("id").get<std::string>(),category=row.at("category").get<std::string>();
                const bool aircraft=category=="fighter"||category=="capital_craft"||category=="gunship";
                const bool hover=category=="hover_vehicle"||category=="hover_drone",submerged=category=="submarine";
                const bool installation=category=="installation";
                for(int state=0;state<3;++state){
                    World world;world.targets.clear();Target actor;applyOriginalModel(actor,id);
                    const float span=std::max({actor.half.x,actor.half.y,actor.half.z})*2;
                    actor.active=true;actor.health=100;actor.spawnType=3;actor.ai.airborne=aircraft||hover;
                    actor.origin.y=aircraft?span*.18f:hover?span*.035f:0;
                    actor.yaw=state==1?.15f:0;
                    actor.ai.velocity=state==1&&!installation?Vec3{2,0,-6}:Vec3{};
                    actor.ai.gaitPhase=state==1?4.5f:0;
                    actor.ai.pitch=aircraft&&state==1?.055f:0;actor.ai.bank=aircraft&&state==1?-.13f:0;
                    actor.ai.weaponYaw=state==2?.55f:state==1&&installation?.3f:actor.yaw;
                    actor.ai.weaponPitch=state==2?.22f:0;actor.ai.muzzleFlash=state==2?.12f:0;
                    world.elapsed=state==0?0:state==1?1.3f:2.3f;
                    world.updateEnemyBounds(actor);world.targets.push_back(actor);
                    const std::string pose=state==0?"bind":state==2?"aim-attack":installation?"scan":aircraft?"flight":submerged?"submerged-maneuver":hover?"hover":"gait";
                    render(world,actor.center,span,id+"-"+pose);
                    auto &frame=frames.back();frame["asset"]=id;frame["category"]=category;frame["archetype"]=row.at("archetype");
                    frame["pose"]=pose;frame["budget"]=row.at("budget");frame["airborne"]=actor.ai.airborne;
                    frame["origin_height_m"]=actor.origin.y;frame["pitch"]=actor.ai.pitch;frame["bank"]=actor.ai.bank;
                }
            }
        }
        if(tankFormation)captureTankFormation(renderer,mapView,frames);
        else if(vehicleAttacks){
            for(const auto &[id,elevation]:std::array{
                    std::pair{"anti_mecha_hovertank",3.f},std::pair{"interceptor",19.f},
                    std::pair{"bomber",32.f}}){
                Session demo(64);
                demo.add(1,"Test Pilot");
                demo.phase=Phase::Combat;
                demo.mission.recipe.opener=5;demo.mission.openerTime=100;
                demo.world.mission=0;demo.world.rpgPrototype=true;
                demo.world.groundColor={.17f,.20f,.23f};
                demo.member(1).body.position={0,0,-37};
                demo.member(1).sheet.hp=demo.member(1).sheet.maxHp=100;
                const auto entries=enemyCatalog();
                auto spec=std::find_if(entries.begin(),entries.end(),[&](const EnemySpec &e){return std::string_view(e.id)==id;});
                if(spec==entries.end())throw std::runtime_error("Missing vehicle demonstration");
                Enemy enemy;enemy.spec=int(spec-entries.begin());enemy.hp=enemy.maxHp=25;enemy.fireClock=0;
                demo.enemies={enemy};demo.mission.recipe.enemies={enemy.spec};
                Target target;target.origin={0,elevation,0};target.spawnType=3;target.ai.airborne=true;
                applyOriginalEnemy(target,enemy.spec);demo.world.targets={target};
                auto capturePass=[&](const char *stage){
                    auto scene=demo.view(1,renderer.armRig());
                    mapView(scene,std::string(id)+"-"+stage,{52,35,-27},{-1,-.46f,.12f},false);
                };
                for(int tick=0;tick<90&&demo.enemies[0].vehiclePass.phase!=VehiclePassPhase::Warning;++tick)demo.step(.02f);
                if(demo.enemies[0].vehiclePass.phase!=VehiclePassPhase::Warning)
                    throw std::runtime_error(std::string(id)+" failed to commit to a warning");
                for(int tick=0;tick<10;++tick)demo.step(.02f);
                capturePass("warning");
                for(int tick=0;tick<100&&demo.enemies[0].vehiclePass.phase!=VehiclePassPhase::Egress;++tick)demo.step(.02f);
                if(demo.enemies[0].vehiclePass.phase!=VehiclePassPhase::Egress)
                    throw std::runtime_error(std::string(id)+" failed to finish a pass");
                capturePass("volley");
            }
        }
        if(arsenal){
            for(auto family:weaponFamilies())for(auto rarity:{Rarity::Common,Rarity::Uncommon,Rarity::Rare}){
                const auto gear=weaponVariant(family,rarity);
                World scene;
                Target target;applyOriginalModel(target,gearVisualId(gear));
                target.origin.y=1.25f;scene.updateEnemyBounds(target);scene.targets.push_back(target);
                render(scene,target.center,2*std::max({target.half.x,target.half.y,target.half.z}),gearVisualId(gear));
            }
            World scene;
            scene.rpgPrototype=true;scene.mission=0;scene.rpgChassis=2;
            scene.pickups.push_back({{1,0,-7},rpgSafeBoxPickup});
            scene.groundColor={.16f,.18f,.20f};
            scene.armRig=renderer.armRig();
            for(int hand=0;hand<2;++hand){
                HandPose pose;pose.position={hand?.38f:-.38f,-.13f,-.88f};
                pose.aim=pose.grip=angles(hand?-.08f:.07f,-.10f);
                scene.arms[hand]=solveArm(pose,hand,{0,12,0},0,scene.armRig,1);
                const auto gear=hand?Gear::RailLanceRare:Gear::Scattergun;
                scene.rpgModelNames[hand]=gearVisualId(gear);
                scene.weapons[hand]=weaponTuning(gear)->visual;
                const auto &arm=scene.arms[hand];
                scene.arms[hand].muzzle=arm.wrist+rotate(arm.aim,weaponTuning(gear)->muzzle);
            }
            scene.rpgAimMask=3;
            auto gun=scene.arms[1];
            scene.effects.push_back({WeaponEffect::Sparks,gun.muzzle,gun.forward,Weapon::Rail,.13f,1.35f,rpgWeaponHeavy});
            mapView(scene,"arsenal-cockpit",{}, {0,-.13f,-1},true);
            scene.effects.clear();
            mapView(scene,"arsenal-safe-box",{2,2.8f,-.8f},{-.15f,-.34f,-1},false);
            for(auto [id,fx]:std::array{
              std::pair{"scatter",WeaponEffect{WeaponEffect::Sparks,{-1,1,-4},{.4f,.05f,-1},Weapon::Shotgun,.05f,1.35f,rpgScatterMuzzle}},
              std::pair{"pulse",WeaponEffect{WeaponEffect::Sparks,{-1,1,-4},{.4f,.05f,-1},Weapon::Rail,.09f,1.f,rpgPulseMuzzle}},
              std::pair{"rocket",WeaponEffect{WeaponEffect::Sparks,{1,1,-5},{.4f,.05f,-1},Weapon::Rocket,.14f,1.f,rpgRocketIgnition}},
              std::pair{"cutter",WeaponEffect{WeaponEffect::Sparks,{-1,1,-4},{.4f,.05f,-1},Weapon::Cutter,.16f,1.5f,rpgCutterSparks}}}){
                scene.effects={fx};scene.pickups.clear();
                mapView(scene,std::string("arsenal-fx-")+id,{0,2.1f,1},{-.16f,-.20f,-1},false);
            }
        }
        if(coverKit){
            Json kit;
            std::ifstream file(assets/"cover-manifest.json");file>>kit;
            for(const auto &row:kit.at("assets")){
                World world;world.targets.clear();
                Target prop;const auto id=row.at("id").get<std::string>();
                applyOriginalModel(prop,id);
                world.targets.push_back(prop);
                render(world,prop.center,2*std::max({prop.half.x,prop.half.y,prop.half.z}),id);
            }
            const std::array<int,0> none{};
            World city;
            buildMissionEnvironment(city,4271,3,750,none,0,0,0,5);
            city.mission=0;
            mapView(city,"cover-city-blocks",{100,240,180},{-.2f,-.66f,-1},false);
            World battle;
            buildMissionEnvironment(battle,2827,0,750,none,2,0,0,5);
            battle.mission=0;
            mapView(battle,"cover-battlefield",{425,160,180},{-.2f,-.53f,-1},false);
            uint32_t underground=0;
            while(!generationPlan(underground,3,0,0).underground)++underground;
            World buried;
            buildMissionEnvironment(buried,underground,3,750,none,0,0,0,5);
            buried.mission=0;
            const Vec3 chamber{-.48f*750,0,-.18f*750};
            const auto floor=buried.surfaceHeight(chamber.x,chamber.z);
            mapView(buried,"cover-underground",{chamber.x+65,floor+20,chamber.z+70},{-.52f,-.08f,-1},false);
            for(const auto &t:city.targets)if(t.generatorAsset=="cover_pipe_crown"){
                const auto forward=rotate(angles(t.yaw),{0,0,-1});
                auto entry=t.center-forward*(t.ai.localHalf.z+12);
                entry.y=city.surfaceHeight(entry.x,entry.z);
                mapView(city,"cover-pipe-route",entry,forward,true);
                break;
            }
        }
        if(surfaceGravity){
            World room;
            room.targets.clear();room.rpgPrototype=true;
            room.position={};room.rpgChassis=2;
            room.interiorRegions.push_back({{-24,-48,24,24},0,28});
            addStructuralPiece(room,"roof_tile",{0,29,-12},{24,1,36});
            addStructuralPiece(room,"ship_bulkhead",{0,14,-46},{24,14,1});
            addStructuralPiece(room,"ship_bulkhead",{23,14,-12},{1,14,36});
            addStructuralPiece(room,"ship_bulkhead",{-23,14,-12},{1,14,36});
            addStructuralPiece(room,"door_header",{-12,16,-27},{5,2,1});
            addStructuralPiece(room,"cargo_container",{13,2.5f,-31},{4,2.5f,4});
            mapView(room,"gravity-floor",room.position,{0,0,-1},true);
            auto transfer=[&](World &scene,Vec3 ray,const char *id){
                const auto origin=scene.eye();
                const auto hit=gravityRay(scene,origin,origin+ray*65.f);
                if(!hit||!beginGravityPull(scene,scene.gravity,scene.position,*hit,13,3.5f))
                    throw std::runtime_error("Gravity gallery could not find a clear surface");
                for(int tick=0;tick<120;++tick)advanceGravity(scene,scene.gravity,scene.position,13,3.5f,1.f/60);
                if(scene.gravity.mode!=GravityMode::Attached)throw std::runtime_error("Gravity gallery transfer did not settle");
                mapView(scene,id,scene.position,{0,0,-1},true);
            };
            auto wall=room;
            transfer(wall,{1,0,0},"gravity-wall");
            transfer(room,{0,1,0},"gravity-ceiling");
        }
        if(voidmaw){
            Json layer;
            std::ifstream file(assets/"models/chassis-voidmaw-appearance.json");
            file>>layer;
            for(const auto& [id,record]:layer.at("models").items()){
                const bool player=id.starts_with("chassis_");
                for(int state=0;state<(player?1:3);++state){
                    World world;
                    Target actor;
                    applyOriginalModel(actor,id);
                    world.elapsed=state==0?0:1.7f;
                    if(player){
                        const std::array<std::string_view,4> names={"brawler","scout","striker","titan"};
                        const auto kind=id.substr(8,id.find('_',8)-8);
                        auto found=std::find(names.begin(),names.end(),kind);
                        if(found==names.end())throw std::runtime_error("Unknown gallery chassis: "+id);
                        Pilot body;body.position={};body.health=100;body.playerSlot=1;
                        body.rpgChassis=int(found-names.begin());
                        body.rpgHullVariant=id.ends_with("_alt");
                        body.weapons.fill(Weapon::Empty);
                        world.mapPilots.push_back(body);
                    }else{
                        actor.active=true;actor.health=100;actor.spawnType=3;
                        actor.ai.airborne=id=="abyssal_kaiju"||id.starts_with("kaiju_tyrant");
                        actor.ai.velocity=state==1?Vec3{3,0,-5}:Vec3{};
                        actor.ai.gaitPhase=state==1?8.5f:0;
                        if(state==2)
                            actor.ai.combatMotion.play(EnemyAction::MeleeWindup,1.2f,.7f);
                        world.updateEnemyBounds(actor);
                        world.targets.push_back(actor);
                    }
                    const auto pose=state==0?"rest":state==1?"travel":"windup";
                    const float span=2*std::max({actor.half.x,actor.half.y,actor.half.z});
                    render(world,actor.center,span,id+"-"+pose);
                    frames.back()["asset"]=id;
                    frames.back()["pose"]=pose;
                    frames.back()["triangles"]=record.at("base_faces");
                }
            }
            Session session(41);
            session.add(1,"Canopy preview");session.phase=Phase::Combat;
            session.world.rpgPrototype=true;session.world.elapsed=1;
            auto &pilot=session.member(1);pilot.body.position={};
            pilot.body.chassisYaw=pilot.body.torsoYaw=0;
            session.enemies.emplace_back();
            auto &enemy=session.enemies.back();
            for(size_t i=0;i<enemyCatalog().size();++i)
                if(std::string_view(enemySpec(i).id)=="kaiju_flea")enemy.spec=int(i);
            session.mission.recipe.enemies={enemy.spec};session.mission.openerTime=100;
            enemy.tactic=1;
            auto &actor=session.world.targets.emplace_back();
            actor.origin={0,0,-5};actor.spawnType=3;actor.active=true;actor.health=8;
            applyOriginalEnemy(actor,enemy.spec);
            for(int tick=0;tick<26;++tick)session.step(1.f/60);
            if(!enemy.grapplePilot)throw std::runtime_error("Canopy gallery could not attach its creature");
            mapView(session.view(1,renderer.armRig()),"voidmaw-canopy",pilot.body.position,{0,0,-1},true);
            const float impactAt=session.world.elapsed;
            pilot.body.rpgKnockdown={impactAt,1};
            int simulated=0;
            for(const auto [label,ticks]:{std::pair{"fall",16},std::pair{"impact",35},std::pair{"recover",120}}){
                while(simulated<ticks){session.step(1.f/60);++simulated;}
                mapView(session.view(1,renderer.armRig()),"voidmaw-knockdown-"+std::string(label),pilot.body.position,{0,0,-1},true);
            }
        }
        if(materials)for(const char* id:{"is_warehouse","is_office","is_water_tower","is_relay","building","industrial","hangar_wall","hangar_gantry","hangar_crane","tunnel_wall","ship_bulkhead","ship_bridge","ship_reactor","service_console","cargo_container","dock_crane","habitat","solar_array","oxygen_tank","mining_rig","city_barrier","broadleaf","rock","coral","weapon_rifle","weapon_heavy_blade","weapon_cannon"}) {
            World world;Target target;applyOriginalModel(target,id);
            target.health=100;target.active=true;target.spawnType=-1;world.targets.push_back(target);
            render(world,target.center,std::max({target.half.x,target.half.y,target.half.z})*2,id);
        }
        if(environmentCatalogPath.empty()&&!weaponWorkings&&!cockpitFamilies&&!weatherFall&&!crossings&&!terrainStudy&&!vehicleAttacks&&!arsenal&&!coverKit&&!surfaceGravity&&!voidmaw&&!motionEffects&&!encounterReactor&&!combatSky&&!enemyEffects&&!menus&&!referenceRefinement&&!destruction&&!skyline&&!landscapes&&!enemyRedesign&&!redesign&&!manualAssets&&!expansion&&!maps&&!openers&&!materials&&!injuries&&!vehicles&&!reticles&&!encounters)for(int chassis=0;chassis<int(chassisIds.size());++chassis)for(int phase:{0,4,9})for(int turn:{-1,0,1}) {
            World world;Pilot body;body.position={};body.playerSlot=1;body.rpgChassis=chassis;body.health=100;
            body.walkingSpeed=2;body.gaitPhase=float(phase);body.torsoYaw=turn*.9f;
            body.weapons.fill(Weapon::Empty);world.mapPilots.push_back(body);
            render(world,{0,6,0},14,"chassis-"+std::string(chassisIds[size_t(chassis)])+"-phase"+std::to_string(phase)+"-turn"+std::to_string(turn));
        }
        if(vehicles)for(int chassis=0;chassis<int(chassisIds.size());++chassis)for(int frame=0;frame<=48;++frame){
            World world;Pilot body;body.position={};body.playerSlot=1;body.rpgChassis=chassis;body.health=100;body.rpgVehicleBlend=frame/48.f;
            body.weapons.fill(Weapon::Empty);world.mapPilots.push_back(body);
            render(world,{0,6,0},14,"vehicle-"+std::string(chassisIds[size_t(chassis)])+"-"+(frame<10?"0":"")+std::to_string(frame));
        }
        if(reticles){
            Session game(723);game.add(1,"Sight preview");GameSettings settings;settings.environment=3;settings.location=0;settings.opener=5;settings.enemy=3;settings.enemyCount=1;
            game.command(1,{{"op","settings"},{"settings",settings}});game.command(1,{{"op","ready"},{"value",true}});game.command(1,{{"op","deploy"}});game.enemies[0].disabled=10000;
            Input in;game.command(1,{{"op","input"},{"epoch",game.epoch},{"input",coop::inputWire(in)}});game.step(1.f/60);
            for(int index=0;index<7;++index){auto &m=game.member(1);m.sheet.equipment.fill(Gear::Empty);m.sheet.heavyOwners={-1,-1,-1};m.vehicleBlend=0;
                if(index==1)m.sheet.equipment[LeftHand]=Gear::Blade;
                if(index==2)m.sheet.equipment[RightHand]=Gear::Rifle;
                if(index==3)m.sheet.equipment[LeftHip]=Gear::ChestBeam;
                if(index==4)m.sheet.equipment[LeftHip]=Gear::NanoShroud;
                if(index==5){m.sheet.equipment[RightHand]=Gear::Cannon;normalizeHeavyEquipment(m.sheet);}
                if(index==6){m.sheet.equipment[RightHand]=Gear::Rifle;m.vehicleBlend=1;}
                auto world=game.view(1,renderer.armRig());mapView(world,"reticle-"+std::to_string(index),world.position,{0,0,-1},true);
            }
        }
        if(injuries){
            Session game(723);game.add(1,"Damage preview");GameSettings settings;
            settings.location=3;settings.environment=3;settings.opener=5;settings.enemy=3;settings.enemyCount=1;
            game.command(1,{{"op","settings"},{"settings",settings}});
            game.command(1,{{"op","ready"},{"value",true}});game.command(1,{{"op","deploy"}});
            game.enemies[0].disabled=10000;
            Input input;game.command(1,{{"op","input"},{"epoch",game.epoch},{"input",coop::inputWire(input)}});game.step(1.f/60);
            auto healthy=game.member(1).sheet;
            struct Condition{const char* id;unsigned flags,missing;};
            for(auto condition:{Condition{"healthy",0,0},Condition{"sensors",1,0},Condition{"lost-left",2,1},Condition{"lost-right",2,2},Condition{"hobbled",4,0},Condition{"targeting",8,0},Condition{"comms",16,0},Condition{"severe",32,0},Condition{"module",64,0},Condition{"combined",127,1}}){
                auto &member=game.member(1);member.injuries=condition.flags;member.missingArms=condition.missing;member.sheet=healthy;
                if(condition.flags&64)destroyModule(member.sheet,Module::TargetingArray);
                auto world=game.view(1,renderer.armRig());
                if(condition.flags&4){world.walkingSpeed=2;world.gaitPhase=3;}
                mapView(world,std::string("injury-")+condition.id,world.position,{0,0,-1},true);
                if(condition.missing){
                    World external;Pilot pilot=world;pilot.playerSlot=1;
                    Vec3 offset=pilot.position;pilot.position={};
                    for(auto &arm:pilot.arms){arm.shoulder=arm.shoulder-offset;arm.elbow=arm.elbow-offset;arm.wrist=arm.wrist-offset;arm.muzzle=arm.muzzle-offset;}
                    external.mapPilots.push_back(pilot);
                    render(external,{0,6,0},14,std::string("external-")+condition.id);
                }
            }
        }
        if(maps)for(int location=0;location<10;++location)for(int region:{0,2,3}) {
            if(region!=0&&location!=0)continue;
            Session game(723);game.add(1,"Preview");GameSettings settings;
            settings.location=location;settings.world=region;settings.opener=5;
            settings.objective=0;settings.enemy=3;settings.enemyCount=1;
            game.command(1,{{"op","settings"},{"settings",settings}});
            game.command(1,{{"op","ready"},{"value",true}});game.command(1,{{"op","deploy"}});
            auto world=game.view(1,renderer.armRig());std::string name=missionLocations()[location].id;
            name+="-region"+std::to_string(region);
            mapView(world,name+"-entry",world.position,{0,0,-1},true);
            mapView(world,name+"-overview",{0,1000,1000},{0,-1,-1},false);
            if(world.interiorRegions.empty()) {
                auto prop=std::find_if(world.targets.begin(),world.targets.end(),[](const Target& t){return t.active&&t.health>0&&t.spawnType<0&&!t.invulnerable;});
                if(prop!=world.targets.end()) {
                    float size=std::max({prop->half.x,prop->half.y,prop->half.z,15.f});
                    Vec3 observer=prop->center+Vec3{-size*2.1f,size*1.25f,-size*2.8f};
                    observer.y=std::max(observer.y,world.surfaceHeight(observer.x,observer.z)+8);
                    mapView(world,name+"-vignette",observer,prop->center-observer,false);
                }
            }
            if(!world.interiorRegions.empty()&&!world.interiorNavNodes.empty()) {
                auto p=world.interiorNavNodes[4];p.y=world.surfaceHeight(p.x,p.z);
                Vec3 look{0,0,-1};
                for(auto edge:world.interiorNavLinks)if(edge[0]==4||edge[1]==4) {
                    auto q=world.interiorNavNodes[edge[0]==4?edge[1]:edge[0]];look=q-p;break;
                }
                mapView(world,name+"-interior",p,look,true);
            }
        }
    }
    std::ofstream report(out/"report.json");report<<Json{{"passed",true},{"native_frames",frames.size()},{"frames",frames}}.dump(2)<<'\n';
    SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"Native asset gallery passed: "<<frames.size()<<" captures / "<<frames.size()*2<<" stereo views\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
