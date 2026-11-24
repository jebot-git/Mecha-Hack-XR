#include "simulation.hpp"
#include <cstdlib>
#include <iostream>
using namespace iron;
static int checks=0;
static void expect(bool b,const char* description) {
    ++checks;if(!b){std::cerr<<"FAIL: "<<description<<"\n";std::exit(1);}
}
static bool near(float a,float b,float tolerance=.001f){return std::abs(a-b)<tolerance;}
static void run(World& w,Input i,int frames=180,float dt=1.f/90){for(int k=0;k<frames;++k)w.step(i,dt);}
int main(){
    {
        auto a=mountButtons(false,false,true,false,false,false);
        auto b=mountButtons(false,false,false,true,false,false);
        auto x=mountButtons(false,false,false,false,true,false);
        auto y=mountButtons(false,false,false,false,false,true);
        expect(a[RightHip]&&!a[LeftShoulder],"A maps to right hip");
        expect(b[RightShoulder]&&!b[RightHip],"B maps to right shoulder");
        expect(x[LeftHip]&&!x[LeftShoulder],"X maps to left hip");
        expect(y[LeftShoulder]&&!y[RightHip],"Y maps to left shoulder");
        auto l=mountButtons(true,false,false,false,false,false),r=mountButtons(false,true,false,false,false,false);
        expect(l[LeftHand]&&!l[RightHand]&&r[RightHand]&&!r[LeftHand],"triggers map to corresponding hand");
    }
    {
        World w;Input i;i.turn=1;run(w,i,1000);
        expect(near(w.torsoYaw,-pi/2),"right torso stops at -90 degrees");
        i.turn=-1;run(w,i,2000);expect(near(w.torsoYaw,pi/2),"left torso stops at +90 degrees");
        expect(near(w.chassisYaw,0),"right stick never turns chassis");
        i.turn=0;i.headYaw=.4f;i.headPitch=.3f;w.step(i,.01f);
        expect(near(w.aimYaw,wrap(pi/2+.4f))&&near(w.aimPitch,.3f),"head aims independently at torso stop");
        i.steer=1;run(w,i);expect(near(w.torsoYaw,pi/2),"chassis steering preserves torso offset");
        expect(std::abs(w.chassisYaw)>1,"left stick steers chassis beyond torso limit");
    }
    {
        World w;Input i;i.headYaw=pi/2;i.forward=1;run(w,i,90);
        expect(near(w.position.x,0)&&near(w.position.z,27,.01f),"movement follows chassis, not gaze");
        World a,b;run(a,i,60,1.f/60);run(b,i,120,1.f/120);
        expect(length(a.position-b.position)<.001f,"movement independent of render frequency");
        World c;i.forward=.1f;run(c,i);expect(near(c.position.z,35),"stick deadzone");
    }
    {
        for(int mount=0;mount<MountCount;++mount){
            World w;Input i;i.fire[mount]=true;w.step(i,.01f);
            for(int other=0;other<MountCount;++other)expect(w.shots[other]==unsigned(other==mount),"mount fires independently");
        }
        World empty; empty.ammo.fill(0); Input all; all.fire.fill(true); run(empty,all);
        expect(empty.projectiles.empty()&&!empty.missile.active,"empty mounts cannot fire or launch cruise");
        for(auto shots:empty.shots)expect(shots==0,"empty mounts do not count shots");
        World limited; limited.ammo[LeftHand]=1; Input trigger; trigger.fire[LeftHand]=true; run(limited,trigger);
        expect(limited.shots[LeftHand]==1&&limited.ammo[LeftHand]==0,"last round consumed without unsigned underflow");
        expect(limited.ammo[RightHand]==400,"firing left mount preserves right ammo");
        World w;Input i;i.fire[LeftHand]=i.fire[RightHand]=true;w.step(i,.01f);
        expect(w.projectiles.size()==2,"both hands can fire together");
        expect(w.ammo[LeftHand]==399&&w.ammo[RightHand]==399,"each hand consumes its own ammo");
        w.step(i,.01f);expect(w.shots[0]==1&&w.shots[1]==1,"hand fire rate limited");
        run(w,i,90);expect(w.shots[0]>1&&w.shots[0]==w.shots[1],"automatic hand weapons repeat symmetrically");
    }
    {
        World w;w.targets.clear();Input i;i.headYaw=.6f;i.headPitch=.2f;i.fire[RightHip]=true;w.step(i,.01f);
        expect(w.missile.active&&near(w.missile.yaw,0)&&near(w.missile.pitch,0),"cruise launches along torso independently of head aim");
        expect(w.ammo[RightHip]==7,"cruise launch consumes one missile");
        float original=w.missile.yaw;i.headYaw=1.2f;i.headPitch=.6f;i.turn=1;i.forward=1;
        w.step(i,.05f);expect(w.missile.yaw>original&&w.missile.yaw<1.2f,"head guides missile with bounded turn rate");
        expect(w.missile.pitch>0&&w.missile.pitch<.4f&&near(w.position.z,35)&&near(w.torsoYaw,0),"cruise pitch follows head while mech stays still");
        run(w,i,1000);expect(!w.missile.active&&w.shots[RightHip]==1,"held button cannot relaunch expired cruise");
        i.fire[RightHip]=false;w.step(i,.01f);i.fire[RightHip]=true;w.step(i,.01f);
        expect(w.missile.active&&w.shots[RightHip]==2,"fresh press relaunches cruise");
    }
    {
        Gesture g;expect(!g.sample({0,0,0},true,.02f),"first pose never punches");
        expect(g.sample({0,0,-.06f},true,.02f),"deliberate swing punches");
        expect(!g.sample({0,0,-.12f},true,.02f),"continuous swing does not repeat");
        for(int k=0;k<30;++k)g.sample({0,0,-.12f},true,.02f);
        expect(g.sample({0,0,-.18f},true,.02f),"settle and cooldown rearm punch");
        g.sample({},false,.02f);expect(!g.sample({3,0,0},true,.02f),"tracking reacquisition never punches");
        expect(!g.sample({30,0,0},true,.02f),"tracking teleport rejected");
        g.sample({},true,.3f);expect(!g.sample({0,0,-.1f},true,.02f),"stall resets gesture history");
        World w;Input i;i.handValid[0]=true;w.step(i,.02f);i.hands[0].z=-.06f;w.step(i,.02f);
        expect(w.punches[0]==1&&w.punches[1]==0,"left swing only activates left fist");
        i.handValid[1]=true;w.step(i,.02f);i.hands[1].z=-.06f;w.step(i,.02f);
        expect(w.punches[1]==1,"right swing activates right fist");
    }
    {
        World w;Input i;i.focused=false;i.fire.fill(true);i.turn=1;i.forward=1;run(w,i);
        expect(near(w.position.z,35)&&near(w.torsoYaw,0)&&w.projectiles.empty()&&!w.missile.active,"unfocused simulation frozen");
        i.focused=true;i.headValid=false;run(w,i);expect(w.shots[0]==0,"invalid head tracking prevents firing");
        i.headValid=true;w.step(i,.3f);expect(w.shots[0]==0,"long stall skipped");
    }
    {
        World w;w.targets={{{0,12,0},{2,4,.05f},100}};
        w.projectiles={{{0,12,1},{0,0,-100},4,100,0}};w.step(Input{},.05f);
        expect(w.destroyed==1&&w.projectiles.empty(),"swept shot hits thin target without tunneling");
        w.explode({0,12,0},10,200);expect(w.destroyed==1,"destroyed target counted once");
        World c;c.targets={{{0,5,31},{5,5,1},100}};Input i;i.forward=1;run(c,i);
        expect(c.position.z>=34-.01f,"mech collision stops before building");
    }
    {
        World w;w.targets.clear();w.configure({Weapon::Empty,Weapon::Empty,Weapon::Rocket,Weapon::Rail,Weapon::Grenade,Weapon::Empty});w.step(Input{},.01f);
        Input in;in.fire[2]=in.fire[4]=true;w.step(in,.01f);expect(w.projectiles.size()==2,"rocket and grenade fire");
        expect(w.projectiles[0].radius>0&&w.projectiles[1].kind==ProjectileKind::Bomb,"explosive rocket and ballistic grenade");
        float vy=w.projectiles[1].velocity.y;w.stepProjectiles(.05f);expect(w.projectiles[1].velocity.y<vy,"grenade falls under gravity");
        w.projectiles[0].life=0;w.stepProjectiles(.01f);expect(std::any_of(w.effects.begin(),w.effects.end(),[](auto& fx){return fx.kind==WeaponEffect::Blast;}),"rocket leaves blast animation");
        w.projectiles.clear();for(int i=0;i<400;++i)w.stepProjectiles(.01f);expect(w.effects.empty(),"effects expire independently of firing");
    }
    {
        World w;w.targets.clear();w.strike({0,0,0},{0,0,-1},Weapon::Empty,65);
        expect(w.effects.empty()&&w.projectiles.empty(),"missed fist has no gunfire or fireball");
        expect(w.sounds.back().kind==SoundEvent::Punch,"gesture uses fist sound regardless of held weapon");
        w.strike({0,0,0},{0,0,-1},Weapon::Cutter,85);expect(w.effects.empty(),"cutter miss has no contact sparks");
        w.targets={{{0,0,-2},{1,1,1},100}};w.strike({0,0,-1},{0,0,-1},Weapon::Cutter,85);
        expect(w.effects.size()==1&&w.effects[0].kind==WeaponEffect::Sparks,"cutter sparks only on contact");
        w.effects.clear();w.projectiles.push_back({{0,30,0},{0,0,-100},.01f,25,0});w.stepProjectiles(.02f);
        expect(w.effects.empty(),"expired missed round does not explode in midair");
        w.targets[0].health=0;w.stepProjectiles(.01f);
        expect(w.targets[0].processed&&!w.effects.empty(),"destroyed scenery emits effects in range too");
        auto count=w.effects.size();w.stepProjectiles(.01f);expect(w.effects.size()==count,"death effects emitted once");
    }
    std::cout<<checks<<" checks passed\n";
}
