#include "render.hpp"
#include "texture_mips.hpp"
#include <bit>
#include "presentation.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstdio>
namespace iron {
using namespace avp;
void Renderer::loadArt(const std::filesystem::path& baseRoot) {
    for(int campaign=0;campaign<2;++campaign){auto root=campaign?baseRoot.parent_path()/"is2/art":baseRoot;if(!std::filesystem::exists(root/"manifest.json")){if(campaign)continue;throw std::runtime_error("Missing original art manifest");}
    std::ifstream file(root/"manifest.json");nlohmann::json manifest;file>>manifest;
    for(const auto& entry:manifest.at("assets")) {
        std::string name=entry.at("name"),fileName=entry.at("rgba");
        if(std::filesystem::path(fileName).filename()!=fileName)throw std::runtime_error("Invalid art filename");
        int width=entry.at("width"),height=entry.at("height");
        if(width<=0||height<=0||width>4096||height>4096)throw std::runtime_error("Invalid art dimensions");
        std::ifstream data(root/fileName,std::ios::binary|std::ios::ate);
        size_t bytes=size_t(width)*height*4;
        if(!data||data.tellg()!=std::streamoff(bytes))throw std::runtime_error("Missing/truncated original art: "+name);
        data.seekg(0);std::vector<unsigned char> pixels(bytes);data.read(reinterpret_cast<char*>(pixels.data()),bytes);
        Texture t;t.width=width;t.height=height;glGenTextures(1,&t.id);glBindTexture(GL_TEXTURE_2D,t.id);
        if(name.find("horizon-")!=std::string::npos){
            // The strip's most common upper-edge colour is the intended clear
            // sky, rather than the unrelated fallback glClear colour.
            std::map<std::array<unsigned char,3>,unsigned> colours;
            for(size_t p=0;p<size_t(width)*std::min(2,height)*4;p+=4)if(pixels[p+3]>128)++colours[{pixels[p],pixels[p+1],pixels[p+2]}];
            if(!colours.empty()){auto colour=std::max_element(colours.begin(),colours.end(),[](const auto& a,const auto& b){return a.second<b.second;})->first;
                t.skyTop={colour[0]/255.f,colour[1]/255.f,colour[2]/255.f};t.hasSky=true;}
        }
        const int frames=entry.value("frames",1);
        float maxLod=1000.f;
        if(name=="hud-font")maxLod=2;
        else if(frames>1 && height%frames==0)
            maxLod=std::countr_zero(unsigned(height/frames)); // Frame boundaries must stay aligned.
        uploadRgbaTexture(width,height,pixels.data(),GL_NEAREST_MIPMAP_LINEAR,maxLod);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        textures.emplace(name,t);
    }
    std::ifstream f(root/"frontend.json");nlohmann::json j;f>>j;
    if(j.at("schema")!=1||!j.at("missions").is_array()||j.at("missions").empty())throw std::runtime_error("Invalid frontend mission manifest");
    auto& missions=campaigns[campaign];missions.resize(j.at("missions").size());
    for(int i=0;i<int(missions.size());++i){const auto& m=j.at("missions")[i];if(m.at("id")!=i+1)throw std::runtime_error("Invalid mission order");missions[i].title=m.at("title");for(const auto& l:m.at("lines"))missions[i].briefing.push_back(l.at("text"));}
    }
    auto loadEffects=[&](const char *file,const char *name,int width,int height){
        std::ifstream input(baseRoot.parent_path()/"fx"/file,std::ios::binary|std::ios::ate);
        const size_t bytes=size_t(width)*height*4;
        if(!input||input.tellg()!=std::streamoff(bytes))return;
        std::vector<unsigned char> pixels(bytes);
        input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(pixels.data()),bytes))return;
        Texture texture;texture.width=width;texture.height=height;
        glGenTextures(1,&texture.id);glBindTexture(GL_TEXTURE_2D,texture.id);
        uploadRgbaTexture(width,height,pixels.data(),GL_NEAREST_MIPMAP_LINEAR,6); // 64px animation cells
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        textures.emplace(name,texture);
    };
    loadEffects("weapon-fx.rgba","mecha-weapon-fx",512,256);
    loadEffects("fire-flipbook.rgba","mecha-fire-flipbook",256,256);
    loadEffects("pilot-service.rgba","pilot-service",512,512);
    loadEffects("cockpit-sights.rgba","chassis-sights",192,512);
    auto compile=[](GLenum type,const char* source){GLuint s=glCreateShader(type);ironShaderSource(s,source);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);if(!ok)throw std::runtime_error("Art shader compilation failed");return s;};
    GLuint v=compile(GL_VERTEX_SHADER,R"(#version 330 core
layout(location=0) in vec2 p;uniform mat4 transform;uniform mat4 worldPose;uniform float taper;uniform vec4 uvRect;uniform float uvAngle;uniform float uvAspect;out vec3 worldPoint;out vec2 uv;out vec2 spritePoint;out float eyeDepth;
void main(){vec2 q=p;q.x=.5+(p.x-.5)*mix(1.0,mix(.035,1.0,p.y),taper);worldPoint=(worldPose*vec4(q,0,1)).xyz;gl_Position=transform*vec4(q,0,1);eyeDepth=gl_Position.w;if(abs(uvAngle)<.00001)uv=uvRect.xy+p*uvRect.zw;
else{vec2 d=(p-.5)*uvRect.zw;d.x*=uvAspect;float c=cos(uvAngle),s=sin(uvAngle);d=mat2(c,s,-s,c)*d;d.x/=uvAspect;uv=uvRect.xy+uvRect.zw*.5+d;}spritePoint=p;})");
    GLuint fshader=compile(GL_FRAGMENT_SHADER,R"(#version 330 core
in vec2 uv;in vec2 spritePoint;in float eyeDepth;uniform sampler2D image;uniform vec3 tint;uniform vec2 depthRange;uniform float opacity;uniform float softness;uniform vec4 envFog;uniform vec3 envTint;uniform float envGain;out vec4 color;
uniform vec4 lampOriginRange;uniform vec4 lampAxisBase;in vec3 worldPoint;
float searchlightTransmission(){float density=envFog.a;
if(lampOriginRange.w>0.0){vec3 delta=worldPoint-lampOriginRange.xyz;float distance=length(delta);
if(distance<lampOriginRange.w&&dot(delta/max(distance,.001),lampAxisBase.xyz)>.9063078)density*=lampAxisBase.w/lampOriginRange.w;}
return floor(clamp(1.0-density*max(0.0,eyeDepth),0.0,1.0)*255.0)/255.0;}
void main(){if(depthRange.y>0.0&&(eyeDepth<depthRange.x||eyeDepth>=depthRange.y))discard;
vec4 c=texture(image,uv);if(c.a<.1)discard;vec2 edge=min(spritePoint,1.0-spritePoint);float feather=clamp(min(edge.x,edge.y)*6.0,0.0,1.0);
color=vec4(mix(c.rgb,vec3(1.0),softness)*tint,c.a*opacity*mix(1.0,feather,softness));color.rgb=mix(envFog.rgb,color.rgb*envTint*envGain,searchlightTransmission());})");
    artProgram=glCreateProgram();glAttachShader(artProgram,v);glAttachShader(artProgram,fshader);glLinkProgram(artProgram);glDeleteShader(v);glDeleteShader(fshader);
    GLint ok;glGetProgramiv(artProgram,GL_LINK_STATUS,&ok);if(!ok)throw std::runtime_error("Art shader link failed");
    artTransform=glGetUniformLocation(artProgram,"transform");artUV=glGetUniformLocation(artProgram,"uvRect");artColor=glGetUniformLocation(artProgram,"tint");
    artDepthRange=glGetUniformLocation(artProgram,"depthRange");artAlpha=glGetUniformLocation(artProgram,"opacity");artTaper=glGetUniformLocation(artProgram,"taper");artSoftness=glGetUniformLocation(artProgram,"softness");
    artAngle=glGetUniformLocation(artProgram,"uvAngle");artAspect=glGetUniformLocation(artProgram,"uvAspect");
    const float quad[]={0,0,1,0,1,1,0,0,1,1,0,1};
    glGenVertexArrays(1,&artVao);glBindVertexArray(artVao);glGenBuffers(1,&artVbo);glBindBuffer(GL_ARRAY_BUFFER,artVbo);glBufferData(GL_ARRAY_BUFFER,sizeof quad,quad,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),nullptr);
    v=compile(GL_VERTEX_SHADER,R"(#version 330 core
uniform mat4 camera;uniform vec4 lens;out vec3 direction;
void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2))*2.0-1.0;
gl_Position=vec4(p,.9999,1.0);direction=mat3(camera)*vec3((p.x+lens.z)/lens.x,(p.y+lens.w)/lens.y,-1.0);})");
    fshader=compile(GL_FRAGMENT_SHADER,R"(#version 330 core
in vec3 direction;
uniform vec3 rim,zenith,cloud;
uniform vec4 skyParams; // SkyKind, night mix, animation time, seed
const int legacySky=0,spaceSky=2,underwaterSky=3,volcanicSky=5,polarSky=7,lunarSky=8,interiorSky=9;
uniform vec4 weather; // coverage, opacity, celestial transmission, lightning
out vec4 color;

float hash31(vec3 p){
    p=fract(p*.1031);
    p+=dot(p,p.yzx+33.33);
    return fract((p.x+p.y)*p.z);
}
float skyNoise2D(vec2 p){
    vec2 cell=floor(p),f=fract(p);
    f=f*f*(3.0-2.0*f);
    return mix(mix(hash31(vec3(cell,1)),hash31(vec3(cell+vec2(1,0),1)),f.x),
               mix(hash31(vec3(cell+vec2(0,1),1)),hash31(vec3(cell+1.0,1)),f.x),f.y);
}
float clouds(vec2 p){
    return .59*skyNoise2D(p)+.28*skyNoise2D(p*2.07+7.2)+.13*skyNoise2D(p*4.17-12.6);
}
vec3 stars(vec3 ray,float density,float seed){
    vec3 p=ray*density;
    // Project on the dominant axis. Blend the planes at cube edges.
    vec3 axis=abs(ray);
    vec3 weights=smoothstep(vec3(.65),vec3(1.0),axis/max(max(axis.x,axis.y),axis.z));
    vec3 cell=floor(p);vec3 frac=fract(p)-.5;
    float dx=length(frac.yz),dy=length(frac.xz),dz=length(frac.xy);
    float a=hash31(vec3(sign(ray.x)*seed,cell.yz));
    float b=hash31(vec3(cell.x,sign(ray.y)*seed,cell.z));
    float c=hash31(vec3(cell.xy,sign(ray.z)*seed));
    vec3 noise=vec3(a,b,c),distance=vec3(dx,dy,dz);
    vec3 brightness=step(vec3(.956),noise)*(1.0-smoothstep(vec3(.045),vec3(.24),distance));
    float star=dot(weights,brightness)/max(1.0,weights.x+weights.y+weights.z);
    return vec3(.70,.83,1.0)*star*.66;
}
void main(){
    vec3 ray=normalize(direction);
    float y=ray.y,t=skyParams.z,seed=skyParams.w;
    int style=int(skyParams.x);
    float night=skyParams.y;
    vec3 result=mix(rim,zenith,smoothstep(-.035,.84,y));
    if(style==legacySky){
        color=vec4(mix(rim,zenith,smoothstep(.055,.9,max(0.0,y))),1.0);
        return;
    }
    if(style==interiorSky){
        float ceiling=smoothstep(-.22,.68,y);
        color=vec4(mix(rim,zenith,ceiling),1.0);
        return;
    }
    if(style==underwaterSky){
        vec2 p=ray.xz/max(.15,y+.33)*7.0;
        float surface=clouds(p+vec2(t*.055,-t*.037));
        float shafts=pow(max(0.0,sin(16.0*ray.x+4.0*ray.z+surface*3.0+t*.27)),5.0);
        vec3 water= mix(rim,zenith,smoothstep(-.27,.91,y));
        water+=cloud*(smoothstep(.46,.90,y)*smoothstep(.46,.88,surface)*.62+
                       shafts*smoothstep(.02,.94,y)*.13);
        color=vec4(water,1.0);
        return;
    }
    if(style==spaceSky||style==lunarSky){
        float dust=pow(max(0.0,1.0-abs(dot(ray,normalize(vec3(.37,.76,.45))))),9.0);
        result+=cloud*dust*(.18+.82*skyNoise2D(ray.xz*7.0+ray.xy*3.1));
        result+=stars(ray,126.0,seed+6.0)+stars(ray,215.0,seed+31.0)*.44;
        vec3 center=normalize(style==spaceSky?vec3(-.42,.44,-.82):vec3(.32,.56,-.76));
        vec3 across=normalize(cross(vec3(0,1,0),center));
        vec2 disk=vec2(dot(ray,across),dot(ray,cross(center,across)))/(style==spaceSky?.114:.066);
        float radial=dot(disk,disk);
        if(dot(ray,center)>0.0&&radial<1.02){
            float depth=sqrt(max(0.0,1.0-radial));
            float lit=max(.06,dot(vec3(disk,depth),normalize(vec3(-.62,.62,.48))));
            float bands=skyNoise2D(disk*vec2(style==spaceSky?4.0:14.0,22.0)+seed*.1);
            vec3 surface=mix(rim+vec3(.06,.09,.17),cloud+vec3(.32,.27,.18),bands);
            if(style==lunarSky)surface=mix(vec3(.055,.14,.23),vec3(.20,.41,.32),
                                    smoothstep(.46,.62,clouds(disk*4.5)));
            result=mix(result,surface*lit,1.0-smoothstep(.97,1.02,radial));
        }
    }else if(y>-.06){
        vec3 sun=normalize(vec3(-.52,.35,-.78));
        vec3 moon=normalize(vec3(.42,.49,-.78));
        float toSun=max(0.0,dot(ray,sun)),toMoon=max(0.0,dot(ray,moon));
        float sunDisk=smoothstep(.99970,.99991,toSun);
        float moonDisk=smoothstep(.99945,.99972,toMoon)*
                       (1.0-smoothstep(.99955,.99984,dot(ray,normalize(moon+vec3(.018,.009,0)))));
        result+=weather.z*(1.0-night)*(vec3(.30,.20,.11)*pow(toSun,7.0)+vec3(1.0,.85,.60)*sunDisk);
        if(night>.01&&weather.z>.01)
            result+=night*weather.z*(stars(ray,135.0,seed+8.0)*.67+vec3(.52,.68,.86)*(moonDisk+.12*pow(toMoon,16.0)));
        vec2 p=ray.xz/max(.22,y+.29)*2.5+vec2(seed*.17+float(style)*4.79,t*.016+float(style)*2.93);
        float n=clouds(p*vec2(.82,1.23));
        float soft=smoothstep(weather.x,weather.x+.21,n)*smoothstep(-.06,.26,y);
        float bright=smoothstep(.46,.75,n);
        vec3 shade=cloud*mix(.59,1.0,bright);
        result=mix(result,shade,soft*weather.y);
        if(style==polarSky&&night>.1&&weather.z>.1){
            float curtain=y-(.27+.063*sin(9.0*ray.x+2.0*ray.z));
            float arc=exp(-abs(curtain)*19.0)*smoothstep(.0,.49,-ray.z);
            result+=vec3(.08,.44,.24)*arc*night*weather.z*(.3+.7*skyNoise2D(ray.xz*18.0+t*.022));
        }
        if(style==volcanicSky){
            float soot=smoothstep(.44,.78,clouds(p*.77+6.0))*exp(-max(0.0,y)*3.3);
            result=mix(result,cloud*.78,soot*.56);
        }
    }
    if(style==lunarSky){
        float ridge=-.033+.024*skyNoise2D(ray.xz*6.7+seed);
        float surface=1.0-smoothstep(ridge-.003,ridge+.003,y);
        result=mix(result,rim*.78,surface);
    }
    result+=weather.w*vec3(.36,.44,.57)*(1.0-.44*max(0.0,y));
    color=vec4(clamp(result,0.0,1.0),1.0);
})");
    skyProgram=glCreateProgram();glAttachShader(skyProgram,v);glAttachShader(skyProgram,fshader);glLinkProgram(skyProgram);glDeleteShader(v);glDeleteShader(fshader);
    glGetProgramiv(skyProgram,GL_LINK_STATUS,&ok);if(!ok)throw std::runtime_error("Sky shader link failed");
    skyCamera=glGetUniformLocation(skyProgram,"camera");skyLens=glGetUniformLocation(skyProgram,"lens");skyRim=glGetUniformLocation(skyProgram,"rim");skyZenith=glGetUniformLocation(skyProgram,"zenith");
    skyCloud=glGetUniformLocation(skyProgram,"cloud");skyParams=glGetUniformLocation(skyProgram,"skyParams");
    skyWeather=glGetUniformLocation(skyProgram,"weather");glGenVertexArrays(1,&skyVao);
}
void Renderer::art(const std::string& name,const Matrix& model,std::array<float,4> uv,Vec3 c) {
    auto found=textures.find(name);if(found==textures.end())throw std::runtime_error("Missing original asset: "+name);
    glUseProgram(artProgram);glBindVertexArray(artVao);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,found->second.id);
    applyEnvironment(artProgram,environmentView.modelGain,&model);
    glUniform1f(artAlpha,spriteAlpha);glUniform1f(artTaper,spriteTaper);
    glUniform1f(artSoftness,spriteSoftness);
    glUniform1f(artAngle,spriteUVRotation);glUniform1f(artAspect,found->second.height>0?float(found->second.width)/found->second.height:1.f);
    glUniform2f(artDepthRange,worldPass?worldNear:0.f,worldPass?worldFar:0.f);
    auto m=multiply(vp,model);glUniformMatrix4fv(artTransform,1,GL_FALSE,m.data());glUniform4fv(artUV,1,uv.data());glUniform3f(artColor,c.x,c.y,c.z);glDrawArrays(GL_TRIANGLES,0,6);
    glUseProgram(program);glBindVertexArray(vao);
}
void Renderer::uploadBriefingHeader(int width,int height,const uint16_t *rgb565) {
    auto &texture=textures["briefing-header-frame"];
    if(!texture.id)glGenTextures(1,&texture.id);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture.id);
    GLint alignment;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT,2); // RGB565 rows may have odd pixel widths.
    if(texture.width!=width||texture.height!=height){
        texture.width=width;texture.height=height;
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,width,height,0,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,rgb565);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,rgb565);
    glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);
    generateTextureMips(GL_TEXTURE_2D,GL_LINEAR_MIPMAP_LINEAR);
}
static std::string thumbnail(int i){char b[40];std::snprintf(b,sizeof b,"mission-thumbnail-%02d",i+1);return b;}
void Renderer::frontend(const Frontend& state,const Matrix& base) {
    const auto& missions=campaigns.at(state.game);
    auto title=[&](int i)->const std::string&{return state.generatedMissions()?state.missionCards.at(i).title:missions.at(i).title;};
    auto thumb=[&](int i){return (state.game?"is2-":"")+thumbnail(i);};
    auto place=[&](float x,float y,float z,float w,float h){return multiply(base,multiply(translation(x,y,z),scaling(w,h,1)));};
    // Native 3D display at 3 m; panels keep the original 320-pixel coordinate system.
    float panelCenter=state.generatedMissions()&&state.screen==Screen::Briefing?171.f:137.f;
    auto pixel=[&](float x,float y,float z=0){return multiply(base,translation((x-160)*.013f,(panelCenter-y)*.013f,-3+z));};
    auto picture=[&](const std::string& name,float x,float y,float w,float h,float z=.005f,Vec3 tint={1,1,1}){art(name,multiply(pixel(x,y,z),scaling(w*.013f,-h*.013f,1)),{0,0,1,1},tint);};
    auto words=[&](const std::string& str,float x,float y,float size=1.f,Vec3 color=Vec3{.6f,1,.8f}){text(str,pixel(x,y,.02f),.013f*size,color);};
    const Vec3 green{.55f,1,.65f},amber{1,.78f,.2f};
    const auto variant=std::to_string(state.interfaceArtSeed&1u);
    auto optionalPicture=[&](const std::string& name,float x,float y,float w,float h,float z=.005f){
        if(name.empty()||!textures.contains(name))return false;
        picture(name,x,y,w,h,z);return true;
    };
    auto servicePortrait=[&](float x,float y,float size){
        const int pilot=state.pilotPortrait;
        if(pilot<0||pilot>=4||!textures.contains("pilot-service"))return false;
        const auto pose=multiply(pixel(x,y,.011f),scaling(size*.013f,-size*.013f,1));
        art("pilot-service",pose,{.5f*(pilot%2),.5f*(pilot/2),.5f,.5f});
        // Register the stitched chevrons to a broad shoulder panel on each painted flight suit.
        constexpr std::array<float,4> shoulderX{.16f,.85f,.12f,.84f},shoulderY{.73f,.73f,.72f,.70f};
        auto stitch=[&](float x1,float y1,float x2,float y2,float width,Vec3 color){
            const float scale=size*.013f;
            line(pixel(x,y,.021f),{x1*scale,-y1*scale,0},{x2*scale,-y2*scale,0},width*scale,color);
        };
        float sx=shoulderX[pilot],sy=shoulderY[pilot];
        const Vec3 braid=state.serviceRank>=2?Vec3{.78f,.62f,.32f}:Vec3{.66f,.73f,.74f};
        for(int row=0;row<state.serviceRank;++row){
            const float yy=sy+.034f*row;
            stitch(sx-.057f,yy+.027f,sx,yy,.006f,braid);
            stitch(sx,yy,sx+.057f,yy+.027f,.006f,braid);
        }
        // The campaign dress uniform gains gold shoulder piping. Match the slope of
        // the painted shoulder pad, mirrored for right-shoulder cels.
        if(state.serviceRank>=2){
            const float facing=sx<.5f?1.f:-1.f;
            stitch(sx-.046f*facing,sy-.046f,sx+.031f*facing,sy-.070f,.006f,braid);
            stitch(sx+.031f*facing,sy-.070f,sx+.079f*facing,sy-.053f,.006f,braid);
        }
        constexpr std::array<Vec3,3> ribbon{{{.70f,.25f,.25f},{.20f,.59f,.64f},{.53f,.38f,.67f}}};
        constexpr std::array<float,4> chest{.56f,.38f,.52f,.48f};
        int index=0;
        for(int f=0;f<3;++f)if(state.pilotHonours&(1u<<f)){
            const float rx=chest[pilot]+.044f*index++,ry=.875f;
            stitch(rx,ry,rx,ry+.036f,.017f,ribbon[f]);
            stitch(rx-.009f,ry,rx-.009f,ry+.036f,.0028f,{.88f,.75f,.54f});
            stitch(rx-.016f,ry-.004f,rx+.016f,ry-.004f,.0035f,{.73f,.66f,.50f});
            stitch(rx-.010f,ry+.040f,rx,ry+.055f,.005f,braid);
            stitch(rx,ry+.055f,rx+.010f,ry+.040f,.005f,braid);
        }
        return true;
    };
    auto panel=[&](const std::string& kind,float height=274.f){
        if(!optionalPicture("ui-panel-"+kind+"-"+variant,0,0,320,height,0))
            picture("mission-briefing-panel",0,0,320,height,0);
    };
    auto cardArt=[&]()->std::string{
        return state.generatedMissions()&&state.mission>=0&&state.mission<state.missionCount()?state.missionCards[state.mission].art:"";
    };
    if(state.screen==Screen::Loadout&&state.equipmentPanel){
        const auto &loadout=*state.equipmentPanel;
        const Vec3 dim{.41f,.61f,.64f},ink{.04f,.078f,.10f},focus{.14f,.17f,.14f};
        auto fill=[&](float x,float y,float w,float h,Vec3 color){
            box(multiply(pixel(x+w*.5f,y+h*.5f,.009f),scaling(w*.0065f,h*.0065f,.001f)),color);
        };
        auto stroke=[&](float x,float y,float xx,float yy,float width,Vec3 color,float depth=.015f){
            line(pixel(0,0,depth),{x*.013f,-y*.013f,0},{xx*.013f,-yy*.013f,0},width*.013f,color);
        };
        auto key=[&](const char *name,float x,float y,Vec3 color,bool trigger=false){
            constexpr int sides=12;
            for(int k=0;k<sides;++k){
                const float a=k*2*pi/sides,b=(k+1)*2*pi/sides;
                auto edge=[&](float angle){
                    const float dx=std::cos(angle),dy=std::sin(angle);
                    return trigger?Vec3{std::clamp(dx*12.f,-9.f,9.f),std::clamp(dy*8.f,-6.f,6.f),0}:
                        Vec3{dx*6.7f,dy*6.7f,0};
                };
                const auto from=edge(a),to=edge(b);
                stroke(x+from.x,y+from.y,x+to.x,y+to.y,1.f,color);
            }
            words(name,x-(trigger?3.7f:1.9f),y-2.f,.92f,color);
        };
        panel("equipment");
        words(loadout.title,13,22,1.45f,amber);
        for(size_t i=0;i<loadout.options.size();++i){
            const float spacing=std::min(76.f,296.f/float(loadout.options.size()));
            const float x=13+spacing*float(i);
            const bool selected=loadout.selectedOption==int(i);
            words(loadout.options[i],x,47,1.03f,selected?amber:dim);
            if(selected)stroke(x,55,x+std::min(57.f,spacing-6.f),55,1.f,amber);
        }
        optionalPicture(state.chassisPortrait,104,63,112,130,.011f);
        constexpr const char *controls[]={"LT","RT","Y","B","X","A"};
        constexpr const char *mounts[]={"LEFT ARM","RIGHT ARM","LEFT SHOULDER","RIGHT SHOULDER","LEFT HIP","RIGHT HIP"};
        constexpr float hardpointY[]={90,120,137};
        for(int i=0;i<MountCount;++i){
            const int slot=equipmentPanelOrder[i];
            const auto &card=loadout.slots[slot];
            const bool left=i%2==0,selected=loadout.selectedSlot==slot;
            const float x=left?12:214,y=57+47*float(i/2);
            const float edge=left?x+94:x,anchorX=left?134:186;
            const float anchorY=hardpointY[i/2];
            const Vec3 accent=selected?amber:(card.empty||card.linked?dim:green);
            fill(x,y,94,42,selected?focus:ink);
            stroke(x,y,x+94,y,selected?1.6f:1.f,accent);
            stroke(edge,y+18,anchorX,anchorY,selected?1.4f:.85f,accent);
            stroke(anchorX-2,anchorY,anchorX+2,anchorY,1.7f,accent);
            key(controls[slot],x+12,y+10,accent,slot<LeftShoulder);
            words(mounts[slot],x+25,y+13,.85f,accent);
            int lineNumber=0;
            for(const auto &part:missionLines(card.item,22))
                if(lineNumber<2)words(part,x+5,y+24+7.5f*lineNumber++,.96f,card.empty?dim:green);
            words(card.detail,x+5,y+39,.84f,card.detail=="DEPLETED"?amber:dim);
        }
        fill(12,201,296,48,ink);
        stroke(12,201,308,201,1.f,dim);
        words(loadout.inspection,16,213,1.16f,green);
        int detailRow=0;
        for(const auto &part:missionLines(loadout.detail,57))
            if(detailRow<2)words(part,16,224+8*detailRow++,.92f,dim);
        if(!loadout.action.empty()){
            if(loadout.actionAvailable)key("A",23,240,amber);
            words(loadout.action,loadout.actionAvailable?34:16,239,1.02f,amber);
        }
        auto modelIcon=state.equipmentModel;
        const bool alt=modelIcon.ends_with("_alt");
        if(alt)modelIcon.resize(modelIcon.size()-4);
        if(!modelIcon.empty())optionalPicture("icon-"+modelIcon+(alt?"-1":"-0"),267,205,37,37,.015f);
        auto status=missionLines(loadout.status,66);
        words(status.empty()?state.message:status.front(),12,262,.96f,status.empty()?dim:amber);
        return;
    }
    if(state.generatedMissions()&&state.screen!=Screen::Missions&&state.screen!=Screen::Playing){
        const std::string context=state.interfaceArt.empty()?"hangar":state.interfaceArt;
        const bool briefing=state.screen==Screen::Briefing;
        const std::string kind=briefing?"briefing":context=="trader"?"trader":context=="debrief"?"debrief":context=="defeat"?"defeat":context=="profile"?"profile":"equipment";
        panel(kind,briefing?342.f:274.f);
        words(state.applicationTitle,117,18,1.65f,amber);
        if(briefing){
            words(state.generatedFinale?title(state.mission):"MISSION "+std::to_string(state.mission+1)+" / "+title(state.mission),12,43,.95f,amber);
            bool illustrated=state.briefingHeader&&optionalPicture("briefing-header-frame",12,53,296,80,.008f);
            if(!illustrated)illustrated=optionalPicture(cardArt(),12,55,296,65,.008f);
            const auto& lines=state.menuLines.empty()?state.missionCards.at(state.mission).briefing:state.menuLines;
            float y=illustrated?139.f:65.f;
            bool opening=true;
            for(const auto& line:lines){
                for(const auto& wrapped:missionLines(line,57)){
                    if(y>316.f)break;
                    words(wrapped,12,y,opening?1.12f:.98f,opening?amber:green);
                    y+=opening?13.f:10.f;
                }
                if(opening)y+=7.f;
                opening=false;
            }
            words(state.message,12,326,.95f,amber);
        }else{
            int row=0;for(const auto& line:state.menuLines)for(const auto& wrapped:missionLines(line,58))
                if(row<17)words(wrapped,12,42+row++*12,.98f,line.starts_with(">")?amber:green);
            float artTop=std::max(176.f,38.f+row*12.f);
            float artHeight=std::min(72.f,251.f-artTop);
            if(context=="profile"){
                servicePortrait(177,112,131);
            }else if(!state.equipmentModel.empty()){
                auto iconModel=state.equipmentModel;bool alternate=iconModel.ends_with("_alt");
                if(alternate)iconModel.resize(iconModel.size()-4);
                bool illustrated=artHeight>=36&&optionalPicture("icon-"+iconModel+(alternate?"-1":"-0"),220,artTop,88,artHeight,.01f);
                if(!illustrated&&models.contains(state.equipmentModel))
                    modelPreview(state.equipmentModel,pixel(265,222,.08f),.45f,state.clock*.35f,state.equipmentHandling);
            }else if(artHeight>=36){
                bool portrait=context=="hangar"||context=="creation"||context=="profile"||context=="debrief";
                bool illustrated=portrait&&servicePortrait(308-artHeight,artTop,artHeight);
                if(portrait&&!illustrated)illustrated=optionalPicture(state.chassisPortrait,236,artTop,72,artHeight,.01f);
                if(!illustrated)optionalPicture("ui-scene-"+context+"-"+variant,200,artTop,108,artHeight,.01f);
            }
            words(state.message,12,260,.9f,amber);
        }
        return;
    }
    if(state.screen==Screen::Skirmish){
        picture("mission-briefing-panel",0,0,320,274,0);words("SKIRMISH SETUP",58,25,2,amber);
        auto entries=state.skirmishRows();
        for(size_t i=0;i<entries.size();++i){float y=44+i*11;words(entries[i],28,y,1.05f,int(i)==state.option?amber:green);if(int(i)==state.option){auto marker=pixel(17,y-1,.04f);line(marker,{-.045f,.035f,0},{0,0,0},.008f,amber);line(marker,{-.045f,-.035f,0},{0,0,0},.008f,amber);}}
        words("UP / DOWN: ROW    LEFT / RIGHT: CHANGE",12,211,1.f,green);
        words("ENTER / TRIGGER: SELECT    ESC / MENU: BACK",12,225,.95f,green);
        words(state.environmentMessage,12,239,.9f,amber);
        int row=0;const auto& status=state.networkMessage.empty()?state.resultMessage:state.networkMessage;
        for(const auto& line:missionLines(status,64)){if(row==2)break;words(line,12,256+row++*10,.95f,amber);}
        return;
    }
    if(state.screen==Screen::Debrief){
        if(state.skirmish){
            picture("mission-briefing-panel",0,0,320,274,0);words("SKIRMISH COMPLETE",35,28,2,amber);
            words("SEED "+std::to_string(state.skirmishSettings.seed),20,65,1.35f,green);
            int row=0;for(const auto& line:missionLines(state.prototypeBriefing,50)){if(row==6)break;words(line,20,102+row++*14,1.2f,green);}
            words(state.resultMessage,20,216,1.1f,amber);words("ENTER / MENU: SKIRMISH SETUP",20,250,1.2f,green);return;
        }
        int completed=std::clamp(state.resultMission,0,int(missions.size())-1);
        picture("mission-briefing-panel",0,0,320,274,0);
        words(state.cooperative?"TEAM VICTORY":"MISSION COMPLETE",38,22,2,amber);
        picture(thumb(completed),92,48,136,116);
        words(std::to_string(completed+1)+" / "+missions[completed].title,12,181,1.1f,green);
        words(state.resultMessage,12,205,1.15f,amber);
        words(state.saveMessage,12,226,1.05f,green);
        words(state.cooperative&&!state.host?"ENTER / MENU: WAIT FOR HOST":"ENTER / MENU: MISSION SELECTION",12,252,1.0f,green);
        if(state.cooperative){words(state.voiceMessage,12,278,1.0f,green);words(state.micEnabled?"TEAM RADIO ON - M: MUTE":"TEAM RADIO MUTED - M: UNMUTE",12,294,1.0f,amber);}
        return;
    }
    if(state.screen==Screen::Campaign||state.screen==Screen::Title||state.screen==Screen::Cooperative||state.screen==Screen::Join||state.screen==Screen::Connecting||state.screen==Screen::Disconnected||state.screen==Screen::Standby||state.screen==Screen::Ready||state.screen==Screen::Pause){
        picture("mission-briefing-panel",0,0,320,274,0);
        words(state.applicationTitle,55,25,2,amber);
        std::vector<std::string> entries;
        if(state.screen==Screen::Campaign){entries={"IRON SOLDIER","IRON SOLDIER 2"};if(state.pendingAction==MenuAction::Singleplayer)entries.push_back("SKIRMISH");}
        if(state.screen==Screen::Title)entries={"SINGLEPLAYER","COOPERATIVE","LOAD","EXIT"};
        if(state.screen==Screen::Cooperative)entries={"HOST LOBBY","JOIN BY CODE","BACK"};
        if(state.screen==Screen::Pause)entries={"RESUME",state.cooperative&&!state.host?"RESTART - HOST ONLY":"RESTART MISSION",state.cooperative&&!state.host?"ABORT - HOST ONLY":"ABORT MISSION","SELF DESTRUCT",state.micEnabled?"MICROPHONE: ON":"MICROPHONE: MUTED","SOUND: "+std::to_string(int(std::round(state.soundVolume*100)))+"%","MUSIC: "+std::to_string(int(std::round(state.musicVolume*100)))+"%","RESET COCKPIT POSITION / HEIGHT","LEAVE TO TITLE"};
        float menuStep=state.screen==Screen::Pause?11.f:17.f,menuSize=state.screen==Screen::Pause?1.05f:1.4f;
        for(size_t i=0;i<entries.size();++i){words(entries[i],32,57+i*menuStep,menuSize,int(i)==state.option?amber:green);if(int(i)==state.option){auto marker=pixel(20,56+i*menuStep,.04f);line(marker,{-.05f,.04f,0},{0,0,0},.01f,amber);line(marker,{-.05f,-.04f,0},{0,0,0},.01f,amber);}}

        if(state.screen==Screen::Join){words("LOBBY CODE",55,80,1.6f);words(state.joinCode,70,110,3,amber);words("^",70+state.codeCursor*12,126,3,amber);words("TYPE CODE OR USE STICK: LEFT/RIGHT SLOT",15,173,1.1f);words("UP/DOWN LETTER    ENTER/TRIGGER JOIN",15,188,1.1f);}
        if(state.screen==Screen::Connecting)words("CONNECTING TO LOBBY SERVER",20,90,1.5f);
        if(state.screen==Screen::Disconnected){words("LOBBY CONNECTION LOST",20,75,1.5f,amber);words("ESC / MENU: LEAVE TO TITLE",20,95,1.2f);}
        if(state.screen==Screen::Standby)words(state.missionChosen?"MISSION SELECTED - ENTER/TRIGGER: BRIEFING":"STANDBY - HOST IS SELECTING A MISSION",12,75,1.3f,amber);
        if(state.screen==Screen::Ready){words("READY - WAITING FOR ALL PLAYERS",15,65,1.4f,amber);words("ENTER/TRIGGER: CHANGE LOADOUT",15,85,1.2f);}
        if(state.screen==Screen::Pause){
            if(!state.skirmish)picture(thumb(state.mission),170,40,136,116);
            words("LEFT / RIGHT: ADJUST VOLUME",15,162,.9f,green);
            words(state.environmentMessage,15,173,.85f,amber);int row=0;
            if(state.skirmish||!state.prototypeBriefing.empty()){
                words(state.skirmish?"SKIRMISH / SEED "+std::to_string(state.skirmishSettings.seed):"GENERATED MISSION",15,187,1.1f,amber);
                for(auto& line:missionLines(state.prototypeBriefing,50)){if(row==4)break;words(line,15,201+row++*11,1.f,green);}
                words(state.skirmish?"RESTART: SAME MAP / ABORT: SETUP":"RESTART REPEATS SEED / ABORT EXITS",15,246,1.f,green);
            }else for(const auto& line:missions[state.mission].briefing)words(line,15,177+row++*9,1.65f,green);
            words(state.audioMessage,8,263,.9f,amber);
        }
        else if(state.cooperative){words("LOBBY",20,139,1.2f,green);words(state.lobbyCode,20,151,state.host?3.5f:2.f,amber);int row=0;for(auto& line:state.roster)words(line,20,177+row++*16,1.2f);}
        words(state.networkMessage,8,278,1.f,amber);words(state.voiceMessage,8,292,1.f,green);
        if(state.cooperative)words(state.micEnabled?"TEAM RADIO ON - M: MUTE":"TEAM RADIO MUTED - M: UNMUTE",8,306,1.f,amber);
        if(!state.skirmish)words(state.saveMessage,8,state.cooperative?320:306,1.f,amber);return;
    }
    if(state.screen==Screen::Missions&&state.generatedMissions()){
        panel("contracts");
        words(state.generatedFinale?"FINAL OPERATION":"MISSIONS",12,19,1.6f,amber);
        // Each row is a real contract selection; decorative thumbnail cubes hid
        // the contract details and suggested controls that did not exist.
        for(int i=0;i<state.missionCount();++i){
            bool selected=i==state.mission;std::string label=(selected?"> ":"  ")+std::to_string(i+1)+" "+state.missionCards[i].title+(state.generatedCompleted&(1u<<i)?" / CLEARED":"");
            words(label,12,46+i*15,1.05f,selected?amber:green);
        }
        optionalPicture(cardArt(),218,185,90,44,.01f);
        int row=0;for(const auto &line:missionLines(state.missionCards.at(state.mission).summary,37))if(row<4)words(line,12,185+row++*11,.88f,amber);
        words("STICK: MOVE   A: BRIEFING   B: BACK",12,239,.88f,green);
        if(!state.resultMessage.empty())words(state.resultMessage,12,267,.88f,amber);
        return;
    }
    if(state.screen==Screen::Missions) {
        picture("mission-selection-panel",0,0,320,274,0);
        const int positions[16][2]={{74,27},{120,27},{166,27},{212,27},{254,64},{254,103},{254,142},{254,181},{212,218},{166,218},{120,218},{74,218},{32,181},{32,142},{32,103},{32,64}};
        for(int i=0;i<state.missionCount();++i){int x=positions[i%16][0],y=positions[i%16][1];if(state.game){int side=i/5,n=i%5; x=side==0?55+n*42:side==1?264:side==2?223-n*42:22;y=side==0?27:side==1?57+n*35:side==2?232:197-n*35;}if(state.generatedMissions()){x=20+(i%3)*47;y=40+(i/3)*47;words(std::to_string(i+1),x+13,y+34,.95f,amber);}picture(thumb(i),x,y,34,29,.01f,state.missionAvailable(i)?Vec3{1,1,1}:Vec3{.22f,.22f,.22f});if(i==state.mission){auto p=pixel(x-1,y-1,.016f);line(p,{0,0,0},{.468f,0,0},.008f,amber);line(p,{0,-.403f,0},{.468f,-.403f,0},.008f,amber);}}
        Matrix cube=multiply(base,multiply(translation(state.generatedMissions()?1.02f:0,.04f,-2.88f),multiply(rotation(state.rotation+.35f+state.clock*.45f,.17f),scaling(state.generatedMissions()?.62f:.80f,state.generatedMissions()?.62f:.80f,state.generatedMissions()?.62f:.80f))));
        // The selected original mission image wraps a real continuously rotating cube.
        for(int face=0;face<4;++face){auto side=multiply(cube,multiply(rotation(face*pi/2),translation(-.5f,.5f,.5f)));art(thumb(state.mission),multiply(side,scaling(1,-1,1)));}
        art(thumb(state.mission),multiply(cube,multiply(rotation(0,pi/2),multiply(translation(-.5f,.5f,.5f),scaling(1,-1,1)))));
        words(std::to_string(state.mission+1)+" / "+title(state.mission),state.generatedMissions()?12:76,196,.95f,amber);
        if(state.generatedMissions()){int row=0;for(const auto& line:missionLines(state.missionCards.at(state.mission).summary,59)){if(row<4)words(line,12,211+row++*11,.95f,green);}}
        words(state.generatedMissions()?state.applicationTitle:state.sandbox?"SANDBOX":state.campaign.finished()?"CAMPAIGN COMPLETE":(state.game?"IRON SOLDIER 2":"SELECT MISSION"),state.generatedMissions()?124:105,state.generatedMissions()?-14:-8,1.4f);
        words(!state.missionAvailable(state.mission)?"LOCKED":(!state.generatedMissions()&&(state.campaign.completed&(1u<<state.mission)))?"COMPLETE":"AVAILABLE",state.generatedMissions()?194:120,178,1.1f,amber);
        if(!state.missionAvailable(state.mission))words((state.game||state.mission<12)?"COMPLETE THE PREVIOUS GROUP OF FOUR":"COMPLETE ALL PREVIOUS MISSIONS",15,268,1.f,amber);
        else words(state.resultMessage,15,268,1.f,amber);words(state.generatedMissions()?"LEFT STICK: SELECT / RIGHT TRIGGER: BRIEFING":"STICK / ARROWS: SELECT    TRIGGER / ENTER: BRIEFING",0,286,1.05f);
    } else if(state.screen==Screen::Briefing) {
        picture("mission-briefing-panel",0,0,320,274,0);
        if(state.skirmish){
            words("SKIRMISH BRIEFING",42,29,2,amber);words("SEED "+std::to_string(state.skirmishSettings.seed),20,65,1.3f);
            words("MAP "+std::to_string(state.skirmishSettings.cells)+" X "+std::to_string(state.skirmishSettings.cells)+" / "+state.skirmishSettings.terrain+" / "+state.skirmishSettings.water,20,87,1.1f,green);
            words(state.environmentMessage,12,103,.9f,amber);
            int row=0;for(const auto& line:missionLines(state.prototypeBriefing,50)){if(row==8)break;words(line,20,119+row++*14,1.25f,green);}
            words("ALL EQUIPMENT AVAILABLE IN ARMING",20,253,1.05f,amber);
        }else{
            picture(thumb(state.mission),170,40,136,116);
            words("MISSION "+std::to_string(state.mission+1),20,56,1.3f,amber);
            words(missions[state.mission].title,20,77,.9f);
            words("RESISTANCE",20,105,1.2f);words("OPERATIONS",20,121,1.2f);
            int row=0;for(const auto& line:missions[state.mission].briefing)words(line,15,170+row++*9,1.9f,green);
        }
        words("ENTER / TRIGGER: ARM     ESC / MENU: BACK",0,286,1.15f);
    } else {
        picture("weapon-loadout-panel",0,0,320,266,0);
        const auto selected=Weapon(state.weapon);
        words(weaponInfo(selected).name,155,41,1.12f,amber);
        words(state.message,0,279,.95f,amber);
        int page=state.weapon>0?(state.weapon-1)/8:0;
        for(int k=0;k<8;++k){int w=page*8+k+1;if(w>=state.weaponCount())break;int x=15+(k%2)*64,y=45+(k/2)*39;words(weaponInfo(Weapon(w)).shortName,x,y,1.15f,!state.weaponAvailable(Weapon(w))?Vec3{.35f,.4f,.4f}:w==state.weapon?amber:green);if(w==state.weapon){auto p=pixel(x-2,y-5,.03f);line(p,{0,0,0},{.663f,0,0},.007f,amber);}modelPreview(weaponModel(Weapon(w)),pixel(39+(k%2)*64,61+(k/2)*39,.16f),.37f,state.clock*.7f+.6f);}
        if(state.weaponCount()>9)words(std::to_string(page+1)+" / 2",103,214,1.1f,green);
        if(state.weapon>0)modelPreview(weaponModel(selected),pixel(230,65,.2f),.44f,state.clock*.5f+.5f);
        if(selected==Weapon::AutoGrenade&&state.weaponAvailable(selected))words("REQUIRES BOTH HIPS",150,91,.9f,amber);
        if(!state.weaponAvailable(selected))for(int i=0;i<state.campaign.rules.count();++i)if(state.campaign.rules.rewards[i]==state.weapon)words("LOCKED - COMPLETE MISSION "+std::to_string(i+1),150,91,.95f,amber);
        words("REMOVE",22,214,1.3f,state.weapon==0?amber:green);
        static const char* mounts[]={"LT / LEFT HAND","RT / RIGHT HAND","Y / LEFT SHOULDER","B / RIGHT SHOULDER","X / LEFT HIP","A / RIGHT HIP"};
        for(int i=0;i<6;++i){float y=105+i*20;bool reserved=i>=LeftHip&&state.loadout[i==LeftHip?RightHip:LeftHip]==Weapon::AutoGrenade;words(mounts[i],155,y,.95f,state.weaponAvailable(selected)&&compatible(selected,i)?amber:Vec3{.35f,.4f,.4f});words(reserved?"RESERVED":weaponInfo(state.loadout[i]).shortName,168,y+9,1.2f);}
        words("STICK / ARROWS: WEAPON",0,-9,1.3f);words(state.cooperative?"PRESS MOUNT TO EQUIP    ENTER / MENU: READY":"PRESS MOUNT TO EQUIP    ENTER / MENU: DEPLOY",0,293,1.05f);
        words("GRIP + MOUNT: REMOVE    ESC / STICK DOWN: BACK",0,304,1.05f);
    }
    if(state.cooperative){
        if(state.host){
            words("LOBBY",0,-23,1.f,green);words(state.lobbyCode,26,-24,3.5f,amber);
            words(state.difficulty==World::Difficulty::Hard?"HARD":"NORMAL",126,-20,1.2f,green);
        }else words("LOBBY "+state.lobbyCode+" / "+(state.difficulty==World::Difficulty::Hard?"HARD":"NORMAL"),0,-22,1.1f,amber);
        words(state.voiceMessage,194,-33,.8f,green);
        words(state.micEnabled?"RADIO ON - M: MUTE":"RADIO MUTED - M: UNMUTE",194,-22,.8f,amber);
    }
    if(!state.skirmish&&!state.saveMessage.empty())words(state.saveMessage,0,318,1.2f,amber);
}
void Renderer::sky(const World& w,const Matrix& projection,const Matrix& camera){
    const auto settings=w.activeEnvironment();
    const bool night=environmentView.night;
    const Vec3 eye{camera[12],camera[13],camera[14]};
    const auto kind=w.skyKind!=SkyKind::Legacy&&w.indoors(eye)?SkyKind::Interior:w.skyKind;
    Vec3 rim=environmentView.sky,zenith=rim,cloud{};
    std::array<float,4> weather{.49f,.77f,1.f,0.f};
    if(kind==SkyKind::Legacy){
        if(!settings.authoredSky&&settings.weather==Weather::Clear&&!night){
            char name[40];if(w.game)std::snprintf(name,sizeof name,"is2-horizon-%02d",w.horizon+1);else std::snprintf(name,sizeof name,"horizon-%02d",std::clamp(w.horizon,0,5)+1);
            auto found=textures.find(name);if(found!=textures.end()&&found->second.hasSky)rim=found->second.skyTop;
            zenith={rim.x*.72f,rim.y*.82f,rim.z*.95f};
        }
    }else{
        switch(kind){
        case SkyKind::Desert:
            rim={.70f,.54f,.36f}; zenith={.22f,.42f,.59f}; cloud={.79f,.65f,.48f};weather[0]=.59f;break;
        case SkyKind::Space:
            rim={.012f,.018f,.041f};zenith={.006f,.010f,.026f};cloud={.17f,.10f,.24f};break;
        case SkyKind::Underwater:
            rim={.021f,.092f,.13f};zenith={.11f,.37f,.41f};cloud={.22f,.44f,.43f};
            if(night){rim=rim*.53f;zenith=zenith*.58f;cloud=cloud*.48f;}
            break;
        case SkyKind::Urban:
            rim={.47f,.50f,.56f}; zenith={.16f,.28f,.43f}; cloud={.62f,.66f,.72f};weather[0]=.48f;break;
        case SkyKind::Volcanic:
            rim={.55f,.26f,.16f};zenith={.18f,.16f,.23f};cloud={.36f,.29f,.31f};weather[0]=.39f;break;
        case SkyKind::Wilderness:
            rim={.55f,.70f,.69f}; zenith={.13f,.38f,.65f}; cloud={.82f,.84f,.81f};break;
        case SkyKind::Polar:
            rim={.62f,.76f,.82f};zenith={.23f,.40f,.62f};cloud={.75f,.82f,.89f};weather[0]=.53f;break;
        case SkyKind::Lunar:
            rim={.026f,.029f,.052f};zenith={.007f,.010f,.024f};cloud={.11f,.14f,.21f};break;
        case SkyKind::Interior:
            rim={.022f,.029f,.035f};zenith={.010f,.015f,.022f};break;
        case SkyKind::Legacy:break;
        }
        const bool atmosphere=kind==SkyKind::Desert||kind==SkyKind::Urban||kind==SkyKind::Volcanic||
                              kind==SkyKind::Wilderness||kind==SkyKind::Polar;
        if(atmosphere){
            if(night){
                const Vec3 cityGlow=kind==SkyKind::Urban?Vec3{.11f,.07f,.043f}:Vec3{.04f,.06f,.09f};
                rim=rim*.10f+cityGlow;zenith=zenith*.13f;cloud=cloud*.18f;
                if(kind==SkyKind::Volcanic)rim=rim+Vec3{.14f,.043f,.017f};
            }
            switch(settings.weather){
            case Weather::Clear:break;
            case Weather::Fog:weather={.39f,.68f,.14f,0};break;
            case Weather::Rain:weather={.28f,.94f,.12f,0};break;
            case Weather::Storm:weather={.24f,.99f,.035f,weatherLightningActive(settings,w.weatherTime())?1.f:0.f};break;
            case Weather::Snowstorm:weather={.22f,.96f,.055f,0};break;
            case Weather::Sandstorm:weather={.33f,.76f,.045f,0};break;
            case Weather::IonStorm:weather={.30f,.91f,.09f,0};break;
            case Weather::CorruptionSporeBurst:weather={.43f,.72f,.10f,0};break;
            case Weather::FireStorm:weather={.24f,.88f,.07f,0};break;
            case Weather::MeteorShower:weather={.50f,.44f,.28f,0};break;
            case Weather::MagneticBurst:weather={.56f,.60f,.17f,0};break;
            case Weather::AshCloud:weather={.19f,.98f,.04f,0};break;
            case Weather::RadioactiveWind:weather={.38f,.87f,.08f,0};break;
            }
            if(settings.weather!=Weather::Clear){
                const float haze=settings.weather==Weather::Fog||settings.weather==Weather::AshCloud?.76f:
                                 settings.weather==Weather::MeteorShower?.22f:.67f;
                const auto fog=environmentView.fog;
                rim=rim*(1-haze)+fog*haze;
                zenith=zenith*(1-haze)+fog*(haze*.84f);
                cloud=cloud*(1-haze)+fog*(haze*.96f);
            }
        }
    }
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_BLEND);
    glUseProgram(skyProgram);glBindVertexArray(skyVao);glUniformMatrix4fv(skyCamera,1,GL_FALSE,camera.data());
    glUniform4f(skyLens,projection[0],projection[5],projection[8],projection[9]);glUniform3f(skyRim,rim.x,rim.y,rim.z);glUniform3f(skyZenith,zenith.x,zenith.y,zenith.z);
    glUniform3f(skyCloud,cloud.x,cloud.y,cloud.z);
    glUniform4f(skyParams,float(kind),night?1.f:0.f,std::fmod(w.elapsed+w.rpgPoseLead,18000.f),float(w.prototypeSeed%4093));
    glUniform4fv(skyWeather,1,weather.data());glDrawArrays(GL_TRIANGLES,0,3);
    glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);glUseProgram(program);glBindVertexArray(vao);
}
void Renderer::horizon(const World& w) {
    if(w.skyKind!=SkyKind::Legacy||w.activeEnvironment().authoredSky)return;
    glDepthMask(GL_FALSE);
    for(int k=0;k<12;++k){float a=k*2*pi/12;
        auto center=multiply(translation(w.position.x,0,w.position.z),rotation(a));
        char name[40];if(w.game)std::snprintf(name,sizeof name,"is2-horizon-%02d",w.horizon+1);else std::snprintf(name,sizeof name,"horizon-%02d",std::clamp(w.horizon,0,5)+1);
        art(name,multiply(center,multiply(translation(-107,36,-390),scaling(214,-36,1))));
    }
    glDepthMask(GL_TRUE);
}
}
