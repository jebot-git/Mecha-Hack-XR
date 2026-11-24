#pragma once
#include "simulation.hpp"
#include <algorithm>
#include <cmath>

namespace iron {
// Static overhead surfaces projected into a height texture. Its texels cover
// roughly two metres at mission scale. Water follows its authored shoreline.
struct SurfaceShelter {
    int width,height;
    std::array<float,4> bounds;
    std::vector<float> ceiling;
    explicit SurfaceShelter(const World &w):SurfaceShelter(w.movementBounds,w.interiorRegions,w.interiorDecks,w.waterRegions){}
    SurfaceShelter(std::array<float,4> area,const std::vector<World::InteriorRegion>& regions,
                   const std::vector<World::InteriorDeck>& decks,const std::vector<ProceduralWater>& water):bounds(area) {
        const float spanX=std::max(1.f,bounds[2]-bounds[0]),spanZ=std::max(1.f,bounds[3]-bounds[1]);
        width=std::clamp(int(std::ceil(spanX/2)),1,1024);
        height=std::clamp(int(std::ceil(spanZ/2)),1,1024);
        ceiling.assign(size_t(width)*height,-1.e6f);
        const float dx=spanX/width,dz=spanZ/height;
        auto stamp=[&](const std::array<float,4>& rect,auto level){
            const int left=std::clamp(int(std::ceil((rect[0]-bounds[0])/dx-.5f)),0,width);
            const int right=std::clamp(int(std::floor((rect[2]-bounds[0])/dx-.5f))+1,0,width);
            const int near=std::clamp(int(std::ceil((rect[1]-bounds[1])/dz-.5f)),0,height);
            const int far=std::clamp(int(std::floor((rect[3]-bounds[1])/dz-.5f))+1,0,height);
            for(int z=near;z<far;++z)for(int x=left;x<right;++x){
                auto &cell=ceiling[size_t(z)*width+x];
                cell=std::max(cell,level(bounds[0]+(x+.5f)*dx,bounds[1]+(z+.5f)*dz));
            }
        };
        for(const auto &r:regions)stamp(r.bounds,[&](float,float){return r.ceiling;});
        for(const auto &d:decks)stamp(d.bounds,[&](float x,float z){return d.height(x,z)-1.2f;});
        for(const auto &r:water)stamp(r.bounds,[&](float x,float z){
            return proceduralWaterEdge(r,x,z)>=0?r.level:-1.e6f;
        });
    }
};
}
