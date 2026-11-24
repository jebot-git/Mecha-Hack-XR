#include "render.hpp"
#include "presentation.hpp"
#include "searchlight.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <algorithm>
namespace iron {
using namespace avp;
static GLuint compile(GLenum type,const char* source) {
    GLuint s=glCreateShader(type);ironShaderSource(s,source);glCompileShader(s);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[2048];glGetShaderInfoLog(s,sizeof log,nullptr,log);glDeleteShader(s);throw std::runtime_error(log);}return s;
}
static std::vector<float> readCockpitMesh(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)throw std::runtime_error("Missing authored cockpit: "+path.string()+". See docs/COCKPIT.md for Blender export.");
    auto bytes=input.tellg();input.seekg(0);
    auto u32=[&](){unsigned char b[4];if(!input.read(reinterpret_cast<char*>(b),4))throw std::runtime_error("Truncated cockpit mesh");return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);};
    char magic[4];input.read(magic,4);uint32_t version=u32(),count=u32();
    if(std::memcmp(magic,"ISCP",4)||version!=1||count==0||count%3||count>300000||bytes!=std::streamoff(12+count*40ull))throw std::runtime_error("Invalid cockpit mesh header/length");
    std::vector<float> data(count*10);
    for(auto& f:data){f=std::bit_cast<float>(u32());if(!std::isfinite(f))throw std::runtime_error("Non-finite cockpit vertex");}
    for(size_t k=0;k<data.size();k+=10) {
        for(int c=0;c<3;++c)if(std::abs(data[k+c])>10)throw std::runtime_error("Cockpit outside physical size budget");
        float n=std::hypot(data[k+3],data[k+4],data[k+5]);if(n<.9f||n>1.1f)throw std::runtime_error("Invalid cockpit normal");
        for(int c=6;c<10;++c)if(data[k+c]<0||data[k+c]>1)throw std::runtime_error("Invalid cockpit material");
    }
    return data;
}
Matrix Renderer::Surface::fit(float designWidth,float designHeight) const {
    const float scale=std::min(width/designWidth,height/designHeight);
    return multiply(pose,scaling(scale,scale,scale));
}
Renderer::Interior Renderer::loadInterior(const std::filesystem::path& file){
    Interior result;
    result.mesh.vertices=readCockpitMesh(file);
    nlohmann::json spec;
    std::ifstream metadata(file.parent_path()/(file.stem().string()+".json"));
    metadata>>spec;
    result.mesh.hullVertices=spec.at("hull_vertices").get<int>();
    if(spec.at("schema")!=1||result.mesh.hullVertices<=0||result.mesh.hullVertices%3||
       result.mesh.hullVertices>=result.mesh.count()||spec.at("ammo").size()!=MountCount)
        throw std::runtime_error("Invalid cockpit interior layout");
    auto vector=[](const auto& json){
        const auto a=json.template get<std::array<float,3>>();
        for(float n:a)if(!std::isfinite(n)||std::abs(n)>10)throw std::runtime_error("Invalid interior coordinate");
        return Vec3{a[0],a[1],a[2]};
    };
    auto surface=[&](const auto& j){
        Surface s;
        s.hatch=j.value("hatch",false);
        const auto style=j.value("style",std::string{});
        if(style=="dial")s.style=Surface::Style::Dial;
        else if(style=="attitude")s.style=Surface::Style::Attitude;
        else if(style=="servo")s.style=Surface::Style::Servo;
        s.width=j.at("width");s.height=j.at("height");
        const auto p=vector(j.at("position")),r=vector(j.at("right")),u=vector(j.at("up")),n=cross(r,u);
        if(!(s.width>0&&s.width<=5&&s.height>0&&s.height<=5)||
           std::abs(length(r)-1)>.011f||std::abs(length(u)-1)>.011f||std::abs(dot(r,u))>.011f)
            throw std::runtime_error("Invalid interior instrument surface");
        s.pose={r.x,r.y,r.z,0,u.x,u.y,u.z,0,n.x,n.y,n.z,0,p.x,p.y,p.z,1};
        return s;
    };
    result.overview=surface(spec.at("overview"));result.radar=surface(spec.at("radar"));
    result.heading=surface(spec.at("heading"));result.torso=surface(spec.at("torso"));
    result.health=surface(spec.at("health"));result.reactor=surface(spec.at("reactor"));
    if(spec.contains("gauges")){
        using Signal=Interior::Gauge::Signal;
        for(const auto& gauge:spec.at("gauges")){
            const std::string name=gauge.at("signal");
            if(name!="heat"&&name!="hull"&&name!="ap")throw std::runtime_error("Unknown auxiliary gauge signal");
            result.gauges.push_back({name=="heat"?Signal::Heat:name=="hull"?Signal::Hull:Signal::Plating,surface(gauge)});
        }
    }
    if(spec.contains("waveform"))result.waveform=surface(spec.at("waveform"));
    if(spec.contains("servo"))result.servo=surface(spec.at("servo"));
    for(size_t i=0;i<MountCount;++i)result.ammo[i]=surface(spec.at("ammo").at(i));
    if(spec.contains("glass")){
        constexpr std::array<std::array<float,2>,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
        for(const auto& pane:spec.at("glass")){
            if(pane.size()!=4)throw std::runtime_error("Invalid cockpit glazing quad");
            std::array<Vec3,4> corners;
            for(size_t i=0;i<4;++i)corners[i]=vector(pane.at(i));
            const float width=length(corners[1]-corners[0]),height=length(corners[3]-corners[0]);
            for(const auto& triangle:std::array<std::array<int,3>,2>{{{0,1,2},{0,2,3}}}){
                auto normal=cross(corners[triangle[1]]-corners[triangle[0]],corners[triangle[2]]-corners[triangle[0]]);
                const float magnitude=length(normal);
                if(magnitude<.000001f)continue;
                normal=normal*(1.f/magnitude);
                for(int i:triangle)result.glass.push_back({corners[i],normal,uv[i],{uv[i][0]*width,uv[i][1]*height}});
            }
        }
    }
    return result;
}
static bool onHatch(const float *triangle){
    return (triangle[1]+triangle[11]+triangle[21])/3>.45f;
}
void Renderer::loadCockpit(const std::filesystem::path& path) {
    auto data=readCockpitMesh(path/"cockpit.mesh");
    const size_t count=data.size()/10;
    std::ifstream manifestFile(path/"mesh-manifest.json");nlohmann::json manifest;manifestFile>>manifest;
    std::array<std::vector<float>,5> lenses;
    std::vector<float> reactorLens;
    unsigned lamp=0;std::vector<bool> isLens(count);
    for(const auto &object:manifest.at("objects")){
        const auto name=object.at("object").get<std::string>();
        const bool reactor=name=="Central red lamp";
        if(!reactor&&!name.starts_with("Status lamp lens"))continue;
        size_t first=object.at("first_vertex"),n=object.at("vertex_count");
        if((!reactor&&lamp>=5)||first+n>count||first%3||n%3)throw std::runtime_error("Invalid cockpit lamp geometry");
        auto &lens=reactor?reactorLens:lenses[lamp++];
        lens.assign(data.begin()+first*10,data.begin()+(first+n)*10);
        std::fill(isLens.begin()+first,isLens.begin()+first+n,true);
    }
    if(lamp!=5)throw std::runtime_error("Cockpit requires five health lamps");
    std::vector<float> hull,canopy;
    for(size_t k=0;k<data.size();k+=30){if(isLens[k/10])continue;auto& part=onHatch(data.data()+k)?canopy:hull;part.insert(part.end(),data.begin()+k,data.begin()+k+30);}
    cockpitHullVertices=GLsizei(hull.size()/10);hull.insert(hull.end(),canopy.begin(),canopy.end());cockpitVertices=GLsizei(hull.size()/10);
    for(unsigned i=0;i<5;++i){
        const auto &lens=lenses[i];
        lampRanges[i]={GLint(hull.size()/10),GLsizei(lens.size()/10)};
        float left=10,bottom=10,right=-10,top=-10;
        for(size_t k=0;k<lens.size();k+=10){
            left=std::min(left,lens[k]);right=std::max(right,lens[k]);
            bottom=std::min(bottom,lens[k+1]);top=std::max(top,lens[k+1]);
        }
        if(!(left<right&&bottom<top))throw std::runtime_error("Invalid cockpit status lamp bounds");
        lampBounds[i]={left,bottom,right-left,top-bottom};
        hull.insert(hull.end(),lens.begin(),lens.end());
    }
    reactorLampRange={GLint(hull.size()/10),GLsizei(reactorLens.size()/10)};
    hull.insert(hull.end(),reactorLens.begin(),reactorLens.end());data.swap(hull);
    std::ifstream layoutFile(path/"layout.json");nlohmann::json layout;layoutFile>>layout;
    if(layout.at("schema")!=1||layout.at("ammo_in_overview")!=false||layout.at("ammo").size()!=MountCount)throw std::runtime_error("Invalid cockpit instrument layout");
    auto position=[](const auto& value){auto a=value.template get<std::array<float,3>>();for(float f:a)if(!std::isfinite(f)||std::abs(f)>10)throw std::runtime_error("Invalid instrument position");return Vec3{a[0],a[1],a[2]};};
    std::array<bool,MountCount> seen{};
    for(const auto& value:layout.at("ammo")){
        int m=value.at("mount");if(m<0||m>=MountCount||seen[m])throw std::runtime_error("Duplicate or invalid ammo mount");seen[m]=true;
        ammoReadouts[m]={position(value.at("position")),value.at("size").get<float>()};
        if(!std::isfinite(ammoReadouts[m].size)||ammoReadouts[m].size<=0||ammoReadouts[m].size>.05f)throw std::runtime_error("Invalid readout scale");
    }
    overview=position(layout.at("overview"));radar=position(layout.at("radar"));torso=position(layout.at("torso"));heading=position(layout.at("heading"));
    auto vertex=compile(GL_VERTEX_SHADER,R"(#version 330 core
layout(location=0) in vec3 p;layout(location=1) in vec3 normal;layout(location=2) in vec3 color;layout(location=3) in float emission;layout(location=4) in vec2 uv;layout(location=5) in float textured;
uniform mat4 transform;uniform mat4 worldPose;out vec3 worldPoint;out vec3 N;out vec3 P;out vec3 C;out float E;out float eyeDepth;out vec2 UV;flat out float T;
uniform bool weaponPass;uniform mat4 weaponPose;uniform vec2 weaponStretch;
void main(){
    vec3 local=p;N=normal;
    if(weaponPass){
        local=(weaponPose*vec4(p,1)).xyz;N=mat3(weaponPose)*normal;
        if(local.z < -weaponStretch.x){
            local.z+=min(local.z+weaponStretch.x,0.0)*weaponStretch.y;
            N.z/=1.0+weaponStretch.y;
        }
        N=mat3(worldPose)*N;
    }
    worldPoint=(worldPose*vec4(local,1)).xyz;gl_Position=transform*vec4(local,1);
    eyeDepth=gl_Position.w;P=local;C=color;E=emission;UV=uv;T=textured;
})");
    auto fragment=compile(GL_FRAGMENT_SHADER,R"(#version 330 core
in vec3 N;in vec3 P;in vec3 C;in float E;in vec2 UV;flat in float T;uniform sampler2D psxAtlas;uniform vec3 eye;uniform bool bakedLighting;uniform vec4 lampColor;uniform float instrumentPower[5];uniform float powerRail;in float eyeDepth;uniform vec4 envFog;uniform vec3 envTint;uniform float envGain;out vec4 color;
uniform vec4 lampBounds;uniform vec2 lampFill;
uniform bool weaponPass;uniform float weaponGlow;
uniform vec4 lampOriginRange;uniform vec4 lampAxisBase;in vec3 worldPoint;
float searchlightTransmission(){float density=envFog.a;
if(lampOriginRange.w>0.0){vec3 delta=worldPoint-lampOriginRange.xyz;float distance=length(delta);
if(distance<lampOriginRange.w&&dot(delta/max(distance,.001),lampAxisBase.xyz)>.9063078)density*=lampAxisBase.w/lampOriginRange.w;}
return floor(clamp(1.0-density*max(0.0,eyeDepth),0.0,1.0)*255.0)/255.0;}
void main(){
if(lampColor.a>0.0){
    vec3 c=lampColor.rgb;
    if(lampColor.a>1.5){
        vec2 cell=(P.xy-lampBounds.xy)/lampBounds.zw;
        if(cell.y>.54)c=cell.x<=lampFill.x&&lampFill.x>0.0?vec3(.015,.32,1.0):vec3(.004,.011,.024);
        else if(cell.y<.46)c=1.0-cell.x<=lampFill.y&&lampFill.y>0.0?c:vec3(.017,.005,.004);
        else c=vec3(.001);
    }
    color=vec4(pow(clamp(c*powerRail,0.0,1.0),vec3(1./2.2)),1);
    return;
}
vec3 n=normalize(N);if(!weaponPass&&!gl_FrontFacing)n=-n;vec3 l=normalize(vec3(-.3,1,.6));
float diffuse=.24+.65*max(dot(n,l),0.0)+.17*max(dot(n,normalize(vec3(.6,.1,-.4))),0.0);
vec3 h=normalize(l+normalize(eye-P));float spec=pow(max(dot(n,h),0.0),38.0)*.20;
vec3 material=C;float emissionMask=0.;
if(T>.5){
    vec4 texel=texture(psxAtlas,UV);
    material*=pow(texel.rgb,vec3(2.2));
    if(!bakedLighting)emissionMask=texel.a;
}
if(weaponPass){
    vec3 lit=pow(clamp(material*diffuse+vec3(spec),0.0,1.0),vec3(1./2.2))*envTint*envGain;
    vec3 glow=pow(1.0-exp(-material*E*weaponGlow),vec3(1./2.2));
    color=vec4(mix(envFog.rgb,max(lit,glow),searchlightTransmission()),1);return;
}
vec3 c=bakedLighting?material:mix(material*diffuse+vec3(spec),material,E);
int module=P.y>0.1?(P.x<0.0?0:1):(P.x<-.2?2:(P.x>.3?3:4));
if(E>.02)c*=instrumentPower[module];
vec3 lit=pow(clamp(c,0.0,1.0),vec3(1./2.2))*envTint*envGain;
if(emissionMask>0.){
    float peak=max(.02,max(material.r,max(material.g,material.b)));
    lit=pow(pow(max(lit,vec3(0)),vec3(2.2))+material*(.85*emissionMask/peak),vec3(1./2.2));
}
color=vec4(mix(envFog.rgb,lit,searchlightTransmission()),1.);})");
    cockpitProgram=glCreateProgram();glAttachShader(cockpitProgram,vertex);glAttachShader(cockpitProgram,fragment);glLinkProgram(cockpitProgram);
    glDeleteShader(vertex);glDeleteShader(fragment);GLint ok=0;glGetProgramiv(cockpitProgram,GL_LINK_STATUS,&ok);if(!ok)throw std::runtime_error("Cockpit shader link failed");
    cockpitTransform=glGetUniformLocation(cockpitProgram,"transform");cockpitEye=glGetUniformLocation(cockpitProgram,"eye");
    weaponPass=glGetUniformLocation(cockpitProgram,"weaponPass");weaponPose=glGetUniformLocation(cockpitProgram,"weaponPose");
    weaponStretch=glGetUniformLocation(cockpitProgram,"weaponStretch");weaponGlow=glGetUniformLocation(cockpitProgram,"weaponGlow");
    cockpitPower=glGetUniformLocation(cockpitProgram,"instrumentPower");cockpitPowerRail=glGetUniformLocation(cockpitProgram,"powerRail");
    cockpitLamp=glGetUniformLocation(cockpitProgram,"lampColor");
    cockpitLampBounds=glGetUniformLocation(cockpitProgram,"lampBounds");
    cockpitLampFill=glGetUniformLocation(cockpitProgram,"lampFill");
    cockpitBaked=glGetUniformLocation(cockpitProgram,"bakedLighting");
    glGenVertexArrays(1,&cockpitVao);glBindVertexArray(cockpitVao);glGenBuffers(1,&cockpitVbo);glBindBuffer(GL_ARRAY_BUFFER,cockpitVbo);
    cockpitMaterials=std::move(data);
    size_t capacity=cockpitMaterials.size();
    for(size_t i=0;i<rpg::chassisIds.size();++i){
        const auto interiorFile=path/"interiors"/(std::string(rpg::chassisIds[i])+".mesh");
        if(std::filesystem::exists(interiorFile)){
            try{
                interiors[i]=loadInterior(interiorFile);
                capacity=std::max(capacity,interiors[i].mesh.vertices.size());
                continue;
            }catch(const std::exception& ex){
                std::cerr<<"Skipping cockpit interior "<<interiorFile<<": "<<ex.what()<<"\n";
            }
        }
        const auto file=path/"variants"/(std::string(rpg::chassisIds[i])+".mesh");
        if(!std::filesystem::exists(file))continue;
        auto source=readCockpitMesh(file);
        auto &trim=cockpitTrims[i];
        trim.vertices.reserve(source.size());
        for(bool hatch:{false,true}){
            for(size_t k=0;k<source.size();k+=30)
                if(onHatch(source.data()+k)==hatch)
                    trim.vertices.insert(trim.vertices.end(),source.data()+k,source.data()+k+30);
            if(!hatch)trim.hullVertices=trim.count();
        }
        capacity=std::max(capacity,cockpitMaterials.size()+source.size());
    }
    glBufferData(GL_ARRAY_BUFFER,capacity*sizeof(float),nullptr,GL_DYNAMIC_DRAW);
    for(int attribute=0;attribute<4;++attribute){glEnableVertexAttribArray(attribute);glVertexAttribPointer(attribute,attribute==3?1:3,GL_FLOAT,GL_FALSE,10*sizeof(float),reinterpret_cast<void*>(size_t(attribute*3*sizeof(float))));}
    std::cout<<"Loaded Blender cockpit: "<<count/3<<" triangles, six live ammo readouts\n";
}
void Renderer::prepareCockpitLighting(const World& w){
    const size_t chassis=w.rpgPrototype&&size_t(w.rpgChassis)<rpg::chassisIds.size()
        ?size_t(w.rpgChassis):rpg::chassisIds.size();
    const auto lighting=cockpitLighting(w);
    if(cockpitBake&&*cockpitBake==lighting&&activeCockpit==chassis)return;
    const bool changed=activeCockpit!=chassis;
    activeCockpit=chassis;
    const auto* interior=activeInterior();
    auto baked=bakeCockpitVertices(interior?interior->mesh.vertices:cockpitMaterials,lighting);
    // Insert chassis hardware at the hull/hatch boundary. Both eyes use the
    // same upload and the hull and ejecting canopy keep their original draws.
    const auto& trim=cockpitTrims[chassis];
    if(!trim.vertices.empty()){
        auto hardware=bakeCockpitVertices(trim.vertices,lighting);
        baked.insert(baked.begin()+size_t(cockpitHullVertices)*10,hardware.begin(),hardware.end());
    }
    glBindBuffer(GL_ARRAY_BUFFER,cockpitVbo);
    glBufferSubData(GL_ARRAY_BUFFER,0,baked.size()*sizeof(float),baked.data());
    if(changed){
        const auto& glass=interior&&!interior->glass.empty()?interior->glass:classicGlass;
        glBindBuffer(GL_ARRAY_BUFFER,glassVbo);
        glBufferData(GL_ARRAY_BUFFER,glass.size()*sizeof(GlassVertex),glass.data(),GL_STATIC_DRAW);
        glassVertices=GLsizei(glass.size());
        prepareCockpitDetails();
    }
    cockpitBake=lighting;
}
void Renderer::line(const Matrix& base,Vec3 a,Vec3 b,float width,Vec3 color) {
    Vec3 d=b-a;float distance=length(d);if(distance<.00001f)return;
    auto model=identity;
    // Screen-local XY line, with a little real thickness; respects depth testing.
    model[0]=d.x/distance;model[1]=d.y/distance;model[4]=-d.y/distance;model[5]=d.x/distance;
    model[12]=(a.x+b.x)*.5f;model[13]=(a.y+b.y)*.5f;model[14]=(a.z+b.z)*.5f;
    box(multiply(base,multiply(model,scaling(distance*.5f,width,.001f))),color);
}
Matrix Renderer::instrumentPose(unsigned id) const{
    if(const auto* seat=activeInterior()){
        const Surface* surfaces[]={&seat->overview,&seat->radar,&seat->ammo[0],&seat->reactor,&seat->heading};
        const auto& surface=*surfaces[id];
        const float width=id<2?.40f:id==4?.23f:.32f;
        const float height=id<2?.40f:id==4?.075f:.13f;
        const auto pose=surface.fit(width,height);
        return surface.hatch?multiply(hatchMotion,pose):pose;
    }
    const Vec3 positions[]={overview,radar,{-.57f,-.46f,-1.226f},heading,torso};
    const auto p=positions[id];
    return translation(p.x,p.y,p.z);
}
void Renderer::instruments(const World& w,const Matrix& cockpit,bool paused) {
    if(cockpitFx.booting()){bootInstruments(cockpit);return;}
    auto module=[&](unsigned id){instrumentGain=cockpitFx.power[id];};module(2);
    const Vec3 green{.25f,.95f,.08f},dim{.055f,.23f,.025f},amber{1,.65f,.05f};
    const auto* seat=activeInterior();
    auto mountPose=[&](const Surface& surface){
        return multiply(surface.hatch?multiply(cockpit,hatchMotion):cockpit,surface.pose);
    };
    auto centered=[&](const std::string& value,const Matrix& pose,float width,float height,Vec3 color){
        const float size=std::min(width/float(4*value.size()+2),height/8.f);
        text(value,multiply(pose,translation(-size*(2.f*value.size()+1.f),size*3.f,0)),size,color);
    };
    auto panelAt=[&](unsigned id){return multiply(cockpit,instrumentPose(id));};
    for(int mount=0;mount<MountCount;++mount) {
        const auto& r=ammoReadouts[mount];auto weapon=w.weapons[mount];
        std::string value=weapon==Weapon::Cutter?"INF":weapon==Weapon::Shield?"ON":weapon==Weapon::Empty?"--":std::to_string(w.ammo[mount]);
        if(w.rpgPrototype)value=w.rpgReadouts[mount];
        if(!w.rpgPrototype&&mount>=LeftHip&&w.weapons[mount==LeftHip?RightHip:LeftHip]==Weapon::AutoGrenade)value="LINK";
        if(seat){
            const auto& surface=seat->ammo[mount];
            centered(value,mountPose(surface),surface.width,surface.height,value=="LOST"?Vec3{1,.2f,.05f}:value=="--"?dim:green);
        }else{
            text(value,multiply(cockpit,translation(r.position.x,r.position.y,r.position.z)),r.size,
                 w.ammo[mount]||weapon==Weapon::Cutter||weapon==Weapon::Shield?green:Vec3{1,.2f,.05f});
        }
    }
    // Keep the schematic and target count on the overview.
    module(0);Matrix panel=panelAt(0);
    const Matrix overviewPanel=panel;
    const bool wideOverview=seat&&seat->overview.width>1.5f*seat->overview.height;
    if(seat&&w.mission>=0){
        if(wideOverview){
            const auto& face=seat->overview;
            const float chartSize=std::min(face.width*.48f,face.height);
            panel=multiply(mountPose(face),multiply(translation((chartSize-face.width)*.5f,0,0),
                                                   scaling(chartSize/.34f,chartSize/.34f,chartSize/.34f)));
        }else panel=multiply(panel,multiply(translation(0,.042f,0),scaling(.74f,.74f,.74f)));
    }
    if(!w.rpgPrototype)
        text(paused?"PAUSED":w.health<25?"ARMOR CRITICAL":"IS ONLINE",
             multiply(panel,translation(-.16f,.17f,0)),.007f,paused?amber:green);
    auto rect=[&](const Matrix& p,float x,float y,float hw,float hh,Vec3 c){
        line(p,{x-hw,y-hh,0},{x+hw,y-hh,0},.002f,c);line(p,{x+hw,y-hh,0},{x+hw,y+hh,0},.002f,c);
        line(p,{x+hw,y+hh,0},{x-hw,y+hh,0},.002f,c);line(p,{x-hw,y+hh,0},{x-hw,y-hh,0},.002f,c);
    };
    Vec3 armor=cockpitFx.armor<25?Vec3{1,.2f,.1f}:cockpitFx.armor<60?amber:green;
    // Overhead view: torso up in cockpit space; legs rotate relative to torso.
    float legAngle=-w.torsoYaw;
    auto planar=identity;planar[0]=std::cos(legAngle);planar[1]=std::sin(legAngle);planar[4]=-std::sin(legAngle);planar[5]=std::cos(legAngle);
    auto legs=multiply(panel,planar);
    for(int side:{-1,1})rect(legs,side*.04f,-.035f,.023f,.075f,green);
    line(legs,{0,-.02f,.002f},{0,.12f,.002f},.002f,green);
    line(legs,{-.02f,.095f,.002f},{0,.12f,.002f},.002f,green);line(legs,{.02f,.095f,.002f},{0,.12f,.002f},.002f,green);
    rect(panel,0,.015f,.105f,.036f,armor);rect(panel,0,.06f,.025f,.025f,armor);
    line(panel,{0,.065f,.004f},{0,.13f,.004f},.003f,amber);
    if(!w.rpgPrototype)text("LEGS / TORSO",multiply(panel,translation(-.15f,-.13f,.003f)),.0055f,green);
    const std::string counter=seat&&w.mission>=0?
        "OBJ "+std::to_string(std::max(0,w.counters[0]))+"/"+std::to_string(std::max(0,w.counters[1]))+"/"+std::to_string(std::max(0,w.counters[2])):
        "TARGETS "+std::to_string(w.destroyed);
    if(seat&&w.mission>=0){
        const size_t columns=wideOverview?16:24;
        const auto& status=w.rpgPilotStatus;
        const bool alert=status.starts_with("EJECTION IN")||status.starts_with("HULL FAULT")||
            status.starts_with("VOIDMAW")||status.starts_with("HOLD NANITES")||
            status.find(" DISABLED /")!=std::string::npos||status.find("SEEK COVER")!=std::string::npos||
            status.find("CLEAR MARKED ZONE")!=std::string::npos;
        const size_t perPage=wideOverview?3:2;
        // Critical actions own the overview immediately. Routine suit reports
        // share its pages with the mission instruction.
        auto lines=missionLines(alert?status:w.objective,columns);
        if(!alert&&!status.empty()){
            while(lines.size()%perPage)lines.emplace_back();
            auto reports=missionLines(status,columns);
            lines.insert(lines.end(),reports.begin(),reports.end());
        }
        const Vec3 messageColor=alert?Vec3{1,.34f,.08f}:green;
        const size_t first=lines.empty()?0:(size_t(std::max(0.f,w.elapsed)/4.f)%((lines.size()+perPage-1)/perPage))*perPage;
        if(wideOverview){
            const auto& face=seat->overview;
            const float chartSize=std::min(face.width*.48f,face.height);
            const float x=-.5f*face.width+chartSize+.01f;
            const float width=std::max(.01f,face.width-chartSize-.014f);
            const float font=std::min(width/(4.f*columns+2.f),face.height*.022f);
            const auto section=multiply(mountPose(face),translation(x,font*14.f,0));
            for(size_t i=first;i<lines.size()&&i<first+perPage;++i)
                text(lines[i],multiply(section,translation(0,-font*9*(i-first),.001f)),font,messageColor);
            text(counter,multiply(section,translation(0,-font*31,.001f)),font,amber);
        }else{
            for(size_t i=first;i<lines.size()&&i<first+perPage;++i)
                text(lines[i],multiply(overviewPanel,translation(-.182f,-.078f-.034f*(i-first),.003f)),.0037f,messageColor);
            text(counter,multiply(overviewPanel,translation(-.182f,-.153f,.003f)),.0045f,amber);
        }
    }else text(counter,multiply(panel,translation(-.16f,-.17f,0)),.0065f,green);
    // Top is the torso's forward bearing, with one sector per 45 degrees.
    for(size_t sector=0;sector<w.damageUntil.size();++sector){
        const float flash=cockpitFade(0,.85f,w.damageUntil[sector]-w.elapsed);
        const Vec3 color=flash>0?Vec3{.32f+.68f*flash,.035f+.22f*flash,.008f}
                               :Vec3{.15f,.13f,.07f};
        if(flash>0)instrumentGain=std::max(.65f,cockpitFx.power[0]);
        else module(0);
        const float start=float(sector)*pi/4-.31f;
        auto point=[](float a){return Vec3{.147f*std::sin(a),.008f+.139f*std::cos(a),.006f};};
        for(int step=0;step<4;++step)
            line(panel,point(start+.155f*step),point(start+.155f*(step+1)),flash>0?.0038f:.0015f,color);
    }
    module(1);panel=panelAt(1);
    const bool radarSensors=w.sensorsWorking();
    if(radarSensors||w.rpgPrototype){
    if(!radarSensors)instrumentGain=.85f; // Navigation stays on when the sensor feed goes offline.
    static const auto circle=[] {
        std::array<Vec3,49> vertices{};
        for(size_t k=0;k<vertices.size();++k){
            const float angle=k*2*pi/48;
            vertices[k]={std::cos(angle),std::sin(angle),0};
        }
        return vertices;
    }();
    for(float radius:{.06f,.12f,.18f})for(size_t k=1;k<circle.size();++k)
        line(panel,circle[k-1]*radius,circle[k]*radius,.001f,dim);
    line(panel,{-.19f,0,0},{.19f,0,0},.001f,dim);line(panel,{0,-.19f,0},{0,.19f,0},.001f,dim);
    for(size_t index=w.rpgRadar.enemyPrefix;radarSensors&&index<w.targets.size();++index){
        const auto &target=w.targets[index];
        if(!target.active||target.escaped||target.health<=0)continue;
        if(w.rpgPrototype&&target.protectedTarget)continue;
        if(target.spawnType>=0&&w.visibilityRules()&&!w.canPerceive(w,target.center,false,int(&target-w.targets.data())))continue;
        Vec3 d=target.center-w.position;float x=(std::cos(w.facing())*d.x-std::sin(w.facing())*d.z)*.0011f;
        float y=(-std::sin(w.facing())*d.x-std::cos(w.facing())*d.z)*.0011f;
        if(std::hypot(x,y)<.18f)box(multiply(panel,multiply(translation(x,y,.002f),scaling(.003f,.003f,.001f))),radarColor(target,w.game));
    }
    {
        const Vec3 colors[]={{.2f,.85f,1.f},{1.f,.55f,.15f},{.9f,.4f,1.f},{1.f,1.f,.8f}};
        auto point=[&](Vec3 position){auto d=position-w.position;float x=(std::cos(w.facing())*d.x-std::sin(w.facing())*d.z)*.0011f,y=(-std::sin(w.facing())*d.x-std::cos(w.facing())*d.z)*.0011f;float r=std::hypot(x,y);if(r>.17f){x*=.17f/r;y*=.17f/r;}return Vec3{x,y,.005f};};
        auto ring=[&](Vec3 p,float radius,Vec3 color,int segments=24){for(int k=0;k<segments;++k){float a=k*2*pi/segments,b=(k+1)*2*pi/segments;line(panel,p+Vec3{radius*std::cos(a),radius*std::sin(a),0},p+Vec3{radius*std::cos(b),radius*std::sin(b),0},.002f,color);}};
        if(w.rpgPrototype){
            for(auto depot:w.rpgSalvageDepots){
                const auto p=point(depot)+Vec3{0,0,.002f};
                const Vec3 color{.31f,.95f,1.f};
                constexpr float r=.009f;
                line(panel,p+Vec3{-r,-r,0},p+Vec3{r,-r,0},.0023f,color);
                line(panel,p+Vec3{r,-r,0},p+Vec3{r,r,0},.0023f,color);
                line(panel,p+Vec3{r,r,0},p+Vec3{-r,r,0},.0023f,color);
                line(panel,p+Vec3{-r,r,0},p+Vec3{-r,-r,0},.0023f,color);
                text("D",multiply(panel,translation(p.x-.0045f,p.y+.001f,p.z+.001f)),.0022f,color);
            }
            if(radarSensors&&w.rpgRadar.strength>.78f){
                const float sweep=(1.f-w.rpgRadar.strength)/.22f;
                ring({0,0,.003f},.014f+.159f*sweep,Vec3{.22f,.8f,.66f}*(1.f-sweep));
            }
            const Vec3 contactColor=Vec3{1.f,.23f,.12f}*std::sqrt(w.rpgRadar.strength);
            for(size_t i=0;i<w.rpgRadar.contacts.size();++i){
                const auto p=point(w.rpgRadar.contacts[i]);
                const Vec3 color=i<w.rpgRadar.contactColors.size()?
                    w.rpgRadar.contactColors[i]*std::sqrt(w.rpgRadar.strength):contactColor;
                box(multiply(panel,multiply(translation(p.x,p.y,p.z),scaling(.004f,.004f,.001f))),color);
            }
            for(const auto &pickup:w.pickups)if(unclaimedSalvage(pickup,w.playerSlot)){
                const auto p=point(pickup.position);
                const Vec3 color{.37f,1.f,.65f};
                constexpr float r=.009f;
                line(panel,p+Vec3{0,r,0},p+Vec3{r,0,0},.0022f,color);
                line(panel,p+Vec3{r,0,0},p-Vec3{0,r,0},.0022f,color);
                line(panel,p-Vec3{0,r,0},p-Vec3{r,0,0},.0022f,color);
                line(panel,p-Vec3{r,0,0},p+Vec3{0,r,0},.0022f,color);
                box(multiply(panel,multiply(translation(p.x,p.y,p.z),scaling(.0025f,.0025f,.001f))),color);
            }
            for(int sector=0;sector<PassiveRadar::sectors;++sector){
                if(!(w.rpgRadar.distantBearings&(1u<<sector)))continue;
                const float angle=float(sector)*2*pi/PassiveRadar::sectors+w.facing();
                const Vec3 bearing{std::sin(angle),std::cos(angle),0};
                const Vec3 tangent{bearing.y,-bearing.x,0};
                const Vec3 edge=bearing*.174f+Vec3{0,0,.005f};
                const Vec3 glow=contactColor*.85f;
                line(panel,edge-bearing*.01f+tangent*.006f,edge,.002f,glow);
                line(panel,edge-bearing*.01f-tangent*.006f,edge,.002f,glow);
            }
            for(const auto &arrival:w.rpgRadar.arrivals){
                const float progress=arrival.progress;
                const float wave=std::fmod(progress*3.f,1.f);
                const auto p=point(arrival.position);
                const Vec3 color=Vec3{1.f,.29f,.15f}*(1.f-progress);
                ring(p,.008f+.019f*wave,color*(1.f-.65f*wave),12);
                line(panel,p-Vec3{.003f,0,0},p+Vec3{.003f,0,0},.0025f,color);
                line(panel,p-Vec3{0,.003f,0},p+Vec3{0,.003f,0},.0025f,color);
            }
            for(const auto &position:w.rpgRadar.objectives){
                const auto p=point(position)+Vec3{0,0,.002f};
                const Vec3 color{1.f,.79f,.23f};
                ring(p,.012f,color,4);
                box(multiply(panel,multiply(translation(p.x,p.y,p.z),scaling(.003f,.003f,.001f))),color);
            }
            for(const auto &position:w.rpgRadar.allies){
                const auto p=point(position)+Vec3{0,0,.001f};
                const Vec3 color{.26f,.91f,1.f};
                line(panel,p-Vec3{.007f,0,0},p+Vec3{.007f,0,0},.003f,color);
                line(panel,p-Vec3{0,.007f,0},p+Vec3{0,.007f,0},.003f,color);
            }
        }
        if(w.proceduralLayout&&!w.rpgPrototype){
            auto marker=[&](Vec3 position,const std::string& label,Vec3 color){auto p=point(position);constexpr float r=.009f;line(panel,p+Vec3{0,r,0},p+Vec3{r,0,0},.002f,color);line(panel,p+Vec3{r,0,0},p+Vec3{0,-r,0},.002f,color);line(panel,p+Vec3{0,-r,0},p+Vec3{-r,0,0},.002f,color);line(panel,p+Vec3{-r,0,0},p+Vec3{0,r,0},.002f,color);text(label,multiply(panel,translation(p.x+.01f,p.y+.006f,p.z+.001f)),.003f,color);};
            for(size_t i=0;i<w.procedural.objectives.size();++i){const auto& goal=w.procedural.objectives[i];if(!goal.complete)marker(goal.position,"O"+std::to_string(i+1),goal.kind==ProceduralGoalKind::Recon?Vec3{.2f,.85f,1.f}:amber);}
            for(const auto& g:w.generators)if(g.remaining)marker(g.origin,"R"+std::to_string(g.remaining),{1,.4f,.15f});
            if(w.procedural.extracting)marker(w.procedural.extraction,"EX",green);
        }
        if(w.rpgTransitActive){auto exit=point(w.rpgTransitPosition);ring(exit,.013f,amber);text("EXIT",multiply(panel,translation(exit.x+.01f,exit.y,exit.z+.001f)),.004f,amber);}
        for(const auto& pilot:radarPilots(w)){if(pilot.playerSlot!=w.playerSlot&&pilot.team!=w.team&&w.visibilityRules()&&!w.canPerceive(w,pilot.position+Vec3{0,6,0},searchlightActive(pilot)))continue;auto p=point(pilot.position);Vec3 color=pilot.team==Team::Neutral?colors[pilot.playerSlot%4]:teamColour(pilot.team);bool dead=pilot.lifeSpent||pilot.health<=0;
            if(dead){line(panel,p+Vec3{-.004f,-.004f,0},p+Vec3{.004f,.004f,0},.002f,dim);line(panel,p+Vec3{-.004f,.004f,0},p+Vec3{.004f,-.004f,0},.002f,dim);}
            else {float a=pilot.facing()-w.facing();float size=w.rpgPrototype?1.3f:1.f;Vec3 tip{-std::sin(a)*.009f,std::cos(a)*.009f,0},side{std::cos(a)*.005f,std::sin(a)*.005f,0};tip=tip*size;side=side*size;line(panel,p+tip,p-tip*.5f+side,.002f*size,color);line(panel,p+tip,p-tip*.5f-side,.002f*size,color);line(panel,p-tip*.5f+side,p-tip*.5f-side,.002f*size,color);}
            text(std::string(pilot.team==Team::Attackers?"A":pilot.team==Team::Defenders?"D":"P")+std::to_string(pilot.playerSlot+1),multiply(panel,translation(p.x+.007f,p.y+.004f,p.z+.001f)),.003f,dead?dim:color);
            if(!dead&&pilot.selfPingUntil>w.elapsed)ring(p,.012f+.005f*(1+std::sin(w.elapsed*8)),color);
            if(pilot.aimPingUntil>w.elapsed){auto q=point(w.mapPingPosition(pilot));float r=.007f;ring(q,.013f+.003f*std::sin(w.elapsed*8),color);line(panel,q+Vec3{-r,0,0},q+Vec3{r,0,0},.002f,color);line(panel,q+Vec3{0,-r,0},q+Vec3{0,r,0},.002f,color);text("P"+std::to_string(pilot.playerSlot+1),multiply(panel,translation(q.x+.014f,q.y,q.z+.001f)),.003f,color);}
        }
    }
    line(panel,{0,0,0},{0,.035f,0},.003f,amber);
    if(seat&&w.visibilityRules()){
        const auto sight=visibilityRanges(w);
        const bool light=searchlightActive(w),scan=w.rpgRadar.strength>0;
        const bool exposed=light||(scan&&w.rpgRadar.exposesPilot);
        const std::string label=std::string(scan?"SCAN":light?"LIGHT":"SIGHT")+
            (exposed?" / EXPOSED ":" / COVERT ")+std::to_string(int(std::lround(exposed?sight.lit:sight.base)))+" M";
        centered(label,multiply(panel,translation(0,-.128f,.009f)),.225f,.030f,exposed?amber:green);
    }
    if(!radarSensors)text("SENSORS OFFLINE",multiply(panel,translation(-.12f,seat&&w.visibilityRules()?.17f:-.164f,.008f)),.0035f,{1,.28f,.14f});
    }else{instrumentGain=.25f;text("SENSORS OFFLINE",multiply(panel,translation(-.15f,0,.005f)),.005f,{1,.15f,.05f});}
    if(w.rpgPrototype){
        module(4);panel=panelAt(4);
        float bearing=std::fmod(-w.facing()*180.f/pi,360.f);
        if(bearing<0)bearing+=360.f;
        const int centerTick=int(std::floor(bearing/15.f));
        line(panel,{-.106f,.015f,0},{.106f,.015f,0},.001f,dim);
        for(int tick=centerTick-2;tick<=centerTick+3;++tick){
            const float x=(tick*15.f-bearing)*.0035f;
            if(std::abs(x)>.106f)continue;
            const int degrees=(tick*15+360)%360;
            const bool major=degrees%30==0;
            line(panel,{x,.015f,.002f},{x,major?.027f:.022f,.002f},.0012f,green);
        }
        line(panel,{0,.004f,.003f},{0,.023f,.003f},.002f,amber);
        constexpr const char *directions[]{"N","NE","E","SE","S","SW","W","NW"};
        const auto degrees=std::to_string(int(std::lround(bearing))%360+1000).substr(1);
        const auto value=std::string(directions[int(std::lround(bearing/45.f))%8])+" "+degrees;
        text(value,multiply(panel,translation(-.012f*float(value.size()),-.004f,.002f)),.006f,green);

        if(seat){
            auto bar=[&](const Matrix& pose,float width,float height,float level,Vec3 color){
                const bool vertical=height>width;
                // Leave dark gutters between cells so both charge and damage
                // remain readable when the two neighbouring rails are full.
                const int cells=8;
                const float step=(vertical?height:width)/cells;
                const float filled=std::clamp(level,0.f,1.f)*cells;
                for(int cell=0;cell<cells;++cell){
                    const float start=(cell-cells*.5f)*step;
                    auto segment=[&](float fraction,float depth,Vec3 tint){
                        const float extent=step*.84f*fraction;
                        const float center=start+step*.08f+extent*.5f;
                        const auto offset=vertical?translation(0,center,depth):translation(center,0,depth);
                        box(multiply(pose,multiply(offset,scaling((vertical?width:extent)*.5f,(vertical?extent:height)*.5f,.0003f))),tint);
                    };
                    segment(1,0,dim);
                    const float fill=std::clamp(filled-cell,0.f,1.f);
                    if(fill>0)segment(fill,.0008f,color);
                }
            };
            auto needle=[&](const Surface& face,float fraction,Vec3 tint,float radiusFraction=1.f){
                const float angle=(std::clamp(fraction,0.f,1.f)-.5f)*1.45f*pi;
                const float radius=.47f*radiusFraction*std::min(face.width,face.height);
                const auto pivot=mountPose(face);
                const Vec3 tip{radius*std::sin(angle),radius*std::cos(angle),.0015f};
                line(pivot,tip*(-.16f)+Vec3{0,0,.002f},tip,radius*.044f,tint);
                box(multiply(pivot,multiply(translation(0,0,.002f),scaling(radius*.080f,radius*.080f,.001f))),tint);
            };
            module(3);
            const auto& reactor=seat->reactor;
            const float heat=std::clamp(w.rpgHeat,0.f,1.f);
            const float pulse=.65f+.35f*std::sin(float(cockpitFx.clock)*2*pi*(w.rpgOverheated?4.f:1.8f));
            const Vec3 heatColor=w.rpgOverheated?Vec3{1,.15f,.04f}*pulse:heat>=.58f?amber:green;
            if(reactor.style==Surface::Style::Dial)needle(reactor,heat,heatColor);
            else bar(mountPose(reactor),reactor.width,reactor.height,heat,heatColor);
            const auto& health=seat->health;
            const auto healthPose=mountPose(health);
            const bool vertical=health.height>health.width;
            const float plating=float(w.rpgAp)/std::max(1,w.rpgMaxAp);
            if(health.style==Surface::Style::Dial){
                needle(health,w.health*.01f,armor);
                needle(health,plating,{.12f,.6f,1.f},.64f);
            }else for(int row=0;row<2;++row){
                const float shift=row?.255f:-.255f;
                const auto pos=vertical?translation(shift*health.width,0,0):translation(0,shift*health.height,0);
                bar(multiply(healthPose,pos),health.width*(vertical?.44f:1.f),health.height*(vertical?1.f:.44f),
                    row?plating:w.health*.01f,row?Vec3{.12f,.6f,1.f}:armor);
            }
            module(4);
            const auto& torsoPanel=seat->torso;
            panel=multiply(mountPose(torsoPanel),scaling(torsoPanel.width,torsoPanel.height,.15f));
            const float deflection=std::clamp(-w.torsoYaw/(pi*.5f),-1.f,1.f)*.43f;
            if(torsoPanel.style==Surface::Style::Attitude){
                // The hull's pitch and bank are independent of where the head looks.
                const auto pose=w.cockpitOrientation();
                const auto localUp=rotate(Quaternion{-pose.x,-pose.y,-pose.z,pose.w},{0,1,0});
                const float pitch=std::asin(std::clamp(-localUp.z,-1.f,1.f));
                const float bank=std::atan2(localUp.x,localUp.y);
                const float offset=std::remainder(pitch,pi/12)/(.5f*pi)*1.15f;
                for(int rung=-2;rung<=2;++rung){
                    const float y=.10f+.13f*rung-offset;
                    for(float s:{-1.f,1.f})line(panel,{s*.085f,y,0},{s*(rung?.28f:.38f),y,0},.0065f,green);
                }
                // A level bar remains valid on traversable tilted gravity surfaces.
                const float tangent=std::clamp(std::tan(bank),-1.f,1.f)*.22f;
                line(panel,{-.4f,.07f-tangent,.01f},{0,.07f,.01f},.012f,amber);
                line(panel,{0,.07f,.01f},{.4f,.07f+tangent,.01f},.012f,amber);
                line(panel,{deflection,-.095f,.015f},{deflection,.075f,.015f},.010f,amber);
            }else if(torsoPanel.style==Surface::Style::Dial){
                needle(torsoPanel,.5f+.5f*std::clamp(-w.torsoYaw/(pi*.5f),-1.f,1.f),amber);
            }else if(torsoPanel.style!=Surface::Style::Servo){
                line(panel,{-.45f,.15f,0},{.45f,.15f,0},.022f,dim);
                for(float x:{-.43f,0.f,.43f})line(panel,{x,.04f,0},{x,.27f,0},.009f,green);
                line(panel,{deflection,-.03f,.007f},{deflection,.35f,.007f},.016f,amber);
            }
            if(w.rpgThrustCapacity>0){
                const float cell=.90f/w.rpgThrustCapacity;
                for(int i=0;i<w.rpgThrustCapacity;++i){
                    const float fill=std::clamp(w.rpgThrustCharge-i,0.f,1.f);
                    const auto p=multiply(panel,translation(-.45f+(i+.5f)*cell,-.29f,.004f));
                    box(multiply(p,scaling(cell*.43f,.055f,.002f)),Vec3{.10f,.17f,.19f}+Vec3{.2f,.58f,.65f}*fill);
                }
            }
            if(seat->waveform){
                const auto& face=*seat->waveform;
                const auto scope=multiply(mountPose(face),scaling(face.width,face.height,.1f));
                for(float y:{-.32f,0.f,.32f})line(scope,{-.49f,y,0},{.49f,y,0},.006f,dim);
                for(float x:{-.4f,-.2f,0.f,.2f,.4f})line(scope,{x,-.40f,0},{x,.40f,0},.003f,dim);
                Vec3 last{};
                const float clock=float(std::fmod(cockpitFx.clock,1024.0));
                for(int i=0;i<=60;++i){
                    const float x=i/60.f,phase=std::fmod(x*2.4f+clock*(.46f+heat*.62f),1.f);
                    const auto peak=[&](float at,float span){return std::max(0.f,1.f-std::abs(phase-at)/span);};
                    const float pulse=.07f*peak(.24f,.10f)-.12f*peak(.40f,.06f)+.40f*peak(.48f,.06f)-.21f*peak(.56f,.065f)+.07f*peak(.71f,.1f);
                    Vec3 sample{x-.5f,pulse*(.55f+.45f*w.health*.01f),.008f};
                    if(i)line(scope,last,sample,.012f,green);
                    last=sample;
                }
            }
            if(seat->servo||torsoPanel.style==Surface::Style::Servo){
                const auto& face=seat->servo?*seat->servo:torsoPanel;
                const auto scope=multiply(mountPose(face),scaling(face.width,face.height,.1f));
                for(int hand=0;hand<2;++hand){
                    const auto& arm=w.arms[hand];
                    const auto q=w.cockpitOrientation();
                    const auto local=rotate(Quaternion{-q.x,-q.y,-q.z,q.w},arm.forward);
                    const float y=hand?-.23f:.23f;
                    const float joint=std::clamp(local.x,-1.f,1.f)*.18f;
                    const float lift=std::clamp(local.y,-1.f,1.f)*.17f;
                    const Vec3 stroke=arm.tracked?(hand?amber:green):dim;
                    const Vec3 points[]={{-.47f,y-.05f,0},{-.23f,y+.08f+joint,0},{.02f,y-.05f,0},{.24f,y+lift,0},{.46f,y+lift+joint*.4f,0}};
                    for(size_t i=1;i<5;++i)line(scope,points[i-1],points[i],.023f,stroke);
                }
            }
            for(const auto& gauge:seat->gauges){
                using Signal=Interior::Gauge::Signal;
                const auto signal=gauge.signal;
                needle(gauge.surface,signal==Signal::Heat?heat:signal==Signal::Hull?w.health*.01f:plating,
                       signal==Signal::Heat?heatColor:signal==Signal::Hull?armor:Vec3{.12f,.6f,1.f});
            }
            return;
        }
        module(3);panel=panelAt(3);
        const float heat=std::clamp(w.rpgHeat,0.f,1.f);
        const Vec3 heatColor=w.rpgOverheated?Vec3{1,.25f,.08f}:heat>=.58f?amber:green;
        text(std::string(w.rpgOverheated?"VENTING ":"REACTOR ")+std::to_string(int(std::lround(heat*100)))+"%",
             multiply(panel,translation(-.153f,.041f,.001f)),.0065f,heatColor);
        for(int segment=0;segment<10;++segment){
            const float x=-.15f+segment*.03f;
            const float fill=std::clamp(heat*10.f-segment,0.f,1.f);
            line(panel,{x,-.022f,.002f},{x+.023f,-.022f,.002f},.006f,dim);
            if(fill>0)line(panel,{x,-.022f,.003f},{x+.023f*fill,-.022f,.003f},.005f,heatColor);
        }
        return;
    }
    module(4);panel=panelAt(4);
    line(panel,{-.10f,.018f,0},{.10f,.018f,0},.0015f,dim);
    for(float x:{-.10f,0.f,.10f})line(panel,{x,.012f,0},{x,.026f,0},.001f,green);
    float needle=-w.torsoYaw/(pi/2)*.10f;
    line(panel,{needle,.008f,.002f},{needle,.035f,.002f},.002f,amber);
    text(std::to_string(int(std::round(w.torsoYaw*180/pi))),multiply(panel,translation(-.035f,-.013f,.001f)),.006f,green);
    module(3);panel=panelAt(3);
    float degrees=std::fmod(-w.chassisYaw*180/pi+360,360);
    text("HDG "+std::to_string(int(degrees)),multiply(panel,translation(-.155f,.04f,0)),.009f,green);
    text("TORSO +/-90",multiply(panel,translation(-.155f,-.025f,0)),.006f,dim);
}
}
