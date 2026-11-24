// Offline surface-material experiment. The live client never links this target.
#include "render.hpp"
#include "texture_mips.hpp"
#include "rpg/original_assets.hpp"
#include <SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace fs=std::filesystem;
using namespace avp;
namespace iron {
namespace {
constexpr int width=480,height=400;
std::string contents(const fs::path& path){
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Missing "+path.string());
    return {std::istreambuf_iterator<char>(file),{}};
}
std::string attachedSource(GLuint program,GLenum stage){
    GLuint shaders[2];GLsizei count=0;
    glGetAttachedShaders(program,2,&count,shaders);
    for(int i=0;i<count;++i){
        GLint type,size;glGetShaderiv(shaders[i],GL_SHADER_TYPE,&type);
        if(GLenum(type)!=stage)continue;
        glGetShaderiv(shaders[i],GL_SHADER_SOURCE_LENGTH,&size);
        std::string source(size,'\0');GLsizei length=0;
        glGetShaderSource(shaders[i],size,&length,source.data());source.resize(length);
        return source;
    }
    throw std::runtime_error("Native model shader unavailable");
}
GLuint compile(GLenum kind,const std::string& source){
    GLuint shader=glCreateShader(kind);ironShaderSource(shader,source.c_str());glCompileShader(shader);
    GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[4096]{};glGetShaderInfoLog(shader,sizeof log,nullptr,log);throw std::runtime_error(log);}
    return shader;
}
GLuint makeProgram(const std::string& vertex,const std::string& fragment){
    auto v=compile(GL_VERTEX_SHADER,vertex),f=compile(GL_FRAGMENT_SHADER,fragment);
    auto p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);
    GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[4096]{};glGetProgramInfoLog(p,sizeof log,nullptr,log);throw std::runtime_error(log);}
    return p;
}
void capture(const fs::path& path){
    std::vector<unsigned char> pixels(width*height*3);glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    std::ofstream file(path,std::ios::binary);file.exceptions(std::ios::badbit|std::ios::failbit);
    file<<"P6\n"<<width<<" "<<height<<"\n255\n";
    for(int y=height-1;y>=0;--y)file.write(reinterpret_cast<const char*>(pixels.data()+y*width*3),width*3);
}
}

