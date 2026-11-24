#include "render.hpp"
#include "rpg/insertion.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace iron {
using namespace avp;
void Renderer::drawInsertionWorld(const World& w) {
    const auto& s=cockpitFx.insertion;
    if(!w.rpgPrototype||!s.active||w.lifeSpent||(worldPass&&worldNear>=64))return;
    // Bounded, stateless 3D energy geometry. No smoke billboard, simulation
    // volume, gameplay damage or effect-slot mutation takes place in this pass.
    glUseProgram(program);glBindVertexArray(vao);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE);glDepthMask(GL_FALSE);
    auto segment=[&](const Matrix& base,Vec3 a,Vec3 b,float width,Vec3 colour){
        Vec3 d=b-a;float len=length(d);if(len<.00001f)return;
        Vec3 axis=d*(1/len),side=unit(cross(axis,{0,1,0}),{1,0,0}),up=unit(cross(side,axis));
        Matrix m=identity;
        m[0]=axis.x;m[1]=axis.y;m[2]=axis.z;
        m[4]=up.x;m[5]=up.y;m[6]=up.z;
        m[8]=side.x;m[9]=side.y;m[10]=side.z;
        auto center=(a+b)*.5f;m[12]=center.x;m[13]=center.y;m[14]=center.z;
        box(multiply(base,multiply(m,scaling(len*.5f,width,width))),colour);
    };
    Matrix body=multiply(translation(w.position.x,w.position.y+12,w.position.z),rotation(w.facing()));
    if(s.kind==rpg::InsertionKind::Drop) {
        if(s.entry>0)for(unsigned i=0;i<24;++i){
            float a=i*2*pi/24,depth=3+cockpitNoise(7,i)*15;
            float phase=std::fmod(s.time*23+cockpitNoise(13,i)*24,24.f)-12;
            Vec3 p{std::cos(a)*(1.6f+depth*.3f),phase,-depth};
            float speed=.45f+s.entry*2.5f;
            segment(body,p,p+Vec3{0,speed,0},.012f,{.13f*s.entry,.21f*s.entry,.25f*s.entry});
        }
        if(s.landed&&s.impact>.005f){
            Matrix ground=translation(w.position.x,w.rpgInsertionGround+.18f,w.position.z);
            float radius=3+(s.time-rpg::dropLandingTime)*24;
            for(int i=0;i<32;++i){
                float a=i*2*pi/32,b=(i+1)*2*pi/32;
                segment(ground,{radius*std::cos(a),0,radius*std::sin(a)},
                    {radius*std::cos(b),0,radius*std::sin(b)},.035f,{.4f*s.impact,.28f*s.impact,.13f*s.impact});
            }
        }
    } else if(s.kind==rpg::InsertionKind::Warp) {
        // Three tilted spatial rings and a quantized arrival shell surround
        // the actual landing point. Their scale evolves; the native cockpit,
        // instruments, glass and the tracked eye separation never rescale.
        auto center=multiply(translation(w.position.x,w.rpgInsertionGround+10,w.position.z),rotation(w.facing()));
        for(int ring=0;ring<3;++ring){
            float radius=(7+ring*3)*(1.45f-.45f*s.arrivalScale);
            float angle=s.time*(ring%2?-1.1f:.8f)+ring*.7f;
            Matrix plane=multiply(center,multiply(rotation(angle,.24f*(ring-1)),translation(0,(ring-1)*7.f,0)));
            for(int i=0;i<32;++i){
                if((i+int(s.time*12)+ring)%4==0)continue;
                float a=i*2*pi/32,b=(i+.78f)*2*pi/32;
                Vec3 colour=ring==1?Vec3{.13f,.45f,.52f}:Vec3{.32f,.16f,.50f};
                segment(plane,{radius*std::cos(a),0,radius*std::sin(a)},
                    {radius*std::cos(b),0,radius*std::sin(b)},.065f,colour*s.warp);
            }
        }
        for(unsigned i=0;i<48;++i){
            float a=i*2*pi/48+s.time*.18f;
            float radius=(4+float(i%3)*1.5f)*(1.6f-.6f*s.arrivalScale);
            float y=std::floor((cockpitNoise(17,i)*24-10+s.time*(i%2?5:-5))*2)*.5f;
            auto pose=multiply(center,multiply(translation(radius*std::cos(a),y,radius*std::sin(a)),rotation(a)));
            float size=(.06f+.12f*cockpitNoise(3,i))*s.arrivalScale;
            box(multiply(pose,scaling(size,size*2,size)),Vec3{.12f,.45f,.5f}*s.warp);
        }
    }
    if(s.kind==rpg::InsertionKind::Water&&s.bubbles>.001f){
        auto water=translation(w.position.x,w.rpgInsertionGround,w.position.z);
        for(unsigned i=0;i<18;++i){
            float depth=1.5f+cockpitNoise(41,i)*5;
            float x=(cockpitNoise(19,i)-.5f)*7;
            float y=.15f+std::fmod(cockpitNoise(31,i)*s.waterDepth+s.time*(.55f+.4f*cockpitNoise(6,i)),s.waterDepth+.3f);
            float radius=.045f+.065f*cockpitNoise(8,i);
            Vec3 c{.15f*s.bubbles,.35f*s.bubbles,.38f*s.bubbles};
            auto bubble=multiply(water,multiply(rotation(w.facing()),translation(x,y,-depth)));
            for(int edge=0;edge<4;++edge){
                float a=edge*pi*.5f,b=(edge+1)*pi*.5f;
                segment(bubble,{radius*std::cos(a),radius*std::sin(a),0},{radius*std::cos(b),radius*std::sin(b),0},.009f,c);
            }
        }
    }
    glDepthMask(GL_TRUE);glDisable(GL_BLEND);
}
void Renderer::drawInsertionCockpit(const World& w,const Matrix& cockpit) {
    const auto& s=cockpitFx.insertion;
    if(!w.rpgPrototype||w.lifeSpent||!s.active||s.kind!=rpg::InsertionKind::Drop)return;
    for(int side:{-1,1})for(int row:{-1,1}){
        float y=row>0?.71f:-.03f,z=row>0?-1.95f:-1.85f;
        auto hinge=multiply(cockpit,multiply(translation(side*.64f,y,z),
            multiply(rotation(side*(1.45f-1.1f*s.brake)),scaling(float(side),1,-1))));
        model("airbrake_panel",hinge);
    }
}
Renderer::InsertionWaterResources::~InsertionWaterResources(){
    if(program)glDeleteProgram(program);
    if(vao)glDeleteVertexArrays(1,&vao);
}
void Renderer::drawInsertionWater(const World& w){
    const auto& s=cockpitFx.insertion;
    if(s.kind!=rpg::InsertionKind::Water||!s.active)return;
    auto& r=insertionWater;
    if(!r.program){
        auto compile=[](GLenum type,const char* source){
            GLuint shader=glCreateShader(type);ironShaderSource(shader,source);glCompileShader(shader);
            GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
            if(!ok){char log[2048];glGetShaderInfoLog(shader,sizeof log,nullptr,log);glDeleteShader(shader);throw std::runtime_error(log);}
            return shader;
        };
        GLuint vertex=compile(GL_VERTEX_SHADER,R"(#version 330 core
uniform mat4 camera;uniform vec4 lens;out vec3 ray;out vec2 uv;
void main(){vec2 p=vec2(float((gl_VertexID<<1)&2),float(gl_VertexID&2))*2.0-1.0;
gl_Position=vec4(p,0,1);uv=p*.5+.5;ray=mat3(camera)*vec3((p.x+lens.z)/lens.x,(p.y+lens.w)/lens.y,-1);})");
        GLuint fragment=compile(GL_FRAGMENT_SHADER,R"(#version 330 core
in vec3 ray;in vec2 uv;out vec4 color;
uniform vec3 eye;uniform float level;uniform float time;uniform float wetness;uniform float bubbles;
float hash(float x){return fract(sin(x*91.17+3.1)*43758.5453);}
void main(){
 float cameraDepth=level-eye.y;
 float submerged=1.0-smoothstep(-.25,.15,-cameraDepth);
 float ripple=.025*sin(uv.x*44.0+time*5.0)+.018*sin(uv.x*19.0-time*3.0);
 float filmDepth=level-(eye.y+ray.y*1.65)+ripple;
 float cover=smoothstep(-.12,.12,filmDepth);
 float transition=1.0-smoothstep(.35,1.4,abs(cameraDepth));
 float band=(1.0-smoothstep(.015,.08,abs(filmDepth)))*transition;
 float alpha=.30*submerged+.11*cover*transition;
 vec3 tint=mix(vec3(.018,.19,.23),vec3(.08,.37,.40),cover);
 float caustic=step(.96,sin(uv.x*39.0+time*1.8)*sin(uv.y*24.0-time));
 tint+=vec3(.025,.06,.065)*caustic*submerged;
 tint+=vec3(.18,.34,.32)*band;alpha=max(alpha,band*.38);
 // Fixed-count canopy runoff. Quantized shafts and rounded highlights are
 // translucent; they never replace the scene with a smoke image.
 for(int i=0;i<12;++i){
   float id=float(i),x=.05+hash(id+1.0)*.9;
   float y=fract(hash(id+23.0)-time*(.075+hash(id+7.0)*.055));
   vec2 d=(uv-vec2(x,y))*vec2(1.65,1);
   float head=1.0-smoothstep(.005,.009,length(d));
   float tail=(1.0-smoothstep(.002,.0035,abs(uv.x-x)))*step(0.0,uv.y-y)*(1.0-smoothstep(.025,.08,uv.y-y));
   float drip=max(head,tail*.6)*wetness;
   tint=mix(tint,vec3(.30,.55,.58),drip);alpha=max(alpha,drip*.35);
 }
 color=vec4(tint,clamp(alpha,0.0,.48));
})");
        r.program=glCreateProgram();glAttachShader(r.program,vertex);glAttachShader(r.program,fragment);glLinkProgram(r.program);
        glDeleteShader(vertex);glDeleteShader(fragment);
        GLint linked=0;glGetProgramiv(r.program,GL_LINK_STATUS,&linked);
        if(!linked)throw std::runtime_error("Water insertion shader link failed");
        glGenVertexArrays(1,&r.vao);
    }
    // worldView is the actual per-eye camera after the common rigid insertion
    // motion; reconstruct its asymmetric lens rather than assuming mono FOV.
    auto camera=inverseRigid(worldView);auto lens=multiply(vp,camera);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(r.program);glBindVertexArray(r.vao);
    glUniformMatrix4fv(glGetUniformLocation(r.program,"camera"),1,GL_FALSE,camera.data());
    glUniform4f(glGetUniformLocation(r.program,"lens"),lens[0],lens[5],lens[8],lens[9]);
    glUniform3f(glGetUniformLocation(r.program,"eye"),camera[12],camera[13],camera[14]);
    glUniform1f(glGetUniformLocation(r.program,"level"),w.rpgInsertionGround+s.waterDepth);
    glUniform1f(glGetUniformLocation(r.program,"time"),s.time);
    glUniform1f(glGetUniformLocation(r.program,"wetness"),s.wetness);
    glUniform1f(glGetUniformLocation(r.program,"bubbles"),s.bubbles);
    glDrawArrays(GL_TRIANGLES,0,3);
    glDepthMask(GL_TRUE);glDisable(GL_BLEND);glEnable(GL_DEPTH_TEST);
    glUseProgram(program);glBindVertexArray(vao);
}
void Renderer::drawInsertionStatus(const World& w,const Matrix& head) {
    const auto& s=cockpitFx.insertion;
    if(!w.rpgPrototype||w.lifeSpent||!s.active)return;
    if(s.kind==rpg::InsertionKind::Water)drawInsertionWater(w);
    auto hud=multiply(head,translation(0,0,-2));
    glDisable(GL_DEPTH_TEST);
    text(rpg::insertionReadout(s),multiply(hud,translation(-.82f,.60f,0)),.0075f,{1,.78f,.25f});
    text("WEAPONS SAFE / HEAD TRACKING FREE",multiply(hud,translation(-.55f,.51f,0)),.006f,{.55f,1,.8f});
    glEnable(GL_DEPTH_TEST);
}
} // namespace iron
