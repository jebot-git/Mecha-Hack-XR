#include "cockpit_lighting.hpp"
#include "simulation.hpp"
#include "jaguar_cry.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace iron {
namespace {
bool same(Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
Vec3 normalized(Vec3 v){float n=length(v);return n>.0001f&&std::isfinite(n)?v*(1/n):Vec3{0,1,0};}
float dotProduct(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float byte(float v){return std::round(std::clamp(v,0.f,1.f)*255)/255;}
}
bool CockpitLighting::operator==(const CockpitLighting& b)const{return night==b.night&&same(sky,b.sky)&&same(ground,b.ground)&&same(direction,b.direction);}
CockpitLighting cockpitLighting(const World& w){
    // A negative time requests the stable profile without the storm pulse.
    auto environment=environmentVisuals(w.activeEnvironment(),w.night,w.skyColor,w.groundColor,-1);
    return {environment.sky,environment.ground,w.missionLight,environment.night};
}
Vec3 bakeCockpitColour(Vec3 p,Vec3 n,Vec3 material,float emission,const CockpitLighting& profile){
    if(emission>.02f)return material; // monitor phosphor and instrument masks
    n=normalized(n);float facing=dotProduct(n,normalized(profile.direction));
    // Same back-face half-light convention as the native mission geometry.
    float direct=facing>=0?facing:-facing*.5f;
    float upper=std::clamp(n.y*.5f+.5f,0.f,1.f);
    Vec3 light=Vec3{1,1,1}*(profile.night?.003f:.12f);
    light=light+profile.sky*((profile.night?.10f:.25f)*upper)+profile.ground*((profile.night?.05f:.15f)*(1-upper));
    Vec3 sun=profile.night?Vec3{.008f,.008f,.01f}:Vec3{.50f,.48f,.42f};
    light=light+sun*direct;
    if(profile.night){
        // Two low maintenance lamps under the canopy brow, plus weak interior
        // bounce. These contributions are baked, not evaluated by either eye.
        float fill=.25f;
        for(auto source:{Vec3{-.7f,.1f,-.55f},Vec3{.7f,.1f,-.55f}}){auto delta=source-p;float distance=length(delta);
            float incidence=std::abs(dotProduct(n,normalized(delta)));fill+=incidence/(1+distance*distance*3);
        }
        Vec3 red=jaguarCRY(0xf009);light=light+red*std::min(fill,1.5f);
    }
    return {byte(material.x*light.x),byte(material.y*light.y),byte(material.z*light.z)};
}
std::vector<float> bakeCockpitVertices(std::span<const float> source,const CockpitLighting& profile){
    if(source.size()%10)throw std::invalid_argument("Cockpit vertices require the ISCP 10-float layout");
    std::vector<float> data(source.begin(),source.end());
    for(size_t i=0;i<data.size();i+=10){auto c=bakeCockpitColour({data[i],data[i+1],data[i+2]},{data[i+3],data[i+4],data[i+5]},{data[i+6],data[i+7],data[i+8]},data[i+9],profile);
        data[i+6]=c.x;data[i+7]=c.y;data[i+8]=c.z;
    }return data;
}
}
