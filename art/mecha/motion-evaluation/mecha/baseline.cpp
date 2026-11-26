// Read the same published mesh bindings and call the live NPC pose function.
#include "rpg/enemy_animation.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
int main(int argc,char **argv){
    if(argc!=3)return 2;
    using namespace iron;
    using namespace iron::rpg;
    using Json=nlohmann::json;
    auto input=Json::parse(std::ifstream(argv[2]));
    const auto *rig=enemyAnimationRig(argv[1],"mecha_ace",input["vertices"].get<size_t>());
    if(!rig)return 1;
    Json output=Json::array();
    for(const auto &row:input.at("frames")){
        const float t=row[0],distance=row[1],speed=row[2],brace=row[3];
        EnemyCombatMotion intent;
        intent.aim=brace;
        if(t>=4.8f&&t<6.35f)intent.play(EnemyAction::Guard,4.8f,1.55f);
        const auto pose=poseEnemy(*rig,std::fmod(distance*(100.f/96.f),18.f),speed,0,0,0,t,
                                  intent,0,{0,0,-speed});
        Json frame=Json::array();
        for(const auto &p:pose.parts){
            const auto q=p.orientation;const auto v=p.offset;
            frame.push_back({q.x,q.y,q.z,q.w,v.x,v.y,v.z});
        }
        output.push_back(frame);
    }
    std::cout<<output.dump();
}
