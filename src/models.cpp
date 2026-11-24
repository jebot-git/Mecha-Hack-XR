#include "render.hpp"
#include "texture_mips.hpp"
#include "rpg/model_appearance.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>
namespace iron {
using namespace avp;
static Matrix orient(Quaternion q,Vec3 p) {return poseMatrix({{q.x,q.y,q.z,q.w},{p.x,p.y,p.z}});}
void Renderer::loadModels(const std::filesystem::path& root) {
    std::map<std::string,std::vector<float>> modelData;
    // The shared material atlas adds no per-model textures or draw calls.
    // Ordinary engine installations without the original pack use a white fallback.
    std::vector<unsigned char> atlas(256*256*4,255);
    const auto atlasPath=root.parent_path()/"textures/psx-atlas.rgba";
    bool emissiveAlpha=false;
    if(std::filesystem::exists(atlasPath)) {
        std::ifstream file(atlasPath,std::ios::binary|std::ios::ate);
        if(!file||file.tellg()!=std::streamoff(atlas.size()))throw std::runtime_error("Invalid PSX atlas dimensions");
        file.seekg(0);if(!file.read(reinterpret_cast<char*>(atlas.data()),atlas.size()))throw std::runtime_error("Truncated PSX atlas");
        if(std::ifstream metadata(atlasPath.parent_path()/"manifest.json");metadata){
            nlohmann::json layout;metadata>>layout;
            emissiveAlpha=layout.value("alpha_usage",std::string{})=="emission";
        }
    }
    // Legacy art used opaque alpha. The white fallback also has no emission.
    if(!emissiveAlpha)for(size_t i=3;i<atlas.size();i+=4)atlas[i]=0;
    glGenTextures(1,&originalTexture);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,originalTexture);
    uploadModelAtlas(atlas.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glUseProgram(cockpitProgram);glUniform1i(glGetUniformLocation(cockpitProgram,"psxAtlas"),1);glActiveTexture(GL_TEXTURE0);
    std::ifstream manifestFile(root/"manifest.json");if(!manifestFile)throw std::runtime_error("Decode original models with tools/decode_models.py first");nlohmann::json manifest;manifestFile>>manifest;
    if(manifest.at("schema")!=1||manifest.at("source_block")!=4)throw std::runtime_error("Invalid original model manifest");
    const auto appearance=rpg::loadModelAppearance(root);
    pickupTypes=manifest.at("pickup_types").get<std::array<unsigned,32>>();for(auto type:pickupTypes)if(type>4)throw std::runtime_error("Invalid pickup model mapping");
    std::ifstream rigFile(root/"rig.json");nlohmann::json rig;rigFile>>rig;
    if(rig.at("schema")!=1||rig.at("held").size()!=decodedRig.muzzle.size())throw std::runtime_error("Invalid hand rig");
    decodedRig.trackingScale=rig.at("tracking_scale");decodedRig.upperLength=rig.at("upper_length");decodedRig.foreLength=rig.at("fore_length");
    for(float v:{decodedRig.trackingScale,decodedRig.upperLength,decodedRig.foreLength})if(!std::isfinite(v)||v<.1f||v>10)throw std::runtime_error("Invalid arm scale");
    std::map<std::string,float> alignments;
    for(const auto& held:rig.at("held")){float a=held.at("alignment_yaw");if(!std::isfinite(a)||std::abs(a)>pi)throw std::runtime_error("Invalid held-model alignment");alignments.emplace(held.at("model").get<std::string>(),a);}
    auto pivot=rig.at("source_pivot").get<std::array<float,3>>();
    for(float v:pivot)if(!std::isfinite(v)||std::abs(v)>4096)throw std::runtime_error("Invalid hand pivot");
    for(size_t i=0;i<decodedRig.muzzle.size();++i){auto a=rig.at("held")[i].at("muzzle").get<std::array<float,3>>();decodedRig.muzzle[i]={a[0],a[1],a[2]};if(!std::isfinite(length(decodedRig.muzzle[i]))||length(decodedRig.muzzle[i])>10)throw std::runtime_error("Invalid muzzle");}
    if(std::ifstream arsenal(root.parent_path()/"arsenal-manifest.json");arsenal){
        nlohmann::json registry;arsenal>>registry;
        if(registry.at("schema")!=1||!registry.at("assets").is_array()||registry.at("assets").size()>64)
            throw std::runtime_error("Invalid arsenal mesh registry");
        for(const auto &row:registry.at("assets")){
            const auto name=row.at("id").get<std::string>();
            if(!name.starts_with("arsenal_")||std::filesystem::path(name).filename()!=name||
               manifest.at("models").contains(name)||row.at("file")!="meshes/"+name+".json")
                throw std::runtime_error("Invalid arsenal mesh entry");
            manifest["models"][name]={{"file",name+".json"},{"native_metres",true},{"budget",row.at("budget")}};
        }
    }
    nlohmann::json cockpitArms;
    if(std::ifstream file(root/"cockpit-arms.json");file){
        nlohmann::json library;file>>library;
        cockpitArms=std::move(library.at("pieces"));
        if(library.at("schema")!=1||!cockpitArms.is_object()||cockpitArms.size()!=rpg::chassisIds.size()*6)
            throw std::runtime_error("Invalid cockpit arm library");
        for(const char *chassis:rpg::chassisIds)for(const char *side:{"L","R"})for(const char *part:{"upper","fore","hand"}){
            const auto key=std::string("cockpit.")+chassis+"."+side+"."+part;
            if(!cockpitArms.contains(key)||manifest.at("models").contains(key))
                throw std::runtime_error("Missing or duplicate cockpit arm piece: "+key);
            manifest["models"][key]={{"file",key+".json"},{"native_metres",true},{"budget",4096}};
        }
    }
    for(const auto& [name,record]:manifest.at("models").items()) {
        std::string filename=record.at("file");if(std::filesystem::path(filename).filename()!=filename)throw std::runtime_error("Invalid model filename");
        const bool native=record.value("native_metres",false);
        auto shared=root.parent_path()/"is2/shared/models"/filename;
        const auto file=native?root.parent_path()/"meshes"/filename:std::filesystem::exists(shared)?shared:root/filename;
        nlohmann::json j;
        if(cockpitArms.contains(name))j=std::move(cockpitArms.at(name));
        else{std::ifstream f(file);f>>j;}
        if(native&&(j.at("id")!=name||j.at("units")!="metres"||j.at("faces").size()>record.at("budget").get<size_t>()))
            throw std::runtime_error("Invalid budget or units for arsenal mesh: "+name);
        if(j.at("vertices").size()>4096||j.at("faces").size()>4096)throw std::runtime_error("Original model exceeds budget");
        rpg::applyModelAppearance(j,appearance,name,100.f);
        std::vector<Vec3> positions;
        Vec3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
        for(const auto& v:j.at("vertices")){auto a=v.get<std::array<float,3>>();for(float c:a)if(!std::isfinite(c)||std::abs(c)>32768)throw std::runtime_error("Invalid model vertex");Vec3 p{a[0],a[1],a[2]};positions.push_back(p);lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
        if(positions.size()<3)throw std::runtime_error("Empty model");
        bool segment=name.find("upper-arm")!=std::string::npos||name.find("forearm")!=std::string::npos;
        bool held=alignments.contains(name);
        bool handPreview=name=="shotgun"||name=="heavy-machinegun";
        bool shoulder=name=="left-shoulder"||name=="right-shoulder";
        for(auto& p:positions) {
            if(held||handPreview)p=rotate(angles(alignments.at(handPreview?"mounted-"+name+"-a":name)),{-(p.x-pivot[0])*.01f,(p.z-pivot[2])*.01f,(p.y-pivot[1])*.01f});
            else if(segment)p={(p.x-(lo.x+hi.x)/2)*.01f,-(p.z-(lo.z+hi.z)/2)*.01f,(p.y-hi.y)/std::max(1.f,hi.y-lo.y)};
            else if(shoulder)p=(p-(lo+hi)*.5f)*.01f;
            else if(name=="iron-soldier")p={p.x*.01f,p.y*.01f,-p.z*.01f};
            else if(!native)p=p*.01f;
        }
        Model asset;asset.low={1e9f,1e9f,1e9f};asset.high={-1e9f,-1e9f,-1e9f};
        for(auto p:positions){asset.low={std::min(asset.low.x,p.x),std::min(asset.low.y,p.y),std::min(asset.low.z,p.z)};asset.high={std::max(asset.high.x,p.x),std::max(asset.high.y,p.y),std::max(asset.high.z,p.z)};}
        std::vector<float> data;
        for(const auto& face:j.at("faces")) {
            auto indices=face.at("indices").get<std::vector<int>>();if(indices.size()<3||indices.size()>32)throw std::runtime_error("Invalid original polygon size");
            for(int i:indices)if(i<0||size_t(i)>=positions.size())throw std::runtime_error("Invalid original polygon index");
            auto rgb=face.at("color").get<std::array<float,3>>();for(float& c:rgb){if(!std::isfinite(c)||c<0||c>255)throw std::runtime_error("Invalid original material");c=std::pow(c/255,2.2f);}
            std::vector<std::array<float,2>> uv(indices.size());
            bool textured=face.contains("texture");
            if(textured){
                if(face.at("texture")!="psx"||!std::filesystem::exists(atlasPath))throw std::runtime_error("Missing model texture");
                uv=face.at("uv").get<decltype(uv)>();
                if(uv.size()!=indices.size())throw std::runtime_error("Invalid model UV count");
                for(auto p:uv)for(float v:p)if(!std::isfinite(v)||v<0||v>1)throw std::runtime_error("Invalid model UV");
            }
            for(size_t i=1;i+1<indices.size();++i) {
                Vec3 a=positions[indices[0]],b=positions[indices[i]],c=positions[indices[i+1]],n=cross(b-a,c-a);
                if(length(n)<.000001f){if(name!="iron-soldier")continue;n={0,1,0};}else n=unit(n);
                for(size_t k:{size_t(0),i,i+1}){auto p=positions[indices[k]];data.insert(data.end(),{p.x,p.y,p.z,n.x,n.y,n.z,rgb[0],rgb[1],rgb[2],0,uv[k][0],uv[k][1],textured?1.f:0.f});}
            }
        }
        if(data.empty())throw std::runtime_error("Original model has no triangles");
        uploadModelVertices(asset,data);models.emplace(name,asset);
        if(!cockpitArms.contains(name))modelData.emplace(name,std::move(data));
    }
    std::ifstream animationFile(root/"animation-rigs.json");nlohmann::json animation;
    // Expansion metadata extends the same native WalkerRig schema. Original
    // player rigs and source references remain independent of the expansion.
    if(animationFile)animationFile>>animation;
    else {
        // Existing installations already have the verified player membership
        // table. Keep those bones usable before optional IS2 rig regeneration.
        std::ifstream bodyFile(root/"iron-soldier.json");nlohmann::json body;bodyFile>>body;
        constexpr int parents[]={-1,-1,1,2,-1,4,5,0,7,8,9,0,11,12,13,0};
        const char* roles[]={"torso","right-hip","right-knee","right-foot","left-hip","left-knee","left-foot","rigid","rigid","rigid","rigid","rigid","rigid","rigid","rigid","rigid"};
        nlohmann::json parts=nlohmann::json::array();std::vector<std::pair<int,int>> ranges;int offset=0;float floor=1e9f;
        auto point=[&](size_t i){auto v=body.at("vertices").at(i).get<std::array<float,3>>();return Vec3{v[0]*.01f,v[1]*.01f,-v[2]*.01f};};
        for(auto& v:body.at("vertices"))floor=std::min(floor,v[1].get<float>()*.01f);
        for(auto& face:body.at("faces")){int count=(int(face.at("indices").size())-2)*3;ranges.emplace_back(offset,count);offset+=count;}
        if(manifest.at("segments").size()!=16)throw std::runtime_error("Missing player animation membership");
        for(size_t i=0;i<16;++i){auto& group=manifest.at("segments")[i];Vec3 low{1e9f,1e9f,1e9f},high{-1e9f,-1e9f,-1e9f},pivot{};nlohmann::json draw=nlohmann::json::array();
            for(auto& index:group.at("vertices")){auto p=point(index.get<size_t>());low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};}
            if(group.at("vertices").empty())low=high={};
            if(!group.at("pivot_records").empty()){auto& ref=group.at("pivot_records")[0];pivot=(point(ref[1].get<size_t>())+point(ref[2].get<size_t>()))*.5f;}
            for(auto& face:group.at("faces")){auto [start,count]=ranges.at(face.get<size_t>());draw.push_back({start,count});}
            parts.push_back({{"parent",parents[i]},{"role",roles[i]},{"arm",i>=7&&i<=10?0:i>=11&&i<=14?1:-1},{"pivot",{pivot.x,pivot.y,pivot.z}},{"low",{low.x,low.y,low.z}},{"high",{high.x,high.y,high.z}},{"faces",group.at("faces")},{"ranges",draw}});
        }
        animation={{"schema",1},{"rigs",{{"iron-soldier",{{"floor",floor},{"mesh_vertices",offset},{"parts",parts}}}}}};
    }
    for(const char *library:{"expansion-animation-rigs.json","manual-player-rigs.json"}){
        std::ifstream expansionFile(root/library);
        if(!expansionFile)continue;
        nlohmann::json expansion;expansionFile>>expansion;
        if(expansion.at("schema")!=1)throw std::runtime_error("Invalid expansion animation schema");
        for(const auto &[name,data]:expansion.at("rigs").items()){
            if(animation["rigs"].contains(name))throw std::runtime_error("Expansion overrides an existing animation rig");
            animation["rigs"][name]=data;
        }
    }
    std::ifstream redesignFile(root/"mecha-redesign-player-rigs.json");
    const bool hasRedesign=bool(redesignFile);
    if(hasRedesign){nlohmann::json redesign;redesignFile>>redesign;
        if(redesign.at("schema")!=1||redesign.at("rigs").size()!=8)throw std::runtime_error("Invalid redesigned player animation library");
        for(const auto &[name,data]:redesign.at("rigs").items()){
            auto base=name.ends_with("_alt")?name.substr(0,name.size()-4):name;
            if((base!="chassis_brawler"&&base!="chassis_scout"&&base!="chassis_striker"&&base!="chassis_titan")||!animation["rigs"].contains(name))throw std::runtime_error("Unknown redesigned player rig");
            animation["rigs"][name]=data;
        }
    }
    // Derive chassis-specific tracked arm pieces from the same authored body
    // triangles. Equipment remains independently attached at the tracked wrist.
    if(hasRedesign&&cockpitArms.empty()){
        for(const auto &[name,description]:animation.at("rigs").items()){
            if(!name.starts_with("chassis_"))continue;
            std::map<std::string,std::vector<float>> pieces;
            for(const auto &part:description.at("parts")){
                if(!part.contains("tracked_segment"))continue;
                const auto segment=part.at("tracked_segment").get<std::string>();
                const int arm=part.value("arm",-1);
                if(arm<0||arm>1||(segment!="shoulder"&&segment!="upper-arm"&&segment!="forearm"))throw std::runtime_error("Invalid chassis tracked arm segment");
                auto &data=pieces[name+"-"+(arm?"right-":"left-")+segment];
                const auto &source=modelData.at(name);
                for(const auto &range:part.at("ranges")){
                    auto r=range.get<std::array<size_t,2>>();
                    if(r[0]%3||r[1]%3||r[0]>source.size()/13||r[1]>source.size()/13-r[0])throw std::runtime_error("Invalid tracked arm triangle range");
                    data.insert(data.end(),source.begin()+r[0]*13,source.begin()+(r[0]+r[1])*13);
                }
            }
            if(pieces.size()!=6)throw std::runtime_error("Redesigned chassis requires six tracked arm segments");
            for(auto &[piece,data]:pieces){
                if(data.empty())throw std::runtime_error("Empty tracked chassis arm");
                Vec3 low{1e9f,1e9f,1e9f},high{-1e9f,-1e9f,-1e9f};
                for(size_t i=0;i<data.size();i+=13){Vec3 p{data[i],data[i+1],data[i+2]};low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};}
                const auto center=(low+high)*.5f;const bool shoulder=piece.ends_with("shoulder");
                const float span=std::max(.01f,high.y-low.y);
                Model asset;asset.low={1e9f,1e9f,1e9f};asset.high={-1e9f,-1e9f,-1e9f};
                for(size_t i=0;i<data.size();i+=13){
                    Vec3 p{data[i],data[i+1],data[i+2]};
                    if(shoulder)p=p-center;
                    else{
                        p={p.x-center.x,-(p.z-center.z),(p.y-high.y)/span};
                        auto normal=unit(Vec3{data[i+3],-data[i+5],data[i+4]*span});
                        data[i+3]=normal.x;data[i+4]=normal.y;data[i+5]=normal.z;
                    }
                    data[i]=p.x;data[i+1]=p.y;data[i+2]=p.z;
                    asset.low={std::min(asset.low.x,p.x),std::min(asset.low.y,p.y),std::min(asset.low.z,p.z)};asset.high={std::max(asset.high.x,p.x),std::max(asset.high.y,p.y),std::max(asset.high.z,p.z)};
                }
                uploadModelVertices(asset,data);models.emplace(piece,asset);
            }
        }
    }
    {
        nlohmann::json sockets;
        if(std::ifstream file(root/"booster-sockets.json");file){
            file>>sockets;
            if(sockets.at("schema")!=1||!sockets.at("rigs").is_object()||sockets.at("rigs").size()!=8)
                throw std::runtime_error("Invalid booster socket library");
            sockets=sockets.at("rigs").get<nlohmann::json::object_t>();
        }
        if(animation.at("schema")!=1)throw std::runtime_error("Invalid walker animation metadata");
        for(auto& [name,description]:animation.at("rigs").items()){
            ModelRig parsed;parsed.rig.floor=description.at("floor");parsed.vertices=description.at("mesh_vertices");
            if(!std::isfinite(parsed.rig.floor)||parsed.vertices<3||parsed.vertices>32768||description.at("parts").size()>32)throw std::runtime_error("Invalid walker animation budget");
            GLint packedFirst=0;std::vector<bool> covered(size_t(parsed.vertices));
            for(auto& value:description.at("parts")){
                WalkerPart part;part.parent=value.at("parent");part.arm=value.value("arm",-1);
                auto vec=[&](const char* key){auto p=value.at(key).get<std::array<float,3>>();for(float v:p)if(!std::isfinite(v)||std::abs(v)>500)throw std::runtime_error("Invalid walker pivot/bounds");return Vec3{p[0],p[1],p[2]};};
                part.pivot=vec("pivot");part.low=vec("low");part.high=vec("high");part.empty=value.at("faces").empty();
                if(part.low.x>part.high.x||part.low.y>part.high.y||part.low.z>part.high.z)throw std::runtime_error("Inverted walker bounds");
                const std::string roles[]={"rigid","torso","left-hip","left-knee","left-foot","right-hip","right-knee","right-foot","head"};
                auto found=std::find(std::begin(roles),std::end(roles),value.at("role").get<std::string>());if(found==std::end(roles))throw std::runtime_error("Invalid walker joint");part.joint=WalkerJoint(found-std::begin(roles));
                if(part.parent>=int(parsed.rig.parts.size())||part.parent< -1||part.arm< -1||part.arm>1)throw std::runtime_error("Invalid walker parent");
                if(value.value("name",std::string{})=="backpack"&&!part.empty)
                    parsed.backpack=int(parsed.rig.parts.size());
                parsed.rig.parts.push_back(part);std::vector<std::pair<GLint,GLsizei>> ranges;
                GLint count=0;
                for(auto& range:value.at("ranges")){auto r=range.get<std::array<int,2>>();if(r[0]<0||r[1]<0||r[0]%3||r[1]%3||r[0]+r[1]>parsed.vertices)throw std::runtime_error("Invalid animated face range");ranges.emplace_back(r[0],r[1]);count+=r[1];for(int n=r[0];n<r[0]+r[1];++n){if(covered[size_t(n)])throw std::runtime_error("Overlapping animated faces");covered[size_t(n)]=true;}}
                parsed.ranges.push_back(std::move(ranges));
                parsed.drawRanges.emplace_back(packedFirst,count);packedFirst+=count;
            }
            if(packedFirst!=parsed.vertices)throw std::runtime_error("Incomplete walker animation coverage");
            if(sockets.contains(name)||description.contains("booster_sockets")){
                const auto &ports=sockets.contains(name)?sockets.at(name):description.at("booster_sockets");
                if(parsed.backpack<0||!ports.is_array()||ports.size()!=2)
                    throw std::runtime_error("Missing booster attachment joint: "+name);
                const auto &backpack=parsed.rig.parts[size_t(parsed.backpack)];
                for(size_t side=0;side<parsed.boosters.size();++side){
                    auto &port=parsed.boosters[side];
                    auto vector=[&](const char *key){
                        auto values=ports[side].at(key).get<std::array<float,3>>();
                        return Vec3{values[0],values[1],values[2]};
                    };
                    port={vector("position"),vector("normal")};
                    const auto position=port.position;
                    const float normalSize=length(port.normal);
                    if(!std::isfinite(length(position))||!(normalSize>.99f&&normalSize<1.01f)||
                       port.normal.z<.25f||position.x<backpack.low.x-.01f||position.x>backpack.high.x+.01f||
                       position.y<backpack.low.y-.01f||position.y>backpack.high.y+.01f||
                       position.z<backpack.low.z-.01f||position.z>backpack.high.z+.01f)
                        throw std::runtime_error("Booster outside authored backpack: "+name);
                    port.normal=unit(port.normal);
                }
            }else parsed.backpack=-1;
            walkerRigs.emplace(name,std::move(parsed));
        }
    }
    // Pack each bone's triangles contiguously once. Animation is a handful of
    // rigid draws, not one draw per source polygon or per-frame CPU skinning.
    for(const auto& [name,rig]:walkerRigs){
        auto source=modelData.find(name);if(source==modelData.end())continue;
        const auto& vertices=source->second;
        if(vertices.size()!=size_t(rig.vertices)*13)throw std::runtime_error("Player animation/model mismatch: "+name);
        std::vector<float> packed;packed.reserve(vertices.size());
        for(auto& part:rig.ranges)for(auto [first,count]:part)packed.insert(packed.end(),vertices.begin()+size_t(first)*13,vertices.begin()+size_t(first+count)*13);
        glBindBuffer(GL_ARRAY_BUFFER,models.at(name).vbo);glBufferData(GL_ARRAY_BUFFER,packed.size()*sizeof(float),packed.data(),GL_STATIC_DRAW);
    }
}
std::string Renderer::soldierModel(const Pilot& pilot)const{
    if(size_t(pilot.rpgChassis)>=rpg::chassisIds.size())return "iron-soldier";
    std::string body=std::string("chassis_")+rpg::chassisIds[size_t(pilot.rpgChassis)];
    if(pilot.rpgHullVariant&&models.contains(body+"_alt"))return body+"_alt";
    return models.contains(body)?body:"iron-soldier";
}
void Renderer::drawSoldier(const Pilot& pilot,float tilt,const Matrix* camera,float time){
    const auto body=soldierModel(pilot);
    auto found=walkerRigs.find(body);
    if(found==walkerRigs.end()){
        model(body,multiply(translation(pilot.position.x,pilot.position.y+(pilot.rpgChassis<0?5.54f:0.f),pilot.position.z),rotation(pilot.chassisYaw,tilt)));return;
    }
    const auto& asset=models.at(body);const auto& data=found->second;if(asset.count!=data.vertices)throw std::runtime_error("Player animation/model mismatch");
    auto pose=pilot.rpgVehicleBlend>0&&!pilot.lifeSpent?poseVehicle(data.rig,pilot.rpgVehicleBlend):poseWalker(data.rig,pilot.gaitPhase,pilot.lifeSpent?0:pilot.walkingSpeed,pilot.lifeSpent?0:pilot.torsoYaw,pilot.lifeSpent?0:wrap(pilot.aimYaw-pilot.facing()),pilot.lifeSpent?0:pilot.aimPitch,
        pilot.lifeSpent?WalkerMotion{}:WalkerMotion{pilot.rpgChassis,pilot.rpgThrust,pilot.rpgBodyRecoil,pilot.rpgLanding});
    auto base=orient(compose(pilot.gravity.frame,angles(pilot.chassisYaw,tilt)),pilot.bodyPoint(-data.rig.floor));
    if(pilot.rpgKnockdown.active(time)){
        const auto fall=rpg::sampleKnockdown(pilot.rpgKnockdown,time);
        const auto upright=orient(pilot.gravityFacing(),pilot.bodyPoint(12));
        const auto pivot=multiply(upright,multiply(orient(fall.orientation,fall.offset),inverseRigid(upright)));
        base=multiply(pivot,base);
    }
    glUseProgram(cockpitProgram);glUniform1i(cockpitBaked,0);applyEnvironment(cockpitProgram,environmentView.modelGain);glBindVertexArray(asset.vao);glUniform3f(cockpitEye,0,0,8);
    for(size_t i=0;i<data.rig.parts.size();++i){auto& part=data.rig.parts[i];if(part.empty||(part.arm>=0&&(!pilot.armAvailable(part.arm)||(pilot.arms[size_t(part.arm)].tracked&&!pilot.lifeSpent&&pilot.rpgVehicleBlend<.01f))))continue;
        auto matrix=multiply(base,orient(pose.parts[i].orientation,pose.parts[i].offset));Model bound=asset;bound.low=part.low;bound.high=part.high;if(!visibleWorld(bound,matrix))continue;
        applyEnvironment(cockpitProgram,environmentView.modelGain,&matrix);auto transform=multiply(vp,matrix);glUniformMatrix4fv(cockpitTransform,1,GL_FALSE,transform.data());auto [first,count]=data.drawRanges[i];glDrawArrays(GL_TRIANGLES,first,count);
    }
    glUseProgram(program);glBindVertexArray(vao);
    if(data.backpack>=0&&pilot.rpgChassis>=0&&!pilot.lifeSpent&&pilot.rpgVehicleBlend<.01f){
        const size_t index=size_t(data.backpack);
        const auto &animated=pose.parts[index];
        const auto matrix=multiply(base,orient(animated.orientation,animated.offset));
        const float power=std::clamp(length(pilot.rpgThrust),0.f,1.f);
        for(const auto &socket:data.boosters){
            const auto axis=cross({0,0,1},socket.normal);
            const float scale=1.f/std::sqrt(2.f*(1.f+socket.normal.z));
            const Quaternion aligned{axis.x*scale,axis.y*scale,axis.z*scale,(1.f+socket.normal.z)*scale};
            const float diameter=pilot.rpgChassis==3?.35f:pilot.rpgChassis==1?.17f:.25f;
            const auto collar=multiply(matrix,orient(aligned,socket.position+socket.normal*.045f));
            const Vec3 ring{.11f,.17f,.20f};
            box(multiply(collar,scaling(diameter,diameter*.60f,.15f)),ring);
            box(multiply(collar,multiply(translation(0,0,.13f),scaling(diameter*.69f,diameter*.43f,.03f))),
                Vec3{.035f,.052f,.059f}+Vec3{.10f,.51f,.85f}*power);
            if(camera&&power>.015f){
                const auto port=point(collar,{0,0,.18f});
                const Vec3 from{port[0],port[1],port[2]};
                const Vec3 flow=unit(rotate(angles(pilot.facing()),pilot.rpgThrust)*-1.f-Vec3{0,.31f,0});
                const float extent=(pilot.rpgChassis==3?8.3f:6.9f)*power;
                const float opacity=shapeOpacity;
                shapeOpacity=.48f*power;
                drawEnemyStreak(from,from+flow*extent,diameter*.87f,{.17f,.43f,1.f},*camera,1);
                shapeOpacity=.90f*power;
                drawEnemyStreak(from,from+flow*extent*.70f,diameter*.43f,{.57f,.89f,1.f},*camera,.86f);
                shapeOpacity=opacity;
            }
        }
        if(!enemyEffectVertices.empty()){
            glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
            flushEnemyEffects();glDepthMask(GL_TRUE);glDisable(GL_BLEND);
        }
    }
}
void Renderer::model(const std::string& name,const Matrix& pose,float phase,int handling) {
    if(name.starts_with("arsenal_")){
        const auto weapon=weapons.find(name);
        if(weapon!=weapons.end()){
            drawWeapon(weapon->second,pose,phase,handling);
            return;
        }
    }
    auto found=models.find(name);if(found==models.end())throw std::runtime_error("Missing decoded model: "+name);
    if(!visibleWorld(found->second,pose))return;
    glUseProgram(cockpitProgram);glUniform1i(cockpitBaked,0);applyEnvironment(cockpitProgram,environmentView.modelGain,&pose);glBindVertexArray(found->second.vao);auto m=multiply(vp,pose);glUniformMatrix4fv(cockpitTransform,1,GL_FALSE,m.data());glUniform3f(cockpitEye,0,0,8);glDrawArrays(GL_TRIANGLES,0,found->second.count);
    glUseProgram(program);glBindVertexArray(vao);
}
static Matrix bone(Vec3 start,Vec3 end,Quaternion roll) {
    Vec3 z=unit(start-end),up=rotate(roll,{0,1,0});
    Vec3 hint=std::abs(z.y)<.9f?Vec3{0,1,0}:Vec3{1,0,0};
    Vec3 x=unit(cross(up,z),unit(cross(hint,z)));Vec3 y=unit(cross(z,x));
    float size=length(end-start);return {x.x,x.y,x.z,0,y.x,y.y,y.z,0,z.x*size,z.y*size,z.z*size,0,start.x,start.y,start.z,1};
}
void Renderer::drawArms(const World& world) {
    if(world.rpgVehicleBlend>.01f)return;
    const auto savedProjection=vp;
    if(world.rpgPrototype&&cockpitFx.insertion.kind==rpg::InsertionKind::Water&&cockpitFx.insertion.active&&!world.lifeSpent)
        vp=multiply(vp,translation(0,cockpitFx.insertion.cameraOffsetY,0));
    const auto torso=world.cockpitOrientation();
    const auto root=world.cockpitOrigin();
    auto mounted=[&](Vec3 local){return root+rotate(torso,local-Vec3{0,12,0});};
    const std::string prefix=world.rpgChassis>=0?soldierModel(world)+"-":"";
    std::string studyChassis;
    if(size_t(world.rpgChassis)<rpg::chassisIds.size())
        studyChassis=std::string("cockpit.")+rpg::chassisIds[size_t(world.rpgChassis)];
    for(int h=0;h<2;++h) {
        const auto& a=world.arms[h];if(!world.armAvailable(h)||!a.tracked)continue;
        const std::string side=h?"right-":"left-";
        auto armModel=[&](const char* segment){auto name=prefix+side+segment;return models.contains(name)?name:side+segment;};
        const auto study=studyChassis+(h?".R.":".L.");
        const bool authored=!studyChassis.empty()&&models.contains(study+"hand");
        if(authored){
            model(study+"upper",bone(a.shoulder,a.elbow,torso));
            model(study+"fore",bone(a.elbow,a.wrist,a.grip));
            model(study+"hand",orient(a.aim,a.wrist));
        }else{
            model(armModel("shoulder"),orient(torso,a.shoulder));
            model(armModel("upper-arm"),bone(a.shoulder,a.elbow,torso));
            model(armModel("forearm"),bone(a.elbow,a.wrist,a.grip));
        }
        static const char* heldNames[]={"manipulator","mounted-rifle-a","mounted-cutter-a","mounted-shotgun-a","mounted-heavy-machinegun-a"};
        const auto& equipped=world.rpgModelNames[h];
        if(authored&&equipped.empty()&&world.weapons[h]==Weapon::Empty)continue;
        std::string held=world.rpgChassis>=0&&!equipped.empty()?equipped:heldNames[heldModel(world.weapons[h])];
        model(held,multiply(orient(a.aim,a.wrist),scaling(h?1.f:-1.f,1,1)),world.rpgWeaponPhase[h],world.rpgWeaponHandling[h]);

    }
    for(int h=0;h<2;++h)if(!world.rpgStashedModels[h].empty()){
        float side=h?1.f:-1.f;
        Vec3 rack=mounted({side*2.7f,9.5f,1.6f});
        model(world.rpgStashedModels[h],multiply(orient(compose(torso,angles(0,pi/2)),rack),scaling(side,1,1)),0,world.rpgWeaponHandling[h]);
    }
    if(world.rpgChassis>=0)for(int slot=LeftShoulder;slot<MountCount;++slot){
        const auto& name=world.rpgModelNames[slot];if(name.empty())continue;
        float side=slot%2?1.f:-1.f;
        Vec3 p=mounted({side*2.3f,slot<LeftHip?8.2f:4.4f,.4f});
        model(name,multiply(orient(torso,p),scaling(side,1,1)),world.rpgWeaponPhase[slot],world.rpgWeaponHandling[slot]);
    }
    vp=savedProjection;
}
void Renderer::modelPreview(const std::string& name,const Matrix& pose,float size,float yaw,int handling) {
    const auto weapon=weapons.find(name);
    const auto m=weapon==weapons.end()?models.at(name):weapon->second.fittedBounds(weapon->second.stretchFor(handling));
    Vec3 span=m.high-m.low,center=(m.low+m.high)*.5f;
    float scale=size/std::max({span.x,span.y,span.z,.001f});
    model(name,multiply(pose,multiply(rotation(yaw,.12f),multiply(scaling(scale,scale,scale),translation(-center.x,-center.y,-center.z)))),0,handling);
}
}
