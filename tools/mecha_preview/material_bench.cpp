// Linux offscreen benchmark. Shader substitution exists only in this executable.
#include "render.hpp"
#include "texture_mips.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/session.hpp"
#include <SDL.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string_view>

using namespace iron;
using namespace avp;
namespace fs=std::filesystem;
namespace {
std::string extension;
}
extern "C" void __real_glShaderSource(GLuint,GLsizei,const GLchar* const*,const GLint*);
extern "C" void __wrap_glShaderSource(GLuint shader,GLsizei count,const GLchar* const* code,const GLint* lengths){
    if(!extension.empty()&&count==1){
        std::string source(code[0],lengths&&lengths[0]>=0?size_t(lengths[0]):std::char_traits<char>::length(code[0]));
        bool world=source.find("vec3 settledSurface(")!=std::string::npos;
        if(world||source.find("uniform float instrumentPower[5]")!=std::string::npos){
            source.insert(source.find('\n')+1,world?"#define STUDY_WORLD\n#define main stockMain\n":"#define main stockMain\n");
            source+="\n"+extension;
            const char* patched=source.c_str();
            __real_glShaderSource(shader,1,&patched,nullptr);return;
        }
    }
    __real_glShaderSource(shader,count,code,lengths);
}
namespace iron {
struct MaterialStudy {
    static void run(Renderer& renderer,World& world,const fs::path& study,std::string_view mode){
        const bool prototype=mode=="prototype";
        constexpr int width=1440,height=1600,frames=24;
        GLuint fbo,colour;
        glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glGenRenderbuffers(1,&colour);glBindRenderbuffer(GL_RENDERBUFFER,colour);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,width,height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,colour);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Incomplete benchmark framebuffer");
        glViewport(0,0,width,height);
        Frontend menu;menu.screen=Screen::Playing;
        const auto head=world.eye();
        const auto orientation=compose(world.cockpitOrientation(),angles(0,-.13f));
        const auto camera=poseMatrix({{orientation.x,orientation.y,orientation.z,orientation.w},{head.x,head.y,head.z}});
        constexpr float vertical=.65f;
        const float horizontal=std::atan(std::tan(vertical)*width/height);
        const auto lens=projection({-horizontal,horizontal,vertical,-vertical},.05f,3000);
        renderer.resetCockpit();renderer.updateCockpit(world,6,true);
        renderer.draw(world,lens,camera,camera,false,&menu); // Initialize the lazy world program.
        std::array programs{renderer.worldProgram,renderer.cockpitProgram};
        std::array<GLint,2> eyeLocations{};
        GLuint surface=0;
        if(mode=="emission-zero"){
            glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,renderer.originalTexture);
            std::vector<unsigned char> atlas(256*256*4);
            glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,atlas.data());
            for(size_t i=3;i<atlas.size();i+=4)atlas[i]=0;
            uploadModelAtlas(atlas.data());glActiveTexture(GL_TEXTURE0);
        }
        if(prototype){
            std::ifstream file(study/"surface.rgba",std::ios::binary);
            std::vector<unsigned char> pixels(256*256*4);
            if(!file.read(reinterpret_cast<char*>(pixels.data()),pixels.size()))throw std::runtime_error("Missing material atlas");
            glGenTextures(1,&surface);glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D,surface);
            uploadModelAtlas(pixels.data());

            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glActiveTexture(GL_TEXTURE0);
            for(size_t i=0;i<programs.size();++i){
                auto program=programs[i];glUseProgram(program);
                eyeLocations[i]=glGetUniformLocation(program,"studyEye");
                if(eyeLocations[i]<0)throw std::runtime_error("Material substitution failed");
                glUniform1i(glGetUniformLocation(program,"surfaceAtlas"),4);
                glUniform3f(glGetUniformLocation(program,"studyLight"),-.53f,.74f,-.42f);
                glUniform1i(glGetUniformLocation(program,"features"),7);
            }
        }
        auto stereo=[&]{
            for(float offset:{-.032f,.032f}){
                const auto eye=multiply(camera,translation(offset,0,0));
                if(prototype)for(size_t i=0;i<programs.size();++i){
                    glUseProgram(programs[i]);glUniform3f(eyeLocations[i],eye[12],eye[13],eye[14]);
                }
                renderer.draw(world,lens,eye,camera,false,&menu);
            }
        };
        std::cerr<<glGetString(GL_RENDERER)<<" / "<<glGetString(GL_VERSION)<<" / "<<world.targets.size()<<" targets\n";
        for(auto weather:{Weather::Clear,Weather::Rain}){
            world.environment.weather=weather;
            for(int i=0;i<12;++i)stereo();
            glFinish();
            std::array<GLuint,frames> queries;glGenQueries(frames,queries.data());
            for(auto query:queries){glBeginQuery(GL_TIME_ELAPSED,query);stereo();glEndQuery(GL_TIME_ELAPSED);}
            std::cout<<(weather==Weather::Clear?"clear":"rain");
            for(auto query:queries){GLuint64 ns;glGetQueryObjectui64v(query,GL_QUERY_RESULT,&ns);std::cout<<" "<<double(ns)/1e6;}
            std::cout<<"\n";glDeleteQueries(frames,queries.data());
            // Hash a resolved eye outside the query to verify that the two shader
            // paths render different images of the same deterministic scene.
            std::vector<unsigned char> pixels(width*height*4);
            glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            if(const char* prefix=std::getenv("MECHA_MATERIAL_CAPTURE")){
                const auto suffix=weather==Weather::Clear?"-clear.rgba":"-rain.rgba";
                std::ofstream image(std::string(prefix)+suffix,std::ios::binary);
                image.exceptions(std::ios::badbit|std::ios::failbit);
                image.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
            }
            uint64_t hash=14695981039346656037ULL;
            for(auto byte:pixels){hash^=byte;hash*=1099511628211ULL;}
            std::cerr<<"Resolved eye hash "<<std::hex<<hash<<std::dec<<"\n";
        }
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Benchmark GL error");
        glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteTextures(1,&surface);glDeleteRenderbuffers(1,&colour);glDeleteFramebuffers(1,&fbo);
    }
};
}
int main(int argc,char** argv)try{
    if(argc!=4&&(argc!=5||std::string_view(argv[4])!="--contacts"))
        throw std::runtime_error("Usage: mecha-material-bench <assets> <study directory> stock|emission|emission-zero|prototype [--contacts]");
    fs::path study=argv[2];std::string_view mode=argv[3];
    if(mode!="emission"&&mode!="emission-zero"&&mode!="stock"&&mode!="prototype")throw std::runtime_error("Unknown shader mode");
    if(mode=="prototype"){
        std::ifstream file(study/"surface.glsl");extension.assign(std::istreambuf_iterator<char>(file),{});
        if(extension.empty())throw std::runtime_error("Missing prototype shader");
    }
    if(SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    auto window=SDL_CreateWindow("Material benchmark",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    if(!window||!SDL_GL_CreateContext(window))throw std::runtime_error(SDL_GetError());
    SDL_GL_SetSwapInterval(0);rpg::setOriginalAssetRoot(argv[1]);
    World world;rpg::buildMissionEnvironment(world,4271,3,500,{},0,3,0,5,-1,0);
    world.mission=0;world.missionStatus=MissionStatus::Active;
    world.position=world.missionEntry+Vec3{-3,0,-9};world.position.y=world.surfaceHeight(world.position.x,world.position.z);
    world.chassisYaw=std::atan2(-11.f,19.f);world.elapsed=world.environmentSeconds=180;
    world.environment={Lighting::Day,Weather::Clear,7,false};
    if(argc==5){
        const auto orient=world.cockpitOrientation();
        const auto forward=rotate(orient,{0,0,-1}),side=rotate(orient,{1,0,0});
        for(int row=0;row<3;++row)for(int col=-1;col<=1;++col){
            Target target;
            target.origin=world.position+forward*(18.f+row*17.f)+side*(col*(7.f+row*4.f));
            target.origin.y=world.surfaceHeight(target.origin.x,target.origin.z);
            target.active=true;target.spawnType=3;target.health=100;target.yaw=world.chassisYaw+pi;
            rpg::applyOriginalModel(target,col==0?"kaiju_obliterator":"terran_mammoth");
            world.targets.push_back(std::move(target));
        }
    }
    {Renderer renderer(fs::path(argv[1])/"cockpit");MaterialStudy::run(renderer,world,study,mode);}
    SDL_DestroyWindow(window);SDL_Quit();
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}
