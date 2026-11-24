#include "render.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <fstream>
#include <stdexcept>

namespace iron {
using namespace avp;

void Renderer::uploadModelVertices(Model& buffer,const std::vector<float>& vertices){
    buffer.count=GLsizei(vertices.size()/13);
    glGenVertexArrays(1,&buffer.vao);
    glBindVertexArray(buffer.vao);
    glGenBuffers(1,&buffer.vbo);
    glBindBuffer(GL_ARRAY_BUFFER,buffer.vbo);
    glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STATIC_DRAW);
    constexpr GLint width[]{3,3,3,1,2,1};
    size_t offset=0;
    for(GLuint i=0;i<6;++i){
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i,width[i],GL_FLOAT,GL_FALSE,13*sizeof(float),reinterpret_cast<void*>(offset*sizeof(float)));
        offset+=size_t(width[i]);
    }
}

void Renderer::loadWeapons(const std::filesystem::path& root){
    const auto index=root/"weapon-workings.json";
    if(!std::filesystem::exists(index))return;
    std::ifstream meta(index);
    nlohmann::json library;
    meta>>library;
    const auto& entries=library.at("weapons");
    if(library.at("schema")!=1||!entries.is_object()||entries.empty()||entries.size()>128)
        throw std::runtime_error("Invalid weapon assembly library");
    std::ifstream geometry(root/"weapon-workings.bin",std::ios::binary|std::ios::ate);
    const std::streamoff bytes=geometry.tellg();
    if(bytes<=0||bytes>64*1024*1024||bytes%(13*sizeof(float)))
        throw std::runtime_error("Invalid weapon assembly vertex buffer");
    std::vector<float> vertices(size_t(bytes)/sizeof(float));
    geometry.seekg(0);
    if(!geometry.read(reinterpret_cast<char*>(vertices.data()),bytes))
        throw std::runtime_error("Incomplete weapon assembly vertex buffer");
    for(float &v:vertices){
        if constexpr(std::endian::native!=std::endian::little){
            auto *b=reinterpret_cast<unsigned char*>(&v);
            std::reverse(b,b+sizeof(v));
        }
        if(!std::isfinite(v))throw std::runtime_error("Invalid weapon vertex");
    }
    const auto vertexCount=int64_t(vertices.size()/13);
    for(const auto& [id,record]:entries.items()){
        if(!id.starts_with("arsenal_")||!models.contains(id))
            throw std::runtime_error("Unknown weapon assembly: "+id);
        auto vector=[](const nlohmann::json& row){
            const auto v=row.get<std::array<float,3>>();
            return Vec3{v[0],v[1],v[2]};
        };
        WeaponAssembly assembly;
        assembly.bounds.low=vector(record.at("low"));
        assembly.bounds.high=vector(record.at("high"));
        assembly.socket=record.at("socket");
        assembly.reach=record.at("reach");
        const auto span=assembly.bounds.high-assembly.bounds.low;
        const auto& parts=record.at("parts");
        if(!(span.x>0&&span.y>0&&span.z>0&&length(span)<100&&assembly.socket>=0&&assembly.socket<20&&
             assembly.reach>=0&&assembly.reach<100)||!parts.is_array()||parts.empty()||parts.size()>256)
            throw std::runtime_error("Invalid weapon extent or parts: "+id);
        for(const auto& row:parts){
            WeaponPart part;
            const auto first=row.at("first").get<int64_t>(),count=row.at("count").get<int64_t>();
            if(first<0||first%3||count<3||count%3||first>vertexCount-count)
                throw std::runtime_error("Invalid weapon triangle range: "+id);
            part.first=GLint(first);
            part.count=GLsizei(count);
            if(row.contains("frames")){
                const auto& frames=row.at("frames");
                if(frames.size()!=25)throw std::runtime_error("Invalid weapon pose count: "+id);
                for(const auto& sample:frames){
                    auto pose=sample.get<Matrix>();
                    if(!std::all_of(pose.begin(),pose.end(),[](float f){return std::isfinite(f);}))
                        throw std::runtime_error("Invalid weapon pose: "+id);
                    part.frames.push_back(pose);
                }
            }
            if(row.contains("glow")){
                part.glow=row.at("glow").get<std::vector<float>>();
                if(part.glow.size()!=25||!std::all_of(part.glow.begin(),part.glow.end(),[](float f){return f>=0&&f<=1;}))
                    throw std::runtime_error("Invalid weapon emission samples: "+id);
            }
            assembly.parts.push_back(std::move(part));
        }
        weapons.emplace(id,std::move(assembly));
    }
    uploadModelVertices(weaponBuffer,vertices);
}

