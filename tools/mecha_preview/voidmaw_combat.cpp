// Controlled native combat capture. AI, projectiles, cockpit and rig poses are stepped in-game.
#include "render.hpp"
#include "rpg/session.hpp"
#include "rpg/mecha_ranged.hpp"
#include "rpg/enemy_effects.hpp"
#include "rpg/original_assets.hpp"
#include <SDL.h>
#include <fstream>
#include <iostream>
using namespace iron;
using namespace iron::rpg;
using namespace avp;
namespace fs=std::filesystem;
namespace {
constexpr int width=512,height=344,fps=30;
struct Trial { const char *name,*model; float distance,seconds; bool evade; bool mecha=false; };
constexpr Trial trials[]={
    {"contact","kaiju_spawnling",35,8.1f,false},
    {"evade","kaiju_flea",32,3.4f,true},
    {"sweep","kaiju_obliterator",48,6.6f,false},
    {"volley","kaiju_broodqueen",44,5.5f,false},
    {"duel","mecha_ace",28,7.2f,false,true},
    {"weave","funnel_drone_mecha",48,7.2f,false,true},
    {"loft","siege_mecha",58,8.4f,false,true}
};
const char* phaseName(VoidmawPose phase){
    constexpr const char* names[]={"Approach","Pursuit","Dodge","Coil","Tackle","Cling","Recover",
                                   "Lumber","Brace","Discharge","Siege recovery"};
    return names[int(phase)];
}
void writeFrame(fs::path path){
    std::vector<unsigned char> pixels(width*height*3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("OpenGL readback failed");
    std::ofstream stream(path,std::ios::binary);
    stream<<"P6\n"<<width<<' '<<height<<"\n255\n";
    for(int y=height-1;y>=0;--y)
        stream.write(reinterpret_cast<char*>(pixels.data()+width*y*3),width*3);
    if(!stream)throw std::runtime_error("Frame write failed");
}
void ground(World& world){
    auto &mesh=world.editGeometry().terrain;
    for(int x=-12;x<12;++x)for(int z=-12;z<12;++z){
        float l=float(x*8),f=float(z*8),r=l+8,b=f+8;
        const Vec3 colour=(x+z)%2?Vec3{.24f,.255f,.22f}:Vec3{.20f,.215f,.19f};
        for(Vec3 v:std::array<Vec3,6>{{{l,0,f},{l,0,b},{r,0,b},{l,0,f},{r,0,b},{r,0,f}}})
            mesh.push_back({v,colour});
    }
}
void render(Renderer &renderer,const Session &s,const fs::path &dir,int frame,const Trial &trial){
    auto w=s.view(1,renderer.armRig());
    w.mission=0;w.missionStatus=MissionStatus::Active;w.rpgInsertion=0;
    w.environment={Lighting::Day,Weather::Clear,7};w.skyColor={.25f,.34f,.41f};w.groundColor={.22f,.23f,.19f};
    if(!frame){renderer.resetCockpit();renderer.updateCockpit(w,6,true);}
    renderer.updateCockpit(w,1.f/fps,false);
    auto &actor=w.targets.at(0);
    for(bool cockpit:{false,true}){
        Frontend menu;menu.screen=Screen::Playing;menu.renderWorldOnly=menu.spectating=!cockpit;
        Vec3 eye;Quaternion q;
        if(cockpit){
            eye=w.eye();q=w.cockpitOrientation();
        }else{
            menu.remotePilots.push_back(s.members.front().body);
            const auto focus=actor.center;
            auto distance=std::max(trial.distance>=40?18.f:12.f,std::max({actor.half.x,actor.half.y,actor.half.z})*2.8f);
            Vec3 aim=focus;
            eye=focus+Vec3{.87f,.38f,.70f}*distance;
            if(trial.mecha){
                const auto pilot=s.members.front().body.bodyPoint(7);
                aim=(focus+pilot)*.5f;
                const auto axis=unit(focus-pilot);
                const auto side=unit(cross(axis,{0,1,0}));
                const float span=std::max(20.f,length(pilot-focus));
                aim.y+=std::string_view(trial.name)=="loft"?5.f:0.f;
                eye=aim+(side*.77f+Vec3{0,.43f,.28f})*span;
            }
            auto ray=unit(aim-eye);
            q=angles(std::atan2(-ray.x,-ray.z),std::asin(ray.y));
        }
        const auto camera=poseMatrix({{q.x,q.y,q.z,q.w},{eye.x,eye.y,eye.z}});
        float v=cockpit?.69f:.46f,h=std::atan(std::tan(v)*width/height);
        glViewport(0,0,width,height);
        renderer.draw(w,projection({-h,h,v,-v},.05f,3000),camera,camera,false,&menu);
        writeFrame(dir/((cockpit?"pilot-":"exterior-")+std::to_string(frame)+".ppm"));
    }
}
void run(const Trial &trial,int hz,bool networked,Renderer *renderer,const fs::path &dir){
    Session s(41,networked);
    s.add(1,"Pilot");s.phase=Phase::Combat;
    s.world.rpgPrototype=true;s.world.elapsed=1;s.world.targets.clear();ground(s.world);
    auto &pilot=s.member(1);
    pilot.body.position={};pilot.body.chassisYaw=pilot.body.torsoYaw=0;
    pilot.sheet.maxHp=pilot.sheet.hp=100;pilot.sheet.ap=0;
    const std::string_view name=trial.name;
    if(name=="duel"){
        pilot.sheet.equipment[LeftHand]=Gear::Blade;
        pilot.sheet.equipment[RightHand]=Gear::Blade;
    }else if(name=="weave"){
        pilot.sheet.attributes[1]=12;pilot.sheet.armor=Armor::Light;
        pilot.body.chassisYaw=-pi/2;pilot.body.torsoYaw=pi/2;
    }
    auto &enemy=s.enemies.emplace_back();
    const auto catalog=enemyCatalog();
    auto spec=std::find_if(catalog.begin(),catalog.end(),[&](const auto &e){return std::string_view(e.id)==trial.model;});
    if(spec==catalog.end())throw std::runtime_error(trial.model);
    enemy.spec=int(spec-catalog.begin());enemy.hp=enemy.maxHp=40;enemy.tactic=0;
    enemy.fireClock=name=="duel"?0.f:1.4f;
    s.mission.recipe.enemies={enemy.spec};s.mission.openerTime=100;
    auto &actor=s.world.targets.emplace_back();
    actor.origin={0,0,-trial.distance};actor.yaw=pi;actor.spawnType=3;actor.active=true;actor.health=40;
    applyOriginalEnemy(actor,enemy.spec);
    fs::create_directories(dir);
    std::ofstream trace(dir/"trace.csv");
    trace<<"tick,time,phase,x,y,z,pilot_x,pilot_z,hull,enemy_hp,latch,projectiles,pulse,speed,bite_at,action,technique,wave_a,wave_b,lofted,return_fire,intercepted,parried\n";
    bool fired=false,evaded=false;
    float firstContact=-1;
    float crossing=-1;
    std::optional<HeavyAttack> firstWeave;
    Vec3 startWalk{};
    bool intercepted=false,parried=false;
    int frame=0;
    for(int tick=0;tick<int(trial.seconds*hz);++tick){
        float time=float(tick)/hz;
        auto &b=s.world.targets.front();
        auto &e=s.enemies.front();
        pilot.input={};pilot.inputAge=0;
        if(name=="duel"&&e.duel.cut==MechaTechnique::Overhead&&
           (e.duel.phase==DuelPhase::Windup||e.duel.phase==DuelPhase::FollowThrough)){
            const auto &duel=e.duel;
            const auto axis=duel.guardAxis;
            const auto orientation=angles(std::atan2(-axis.x,-axis.z),std::asin(axis.y));
            auto hand=duel.guardPoint.x<0?LeftHand:RightHand;
            pilot.input.handValid[hand]=true;
            pilot.input.handPoses[hand]={(duel.guardPoint-pilot.body.bodyPoint(12))*(1.f/6),
                                         orientation,orientation,true};
        }
        if(name=="weave"&&!firstWeave&&e.heavyAttack.pattern==HeavyPattern::Weave){
            firstWeave=e.heavyAttack;startWalk=pilot.body.position;
        }
        if(firstWeave){
            const auto &lock=*firstWeave;
            const auto mark=lock.origin+lock.aim*lock.lockDistance;
            if(crossing<0&&!s.ordnance.empty()){
                const auto &p=s.ordnance.front().body;
                crossing=s.world.elapsed+dot(mark-p.position,lock.aim)/dot(p.velocity,lock.aim);
            }
            const float offset=dot(pilot.body.position-startWalk,lock.lateral);
            const float desired=(lock.reverse?-1.f:1.f)*mechaWaveSpacing*(crossing<0?1.f:
                       std::clamp(1.f-(s.world.elapsed-crossing)/mechaWaveCadence,-1.f,1.f));
            pilot.input.forward=std::clamp((desired-offset)*lock.lateral.x*3,-1.f,1.f);
        }
        if(trial.evade&&!evaded&&b.ai.voidmawPose.phase==VoidmawPose::Coil){
            pilot.input.steer=1;pilot.input.thrust=true;evaded=true;
        }
        if(e.grapplePilot&&firstContact<0)firstContact=time;
        if(firstContact>=0&&time-firstContact>3.38f&&!fired&&e.grapplePilot){
            // Single controlled player shot through the regular damage and release paths.
            auto eye=s.view(1).eye();
            Shot shot{};shot.body.position=eye;shot.body.velocity=unit(b.center-eye)*1100;
            shot.body.damage=8;shot.body.shooter=pilot.id;shot.body.owner=-1;shot.body.life=.5f;
            shot.quality=Quality::Plasma;s.ordnance.push_back(shot);fired=true;
        }
        if(name=="loft"&&!fired){
            // Fire one controlled shot through the normal swept rocket interception path.
            const auto eye=pilot.body.bodyPoint(10);
            for(const auto &missile:s.ordnance){
                if(missile.body.visual!=ProjectileVisual::LoftMissile||
                   dot(missile.body.velocity,eye-missile.body.position)<=0||
                   length(missile.body.position-eye)>33.f)continue;
                constexpr float speed=300.f;
                const float flight=length(missile.body.position-eye)/speed;
                Shot shot{};
                shot.body.position=eye;
                shot.body.velocity=unit(missile.body.position+missile.body.velocity*flight-eye)*speed;
                shot.body.shooter=pilot.id;shot.body.owner=-1;shot.body.life=.6f;shot.body.damage=4;
                s.ordnance.push_back(shot);fired=true;break;
            }
        }
        s.step(1.f/hz);
        auto &body=s.world.targets.front();auto &state=s.enemies.front();
        parried|=pilot.riposteRemaining>0;
        if(name=="loft"&&fired)
            for(const auto &fx:s.world.effects)
                intercepted|=fx.model==rpgWeaponBlast&&length(fx.position-pilot.body.bodyPoint(6))>15.f;
        std::array<int,4> counts{};
        for(const auto &shot:s.ordnance)++counts[size_t(shot.body.visual)];
        trace<<tick<<','<<time<<','<<phaseName(body.ai.voidmawPose.phase)<<','<<body.origin.x<<','<<body.origin.y
             <<','<<body.origin.z<<','<<pilot.body.position.x<<','<<pilot.body.position.z<<','<<pilot.sheet.hp
             <<','<<state.hp<<','<<state.grapplePilot<<','<<s.ordnance.size()<<','<<state.heavyAttack.pulse
             <<','<<length(body.ai.velocity)<<','<<(state.grapplePilot&&body.ai.combatMotion.action==EnemyAction::Strike?body.ai.combatMotion.started:-1.f)<<','<<int(body.ai.combatMotion.action)<<','<<int(body.ai.combatMotion.technique)
             <<','<<counts[1]<<','<<counts[2]<<','<<counts[3]<<','<<int(fired)<<','<<int(intercepted)<<','<<int(parried)<<'\n';
        if(renderer&&tick% (hz/fps)==0)render(*renderer,s,dir,frame++,trial);
    }
    if(!trace)throw std::runtime_error("Trace write failed");
}
}
int main(int argc,char **argv){
    try{
        if(argc!=6)throw std::runtime_error("Usage: mecha-voidmaw-combat-preview ASSETS OUT trace|render|network HZ TRIAL|all|mecha");
        fs::path assets=argv[1],out=argv[2];std::string mode=argv[3],selected=argv[5];int hz=std::stoi(argv[4]);
        if(hz<15||hz>240||(mode=="render"&&hz%fps))throw std::runtime_error("Invalid simulation rate");
        if(mode!="render"&&mode!="trace"&&mode!="network")throw std::runtime_error("Invalid mode");
        setOriginalAssetRoot(assets);SDL_Window *window=nullptr;SDL_GLContext context=nullptr;
        std::unique_ptr<Renderer> renderer;
        if(mode=="render"){
            if(SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
            window=SDL_CreateWindow("Enemy combat preview",0,0,width,height,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
            if(!window||!(context=SDL_GL_CreateContext(window)))throw std::runtime_error(SDL_GetError());
            renderer=std::make_unique<Renderer>(assets/"cockpit");
        }
        bool found=false;
        for(const auto &trial:trials)if((selected=="all"&&!trial.mecha)||(selected=="mecha"&&trial.mecha)||selected==trial.name){
            found=true;run(trial,hz,mode=="network",renderer.get(),out/trial.name);
        }
        if(!found)throw std::runtime_error("Unknown trial "+selected);
        renderer.reset();
        if(context)SDL_GL_DeleteContext(context);
        if(window)SDL_DestroyWindow(window);
        SDL_Quit();
    }catch(const std::exception &err){std::cerr<<err.what()<<'\n';return 1;}
}
