#pragma once
#include "destruction_visuals.hpp"
#include "surface_weather.hpp"

// Fixed cameras and simulation-clock samples for the offscreen prototype reel.
template<class View>
void captureWeatherAndFalls(View view,Json &frames) {
    auto name=[](const std::string &scene,int frame){
        return scene+"-"+(frame<10?"00":frame<100?"0":"")+std::to_string(frame);
    };
    for(bool submerged:{false,true}){
        Session simulation(4271);
        simulation.biome=submerged?2:3;
        simulation.mission.recipe.location=submerged?8:0;
        auto &world=simulation.world;
        buildMissionEnvironment(world,4271,simulation.biome,500,{},simulation.mission.recipe.location,3,0,5,-1,0);
        world.mission=0;
        simulation.add(1,"Preview");
        auto &pilot=simulation.member(1);
        pilot.sheet.hp=10;
        pilot.body.rpgChassis=2;
        pilot.body.chassisYaw=.58f;
        Vec3 p=world.missionEntry;
        const float ground=world.surfaceHeight(p.x,p.z);
        if(submerged){
            // Flood the open insertion court for a clear side-on comparison:
            // the camera and the entire hull start below the same water surface.
            const float level=ground+38;
            world.waterRegions.push_back({{p.x-70,p.z-70,p.x+70,p.z+70},level,38,0});
            for(auto d:{Vec3{-15,0,-11},Vec3{-11,0,7},Vec3{13,0,-21},Vec3{19,0,-13}}){
                const float floor=world.surfaceHeight(p.x+d.x,p.z+d.z);
                addStructuralPiece(world,"kelp",{p.x+d.x,floor+4,p.z+d.z},{2.4f,4,2.4f});
            }
            p.y=ground+16;
        }else{
            addStructuralPiece(world,"roof_tile",{p.x,ground+23,p.z},{9,1,9});
            world.targets.back().invulnerable=false;
            p.y=world.pilotSupportHeight({p.x,ground+25,p.z});
            if(p.y<ground+21)throw std::runtime_error("Missing gallery platform");
        }
        pilot.body.position=p;
        const Vec3 eye=p+Vec3{19,submerged?-1.f:-6.f,26};
        const auto look=p+Vec3{0,submerged?-2.f:-3.f,0}-eye;
        const float dt=1.f/60;
        const int stride=submerged?12:4;
        for(int frame=0;frame<(submerged?30:42);++frame){
            if(!submerged&&frame==6){world.targets.back().health=0;world.targets.back().active=false;}
            for(int tick=0;tick<stride;++tick){
                world.elapsed+=dt;
                for(auto &fx:world.effects)fx.age+=dt;
                std::erase_if(world.effects,[](auto &fx){return fx.age>destructionEffectLife(fx.model,fx.kind);});
                simulation.stepPilotFall(pilot,dt);
            }
            world.mapPilots={pilot.body};
            view(world,name(submerged?"sink":"fall",frame),eye,look,false,{Lighting::Day,Weather::Clear,7,true});
            auto &frameData=frames.back();
            frameData["feet_m"]=pilot.body.position.y;
            frameData["time_s"]=world.elapsed;
            frameData["impact"]=std::any_of(world.effects.begin(),world.effects.end(),[](auto &fx){return fx.model==rpgLandingWave;});
        }
    }
    struct Study {const char *id;Weather weather;int biome;};
    for(const auto study:{Study{"snow",Weather::Snowstorm,6},Study{"sand",Weather::Sandstorm,0},
                          Study{"ash",Weather::AshCloud,4},Study{"thaw",Weather::Clear,6}}){
        World world;
        buildMissionEnvironment(world,4271,study.biome,500,{},0,3,0,5,-1,0);
        world.mission=0;
        const Vec3 site=world.missionEntry+Vec3{8,0,-28};
        const Vec3 eye=site+Vec3{-11,world.surfaceHeight(site.x-11,site.z+19)-site.y+10,19};
        const Vec3 target{site.x,world.surfaceHeight(site.x,site.z),site.z};
        const auto settings=EnvironmentSettings{Lighting::Day,study.weather,7,false};
        if(study.weather==Weather::Clear)world.previousSurfaceWeather=Weather::Snowstorm;
        for(int frame=0;frame<=30;++frame){
            world.environmentSeconds=world.elapsed=float(frame);
            view(world,name(study.id,frame),eye,target-eye,false,settings);
            frames.back()["weather_age_s"]=frame;
            frames.back()["coverage"]=surfaceWeather(world.previousSurfaceWeather,study.weather,float(frame));
        }
        if(study.weather==Weather::Snowstorm){
            // Render the localized hail tell and the shattering ice at the
            // target using the same effect models, lifetimes and scale as play.
            for(int frame=0;frame<26;++frame){
                const bool impact=frame>=16;
                world.environmentSeconds=21+float(frame)/12;
                world.elapsed=world.environmentSeconds;
                world.effects={{WeaponEffect::Sparks,target+Vec3{0,.22f,0},{0,1,0},Weapon::Empty,
                    impact?float(frame-16)/12:1.65f+float(frame)/12,12,impact?rpgWeatherHail:rpgWeatherWarning}};
                view(world,name("hail",frame),eye,target-eye,false,settings);
            }
        }
    }
}