namespace {
// Normalizing the interpolated basis keeps the rotary barrels and chain links
// rigid between the authored samples, even on a high-refresh headset.
Matrix samplePose(const std::vector<Matrix>& frames,int frame,int next,float t){
    if(frames.empty())return identity;
    const auto& a=frames[frame];
    if(t==0)return a;
    const auto& b=frames[next];
    auto column=[&](int i){
        return Vec3{std::lerp(a[i],b[i],t),std::lerp(a[i+1],b[i+1],t),std::lerp(a[i+2],b[i+2],t)};
    };
    const Vec3 x=unit(column(0)),z=unit(cross(x,column(4))),y=cross(z,x),p=column(12);
    return {x.x,x.y,x.z,0,y.x,y.y,y.z,0,z.x,z.y,z.z,0,p.x,p.y,p.z,1};
}
}

float Renderer::WeaponAssembly::stretchFor(int handling)const{
    return reach>0?.03f*std::clamp(handling,-1,3):0.f;
}

Renderer::Model Renderer::WeaponAssembly::fittedBounds(float stretch)const{
    auto fitted=bounds;
    fitted.low.z+=std::min(0.f,fitted.low.z+socket)*stretch;
    return fitted;
}

void Renderer::drawWeapon(const WeaponAssembly& weapon,const Matrix& pose,float phase,int handling){
    const float stretch=weapon.stretchFor(handling);
    if(!visibleWorld(weapon.fittedBounds(stretch),pose))return;
    const float time=std::isfinite(phase)?std::clamp(phase,0.f,1.f)*24:0;
    const int frame=int(time),next=std::min(frame+1,24);
    const float t=time-float(frame);
    glUseProgram(cockpitProgram);
    glUniform1i(cockpitBaked,0);
    glUniform1i(weaponPass,1);
    glUniform2f(weaponStretch,weapon.socket,stretch);
    applyEnvironment(cockpitProgram,environmentView.modelGain,&pose);
    const auto clip=multiply(vp,pose);
    glUniformMatrix4fv(cockpitTransform,1,GL_FALSE,clip.data());
    glUniform3f(cockpitEye,0,0,8);
    glBindVertexArray(weaponBuffer.vao);
    // Rest poses share the same transform. Submit contiguous pieces together.
    GLint first=0;
    GLsizei count=0;
    Matrix current=identity;
    float gain=1;
    auto flush=[&]{
        glUniformMatrix4fv(weaponPose,1,GL_FALSE,current.data());
        glUniform1f(weaponGlow,gain);
        glDrawArrays(GL_TRIANGLES,first,count);
    };
    for(const auto& part:weapon.parts){
        const auto transform=samplePose(part.frames,frame,next,t);
        const float glow=part.glow.empty()?1:std::lerp(part.glow[frame],part.glow[next],t);
        if(count&&(transform!=current||glow!=gain||part.first!=first+count)){
            flush();
            count=0;
        }
        if(!count){
            first=part.first;
            current=transform;
            gain=glow;
        }
        count+=part.count;
    }
    flush();
    glUniform1i(weaponPass,0);
    glUseProgram(program);
    glBindVertexArray(vao);
}
}
