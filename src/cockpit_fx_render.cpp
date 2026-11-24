#include "render.hpp"
#include "cockpit_glass.hpp"
#include "rpg/enemy_animation.hpp"
#include <cstddef>
#include <stdexcept>
namespace iron {
using namespace avp;
namespace {
GLuint effectProgram(const char* vertex,const char* fragment){
    auto compile=[](GLenum type,const char* code){
        GLuint s=glCreateShader(type);ironShaderSource(s,code);glCompileShader(s);GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
        if(!ok){char log[2048];glGetShaderInfoLog(s,sizeof log,nullptr,log);glDeleteShader(s);throw std::runtime_error(log);}return s;
    };
    GLuint v=compile(GL_VERTEX_SHADER,vertex),f=compile(GL_FRAGMENT_SHADER,fragment),p=glCreateProgram();
    glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);
    GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(p);throw std::runtime_error("Cockpit effect shader link failed");}return p;
}
template<class Vertices> void segment(Vertices& vertices,const Matrix& base,Vec3 a,Vec3 b,float width,Vec3 color){
    Vec3 d=b-a;float size=std::hypot(d.x,d.y);if(size<.000001f||width<=0)return;
    Vec3 side{-d.y/size*width,d.x/size*width,0};Vec3 corners[]={a-side,a+side,b+side,b-side};
    for(int i:{0,1,2,0,2,3}){auto p=point(base,{corners[i].x,corners[i].y,corners[i].z});vertices.push_back({{p[0],p[1],p[2]},color});}
}
}
Matrix Renderer::cockpitPose(const World& w)const{
    const auto origin=w.cockpitOrigin();
    const auto q=w.cockpitOrientation();
    return multiply(poseMatrix({{q.x,q.y,q.z,q.w},{origin.x,origin.y,origin.z}}),cockpitFx.hull);
}
void Renderer::loadCockpitEffects(){
    glassProgram=effectProgram(R"(#version 330 core
layout(location=0) in vec3 position;layout(location=1) in vec3 normal;layout(location=2) in vec2 uv;layout(location=3) in vec2 metres;
uniform mat4 transform;out vec3 P;out vec3 N;out vec2 UV;out vec2 Wet;
void main(){P=position;N=normal;UV=uv;Wet=metres;gl_Position=transform*vec4(position,1);})",
R"(#version 330 core
in vec3 P;in vec3 N;in vec2 UV;in vec2 Wet;uniform vec3 eye;uniform vec3 sky;uniform vec3 ground;uniform float rain;uniform float seconds;uniform vec4 slime;uniform float bite;out vec4 color;
vec3 dropHash(vec2 p){vec3 q=fract(vec3(p.xyx)*vec3(.1031,.1030,.0973));q+=dot(q,q.yxz+33.33);return fract((q.xxy+q.yzz)*q.zyx);}
void main(){
    vec3 n=normalize(N),v=normalize(eye-P);if(dot(n,v)<0.0)n=-n;
    float fresnel=.04+.96*pow(1.0-max(0.0,dot(n,v)),5.0);
    vec3 reflected=reflect(-v,n);
    vec3 environment=mix(ground*.42,sky*.7,smoothstep(-.15,.45,reflected.y));
    float highlight=pow(max(0.0,dot(reflected,normalize(vec3(-.35,1,.7)))),64.0);
    float edge=1.0-smoothstep(.0,.026,min(min(UV.x,1.0-UV.x),min(UV.y,1.0-UV.y)));
    float opacity=.065+fresnel*.38+highlight*.12+edge*.10;
    vec3 reflection=mix(vec3(.13,.32,.34),environment,.60)+vec3(.7,.84,.8)*highlight;
    if(rain>0.0){
        // Pane-local metre coordinates: equal bead sizes on front/side/roof,
        // stable under head movement and identical for the two eye draws.
        vec2 cell=Wet*24.0+vec2(0,seconds*.8);vec3 h=dropHash(floor(cell));
        vec2 d=fract(cell)-(.25+h.xy*.5);float radius=.09+h.x*.09;
        float shape=length(d*vec2(1,.8))/radius;
        float bead=(1.0-smoothstep(.8,1.0,shape))*step(h.z,rain*.38);
        float rim=(1.0-smoothstep(.07,.22,abs(shape-.72)))*bead;
        float shine=max(0.0,dot(normalize(d+vec2(.0001)),normalize(vec2(-.6,.8))));
        float trail=(1.0-smoothstep(.012,.036,abs(d.x)))*step(0.0,d.y)*(1.0-smoothstep(.05,.5,d.y))*step(h.z,rain*.15);
        float wetAlpha=bead*.16+rim*.38+trail*.09;
        vec3 wetColour=mix(environment*.32,vec3(.72,.86,.9),shine*rim);
        reflection=mix(reflection,wetColour,clamp(wetAlpha/max(opacity+wetAlpha,.001),0.0,1.0));opacity=min(.8,opacity+wetAlpha);
    }
    // A viscous contact print feeds irregular rivulets down the front pane.
    // P is the physical canopy mesh, so looking around never moves the deposit.
    if(slime.w>0.0 && abs(N.z)>.65 && abs(P.x-slime.x)<.43 && P.y<slime.y+.23){
        vec2 d=P.xy-slime.xy;
        d.x*=sign(slime.x);
        float down=-d.y;
        float spread=1.0+.38*(1.0-smoothstep(0.0,.32,bite));
        vec2 printCoord=d/vec2(.22*spread,.10*spread);
        float printRadius=length(printCoord)+.12*sin(d.x*39.0+d.y*18.0);
        float film=.43*(1.0-smoothstep(.6,1.1,printRadius));
        float glint=.30*(1.0-smoothstep(.05,.22,abs(printRadius-.78)));
        float body=0.0;
        for(int i=0;i<5;++i){
            float k=float(i);
            float offset=(k-2.0)*.074+.022*sin(k*2.3);
            float travel=clamp((slime.z-.19-k*.075)*(.44-.04*k),0.0,.45+.14*cos(k*2.7));
            float spine=offset+(.018+.005*k)*(sin(down*11.0+k*1.7)-sin(k*1.7))+
                        (.08+.04*cos(k))*down;
            float x=d.x-spine;
            float width=.015+.006*cos(k*4.0)+.012*max(0.0,1.0-down*5.0);
            float core=(1.0-smoothstep(width*.48,width*1.4,abs(x)))*
                       smoothstep(-.05,.025,down)*(1.0-smoothstep(travel-.012,travel+.015,down));
            vec2 tip=vec2(x/(width*1.24),(down-travel)/(.023+.008*sin(k)));
            float bead=1.0-smoothstep(.65,1.15,length(tip));
            float rim=(1.0-smoothstep(.15,.8,abs(x/width+.53)))*core;
            body=max(body,max(core*.75,bead));
            glint=max(glint,max(rim*.7,bead*(1.0-smoothstep(.05,.7,length(tip+vec2(.36,.36))))));
        }
        // Fresh beads spread out from the compression point and join the older trails.
        float biteAge=max(0.0,bite);
        if(biteAge<1.25){
            for(int j=0;j<3;++j){
                float k=float(j);
                vec2 landed=vec2(.19-.14*k,.045+.045*sin(4.0*k));
                vec2 beadPos=landed*(.4+.6*smoothstep(0.0,.10,biteAge));
                beadPos.y-=.28*biteAge*biteAge*(.7+.2*k);
                vec2 bd=(d-beadPos)/vec2(.021+.007*k,.035+.014*biteAge);
                float bead=(1.0-smoothstep(.55,1.05,length(bd)))*(1.0-smoothstep(.65,1.25,biteAge));
                body=max(body,bead);glint=max(glint,bead*(1.0-smoothstep(.0,.8,length(bd+vec2(.33,.33)))));
            }
        }
        float cover=clamp(max(film,body)*slime.w,0.0,.86);
        vec3 gel=mix(vec3(.15,.28,.13),vec3(.77,.88,.58),glint);
        // Compose the gel over the glass before blending the pane with the world.
        float combined=cover+opacity*(1.0-cover);
        reflection=(gel*cover+reflection*opacity*(1.0-cover))/combined;
        opacity=combined;
    }
    color=vec4(reflection,opacity);
})");
    glassTransform=glGetUniformLocation(glassProgram,"transform");glassEye=glGetUniformLocation(glassProgram,"eye");
    glassSky=glGetUniformLocation(glassProgram,"sky");glassGround=glGetUniformLocation(glassProgram,"ground");
    glassRain=glGetUniformLocation(glassProgram,"rain");glassTime=glGetUniformLocation(glassProgram,"seconds");
    glassSlime=glGetUniformLocation(glassProgram,"slime");glassBite=glGetUniformLocation(glassProgram,"bite");
    classicGlass=cockpitGlazing().vertices;glassVertices=GLsizei(classicGlass.size());
    glGenVertexArrays(1,&glassVao);glBindVertexArray(glassVao);glGenBuffers(1,&glassVbo);glBindBuffer(GL_ARRAY_BUFFER,glassVbo);
    glBufferData(GL_ARRAY_BUFFER,classicGlass.size()*sizeof(GlassVertex),classicGlass.data(),GL_STATIC_DRAW);
    const size_t offsets[]={offsetof(GlassVertex,position),offsetof(GlassVertex,normal),offsetof(GlassVertex,uv),offsetof(GlassVertex,metres)};
    for(int i=0;i<4;++i){glEnableVertexAttribArray(i);glVertexAttribPointer(i,i<2?3:2,GL_FLOAT,GL_FALSE,sizeof(GlassVertex),reinterpret_cast<void*>(offsets[i]));}
    detailProgram=effectProgram(R"(#version 330 core
layout(location=0) in vec3 position;layout(location=1) in vec3 tint;
uniform mat4 transform;out vec3 C;void main(){C=tint;gl_Position=transform*vec4(position,1);})",
R"(#version 330 core
in vec3 C;out vec4 color;void main(){color=vec4(C,1);})");
    detailTransform=glGetUniformLocation(detailProgram,"transform");
    glGenVertexArrays(1,&detailVao);glBindVertexArray(detailVao);glGenBuffers(1,&detailVbo);glBindBuffer(GL_ARRAY_BUFFER,detailVbo);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(DetailVertex),nullptr);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(DetailVertex),reinterpret_cast<void*>(offsetof(DetailVertex,color)));
    Matrix surface=identity;
    for(int stage=1;stage<=3;++stage){
        auto& mesh=fractures[stage];int clusters=stage==1?1:3;float growth=stage==3?1.22f:1;
        for(int cluster=0;cluster<clusters;++cluster){
            Vec3 center=cluster==0?Vec3{-.42f,.36f,0}:cluster==1?Vec3{.47f,.1f,0}:Vec3{.15f,.55f,0};
            for(int ray=0;ray<7;++ray){
                float angle=ray*2*pi/7+cluster*.47f;Vec3 last=center;
                for(int step=1;step<=4;++step){
                    float reach=step*.080f*growth*(.65f+cockpitNoise(float(ray),cluster+3)*.8f);
                    float a=angle+(cockpitNoise(float(step),ray+cluster*9)-.5f)*.4f;
                    Vec3 next=center+Vec3{std::cos(a)*reach,std::sin(a)*reach,0};next.y=std::clamp(next.y,-.20f,.84f);
                    segment(mesh,surface,last,next,.0018f,{.035f,.065f,.075f});
                    segment(mesh,surface,last+Vec3{.0015f,.0015f,.001f},next+Vec3{.0015f,.0015f,.001f},.0007f,{.68f,.87f,.84f});
                    if(step==2||step==3){
                        Vec3 fork=next+Vec3{std::cos(a+.65f)*.06f*growth,std::sin(a+.65f)*.06f*growth,0};fork.y=std::clamp(fork.y,-.20f,.84f);
                        segment(mesh,surface,next,fork,.0007f,{.50f,.67f,.65f});
                    }
                    last=next;
                }
            }
        }
        for(auto& vertex:mesh)vertex.position=cockpitFrontGlass(vertex.position.x,vertex.position.y);
    }
    glUseProgram(program);glBindVertexArray(vao);
}
Vec3 Renderer::frontGlassPoint(float x,float y) const{
    if(const auto* seat=activeInterior();seat&&!seat->glass.empty()){
        // Interpolate the actual triangle struck by this forward ray. Only front
        // glazing can receive cracks or a canopy contact print.
        float best=-100.f;
        for(size_t i=0;i<seat->glass.size();i+=3){
            const auto& v=seat->glass[i];
            if(std::abs(v.normal.z)<.55f)continue;
            const auto a=v.position,b=seat->glass[i+1].position,c=seat->glass[i+2].position;
            const float det=(b.x-a.x)*(c.y-a.y)-(c.x-a.x)*(b.y-a.y);
            if(std::abs(det)<1e-6f)continue;
            const float u=((x-a.x)*(c.y-a.y)-(c.x-a.x)*(y-a.y))/det;
            const float t=((b.x-a.x)*(y-a.y)-(x-a.x)*(b.y-a.y))/det;
            const float z=a.z+u*(b.z-a.z)+t*(c.z-a.z);
            if(u>=0&&t>=0&&u+t<=1.f&&z<0)best=std::max(best,z);
        }
        if(best>-100.f)return {x,y,best+.002f};
        return {x,y,0}; // Outside the authored pane.
    }
    return cockpitFrontGlass(x,y);
}
void Renderer::prepareCockpitDetails(){
    detailVertices=0;
    const int stage=cockpitFx.fractureStage();
    if(stage!=projectedStage||activeCockpit!=fractureCockpit){
        projectedStage=stage;
        fractureCockpit=activeCockpit;
        projectedFractures.clear();
        const auto& source=fractures[stage];
        if(!activeInterior())projectedFractures=source;
        else for(size_t i=0;i<source.size();i+=3){
            std::array<DetailVertex,3> triangle;
            bool inside=true;
            for(size_t j=0;j<3;++j){
                triangle[j]=source[i+j];
                auto& p=triangle[j].position;
                p=frontGlassPoint(p.x,p.y);
                inside&=p.z<0;
            }
            if(inside)projectedFractures.insert(projectedFractures.end(),triangle.begin(),triangle.end());
        }
    }
    auto vertices=projectedFractures;
    fractureVertices=GLsizei(vertices.size());
    float time=float(std::fmod(cockpitFx.clock,3600.0)),armor=cockpitFx.armor,severity=1-armor*.01f;
    if(!cockpitFx.booting()&&armor<=75){
        for(unsigned id=0;id<5;++id){
            Matrix base=multiply(instrumentPose(id),translation(0,id==2&&!activeInterior()?-.01f:0.f,.008f));
            if(armor<50&&id<2&&cockpitNoise(std::floor(time*8),id)<severity*.7f){
                float y=(cockpitNoise(std::floor(time*8),id+10)-.5f)*.3f;
                segment(vertices,base,{-.182f,y,.001f},{.182f,y,.001f},.012f,{.001f,.004f,.002f});
                segment(vertices,base,{-.17f,y+.012f,.004f},{.17f,y+.012f,.004f},.0007f,Vec3{.13f,.45f,.12f}*cockpitFx.power[id]);
            }
            float clock=time*(armor<25?1.4f:.65f)+id*.371f,age=clock-std::floor(clock);
            if(age>.14f||cockpitNoise(std::floor(clock),id+28)>severity*.9f)continue;
            Vec3 origin{id<2?.19f:.15f,id<2?-.15f:-.025f,.012f};float flash=std::max(0.f,1-age/.14f);
            for(unsigned ray=0;ray<6;++ray){
                Vec3 end=origin+Vec3{(cockpitNoise(std::floor(clock),ray+id*6)-.5f)*.15f,-.01f-cockpitNoise(float(ray),id+unsigned(clock))*.07f,.01f};
                Vec3 middle=(origin+end)*.5f+Vec3{.012f*std::sin(time*87+ray),.01f,0};
                segment(vertices,base,origin,middle,.0015f*flash,{.55f,.83f,1});
                segment(vertices,base,middle,end,.001f*flash,{1,.48f,.06f});
            }
            segment(vertices,base,origin+Vec3{-.008f*flash,0,.003f},origin+Vec3{.008f*flash,0,.003f},.007f*flash,{.8f,.92f,1});
        }
    }
    detailVertices=GLsizei(vertices.size());
    if(detailVertices){glBindBuffer(GL_ARRAY_BUFFER,detailVbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(DetailVertex),vertices.data(),GL_STREAM_DRAW);}
}
// Emit into the existing depth-tested world effect batch. Both endpoints live in
// 3D: the tooth follows the current rig pose and the contact follows the hull.
void Renderer::drawCanopyDrool(const World& w,const rpg::EnemyRig& rig,const Target& creature,
                                const WalkerPose& pose,const Matrix& body){
    const auto &cue=creature.ai.voidmawPose;
    const float age=w.elapsed+w.rpgPoseLead-cue.started;
    if(cue.phase!=rpg::VoidmawPose::Cling||age<.28f)return;
    const int side=cue.side<0?-1:1;
    if(droolRig!=&rig||droolSide!=side){
        droolRig=&rig;droolSide=side;droolPart=-1;
        if(auto socket=rpg::voidmawBiteSocket(rig,creature,cue.side)){
            droolPart=int(socket->part);droolTip=socket->point;
        }
    }
    if(droolPart<0||size_t(droolPart)>=pose.parts.size())return;
    auto transform=[](const Matrix& m,Vec3 v){
        auto p=point(m,{v.x,v.y,v.z});return Vec3{p[0],p[1],p[2]};
    };
    const auto mouth=transform(body,animatedPoint(pose.parts[droolPart],droolTip));
    const auto hull=cockpitPose(w),camera=inverseRigid(worldView);
    const Vec3 right{hull[0],hull[1],hull[2]},up{hull[4],hull[5],hull[6]};
    const auto contact=rpg::voidmawCanopyContact(cue.side);
    const float gain=cockpitFade(.28f,.52f,age);
    const auto oldOpacity=shapeOpacity;
    for(int i=0;i<3;++i){
        const float offset=(float(i)-1)*.064f;
        auto start=mouth+right*offset;
        const float cycle=std::fmod(age*.88f+float(i)*.33f,1.f);
        auto glass=frontGlassPoint(contact.x+offset*1.8f,contact.y-.12f-.06f*float(i));
        if(glass.z>=0)continue;
        glass.z-=.016f;
        auto end=transform(hull,glass);
        if(i==1)end=start-up*(.10f+.24f*cycle);
        // Between bites, two threads stretch to the glass and a free bead forms on the lip.
        Vec3 last=start;
        for(int j=1;j<=5;++j){
            const float t=float(j)/5;
            auto next=start+(end-start)*t-up*(.09f*std::sin(pi*t));
            next=next+right*(.009f*std::sin(age*4.8f+i)*std::sin(pi*t));
            const float width=.008f+(j==5?.006f:.002f);
            shapeOpacity=.77f*gain;
            drawEnemyStreak(last,next,width,{.34f,.54f,.24f},camera);
            shapeOpacity=.82f*gain;
            drawEnemyStreak(last+right*.004f,next+right*.004f,width*.32f,{.85f,.94f,.66f},camera);
            last=next;
        }
        if(i==1){
            auto falling=end-up*(cycle*cycle*.21f);
            drawEnemyStreak(falling,falling-up*.033f,.015f,{.73f,.85f,.49f},camera,-.55f);
        }
    }
    shapeOpacity=oldOpacity;
}

