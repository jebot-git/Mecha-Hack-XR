#include "render.hpp"
#include "presentation.hpp"
#include "jaguar_cry.hpp"
#include <stdexcept>
namespace iron {
void Renderer::prepareSearchlights(const World& w){
    auto ranges=visibilityRanges(w);visibilityBase=ranges.base;
    visibilityPenalty=w.visibilityRules()&&!weatherLightningActive(w.activeEnvironment(),w.weatherTime())&&(environmentView.night||environmentView.density>0);
    localLampOrigin=searchlightOrigin(visibilityObserver);localLampAxis=searchlightDirection(visibilityObserver);localLampRange=w.visibilityRules()&&searchlightActive(visibilityObserver)?ranges.lit:0;
    const auto* map=w.targets.empty()?nullptr:w.targets.front().mesh.get();bool refresh=ranges.lit!=lightTraceRange||map!=lightMap||w.elapsed<lightTraceTime||w.elapsed-lightTraceTime>=.05f;
    for(const auto& p:radarPilots(w))if(p.playerSlot<lightStates.size()){
        bool on=w.visibilityRules()&&searchlightActive(p);
        if(on&&(refresh||!lightStates[p.playerSlot]))lightBeams[p.playerSlot]=traceSearchlight(w,p);
        lightStates[p.playerSlot]=on;
    }
    if(refresh){lightTraceRange=ranges.lit;lightTraceTime=w.elapsed;lightMap=map;}
}
void Renderer::loadSearchlight(){
    const char* sources[]={R"(#version 330 core
layout(location=0) in vec2 position;
uniform mat4 transform;uniform mat4 viewModel;uniform int kind;
out vec2 UV;out float eyeDepth;
void main(){UV=position;vec2 p=position;if(kind==1)p.x=(p.x-.5)*mix(.035,1.0,p.y)+.5;
vec4 local=vec4(p,0,1);eyeDepth=-(viewModel*local).z;gl_Position=transform*local;})",
R"(#version 330 core
in vec2 UV;in float eyeDepth;uniform vec2 depthRange;uniform vec3 tint;
uniform float opacity;uniform int kind;out vec4 color;
void main(){
    if(eyeDepth<depthRange.x||eyeDepth>=depthRange.y)discard;
    float alpha;
    if(kind==1){float edge=1.0-smoothstep(.10,.5,abs(UV.x-.5));
        alpha=edge*smoothstep(0.0,.04,UV.y)*(1.0-smoothstep(.65,1.0,UV.y));}
    else {float radius=length((UV-.5)*2.0);alpha=pow(max(0.0,1.0-radius),kind==0?2.5:1.5);
        if(kind==0)alpha+=.35*(1.0-smoothstep(.02,.20,radius));}
    color=vec4(tint,min(1.0,alpha*opacity));
})"};
    lightProgram=glCreateProgram();
    for(int i=0;i<2;++i){auto shader=glCreateShader(i?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);ironShaderSource(shader,sources[i]);glCompileShader(shader);GLint ok=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
        if(!ok){char log[4096]{};glGetShaderInfoLog(shader,sizeof log,nullptr,log);glDeleteShader(shader);throw std::runtime_error(std::string("Searchlight shader: ")+log);}
        glAttachShader(lightProgram,shader);glDeleteShader(shader);}
    glLinkProgram(lightProgram);GLint ok=0;glGetProgramiv(lightProgram,GL_LINK_STATUS,&ok);if(!ok)throw std::runtime_error("Searchlight shader link failed");
    lightTransform=glGetUniformLocation(lightProgram,"transform");lightViewModel=glGetUniformLocation(lightProgram,"viewModel");lightDepthRange=glGetUniformLocation(lightProgram,"depthRange");
    lightColour=glGetUniformLocation(lightProgram,"tint");lightOpacity=glGetUniformLocation(lightProgram,"opacity");lightKind=glGetUniformLocation(lightProgram,"kind");
    constexpr float quad[]={0,0,1,0,1,1,0,0,1,1,0,1};
    glGenVertexArrays(1,&lightVao);glBindVertexArray(lightVao);glGenBuffers(1,&lightVbo);glBindBuffer(GL_ARRAY_BUFFER,lightVbo);glBufferData(GL_ARRAY_BUFFER,sizeof quad,quad,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),nullptr);
}
void Renderer::drawSearchlights(const World& w,const avp::Matrix& camera){
    if(!w.visibilityRules())return;
    using namespace avp;
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE);glDepthMask(GL_FALSE);
    glUseProgram(lightProgram);glBindVertexArray(lightVao);Vec3 colour=jaguarCRY(0x88e0);
    glUniform3f(lightColour,colour.x,colour.y,colour.z);glUniform2f(lightDepthRange,worldNear,worldFar);
    auto draw=[&](const Matrix& model,int kind,float opacity){
        auto transform=multiply(vp,model),view=multiply(worldView,model);
        glUniformMatrix4fv(lightTransform,1,GL_FALSE,transform.data());glUniformMatrix4fv(lightViewModel,1,GL_FALSE,view.data());
        glUniform1i(lightKind,kind);glUniform1f(lightOpacity,opacity);glDrawArrays(GL_TRIANGLES,0,6);
    };
    for(const auto& p:radarPilots(w)){
        if(p.playerSlot>=lightStates.size()||!lightStates[p.playerSlot]||!visibilityInRange(w,visibilityObserver,p.position+Vec3{0,6,0},true))continue;
        auto beam=lightBeams[p.playerSlot];beam.origin=searchlightOrigin(p);Vec3 delta=beam.end-beam.origin;float distance=length(delta);if(distance<.5f)continue;
        auto axis=unit(delta),side=unit(cross(axis,{0,1,0}),{1,0,0});
        Matrix corona=camera;corona[12]=beam.origin.x;corona[13]=beam.origin.y;corona[14]=beam.origin.z;
        draw(multiply(corona,multiply(translation(-.25f,-.25f,0),scaling(.5f,.5f,1))),0,.85f);
        for(Vec3 right:{side,unit(cross(axis,side))}){
            float width=std::min(16.f,distance*.25f);Matrix ribbon=identity;
            ribbon[0]=right.x*width;ribbon[1]=right.y*width;ribbon[2]=right.z*width;
            ribbon[4]=delta.x;ribbon[5]=delta.y;ribbon[6]=delta.z;auto start=beam.origin-right*(width*.5f);ribbon[12]=start.x;ribbon[13]=start.y;ribbon[14]=start.z;
            draw(ribbon,1,environmentView.night||environmentView.density>0?.035f:.012f);
        }
        if(beam.hit){auto right=unit(cross(beam.normal,{0,1,0}),{1,0,0}),up=unit(cross(beam.normal,right));Matrix patch=identity;
            float size=std::clamp(distance*.06f,1.f,6.f);auto origin=beam.end+beam.normal*.04f-right*size*.5f-up*size*.5f;
            patch[0]=right.x*size;patch[1]=right.y*size;patch[2]=right.z*size;patch[4]=up.x*size;patch[5]=up.y*size;patch[6]=up.z*size;
            patch[12]=origin.x;patch[13]=origin.y;patch[14]=origin.z;draw(patch,2,.26f);
        }
    }
    glDepthMask(GL_TRUE);glDisable(GL_BLEND);glUseProgram(program);glBindVertexArray(vao);
}
}
