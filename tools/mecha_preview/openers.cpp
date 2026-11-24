#include "openers.hpp"
#include "render.hpp"
#include "rpg/insertion.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/session.hpp"
#include <SDL.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
using namespace iron;
using namespace iron::rpg;
using namespace avp;
namespace {
void captureOpener(const std::filesystem::path &path,int width,int height) {
    std::vector<unsigned char> pixels(size_t(width)*height*3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Opener framebuffer read failed");
    std::ofstream out(path,std::ios::binary);
    out<<"P6\n"<<width<<' '<<height<<"\n255\n";
    for(int y=height-1;y>=0;--y)
        out.write(reinterpret_cast<const char*>(pixels.data()+size_t(y)*width*3),width*3);
    if(!out)throw std::runtime_error("Cannot write opener capture: "+path.string());
}
}
void previewOpeners(Renderer &renderer,SDL_Window *window,
                    const std::filesystem::path &out,nlohmann::json &frames) {
    std::filesystem::create_directories(out);
    constexpr float dt=1.f/30;
    for(auto kind:{InsertionKind::Drop,InsertionKind::Warp,InsertionKind::Water}) {
        const char *name=kind==InsertionKind::Drop?"drop":kind==InsertionKind::Warp?"warp":"water";
        const int count=kind==InsertionKind::Drop?180:kind==InsertionKind::Warp?102:174;
        Session game(7);game.add(1,"Insertion QA");
        GameSettings settings;
        settings.location=kind==InsertionKind::Water?4:3;
        settings.environment=kind==InsertionKind::Water?3:0;
        settings.opener=kind==InsertionKind::Drop?0:kind==InsertionKind::Warp?2:4;
        settings.objective=3;settings.enemy=3;settings.enemyCount=2;settings.tactic=2;
        game.command(1,{{"op","settings"},{"settings",settings}});
        game.command(1,{{"op","ready"},{"value",true}});
        game.command(1,{{"op","deploy"}});
        // A labeled local fixture demonstrates the authoritative landing damage,
        // wreck and debris path. No renderer-created damage/effects are used.
        auto landing=game.member(1).body.position;
        landing.y=game.world.surfaceHeight(landing.x,landing.z);
        auto &actor=game.world.targets[0];
        actor.origin={landing.x-8,0,landing.z-10};
        actor.origin.y=game.world.surfaceHeight(actor.origin.x,actor.origin.z);
        game.world.updateEnemyBounds(actor);
        Target building;building.origin={landing.x+10,0,landing.z-12};
        building.origin.y=game.world.surfaceHeight(building.origin.x,building.origin.z);
        applyOriginalModel(building,"building");building.health=40;building.spawnType=-1;
        const size_t buildingIndex=game.world.targets.size();
        game.world.targets.push_back(building);
        const int pilotHp=game.member(1).sheet.hp,pilotAp=game.member(1).sheet.ap;
        renderer.resetCockpit();
        Frontend menu;menu.screen=Screen::Playing;
        int fixedWidth=0,fixedHeight=0;
        Input input;input.focused=true;input.headValid=true;
        for(int frame=0;frame<count;++frame) {
            SDL_Event event;while(SDL_PollEvent(&event)) {
                if(event.type==SDL_QUIT)throw std::runtime_error("Opener capture cancelled");
            }
            for(auto &enemy:game.enemies)enemy.disabled=10000;
            if(frame) {
                // This is the actual fixed-step Session path, not a sampled
                // pose override. Input injection has no socket or transport.
                game.command(1,{{"op","input"},{"epoch",game.epoch},
                                {"input",coop::inputWire(input)},{"systems",input.ping}});
                game.step(dt);
            }
            auto world=game.view(1,renderer.armRig());
            if(world.rpgInsertion!=int(kind))throw std::runtime_error("Outdoor opener selected the wrong insertion kind");
            world.environment={Lighting::Day,Weather::Clear,7};
            menu.remotePilots=world.mapPilots;
            renderer.updateCockpit(world,frame?dt:0,true);
            int width,height;SDL_GL_GetDrawableSize(window,&width,&height);
            if(width<1||height<1)throw std::runtime_error("No opener framebuffer");
            if(frame==0){fixedWidth=width;fixedHeight=height;}
            if(width!=fixedWidth||height!=fixedHeight)throw std::runtime_error("Opener capture dimensions changed mid-sequence");
            auto eye=world.eye();
            auto camera=multiply(translation(eye.x,eye.y,eye.z),rotation(world.facing()));
            float vertical=.65f,horizontal=std::atan(std::tan(vertical)*float(width)/height);
            auto lens=projection({-horizontal,horizontal,vertical,-vertical},.05f,3000);
            const auto &fx=renderer.cockpitEffects();
            auto clock=fx.clock;auto seat=fx.seat,hull=fx.hull;auto sample=fx.insertion;
            if(kind==InsertionKind::Water){
                if(world.waterDepthAt(world.position)<=.2f)throw std::runtime_error("Water opener preview has no real entry canal");
                if(eye.y+sample.cameraOffsetY-world.rpgInsertionGround<.049f)
                    throw std::runtime_error("Water presentation eye penetrated the canal bed");
                if(std::abs(world.position.y-world.rpgInsertionGround)>.001f)
                    throw std::runtime_error("Water opener moved its physical body below/above the valid bed");
            }
            for(float offset:{-.032f,.032f}) {
                glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width,height);
                renderer.draw(world,lens,multiply(camera,translation(offset,0,0)),camera,false,&menu);
                if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Native opener stereo GL error");
                const auto &drawn=renderer.cockpitEffects();
                if(drawn.clock!=clock||drawn.seat!=seat||drawn.hull!=hull||drawn.insertion.time!=sample.time)
                    throw std::runtime_error("An eye draw advanced insertion/cockpit state");
            }
            char filename[40];std::snprintf(filename,sizeof filename,"%s-%04d.ppm",name,frame);
            captureOpener(out/filename,width,height);SDL_GL_SwapWindow(window);
            frames.push_back({{"id",std::string(name)+"-"+std::to_string(frame)},
                {"file",filename},{"sequence",name},{"frame",frame},{"fps",30},
                {"width",width},{"height",height},{"stereo_views",2},
                {"opener_time",game.mission.openerTime},{"altitude",sample.altitude},
                {"vertical_speed",sample.verticalSpeed},{"airbrake",sample.brake},
                {"water_depth",sample.waterDepth},{"camera_offset_y",sample.cameraOffsetY},
                {"camera_y",eye.y+sample.cameraOffsetY},{"body_y",world.position.y},
                {"submersion",sample.submersion},{"wetness",sample.wetness},{"bubbles",sample.bubbles},
                {"phase",insertionPhaseName(sample)},{"controls_locked",sample.controlsLocked},
                {"landed",sample.landed},{"arrival_scale",sample.arrivalScale},
                {"effects",world.effects.size()},{"destroyed_enemies",world.destroyed},
                {"fixture_enemy_hp",game.enemies[0].hp},
                {"fixture_scenery_hp",game.world.targets[buildingIndex].health},
                {"pilot_hp",game.member(1).sheet.hp},{"pilot_ap",game.member(1).sheet.ap}});
        }
        if(game.member(1).sheet.hp!=pilotHp||game.member(1).sheet.ap!=pilotAp)
            throw std::runtime_error("Insertion capture damaged its pilot");
        if(kind==InsertionKind::Drop&&(game.enemies[0].hp>0||game.world.targets[buildingIndex].health>0))
            throw std::runtime_error("Native drop did not crush the labeled landing fixtures");
        if(kind!=InsertionKind::Drop&&(game.enemies[0].hp<=0||game.world.targets[buildingIndex].health<=0))
            throw std::runtime_error("Non-drop opener caused physical landing-crush damage");
        if(kind==InsertionKind::Water){
            const auto& finish=renderer.cockpitEffects().insertion;
            if(finish.active||finish.cameraOffsetY!=0||finish.submersion!=0||finish.wetness!=0||finish.bubbles!=0)
                throw std::runtime_error("Canal overlay/offset persists after emergence");
        }
    }
}