void Renderer::drawCockpitGlass(const World& world,const Matrix& cockpit,const Matrix& camera){
    auto eye=point(multiply(inverseRigid(cockpit),camera));auto m=multiply(vp,cockpit);
    glUseProgram(glassProgram);glBindVertexArray(glassVao);glUniformMatrix4fv(glassTransform,1,GL_FALSE,m.data());glUniform3fv(glassEye,1,eye.data());
    glUniform3f(glassSky,environmentView.sky.x,environmentView.sky.y,environmentView.sky.z);glUniform3f(glassGround,environmentView.ground.x,environmentView.ground.y,environmentView.ground.z);
    glUniform1f(glassRain,world.activeEnvironment().weather==Weather::Rain?.8f:world.activeEnvironment().weather==Weather::Storm?1.f:0.f);
    glUniform1f(glassTime,float(std::fmod(cockpitFx.clock,3600.0)));
    const auto &wet=cockpitFx.slime;
    const float now=world.elapsed+world.rpgPoseLead,age=std::max(0.f,now-wet.attachedAt);
    const float strength=wet.side==0?0.f:cockpitFade(.19f,.44f,age)*(1-cockpitFade(.12f,1.85f,now-wet.lastContact));
    if(strength>0){
        const auto contact=rpg::voidmawCanopyContact(wet.side);
        glUniform4f(glassSlime,contact.x,contact.y,age,strength);
        glUniform1f(glassBite,std::max(0.f,now-wet.biteAt));
    }else glUniform4f(glassSlime,0,0,0,0);
    glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ZERO,GL_ONE);glDepthMask(GL_FALSE);glDrawArrays(GL_TRIANGLES,0,glassVertices);
    auto cracks=fractureVertices;
    if(cracks&&detailVertices>=cracks){glUseProgram(detailProgram);glBindVertexArray(detailVao);glUniformMatrix4fv(detailTransform,1,GL_FALSE,m.data());glDrawArrays(GL_TRIANGLES,0,cracks);}
    glDepthMask(GL_TRUE);glDisable(GL_BLEND);glUseProgram(program);glBindVertexArray(vao);
}
void Renderer::drawCockpitDetails(const World& w,const Matrix& cockpit,const Matrix& camera){
    auto m=multiply(vp,cockpit);auto cracks=fractureVertices;
    if(detailVertices>cracks){glUseProgram(detailProgram);glBindVertexArray(detailVao);glUniformMatrix4fv(detailTransform,1,GL_FALSE,m.data());glDrawArrays(GL_TRIANGLES,cracks,detailVertices-cracks);}
    glUseProgram(program);glBindVertexArray(vao);
    const float steam=w.rpgPrototype?reactorSteam(w.rpgHeat,w.rpgOverheated):0.f;
    if(steam>.01f&&textures.contains("smoke-atlas")){
        const float time=float(std::fmod(cockpitFx.clock,3600.0));
        const float oldAlpha=spriteAlpha,oldSoftness=spriteSoftness;
        spriteSoftness=.7f;
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
        for(float side:{-1.f,1.f})for(int i=0;i<4;++i){
            const float cycle=time*1.25f+i*.25f+side*.13f;
            const float age=cycle-std::floor(cycle);
            const float spread=.11f+.40f*age;
            const float drift=(cockpitNoise(std::floor(cycle),unsigned(i)+(side<0?0u:4u))-.5f)*.15f*age;
            const auto p=point(cockpit,{side*(.66f-.20f*age)+drift,-.31f+age*.72f,-1.08f+age*.24f});
            auto pose=camera;pose[12]=p[0];pose[13]=p[1];pose[14]=p[2];
            spriteAlpha=steam*.59f*std::sin(age*pi);
            art("smoke-atlas",multiply(pose,multiply(translation(-spread*.5f,spread*.5f,0),
                scaling(spread,-spread,1))),{0,float(std::min(9,int(age*10)))*.1f,1,.1f},{.84f,.94f,1.f});
        }
        glDepthMask(GL_TRUE);glDisable(GL_BLEND);
        spriteAlpha=oldAlpha;spriteSoftness=oldSoftness;
        glUseProgram(program);glBindVertexArray(vao);
    }
    if(!cockpitFx.booting()&&cockpitFx.armor<25)text("NO SIGNAL",multiply(cockpit,multiply(instrumentPose(3),translation(-.14f,.005f,.02f))),.005f,{.45f,.16f,.025f});
}
void Renderer::bootInstruments(const Matrix& cockpit){
    const Vec3 green{.25f,.95f,.08f},amber{1,.65f,.05f};
    for(unsigned id=0;id<5;++id){
        instrumentGain=cockpitFx.power[id];if(instrumentGain<.02f)continue;
        Matrix panel=multiply(cockpit,instrumentPose(id));float age=cockpitFx.bootTime-id*.19f;
        if(id<2){
            if(age<1.6f){float width=.18f*cockpitFade(.5f,1.5f,age);line(panel,{-width,0,0},{width,0,0},.0025f,green);}
            else if(age<3.5f){
                text(id==0?"CORE / SELF TEST":"SCAN / CALIBRATE",multiply(panel,translation(-.18f,.12f,.003f)),.0055f,green);
                for(int k=0;k<3;++k)if(age>1.7f+k*.36f)text(id==0?(k==0?"REACTOR  OK":k==1?"SERVOS   OK":"ARMOUR   OK"):(k==0?"ARRAY    OK":k==1?"UPLINK   OK":"CONTACTS OK"),multiply(panel,translation(-.15f,.035f-k*.065f,.003f)),.005f,green);
            }else{
                text(id==0?"IS ONLINE":"RADAR ONLINE",multiply(panel,translation(-.16f,.10f,.003f)),.0065f,green);
                text("SYSTEM READY",multiply(panel,translation(-.15f,-.05f,.003f)),.0055f,green);
            }
        }else{
            const char* message=age<2.4f?"8888":age<3.5f?"TEST":"READY";
            text(message,multiply(panel,translation(-.06f,0,.004f)),.006f,age<3.5f?amber:green);
        }
    }
}
}