// Query the driver so missing levels, stale animation frames and collapsed
// array layers fail even when the close-up gallery happens to sample level 0.
static int verifyChain(GLenum target,GLuint texture,const std::string& name){
    if(!texture)throw std::runtime_error("Missing texture: "+name);
    glBindTexture(target,texture);
    auto dimension=[&](int level,GLenum axis){
        GLint value;glGetTexLevelParameteriv(target,level,axis,&value);return value;
    };
    int width=dimension(0,GL_TEXTURE_WIDTH),height=dimension(0,GL_TEXTURE_HEIGHT);
    const int layers=dimension(0,GL_TEXTURE_DEPTH);
    if(width<=0||height<=0)throw std::runtime_error("Missing base level: "+name);
    GLint minFilter,baseLevel,maxLevel;
    glGetTexParameteriv(target,GL_TEXTURE_MIN_FILTER,&minFilter);
    glGetTexParameteriv(target,GL_TEXTURE_BASE_LEVEL,&baseLevel);
    glGetTexParameteriv(target,GL_TEXTURE_MAX_LEVEL,&maxLevel);
    if(baseLevel!=0 || minFilter==GL_NEAREST || minFilter==GL_LINEAR)
        throw std::runtime_error("Mipmap sampling disabled: "+name);
    for(int level=0;;++level){
        if(level>maxLevel)throw std::runtime_error("Truncated mip chain: "+name);
        if(dimension(level,GL_TEXTURE_WIDTH)!=width||dimension(level,GL_TEXTURE_HEIGHT)!=height||
           dimension(level,GL_TEXTURE_DEPTH)!=layers)throw std::runtime_error("Incomplete mip chain: "+name);
        if(width==1&&height==1)return level;
        width=std::max(1,width/2);height=std::max(1,height/2);
    }
}
struct MaterialStudy {
    static void verifyMipmaps(Renderer& renderer,World& world,GLuint surface){
        world.proceduralTerrain=true;world.environment.weather=Weather::Rain;
        world.elapsed=world.environmentSeconds=180;world.movementBounds={0,0,14,10};
        world.interiorRegions.push_back({{12,8,14,10},0,42});
        renderer.drawWorld(world);
        verifyChain(GL_TEXTURE_2D_ARRAY,renderer.terrainTexture,"terrain");
        verifyChain(GL_TEXTURE_2D,renderer.originalTexture,"PSX atlas");
        if(surface)verifyChain(GL_TEXTURE_2D,surface,"study material atlas");
        for(auto texture:renderer.worldTextures)verifyChain(GL_TEXTURE_2D,texture,"world atlas");
        for(auto texture:renderer.pickupTextures)verifyChain(GL_TEXTURE_2D,texture,"pickup atlas");
        int last=verifyChain(GL_TEXTURE_2D,renderer.shelterTexture,"shelter");
        float roof;glGetTexImage(GL_TEXTURE_2D,last,GL_RED,GL_FLOAT,&roof);
        if(roof!=42)throw std::runtime_error("Odd-edge shelter roof lost in reduction");
        for(uint16_t fill:{uint16_t(0xf800),uint16_t(0x1f)}){
            std::array<uint16_t,35> frame;frame.fill(fill);
            renderer.uploadBriefingHeader(7,5,frame.data());
            last=verifyChain(GL_TEXTURE_2D,renderer.textures.at("briefing-header-frame").id,"briefing");
            std::array<unsigned char,4> pixel;
            glGetTexImage(GL_TEXTURE_2D,last,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
            if(pixel[0]!=(fill==0xf800?255:0)||pixel[2]!=(fill==0x1f?255:0))
                throw std::runtime_error("Stale briefing mip");
        }
        for(const auto& [name,texture]:renderer.textures)verifyChain(GL_TEXTURE_2D,texture.id,name);
        std::cout<<"Verified "<<renderer.textures.size()+7+bool(surface)<<" complete mip chains on the GPU\n";
    }
    static void checkLiveTextures(Renderer& renderer,const fs::path& pack){
        World world;world.targets.clear();world.rpgPrototype=true;world.mission=0;
        Target target;rpg::applyOriginalModel(target,"terran_mammoth");world.targets.push_back(target);
        renderer.prepareWorldDrawCandidates=true;
        verifyMipmaps(renderer,world,0);
        const auto expected=contents(pack/"textures/psx-atlas.rgba");
        if(expected.size()!=256*256*4)throw std::runtime_error("Invalid packed texture size");
        glBindTexture(GL_TEXTURE_2D,renderer.originalTexture);
        std::string gpu(expected.size(),'\0');
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,gpu.data());
        if(gpu!=expected||glGetError()!=GL_NO_ERROR)throw std::runtime_error("GPU albedo/emission atlas mismatch");
    }
    static void run(Renderer& renderer,const fs::path& study,const fs::path& out){
        fs::create_directories(out);
        World world;world.targets.clear();world.rpgPrototype=true;world.mission=0;
        for(auto id:{"terran_mammoth","ship_reactor","kaiju_obliterator"}){
            Target target;rpg::applyOriginalModel(target,id);target.health=100;
            world.targets.push_back(target);
        }
        renderer.prepareWorldDrawCandidates=true;
        renderer.drawWorld(world); // Use the same loader and VAOs as live scenery and enemies.
        std::array<GLuint,2> stocks{renderer.cockpitProgram,renderer.worldProgram},variants{};
        for(size_t i=0;i<stocks.size();++i){
            const auto vert=attachedSource(stocks[i],GL_VERTEX_SHADER);
            auto frag=attachedSource(stocks[i],GL_FRAGMENT_SHADER);
            frag.insert(frag.find('\n')+1,i?"#define STUDY_WORLD\n#define main stockMain\n":"#define main stockMain\n");
            frag+="\n"+contents(study/"surface.glsl");
            std::string name=i?"world":"model";
            std::ofstream(out/(name+".vert"))<<vert;std::ofstream(out/(name+".frag"))<<frag;
            variants[i]=makeProgram(vert,frag);
        }
        const auto pixels=contents(study/"surface.rgba");
        if(pixels.size()!=256*256*4)throw std::runtime_error("Invalid packed material atlas");
        GLuint texture;glGenTextures(1,&texture);glActiveTexture(GL_TEXTURE4);glBindTexture(GL_TEXTURE_2D,texture);
        uploadModelAtlas(pixels.data());

        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,renderer.originalTexture);
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width,height);
        glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
        glClearColor(.064f,.082f,.09f,1.f);
        for(size_t i=0;i<4;++i){
            const bool exterior=i<world.targets.size();
            const std::string id=exterior?world.targets[i].generatorAsset:"chassis_striker";
            if(exterior)renderer.uploadTargetMesh(world,i,false);
            const auto& mesh=exterior?renderer.worldModels.at(i).model():renderer.models.at(id);
            if(!mesh.vao||mesh.count==0)throw std::runtime_error("No uploaded triangles for "+id);
            const auto stock=stocks[exterior],variant=variants[exterior];
            const Vec3 mid=(mesh.low+mesh.high)*.5f,extent=mesh.high-mesh.low;
            const float size=std::max({extent.x,extent.y,extent.z});
            const auto pose=translation(-mid.x,-mid.y,-mid.z);
            auto render=[&](const char* label,int flags,float exposure,float angle,float eyeShift=0.f,bool shiftCamera=true){
                auto program=flags<0?stock:variant;
                auto uniform=[&](const char* name){return glGetUniformLocation(program,name);};
                const Vec3 back=unit({std::sin(angle),.23f,-std::cos(angle)});
                const Vec3 right=unit(cross({0,1,0},back)),up=cross(back,right);
                const Vec3 optical=back*(size*1.89f);
                const Vec3 eye=optical+right*eyeShift,camera=shiftCamera?eye:optical;
                const Matrix view=inverseRigid({right.x,right.y,right.z,0,up.x,up.y,up.z,0,
                    back.x,back.y,back.z,0,camera.x,camera.y,camera.z,1});
                const float halfY=.34f,halfX=std::atan(std::tan(halfY)*width/height);
                const auto lens=projection({-halfX,halfX,halfY,-halfY},.05f,size*8);
                const auto transform=multiply(lens,multiply(view,pose));
                glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glUseProgram(program);
                glUniformMatrix4fv(uniform("transform"),1,GL_FALSE,transform.data());
                glUniformMatrix4fv(uniform("worldPose"),1,GL_FALSE,pose.data());
                glUniform3f(uniform("eye"),0,0,8);glUniform3f(uniform("envTint"),1,1,1);
                glUniform1f(uniform("envGain"),exposure);glUniform4f(uniform("envFog"),.064f,.082f,.09f,0);
                glUniform1i(uniform("bakedLighting"),0);glUniform4f(uniform("lampColor"),0,0,0,0);
                glUniform1i(uniform("psxAtlas"),1);glUniform1i(uniform("surfaceAtlas"),4);
                glUniform1i(uniform("terrainSurfaces"),2);glUniform1i(uniform("shelter"),3);
                glUniform1i(uniform("receivesDeposits"),0);
                glUniform3f(uniform("studyEye"),eye.x,eye.y,eye.z);
                glUniform3f(uniform("studyLight"),-.53f,.74f,-.42f);glUniform1i(uniform("features"),flags);
                glBindVertexArray(mesh.vao);glDrawArrays(GL_TRIANGLES,0,mesh.count);
                capture(out/(id+"-"+label+".ppm"));
            };
            render("stock",-1,1.f,.49f);render("control",0,1.f,.49f);
            render("normal",1,1.f,.49f);render("specular",2,1.f,.49f);
            render("emission",4,1.f,.49f);render("combined",7,1.f,.49f);
            render("night-off",3,.18f,.49f);render("night-on",7,.18f,.49f);
            render("left",7,1.f,.49f,-.032f);render("right",7,1.f,.49f,.032f);
            // Fixed projection isolates highlight motion from parallax.
            render("view-control",0,1.f,.49f,size*.75f,false);
            render("view-specular",2,1.f,.49f,size*.75f,false);
            std::cout<<id<<" "<<mesh.count/3<<" triangles\n";
        }
        verifyMipmaps(renderer,world,texture);
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("GL error in surface probe");
        glDeleteTextures(1,&texture);for(auto program:variants)glDeleteProgram(program);
    }
};
}

