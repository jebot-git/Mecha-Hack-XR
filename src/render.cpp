#include "render.hpp"
#include "destruction_visuals.hpp"
#include "presentation.hpp"
#include "searchlight.hpp"
#include "rpg/weak_points.hpp"
#include "rpg/mecha_ranged.hpp"
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <fstream>
#include <iostream>
#include <bit>
#include <cstring>
#include <nlohmann/json.hpp>
namespace iron {
using namespace avp;
static GLuint shader(GLenum type,const char* source) {
    GLuint s=glCreateShader(type); ironShaderSource(s,source); glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok) { char log[2048]; glGetShaderInfoLog(s,sizeof log,nullptr,log); throw std::runtime_error(log); }
    return s;
}
Renderer::Renderer(const std::filesystem::path& cockpit) {
    auto v=shader(GL_VERTEX_SHADER,R"(#version 330 core
layout(location=0) in vec3 p;
layout(location=1) in float light;
layout(location=2) in vec4 vertexColor;
uniform mat4 transform;uniform mat4 worldPose;out vec3 worldPoint;
out float shade;out float eyeDepth;out vec4 primitiveColor;
void main(){worldPoint=(worldPose*vec4(p,1)).xyz;gl_Position=transform*vec4(p,1);eyeDepth=gl_Position.w;shade=light;primitiveColor=vertexColor;})");
    auto f=shader(GL_FRAGMENT_SHADER,R"(#version 330 core
in float shade;in float eyeDepth; uniform vec3 tint;uniform float primitiveOpacity;uniform vec4 envFog;uniform vec3 envTint;uniform float envGain;out vec4 color;
in vec4 primitiveColor;uniform bool vertexColors;
uniform vec4 lampOriginRange;uniform vec4 lampAxisBase;in vec3 worldPoint;
float searchlightTransmission(){float density=envFog.a;
if(lampOriginRange.w>0.0){vec3 delta=worldPoint-lampOriginRange.xyz;float distance=length(delta);
if(distance<lampOriginRange.w&&dot(delta/max(distance,.001),lampAxisBase.xyz)>.9063078)density*=lampAxisBase.w/lampOriginRange.w;}
return floor(clamp(1.0-density*max(0.0,eyeDepth),0.0,1.0)*255.0)/255.0;}
void main(){color=vertexColors?primitiveColor:vec4(tint*shade,primitiveOpacity);color.rgb=mix(envFog.rgb,color.rgb*envTint*envGain,searchlightTransmission());})");
    program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);
    glDeleteShader(v);glDeleteShader(f);
    GLint ok=0;glGetProgramiv(program,GL_LINK_STATUS,&ok);if(!ok) throw std::runtime_error("Shader link failed");
    transform=glGetUniformLocation(program,"transform");tint=glGetUniformLocation(program,"tint");
    primitiveOpacity=glGetUniformLocation(program,"primitiveOpacity");
    vertexColors=glGetUniformLocation(program,"vertexColors");
    glUseProgram(program);glUniform1f(primitiveOpacity,1);
    const float vertices[]={
        -1,-1,1,1, 1,-1,1,1, 1,1,1,1, -1,-1,1,1, 1,1,1,1, -1,1,1,1,
        1,-1,-1,.6, -1,-1,-1,.6, -1,1,-1,.6, 1,-1,-1,.6, -1,1,-1,.6, 1,1,-1,.6,
        -1,-1,-1,.7, -1,-1,1,.7, -1,1,1,.7, -1,-1,-1,.7, -1,1,1,.7, -1,1,-1,.7,
        1,-1,1,.8, 1,-1,-1,.8, 1,1,-1,.8, 1,-1,1,.8, 1,1,-1,.8, 1,1,1,.8,
        -1,1,1,1, 1,1,1,1, 1,1,-1,1, -1,1,1,1, 1,1,-1,1, -1,1,-1,1,
        -1,-1,-1,.5, 1,-1,-1,.5, 1,-1,1,.5, -1,-1,-1,.5, 1,-1,1,.5, -1,-1,1,.5};
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,4*sizeof(float),nullptr);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,1,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(3*sizeof(float)));
    glGenVertexArrays(1,&enemyEffectVao);glBindVertexArray(enemyEffectVao);
    glGenBuffers(1,&enemyEffectVbo);glBindBuffer(GL_ARRAY_BUFFER,enemyEffectVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(EnemyEffectVertex),nullptr);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(EnemyEffectVertex),reinterpret_cast<void*>(offsetof(EnemyEffectVertex,color)));
    glBindVertexArray(vao);
    loadCockpit(cockpit);
    loadCockpitEffects();
    loadSearchlight();
    loadArt(cockpit.parent_path()/"art");
    loadPanelBatch();
    loadModels(cockpit.parent_path()/"models");
    loadWeapons(cockpit.parent_path()/"models");
    gameplayRoot=cockpit.parent_path()/"gameplay";
}
Renderer::~Renderer(){
    glDeleteVertexArrays(1,&panelBoxes);glDeleteVertexArrays(1,&panelGlyphs);
    glDeleteBuffers(1,&panelBuffer);glDeleteProgram(panelProgram);
    glDeleteVertexArrays(1,&enemyEffectVao);glDeleteBuffers(1,&enemyEffectVbo);
    glDeleteProgram(lightProgram);glDeleteVertexArrays(1,&lightVao);glDeleteBuffers(1,&lightVbo);
    glDeleteFramebuffers(1,&aaFbo);glDeleteRenderbuffers(1,&aaColor);glDeleteRenderbuffers(1,&aaDepth);
    glDeleteTextures(1,&originalTexture);
    glDeleteTextures(1,&terrainTexture);
    clearWorld();glDeleteProgram(worldProgram);glDeleteTextures(2,worldTextures);glDeleteTextures(2,pickupTextures);
    glDeleteBuffers(1,&weaponBuffer.vbo);glDeleteVertexArrays(1,&weaponBuffer.vao);
    for(auto& [name,m]:models){glDeleteBuffers(1,&m.vbo);glDeleteVertexArrays(1,&m.vao);}
    for(auto& [name,t]:textures)glDeleteTextures(1,&t.id);
    glDeleteBuffers(1,&artVbo);glDeleteVertexArrays(1,&artVao);glDeleteProgram(artProgram);
    glDeleteProgram(skyProgram);glDeleteVertexArrays(1,&skyVao);
    glDeleteBuffers(1,&cockpitVbo);glDeleteVertexArrays(1,&cockpitVao);glDeleteProgram(cockpitProgram);
    glDeleteBuffers(1,&glassVbo);glDeleteVertexArrays(1,&glassVao);glDeleteProgram(glassProgram);
    glDeleteBuffers(1,&detailVbo);glDeleteVertexArrays(1,&detailVao);glDeleteProgram(detailProgram);
    glDeleteBuffers(1,&vbo);glDeleteVertexArrays(1,&vao);glDeleteProgram(program);}
// Instruments are drawn after the canopy, in an unfogged depth-tested pass.
void Renderer::loadPanelBatch(){
    GLuint v=shader(GL_VERTEX_SHADER,R"(#version 330 core
layout(location=0) in vec3 p;layout(location=1) in float shade;
layout(location=4) in mat4 transform;layout(location=8) in vec4 ink;
layout(location=9) in vec4 region;uniform bool glyph;out vec4 color;out vec2 uv;
void main(){gl_Position=transform*vec4(p,1);color=vec4(ink.rgb*(glyph?1.0:shade),ink.a);uv=region.xy+p.xy*region.zw;})");
    GLuint f=shader(GL_FRAGMENT_SHADER,R"(#version 330 core
in vec4 color;in vec2 uv;uniform bool glyph;uniform sampler2D atlas;out vec4 result;
void main(){result=color;if(glyph){vec4 texel=texture(atlas,uv);if(texel.a<.1)discard;result*=texel;}})");
    panelProgram=glCreateProgram();glAttachShader(panelProgram,v);glAttachShader(panelProgram,f);
    glLinkProgram(panelProgram);glDeleteShader(v);glDeleteShader(f);
    GLint ok=0;glGetProgramiv(panelProgram,GL_LINK_STATUS,&ok);
    if(!ok)throw std::runtime_error("Cockpit instrument shader link failed");
    panelTextured=glGetUniformLocation(panelProgram,"glyph");
    glGenBuffers(1,&panelBuffer);
    auto setup=[&](GLuint &array,GLuint mesh,bool glyph){
        glGenVertexArrays(1,&array);glBindVertexArray(array);glBindBuffer(GL_ARRAY_BUFFER,mesh);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,glyph?2:3,GL_FLOAT,GL_FALSE,(glyph?2:4)*sizeof(float),nullptr);
        if(!glyph){glEnableVertexAttribArray(1);glVertexAttribPointer(1,1,GL_FLOAT,GL_FALSE,4*sizeof(float),reinterpret_cast<void*>(3*sizeof(float)));}
        glBindBuffer(GL_ARRAY_BUFFER,panelBuffer);
        for(unsigned i=0;i<6;++i){
            glEnableVertexAttribArray(4+i);
            glVertexAttribPointer(4+i,4,GL_FLOAT,GL_FALSE,sizeof(PanelInstance),reinterpret_cast<void*>(4*i*sizeof(float)));
            glVertexAttribDivisor(4+i,1);
        }
    };
    setup(panelBoxes,vbo,false);setup(panelGlyphs,artVbo,true);glBindVertexArray(vao);
}
void Renderer::panelInstance(bool glyph,const Matrix &model,Vec3 color,const std::array<float,4>& uv){
    if(glyph!=panelIsText)flushPanel();
    panelIsText=glyph;
    panelInstances.push_back({multiply(vp,model),{color.x,color.y,color.z,glyph?spriteAlpha:shapeOpacity},uv});
}
void Renderer::flushPanel(){
    if(panelInstances.empty())return;
    glUseProgram(panelProgram);glBindVertexArray(panelIsText?panelGlyphs:panelBoxes);
    glUniform1i(panelTextured,panelIsText);
    if(panelIsText){glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,textures.at("hud-font").id);}
    glBindBuffer(GL_ARRAY_BUFFER,panelBuffer);
    glBufferData(GL_ARRAY_BUFFER,panelInstances.size()*sizeof(PanelInstance),panelInstances.data(),GL_STREAM_DRAW);
    glDrawArraysInstanced(GL_TRIANGLES,0,panelIsText?6:36,GLsizei(panelInstances.size()));
    panelInstances.clear();
}
void Renderer::box(const Matrix& model,Vec3 c) {
    c=c*instrumentGain;
    if(batchingPanel){panelInstance(false,model,c,{});return;}
    applyEnvironment(program,environmentView.modelGain,&model);
    glUniform1f(primitiveOpacity,shapeOpacity);
    auto m=multiply(vp,model);glUniformMatrix4fv(transform,1,GL_FALSE,m.data());glUniform3f(tint,c.x,c.y,c.z);glDrawArrays(GL_TRIANGLES,0,36);
}
void Renderer::text(const std::string& str,const Matrix& base,float s,Vec3 c) {
    c=c*instrumentGain;
    float x=0;
    for(unsigned char ch:str) {
        ch=static_cast<unsigned char>(std::toupper(ch));
        if(ch>=32 && ch<128 && ch!=' ') {
            int slot=ch-32;
            // Authored 5x7 glyphs occupy an 8x8 atlas cell. Preserve all seven
            // rows at native menu sizes while retaining the four-unit advance.
            const auto model=multiply(base,multiply(translation(x,-7*s,0),scaling(6*s,8*s,1)));
            const std::array<float,4> uv{float(slot%16)/16,float(slot/16+1)/6,1.f/16,-1.f/6};
            if(batchingPanel)panelInstance(true,model,c,uv);
            else art("hud-font",model,uv,c);
        }
        x+=4*s;
    }
}
void Renderer::draw(const World& w,const Matrix& projection,const Matrix& camera,const Matrix& head,bool paused,const Frontend* menu) {
    GLint destination=0,read=0,viewport[4];glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&destination);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read);glGetIntegerv(GL_VIEWPORT,viewport);
    if(!aaFbo){glGenFramebuffers(1,&aaFbo);glGenRenderbuffers(1,&aaColor);glGenRenderbuffers(1,&aaDepth);}
    glBindFramebuffer(GL_FRAMEBUFFER,aaFbo);
    if(aaWidth!=viewport[2]||aaHeight!=viewport[3]){
        aaWidth=viewport[2];aaHeight=viewport[3];GLint maxSamples=0;glGetIntegerv(GL_MAX_SAMPLES,&maxSamples);int samples=std::min(4,maxSamples);
        glBindRenderbuffer(GL_RENDERBUFFER,aaColor);glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_RGBA8,aaWidth,aaHeight);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,aaColor);
        glBindRenderbuffer(GL_RENDERBUFFER,aaDepth);glRenderbufferStorageMultisample(GL_RENDERBUFFER,samples,GL_DEPTH_COMPONENT32F,aaWidth,aaHeight);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,aaDepth);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Multisample scene framebuffer incomplete");
    }
