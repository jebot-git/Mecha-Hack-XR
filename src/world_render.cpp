#include "render.hpp"
#include "texture_mips.hpp"
#include "terrain_art.hpp"
#include "surface_weather.hpp"
#include "surface_shelter.hpp"
#include "searchlight.hpp"
#include "rpg/enemy_animation.hpp"
#include "rpg/destruction.hpp"
#include "rpg/enemy_effects.hpp"
#include <bit>
#include <chrono>
#include <numeric>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace iron {
using namespace avp;
// A weather-shelter texel is a ceiling height. Reduce with max to preserve
// roofs and the open-air sentinel, including the last row/column of odd grids.
// R32F linear filtering / automatic mip generation is optional on GLES.
void Renderer::prepareShelter(const World& w,bool required){
    if(shelterTexture||!w.rpgPrototype||!w.proceduralTerrain)return;
    if(!shelterJob.valid()){
        // Capture only immutable roof, deck and shoreline data. GL calls stay
        // on the rendering thread and a new world discards the old pyramid.
        shelterJob=std::async(std::launch::async,[bounds=w.movementBounds,regions=w.interiorRegions,
                                                  decks=w.interiorDecks,water=w.waterRegions]{
            SurfaceShelter cover(bounds,regions,decks,water);
            std::vector<ShelterMip> levels;
            levels.push_back({cover.width,cover.height,std::move(cover.ceiling)});
            while(levels.back().width>1||levels.back().height>1){
                const auto& prev=levels.back();
                int width=std::max(1,prev.width/2),height=std::max(1,prev.height/2);
                ShelterMip next{width,height,std::vector<float>(size_t(width)*height)};
                for(int y=0;y<height;++y)for(int x=0;x<width;++x){
                    int left=x*prev.width/width,top=y*prev.height/height;
                    float ceiling=prev.pixels[top*prev.width+left];
                    for(int r=top;r<(y+1)*prev.height/height;++r)
                        for(int c=left;c<(x+1)*prev.width/width;++c)
                            ceiling=std::max(ceiling,prev.pixels[r*prev.width+c]);
                    next.pixels[y*width+x]=ceiling;
                }
                levels.push_back(std::move(next));
            }
            return levels;
        });
    }
    if(!required&&shelterJob.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
    auto levels=shelterJob.get();
    glActiveTexture(GL_TEXTURE3);
    glGenTextures(1,&shelterTexture);
    glBindTexture(GL_TEXTURE_2D,shelterTexture);
    for(size_t i=0;i<levels.size();++i){
        const auto& m=levels[i];
        glTexImage2D(GL_TEXTURE_2D,GLint(i),GL_R32F,m.width,m.height,0,GL_RED,GL_FLOAT,m.pixels.data());
    }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE0);
}
void Renderer::clearWorld(){
    auto release=[](Model& m){if(m.vbo)glDeleteBuffers(1,&m.vbo);if(m.vao)glDeleteVertexArrays(1,&m.vao);m={};};
    sectorBuffers.clear();
    shelterJob={};
    if(shelterTexture){glDeleteTextures(1,&shelterTexture);shelterTexture=0;}
    release(groundMesh);release(waterMesh);release(interiorDeckMesh);release(damageDecals);damageDecalKey=0;corpsePoses.clear();livingPoses.clear();
    for(auto& m:pickupModels)release(m);
    stagingOrder.clear();stagingCursor=0;
    worldModels.clear();worldWrecks.clear();worldMeshSources.clear();worldEnemyRigs.clear();worldSignature.reset();
}
void Renderer::prepareWorldMaterials(const World& w){
    if(!worldProgram){
        auto compile=[](GLenum type,const char* code){GLuint shader=glCreateShader(type);ironShaderSource(shader,code);glCompileShader(shader);GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);if(!ok)throw std::runtime_error("World shader compile failed");return shader;};
        GLuint v=compile(GL_VERTEX_SHADER,R"(#version 330 core
layout(location=0) in vec3 p;layout(location=1) in vec3 color;layout(location=2) in vec2 uv;layout(location=3) in float textured;layout(location=4) in float oneSided;
uniform mat4 transform;uniform mat4 worldPose;out vec3 worldPoint;out vec3 C;out vec2 UV;flat out float T;flat out float S;out float eyeDepth;
// Perspective-correct interpolation keeps facade panels attached to their
// geometry as either XR eye moves. Screen-affine UVs visibly stretch in VR.
void main(){worldPoint=(worldPose*vec4(p,1)).xyz;gl_Position=transform*vec4(p,1);eyeDepth=gl_Position.w;C=color;UV=uv;T=textured;S=oneSided;})");
        GLuint f=compile(GL_FRAGMENT_SHADER,R"(#version 330 core
in vec3 C;in vec2 UV;flat in float T;flat in float S;uniform sampler2D atlas;uniform sampler2D psxAtlas;in float eyeDepth;uniform vec4 envFog;uniform vec3 envTint;uniform float envGain;out vec4 color;
uniform vec4 lampOriginRange;uniform vec4 lampAxisBase;in vec3 worldPoint;
uniform highp sampler2DArray terrainSurfaces;uniform bool terrainArtReady;
uniform vec4 depositPaint; // blended solid colour and coverage
uniform float wetness;
uniform bool receivesDeposits;uniform highp sampler2D shelter;uniform vec4 shelterArea;
vec3 settledSurface(vec3 base,float tileMask){
    vec3 face=cross(dFdx(worldPoint),dFdy(worldPoint));
    if(face.y<=.35*length(face))return base;
    vec2 p=worldPoint.xz;
    if(worldPoint.y+.08<textureLod(shelter,(p-shelterArea.xy)*shelterArea.zw,0.0).r)return base;
    if(depositPaint.a>0.0){
        float coverage=depositPaint.a;
        if(coverage<1.0&&tileMask>=0.0){
            coverage=clamp(depositPaint.a*2.0-tileMask,0.0,1.0);
        }
        base=mix(base,depositPaint.rgb*.94,coverage*.77);
    }
    return base*(1.0-.38*wetness);
}
float searchlightTransmission(){float density=envFog.a;
if(lampOriginRange.w>0.0){vec3 delta=worldPoint-lampOriginRange.xyz;float distance=length(delta);
if(distance<lampOriginRange.w&&dot(delta/max(distance,.001),lampAxisBase.xyz)>.9063078)density*=lampAxisBase.w/lampOriginRange.w;}
return floor(clamp(1.0-density*max(0.0,eyeDepth),0.0,1.0)*255.0)/255.0;}
void main(){if(T<2.5&&S>.5&&!gl_FrontFacing)discard;
vec3 material=C;
float emissionMask=0.;
float tileMask=-1.0;
if(T>3.5){
    if(terrainArtReady){
        bool road=T>4.5;
        float baseLayer=road?floor((T-5.0)*.5):S;
        vec2 groundUV=road?worldPoint.xz/32.0:UV;
        vec3 paint=texture(terrainSurfaces,vec3(groundUV,baseLayer)).rgb;
        // Share the world-aligned alpha sample with the base terrain shading.
        float deposits=texture(terrainSurfaces,vec3(worldPoint.xz/230.0,baseLayer)).a;
        tileMask=clamp(2.5*deposits-.75,0.0,1.0);
        if(road){
            float lane=8.0+mod(T-5.0,2.0);
            // Clip at the measured carriageway edges and the two segment ends.
            // fwidth antialiases those boundaries as the road recedes.
            vec2 width=max(fwidth(UV),vec2(.00001));
            float across=(.5-abs(UV.x-.5))/width.x;
            float along=min(UV.y,abs(S)-UV.y)/width.y;
            float cover=clamp(min(across,along)+.5,0.0,1.0);
            vec3 asphalt=texture(terrainSurfaces,vec3(clamp(UV.x,.01,.99),UV.y,lane)).rgb;
            if(S<0.0){
                // Beyond the chosen segment this junction is still paved by a
                // connecting road. Keep the crossing clear of misleading stripes.
                paint=texture(terrainSurfaces,vec3(worldPoint.xz/24.0,8.0)).rgb;
            }
            paint=mix(paint,asphalt,cover);
        }
        vec3 face=cross(dFdx(worldPoint),dFdy(worldPoint));
        float side=dot(face.xz,vec2(.78,.62))*sign(face.y)/max(.00001,abs(face.y));
        material=paint*mix(.82,1.14,deposits)*clamp(1.0+.7*side,.78,1.22);
    }
    color=vec4(material*envGain,1);
}else if(T>2.5){
// Procedural structure pieces repeat their trim tile in local metres. Their
// geometry can span a room without stretching a 28px panel over the whole wall.
vec2 tile=vec2(mod(S,8.0),floor(S/8.0));
vec2 sampleUV=(tile*32.0+vec2(2.5)+fract(UV)*27.0)/256.0;
vec4 texel=textureGrad(psxAtlas,sampleUV,dFdx(UV)*27.0/256.0,dFdy(UV)*27.0/256.0);
material*=texel.rgb;
emissionMask=texel.a;
color=vec4(material*envGain,1);
}else if(T>1.5){
    vec4 texel=texture(psxAtlas,UV);
    material=texel.rgb*C;
    emissionMask=texel.a;
    color=vec4(material*envGain,1);
}else color=T>.5?texture(atlas,UV):vec4(C*envGain,1);
if(receivesDeposits)color.rgb=settledSurface(color.rgb/max(envGain,.01),tileMask)*envGain;
vec3 lit=color.rgb*envTint;
if(!receivesDeposits && emissionMask>0.){
    vec3 emitter=pow(max(material,vec3(0)),vec3(2.2));
    float peak=max(.02,max(emitter.r,max(emitter.g,emitter.b)));
    lit=pow(pow(max(lit,vec3(0)),vec3(2.2))+emitter*(.85*emissionMask/peak),vec3(1./2.2));
}
color.rgb=mix(envFog.rgb,lit,searchlightTransmission());})");
        worldProgram=glCreateProgram();glAttachShader(worldProgram,v);glAttachShader(worldProgram,f);glLinkProgram(worldProgram);glDeleteShader(v);glDeleteShader(f);GLint ok;glGetProgramiv(worldProgram,GL_LINK_STATUS,&ok);if(!ok)throw std::runtime_error("World shader link failed");worldTransform=glGetUniformLocation(worldProgram,"transform");glUseProgram(worldProgram);glUniform1i(glGetUniformLocation(worldProgram,"psxAtlas"),1);glUniform1i(glGetUniformLocation(worldProgram,"terrainSurfaces"),2);
        surfaceReceiver=glGetUniformLocation(worldProgram,"receivesDeposits");
        surfacePaint=glGetUniformLocation(worldProgram,"depositPaint");
        surfaceWetness=glGetUniformLocation(worldProgram,"wetness");
        shelterArea=glGetUniformLocation(worldProgram,"shelterArea");
        glUniform1i(glGetUniformLocation(worldProgram,"shelter"),3);
    }
    if(w.rpgPrototype&&!terrainTexture){
        rpg::preloadEnemyAnimationRigs(gameplayRoot.parent_path()/"models");
        // Older external packs can still use the heightfield's existing vertex colours.
        const auto path=gameplayRoot.parent_path()/"textures/terrain-surfaces.rgba";
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        constexpr auto bytes=terrainArt::side*terrainArt::side*terrainArt::Count*4;
        glUseProgram(worldProgram);
        glGenTextures(1,&terrainTexture);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D_ARRAY,terrainTexture);
        if(file){
            if(file.tellg()!=bytes)throw std::runtime_error("Invalid terrain material array: "+path.string());
            std::vector<unsigned char> pixels(bytes);
            file.seekg(0);
            if(!file.read(reinterpret_cast<char*>(pixels.data()),bytes))throw std::runtime_error("Incomplete terrain materials");
            glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA8,terrainArt::side,terrainArt::side,terrainArt::Count,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            glUniform1i(glGetUniformLocation(worldProgram,"terrainArtReady"),1);
        }else{
            const unsigned char neutral[]{255,255,255,0};
            glTexImage3D(GL_TEXTURE_2D_ARRAY,0,GL_RGBA8,1,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,neutral);
        }
        generateTextureMips(GL_TEXTURE_2D_ARRAY,GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glActiveTexture(GL_TEXTURE0);
    }
    int textureSet=w.missionPrototype?2:w.game;
    if(worldTextureGame!=textureSet){
        if(!worldTextures[0])glGenTextures(2,worldTextures);
        auto root=w.game?gameplayRoot.parent_path()/"is2/gameplay":gameplayRoot;
        if((!w.game||w.missionPrototype)&&std::filesystem::exists(gameplayRoot.parent_path()/"is2/shared/gameplay/missions.json"))root=gameplayRoot.parent_path()/"is2/shared/gameplay";
        int width=256,height=158;{std::ifstream info(root/"missions.json");nlohmann::json j;info>>j;if(j.contains("atlas_size")){width=j.at("atlas_size")[0];height=j.at("atlas_size")[1];}if(width<1||height<1||width>4096||height>4096)throw std::runtime_error("Invalid atlas dimensions");}
        for(int night=0;night<2;++night){std::ifstream file(root/(night?"atlas-night.rgba":"atlas-day.rgba"),std::ios::binary|std::ios::ate);if(!file||file.tellg()!=width*height*4)throw std::runtime_error("Decode gameplay atlas first");file.seekg(0);std::vector<unsigned char> pixels(width*height*4);file.read(reinterpret_cast<char*>(pixels.data()),pixels.size());glBindTexture(GL_TEXTURE_2D,worldTextures[night]);uploadRgbaTexture(width,height,pixels.data(),GL_LINEAR_MIPMAP_LINEAR,3);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
    }
    if(!pickupTextures[0]){
        auto root=gameplayRoot.parent_path()/"is2/shared/gameplay";if(!std::filesystem::exists(root/"missions.json"))root=gameplayRoot;
        std::ifstream info(root/"missions.json");nlohmann::json j;info>>j;int width=256,height=158;if(j.contains("atlas_size")){width=j["atlas_size"][0];height=j["atlas_size"][1];}
        glGenTextures(2,pickupTextures);
        for(int night=0;night<2;++night){std::ifstream file(root/(night?"atlas-night.rgba":"atlas-day.rgba"),std::ios::binary);std::vector<unsigned char> pixels(width*height*4);if(!file.read(reinterpret_cast<char*>(pixels.data()),pixels.size()))throw std::runtime_error("Missing pickup atlas");glBindTexture(GL_TEXTURE_2D,pickupTextures[night]);uploadRgbaTexture(width,height,pixels.data(),GL_NEAREST_MIPMAP_LINEAR,1);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
    }
    worldTextureGame=textureSet;
}
static const char* worldRigName(const World& w,const Target& t){
    if(w.rpgPrototype)return "";
    if(t.spawnType==3)return (t.assetGame<0?w.game:t.assetGame)?"iron-soldier-is2":"iron-soldier";
    return t.spawnType==14?"satyr":"";
}
Renderer::Model Renderer::worldBounds(const WorldMesh& vertices){
    Model m;
    if(vertices.empty())return m;
    m.count=GLsizei(vertices.size());
    m.low=m.high=vertices.front().position;
    for(const auto& v:vertices){
        const auto p=v.position;
        m.low={std::min(m.low.x,p.x),std::min(m.low.y,p.y),std::min(m.low.z,p.z)};
        m.high={std::max(m.high.x,p.x),std::max(m.high.y,p.y),std::max(m.high.z,p.z)};
    }
    return m;
}
Renderer::Model Renderer::uploadWorldMesh(const WorldMesh& vertices){
    auto m=worldBounds(vertices);
    if(!m.count)return m;
    glGenVertexArrays(1,&m.vao);glBindVertexArray(m.vao);
    glGenBuffers(1,&m.vbo);glBindBuffer(GL_ARRAY_BUFFER,m.vbo);
    static_assert(std::is_standard_layout_v<WorldVertex> && sizeof(Vec3)==3*sizeof(float));
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(WorldVertex),vertices.data(),GL_STATIC_DRAW);
    constexpr size_t offsets[]={offsetof(WorldVertex,position),offsetof(WorldVertex,color),offsetof(WorldVertex,u),offsetof(WorldVertex,textured),offsetof(WorldVertex,oneSided)};
    static_assert(offsetof(WorldVertex,v)==offsetof(WorldVertex,u)+sizeof(float));
    constexpr int sizes[]={3,3,2,1,1};
    for(int k=0;k<5;++k){glEnableVertexAttribArray(k);glVertexAttribPointer(k,sizes[k],GL_FLOAT,GL_FALSE,sizeof(WorldVertex),reinterpret_cast<void*>(offsets[k]));}
    return m;
}
void Renderer::prepareWorldSlots(const World& w){
    if(worldSignature!=w.renderIdentity){
        clearWorld();worldSignature=w.renderIdentity;
        groundMesh=uploadWorldMesh(w.geometry().terrain);waterMesh=uploadWorldMesh(w.geometry().waterMesh);interiorDeckMesh=uploadWorldMesh(w.geometry().interiorDeckMesh);
        for(unsigned i=0;i<5;++i)pickupModels[i]=uploadWorldMesh(w.pickupMeshes[i]);
    }
    if(worldModels.size()!=w.targets.size()){
        // The RPG inserts reinforcements at the end of its actor prefix.
        // Move the shared scenery suffix and its existing GL handles together.
        const auto oldCount=worldModels.size(),newCount=w.targets.size();
        size_t suffix=0;
        while(suffix<std::min(oldCount,newCount)){
            const auto &old=worldMeshSources[oldCount-1-suffix];
            const auto &target=w.targets[newCount-1-suffix];
            if(old.first!=target.mesh||old.second!=target.wreck)break;
            ++suffix;
        }
        const size_t first=std::min(oldCount,newCount)-suffix;
        auto shiftSlots=[&](auto &slots){
            auto pos=slots.begin()+first;
            if(newCount>oldCount)slots.insert(pos,newCount-oldCount,typename std::decay_t<decltype(slots)>::value_type{});
            else slots.erase(pos,pos+(oldCount-newCount));
        };
        shiftSlots(worldModels);shiftSlots(worldWrecks);shiftSlots(worldMeshSources);
        shiftSlots(worldEnemyRigs);shiftSlots(corpsePoses);shiftSlots(livingPoses);
    }
    // Reusable enemy slots may change family and rig. Refresh only that slot's
    // mesh/wreck pair; keeping the original VBO would draw the previous enemy.
    for(size_t i=0;i<w.targets.size();++i){const auto& t=w.targets[i];auto source=std::make_pair(t.mesh,t.wreck);
        if(worldMeshSources[i]==source)continue;
        corpsePoses[i]={};livingPoses[i]={};
        worldModels[i]={};worldWrecks[i]={};
        worldMeshSources[i]=std::move(source);
        worldEnemyRigs[i]=nullptr;
        if(w.rpgPrototype&&t.mesh){
            auto [it,inserted]=assetRigs.try_emplace({t.generatorAsset,t.mesh->size()},nullptr);
            if(inserted)it->second=motionLibrary.visualRig(t.generatorAsset,rpg::enemyAnimationRig(gameplayRoot.parent_path()/"models",t.generatorAsset,t.mesh->size()));
            worldEnemyRigs[i]=it->second;
        }
    }
}
void Renderer::uploadTargetMesh(const World& w,size_t i,bool wreck){
    const auto& t=w.targets[i];
    const auto& mesh=wreck?t.wreck:t.mesh;
    auto& slot=wreck?worldWrecks[i]:worldModels[i];
    if(slot.buffer||!mesh||mesh->empty())return;
    const auto* enemyRig=wreck?nullptr:worldEnemyRigs[i];
    auto rig=walkerRigs.find(worldRigName(w,t));
    const auto* walker=!wreck&&rig!=walkerRigs.end()&&mesh->size()==size_t(rig->second.vertices)?&rig->second:nullptr;
    // Geometry and vertex packing must both match. Differently painted or
    // scaled copies deliberately have distinct source meshes.
    const void* layout=enemyRig?static_cast<const void*>(enemyRig):walker;
    auto& cached=sectorBuffers[{mesh.get(),layout}];
    if(auto shared=cached.lock()){slot.buffer=std::move(shared);return;}
    WorldMesh packed;
    if(enemyRig||walker){
        packed.reserve(mesh->size());
        auto append=[&](const auto& ranges){
            for(auto [first,count]:ranges)packed.insert(packed.end(),mesh->begin()+first,mesh->begin()+first+count);
        };
        if(enemyRig)for(const auto& part:enemyRig->parts)append(part.ranges);
        else for(const auto& ranges:walker->ranges)append(ranges);
    }
    auto release=[](Model* model){
        glDeleteBuffers(1,&model->vbo);glDeleteVertexArrays(1,&model->vao);delete model;
    };
    slot.buffer=std::shared_ptr<Model>(new Model(uploadWorldMesh((enemyRig||walker)?packed:*mesh)),release);
    cached=slot.buffer;
    if(sectorBuffers.size()>2*w.targets.size()+32)std::erase_if(sectorBuffers,[](const auto& item){return item.second.expired();});
}
void Renderer::stageWorld(const World& w){
    if(w.targets.empty())return;
    if(worldSignature==w.renderIdentity&&stagingOrder.size()==w.targets.size()&&stagingCursor==stagingOrder.size()){
        prepareShelter(w,false);return;
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::microseconds(750);
    prepareWorldMaterials(w);
    prepareWorldSlots(w);
    prepareShelter(w,false);
    if(stagingOrder.size()!=w.targets.size()){
        stagingOrder.resize(w.targets.size());
        std::iota(stagingOrder.begin(),stagingOrder.end(),size_t{0});
        std::stable_sort(stagingOrder.begin(),stagingOrder.end(),[&](size_t a,size_t b){
            auto da=w.targets[a].center-w.position,db=w.targets[b].center-w.position;
            return dot(da,da)<dot(db,db);
        });
        stagingCursor=0;
    }
    while(stagingCursor<stagingOrder.size()&&std::chrono::steady_clock::now()<deadline){
        size_t i=stagingOrder[stagingCursor++];
        const auto& t=w.targets[i];
        if(t.active&&!t.escaped&&t.health>0)uploadTargetMesh(w,i,false);
    }
}
void Renderer::drawWorld(const World& w){
    if(w.targets.empty())return;
    prepareWorldMaterials(w);
    if(prepareWorldDrawCandidates)prepareWorldSlots(w);
    glUseProgram(worldProgram);glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D_ARRAY,terrainTexture);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,originalTexture);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,worldTextures[environmentView.night]);
    const auto deposits=surfaceWeather(w.previousSurfaceWeather,w.environment.weather,w.weatherTime());
    const bool coating=w.rpgPrototype&&w.proceduralTerrain&&
        std::any_of(deposits.begin(),deposits.end(),[](float amount){return amount>0;});
    prepareShelter(w,coating);
    if(coating){
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D,shelterTexture);
        glActiveTexture(GL_TEXTURE0);
        const auto &b=w.movementBounds;
        glUniform4f(shelterArea,b[0],b[1],1.f/std::max(1.f,b[2]-b[0]),1.f/std::max(1.f,b[3]-b[1]));
        constexpr Vec3 colours[]={{.81f,.60f,.32f},{.87f,.93f,.99f},{.48f,.47f,.49f},
                                  {.12f,.088f,.07f},{.36f,.44f,.17f}};
        float solid=0;
        Vec3 paint{};
        for(size_t i=0;i<std::size(colours);++i){
            solid+=deposits[i];
            paint=paint+colours[i]*deposits[i];
        }
        if(solid>0)paint=paint*(1.f/solid);
        glUniform4f(surfacePaint,paint.x,paint.y,paint.z,solid);
        glUniform1f(surfaceWetness,deposits[5]);
    }
    glUniform1i(surfaceReceiver,coating);
    teamTint={1,1,1};
    applyEnvironment(worldProgram,environmentView.solidGain);
    const auto uniforms=environmentLocations.at(worldProgram);
    auto setPose=[&](const Matrix &pose){
        const auto tint=environmentView.tint;
        glUniform3f(uniforms.tint,tint.x*teamTint.x,tint.y*teamTint.y,tint.z*teamTint.z);
        glUniformMatrix4fv(uniforms.pose,1,GL_FALSE,pose.data());
    };
    auto draw=[&](const Model& m,const Matrix& pose,GLint first=0){if(!m.count||!visibleWorld(m,pose))return;setPose(pose);glBindVertexArray(m.vao);auto transform=multiply(vp,pose);glUniformMatrix4fv(worldTransform,1,GL_FALSE,transform.data());glDrawArrays(GL_TRIANGLES,first,m.count);};
    if(w.rpgPrototype){
        uint64_t key=1469598103934665603ull;unsigned marks=0;
        for(const auto &fx:w.effects)if(fx.model==rpgScorch&&marks++<12)for(float n:{fx.position.x,fx.position.z,fx.size,float(int(fx.age*4))}){key^=std::bit_cast<uint32_t>(n);key*=1099511628211ull;}
        if(key!=damageDecalKey){WorldMesh mesh;marks=0;
            for(const auto &fx:w.effects)if(fx.model==rpgScorch&&marks++<12){
                float radius=fx.size*std::clamp((20-fx.age)/5,0.f,1.f);auto center=fx.position;center.y=w.surfaceHeight(center.x,center.z)+.035f;
                for(int n=0;n<10;++n){Vec3 p[2];for(int k=0;k<2;++k){float a=float(n+k)*2*pi/10;float r=radius*(.88f+.12f*std::sin(a*3+fx.position.x));
                    p[k]={center.x+std::sin(a)*r,0,center.z+std::cos(a)*r};p[k].y=w.surfaceHeight(p[k].x,p[k].z)+.035f;}
                    mesh.push_back({center,{.045f,.037f,.025f}});mesh.push_back({p[0],{.09f,.07f,.045f}});mesh.push_back({p[1],{.09f,.07f,.045f}});}}
            if(damageDecals.vbo)glDeleteBuffers(1,&damageDecals.vbo);
            if(damageDecals.vao)glDeleteVertexArrays(1,&damageDecals.vao);
            damageDecals=uploadWorldMesh(mesh);damageDecalKey=key;
        }
    }
    // Overlay colors use source painter order on the continuous ground slab.
    // Keep the slab's depth for subsequent actors, without per-polygon lifts
    // or competing coplanar depth values on desktop/GLES multisample buffers.
    if(w.proceduralTerrain){glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
        if(w.geometry().terrainPatches.empty())draw(groundMesh,identity);
        else for(const auto& patch:w.geometry().terrainPatches){Model region=groundMesh;region.low=patch.low;region.high=patch.high;region.count=GLsizei(patch.count);draw(region,identity,GLint(patch.first));}
        draw(interiorDeckMesh,identity);glUniform1i(surfaceReceiver,0);draw(waterMesh,identity);}
    else {glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);draw(groundMesh,identity);glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);}
    if(w.rpgPrototype&&damageDecals.count){glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1,-1);glDepthMask(GL_FALSE);draw(damageDecals,identity);glDepthMask(GL_TRUE);glDisable(GL_POLYGON_OFFSET_FILL);}
    glDepthFunc(GL_LEQUAL);
    if(corpsePoses.size()!=w.targets.size())corpsePoses.resize(w.targets.size());
    if(prepareWorldDrawCandidates){
        prepareWorldDrawCandidates=false;
        worldDrawCandidates.clear();
        for(size_t i=0;i<w.targets.size();++i){
            const auto &t=w.targets[i];
            auto& motion=livingPoses[i];
            bool diving=false;
            if(worldEnemyRigs[i]&&!t.active&&motion.seen&&motion.wasActive){
                // The authority moves a burrower to its exit while it is hidden.
                // Render the dive at the last visible position, then the breach at the exit.
                const auto* actor=motionLibrary.find(t.generatorAsset);
                if(actor&&actor->clips.contains("burrow-entry")&&std::any_of(w.effects.begin(),w.effects.end(),[&](const WeaponEffect& e){
                    return e.model==rpg::rpgArrivalBurrow&&e.age<1.f&&length(e.position-t.origin)<10.f;
                }))motion.burrowStart=w.elapsed;
            }
            if(!t.active)diving=w.elapsed>=motion.burrowStart&&w.elapsed-motion.burrowStart<.82f;
            if(!motion.wasActive&&t.active&&worldEnemyRigs[i]){
                const auto* actor=motionLibrary.find(t.generatorAsset);
                if(actor&&actor->clips.contains("burrow-eruption")&&std::any_of(w.effects.begin(),w.effects.end(),[&](const WeaponEffect& e){
                    return e.model==rpg::rpgArrivalBurrow&&e.age<2.8f&&length(e.position-t.origin)<10.f;
                }))motion.burrowStart=w.elapsed;
            }
            motion.wasActive=t.active;motion.seen=true;
            if(t.active)motion.lastOrigin=t.origin;
            if((!t.active&&!diving)||t.escaped||(t.spawnType>=0&&!visibilityInRange(w,visibilityObserver,diving?motion.lastOrigin:t.center)))continue;
            auto q=compose(t.ai.gravity.frame,angles(t.yaw,t.ai.pitch,t.ai.bank));
            auto p=diving?motion.lastOrigin:t.health>0?t.origin:w.wreckPosition(t);
            if(t.health>0&&t.ai.rpgGrapplePilot&&t.ai.rpgGrapplePilot==w.rpgLocalPilot&&w.rpgPoseLead>0){
                const auto before=rpg::sampleKnockdown(w.rpgKnockdown,w.elapsed);
                const auto after=w.knockdownPose();
                const auto upright=w.gravityFacing();
                const auto atTick=compose(upright,before.orientation);
                const auto delta=compose(compose(upright,after.orientation),{-atTick.x,-atTick.y,-atTick.z,atTick.w});
                const auto seat=w.bodyPoint(12);
                p=seat+rotate(upright,after.offset)+rotate(delta,t.origin-seat-rotate(upright,before.offset));
                q=compose(delta,q);
            }
            const auto pose=poseMatrix({{q.x,q.y,q.z,q.w},{p.x,p.y,p.z}});
            unsigned bands=7;
            // Animated parts and collapsing structures can move beyond their
            // bind bounds. Keep their existing per-part / per-pose tests.
            if(t.spawnType<0&&!worldEnemyRigs[i]&&(t.health>0||!w.rpgPrototype||t.rpgDeathAt<0)){
                auto &slot=t.health>0?worldModels[i]:worldWrecks[i];
                const auto& source=t.health>0?t.mesh:t.wreck;
                if(!slot.model().count&&source)slot.bounds=worldBounds(*source);
                const auto& mesh=slot.model();
                if(!mesh.count)continue;
                const auto local=(mesh.low+mesh.high)*.5f;
                const auto eye=point(multiply(worldView,pose),{local.x,local.y,local.z});
                const float radius=length(mesh.high-mesh.low)*.50001f;
                const Vec3 center{eye[0],eye[1],eye[2]};
                if(std::any_of(worldFrustum.begin(),worldFrustum.end(),[&](Vec3 plane){
                    return dot(plane,center)<-radius-.02f;
                }))continue;
                bands=0;
                for(size_t band=0;band<worldDepthBands.size();++band){
                    const auto [nearPlane,farPlane]=worldDepthBands[band];
                    if(-center.z+radius>=nearPlane-.02f&&-center.z-radius<=farPlane+.02f)bands|=1u<<band;
                }
            }
            if(!bands)continue;
            const bool livePose=t.health>0||(w.rpgPrototype&&t.rpgDeathAt>=0&&
                (worldEnemyRigs[i]||rpg::collapsePose(w.elapsed-t.rpgDeathAt,unsigned(i)).intact));
            uploadTargetMesh(w,i,!livePose);
            worldDrawCandidates.push_back({i,pose,q,bands});
        }
    }
    bool sceneryCoated=false;
    for(const auto &entry:worldDrawCandidates){
        if(!(entry.bands&worldDrawBand))continue;
        const auto i=entry.index;
        const auto &t=w.targets[i];
        const bool staticSurface=coating&&t.spawnType<0;
        if(sceneryCoated!=staticSurface){glUniform1i(surfaceReceiver,staticSurface);sceneryCoated=staticSurface;}
        auto base=entry.pose;
        teamTint=t.spawnType>=0&&t.team!=Team::Neutral?teamColour(t.team):Vec3{1,1,1};
        if(w.rpgPrototype)teamTint={teamTint.x*t.rpgTint.x,teamTint.y*t.rpgTint.y,teamTint.z*t.rpgTint.z};
        if(w.rpgPrototype&&t.health>0&&t.rpgInitialHealth>0){
            const float damage=std::clamp(1-t.health/t.rpgInitialHealth,0.f,1.f);
            teamTint=teamTint*(1-damage*.35f);
        }
        const auto enemyRig=worldEnemyRigs[i];
        const auto rig=walkerRigs.find(worldRigName(w,t));
        if(t.health<=0){glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1,-1);}
        if(enemyRig&&(t.health>0||t.rpgDeathAt>=0)){
            const WalkerPose *pose=nullptr;
            auto q=entry.orientation;
            if(t.health>0){
                auto &cached=livingPoses[i];
                const float time=w.elapsed+w.rpgPoseLead;
                if(cached.frame!=poseFrame||cached.time!=time){
                    auto live=rpg::poseLiveEnemy(*enemyRig,t,q,time);
                    if(const auto* a=motionLibrary.find(t.generatorAsset)){
                        auto cue=motionLibrary.enemyCue(*a,t,time);
                        float age=time-cached.burrowStart;
                        if(age>=0&&age<.82f)cue=motionLibrary.at(*a,{t.active?"burrow-eruption":"burrow-entry"},age/.82f);
                        if(cue.clip!=cached.clip){
                            cached.previous=cached.pose;cached.changed=time;cached.clip=cue.clip;
                        }
                        auto jump=a->clips.find("leap-land");
                        if(jump!=a->clips.end()&&cue.clip==&jump->second)
                            cue.phase=std::clamp((time-cached.changed)/1.55f,0.f,1.f);
                        motionLibrary.apply(*a,cue,live,enemyRig,nullptr,t.ai.combatMotion.aim>.1f,
                                            t.ai.voidmawPose.phase==rpg::VoidmawPose::Cling,t.ai.weaponYaw-t.yaw,t.ai.weaponPitch,t.ai.voidmawPose.phase==rpg::VoidmawPose::Discharge);
                        rpg::blendMotionTransition(live,cached.previous,(time-cached.changed)/.09f);
                    }
                    cached.pose=std::move(live);
                    cached.frame=poseFrame;cached.time=time;
                }
                pose=&cached.pose;
            }
            else {
                float age=std::max(0.f,w.elapsed-t.rpgDeathAt),poseAge=std::min(1.6f,age);auto &cached=corpsePoses[i];
                if(cached.age!=poseAge||cached.started!=t.rpgDeathAt){
                    cached.pose=rpg::poseEnemyDeath(*enemyRig,t.ai.gaitPhase,wrap(t.ai.weaponYaw-t.yaw),t.ai.weaponPitch,poseAge,unsigned(i)^w.prototypeSeed);cached.floor=-1e9f;
                    if(const auto* a=motionLibrary.find(t.generatorAsset))
                        motionLibrary.apply(*a,motionLibrary.enemyCue(*a,t,t.rpgDeathAt+poseAge),cached.pose,enemyRig);
                    auto support=[&](size_t part,Vec3 vertex){auto p=rotate(angles(t.yaw),animatedPoint(cached.pose.parts[part],vertex));cached.floor=std::max(cached.floor,w.walkableHeight(t.origin+Vec3{p.x,0,p.z})-p.y);};
                    for(size_t part=0;part<enemyRig->parts.size();++part){const auto &bounds=enemyRig->parts[part];
                        if(poseAge>=1.6f)for(auto [first,count]:bounds.ranges)for(size_t n=first;n<first+count;++n)support(part,(*t.mesh)[n].position);
                        else for(float x:{bounds.low.x,bounds.high.x})for(float y:{bounds.low.y,bounds.high.y})for(float z:{bounds.low.z,bounds.high.z})support(part,{x,y,z});}
                    cached.age=poseAge;cached.started=t.rpgDeathAt;
                }
                pose=&cached.pose;auto p=t.origin;p.y=std::max(cached.floor,t.origin.y-9*age*age);q=angles(t.yaw);
                base=poseMatrix({{q.x,q.y,q.z,q.w},{p.x,p.y,p.z}});teamTint={.60f,.56f,.48f};
            }
            const auto &mesh=worldModels[i].model();glBindVertexArray(mesh.vao);
            for(size_t part=0;part<enemyRig->parts.size();++part){
                bool detached=false;
                for(int joint=int(part);joint>=0&&t.rpgEnemyMissingArms;joint=enemyRig->parts[size_t(joint)].parent){
                    const auto &piece=enemyRig->parts[size_t(joint)];
                    if(piece.joint==rpg::EnemyJoint::Arm &&
                       (t.rpgEnemyMissingArms&(piece.side<0||(piece.side==0&&piece.pivot.x<0)?1u:2u))){
                        detached=true;break;
                    }
                }
                if(detached)continue;
                const auto &bounds=enemyRig->parts[part];const auto &local=pose->parts[part];
                auto matrix=multiply(base,poseMatrix({{local.orientation.x,local.orientation.y,local.orientation.z,local.orientation.w},{local.offset.x,local.offset.y,local.offset.z}}));
                Model bound=mesh;bound.low=bounds.low-Vec3{.15f,.15f,.15f};bound.high=bounds.high+Vec3{.15f,.15f,.15f};
                if(!visibleWorld(bound,matrix))continue;
                setPose(matrix);auto transform=multiply(vp,matrix);
                glUniformMatrix4fv(worldTransform,1,GL_FALSE,transform.data());auto [first,count]=enemyRig->drawRanges[part];glDrawArrays(GL_TRIANGLES,GLint(first),GLsizei(count));
            }
            if(int(i)==w.rpgCanopyAttacker&&worldNear<1.f&&t.health>0)
                drawCanopyDrool(w,*enemyRig,t,*pose,base);
        }else if(t.health>0&&rig!=walkerRigs.end()&&worldModels[i].model().count==rig->second.vertices){
            const auto& data=rig->second;auto pose=poseWalker(data.rig,t.ai.gaitPhase,t.ai.gravity.active()?length(t.ai.velocity):std::hypot(t.ai.velocity.x,t.ai.velocity.z),wrap(t.ai.weaponYaw-t.yaw));
            const auto& mesh=worldModels[i].model();glBindVertexArray(mesh.vao);
            for(size_t part=0;part<data.rig.parts.size();++part){auto& bounds=data.rig.parts[part];if(bounds.empty||(bounds.arm>=0&&(t.rpgEnemyMissingArms&(1u<<bounds.arm))))continue;
                auto& local=pose.parts[part];auto matrix=multiply(base,poseMatrix({{local.orientation.x,local.orientation.y,local.orientation.z,local.orientation.w},{local.offset.x,local.offset.y,local.offset.z}}));
                Model bound=mesh;bound.low=bounds.low-Vec3{.15f,.15f,.15f};bound.high=bounds.high+Vec3{.15f,.15f,.15f};if(!visibleWorld(bound,matrix))continue;
                setPose(matrix);auto transform=multiply(vp,matrix);glUniformMatrix4fv(worldTransform,1,GL_FALSE,transform.data());auto [first,count]=data.drawRanges[part];glDrawArrays(GL_TRIANGLES,first,count);
            }
        }else if(t.health<=0&&w.rpgPrototype&&t.rpgDeathAt>=0&&rpg::collapsePose(w.elapsed-t.rpgDeathAt,unsigned(i)).intact){
            float age=std::max(0.f,w.elapsed-t.rpgDeathAt);auto pose=rpg::collapsePose(age,unsigned(i));
            Vec3 origin=t.origin;float blend=std::clamp(age/1.6f,0.f,1.f);origin.y=origin.y*(1-blend)+t.rpgRestY*blend;
            if(rpg::destructionMaterial(t.generatorAsset)==DestructionMaterial::Wood){
                float progress=blend*blend*(3-2*blend);pose.rotation=angles(0,progress*pi/2);pose.scale={1,1,1};
                auto rotation=compose(angles(t.yaw),pose.rotation);auto &cached=corpsePoses[i];
                if(cached.age!=age||cached.started!=t.rpgDeathAt){cached.floor=-1e9f;for(const auto &v:*t.mesh){auto p=rotate(rotation,v.position);cached.floor=std::max(cached.floor,w.walkableHeight(t.origin+Vec3{p.x,0,p.z})-p.y);}cached.age=age;cached.started=t.rpgDeathAt;}
                origin=t.origin;origin.y=cached.floor;
            }
            auto rot=compose(angles(t.yaw),pose.rotation);auto collapse=multiply(poseMatrix({{rot.x,rot.y,rot.z,rot.w},{origin.x,origin.y,origin.z}}),scaling(pose.scale.x,pose.scale.y,pose.scale.z));
            draw(worldModels[i].model(),collapse);
        }else draw((t.health>0?worldModels[i]:worldWrecks[i]).model(),base);
        if(t.health<=0)glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if(sceneryCoated)glUniform1i(surfaceReceiver,0);
    teamTint={1,1,1};
    glBindTexture(GL_TEXTURE_2D,pickupTextures[environmentView.night]);
    for(const auto& p:w.pickups)if((!w.rpgPrototype||p.kind!=rpgSafeBoxPickup)&&!(w.cooperative?(p.collectedBy&(1u<<w.playerSlot)):p.collected)){
        unsigned kind=w.pickupTypes[unsigned(p.kind)&31];auto position=w.pickupPosition(p);draw(pickupModels.at(kind),translation(position.x,position.y,position.z));
    }
    glDepthFunc(GL_LESS);glUseProgram(program);glBindVertexArray(vao);
}
}
