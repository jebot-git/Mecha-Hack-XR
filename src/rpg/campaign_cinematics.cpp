#include "rpg/campaign_cinematics.hpp"
#include "rpg/campaign_story.hpp"
#include "rpg/session.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace iron::rpg {
namespace {
using Json=nlohmann::json;
std::string value(const Json &j,const char *field){return j.at(field).get<std::string>();}
std::string identity(const Session &s,const MissionRecipe &r) {
    std::ostringstream key;
    // Epoch changes on ready/unready and is deliberately excluded. Narrative
    // legacy and board revisions distinguish new campaign generations instead.
    key<<s.seed<<':'<<s.boardRevision<<':'<<s.campaign.seed<<':'<<s.campaign.revision
       <<':'<<r.storyArc<<':'<<r.legacyArc<<':'<<r.legacyEnding<<':'<<r.legacyScars;
    return key.str();
}
void panels(FoldoutFrame &f,const Json &arc,int rotation) {
    const auto id=value(arc,"id");
    for(int i=0;i<3;++i){
        f.panels[size_t(i)].art="foldout-"+id+'-'+std::to_string((i+rotation)%3);
        f.panels[size_t(i)].foreground="foldout-"+id+"-actor";
    }
}
bool finalSector(const Session &s){return s.mission.mapCount>0&&s.mission.mapIndex>=0&&s.mission.mapIndex+1==s.mission.mapCount;}
}
std::optional<CampaignCinematic> campaignCinematic(const Session &s) {
    const auto &r=s.mission.recipe;
    if(r.storyArc<0||r.storyArc>=campaignStoryCount||r.storyApproach<0||r.storyApproach>2)return std::nullopt;
    if(s.phase!=Phase::Briefing&&s.phase!=Phase::Complete)return std::nullopt;
    const auto &all=campaignStoryCatalog().at("campaigns");
    const auto &arc=all.at(size_t(r.storyArc));
    CampaignCinematic result;
    auto &f=result.frame;f.active=true;f.title=value(arc,"title");
    f.motif=r.storyArc;f.approach=r.storyApproach;f.objective=r.objective;
    f.visualOnly=true;f.collateral=r.storyCollateral;f.legacyScars=r.legacyScars;
    result.key=identity(s,r);
    if(s.phase==Phase::Briefing) {
        if(r.finale||r.campaignStage!=1||s.campaign.completed)return std::nullopt;
        result.key+="/intro";f.kind=FoldoutKind::Introduction;f.duration=18;
        f.label="CAMPAIGN OPENING";panels(f,arc,0);
        f.panels[0].caption=value(arc,"setting");
        f.panels[1].caption=value(arc,"premise");
        f.panels[2].caption=value(arc,"question");
        // A continuation recalls the previous local ending, never the future
        // conclusion of this framework or a conditional unearned clue.
        if(r.legacyArc>=0&&r.legacyArc<campaignStoryCount&&r.legacyEnding>=0&&r.legacyEnding<3){
            const auto &previous=all.at(size_t(r.legacyArc)).at("finales").at(size_t(r.legacyEnding));
            f.panels[0].caption="AFTERMATH: "+value(previous,"title")+". "+value(arc,"setting");
        }
        return result;
    }
    if(!finalSector(s))return std::nullopt;
    if(r.finale){
        if(!s.campaign.victorious||!campaignFinale(s.campaign)||s.campaign.storyArc!=r.storyArc||
           storyDominant(s.campaign.storyTracks,s.campaign.storyLastApproach)!=r.storyApproach)return std::nullopt;
        const auto &ending=arc.at("finales").at(size_t(r.storyApproach));
        result.key+="/ending/"+std::to_string(r.storyApproach);
        f.kind=FoldoutKind::Ending;f.duration=20;f.label="CAMPAIGN ENDING";
        panels(f,arc,r.storyApproach);
        f.panels[0].caption=value(ending,"title");
        f.panels[1].caption=value(ending,"payoff");
        f.panels[2].caption=value(ending,"continuation");
        return result;
    }
    if(r.campaignStage<1||r.campaignStage>9||s.selectedMission<0||s.selectedMission>=9||
       !(s.campaign.completed&(1u<<unsigned(s.selectedMission)))||s.campaign.storyArc!=r.storyArc)return std::nullopt;
    const auto &record=s.campaign.history[size_t(s.selectedMission)];
    const auto &earned=record.context;
    f.collateral=std::clamp(r.storyCollateral+(record.structuresLost>=8?1:0),0,9);
    if(!record.complete||earned.storyArc!=r.storyArc||earned.stage!=r.campaignStage||
       earned.storyApproach!=r.storyApproach||earned.storyFront!=r.storyFront)return std::nullopt;
    const auto &beat=arc.at("beats").at(size_t(earned.stage-1));
    const auto &choice=beat.at("offers").at(size_t(earned.storyApproach));
    const auto &thread=arc.at("threads").at(size_t(earned.storyApproach));
    result.key+="/aftermath/"+std::to_string(s.selectedMission)+'/'+std::to_string(record.boardSeed)+'/'+std::to_string(earned.stage)+'/'+std::to_string(earned.storyApproach)+'/'+std::to_string(earned.storyFront);
    f.kind=FoldoutKind::Intermission;f.duration=16;
    f.label="AFTERMATH "+std::to_string(earned.stage)+" OF 9";panels(f,arc,earned.storyApproach);
    f.panels[0].caption=value(choice,"title")+". "+value(choice,"result");
    f.panels[1].caption=value(thread,"label")+". "+value(thread,"tradeoff");
    f.panels[2].caption=record.structuresLost>=8?
        "Damaged routes will need rebuilding. The next operation inherits that pressure.":
        earned.stage==9?"Nine contracts are complete. The final operation remains ahead.":
        "The next offers will answer the squad's priorities and the state of the front.";
    return result;
}
FoldoutFrame CampaignCinematics::update(const Session &s,float dt,bool focused,bool dismissPressed) {
    auto candidate=campaignCinematic(s);
    if(!candidate){current=FoldoutFrame{};key.clear();return current;}
    if(candidate->key!=key){
        current=FoldoutFrame{};key.clear();
        if(presented.contains(candidate->key))return current;
        key=candidate->key;current=std::move(candidate->frame);presented.insert(key);
        return current; // An input that entered this screen cannot also skip it.
    }
    if(!current.active)return current;
    if(focused){
        if(dismissPressed)current.active=false;
        else if(std::isfinite(dt)&&dt>0){
            current.elapsed=std::min(current.duration,current.elapsed+std::min(dt,.25f));
            if(current.elapsed>=current.duration)current.active=false;
        }
    }
    return current;
}
} // namespace iron::rpg
