#include "autopilot.hpp"
#include <queue>
#include <limits>
namespace iron {
Input Autopilot::update(const World& w,float dt){
    Input input;
    if(!w.missionPrototype||w.missionStatus!=MissionStatus::Active)return input;
    constexpr int side=281,mid=side/2;constexpr float step=4;
    if(clear.empty()||w.elapsed<lastElapsed){
        anchor=w.position;path.clear();target=-1;replan=0;clear.assign(side*side,true);
        for(int z=0;z<side;++z)for(int x=0;x<side;++x){
            Vec3 p{anchor.x+(x-mid)*step,0,anchor.z+(z-mid)*step};
            bool free=std::abs(p.x)<990&&std::abs(p.z)<990;
            for(const auto& t:w.targets)if(t.spawnType<0&&t.active&&t.health>0&&t.center.y+t.half.y>0&&t.center.y-t.half.y<12&&
                std::abs(p.x-t.center.x)<t.half.x+6&&std::abs(p.z-t.center.z)<t.half.z+6){free=false;break;}
            clear[z*side+x]=free;
        }
    }
    lastElapsed=w.elapsed;replan-=dt;
    auto visible=[&](size_t i,Vec3 from){auto d=w.targets[i].center-from;return w.traceAim(from,d,length(d)+1).target==int(i);};
    int selected=-1;float best=std::numeric_limits<float>::max();
    for(size_t i=0;i<w.targets.size();++i){auto& t=w.targets[i];if(!t.prototypeObjective||t.health<=0||!t.active)continue;
        float distance=length(t.center-w.eye());float score=distance+(visible(i,w.eye())?0:400);
        if(int(i)==target)score-=20;
        if(score<best){best=score;selected=int(i);}
    }
    if(selected<0)return input;
    const auto& enemy=w.targets[selected];
    auto cell=[&](Vec3 p){int x=int(std::round((p.x-anchor.x)/step))+mid,z=int(std::round((p.z-anchor.z)/step))+mid;return x>=0&&x<side&&z>=0&&z<side?z*side+x:-1;};
    auto point=[&](int id){return Vec3{anchor.x+(id%side-mid)*step,0,anchor.z+(id/side-mid)*step};};
    if(selected!=target||replan<=0){
        target=selected;replan=2;path.clear();int start=cell(w.position),goal=cell(enemy.origin);
        if(start>=0&&goal>=0){
            std::vector<int> parent(side*side,-1);std::queue<int> queue;queue.push(start);parent[start]=start;
            int reached=-1;
            while(!queue.empty()){
                int id=queue.front();queue.pop();
                if(id==goal||length(point(id)-Vec3{enemy.origin.x,0,enemy.origin.z})<8){reached=id;break;}
                for(auto delta:std::array<std::array<int,2>,4>{{{{-1,0}},{{1,0}},{{0,-1}},{{0,1}}}}){
                    int x=id%side+delta[0],z=id/side+delta[1];if(x<0||z<0||x>=side||z>=side)continue;
                    int next=z*side+x;if(clear[next]&&parent[next]<0){parent[next]=id;queue.push(next);}
                }
            }
            for(int id=reached;id>=0&&id!=start;id=parent[id])path.push_back(point(id));
            std::reverse(path.begin(),path.end());
        }
    }
    Vec3 delta=enemy.center-w.eye();float distance=length(delta);
    bool sight=visible(selected,w.eye());
    Vec3 destination=enemy.origin;
    while(!path.empty()&&length(path.front()-Vec3{w.position.x,0,w.position.z})<2)path.erase(path.begin());
    bool advance=(!sight||distance>65)&&!path.empty();
    if(advance)destination=path.front();
    auto move=destination-w.position;float desired=std::atan2(-move.x,-move.z);
    float turn=wrap(desired-w.chassisYaw);
    float drive=1;
    if(advance&&std::abs(turn)>pi/2){turn=wrap(turn+pi);drive=-1;}
    input.steer=-std::clamp(turn*3.f,-1.f,1.f);input.turn=std::clamp(w.torsoYaw*3.f,-1.f,1.f);
    input.forward=advance&&std::abs(turn)<.8f?drive*.7f:0;
    float aim=wrap(std::atan2(-delta.x,-delta.z)-w.facing());
    input.headYaw=std::clamp(aim,-pi/4,pi/4);input.headPitch=std::atan2(delta.y,std::hypot(delta.x,delta.z));
    for(int hand=0;hand<2;++hand){
        auto& pose=input.handPoses[hand];pose.grip=angles(aim,input.headPitch);pose.aim=pose.grip;
    }
    // Aim from the solved rifle muzzle, so close targets respect hand parallax.
    auto arm=solveArm(input.handPoses[1],1,w.eye(),w.facing(),w.armRig,1);
    auto rifle=enemy.center-arm.muzzle;
    float rifleYaw=wrap(std::atan2(-rifle.x,-rifle.z)-w.facing());
    input.handPoses[1].aim=angles(rifleYaw,std::atan2(rifle.y,std::hypot(rifle.x,rifle.z)));
    if(sight&&std::abs(aim)<.65f&&distance<160){
        input.fire[RightHand]=w.ammo[RightHand]>0;
        input.fire[LeftShoulder]=w.ammo[LeftShoulder]>0&&distance<100;
        input.fire[LeftHip]=w.ammo[LeftHip]>0&&distance>25&&distance<80;
    }
    return input;
}
}
