#include "rpg/campaign_cinematics.hpp"
#include "rpg/campaign_story.hpp"
#include "rpg/original_assets.hpp"
#include "rpg/session.hpp"
#include "presentation.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
using namespace iron;
using namespace iron::rpg;
namespace {
Session fixture(int arc,int stage,int choice,bool finale=false){
    Session s(7381);s.selectedMission=stage-1;s.phase=Phase::Complete;
    auto &r=s.mission.recipe;r.seed=473;r.storyArc=arc;r.storyApproach=choice;r.campaignStage=stage;r.storyFront=1;r.finale=finale;
    s.campaign.storyArc=arc;s.campaign.completed=(1u<<unsigned(stage))-1;
    s.campaign.storyTracks[size_t(choice)]=stage;s.campaign.storyLastApproach=choice;
    auto &record=s.campaign.history[size_t(stage-1)];record.complete=true;record.boardSeed=873;record.context.storyArc=arc;record.context.storyApproach=choice;record.context.storyFront=1;record.context.stage=stage;
    if(finale){s.campaign.victorious=true;r.campaignStage=10;}
    return s;
}
size_t maxRows=0;
std::set<int> layoutsSeen,actionsSeen,camerasSeen;
void validate(const FoldoutFrame &frame){
    assert(frame.active&&frame.visualOnly&&frame.title.size()<=65&&frame.duration>=16);
    layoutsSeen.insert(int(foldoutLayout(frame)));
    for(const auto &p:frame.panels){
        assert(p.art.starts_with("foldout-")&&!p.caption.empty()&&p.foreground.starts_with("foldout-")&&p.foreground.ends_with("-actor"));
        auto rows=missionLines(p.caption,37).size();maxRows=std::max(maxRows,rows);
        if(rows>5)std::cerr<<frame.title<<": caption "<<rows<<" rows: "<<p.caption<<'\n';
        assert(rows<=5);
    }
    auto f=frame;
    for(float t=-1;t<=frame.duration+1;t+=.125f){f.elapsed=t;for(int panel=0;panel<3;++panel){
        auto p=sampleFoldout(f,panel);
        for(float v:{p.x,p.y,p.z,p.yaw,p.pitch,p.opacity,p.captionOpacity})assert(std::isfinite(v));
        assert(p.z<=-2.89f&&p.z>=-5.f&&std::abs(p.x)<=1.8f&&std::abs(p.y)<=1.1f&&std::abs(p.yaw)<=1.46f);
        assert(std::isfinite(p.width)&&std::isfinite(p.height)&&std::isfinite(p.roll));
        assert(p.width>=1.f&&p.width<=3.2f&&p.height>=.7f&&p.height<=1.8f&&std::abs(p.roll)<=.081f);
        assert(p.captionOpacity==0); // text is developer/source data, never visible narration
        assert(p.opacity>=0&&p.opacity<=1&&p.captionOpacity>=0&&p.captionOpacity<=1);
        assert(p.captionOpacity==0||std::abs(p.yaw)<.12f);
        auto motion=sampleFoldoutMotion(f,panel),again=sampleFoldoutMotion(f,panel);
        for(float v:{motion.shotTime,motion.focus,motion.anticipation,motion.drive,motion.impact,motion.settle,
                     motion.backgroundX,motion.backgroundY,motion.backgroundScale,
                     motion.foregroundX,motion.foregroundY,motion.foregroundScale,motion.foregroundRoll,
                     motion.echo,motion.captionOpacity,motion.cameraX,motion.cameraY,motion.cameraZoom,motion.cameraRoll})assert(std::isfinite(v));
        actionsSeen.insert(int(motion.action));camerasSeen.insert(int(motion.camera));
        assert(motion.captionOpacity==0&&motion.camera==foldoutCamera(motion.action,motion.cel));
        assert(std::abs(motion.cameraX)<=.13f&&std::abs(motion.cameraY)<=.11f&&motion.cameraZoom>=1&&motion.cameraZoom<=1.5f&&std::abs(motion.cameraRoll)<=.03f);
        assert(int(motion.cue)>=0&&int(motion.cue)<=int(FoldoutCue::Archive));
        assert(motion.focus>=0&&motion.focus<=1&&motion.cel>=0&&motion.cel<foldoutActionFrameCount(motion.action));
        if(motion.shotTime>=0){
            assert(motion.gesture==foldoutActionGesture(motion.action));
            if(f.kind==FoldoutKind::Introduction||f.kind==FoldoutKind::Ending||panel>0)
                assert(motion.action!=FoldoutAction::Rifle&&motion.action!=FoldoutAction::Melee);
        }
        assert(std::abs(motion.backgroundX)<=.05f&&std::abs(motion.backgroundY)<=.05f&&motion.backgroundScale>=1&&motion.backgroundScale<=1.15f);
        assert(std::abs(motion.foregroundX)<=.15f&&std::abs(motion.foregroundY)<=.15f&&std::abs(motion.foregroundRoll)<=.15f);
        assert(motion.foregroundScale>=.9f&&motion.foregroundScale<=1.2f);
        assert(motion.effectCount<=64&&motion.effectCount==again.effectCount&&motion.foregroundX==again.foregroundX&&motion.focus==again.focus);
        assert(p.captionOpacity==motion.captionOpacity);
        if(motion.captionOpacity>0){
            assert(motion.effectCount==0&&motion.echo==0&&std::abs(motion.foregroundRoll)<.00001f);
            assert(motion.settle==1&&motion.backgroundY==0); // readable held pose, never moving type
        }
        for(size_t effect=0;effect<motion.effectCount;++effect){
            const auto &e=motion.effects[effect],&same=again.effects[effect];
            for(float v:{e.x,e.y,e.x2,e.y2,e.radius,e.width,e.opacity,e.roll})assert(std::isfinite(v));
            assert(std::abs(e.x)<=.55f&&std::abs(e.x2)<=.55f&&std::abs(e.y)<=.40f&&std::abs(e.y2)<=.40f);
            assert(e.radius>=0&&e.radius<.35f&&e.width>0&&e.opacity>0&&e.opacity<=1);
            assert(e.kind==same.kind&&e.x==same.x&&e.y==same.y&&e.x2==same.x2&&e.y2==same.y2&&e.opacity==same.opacity);
            if(motion.gesture!=FoldoutGesture::Impact)
                assert(e.kind!=FoldoutEffectKind::Debris&&e.kind!=FoldoutEffectKind::InkSlash);
        }
    }}
    f.elapsed=frame.duration;for(int p=0;p<3;++p)assert(!sampleFoldout(f,p).visible);
}
}
int main(){
    setOriginalAssetRoot(MECHA_ASSETS);const auto &arcs=campaignStoryCatalog().at("campaigns");
    std::set<std::string> ids;
    int intros=0,intermissions=0,endings=0;
    for(int arc=0;arc<8;++arc){
        auto opening=fixture(arc,1,0);opening.phase=Phase::Briefing;opening.campaign.completed=0;
        auto clip=campaignCinematic(opening);assert(clip&&clip->frame.kind==FoldoutKind::Introduction);
        assert(clip->frame.motif==arc&&clip->frame.approach==0);
        validate(clip->frame);ids.insert(clip->key);++intros;
        // An opening contains the question and premise, never an ending or the
        // first contract's success text before it has actually happened.
        assert(clip->frame.panels[1].caption==arcs[size_t(arc)]["premise"].get<std::string>());
        assert(clip->frame.panels[2].caption==arcs[size_t(arc)]["question"].get<std::string>());
        opening.mission.recipe.legacyArc=(arc+1)%8;opening.mission.recipe.legacyEnding=2;
        auto legacy=campaignCinematic(opening);assert(legacy&&legacy->key!=clip->key&&legacy->frame.panels[0].caption.starts_with("AFTERMATH:"));validate(legacy->frame);
        for(int stage=1;stage<=9;++stage)for(int choice=0;choice<3;++choice){
            auto s=fixture(arc,stage,choice);s.campaign.history[size_t(stage-1)].structuresLost=stage%2?0:8;
            s.mission.recipe.objective=arcs[size_t(arc)]["beats"][size_t(stage-1)]["offers"][size_t(choice)]["objective"].get<int>();
            auto c=campaignCinematic(s);assert(c&&c->frame.kind==FoldoutKind::Intermission&&c->frame.motif==arc&&c->frame.approach==choice&&c->frame.objective==s.mission.recipe.objective);validate(c->frame);ids.insert(c->key);++intermissions;
            auto &b=arcs[size_t(arc)]["beats"][size_t(stage-1)];
            assert(c->frame.panels[0].caption.find(b["offers"][size_t(choice)]["result"].get<std::string>())!=std::string::npos);
            assert(c->frame.panels[1].caption.find(arcs[size_t(arc)]["threads"][size_t(choice)]["tradeoff"].get<std::string>())!=std::string::npos);
            for(auto phase:{Phase::Hangar,Phase::Combat,Phase::Defeat}){auto other=s;other.phase=phase;assert(!campaignCinematic(other));}
            auto partial=s;partial.mission.mapCount=2;assert(!campaignCinematic(partial));partial.mission.mapIndex=1;assert(campaignCinematic(partial));
            auto unearned=s;unearned.campaign.history[size_t(stage-1)].complete=false;assert(!campaignCinematic(unearned));
            auto mismatch=s;mismatch.campaign.history[size_t(stage-1)].context.storyApproach=(choice+1)%3;assert(!campaignCinematic(mismatch));
        }
        for(int choice=0;choice<3;++choice){
            auto s=fixture(arc,9,choice,true);auto c=campaignCinematic(s);assert(c&&c->frame.kind==FoldoutKind::Ending);validate(c->frame);ids.insert(c->key);++endings;
            auto &ending=arcs[size_t(arc)]["finales"][size_t(choice)];
            assert(c->frame.panels[1].caption==ending["payoff"].get<std::string>()&&c->frame.panels[2].caption==ending["continuation"].get<std::string>());
            s.campaign.victorious=false;assert(!campaignCinematic(s));
        }
    }
    assert(intros==8&&intermissions==216&&endings==24&&ids.size()==248);
    assert(layoutsSeen.size()==6&&actionsSeen.size()==7&&camerasSeen.size()==6);
    // New artwork banks carry real angle changes and distinct mechanical jobs.
    const std::array<FoldoutAction,7> actionOrder{FoldoutAction::Rifle,FoldoutAction::Guard,FoldoutAction::Scan,FoldoutAction::Carry,FoldoutAction::Melee,FoldoutAction::Launch,FoldoutAction::Command};
    const std::array<const char*,7> names{"rifle","guard","scan","carry","melee","launch","command"};
    for(size_t action=0;action<actionOrder.size();++action){
        assert(std::string(foldoutActionName(actionOrder[action]))==names[action]);
        assert(foldoutCamera(actionOrder[action],0)==FoldoutCamera::Wide);
        assert(foldoutActionFrameCount(actionOrder[action])==(actionOrder[action]==FoldoutAction::Launch?18:3));
        assert(foldoutCamera(actionOrder[action],actionOrder[action]==FoldoutAction::Launch?6:1)!=FoldoutCamera::Wide&&foldoutCamera(actionOrder[action],foldoutActionFrameCount(actionOrder[action])-1)!=FoldoutCamera::Wide);
    }
    {
        FoldoutFrame varied;varied.visualOnly=true;std::set<int> jobs;
        for(int objective=0;objective<10;++objective){varied.objective=objective;jobs.insert(int(foldoutLayout(varied)));}
        assert(jobs.size()==6); // successive jobs need not repeat one fixed campaign page
    }
    for(int objective=0;objective<10;++objective){
        for(int priority=0;priority<3;++priority){
            FoldoutFrame visual;visual.active=true;visual.visualOnly=true;visual.kind=FoldoutKind::Intermission;visual.objective=objective;visual.approach=priority;visual.duration=16;
            visual.elapsed=foldoutShotStart(visual,0)+1.02f;auto action=sampleFoldoutMotion(visual,0);
            const bool combat=objective==0||objective==5||objective==6;
            assert((action.gesture==FoldoutGesture::Impact)==combat);
            if(objective==4)assert(action.action==FoldoutAction::Carry&&action.cue==FoldoutCue::Payload);
            if(objective==1||objective==9)assert(action.action==FoldoutAction::Guard&&action.cue==FoldoutCue::Shield);
            if(objective==3)assert(action.action==FoldoutAction::Scan);
            for(int panel=0;panel<3;++panel){
                const bool launch=foldoutAction(visual,panel)==FoldoutAction::Launch;
                visual.elapsed=foldoutShotStart(visual,panel)+(launch?.90f:.98f);auto held=sampleFoldoutMotion(visual,panel);
                visual.elapsed+=.10f;auto heldAgain=sampleFoldoutMotion(visual,panel);
                if(launch)assert(held.cel==11&&heldAgain.cel==11);
                assert(std::abs(held.foregroundX-heldAgain.foregroundX)<.00001f&&std::abs(held.foregroundY-heldAgain.foregroundY)<.00001f&&held.cel==heldAgain.cel);
                if(panel>0)for(size_t i=0;i<held.effectCount;++i)assert(held.effects[i].kind!=FoldoutEffectKind::Debris&&held.effects[i].kind!=FoldoutEffectKind::InkSlash);
                if(panel<2)assert(!sampleFoldout(visual,panel+1).visible); // no premature inset/story disclosure
            }
            visual.collateral=1;visual.elapsed=foldoutShotStart(visual,1)+.6f;assert(sampleFoldoutMotion(visual,1).cue==FoldoutCue::Damage);
            visual.collateral=0;visual.legacyScars=1;assert(sampleFoldoutMotion(visual,1).cue==FoldoutCue::Damage);
            visual.kind=FoldoutKind::Ending;
            assert(foldoutAction(visual,0)!=FoldoutAction::Rifle&&foldoutAction(visual,0)!=FoldoutAction::Melee);
        }
    }
    {
        FoldoutFrame launch;launch.active=true;launch.visualOnly=true;launch.kind=FoldoutKind::Intermission;launch.objective=2;launch.duration=16;
        std::set<int> gait;
        for(int step=0;step<=340;++step){
            launch.elapsed=foldoutShotStart(launch,0)+float(step)*.005f;
            auto m=sampleFoldoutMotion(launch,0);assert(m.action==FoldoutAction::Launch&&m.cel>=0&&m.cel<18);gait.insert(m.cel);
            assert(m.camera==(m.cel<6?FoldoutCamera::Wide:m.cel<12?FoldoutCamera::LowAngle:FoldoutCamera::HighAngle));
        }
        assert(gait.size()==18);
        launch.elapsed=foldoutShotStart(launch,0)+2.5f;assert(sampleFoldoutMotion(launch,0).cel==17);
    }
    // Every objective chooses symbolic effects appropriate to its actual job.
    // A scan, escort or rescue cannot acquire a fabricated destructive impact.
    for(int objective=0;objective<10;++objective){
        FoldoutFrame f;f.active=true;f.kind=FoldoutKind::Intermission;f.objective=objective;f.elapsed=4.95f;
        auto m=sampleFoldoutMotion(f,0);
        const bool destructive=objective==0||objective==5||objective==6;
        assert((m.gesture==FoldoutGesture::Impact)==destructive&&m.effectCount>0);
        for(size_t i=0;i<m.effectCount;++i)if(!destructive)
            assert(m.effects[i].kind!=FoldoutEffectKind::Debris&&m.effects[i].kind!=FoldoutEffectKind::InkSlash);
        // The foreground and background are exactly held during the impact cel;
        // the surrounding graphic ring may expand without camera/actor shake.
        f.elapsed=4.90f;auto held=sampleFoldoutMotion(f,0);f.elapsed=5.00f;auto heldAgain=sampleFoldoutMotion(f,0);
        assert(std::abs(held.foregroundX-heldAgain.foregroundX)<.00001f&&std::abs(held.foregroundY-heldAgain.foregroundY)<.00001f);
        assert(std::abs(held.foregroundScale-heldAgain.foregroundScale)<.00001f&&std::abs(held.backgroundScale-heldAgain.backgroundScale)<.00001f);
        assert(held.cel==1&&heldAgain.cel==1);
        for(int panel=1;panel<3;++panel){
            f.elapsed=4.f+panel*((f.duration-5.5f)/3.f)+.95f;
            auto later=sampleFoldoutMotion(f,panel);
            assert(later.gesture==(panel==1?FoldoutGesture::Signal:FoldoutGesture::Transit));
            for(size_t i=0;i<later.effectCount;++i)
                assert(later.effects[i].kind!=FoldoutEffectKind::Debris&&later.effects[i].kind!=FoldoutEffectKind::InkSlash);
            assert(std::abs(later.backgroundScale-(1.f+.065f*(panel==1?.72f:.52f)))<.00001f);
            if(destructive)assert(std::abs(later.foregroundX)<std::abs(m.foregroundX));
            f.elapsed=4.f+panel*((f.duration-5.5f)/3.f)+.90f;auto heldLater=sampleFoldoutMotion(f,panel);
            f.elapsed+=.10f;auto heldLaterAgain=sampleFoldoutMotion(f,panel);
            assert(std::abs(heldLater.foregroundX-heldLaterAgain.foregroundX)<.00001f&&heldLater.cel==heldLaterAgain.cel);
        }
        f.kind=FoldoutKind::Introduction;assert(sampleFoldoutMotion(f,0).gesture==FoldoutGesture::Signal);
    }
    {
        FoldoutFrame f;f.active=true;f.kind=FoldoutKind::Intermission;f.objective=0;
        for(int panel=0;panel<3;++panel){
            f.elapsed=4.f+panel*((f.duration-5.5f)/3.f)+.95f;
            auto active=sampleFoldoutMotion(f,panel);assert(active.focus>.99f&&active.effectCount>0&&active.captionOpacity==0);
            if(panel<2)assert(sampleFoldoutMotion(f,panel+1).focus==0); // reveal ordering is causal
        }
        f.elapsed=12.f;f.motif=-999;f.approach=999;
        auto bounded=sampleFoldoutMotion(f,0);assert(std::isfinite(bounded.foregroundX)&&bounded.effectCount<=64);
    }
    auto s=fixture(0,1,0);s.phase=Phase::Briefing;s.campaign.completed=0;
    CampaignCinematics controller;
    auto initial=controller.update(s,.1f,true,true);assert(initial.active&&initial.elapsed==0); // entering press is consumed
    assert(controller.update(s,1.f,false,true).elapsed==0&&controller.active());
    assert(controller.update(s,-1.f,true,false).elapsed==0);
    assert(controller.update(s,std::numeric_limits<float>::infinity(),true,false).elapsed==0);
    assert(controller.update(s,.1f,true,false).elapsed==.1f);
    assert(!controller.update(s,.1f,true,true).active);
    ++s.epoch;assert(!controller.update(s,.1f,true,false).active); // ready/unready is not a new opening
    s.phase=Phase::Hangar;assert(!controller.update(s,.1f,true,false).active);
    s.phase=Phase::Briefing;assert(!controller.update(s,.1f,true,false).active);
    ++s.boardRevision;assert(controller.update(s,.1f,true,false).active);
    controller.cancel();assert(!controller.active()&&!controller.update(s,.1f,true,false).active);
    controller.reset();assert(controller.update(s,.1f,true,false).active);
    auto snapshot=s.snapshot();
    for(int tick=0;tick<150;++tick)controller.update(s,.25f,true,false);
    assert(!controller.active()&&s.snapshot()==snapshot); // playback never advances gameplay
    s.phase=Phase::Combat;assert(!controller.update(s,.1f,true,false).active);
    controller.reset();s.phase=Phase::Briefing;assert(controller.update(s,.1f,true,false).active);
    s.phase=Phase::Combat;assert(!controller.update(s,.1f,true,false).active); // host deployment cancels stale overlay
    auto invalid=initial;invalid.elapsed=std::numeric_limits<float>::quiet_NaN();assert(!sampleFoldout(invalid,0).visible);
    assert(!sampleFoldout(initial,-1).visible&&!sampleFoldout(initial,3).visible);
    assert(sampleFoldoutMotion(invalid,0).effectCount==0&&sampleFoldoutMotion(initial,3).effectCount==0);
    std::cout<<"8 openings,216 earned intermissions,24 endings,6 layouts,7 actions,6 painted camera views,zero narration opacity,finite motion/effects,focus,skip and pure local playback verified; developer-only max caption rows "<<maxRows<<'\n';
}