#ifndef __ANDROID__
    glEnable(GL_MULTISAMPLE);
#endif
    Matrix view=camera,trackedHead=head;
    if(cockpitFx.visible&&!w.rpgDroneView&&(!menu||menu->screen==Screen::Playing)&&!(menu&&menu->spectating)){
        const auto q=w.cockpitOrientation();
        const auto p=w.cockpitOrigin();
        auto body=poseMatrix({{q.x,q.y,q.z,q.w},{p.x,p.y,p.z}});
        auto motion=multiply(body,multiply(cockpitFx.seat,inverseRigid(body)));
        view=multiply(motion,camera);trackedHead=multiply(motion,head);
    }
    glViewport(0,0,aaWidth,aaHeight);drawScene(w,projection,view,trackedHead,paused,menu);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,aaFbo);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,GLuint(destination));
    glBlitFramebuffer(0,0,aaWidth,aaHeight,viewport[0],viewport[1],viewport[0]+viewport[2],viewport[1]+viewport[3],GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(read));glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
}
bool Renderer::visibleWorld(const Model& model,const Matrix& pose)const{
    if(!worldPass)return true;
    auto local=(model.low+model.high)*.5f;auto center=point(multiply(worldView,pose),{local.x,local.y,local.z});
    float scale=0;for(int c=0;c<3;++c)scale=std::max(scale,std::hypot(pose[c*4],pose[c*4+1],pose[c*4+2]));
    return visibleViewSphere({center[0],center[1],center[2]},length(model.high-model.low)*.5f*scale);
}
bool Renderer::visibleViewSphere(Vec3 center,float radius) const{
    const float depth=-center.z;
    if(depth+radius<worldNear-.02f||depth-radius>worldFar+.02f)return false;
    for(const auto plane:worldFrustum)if(dot(plane,center)<-radius-.02f)return false;
    return true;
}
void Renderer::drawCockpit(const World& w,const Matrix& camera,bool paused){
    Matrix cockpit=cockpitPose(w);
    if(!w.missile.active) {
        // Glass has no depth writes. Render hardware afterward so frames, CRTs
        // and their legends mask glazing/beads throughout the canopy cutouts.
        if(!w.lifeSpent)drawCockpitGlass(w,cockpit,camera);
        if(cockpitVertices) {
            glUseProgram(cockpitProgram);applyEnvironment(cockpitProgram,1,&cockpit);glUniform1i(cockpitBaked,1);glBindVertexArray(cockpitVao);
            glUniform1fv(cockpitPower,5,cockpitFx.power.data());
            glUniform1f(cockpitPowerRail,cockpitFx.booting()?cockpitFade(.3f,1.3f,cockpitFx.bootTime):1);
            auto m=multiply(vp,cockpit);
            glUniformMatrix4fv(cockpitTransform,1,GL_FALSE,m.data());
            auto localEye=point(multiply(inverseRigid(cockpit),camera));
            glUniform3fv(cockpitEye,1,localEye.data());
            const auto& trim=cockpitTrims[activeCockpit];
            const auto* interior=activeInterior();
            const auto hullVertices=interior?interior->mesh.hullVertices:cockpitHullVertices+trim.hullVertices;
            const auto trimVertices=trim.count();
            glDrawArrays(GL_TRIANGLES,0,hullVertices);
            float hatch=w.lifeSpent?std::max(0.f,w.ejectionTime-1):0;
            hatchMotion=multiply(translation(0,16*hatch,5*hatch),rotation(hatch*.8f,hatch*.7f));
            auto released=multiply(cockpit,hatchMotion);
            applyEnvironment(cockpitProgram,1,&released);m=multiply(vp,released);glUniformMatrix4fv(cockpitTransform,1,GL_FALSE,m.data());
            const auto total=interior?interior->mesh.count():cockpitVertices+trimVertices;
            glDrawArrays(GL_TRIANGLES,hullVertices,total-hullVertices);
            if(!interior){
                applyEnvironment(cockpitProgram,1,&cockpit);m=multiply(vp,cockpit);glUniformMatrix4fv(cockpitTransform,1,GL_FALSE,m.data());
                const float plating=100.f*float(w.rpgAp)/std::max(1,w.rpgMaxAp);
                for(unsigned i=0;i<lampRanges.size();++i){
                    const unsigned hullSegment=unsigned(lampRanges.size())-1-i;
                    const auto hull=armorLamp(w.health,hullSegment);
                    if(w.rpgPrototype){
                        glUniform4fv(cockpitLampBounds,1,lampBounds[i].data());
                        glUniform2f(cockpitLampFill,statusLampFill(plating,i),statusLampFill(w.health,hullSegment));
                    }
                    glUniform4f(cockpitLamp,hull.x,hull.y,hull.z,w.rpgPrototype?2.f:1.f);
                    glDrawArrays(GL_TRIANGLES,lampRanges[i].first+trimVertices,lampRanges[i].second);
                }
                applyEnvironment(cockpitProgram,1,&released);m=multiply(vp,released);
                glUniformMatrix4fv(cockpitTransform,1,GL_FALSE,m.data());
                if(w.rpgPrototype){
                    const float heat=reactorWarning(w.rpgHeat);
                    const float frequency=w.rpgOverheated?4.f:1.8f;
                    const float pulse=.6f+.4f*std::sin(float(std::fmod(cockpitFx.clock*frequency,1.0))*2*pi);
                    const float light=w.rpgOverheated?.30f+.70f*pulse:heat*(.42f+.52f*pulse);
                    glUniform4f(cockpitLamp,.019f+light*.96f,.003f+light*.055f,.002f+light*.019f,1);
                }else glUniform4f(cockpitLamp,0,0,0,0);
                glDrawArrays(GL_TRIANGLES,reactorLampRange.first+trimVertices,reactorLampRange.second);
            }
            glUniform4f(cockpitLamp,0,0,0,0);glUniform1i(cockpitBaked,0);
            glUseProgram(program);glBindVertexArray(vao);
            batchingPanel=true;
            instruments(w,cockpit,paused);
            batchingPanel=false;flushPanel();
            glUseProgram(program);glBindVertexArray(vao);
            instrumentGain=1;
            if(!w.lifeSpent)drawCockpitDetails(w,cockpit,camera);
            drawInsertionCockpit(w,cockpit);
        }

    }
}
void Renderer::drawEnemyStreak(Vec3 from, Vec3 to, float width, Vec3 color, const Matrix& camera,float taper) {
    const Vec3 delta = to - from;
    const float span = length(delta);
    if (span < .001f) return;
    const Vec3 forward = delta * (1.f / span);
    const Vec3 center = (from + to) * .5f;
    const Vec3 eye{camera[12], camera[13], camera[14]};
    Vec3 normal = cross(forward, eye - center);
    if (length(normal) < .001f) normal = cross(forward, {camera[0], camera[1], camera[2]});
    const Vec3 side = unit(normal, {0, 1, 0});
    const float start=1-std::max(0.f,-taper)*.94f,end=1-std::max(0.f,taper)*.94f;
    const Vec3 nearEdge=side*(width*start),farEdge=side*(width*end);
    const EnemyEffectVertex corners[]{
        {from-nearEdge,color,shapeOpacity*start},{to-farEdge,color,shapeOpacity*end},
        {to+farEdge,color,shapeOpacity*end},{from+nearEdge,color,shapeOpacity*start}};
    for(int i:{0,1,2,0,2,3})enemyEffectVertices.push_back(corners[i]);
}

