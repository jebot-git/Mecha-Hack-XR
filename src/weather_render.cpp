#include "render.hpp"
namespace iron {
void Renderer::applyEnvironment(GLuint shader,float gain,const avp::Matrix* pose){
    auto [entry,fresh]=environmentLocations.try_emplace(shader);
    if(fresh)entry->second={glGetUniformLocation(shader,"envFog"),glGetUniformLocation(shader,"envTint"),glGetUniformLocation(shader,"envGain"),glGetUniformLocation(shader,"worldPose"),glGetUniformLocation(shader,"lampOriginRange"),glGetUniformLocation(shader,"lampAxisBase")};
    auto u=entry->second;auto fog=environmentView.fog,tint=environmentView.tint;
    if(worldPass){tint={tint.x*teamTint.x,tint.y*teamTint.y,tint.z*teamTint.z};}
    else {tint={1,1,1};gain=1;}
    float density=environmentView.density;if(visibilityPenalty)density=std::max(density,1/visibilityBase);
    if(searchlightPass||emissiveWorldMarkers){density=0;tint={1,1,1};gain=1;}
    glUniform4f(u.fog,fog.x,fog.y,fog.z,worldPass?density:0);
    glUniformMatrix4fv(u.pose,1,GL_FALSE,pose?pose->data():avp::identity.data());
    glUniform4f(u.lamp,localLampOrigin.x,localLampOrigin.y,localLampOrigin.z,
                worldPass&&!searchlightPass&&!emissiveWorldMarkers?localLampRange:0);
    glUniform4f(u.axis,localLampAxis.x,localLampAxis.y,localLampAxis.z,visibilityBase);
    glUniform3f(u.tint,tint.x,tint.y,tint.z);glUniform1f(u.gain,gain);
}
void Renderer::drawWeather(const World& w,const avp::Matrix& camera){
    if(environmentView.precipitation<=0)return;
    using namespace avp;
    auto sprites=weatherSprites(w.activeEnvironment(),w.position+Vec3{0,12,0},w.weatherTime());
    const Vec3 eye{camera[12],camera[13],camera[14]};
    for(auto& sprite:sprites)if(sprite.layer==WeatherLayer::SandSheet)
        sprite.position.y=w.surfaceHeight(sprite.position.x,sprite.position.z)+sprite.height*.5f+.15f;
    // Same back-to-front, nearest-filtered, zero-key sprite path as cartridge
    // fire/smoke; no separate weather mesh, procedural texture or additive pass.
    std::stable_sort(sprites.begin(),sprites.end(),[&](const auto& a,const auto& b){return dot(a.position-eye,a.position-eye)>dot(b.position-eye,b.position-eye);});
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
    for(const auto& sprite:sprites){
        auto local=point(worldView,{sprite.position.x,sprite.position.y,sprite.position.z});float depth=-local[2];
        if(depth+sprite.height<worldNear||depth-sprite.height>=worldFar||depth<2||length(sprite.position-w.eye())<4)continue;
        if(w.environment.weather==Weather::Snowstorm&&!w.exposedToPrecipitation(sprite.position))continue;
        if(sprite.position.y+sprite.height*.5f<w.surfaceHeight(sprite.position.x,sprite.position.z))continue;
        Matrix billboard=camera;billboard[12]=sprite.position.x;billboard[13]=sprite.position.y;billboard[14]=sprite.position.z;
        if(sprite.layer==WeatherLayer::SandGrain||sprite.layer==WeatherLayer::HazardStreak){
            Vec3 right{camera[0],camera[1],camera[2]},up{camera[4],camera[5],camera[6]};
            float x=dot(sprite.drift,right),y=dot(sprite.drift,up),n=std::hypot(x,y);
            if(n>.01f){x/=n;y/=n;auto across=right*x+up*y,vertical=up*x-right*y;
                billboard[0]=across.x;billboard[1]=across.y;billboard[2]=across.z;
                billboard[4]=vertical.x;billboard[5]=vertical.y;billboard[6]=vertical.z;}
        }
        spriteAlpha=sprite.opacity;spriteSoftness=sprite.layer==WeatherLayer::Precipitation?0:1;
        art(weatherSpriteAsset,multiply(billboard,multiply(translation(-sprite.width*.5f,sprite.height*.5f,0),scaling(sprite.width,-sprite.height,1))),{0,float(sprite.frame)/weatherSpriteFrames,1,1.f/weatherSpriteFrames},sprite.colour);
    }
    spriteAlpha=1;spriteSoftness=0;
    glDepthMask(GL_TRUE);glDisable(GL_BLEND);
}
}
