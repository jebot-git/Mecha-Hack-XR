#pragma once
#include "terrain_art.hpp"

// This gallery uses an ordinary generated mission. Previous vertex colours are
// retained for older packs, so a copy with the material flags cleared reproduces
// the previous ground without changing placement, camera, weather or geometry.
template<class View>
static void captureTerrainArt(View&& view, Json& frames){
    struct Setting {const char* name;int biome,location,layer;};
    constexpr Setting settings[]{
        {"desert",0,3,terrainArt::Desert}, {"wilderness",5,1,terrainArt::Wilderness},
        {"volcanic",4,3,terrainArt::Volcanic}, {"city-arterial",3,0,terrainArt::Arterial},
    };
    for(const auto& sample:settings){
        World world;
        constexpr unsigned seed=4271;
        buildMissionEnvironment(world,seed,sample.biome,500,{},sample.location,3,0,5,-1,2);
        world.mission=0;
        const auto& mesh=world.geometry().terrain;
        Vec3 target{};
        float best=std::numeric_limits<float>::infinity();
        for(size_t i=0;i+2<mesh.size();i+=3){
            const auto& v=mesh[i];
            if(v.textured<terrainArt::textureMode)continue;
            if(terrainArt::surfaceLayer(v.textured,v.oneSided)!=sample.layer)continue;
            if(v.textured>terrainArt::textureMode&&(v.u<.05f||v.u>.95f||v.v<0||v.v>std::abs(v.oneSided)))continue;
            const auto middle=(v.position+mesh[i+1].position+mesh[i+2].position)*(1.f/3);
            // Remain in the open insertion court, with some settlement visible in the distance.
            float score=std::hypot(middle.x-15,middle.z-290);
            if(score<best){best=score;target=middle;}
        }
        if(!std::isfinite(best))throw std::runtime_error(std::string("Missing terrain study surface: ")+sample.name);
        Vec3 eye=target+Vec3{-8,0,24};
        eye.y=world.surfaceHeight(eye.x,eye.z)+13;
        const auto ray=target-eye;
        auto control=world;
        for(auto& v:control.editGeometry().terrain){v.textured=0;v.oneSided=0;}
        for(bool painted:{false,true}){
            view(painted?world:control,std::string("terrain-")+sample.name+(painted?"-after":"-before"),eye,ray,false);
            frames.back()["seed"]=seed;
            frames.back()["biome"]=sample.biome;
            frames.back()["terrain_vertices"]=mesh.size();
            frames.back()["camera_position"]={eye.x,eye.y,eye.z};
            frames.back()["camera_target"]={target.x,target.y,target.z};
        }
    }
}