void Renderer::drawMechaWave(const Projectile& shot,const Matrix& camera){
    // The edge of the bright core matches the collision radius. Layer two
    // keeps its distinct diamond silhouette when colours are hard to see.
    const float radius=rpg::mechaWaveRadius;
    const auto viewPosition=point(worldView,{shot.position.x,shot.position.y,shot.position.z});
    if(!visibleViewSphere({viewPosition[0],viewPosition[1],viewPosition[2]},radius*2.2f))return;
    const bool amber=shot.visual==ProjectileVisual::WaveA;
    const Vec3 rim=amber?Vec3{1,.40f,.08f}:Vec3{.13f,.81f,1};
    const Vec3 core=amber?Vec3{1,.94f,.65f}:Vec3{.83f,1,1};
    const Vec3 outline=amber?Vec3{.28f,.07f,.02f}:Vec3{.02f,.11f,.27f};
    const Vec3 right{camera[0],camera[1],camera[2]},up{camera[4],camera[5],camera[6]};
    constexpr float diag=.70710678f;
    constexpr std::array<std::array<float,2>,8> ring{{{0,1},{diag,diag},{1,0},{diag,-diag},
                                                      {0,-1},{-diag,-diag},{-1,0},{-diag,diag}}};
    const int stride=amber?1:2;
    const Vec3 heading=unit(shot.velocity);
    auto oldOpacity=shapeOpacity;shapeOpacity=.39f;
    drawEnemyStreak(shot.position-heading*(radius*2.0f),shot.position,radius*.45f,rim,camera,-.9f);
    shapeOpacity=oldOpacity;
    for(int j=0;j<8;j+=stride){
        auto radial=[&](int index){
            const auto &v=ring[index%8];
            return (right*v[0]+up*v[1])*radius;
        };
        const auto a=radial(j),b=radial(j+stride);
        const EnemyEffectVertex edgeA{shot.position+a*.78f,rim,1},edgeB{shot.position+b*.78f,rim,1};
        const EnemyEffectVertex outerA{shot.position+a,outline,.96f},outerB{shot.position+b,outline,.96f};
        enemyEffectVertices.insert(enemyEffectVertices.end(),{
            {shot.position,core,1},edgeA,edgeB,edgeA,outerA,outerB,edgeA,outerB,edgeB});
    }
}

void Renderer::flushEnemyEffects() {
    if (enemyEffectVertices.empty()) return;
    glUseProgram(program);
    glBindVertexArray(enemyEffectVao);
    glBindBuffer(GL_ARRAY_BUFFER,enemyEffectVbo);
    glBufferData(GL_ARRAY_BUFFER,enemyEffectVertices.size()*sizeof(EnemyEffectVertex),
                 enemyEffectVertices.data(),GL_STREAM_DRAW);
    applyEnvironment(program,1.f,&identity);
    glUniformMatrix4fv(transform,1,GL_FALSE,vp.data());
    glUniform1i(vertexColors,GL_TRUE);
    glDrawArrays(GL_TRIANGLES,0,GLsizei(enemyEffectVertices.size()));
    glUniform1i(vertexColors,GL_FALSE);
    glBindVertexArray(vao);
    enemyEffectVertices.clear();
}