int main(int argc,char** argv){
    const bool check=argc==3 && std::string_view(argv[2])=="--check-live-textures";
    if(argc!=4 && !check){std::cerr<<"Usage: mecha-material-study <published asset root> <study dir> <output dir>\n";return 2;}
    try{
        if(SDL_Init(SDL_INIT_VIDEO)!=0)throw std::runtime_error(SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
        auto* window=SDL_CreateWindow("Surface material study",0,0,iron::width,iron::height,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        if(!window)throw std::runtime_error(SDL_GetError());
        auto context=SDL_GL_CreateContext(window);if(!context)throw std::runtime_error(SDL_GetError());
#ifdef _WIN32
        glewExperimental=GL_TRUE;
        if(glewInit()!=GLEW_OK)throw std::runtime_error("GLEW initialization failed");
        glGetError(); // GLEW can probe unsupported extensions while initializing a core context.
#endif
        std::cout<<glGetString(GL_RENDERER)<<" / "<<glGetString(GL_VERSION)<<"\n";
        iron::rpg::setOriginalAssetRoot(argv[1]);
        {
            iron::Renderer renderer(fs::path(argv[1])/"cockpit");
            if(check)iron::MaterialStudy::checkLiveTextures(renderer,argv[1]);
            else iron::MaterialStudy::run(renderer,argv[2],argv[3]);
        }
        SDL_GL_DeleteContext(context);SDL_DestroyWindow(window);SDL_Quit();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
