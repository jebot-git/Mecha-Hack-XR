#include "rpg/motion_playback.hpp"
#include "simulation.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <set>

namespace iron::rpg {
namespace {
float loop(float phase){ return std::isfinite(phase)?phase-std::floor(phase):0.f; }
Quaternion interpolate(Quaternion a,Quaternion b,float t){
    float v=(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w<0)?-t:t;
    return normalized({(1-t)*a.x+v*b.x,(1-t)*a.y+v*b.y,(1-t)*a.z+v*b.z,(1-t)*a.w+v*b.w});
}
Vec3 vector3(const nlohmann::json& row){
    auto v=row.get<std::array<float,3>>();return {v[0],v[1],v[2]};
}
}
void blendMotionTransition(WalkerPose& to,const WalkerPose& from,float fraction){
    if(fraction>=1||from.parts.size()!=to.parts.size())return;
    float t=std::clamp(fraction,0.f,1.f);t=t*t*(3-2*t);
    for(size_t i=0;i<to.parts.size();++i){
        auto& end=to.parts[i];const auto& start=from.parts[i];
        end={interpolate(start.orientation,end.orientation,t),start.offset*(1-t)+end.offset*t};
    }
}
void MotionPlayback::load(const std::filesystem::path& dir){
    actors.clear();samples.clear();visualRigs.clear();
    std::ifstream index(dir/"library.json");
    if(!index)return; // Older and non-RPG asset packs use the procedural renderer.
    nlohmann::json doc;index>>doc;
    std::ifstream file(dir/"poses.bin",std::ios::binary|std::ios::ate);
    const auto bytes=file.tellg();
    if(doc.at("version")!=1||doc.at("fps")!=24||bytes<=0||bytes>64*1024*1024||std::streamoff(bytes)%2)
        throw std::runtime_error("Invalid motion library");
    samples.resize(size_t(bytes)/2);
    file.seekg(0);if(!file.read(reinterpret_cast<char*>(samples.data()),bytes))throw std::runtime_error("Incomplete motion library");
    for(const auto& [id,row]:doc.at("actors").items()){
        auto& a=actors[id];a.kind=row.at("kind");a.family=row.at("family");
        a.joints=row.at("parts").get<std::vector<std::string>>();
        if(a.joints.empty()||a.joints.size()>128)throw std::runtime_error("Invalid motion joint count");
        for(const auto& [key,c]:row.at("clips").items()){
            MotionClip clip{c.at(0),c.at(1),c.at(2)};
            const size_t stride=a.joints.size()*7+1;
            if(clip.frames<2||clip.first>samples.size()||clip.frames>(samples.size()-clip.first)/stride||
               !(clip.scale>0)||!std::isfinite(clip.scale))throw std::runtime_error("Invalid motion clip "+id+"/"+key);
            a.clips.emplace(key,clip);
        }
        if(row.contains("elbows"))for(const auto& p:row.at("elbows")){
            EnemyPart part;part.name=p.at("name");part.parent=p.at("parent");part.pivot=vector3(p.at("pivot"));
            part.low=vector3(p.at("low"));part.high=vector3(p.at("high"));
            part.ranges=p.at("ranges").get<decltype(part.ranges)>();
            a.elbowRig.push_back(std::move(part));
        }
    }
}
const MotionActor* MotionPlayback::find(std::string_view id)const{
    auto it=actors.find(id);return it==actors.end()?nullptr:&it->second;
}
const EnemyRig* MotionPlayback::visualRig(const std::string& id,const EnemyRig* source){
    const auto* a=find(id);
    if(!source||!a)return source;
    if(a->elbowRig.empty()){
        if(source->parts.size()!=a->joints.size())throw std::runtime_error("Motion joint count mismatch: "+id);
        for(size_t i=0;i<a->joints.size();++i)if(source->parts[i].name!=a->joints[i])throw std::runtime_error("Motion binding mismatch: "+id);
        return source;
    }
    if(auto it=visualRigs.find(id);it!=visualRigs.end())return &it->second;
    EnemyRig r=*source;r.parts.clear();r.drawRanges.clear();
    size_t cursor=0;std::vector<bool> used(r.vertices);
    for(const auto& p:a->elbowRig){
        auto original=std::find_if(source->parts.begin(),source->parts.end(),[&](const EnemyPart& o){return o.name==p.name;});
        auto part=original==source->parts.end()?EnemyPart{}:*original;
        part.name=p.name;part.parent=p.parent;part.low=p.low;part.high=p.high;part.pivot=p.pivot;part.ranges=p.ranges;
        if(part.parent>=int(r.parts.size())||part.parent< -1)throw std::runtime_error("Invalid visual motion parent");
        size_t count=0;
        for(auto [first,n]:part.ranges){
            if(first>used.size()||n>used.size()-first||first%3||n%3)throw std::runtime_error("Invalid visual elbow ranges");
            for(size_t i=first;i<first+n;++i){if(used[i])throw std::runtime_error("Overlapping visual elbow ranges");used[i]=true;}
            count+=n;
        }
        r.drawRanges.emplace_back(cursor,count);cursor+=count;r.parts.push_back(std::move(part));
    }
    if(cursor!=r.vertices||r.parts.size()!=a->joints.size())throw std::runtime_error("Incomplete visual elbow rig");
    return &visualRigs.emplace(id,std::move(r)).first->second;
}
MotionCue MotionPlayback::at(const MotionActor& a,std::initializer_list<std::string_view> keys,float t){
    for(auto key:keys)if(auto it=a.clips.find(key);it!=a.clips.end())
        return {&it->second,std::clamp(std::isfinite(t)?t:0.f,0.f,1.f)};
    return {};
}
MotionCue MotionPlayback::enemyCue(const MotionActor& a,const Target& body,float now)const{
    const auto& ai=body.ai;const auto& v=ai.voidmawPose;const auto& m=ai.combatMotion;
    const float phase=m.phase(now),age=std::max(0.f,now-v.started);
    float t=v.duration>0?std::clamp(age/v.duration,0.f,1.f):loop(age/1.6f);
    const float gait=loop(ai.gaitPhase/(a.family=="mecha"||a.family=="infantry"?36.f:18.f));
    auto pick=[&](std::initializer_list<std::string_view> keys,float p){return at(a,keys,p);};
    if(body.health<=0)return pick({"death","collapse","destruction"},(now-body.rpgDeathAt)/1.6f);
    if(v.phase!=VoidmawPose::Default&&now>=v.started&&(v.duration<=0||age<=v.duration)){
        MotionCue c;
        switch(v.phase){
        case VoidmawPose::Pursuit:c=pick({"pursuit"},gait);break;
        case VoidmawPose::Dodge:c=pick({"dodge"},t);break;
        case VoidmawPose::Coil:c=pick({"coil"},t);break;
        case VoidmawPose::Tackle:c=pick({"tackle"},t);break;
        case VoidmawPose::Cling:c=pick({"cockpit-cling"},loop(age/1.6f));break;
        case VoidmawPose::Recover:c=pick({"recover"},t);break;
        case VoidmawPose::Lumber:c=pick({"lumber"},gait);break;
        case VoidmawPose::Brace:c=pick({"siege-brace"},t);break;
        case VoidmawPose::Discharge:c=pick({"flying-maw-sweep","sweeping-maw-beam","axial-maw-beam","caustic-web","caustic-spit","rocket-elbow","maw-beam"},.36f+.17f*t);break;
        case VoidmawPose::SiegeRecover:c=pick({"siege-recovery"},t);break;
        default:break;
        }
        if(c.clip)return c;
    }
    if(m.active(now)){
        MotionCue c;
        const char* cut=m.technique==MechaTechnique::LeftCut?"left-cut":m.technique==MechaTechnique::RightCut?"right-cut":
            m.technique==MechaTechnique::Overhead?"overhead-cut":"close-strike";
        switch(m.action){
        case EnemyAction::MeleeWindup:if(m.technique==MechaTechnique::Rush)return pick({"melee-rush","run"},gait);c=pick({cut,"rending-claws","ripping-talons","barbed-tail","rending-maw","rending-mandibles","crushing-bite","bone-axe","melee-windup"},phase*.49f);break;
        case EnemyAction::Strike:c=pick({cut,"rending-claws","ripping-talons","barbed-tail","rending-maw","rending-mandibles","crushing-bite","bone-axe","chittering-mandibles","coiling-tentacles"},.49f+.51f*phase);break;
        case EnemyAction::Guard:c=pick({"guard"},.28f+.36f*phase);break;
        case EnemyAction::Parry:c=pick({"parry"},.36f+.64f*phase);break;
        case EnemyAction::Stagger:c=pick({"stagger","impact-stagger"},phase);break;
        case EnemyAction::RangedWindup:c=pick({"ranged-windup","siege-brace","ranged-aim"},phase);break;
        case EnemyAction::Fire:
            if(m.technique==MechaTechnique::MissileRack)c=pick({"lofted-missile-salvo"},.20f+.8f*phase);
            else if(m.technique==MechaTechnique::Skirt)c=pick({"skirt-volley"},.20f+.8f*phase);
            if(!c.clip)c=pick({"fire-recoil","cannon-fire","chain-gun-burst","heavy-broadside","ion-barrage","artillery-bombardment","torpedo-spread","rocket-volley","point-defence-fire","maw-beam","miniature-maw-beam","dorsal-spines","caustic-spit"},.2f+.8f*phase);
            break;
        default:break;
        }
        if(c.clip)return c;
    }
    float speed=length(ai.velocity);
    if(ai.state==AIState::BombRun)if(auto c=pick({"bombing-run","strafe-pass"},loop(now/2.f));c.clip)return c;
    if(ai.state==AIState::Retreat||ai.state==AIState::Egress)if(auto c=pick({"retreat","withdraw","climb-out"},gait);c.clip)return c;
    if(speed>.25f){
        if(hasInjury(body.rpgEnemyInjuries,MechaInjury::Hobbled))if(auto c=pick({"hobbled-walk"},gait);c.clip)return c;
        if(speed>13.f||ai.state==AIState::Evade)
            if(auto c=pick({"leap-land","boost-evade","banked-pass","dive-turn"},loop(now/1.55f));c.clip)return c;
        if(m.retreat>.55f)if(auto c=pick({"retreat","withdraw"},gait);c.clip)return c;
        if(m.technique==MechaTechnique::Rush)if(auto c=pick({"melee-rush"},gait);c.clip)return c;
        auto local=rotate(gravityInverse(compose(ai.gravity.frame,angles(body.yaw))),ai.velocity);
        if(std::abs(local.x)>1.3f*std::abs(local.z))if(auto c=pick({"strafe"},gait);c.clip)return c;
        if(ai.state==AIState::Melee)if(auto c=pick({"melee-rush","pursuit"},gait);c.clip)return c;
        if(speed>5)if(auto c=pick({"run","pursuit"},gait);c.clip)return c;
        if(auto c=pick({"walk","slither","lumber"},gait);c.clip)return c;
        return pick({"track-drive","flight-cruise","hover-cruise","fleet-cruise","submerged-cruise","swim","wingbeat"},loop(now/2.f));
    }
    if(m.aim>.3f)if(auto c=pick({"ranged-aim","aim-track"},.48f);c.clip)return c;
    return pick({"idle","idle-scan"},loop(now/2.4f));
}
MotionCue MotionPlayback::pilotCue(const MotionActor& a,const Pilot& p,float now)const{
    if(p.lifeSpent)return at(a,{"pilot-ejection"},p.ejectionTime/1.55f);
    if(p.health<=0)return at(a,{"disabled-kneel"},1.f);
    if(p.rpgVehicleBlend>0)return at(a,{"vehicle-transform"},p.rpgVehicleBlend);
    if(p.rpgLanding>.03f)return at(a,{"leap-land"},.73f+.27f*(1-p.rpgLanding));
    if(length(p.rpgThrust)>.05f)return at(a,{p.gravity.active()?"zero-g-thrust":"boost-evade"},.50f);
    if(std::abs(p.walkingSpeed)>.15f){
        auto name=p.injured(MechaInjury::Hobbled)?"hobbled-walk":p.walkingSpeed<0?"retreat":p.walkingSpeed>5?"run":"walk";
        return at(a,{name},loop(p.gaitPhase/36.f));
    }
    if(p.rpgBodyRecoil>.02f)return at(a,{"fire-recoil"},.2f+.8f*(1-p.rpgBodyRecoil));
    return at(a,{"idle"},loop(now/2.4f));
}
void MotionPlayback::apply(const MotionActor& a,MotionCue cue,WalkerPose& pose,const EnemyRig* enemy,
                           const WalkerRig* player,bool preserveAim,bool canopy,float yaw,float pitch,bool discharge)const{
    if(!cue.clip||pose.parts.size()!=a.joints.size())return;
    const auto& c=*cue.clip;
    float sample=cue.phase*(c.frames-1);unsigned f=unsigned(sample);float t=sample-f;
    const size_t stride=a.joints.size()*7+1;
    const int16_t* left=samples.data()+c.first+f*stride;
    const int16_t* right=f+1<c.frames?left+stride:left;
    const float height=(left[stride-1]*(1-t)+right[stride-1]*t)*c.scale;
    const bool mobileAim=preserveAim&&enemy&&a.family=="mecha";
    Quaternion aim=angles(std::clamp(wrap(yaw),-pi/2,pi/2),std::clamp(pitch,-.6f,.6f));
    Vec3 pivot{};
    if(mobileAim)for(const auto& p:enemy->parts)if(p.joint==EnemyJoint::Torso){pivot=p.pivot;break;}
    // Pin the visual mouth to the socket used by the authoritative bite / beam.
    // Apply a common transform to all pieces to keep the neck and limbs connected.
    bool pinMouth=false;
    PartPose pin{};
    if(enemy&&(canopy||discharge)){
        for(size_t i=0;i<enemy->parts.size();++i){
            const auto& part=enemy->parts[i];
            bool elbow=discharge&&a.clips.contains("rocket-elbow");
            if(elbow?part.joint!=EnemyJoint::Arm:part.joint!=EnemyJoint::Jaw)continue;
            const int16_t* l=left+i*7;const int16_t* r=right+i*7;
            auto quat=[](const int16_t* v){return Quaternion{v[0]/32767.f,v[1]/32767.f,v[2]/32767.f,v[3]/32767.f};};
            Quaternion authored=interpolate(quat(l),quat(r),t);
            Vec3 shift{(l[4]*(1-t)+r[4]*t)*c.scale,(l[5]*(1-t)+r[5]*t)*c.scale+height,(l[6]*(1-t)+r[6]*t)*c.scale};
            pin.orientation=compose(pose.parts[i].orientation,gravityInverse(authored));
            pin.offset=pose.parts[i].offset-rotate(pin.orientation,shift);
            pinMouth=true;break;
        }
    }
    for(size_t i=0;i<a.joints.size();++i){
        // Player tracking, aim and upper-body transforms stay under live control.
        if(player){
            auto joint=player->parts[i].joint;
            if(joint<WalkerJoint::LeftHip||joint>WalkerJoint::RightFoot)continue;
        }
        if(enemy){
            const auto joint=enemy->parts[i].joint;
            if((enemy->turretBarrel>=0||(preserveAim&&a.family!="mecha"))&&
               (joint==EnemyJoint::Turret||joint==EnemyJoint::Barrel))continue;

        }
        auto unpack=[i,&c](const int16_t* p){
            p+=i*7;constexpr float u=1.f/32767;
            return PartPose{{p[0]*u,p[1]*u,p[2]*u,p[3]*u},{p[4]*c.scale,p[5]*c.scale,p[6]*c.scale}};
        };
        auto l=unpack(left),r=unpack(right);
        PartPose out{interpolate(l.orientation,r.orientation,t),l.offset*(1-t)+r.offset*t};
        out.offset.y+=height;
        if(mobileAim){
            for(int j=int(i);j>=0;j=enemy->parts[size_t(j)].parent)
                if(enemy->parts[size_t(j)].joint==EnemyJoint::Torso){
                    out={compose(aim,out.orientation),pivot+rotate(aim,out.offset-pivot)};break;
                }
        }
        if(pinMouth)out={compose(pin.orientation,out.orientation),animatedPoint(pin,out.offset)};
        pose.parts[i]=out;
    }
}
}