void Renderer::drawEnemyEffect(const WeaponEffect& fx, const Matrix& camera, const World& world) {
    using namespace rpg;
    const float progress = fx.age / enemyEffectLife(fx.model);
    if (!std::isfinite(progress) || progress >= 1.f) return;
    const bool attackLane=fx.model==rpgHeavyCrossfireWarning||fx.model==rpgAtomicBreathWarning||fx.model==rpgAtomicBreath;
    const bool hazardEffect=(fx.model>=rpgWeatherWarning&&fx.model<=rpgWeatherFire)||fx.model==rpgWeatherHail;
    float visualScale=1.f;
    switch(fx.model){
    case rpgEnemyKinetic:case rpgEnemyEnergy:case rpgEnemyShieldHit:case rpgWeaponArc:
        visualScale=1.25f;break;
    case rpgWeaponHeavy:case rpgScatterMuzzle:case rpgPulseMuzzle:
        visualScale=1.30f;break;
    case rpgWeaponMuzzle:case rpgRocketIgnition:case rpgWeaponBlast:
        visualScale=1.45f;break;
    case rpgEnemyHit:case rpgCutterSparks:
        visualScale=1.60f;break;
    }
    const float size = visualScale * std::clamp(fx.size, .2f, attackLane?180.f:
        fx.model==rpgVehicleVolleyWarning?70.f:fx.model==rpgWeaponPulse?22.f:hazardEffect?16.f:7.f);
    const float fade = 1.f - progress;
    const Vec3 aim = unit(fx.direction);
    const Vec3 side = unit(cross(aim, std::abs(aim.y) < .85f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
    const Vec3 up = cross(side, aim);
    const float savedOpacity = shapeOpacity;
    shapeOpacity = std::min(1.f, 1.6f * fade);
    auto stroke = [&](Vec3 from, Vec3 to, float width, Vec3 color,float taper=0){
        drawEnemyStreak(from,to,width*size,color,camera,taper);
    };
    auto segment = [&](float angle, float radius) {
        return (side * std::cos(angle) + up * std::sin(angle)) * (radius * size);
    };
    auto arc = [&](Vec3 center, float radius, float twist, int sides, int count, Vec3 color) {
        for (int i = 0; i < count; ++i) {
            const float angle = twist + float(i) * 2.f * pi / sides;
            stroke(center + segment(angle, radius), center + segment(angle + 2.f * pi / sides, radius), .028f, color);
        }
    };
    const Vec3 origin = fx.position;
    const Vec3 cyan{.25f, .8f, 1.f};
    const Vec3 orange{1.f, .48f, .12f};
    auto sprite=[&](int cell,Vec3 point,float diameter,Vec3 color,float alpha,bool floor=false,bool additive=true){
        const bool fire=cell>=32;
        static const std::string flipbook="mecha-fire-flipbook", accents="mecha-weapon-fx";
        const std::string& texture=fire?flipbook:accents;
        if(!textures.contains(texture))return;
        if(worldPass){
            const Vec3 eye{camera[12],camera[13],camera[14]};
            const float depth=dot(eye-point,{camera[8],camera[9],camera[10]});
            const float radius=floor?diameter*.71f:0.f;
            if(depth+radius<worldNear||depth-radius>=worldFar)return;
        }
        Matrix pose=floor?rotation(0,-pi*.5f):camera;
        pose[12]=point.x;pose[13]=point.y;pose[14]=point.z;
        const float oldAlpha=spriteAlpha;spriteAlpha=alpha;
        if(fire)cell-=32;
        const int columns=fire?4:8;
        const float columnWidth=1.f/columns;
        const float u=float(cell%columns)*columnWidth,v=float(cell/columns)*.25f;
        glBlendFunc(GL_SRC_ALPHA,additive?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);
        art(texture,multiply(pose,multiply(translation(-diameter*.5f,diameter*.5f,0),
            scaling(diameter,-diameter,1))),{u+.001f,v+.001f,columnWidth-.002f,.248f},
            additive?color*(1.f/std::max(.35f,environmentView.modelGain)):color);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        spriteAlpha=oldAlpha;
    };
    const int zap=std::min(5,int(progress*6));
    const int charging=6+int(fx.age*20)%9;
    const int frame=std::min(3,int(progress*4));
    switch (fx.model) {
    case rpgEnemyCharge:
    case rpgEnemyBossCharge: {
        // A ring contracts into the weapon. Four inward ticks make the
        // silhouette read against both sky and ground and from either eye.
        const Vec3 center = origin + aim * (size * .3f);
        const float radius = .25f + .9f * fade;
        shapeOpacity = .42f + .55f * progress;
        const Vec3 chargeColor = fx.model == rpgEnemyBossCharge ? Vec3{1.f, .34f, .54f} : cyan;
        sprite(charging,center,size*2.2f,chargeColor,.8f);
        arc(center, radius, progress * 2.f, 8, 8, chargeColor);
        for (int i = 0; i < 4; ++i) {
            const float angle = float(i) * pi * .5f + progress * 2.f;
            stroke(center + segment(angle, radius * .64f), center + segment(angle, radius * 1.2f), .04f, chargeColor);
        }
        break;
    }
    case rpgEnemyBoost: {
        const float span=size*(1.8f+progress*3.4f)*fade;
        for(float jet:{-1.f,1.f}){
            const auto nozzle=origin+side*(jet*size*.56f);
            const auto plume=nozzle-aim*span-Vec3{0,.31f*size,0};
            stroke(nozzle,plume,.19f,{.19f,.44f,1.f},1);
            stroke(nozzle,nozzle+(plume-nozzle)*.73f,.085f,{.58f,.84f,1.f},.85f);
            sprite(36+frame,nozzle,size*(.55f+fade*.4f),cyan,fade*.70f);
        }
        break;
    }
    case rpgEnemyKinetic:
        if(progress<.65f)sprite(32+frame,origin,size*(1.f+.43f*progress),{1,.81f,.56f},fade*.68f);
        stroke(origin, origin + aim * (size * 2.4f * fade), .11f, {1.f, .88f, .48f});
        for (int i = 0; i < 4; ++i) {
            const Vec3 tip = origin + aim * (size * fade) + segment(float(i) * pi * .5f, fade * .8f);
            stroke(origin, tip, .035f, orange);
        }
        break;
    case rpgEnemyEnergy:
        sprite(36+frame,origin,size*(1.65f+progress),cyan,fade*.83f);
        stroke(origin,origin+aim*size*2.55f*fade,.22f,{.13f,.41f,1.f});
        stroke(origin, origin + aim * (size * 2.8f * fade), .08f, {.78f, .98f, 1.f});
        arc(origin + aim * (size * progress), .25f + progress, 0, 6, 6, cyan);
        break;
    case rpgEnemyDisruption:
        sprite(zap,origin+aim*size*progress,size*.6f,{.65f,.43f,1},fade);
        arc(origin + aim * (size * progress), .3f + progress * 1.4f, progress * pi, 6, 6, {.72f, .43f, 1.f});
        arc(origin + aim * (size * progress * 1.6f), .15f + progress, -progress * pi, 4, 4, cyan);
        break;
    case rpgDuelGuardTell: {
        // Two ends of the required blade line converge onto the guard point.
        const float remaining=enemyEffectLife(fx.model)-fx.age;
        const float gather=1-std::clamp(remaining/.8f,0.f,1.f);
        const Vec3 gold{1.f,.67f,.13f};
        for(float sign:{-1.f,1.f}){
            const Vec3 end=origin+aim*(sign*size*(1.05f-.25f*gather));
            const Vec3 inner=origin+aim*(sign*size*(.45f-.36f*gather));
            shapeOpacity=.94f;
            stroke(inner,end,.066f,gold);
            stroke(inner,end,.023f,{1.f,1.f,.87f});
        }
        break;
    }
    case rpgDuelCut: {
        const Vec3 travel=origin+aim*size*(-1.1f+2.2f*progress);
        shapeOpacity=.94f*fade;
        stroke(travel-aim*size*.75f,travel,.12f,{.36f,.77f,1.f});
        stroke(travel-aim*size*.5f,travel,.045f,{.96f,1.f,1.f});
        break;
    }
    case rpgEnemyMeleeTell: {
        const float remaining=enemyEffectLife(fx.model)-fx.age;
        const float drawBack=1.f-std::clamp(remaining/.48f,0.f,1.f);
        const auto hilt=origin-aim*(size*.34f)+up*(size*.16f);
        const auto edge=unit(side*(1.f-drawBack*.38f)+up*(.60f+drawBack*.35f));
        shapeOpacity=.25f+drawBack*.52f;
        for(float sign:{-1.f,1.f})
            stroke(hilt+edge*sign*size*.43f,hilt+edge*sign*size*(.66f+.29f*drawBack),
                   .048f,orange,.86f);
        break;
    }
    case rpgNpcMeleeSlash: {
        const Vec3 center=origin-aim*(size*.18f);
        const Vec3 sweep=unit(side*.85f+up*.52f);
        const float leading=-1.18f+2.50f*(1.f-fade*fade);
        for(int band=0;band<2;++band){
            const float width=band?.034f:.084f;
            const float radius=band?1.70f:1.86f;
            for(int i=0;i<6;++i){
                const float angle=leading-float(i)*.23f;
                auto edge=[&](float a){return center+(aim*std::cos(a)+sweep*std::sin(a))*(size*radius);};
                shapeOpacity=fade*(1.f-float(i)*.105f);
                stroke(edge(angle-.24f),edge(angle),width,band?Vec3{1,.84f,.52f}:orange,-.62f);
            }
        }
        break;
    }
    case rpgEnemyMelee: {
        const Vec3 center = origin - aim * (size * .35f);
        const Vec3 slashAxis = unit(up + side * .38f);
        auto tip = [&](float angle, float radius) {
            return center + (aim * std::cos(angle) + slashAxis * std::sin(angle)) * (radius * size);
        };
        const float leading = -.8f + progress * 1.6f;
        for (int i = 0; i < 5; ++i) {
            const float angle = leading - float(i) * .22f;
            stroke(tip(angle,1.63f),tip(angle-.22f,1.63f),.11f,{.88f,.29f,.075f},i==4?.85f:0);
            if(i<3)stroke(tip(angle,1.57f),tip(angle-.22f,1.57f),.036f,{1,.82f,.44f});
        }
        break;
    }
    case rpgEnemyShieldHit:
        sprite(29,origin+aim*.12f,size*(1+progress),cyan,fade*.75f);
        arc(origin + aim * (.08f * size), .3f + progress * 1.15f, pi / 6.f, 6, 6, cyan);
        [[fallthrough]];
    case rpgEnemyHit:
        if(fx.model==rpgEnemyHit){
            const bool energy=fx.weapon==Weapon::Rail;
            sprite((energy?36:32)+frame,origin,size*(.55f+progress),{1,1,1},fade*.72f);
            if(!energy&&progress>.29f)
                sprite(44+frame,origin+Vec3{0,size*.4f*progress,0},size*(.6f+progress),{.68f,.69f,.73f},fade*.32f,false,false);
        }
        for (int i = 0; i < 5; ++i) {
            const Vec3 axis = unit(aim * .72f + segment(float(i) * 2.4f, .8f) * (1.f / size));
            const Vec3 start = origin + axis * (size * progress * 1.25f);
            stroke(start, start + axis * (size * fade * .65f), .025f, fx.model == rpgEnemyShieldHit ? cyan : orange);
        }
        break;
    case rpgMechaWeaveTell: {
        // Paired emitter flashes precede a volley without filling the dodge lanes.
        const float span=1.3f+2.4f*progress;
        for(float sign:{-1.f,1.f}){
            const auto lamp=origin+side*(span*sign);
            const Vec3 tint=sign<0.f?orange:cyan;
            shapeOpacity=.72f+.22f*std::sin(progress*pi*3);
            drawEnemyStreak(lamp-side*.5f,lamp+side*.5f,.12f,tint,camera);
            sprite(charging,lamp,1.1f+.9f*progress,tint,.66f);
        }
        break;
    }
    case rpgMechaRackTell: {
        const Vec3 lift{0,1,0};
        for(int tube=-1;tube<=1;++tube){
            const auto vent=origin+side*(tube*1.1f);
            const float burn=.45f+.55f*std::abs(std::sin(progress*pi*2.f+tube*.55f));
            shapeOpacity=.85f;
            drawEnemyStreak(vent,vent+lift*(1.f+2.3f*burn),.20f,{1,.47f,.14f},camera,.9f);
            sprite(32,vent,1.25f,{1,.78f,.38f},burn*.75f);
        }
        break;
    }
    case rpgHeavyCrossfireWarning:
    case rpgAtomicBreathWarning: {
        const bool breath=fx.model==rpgAtomicBreathWarning;
        const bool missile=fx.weapon==Weapon::Rocket;
        const Vec3 color=breath?Vec3{.22f,1.f,.78f}:missile?orange:Vec3{1.f,.65f,.20f};
        const auto lateral=unit(cross(aim,std::abs(aim.y)<.85f?Vec3{0,1,0}:Vec3{1,0,0}));
        const std::array<float,3> offsets=breath?std::array{-.11f,0.f,.11f}:
            std::array{heavyFanOffsets[1][0],0.f,heavyFanOffsets[2][4]};
        shapeOpacity=.57f+.34f*std::abs(std::sin(fx.age*(breath?10.f:15.f)));
        for(float offset:offsets){
            const auto lane=patternLane(aim,lateral,offset);
            for(int dash=0;dash<6;++dash){
                const float start=(float(dash)+.25f)/6.f;
                drawEnemyStreak(origin+lane*(size*start),
                    origin+lane*(size*(start+.10f)),breath?.15f:.12f,color,camera);
            }
        }
        sprite(breath?31:32,origin,(breath?2.7f:1.8f)+1.5f*progress,color,
               .52f+.43f*progress);
        break;
    }
    case rpgAtomicBreath: {
        const Vec3 end=origin+aim*size;
        const Vec3 mint{.21f,1.f,.72f};
        shapeOpacity=.37f;
        drawEnemyStreak(origin,end,1.04f,mint,camera,-.29f);
        shapeOpacity=.86f;
        drawEnemyStreak(origin,end,.46f,{.31f,1.f,.86f},camera,-.32f);
        drawEnemyStreak(origin,end,.17f,{.83f,1.f,.91f},camera,-.45f);
        sprite(31,origin,3.2f,mint,.89f);
        break;
    }
    case rpgVehicleVolleyWarning: {
        const bool energy=fx.weapon==Weapon::Rail;
        const Vec3 color=energy?cyan:orange;
        const Vec3 lateral=unit(cross(aim,Vec3{0,1,0}),{1,0,0});
        const float width=energy?.21f*size:fx.weapon==Weapon::Rocket?7.5f:5.f;
        const auto end=origin+aim*size;
        shapeOpacity=.72f+.26f*progress;
        for(float edge:{-1.f,1.f})
            for(int dash=0;dash<4;++dash){
                const auto ray=end+lateral*(width*edge)-origin;
                const float start=.12f+float(dash)*.21f;
                drawEnemyStreak(origin+ray*start,origin+ray*(start+.14f),.22f,color,camera,-.45f);
            }
        sprite(energy?31:32,origin,1.8f,color*.8f,.35f+.55f*progress);
        break;
    }
    case rpgWeatherWarning:
    case rpgWeatherMeteor: {
        const auto *hazard=weatherHazard(world.environment.weather);
        const Vec3 color=hazard?hazard->particles:orange;
        const bool ground=fx.weapon==Weapon::Grenade;
        shapeOpacity=.66f+.28f*std::sin(fx.age*17.f);
        if(ground){
            auto rim=[&](float angle){
                auto point=origin+Vec3{std::sin(angle)*size,0,std::cos(angle)*size};
                point.y=(world.environment.weather==Weather::Snowstorm?world.pilotSupportHeight(point):world.walkableHeight(point))+.32f;
                return point;
            };
            for(int n=0;n<20;++n){
                const float angle=float(n)*2*pi/20;
                drawEnemyStreak(rim(angle),rim(angle+2*pi/20*.83f),.31f,color,camera);
            }
            for(auto offset:{Vec3{1.9f,0,0},Vec3{0,0,1.9f}})
                drawEnemyStreak(origin+Vec3{0,.35f,0}-offset,origin+Vec3{0,.35f,0}+offset,.25f,color,camera);
        }else{
            arc(origin,.76f+progress*.22f,progress,12,12,color);
            sprite(27,origin+Vec3{0,3,0},2.8f,color,.4f+.5f*progress);
        }
        if(fx.model==rpgWeatherMeteor){
            const auto incoming=unit(Vec3{.32f,-1,-.22f});
            const auto head=origin-incoming*(74.f*(1-progress*progress));
            drawEnemyStreak(head-incoming*12.f,head,1.3f,color,camera,1.f);
            drawEnemyStreak(head-incoming*8.f,head,.47f,{1.f,.9f,.7f},camera,1.f);
            sprite(38,head,4.1f,color,.82f);
        }
        break;
    }
    case rpgWeatherPulse: {
        const auto *hazard=weatherHazard(world.environment.weather);
        const Vec3 color=hazard?hazard->particles:cyan;
        arc(origin,1.2f+progress*1.9f,progress*1.3f,18,18,color);
        if(world.environment.weather==Weather::IonStorm){
            Vec3 last=origin+Vec3{0,1,0};
            for(int n=1;n<=5;++n){
                const auto next=origin+Vec3{(n%2?1.f:-1.f)*float(n)*.46f,4.f*n,float(n%3)-1.f};
                drawEnemyStreak(last,next,.33f*fade,color,camera);
                last=next;
            }
        }
        break;
    }
    case rpgWeatherHail: {
        for(unsigned n=0;n<23;++n){
            const float angle=weatherNoise(769,n*3)*2*pi;
            const float radius=size*std::sqrt(weatherNoise(769,n*3+1));
            auto point=origin+Vec3{std::cos(angle)*radius,0,std::sin(angle)*radius};
            point.y=world.pilotSupportHeight(point)+.22f;
            const float phase=progress*1.4f-weatherNoise(769,n*3+2)*.4f;
            if(phase<0||phase>1)continue;
            const Vec3 ice{.78f,.91f,1.f};
            if(phase<.58f){
                const Vec3 offset=Vec3{-4,18,2}*(1-phase/.58f);
                drawEnemyStreak(point+offset,point+offset+Vec3{-.34f,1.4f,.16f},.30f,ice,camera);
            }else{
                const float bounce=(phase-.58f)/.42f;
                sprite(31,point+Vec3{bounce,5*bounce*(1-bounce),0},.7f,ice,1-bounce);
            }
        }
        break;
    }
    case rpgWeatherSpore:
    case rpgWeatherFire: {
        const bool fire=fx.model==rpgWeatherFire;
        for(int n=0;n<5;++n){
            const float angle=float(n)*2.4f+progress*.8f;
            const float radius=size*(.22f+.47f*progress);
            const auto p=origin+Vec3{std::sin(angle)*radius,1.8f+progress*(fire?9.f:4.f),std::cos(angle)*radius};
            const int frame=(int(fx.age*9)+n)%8;
            if(fire)sprite(32+frame,p,size*(.42f+.45f*fade),orange,.71f*fade);
            else sprite(28+int(n%3),p,size*(1.04f+progress*.48f),{.66f,.31f,.43f},.5f*fade,false,false);
        }
        break;
    }
    case rpgVehicleBombWarning: {
        const Vec3 forward=unit(Vec3{aim.x,0,aim.z});
        const Vec3 lateral=cross(forward,Vec3{0,1,0});
        shapeOpacity=.77f+.22f*std::sin(fx.age*11.f);
        const Vec3 color{1,.46f,.16f};
        auto surface=[&](Vec3 p){
            p.y=world.surfaceHeight(p.x,p.z)+world.waterDepthAt(p)+.31f;
            return p;
        };
        std::array<Vec3,16> rim;
        for(size_t i=0;i<rim.size();++i){
            const float at=float(i)*(2*pi/float(rim.size()));
            rim[i]=surface(origin+(lateral*std::sin(at)+forward*std::cos(at))*size);
        }
        for(size_t i=0;i<rim.size();++i)
            drawEnemyStreak(rim[i],rim[(i+1)%rim.size()],.29f,color,camera,-.35f);
        for(auto axis:{forward,lateral})
            drawEnemyStreak(surface(origin-axis*1.6f),surface(origin+axis*1.6f),.25f,color,camera,-.35f);
        break;
    }
    case rpgScatterMuzzle: {
        sprite(32+frame,origin,size*(2.4f+progress),{1,.79f,.46f},fade);
        for(int i=0;i<6;++i){
            auto ray=unit(aim+segment(i*2*pi/6,.52f));
            auto start=origin+ray*(size*progress);
            stroke(start,start+ray*size*(2.1f*fade),.055f,{1,.69f,.27f},.9f);
        }
        sprite(44+frame,origin+aim*(size*progress),size*(.7f+1.8f*progress),{.7f,.65f,.6f},progress*fade,false,false);
        break;
    }
    case rpgPulseMuzzle:
        sprite(36+frame,origin,size*(1.4f+progress),{.6f,1,.85f},fade);
        stroke(origin,origin+aim*size*(3.2f+progress),.13f,{.13f,1,.78f},1);
        arc(origin+aim*size*(.4f+progress),.26f+progress,.4f,8,8,{.3f,1,.9f});
        break;
    case rpgCutterSparks:
        for(int i=0;i<7;++i){
            auto ray=unit(aim*.65f+segment(i*2.4f,.8f));
            auto start=origin+ray*(size*progress);
            stroke(start,start+ray*size*fade,.035f,{1,.77f,.29f},.9f);
        }
        sprite(44+frame,origin+up*(size*progress*.46f),size*(.5f+progress),{.67f,.62f,.56f},progress*fade,false,false);
        break;
    case rpgWeaponMuzzle:
    case rpgWeaponHeavy: {
        const bool heavy=fx.model==rpgWeaponHeavy, energy=fx.weapon==Weapon::Rail;
        const Vec3 color=energy?cyan:orange;
        const float flash=std::max(0.f,1.f-progress*1.57f);
        if(flash>0){
            sprite((energy?36:32)+frame,origin,size*(heavy?2.25f:1.85f),{1,1,1},flash*.97f);
            stroke(origin,origin+aim*size*(heavy?3.1f:2.05f)*flash,.19f,color,1);
            stroke(origin,origin+aim*size*1.3f*flash,.07f,{1,.96f,.85f},.90f);
        }
        if(energy){
            if(progress<.76f)arc(origin+aim*size*(.2f+progress*.65f),.35f+progress,.4f,8,8,cyan);
        }else{
            sprite(44+frame,origin+aim*(size*progress)+Vec3{0,size*progress*.72f,0},
                   size*(.56f+progress*1.54f),{.66f,.64f,.60f},sin(progress*pi)*.62f,false,false);
            if(heavy)for(int n=0;n<5;++n){
                const auto offset=segment(n*2.39996f,.56f+progress*1.7f)+aim*size*(.37f+progress);
                stroke(origin+offset,origin+offset+aim*(.4f*fade),.025f,{1,.71f,.28f});
            }
        }
        break;
    }
    case rpgRocketIgnition: {
        if(progress<.55f)sprite(32+frame,origin,size*1.55f,{1,.78f,.54f},fade);
        stroke(origin,origin-aim*size*(1.8f+3.f*progress)*fade,.28f,{1,.42f,.14f},1);
        stroke(origin,origin-aim*size*1.7f*fade,.11f,{1,.85f,.52f},.93f);
        sprite(44+frame,origin-aim*(size*progress*1.8f)+Vec3{0,progress*.8f,0},
               size*(.7f+progress*2.3f),{.80f,.76f,.67f},fade*.64f,false,false);
        break;
    }
    case rpgWeaponArc:
        sprite(31,origin,size*(1.2f+progress),cyan,fade);
        sprite(zap,origin+aim*.12f,size*.4f,{.67f,.9f,1},.6f*fade);
        arc(origin,.22f+progress,.4f,7,5,cyan);
        break;
    case rpgWeaponPulse: {
        auto floor=origin;
        floor.y+=.18f;
        sprite(27,floor,size*(.22f+progress)*2,orange,fade,true);
        sprite(23-std::min(8,int(progress*9)),floor,size*(.3f+progress),orange,fade*.7f,true);
        break;
    }
    case rpgWeaponBarrier:
        sprite(29,origin,size*(.65f+progress*.5f),cyan,fade);
        sprite(charging,origin,size*1.1f,cyan,fade*.6f);
        break;
    case rpgArrivalDropship: {
        const float transit=std::abs(progress-.47f)/.53f;
        const auto along=unit(Vec3{aim.x,0,aim.z});
        const auto ship=origin+along*((progress-.47f)*86.f)+Vec3{0,11.f+30.f*transit,0};
        if(models.contains("carrier"))
            modelPreview("carrier",translation(ship.x,ship.y,ship.z),23.f,std::atan2(-along.x,-along.z));
        for(float engine:{-1.f,1.f}){
            const auto exhaust=ship+side*(engine*5.f)-along*7.f;
            stroke(exhaust,exhaust-along*7.f-Vec3{0,3,0},.12f,cyan);
        }
        sprite(27,origin+Vec3{0,.12f,0},18.f,Vec3{.88f,.67f,.42f},.42f*(1-transit),true);
        break;
    }
    case rpgArrivalWarp: {
        const float aperture=std::sin(progress*pi);
        const auto center=origin+Vec3{0,5,0};
        const auto across=unit(cross(aim,{0,1,0}));
        const float radius=(.32f+aperture)*size*1.6f;
        auto rim=[&](float a){return center+across*(std::cos(a)*radius)+Vec3{0,std::sin(a)*radius,0};};
        for(int i=0;i<10;++i)stroke(rim(i*2*pi/10),rim((i+1)*2*pi/10),.035f,cyan);
        sprite(charging,center,size*(.6f+aperture*2.5f),{.45f,.64f,1},aperture*.94f);
        break;
    }
    case rpgArrivalKaijuRift: {
        const float aperture=std::pow(std::max(0.f,std::sin(pi*progress)),.75f);
        const Vec3 center=origin+Vec3{0,size*1.65f,0};
        const Vec3 lateral=unit(cross(aim,{0,1,0}),{1,0,0});
        const Vec3 violet{.67f,.16f,.94f},mint{.38f,1.f,.69f};
        // Uneven lips peel apart vertically and then collapse into a single seam.
        std::array<Vec3,9> left{},right{};
        for(int i=0;i<9;++i){
            const float t=float(i)/8.f;
            const float opening=std::sin(t*pi)*aperture;
            const auto seam=center+Vec3{0,size*(t-.5f)*3.4f,0}+
                lateral*(size*.20f*std::sin(13.f*t+progress*9.f));
            left[i]=seam-lateral*(size*opening*(i%2?.42f:.78f));
            right[i]=seam+lateral*(size*opening*(i%2?.86f:.46f));
        }
        for(int i=0;i<8;++i){
            stroke(left[i],left[i+1],.075f,violet);
            stroke(right[i],right[i+1],.075f,violet);
            stroke(left[i],left[i+1],.027f,mint);
            stroke(right[i],right[i+1],.027f,mint);
            const auto ember=(left[i]+right[i+1])*.5f+
                lateral*(size*aperture*(i%2?-1.f:1.f)*(.75f+progress));
            stroke(ember,ember+Vec3{0,size*(.11f+fade*.19f),0},.033f,i%2?violet:mint,.7f);
        }
        sprite(28,center,size*(1.4f+aperture*2.5f),{.21f,.08f,.28f},aperture*.76f,false,false);
        sprite(zap,center,size*(.7f+aperture),mint,aperture*.50f);
        break;
    }
    case rpgArrivalAirstrike: {
        const auto tip=origin+aim*(size*progress*4);
        const auto plume=tip-aim*size*(1.5f+progress*4);
        shapeOpacity=.30f*fade;
        stroke(plume,tip,.09f,{.70f,.78f,.86f},-.86f);
        break;
    }
    case rpgArrivalBurrow: {
        const Vec3 floor=origin+Vec3{0,.18f,0};
        sprite(27,floor,size*(1.6f+progress*2.7f),{.88f,.61f,.33f},fade*.76f,true);
        for(int i=0;i<7;++i){
            const float a=float(i)*2*pi/7;
            const auto dirt=Vec3{std::cos(a),0,std::sin(a)}*(size*(.35f+progress));
            const auto p=floor+dirt+Vec3{0,std::sin(progress*pi)*(2.f+i*.21f),0};
            stroke(p,p+dirt*.20f,.043f,{.64f,.46f,.26f});
        }
        break;
    }
    case rpgArrivalEdge:
        sprite(25, origin-aim*(fade*10.f)+Vec3{0,1.4f,0},size*(1.6f+progress),{.61f,.69f,.68f},fade*.65f);
        break;
    case rpgWeaponBlast: {
        const float flame=std::max(0.f,1.f-progress*1.8f);
        for(int i=0;i<2;++i){
            const float sign=i?1.f:-1.f;
            const auto puff=origin+side*(sign*size*(.10f+progress*.25f))+Vec3{0,size*(.16f+progress*.60f),0};
            if(flame>0)sprite(40+frame,puff,size*(.9f+progress*1.8f),{1,.86f,.70f},flame*.82f);
            if(progress>.13f)sprite(44+frame,puff+Vec3{0,size*progress*.23f,0},size*(1.f+progress*1.26f),
                                     {.71f,.64f,.57f},sin(progress*pi)*.86f,false,false);
        }
        if(progress<.40f)sprite(27,origin+Vec3{0,.13f,0},size*(1.3f+5.4f*progress),orange,flame*.48f,true);
        for(int i=0;i<9;++i){
            const auto direction=segment(float(i)*2.39996f,1.f)*.80f+Vec3{0,size*(.25f+float(i%3)*.24f),0};
            const auto spark=origin+direction*(.22f+1.75f*progress)-Vec3{0,size*progress*progress*.82f,0};
            stroke(spark,spark+direction*(.18f*fade),.021f,{1,.66f,.21f});
        }
        break;
    }
    }
    shapeOpacity = savedOpacity;
}

void Renderer::drawWorldLayer(const World& w,const Matrix& camera,bool paused,const Frontend* menu){
    glUseProgram(program);glBindVertexArray(vao);
    auto block=[&](Vec3 p,Vec3 half,Vec3 color){box(multiply(translation(p.x,p.y,p.z),scaling(half.x,half.y,half.z)),color);};
    if(w.mission>=0){
        if(!w.proceduralTerrain)block({0,-.4f,0},{1500,.4f,1500},w.groundColor);
        drawWorld(w);
    }else{
        block({0,-.4f,-40},{180,.4f,180},w.groundColor);
        for(const auto& t:w.targets)if(t.health>0)block(t.center,t.half,{.3f,.4f,.45f});
    }
    if(w.rpgPrototype){
        const auto eye=w.eye();
        for(const auto &pickup:w.pickups){
            const auto p=pickup.position;
            if(unclaimedSalvage(pickup,w.playerSlot)&&length(p-eye)<=300)
                model("arsenal_safe_box",translation(p.x,p.y,p.z));
        }
        emissiveWorldMarkers=true;
        auto salvageBeacon=[&](Vec3 position,bool depot){
            const float range=length(position-eye);
            if(range>(depot?480.f:300.f))return;
            auto tag=camera;tag[12]=position.x;tag[13]=position.y;tag[14]=position.z;
            const float span=std::clamp(range*.016f,.84f,2.5f);
            const float pulse=.86f+.14f*std::sin(w.elapsed*5.f);
            const Vec3 color=(depot?Vec3{.31f,.95f,1.f}:Vec3{.38f,1.f,.65f})*pulse;
            const Vec3 vertices[]={{0,span,0},{span,0,0},{0,-span,0},{-span,0,0}};
            auto front=multiply(tag,translation(0,0,.027f));
            for(int i=0;i<4;++i){
                line(tag,vertices[i],vertices[(i+1)%4],span*.14f,{.015f,.038f,.025f});
                line(front,vertices[i],vertices[(i+1)%4],span*.065f,color);
            }
            line(front,{0,-span,0},{0,-3.f,0},.045f,color);
            const float font=std::clamp(range*.0026f,.16f,.46f);
            const std::string label=depot?"SALVAGE DEPOT":"SALVAGE";
            text(label,multiply(front,translation(-2.f*float(label.size())*font,span+8*font,0)),font,color);
        };
        for(auto p:w.rpgSalvageDepots)salvageBeacon(p,true);
        for(const auto &pickup:w.pickups)
            if(unclaimedSalvage(pickup,w.playerSlot))salvageBeacon(pickup.position+Vec3{0,3.9f,0},false);
        for(const auto &target:w.targets){
            if(!target.active||target.escaped||target.health<=0||target.rpgWeakReveal<=0||
               length(target.center-eye)>PassiveRadar::range)continue;
            for(unsigned n=0;n<unsigned(rpg::WeakPoint::Count);++n){
                const auto part=rpg::WeakPoint(n);
                if((part==rpg::WeakPoint::LeftArm&&(target.rpgEnemyMissingArms&1u))||
                   (part==rpg::WeakPoint::RightArm&&(target.rpgEnemyMissingArms&2u)))continue;
                auto point=rpg::weakPointPosition(target,part);
                const auto outside=unit(point-target.center);
                if(dot(outside,unit(eye-point))<.10f)continue;
                const float range=length(eye-point);
                const float span=std::clamp(range*.012f,.65f,2.35f);
                // Keep the outline in the eye's plane. Screen-local strokes
                // stay the same shape when seen from the side or above.
                point=point+outside*.24f+unit(eye-point)*.24f;
                auto tag=camera;tag[12]=point.x;tag[13]=point.y;tag[14]=point.z;
                const float fade=.45f+.55f*std::sqrt(std::min(1.f,target.rpgWeakReveal));
                const Vec3 color=Vec3{1,.72f,.20f}*fade;
                const Vec3 corners[]={{0,span,0},{span,0,0},{0,-span,0},{-span,0,0}};
                // Offset the visible core toward the eye so it wins depth
                // against the larger outline behind it.
                auto front=multiply(tag,translation(0,0,.015f));
                for(int k=0;k<4;++k){
                    line(tag,corners[k],corners[(k+1)%4],span*.19f,{.04f,.015f,.006f});
                    line(front,corners[k],corners[(k+1)%4],span*.095f,color);
                }
                box(multiply(front,scaling(span*.11f,span*.11f,.012f)),Vec3{1,.93f,.63f}*fade);
            }
        }
        emissiveWorldMarkers=false;
    }
    for(const auto &drone:w.rpgDrones)if(!w.rpgDroneView||length(drone.position-w.eye())>.5f)model("weapon_scouting_drone",multiply(translation(drone.position.x,drone.position.y,drone.position.z),rotation(drone.yaw)));
    if(w.rpgTransitActive){
        Vec3 p=w.rpgTransitPosition;
        for(int i=0;i<16;++i){float a=i*2*pi/16,b=(i+1)*2*pi/16;
            line(translation(p.x,p.y+.2f,p.z),{25*std::cos(a),25*std::sin(a),0},{25*std::cos(b),25*std::sin(b),0},.12f,{.15f,.8f,.7f});}
        auto sign=camera;sign[12]=p.x;sign[13]=p.y+20;sign[14]=p.z;
        text((w.objective.starts_with("EXTRACT")?std::string("EXTRACTION "):"PARTY TRANSIT ")+std::to_string(w.rpgTransitReady)+"/"+std::to_string(w.rpgTransitRequired),multiply(sign,translation(-12,0,0)),.3f,{.2f,1,.8f});
    }
    // Original bright ballistic streaks and low-poly ordnance, differentiated by weapon.
    for(const auto& p:w.projectiles){
        Vec3 d=p.velocity*(1/std::max(.001f,length(p.velocity)));float yaw=std::atan2(-d.x,-d.z),pitch=std::asin(std::clamp(d.y,-1.f,1.f));
        auto pose=multiply(translation(p.position.x,p.position.y,p.position.z),rotation(yaw,pitch));
        bool missile=p.kind==ProjectileKind::Rocket,bomb=p.kind==ProjectileKind::Bomb,rail=p.weapon==Weapon::Rail;
        if(w.rpgPrototype&&(p.visual==ProjectileVisual::WaveA||p.visual==ProjectileVisual::WaveB)){
            drawMechaWave(p,camera);
        }else if(bomb||missile){
            const bool lofted=p.visual==ProjectileVisual::LoftMissile;
            if(lofted)pose=multiply(pose,scaling(3.4f,3.4f,1.8f));
            model(p.weapon==Weapon::Homing?"effect-homing":p.weapon==Weapon::AutoGrenade?"effect-auto-grenade":bomb?"effect-grenade":"effect-rocket",pose);
            if(w.rpgPrototype&&missile){
                const float opacity=shapeOpacity;
                const auto tail=p.position-d*(lofted?.92f:.45f);
                const auto exhaust=tail-d*6.f;
                shapeOpacity=.24f;
                drawEnemyStreak(exhaust-d*3.4f,tail,.50f,{.55f,.49f,.43f},camera,-1);
                shapeOpacity=.68f;
                drawEnemyStreak(exhaust,tail,.32f,{1,.33f,.075f},camera,-1);
                shapeOpacity=.96f;
                drawEnemyStreak(tail-d*2.3f,tail,.17f,{1,.86f,.54f},camera,-.84f);
                shapeOpacity=opacity;
            }
        }
        else if (w.rpgPrototype) {
            const bool cannon=p.weapon==Weapon::HeavyMachinegun;
            const bool pellet=p.weapon==Weapon::Shotgun,rotary=p.weapon==Weapon::Gatling;
            const float span=rail?6.4f:cannon?5.f:pellet?1.6f:rotary?2.5f:3.2f;
            // Calibrate against the original 1.4 m ballistic sprite. Pellets
            // and rotary rounds keep a smaller silhouette in dense volleys.
            const float halfWidth=rail?.68f:cannon?.59f:pellet?.26f:rotary?.36f:.46f;
            const bool pulse=rail&&p.mount>=0&&p.mount<2&&p.shooter>=0&&length(p.velocity)<171.f;
            const Vec3 glow=pulse?Vec3{.13f,1,.73f}:rail?Vec3{.19f,.61f,1.f}:Vec3{1,.51f,.15f};
            const Vec3 core=pulse?Vec3{.68f,1,.84f}:rail?Vec3{.79f,.94f,1.f}:Vec3{1.f,.93f,.71f};
            const float previous=shapeOpacity;
            shapeOpacity=.33f;
            drawEnemyStreak(p.position-d*span,p.position,halfWidth,glow,camera,-1);
            shapeOpacity=.84f;
            drawEnemyStreak(p.position-d*(span*.72f),p.position,halfWidth*.42f,core,camera,-.90f);
            shapeOpacity=previous;

            // The glowing projectile head remains visible when the trail
            // points directly at an eye. It shares the trail's vertex batch.
            const Vec3 right=Vec3{camera[0],camera[1],camera[2]}*(halfWidth*.78f);
            const Vec3 up=Vec3{camera[4],camera[5],camera[6]}*halfWidth;
            const Vec3 rim[]={p.position-up,p.position+right,p.position+up,p.position-right};
            for(int edge=0;edge<4;++edge){
                enemyEffectVertices.push_back({p.position,core,.98f});
                enemyEffectVertices.push_back({rim[edge],glow,.17f});
                enemyEffectVertices.push_back({rim[(edge+1)%4],glow,.17f});
            }
        }else {
            Matrix facing=camera;facing[12]=p.position.x;facing[13]=p.position.y;facing[14]=p.position.z;
            float size=1.4f; // Shared $1DFF0/$658D8 ballistic descriptor: 140 cm.
            glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
            art(rail?"shot-rail":p.weapon==Weapon::Gatling?"shot-gatling":"shot-rifle",multiply(facing,multiply(translation(-size/2,size/2,0),scaling(size,-size,1))));
            glDepthMask(GL_TRUE);glDisable(GL_BLEND);
        }
    }
    if (!enemyEffectVertices.empty()) {
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
        flushEnemyEffects();
        glDepthMask(GL_TRUE);glDisable(GL_BLEND);
    }
    glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1,-1);
    for(const auto& fx:w.effects)if(fx.kind==WeaponEffect::Debris&&fx.age>=0){
        // Newly spawned fragments can render before their first physics update.
        float y=std::max(fx.position.y,w.debrisFloor(fx));
        float spin=destructionSpin(fx.age,fx.model);auto pose=multiply(translation(fx.position.x,y,fx.position.z),multiply(rotation(spin*700*2*pi/2048,spin*1600*2*pi/2048),scaling(fx.size,fx.size,fx.size)));
        if(rpgDebris(fx.model)){auto half=rpgFragmentHalf(fx.model);box(multiply(pose,scaling(half.x,half.y,half.z)),rpgFragmentColor(fx.model));}
        else model(fx.model?"fragment-"+std::to_string(fx.model):"effect-debris",pose);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
    std::vector<const WeaponEffect*> sorted;for(const auto& fx:w.effects)if(fx.model!=rpg::rpgRadarArrival)sorted.push_back(&fx);
    Vec3 eye{camera[12],camera[13],camera[14]};
    std::stable_sort(sorted.begin(),sorted.end(),[&](auto a,auto b){return dot(a->position-eye,a->position-eye)>dot(b->position-eye,b->position-eye);});
    for(const auto* entry:sorted){const auto& fx=*entry;
        if(fx.age<0||fx.kind==WeaponEffect::Debris||fx.model==rpgScorch)continue;
        if(w.rpgPrototype && rpg::enemyEffectLife(fx.model)>0.f &&
           (fx.model!=rpg::rpgWeaponBlast||textures.contains("mecha-weapon-fx"))){
            if(visibilityInRange(w,visibilityObserver,fx.position))drawEnemyEffect(fx,camera,w);
            continue;
        }
        if(fx.kind==WeaponEffect::Muzzle)continue;
        Matrix billboard=camera;billboard[12]=fx.position.x;billboard[13]=fx.position.y;billboard[14]=fx.position.z;
        const float particleScale=w.rpgPrototype?1.35f:1.f;
        if(fx.model==rpgLandingWave){
            const float t=std::clamp(fx.age/.55f,0.f,1.f);
            const float radius=1.7f+(fx.size-1.7f)*(1-(1-t)*(1-t));
            const bool wet=w.waterLevelAt(fx.position)>fx.position.y;
            auto color=wet?Vec3{.45f,.78f,.85f}:Vec3{.79f,.71f,.56f};
            const auto normal=unit(fx.direction,{0,1,0});
            const auto right=unit(cross(normal,Vec3{0,0,1}),{1,0,0});
            const auto forward=cross(right,normal);
            for(int i=0;i<32;++i){
                const float a=2*pi*i/32,b=2*pi*(i+1)/32;
                auto point=[&](float angle){return fx.position+(right*std::sin(angle)+forward*std::cos(angle))*radius;};
                line(identity,point(a),point(b),(.24f-.21f*t)*(i%4==0?.6f:1.f),color*(1-.42f*t));
            }
        }else if(fx.model==rpgSplash){
            for(int i=0;i<6;++i){float a=i*2*pi/6,r=fx.size*fx.age;Vec3 p=fx.position+Vec3{std::sin(a)*r,std::max(0.f,std::min(9.f,fx.size*1.5f)*fx.age-9*fx.age*fx.age),std::cos(a)*r};line(identity,p,p+Vec3{0,.35f*particleScale,0},.045f*particleScale,{.36f,.65f,.76f});}
        }else if(fx.model==rpgFire){
            float size=fx.size*particleScale*(1-fx.age*.4f);billboard[13]+=fx.age*2;
            art("explosion-atlas",multiply(billboard,multiply(translation(-size*.5f,size*.5f,0),scaling(size,-size*1.5f,1))),{0,float(3+int(fx.age*8)%4)*.1f,1,.1f},{1,.75f,.45f});
        }else if(fx.kind==WeaponEffect::Blast||fx.kind==WeaponEffect::Smoke){
            int frame=std::min(9,int(fx.age*10));float size=fx.size*particleScale;
            if(fx.model==rpgDust){frame=std::min(9,int(fx.age*4));size*=.65f+fx.age*.8f;}
            art(fx.kind==WeaponEffect::Smoke?"smoke-atlas":"explosion-atlas",multiply(billboard,multiply(translation(-size*.5f,size*.5f,0),scaling(size,-size,1))),{0,float(frame)*.1f,1,.1f},fx.model==rpgDust?Vec3{.62f,.54f,.43f}:Vec3{1,1,1});
        }else{
            // Short impact fragments; never recycle the fireball for melee or gunfire.
            for(int i=0;i<4;++i){float a=i*pi*.5f+fx.age*3;Vec3 d{std::cos(a),std::sin(a),0};float r=fx.size*(.15f+fx.age*3);line(billboard,d*r,d*(r+.25f*fx.size*particleScale),.025f*particleScale,{1,.75f,.2f});}
        }
    }
    flushEnemyEffects();
    glDepthMask(GL_TRUE);glDisable(GL_BLEND);
    if(menu)for(const auto& pilot:menu->remotePilots){
        if(!visibilityInRange(w,visibilityObserver,pilot.position+Vec3{0,6,0},searchlightActive(pilot)))continue;
        teamTint=pilot.team==Team::Neutral?Vec3{1,1,1}:teamColour(pilot.team);
        auto savedProjection=vp;
        const auto& arrival=cockpitFx.insertion;
        if(w.rpgPrototype&&arrival.active&&arrival.kind==rpg::InsertionKind::Warp&&!pilot.lifeSpent){
            // Materializing remote hulls use the same quantized timeline as the
            // spatial envelope. The player's physical cockpit/IPD never scale.
            float scale=arrival.arrivalScale;
            auto origin=translation(pilot.position.x,pilot.position.y,pilot.position.z);
            vp=multiply(vp,multiply(origin,multiply(scaling(scale,scale,scale),translation(-pilot.position.x,-pilot.position.y,-pilot.position.z))));
            float phase=arrival.materialization;
            teamTint={.3f+.7f*phase,.8f+.2f*phase,1};
        }
        drawSoldier(pilot,pilot.lifeSpent?.4f:0,&camera,w.elapsed+w.rpgPoseLead);
        if(!pilot.lifeSpent){World remote;static_cast<Pilot&>(remote)=pilot;remote.elapsed=w.elapsed;remote.rpgPoseLead=w.rpgPoseLead;drawArms(remote);}
        vp=savedProjection;
        teamTint={1,1,1};
        if(pilot.missile.active)modelPreview("cruise",translation(pilot.missile.position.x,pilot.missile.position.y,pilot.missile.position.z),2,pilot.missile.yaw);
    }
    if(w.lifeSpent&&w.ejectionTime>2)drawSoldier(w,std::min(.4f,std::max(0.f,w.ejectionTime-3)*.15f));
    if(w.lifeSpent&&!(menu&&menu->spectating))drawCockpit(w,camera,paused);
    if(w.lifeSpent&&w.ejectionTime>=3&&w.ejectionTime<4){
        int frame=std::min(9,int((w.ejectionTime-3)*10));
        Matrix billboard=camera;billboard[12]=w.position.x;billboard[13]=w.position.y+7;billboard[14]=w.position.z;
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
        art("explosion-atlas",multiply(billboard,multiply(translation(-12,10,0),scaling(24,-20,1))),{0,float(frame)*.1f,1,.1f});
        glDepthMask(GL_TRUE);glDisable(GL_BLEND);
    }
}
void Renderer::drawScene(const World& w,const Matrix& projection,const Matrix& camera,const Matrix& head,bool paused,const Frontend* menu) {
    glEnable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
    environmentProfile=w.activeEnvironment();environmentView=environmentVisuals(w.activeEnvironment(),w.night,w.skyColor,w.groundColor,w.weatherTime());
    visibilityObserver=w;
    if(menu&&menu->spectating){std::vector<const Pilot*> living;for(const auto& p:menu->remotePilots)if(!p.lifeSpent&&p.health>0)living.push_back(&p);if(!living.empty())visibilityObserver=*living[size_t(menu->spectator)%living.size()];}
    teamTint={1,1,1};prepareCockpitLighting(w);prepareSearchlights(w);
    glClearColor(environmentView.sky.x,environmentView.sky.y,environmentView.sky.z,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    sky(w,projection,camera);
    glUseProgram(program);glBindVertexArray(vao);vp=multiply(projection,inverseRigid(camera));
    if(menu && menu->screen!=Screen::Playing) {
        frontend(*menu,multiply(translation(w.position.x,w.position.y+12,w.position.z),rotation(w.facing())));return;
    }
    // Preserve asymmetric XR frusta, but give the world a useful depth range.
    Matrix worldProjection=projection;constexpr float nearWorld=.75f,farWorld=3000.f;
    worldProjection[10]=-(farWorld+nearWorld)/(farWorld-nearWorld);worldProjection[14]=-2*farWorld*nearWorld/(farWorld-nearWorld);
    vp=multiply(worldProjection,inverseRigid(camera));
    worldView=inverseRigid(camera);worldPass=true;worldNear=0;worldFar=3000;
    // Use the current eye's asymmetric side planes for structures and parts.
    worldFrustum={unit({projection[0],0,projection[8]-1}),unit({-projection[0],0,-projection[8]-1}),
                  unit({0,projection[5],projection[9]-1}),unit({0,-projection[5],-projection[9]-1})};
    horizon(w);
    // Keep millimetre-scale facade details resolvable at long range without
    // changing geometry, XR eye projections, or requiring depth extensions.
    prepareWorldDrawCandidates=true;
    worldDrawBand=1;
    for(auto [nearPlane,farPlane]:worldDepthBands){
        worldNear=nearPlane;worldFar=farPlane;
        // Slight opaque overlap closes floating-point clipping seams. World
        // sprites use the nominal half-open interval in their fragment shader,
        // so translucent effects never blend twice across a slice boundary.
        float clipNear=std::max(.75f,nearPlane-.02f),clipFar=farPlane+.02f;
        auto slice=worldProjection;slice[10]=-(clipFar+clipNear)/(clipFar-clipNear);slice[14]=-2*clipFar*clipNear/(clipFar-clipNear);
        vp=multiply(slice,worldView);glClear(GL_DEPTH_BUFFER_BIT);
        drawWorldLayer(w,camera,paused,menu);
        drawWeather(w,camera);
        drawSearchlights(w,camera);
        drawInsertionWorld(w);
        worldDrawBand<<=1;
    }
    worldPass=false;
    if(menu&&menu->renderWorldOnly)return;
    if(menu&&menu->spectating){
        vp=multiply(projection,inverseRigid(camera));glClear(GL_DEPTH_BUFFER_BIT);
        Matrix hud=multiply(head,translation(0,0,-2));
        std::vector<const Pilot*> living;for(const auto& p:menu->remotePilots)if(!p.lifeSpent&&p.health>0)living.push_back(&p);
        std::string target=living.empty()?"WAITING":"P"+std::to_string(living[size_t(menu->spectator)%living.size()]->playerSlot+1);
        text("SPECTATING "+target+" - STICK RIGHT / TAB: NEXT",multiply(hud,translation(-.65f,.55f,0)),.009f,{.7f,1,.8f});
        text(menu->voiceMessage,multiply(hud,translation(-.6f,-.5f,0)),.008f,{.7f,1,.8f});return;
    }
    if(w.rpgDroneView){
        vp=multiply(projection,inverseRigid(camera));glClear(GL_DEPTH_BUFFER_BIT);
        auto hud=multiply(head,translation(0,0,-2));
        text("STRIKE DRONE / "+std::to_string(int(w.rpgDroneRemaining))+" S",multiply(hud,translation(-.65f,.55f,0)),.009f,{.2f,1,.8f});
        text("LOOK TO STEER / RT DETONATE / LEFT GRIP RETURN",multiply(hud,translation(-.75f,-.5f,0)),.007f,{.7f,1,.8f});return;
    }
    // Cockpit/arms use a separate near pass while ejected geometry stays in the world.
    if(!w.lifeSpent){glClear(GL_DEPTH_BUFFER_BIT);vp=multiply(projection,inverseRigid(camera));}
    if(!w.missile.active&&!w.lifeSpent)drawArms(w);
    if(!w.lifeSpent)drawCockpit(w,camera,paused);
    drawInsertionStatus(w,head);
    Matrix cockpit=cockpitPose(w);
    // Status overlays use a head plane; aim sights use their actual target depth.
    Matrix hud=multiply(head,translation(0,0,-2));
    if(menu&&menu->environmentIntro>0&&!menu->environmentMessage.empty())
        text(menu->environmentMessage,multiply(hud,translation(-.75f,.66f,0)),.008f,{.8f,.95f,.6f});
    if(w.missionStatus==MissionStatus::Ejecting){
        if(w.ejectionTime<1.5f)text(w.ejectionTime<1?"REACTOR FAILURE":"EJECT EJECT EJECT",multiply(cockpit,translation(-.38f,.2f,-1.2f)),.014f,{1,.15f,.05f});
        return;
    }
    if(!w.missile.active&&!w.lifeSpent&&w.targetingWorking()) {
        const bool fittedSights = activeInterior() && textures.contains("chassis-sights");
        // The near pass contains only local arms and cockpit steel. It can
        // occlude the optical sights when the pilot leans over a solid panel.
        if(fittedSights){
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
        }else glDisable(GL_DEPTH_TEST);
        auto reticle=[&](Vec3 target,const char* label){
            auto local=point(multiply(inverseRigid(head),translation(target.x,target.y,target.z)));
            if(local[2]>=-.1f)return;
            auto placement=sightPlacement({local[0],local[1],local[2]});
            Matrix sight=multiply(head,multiply(translation(placement.position.x,placement.position.y,placement.position.z),scaling(placement.scale,placement.scale,placement.scale)));
            if(fittedSights && label[0]!='T'){
                const int column=label[0]=='H'?0:label[0]=='L'?1:2;
                // The atlas covers .1 tangent units at the source camera.
                art("chassis-sights",multiply(sight,multiply(translation(-.1f,-.1f,0),scaling(.2f,.2f,1))),
                    {column/3.f,(w.rpgChassis+1)/8.f,1.f/3,-1.f/8});
                return;
            }
            float radius=label[0]=='L'?.035f:label[0]=='R'?.05f:.065f;
            Vec3 color=label[0]=='T'?Vec3{1,.85f,.1f}:Vec3{.6f,1,.8f};
            for(float x:{-radius,radius})box(multiply(sight,multiply(translation(x,0,0),scaling(.011f,.004f,.001f))),color);
            for(float y:{-radius,radius})box(multiply(sight,multiply(translation(0,y,0),scaling(.004f,.011f,.001f))),color);
            text(label,multiply(sight,translation(label[0]=='L'?-.1f:label[0]=='R'?.08f:-.018f,label[0]=='H'?.12f:-.055f,0)),.009f,color);
        };
        if((!w.rpgPrototype||w.rpgAimMask&4)&&w.rpgVehicleBlend<.01f)reticle(w.traceAim(w.eye(),direction(w.aimYaw,w.aimPitch)).position,"H");
        if((!w.rpgPrototype||w.rpgAimMask)&&w.homingTarget>=0&&size_t(w.homingTarget)<w.targets.size())reticle(w.targets[w.homingTarget].center,"T");
        for(int h=0;h<2;++h)if(w.arms[h].tracked&&(!w.rpgPrototype||(w.rpgAimMask&(1u<<h)))&&w.rpgVehicleBlend<.01f)
            reticle(w.traceAim(w.arms[h].muzzle,w.arms[h].forward).position,h?"R":"L");
        if(fittedSights){glDepthMask(GL_TRUE);glDisable(GL_BLEND);}
        glEnable(GL_DEPTH_TEST);
    }
    if(menu&&menu->cooperative)text(menu->voiceMessage,multiply(hud,translation(-.6f,-.65f,0)),.007f,{.7f,1,.8f});
    if(w.mission>=0){
        // Transparent, body-anchored text just above the central instrument.
        if(!activeInterior()&&w.visibilityRules()){
            const auto sight=visibilityRanges(w);
            const bool scan=w.rpgPrototype&&w.rpgRadar.strength>0;
            const bool scanExposes=w.rpgPrototype&&w.rpgRadar.exposesPilot;
            const bool light=searchlightActive(w);
            const bool exposed=light||scanExposes;
            const std::string label=scanExposes?"SCAN / EXPOSED ":light?"LIGHT ON / EXPOSED ":
                scan?"SCAN / COVERT ":"LIGHT OFF / SIGHT ";
            text(label+std::to_string(int(std::lround(exposed?sight.lit:sight.base)))+" M",
                multiply(hud,translation(-.6f,.46f,0)),.007f,exposed?Vec3{1,.8f,.35f}:Vec3{.6f,.8f,.7f});
        }
        if(w.rpgPrototype&&!activeInterior()&&!w.rpgPilotStatus.empty())
            text(w.rpgPilotStatus,multiply(hud,translation(-.6f,.40f,0)),.006f,{.75f,1,.7f});
        if(w.rpgPrototype&&!activeInterior()&&w.rpgThrustCapacity>0){
            auto meter=multiply(cockpit,translation(torso.x+.18f,torso.y-.04f,torso.z+.25f));
            for(int i=0;i<w.rpgThrustCapacity;++i){
                const float fill=std::clamp(w.rpgThrustCharge-float(i),0.f,1.f);
                box(multiply(meter,multiply(translation(.026f*float(i),0,0),
                    scaling(.009f,.003f,.001f))),Vec3{.10f,.17f,.19f}+Vec3{.2f,.58f,.65f}*fill);
            }
        }
        if(w.assault){text(std::string(teamName(w.team))+" / A ORANGE / D CYAN",multiply(hud,translation(-.6f,.52f,0)),.007f,teamColour(w.team));}
        if(!activeInterior()){
            auto lines=missionLines(w.objective,26);
            lines.push_back((w.rpgPrototype?std::string{}:"ARMOR "+std::to_string(int(w.health))+"  ")+"OBJ "+std::to_string(std::max(0,w.counters[0]))+"/"+std::to_string(std::max(0,w.counters[1]))+"/"+std::to_string(std::max(0,w.counters[2])));
            float row=0;constexpr float font=.0055f;
            const float top=torso.y+(w.rpgPrototype?.17f+.05f*float(lines.size()-1):.18f);
            for(const auto& line:lines){text(line,multiply(cockpit,translation(torso.x-float(line.size())*2*font,top-row*.05f,torso.z+.25f)),font,{.75f,1,.7f});++row;}
        }
        if(w.missionStatus==MissionStatus::Complete||w.missionStatus==MissionStatus::Failed){
            if(menu){
                text(menu->resultMessage,multiply(hud,translation(-.5f,.18f,0)),.008f,{1,.8f,.3f});
                text(menu->saveMessage,multiply(hud,translation(-.5f,.12f,0)),.008f,{1,.4f,.3f});
            }
            text((w.missionStatus==MissionStatus::Complete||(menu&&menu->teamResult==1))?"MISSION COMPLETE":"MISSION FAILED",multiply(hud,translation(-.38f,.3f,0)),.012f,{1,.8f,.3f});
            text(w.rpgPrototype?"A: DEBRIEF":(w.missionPrototype||w.assault)?"ENTER: EXIT PROTOTYPE":menu&&menu->cooperative?"ENTER / MENU: MISSION CONTROLS":w.missionStatus==MissionStatus::Complete?"ENTER / MENU: MISSION SELECT":"ONE LIFE LOST - ENTER: MISSION SELECT",multiply(hud,translation(-.5f,.24f,0)),.008f,{.8f,1,.8f});
        }
    }
    if(w.missile.active) {
        text("CRUISE / HEAD GUIDANCE",multiply(hud,translation(-.34f,.64f,0)),.01f,{.6f,.85f,.8f});
        if(paused) text("PAUSED",multiply(hud,translation(-.12f,.16f,0)),.015f,{1,.7f,.3f});
    }
}
}
