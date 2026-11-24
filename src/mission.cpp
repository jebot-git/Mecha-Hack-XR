#include "simulation.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <map>
#include <stdexcept>
namespace iron {
using Json=nlohmann::json;
static Json readJson(const std::filesystem::path& p){std::ifstream f(p);if(!f)throw std::runtime_error("Missing decoded gameplay file: "+p.string());Json j;f>>j;return j;}
static Vec3 original(const Json& a){return {a.at(0).get<float>()*.01f,a.at(1).get<float>()*.01f,-a.at(2).get<float>()*.01f};}
static Vec3 rgb(const Json& a){return {a.at(0).get<float>()/255,a.at(1).get<float>()/255,a.at(2).get<float>()/255};}
void World::loadMission(const std::filesystem::path& base,int index,int campaign,bool sharedAssets){
    renderIdentity=std::make_shared<const int>(0);
    assault=false;searchlightOn=false;wasSearchlight=false;environmentSeconds=-1;climate=campaign==1&&index==0?Climate::Desert:Climate::Temperate;environment={};missionPrototype=false;prototypeSeed=0;proceduralLayout=false;procedural={};proceduralPopulation={};proceduralTerrain=false;movementBounds={-1000,-1000,1000,1000};editGeometry().terrainPatches.clear();waterRegions.clear();waterCrossings.clear();editGeometry().waterMesh.clear();
    if(campaign<0||campaign>1)throw std::runtime_error("Invalid campaign");game=campaign;auto root=game?base/"is2":base;
    if(index<0||index>=(game?20:16))throw std::runtime_error("Invalid mission index");
    auto shared=base/"is2/shared/gameplay";
    auto catalog=readJson(!game&&sharedAssets&&std::filesystem::exists(shared/"missions.json")?shared/"missions.json":root/"gameplay/missions.json");if(catalog.at("schema")!=1)throw std::runtime_error("Invalid mission schema");
    targetSlots.fill(-1);actorDeaths.fill(0);objectiveRegions.clear();objectiveStage=0;
    const auto& m=catalog.at("missions").at(index);night=m.at("night");if(m.contains("objective_regions"))objectiveRegions=m.at("objective_regions").get<std::vector<std::array<float,4>>>();mission=index;missionStatus=MissionStatus::Active;
    const auto& tuning=m.at("difficulty");auto factors=tuning.at("multipliers").get<std::array<int,4>>();
    int factor=factors[difficulty==Difficulty::Hard?2:1];difficultyRatio=float(factor)/factors[1];
    int tuningIndex=0;for(const char* pair:{"projectile","impact","health"})for(float value:tuning.at(pair).get<std::array<float,2>>())difficultyValues[tuningIndex++]=value*factor*.1f;
    targets.clear();generators.clear();cargoFlights.clear();projectiles.clear();effects.clear();pickups.clear();sounds.clear();editGeometry().terrain.clear();destroyed=0;lifeSpent=false;ejectionTime=0;health=100;gaitPhase=0;damageUntil={};elapsed=0;missionFlags=0;counters={};missile={};
    position=original(m.at("spawn"));chassisYaw=-m.at("yaw").get<float>()*pi/1024;torsoYaw=0;horizon=m.at("horizon");
    groundColor=rgb(m.at("ground"));skyColor=rgb(m.at("sky"));
    loadDecorations(base);
    auto lv=m.at("light").get<std::array<float,3>>();float ll=std::hypot(lv[0],lv[1],lv[2]);
    missionLight=rotate(angles(-chassisYaw),{lv[0],lv[1],-lv[2]});
    std::array<int,3> light{};for(int k=0;k<3;++k)light[k]=int(lv[k]*256/ll);
    auto color=[&](int cry){int c=(cry>>12)&15,r=(cry>>8)&15,y=cry&255;auto& t=catalog.at("cry_table");return Vec3{float((t["red"][c][r].get<int>()*y)>>8)/255,float((t["green"][c][r].get<int>()*y)>>8)/255,float((t["blue"][c][r].get<int>()*y)>>8)/255};};
    std::map<std::string,Json> source;
    auto geometry=[&](const std::string& name,float yaw,int type){
        if(!source.contains(name))source[name]=readJson(!game&&sharedAssets&&std::filesystem::exists(shared/(name+".json"))?shared/(name+".json"):root/"gameplay"/(name+".json"));
        auto& j=source.at(name);auto mesh=std::make_shared<WorldMesh>();std::vector<Vec3> vertices;
        if(j.at("vertices").size()>4096||j.at("faces").size()>4096)throw std::runtime_error("Invalid world mesh budget");
        for(auto& p:j.at("vertices"))vertices.push_back(original(p));
        bool night=m.at("night").get<bool>()&&type!=22;
        auto intensity=[&](std::array<int,3> n,int shade,int bias){
            Vec3 normal=rotate(angles(yaw),{float(n[0]),float(n[1]),-float(n[2])});
            int dot=int(normal.x*light[0]+normal.y*light[1]-normal.z*light[2]);
            int illumination=dot>>3;bool negative=illumination<0;illumination=std::abs(illumination);if(negative)illumination>>=1;
            if(night){shade>>=1;bias>>=1;}
            return std::clamp(((illumination*shade)>>12)+bias,0,255);
        };
        std::vector<int> vertexLight(vertices.size(),-1);
        for(auto& record:j.at("lighting")){
            auto a=record.get<std::array<int32_t,4>>();unsigned v=uint32_t(a[1])&65535;
            if(v>=vertices.size())throw std::runtime_error("Invalid vertex lighting reference");
            vertexLight[v]=intensity({int16_t(uint32_t(a[0])>>16),int16_t(a[0]),int16_t(uint32_t(a[1])>>16)},a[2],a[3]);
        }
        // Some ducts have separate flat/Gouraud front and back polygons on
        // exactly the same vertices. Preserve both sides' lighting, but prevent
        // the hidden side from competing for depth (including after scaling).
        std::vector<bool> oneSided(j.at("faces").size(),false);
        std::map<std::vector<size_t>,std::vector<size_t>> coincidentFaces;
        for(size_t i=0;i<j.at("faces").size();++i){
            auto key=j.at("faces")[i].at("indices").get<std::vector<size_t>>();
            for(auto id:key)if(id>=vertices.size())throw std::runtime_error("World polygon index out of bounds");
            std::sort(key.begin(),key.end());
            auto current=j.at("normals").at(i).get<std::array<int,4>>();
            Vec3 normal=unit({float(current[0]),float(current[1]),float(current[2])});
            for(auto previous:coincidentFaces[key]){
                auto old=j.at("normals").at(previous).get<std::array<int,4>>();
                if(dot(normal,unit({float(old[0]),float(old[1]),float(old[2])}))<-.99f)
                    oneSided[i]=oneSided[previous]=true;
            }
            coincidentFaces[key].push_back(i);
        }
        // The Jaguar painter drew coplanar details in source order. A depth buffer
        // needs explicit separation; keep the original face colors and lighting.
        std::map<std::array<int,4>,unsigned> planeLayers;
        std::vector<std::pair<Vec3,float>> opaquePlanes;
        size_t faceIndex=0;
        for(auto& face:j.at("faces")){
            bool sided=oneSided[faceIndex];
            auto ids=face.at("indices").get<std::vector<size_t>>();for(auto v:ids)if(v>=vertices.size())throw std::runtime_error("World polygon index out of bounds");
            auto n=j.at("normals").at(faceIndex++).get<std::array<int,4>>();
            int group=face.at("group"),material=face.at("material");
            if(group==0)for(auto& replacement:m.at("replacements"))if(replacement[0]==type&&replacement[1]==material)material=replacement[2];
            int flat=intensity({n[0],n[1],n[2]},face.at("shade"),face.at("bias"));
            Vec3 normal{float(n[0]),float(n[1]),-float(n[2])};float normalLength=length(normal);
            Vec3 separation{};
            if(normalLength>0&&!ids.empty()){
                normal=normal*(1/normalLength);float distance=dot(normal,vertices[ids[0]]);
                std::array<int,4> plane{int(std::round(normal.x*1000)),int(std::round(normal.y*1000)),int(std::round(normal.z*1000)),int(std::round(distance*1000))};
                float offset=.003f*planeLayers[plane]++;
                // Retail textured panels can sit a few centimetres inside the shell:
                // its painter still overlaid them. Lift only these near-parallel decals.
                if(group==2)for(auto& [baseNormal,baseDistance]:opaquePlanes){
                    float alignment=dot(normal,baseNormal);if(alignment<.995f)continue;
                    float minimum=1e9f;for(auto v:ids)minimum=std::min(minimum,dot(baseNormal,vertices[v]));
                    float gap=baseDistance-minimum;if(gap>=-.12f&&gap<=.12f)offset=std::max(offset,(gap+.004f)/alignment);
                }
                if(sided)offset=0;
                separation=normal*offset;
                if(group==0)opaquePlanes.push_back({normal,distance+offset});
            }
            std::vector<WorldVertex> faceVertices;
            for(size_t k=0;k<ids.size();++k){
                auto v=ids[k];WorldVertex output{vertices[v]+separation,color(material|flat)};output.oneSided=sided?1:0;
                if(group==1){if(vertexLight[v]<0)throw std::runtime_error("Missing Gouraud vertex lighting");output.color=color(material|vertexLight[v]);}
                if(group==2){auto& uv=catalog.at("texture_uv").at(material);if(k>=uv.size())throw std::runtime_error("Texture vertex count mismatch");output.u=uv[k][0];output.v=uv[k][1];output.color={1,1,1};output.textured=1;}
                faceVertices.push_back(output);
            }
            for(size_t k=1;k+1<ids.size();++k){
                size_t a=k,b=k+1;
                // Native coordinates reverse source Z, so normalize the winding
                // against the decoded outward normal before using gl_FrontFacing.
                if(sided&&dot(cross(vertices[ids[a]]-vertices[ids[0]],vertices[ids[b]]-vertices[ids[0]]),normal)<0)std::swap(a,b);
                for(auto v:{size_t(0),a,b})mesh->push_back(faceVertices[v]);
            }
        }
        return mesh;
    };
    auto bounds=[](Target& t){Vec3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};for(auto& v:*t.mesh){auto p=rotate(angles(t.yaw),v.position);lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}t.center=t.origin+(lo+hi)*.5f;t.half=(hi-lo)*.5f;};
    for(auto& o:m.at("objects")){
        Target t;t.origin=original(o.at("position"));t.type=o.at("type");t.tag=o.at("tag");t.action=o.at("action");t.yaw=-o.at("yaw").get<float>()*pi/1024;
        auto& spec=catalog.at("objects").at(t.type);t.health=spec.at("health").get<float>()*.1f;
        if(!game&&index==2&&t.tag==32)t.health=200; // native grenade support tuning until collapse actors are ported
        t.mesh=geometry(spec.at("model"),t.yaw,t.type);t.wreck=geometry(spec.at("wreck"),t.yaw,t.type);bounds(t);
        if(spec.contains("destruction")&&spec["destruction"].contains("fragments")){
            auto& d=spec["destruction"];auto a=d["span"].get<std::array<float,3>>();t.fragmentSpan={a[0]*.01f,a[1]*.01f,a[2]*.01f};t.fragmentStep=d["step"].get<float>()*.01f;t.fragmentModels=d["fragments"].get<std::vector<unsigned>>();
        }
        if(!game){
        t.protectedTarget=(index==5||index==11)&&t.action==0x80010000u;
        if(t.tag==32&&index==0)++counters[0];
        if(t.tag==22&&index==3)++counters[0];
        if(index==12&&t.tag<128)++counters[1];
        if(index==15&&t.tag==33)++counters[1];
        if(index==10&&t.origin.x>=14.08f&&t.origin.x<=290.56f&&t.origin.z>=-71.68f&&t.origin.z<=102.4f)++counters[2];
        }
        int targetSlot=o.value("target_slot",255);if(targetSlot>0&&targetSlot<32){targetSlots[targetSlot]=int(targets.size());t.ai.identity=targetSlot;}
        targets.push_back(std::move(t));
    }
    const int initial[]={-1,0,2,-1,1,20,3,4,1,5,-1,1,-1,3,5,1};if(!game){if(initial[index]>=0)counters[0]=initial[index];if(index==3)counters[1]=4;}
    for(auto& polygon:m.at("terrain")){
        const auto& v=polygon.at("vertices");Vec3 c=rgb(polygon.at("color"));
        // These are flat ground overlays. The source's repeated 40 cm layer
        // offsets must not become raised, disconnected collision surfaces.
        for(size_t k=1;k+1<v.size();++k)for(size_t n:{size_t(0),k,k+1}){auto p=original(v[n]);p.y=0;editGeometry().terrain.push_back({p,c});}
    }
    indexTerrain();
    std::vector<int> spawnIndices(m.at("spawns").size(),-1);size_t spawnIndex=0;
    for(auto& s:m.at("spawns")){
        size_t sourceIndex=spawnIndex++;
        int type=s.at("type");if(type==4)continue; // invisible generators are retained in decoded records
        Target t;t.spawnType=type;t.origin=original(s.at("position"));t.yaw=-s.at("yaw").get<float>()*pi/1024;t.trigger=s.at("trigger");t.spawnFlags=s.at("flags");t.activationRange=s.at("range").get<float>()*.01f;t.active=false;
        auto p=s.at("parameters").get<std::vector<unsigned char>>();
        auto u16=[&](size_t n){return n+1<p.size()?unsigned(p[n])*256+p[n+1]:0u;};
        t.parameters=(u16(0)<<16)|u16(2);t.deathFlag=u16(4)&31;t.health=type==3?800:type==9?500:type==8?50:80;
        t.ai.targetSlot=int(t.parameters>>16);t.ai.identity=type==8?int(t.parameters>>16):-1;
        for(size_t k=0;k<t.ai.words.size();++k)t.ai.words[k]=u16(4+k*2);
        if(type==3){auto profile=m.at("mech_profile").get<std::array<unsigned,4>>();t.health=profile[0]*.1f;t.ai.words[0]=profile[1];t.ai.words[2]=profile[2];t.ai.words[3]=profile[3];}
        t.ai.respawnRange=s.at("respawn").get<float>()*10;
        std::string name=type==21&&game?"":catalog.at("dynamic_models").at(std::to_string(type)).get<std::string>();
        auto variant=[&](const std::string& key){name=catalog.at("variants").at(key).get<std::string>();};
        if(type==0)t.health=difficultyValues[4];
        if(type==1)t.health=difficultyValues[5];
        if(type==0&&(t.parameters&64)){variant("heavy-tank");t.health*=32;}
        if(type==1&&u16(12)==2)variant("helicopter-bomber");
        if(type==2||type==5)variant("gun-"+std::to_string((t.parameters&1)?std::min(3u,u16(8)+1):0));
        if(type==6)variant("transport-"+std::to_string(std::min(1u,u16(6))));
        if(type==10)t.health=150;
        if(!name.empty())t.mesh=geometry(name,t.yaw,-1);else t.mesh=std::make_shared<WorldMesh>();
        if(game){static const float hp[]={0,0,15,0,0,15,5,10,50,400,150,15,160,8,200,50,50,50,25,3000,1000,1,25};if(hp[type])t.health=hp[type];}
        // Type-specific count locations verified against the constructors, not a trailing-byte heuristic.
        int routeOffset=type==0||type==1?14:type==3?6:type==6?10:type==8?18:-1;
        if(game){if(type==9)routeOffset=34;if(type==12)routeOffset=26;if(type==14||type==22)routeOffset=14;if(type==15)routeOffset=10;if(type==17||type==18)routeOffset=6;}
        if(routeOffset>=0){size_t at=size_t(routeOffset);unsigned n=u16(at);
            if(n>64||at+2+n*8>p.size())throw std::runtime_error("Invalid AI route record");
            if(t.parameters&1)for(unsigned k=0;k<n;++k){auto number=[&](size_t q){uint32_t v=0;for(int c=0;c<4;++c)v=(v<<8)|p[q+c];return int32_t(v);};t.route.push_back({number(at+2+k*8)*.01f,t.origin.y,-number(at+6+k*8)*.01f});}
        }
        if(game&&type==20){t.route={original(s.at("destination"))};t.invulnerable=true;}
        t.speed=type==0?6:type==8?8:type==6?12:type==9?u16(12)*.01f:type==7?76:type==1?16:4;
        if(game)t.speed=type==14?15:type==17?14:type==18?65:type==12?32:type==22?22:t.speed;
        t.attackClock=0;t.protectedTarget=type==8&&(game?(index==4||index==8||index==18):index==13);
        if(!t.mesh->empty())bounds(t);else {t.center=t.origin;t.half={.1f,.1f,.1f};}t.ai.spawn=t.origin;t.ai.spawnYaw=t.yaw;t.ai.maxHealth=t.health;t.ai.lastHealth=t.health;
        t.ai.weaponYaw=t.yaw;t.ai.spawnDelay=type==9?float(u16(10)):0;t.ai.passes=type==7?u16(10):0;
        t.ai.burst=type==1?u16(10):type==0||type==2||type==5||type==10?u16(8):0;
        updateEnemyBounds(t);spawnIndices[sourceIndex]=int(targets.size());targets.push_back(std::move(t));
    }
    // Truck trailers are separate collision actors following the decoded coupling lengths.
    for(size_t i=0,n=targets.size();i<n;++i)if(targets[i].spawnType==8){
        int parent=int(i);float rear=targets[i].ai.words[0]*.01f;
        for(int k=2;k<7;++k){unsigned kind=targets[i].ai.words[k];if(!kind)break;if(kind>3)throw std::runtime_error("Invalid trailer variant");
            Target trailer;trailer.spawnType=game?23:11;trailer.generated=true;trailer.active=false;trailer.health=50;
            trailer.ai.parent=parent;trailer.ai.words[0]=unsigned((rear+(kind==2?1.58f:2.30f))*100);
            trailer.origin=targets[parent].origin- direction(targets[parent].yaw,0)*(trailer.ai.words[0]*.01f);trailer.yaw=targets[parent].yaw;
            trailer.mesh=geometry(catalog.at("variants").at("trailer-"+std::to_string(kind)),trailer.yaw,-1);
            bounds(trailer);updateEnemyBounds(trailer);parent=int(targets.size());targets.push_back(std::move(trailer));rear=kind==2?3.02f:4.84f;
        }
    }
    // Original generator routine $7A42: quota, simultaneous limit, template and burst cadence.
    for(size_t si=0;si<m.at("spawns").size();++si){auto& s=m.at("spawns")[si];if(s.at("type")!=4)continue;
        auto p=s.at("parameters").get<std::vector<unsigned char>>();
        auto u16=[&](size_t n){return n+1<p.size()?unsigned(p[n])*256+p[n+1]:0u;};
        unsigned templateIndex=u16(10);if(templateIndex>=spawnIndices.size()||spawnIndices[templateIndex]<0)continue;
        Generator g;g.source=int(si);g.prototype=targets[spawnIndices[templateIndex]];g.prototype.generated=true;g.prototype.active=true;g.prototype.ai.generator=int(generators.size());
        g.flags=(u16(0)<<16)|u16(2);g.remaining=u16(4);g.interval=u16(6)/256.f;g.burstInterval=u16(12)/256.f;g.burstSize=u16(14);g.burst=g.burstSize;
        g.origin=original(s.at("position"));g.range=s.at("range").get<float>()*.01f;g.trigger=s.at("trigger");
        unsigned capacity=std::clamp(u16(8)?u16(8):8u,1u,16u);
        for(unsigned k=0;k<capacity;++k){g.slots.push_back(targets.size());Target slot=g.prototype;slot.active=false;targets.push_back(std::move(slot));}
        generators.push_back(std::move(g));
    }
    if(game)for(size_t ci=0,carrierLimit=targets.size();ci<carrierLimit;++ci){
        if(targets[ci].spawnType!=12)continue;
        CargoFlight flight;flight.carrier=ci;auto words=targets[flight.carrier].ai.words;
        flight.release=words[2];flight.salvo=words[3];flight.interval=std::max(.04f,words[4]/256.f);flight.passes=words[5];flight.group=std::min(2u,words[10]);
        for(unsigned k=6;k<10&&words[k];++k){unsigned choice=words[k];if(choice>2)continue;unsigned source=words[choice-1];if(source>=spawnIndices.size()||spawnIndices[source]<0)throw std::runtime_error("Invalid IS2 air-drop template");Target payload=targets[spawnIndices[source]];payload.generated=true;payload.ai.generator=-1;payload.ai.cargoGroup=int(flight.group);flight.choices.push_back(payload);}
        if(!flight.choices.empty())for(int k=0;k<16;++k){Target slot=flight.choices[k%flight.choices.size()];slot.active=false;slot.ai.attachment=-1;flight.slots.push_back(targets.size());targets.push_back(std::move(slot));}
        cargoFlights.push_back(std::move(flight));
    }
    // Ships can carry a referenced spawn/generator; its anchor moves with the ship.
    for(size_t si=0;si<spawnIndices.size();++si)if(spawnIndices[si]>=0){auto& ship=targets[spawnIndices[si]];
        if(ship.spawnType!=9||!(ship.parameters&2))continue;
        unsigned child=ship.ai.words[2];if(child>=spawnIndices.size())throw std::runtime_error("Invalid ship attachment");
        if(spawnIndices[child]>=0){auto& actor=targets[spawnIndices[child]];actor.ai.attachment=spawnIndices[si];actor.ai.attachmentOffset=rotate(angles(-ship.yaw),actor.origin-ship.origin);}
        for(auto& g:generators)if(g.source==int(child)){g.prototype.ai.attachment=spawnIndices[si];g.prototype.ai.attachmentOffset=rotate(angles(-ship.yaw),g.prototype.origin-ship.origin);}
    }
    static const char* goals[]={"WAREHOUSES + SUPPLIES","CARGO SHIPS / 8 MIN","BRIDGE SUPPORTS","SUPPLY DUMP + SUPPLIES","ENEMY MECH","DEFEND HEADQUARTERS","TRUCKS + CAPTURE WEAPON","CARGO SHIPS / 12:30","FORTRESS + SUPPLIES","AIRFIELD + HELICOPTERS","FACTORY COMPLEX","DEFEND LABORATORY","INDUSTRIAL COMPLEX","PROTECT CONVOY","WARSHIPS","COMMAND CENTER"};objective=game?std::string("IS2 MISSION ")+std::to_string(index+1):goals[index];
    if(game)initIS2();
    missionBuildingCount=counters[2];
    resetTracking();
}
void World::applyMissionAction(uint32_t a){
    if(a&0x80000000u){unsigned type=(a>>16)&0x7fff,argument=a&65535;if(type==2&&argument<3)counters[argument]=std::max(0,counters[argument]-1);else if(type==1&&argument<32)missionFlags|=1u<<argument;else if(type==0&&argument<32)missionFlags&=~(1u<<argument);}
    else if(a>=0x1bba0&&a<=0x1bbd0&&(a-0x1bba0)%12==0){counters[0]=std::max(0,counters[0]-1);missionFlags|=1u<<((a-0x1bba0)/12);}
}
void World::targetDestroyed(Target& t){
    if(t.processed)return;
    t.processed=true;
    if(rpgPrototype)return; // The RPG owns its replicated destruction timeline.
    // Count each pooled life once; the reusable target index is not a kill ID.
    if(proceduralLayout&&proceduralPopulation.enabled&&missionStatus==MissionStatus::Active&&t.spawnType>=0&&t.active&&t.health<=0&&!t.escaped&&t.prototypeObjective&&t.proceduralZone>=0&&size_t(t.proceduralZone)<procedural.objectives.size()){
        auto& goal=procedural.objectives[size_t(t.proceduralZone)];
        if(goal.kind==ProceduralGoalKind::Eliminate&&goal.killQuota)goal.kills=std::min(goal.killQuota,goal.kills+1);
    }
    if(t.ai.identity>0&&t.ai.identity<32){int id=targetSlots[t.ai.identity];if(id>=0&& &targets[id]==&t){targetSlots[t.ai.identity]=-1;missionFlags&=~(1u<<t.ai.identity);}}
    if(!game&&t.spawnType==8&&mission==6)missionFlags&=~(1u<<(t.ai.words[1]&31));
    float size=std::clamp(length(t.half)*2,4.f,30.f);
    if(t.fragmentModels.empty())destruction(t.center,size,Weapon::Empty);
    else{
        effect(WeaponEffect::Blast,t.center,{},Weapon::Empty,size);
        if(effect(WeaponEffect::Smoke,t.center,{},Weapon::Empty,size))effects.back().age=-.3f;
        Vec3 base=t.origin;
        if(t.mesh&&!t.mesh->empty()){float bottom=1e9f;for(const auto& v:*t.mesh)bottom=std::min(bottom,v.position.y);base.y+=bottom;}
        int nx=std::max(1,int(t.fragmentSpan.x/t.fragmentStep+.5f)),ny=std::max(1,int(t.fragmentSpan.y/t.fragmentStep+.5f)),nz=std::max(1,int(t.fragmentSpan.z/t.fragmentStep+.5f));unsigned count=0;
        for(int y=0;y<ny&&count<160;++y)for(int x=0;x<nx&&count<160;++x)for(int z=0;z<nz&&count<160;++z){
            if(x>0&&x<nx-1&&z>0&&z<nz-1&&y>0&&y<ny-1)continue;
            Vec3 local{(x-(nx-1)*.5f)*t.fragmentStep,(y+.5f)*t.fragmentStep,(z-(nz-1)*.5f)*t.fragmentStep};
            Vec3 offset=rotate(angles(t.yaw),local),velocity=unit(Vec3{offset.x,.5f,offset.z})*(5.f+float(count%5));velocity.y=10.f+float(count%16);
            if(effect(WeaponEffect::Debris,base+offset,velocity,Weapon::Empty,1))effects.back().model=t.fragmentModels[count%t.fragmentModels.size()];++count;
        }
    }
    if(t.spawnType<0&&size>10)for(int i=0;i<3;++i){Vec3 p=t.center+Vec3{(i-1)*t.half.x*.5f,(i-1)*t.half.y*.5f,0};if(effect(WeaponEffect::Blast,p,{},Weapon::Empty,size*.65f))effects.back().age=-.12f*(i+1);}
    sounds.push_back({size>15?SoundEvent::LargeExplosion:SoundEvent::Explosion,0,t.center});
    if(rpgPrototype||missionStatus==MissionStatus::Range||missionPrototype||(assault&&t.spawnType>=0))return;
    if(game){destroyedIS2(t);return;}
    if(t.spawnType<0){applyMissionAction(t.action);if(mission==12&&t.tag<128)--counters[1];
        if(mission==10&&t.origin.x>=14.08f&&t.origin.x<=290.56f&&t.origin.z>=-71.68f&&t.origin.z<=102.4f)--counters[2];
        if(t.tag<32)pickups.push_back({{t.origin.x,1.5f,t.origin.z},t.tag});
    }else{
        if(t.spawnType==3||t.spawnType==8||(t.spawnType==9&&mission!=1)||(mission==5&&t.spawnType==0))counters[0]=std::max(0,counters[0]-1);
        if(t.spawnType==9)missionFlags|=1u<<t.deathFlag;
        if(mission==6&&t.spawnType==8&&t.ai.words[1]==2)pickups.push_back({{t.origin.x,1.5f,t.origin.z},14});
    }
    if(t.protectedTarget&&t.spawnType!=8)missionStatus=MissionStatus::Failed;
}
void World::hurt(float amount,Vec3 towardSource){
    if(missionStatus!=MissionStatus::Active||amount<=0)return;
    for(int k=2;k<4;++k)if(weapons[k]==Weapon::Shield)amount*=.5f;
    health=std::max(0.f,health-amount);
    markDamage(towardSource,elapsed);
    sounds.push_back({SoundEvent::Damage,0,eye()});
    if(health==0){
        searchlightOn=false;lifeSpent=true;ejectionTime=0;missionStatus=MissionStatus::Ejecting;
        missile.active=false;resetTracking();sounds.push_back({SoundEvent::Failed,0,eye()});
    }
}
bool World::destructionPending()const{
    return std::any_of(effects.begin(),effects.end(),[](auto& fx){return fx.kind==WeaponEffect::Blast||fx.kind==WeaponEffect::Smoke||fx.kind==WeaponEffect::Debris;});
}
void World::evaluateMission(){
    if(missionStatus!=MissionStatus::Active)return;
    if(proceduralLayout){updateProceduralMission(*this,0);return;}
    if(missionPrototype){
        unsigned remaining=0;for(const auto& t:targets)if(t.prototypeObjective&&t.health>0)++remaining;
        objective="PROTOTYPE / SEED "+std::to_string(prototypeSeed)+" / DESTROY "+std::to_string(remaining)+" TARGETS";
        if(!remaining&&!destructionPending())missionStatus=MissionStatus::Complete;
        return;
    }
    if(game){evaluateIS2();return;}
    bool success=false,fail=false;bool supply=(missionFlags&0x80000000u)!=0;
    switch(mission){
    case 0:case 3:case 6:case 8:success=counters[0]==0&&supply;break;
    case 1:success=counters[0]==0&&(missionFlags&3)==3;fail=elapsed>=480;break;
    case 2:case 4:case 14:success=counters[0]==0;break;
    case 5:case 11:success=counters[0]==0;fail=missionFlags&1;break;
    case 7:success=counters[0]==0;fail=elapsed>=750;break;
    case 9:{unsigned helicopters=0;for(auto& t:targets)if(t.active&&t.health>0&&t.spawnType==1)++helicopters;success=counters[0]==0&&helicopters<=5;break;}
    case 10:success=counters[2]==0;break;
    case 12:success=counters[1]==0;break;
    case 13:success=missionFlags&(1u<<30);fail=counters[0]==0;break;
    case 15:success=counters[0]==0&&counters[1]==0;break;
    }
    if(mission==6&&(missionFlags&(1u<<30)))fail=true;
    if(fail)missionStatus=MissionStatus::Failed;else if(success&&!destructionPending())missionStatus=MissionStatus::Complete;
}
void World::stepMission(float dt){
    if(missionStatus!=MissionStatus::Active)return;
    elapsed+=dt;stepGenerators(dt);
    if(!game&&mission==5&&position.x>=-232.96f&&position.x<=230.4f&&position.z>=-99.84f&&position.z<=99.84f)missionFlags|=2;
    if(!game&&mission==3&&counters[1]==0){counters[1]=-1;missionFlags|=1;}
    if(!game&&mission==10&&missionBuildingCount>0){int stage=(7^(std::max(0,counters[2])*8/(missionBuildingCount+1)))-1;if(stage>=0&&stage<7)missionFlags|=1u<<stage;}
    stepEnemies(dt);
    for(auto& t:targets)if(t.active&&t.health<=0&&!t.processed)targetDestroyed(t);
    if(missionStatus!=MissionStatus::Active)return;
    if(!cooperative)collectPickups();
    if(!game&&mission==13){bool survivor=false,arrived=true;for(auto& t:targets)if(t.spawnType==8&&t.health>0&&!t.generated&&!(t.spawnFlags&4)){survivor=true;arrived&=t.escaped;}if(survivor&&arrived)missionFlags|=1u<<30;}
    auto before=missionStatus;if(proceduralLayout)updateProceduralMission(*this,dt);else evaluateMission();if(before!=missionStatus)sounds.push_back({missionStatus==MissionStatus::Complete?SoundEvent::Complete:SoundEvent::Failed,0,eye()});
}
void World::damagePilot(Pilot& pilot,float amount,Vec3 towardSource){
    if(&pilot==static_cast<Pilot*>(this)){hurt(amount,towardSource);return;}
    Pilot local=*this;static_cast<Pilot&>(*this)=pilot;hurt(amount,towardSource);pilot=*this;static_cast<Pilot&>(*this)=local;
}
void World::collectPickups(){
    if(missionStatus!=MissionStatus::Active)return;
    if(!game&&mission==5&&position.x>=-232.96f&&position.x<=230.4f&&position.z>=-99.84f&&position.z<=99.84f)missionFlags|=2;
    for(auto& p:pickups){
        unsigned bit=1u<<playerSlot;
        if((cooperative?(p.collectedBy&bit)!=0:p.collected)||length(Vec3{position.x,0,position.z}-Vec3{p.position.x,0,p.position.z})>=7)continue;
        bool first=!p.collected;p.collected=true;p.collectedBy|=bit;
        if(first){if(p.kind>=0&&p.kind<32&&(0x314180u&(1u<<p.kind)))missionFlags|=0x80000000u;if(p.kind==22&&(game?(mission==2||mission==12||mission==16):mission==3))counters[0]=std::max(0,counters[0]-1);}
        health=std::min(100.f,health+(cooperative?7.5f:15.f));
        for(int k=0;k<MountCount;++k){unsigned amount=weaponInfo(weapons[k]).capacity/4;float refill=amount*(cooperative?.5f:1.f)+ammoRemainder[k];unsigned whole=unsigned(refill);ammoRemainder[k]=refill-whole;ammo[k]=std::min(weaponInfo(weapons[k]).capacity,ammo[k]+whole);if(ammo[k]==weaponInfo(weapons[k]).capacity)ammoRemainder[k]=0;}
        sounds.push_back({SoundEvent::PickupSound,0,p.position});
    }
}
}
