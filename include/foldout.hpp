#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace iron {
// Presentation data only: no rules, save, network or gameplay RNG dependency.
enum class FoldoutKind { Introduction, Intermission, Ending };
enum class FoldoutAction { Rifle, Guard, Scan, Carry, Melee, Launch, Command };
enum class FoldoutCamera { Wide, LowAngle, Profile, OverShoulder, Detail, HighAngle };
enum class FoldoutLayout { Triptych, HeroInset, SplitStack, Letterbox, Diagonal, PortraitCascade };
enum class FoldoutCue { None, Shield, Relay, Payload, Route, Damage, Archive };
inline const char *foldoutActionName(FoldoutAction action) {
    switch(action){
        case FoldoutAction::Rifle:return "rifle";case FoldoutAction::Guard:return "guard";
        case FoldoutAction::Scan:return "scan";case FoldoutAction::Carry:return "carry";
        case FoldoutAction::Melee:return "melee";case FoldoutAction::Launch:return "launch";
        default:return "command";
    }
}
inline int foldoutActionFrameCount(FoldoutAction action) {
    return action==FoldoutAction::Launch?18:3;
}
struct FoldoutPanel {
    std::string art;
    std::string caption;
    std::string foreground; // Optional shared actor prefix; append -0/-1/-2.
};
struct FoldoutFrame {
    bool active=false;
    float elapsed=0,duration=32;
    FoldoutKind kind=FoldoutKind::Introduction;
    std::string title,label;
    std::array<FoldoutPanel,3> panels;
    int approach=0,motif=0,objective=-1;
    bool visualOnly=false; // Authored campaign comics draw no narration or title text.
    int collateral=0,legacyScars=0; // Earned damage only, never an invented casualty.

};
struct FoldoutPose {
    // Metres in the frontend base frame. Cards are centred here, 1.16 x .87 m.
    float x=0,y=.25f,z=-3,yaw=0,pitch=0,opacity=0,captionOpacity=0;
    bool visible=false;
    float width=1.16f,height=.87f,roll=0;

};
enum class FoldoutEffectKind { SpeedLine, InkSlash, ShockRing, Debris, Halftone };
enum class FoldoutGesture { Impact, Protection, Signal, Transit };
struct FoldoutEffect {
    FoldoutEffectKind kind=FoldoutEffectKind::SpeedLine;
    // Metres local to the illustration centre; never enter its caption field.
    float x=0,y=0,x2=0,y2=0,radius=0,width=.003f,opacity=0,roll=0;
};
struct FoldoutMotion {
    FoldoutGesture gesture=FoldoutGesture::Signal;
    FoldoutAction action=FoldoutAction::Command;
    FoldoutCamera camera=FoldoutCamera::Wide;
    FoldoutCue cue=FoldoutCue::None;
    // Normalised image-centre offsets and lens scale INSIDE the comic. No eye/head motion.
    float cameraX=0,cameraY=0,cameraZoom=1,cameraRoll=0;

    float shotTime=0,focus=0,anticipation=0,drive=0,impact=0,settle=0;
    float backgroundX=0,backgroundY=0,backgroundScale=1;
    float foregroundX=0,foregroundY=0,foregroundScale=1,foregroundRoll=0;
    float echo=0,captionOpacity=0;
    // Three authored cels per action, or eighteen distinct launch gait cels.
    int cel=0;
    std::array<FoldoutEffect,64> effects{};
    size_t effectCount=0;
};
inline float foldoutEase(float t) {
    t=std::clamp(t,0.f,1.f);
    return t*t*(3.f-2.f*t);
}
inline bool validFoldoutSample(const FoldoutFrame &f,int panel) {
    return f.active&&panel>=0&&panel<3&&std::isfinite(f.elapsed)&&
           std::isfinite(f.duration)&&f.duration>0;
}
inline float foldoutNoise(uint32_t x) {
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;
    return float(x&0xffffffu)/16777215.f;
}
inline float foldoutCaptionOpacity(const FoldoutFrame &f,int panel) {
    if(!validFoldoutSample(f,panel)||f.visualOnly)return 0;
    const float t=std::clamp(f.elapsed,0.f,f.duration);
    const float shotDuration=std::max(3.f,(f.duration-5.5f)/3.f);
    const float a=t-(4.f+float(panel)*shotDuration);
    return foldoutEase((a-1.85f)/.45f)*
           (1.f-foldoutEase((t-(f.duration-1.8f))/.6f));
}
inline FoldoutGesture foldoutGesture(const FoldoutFrame &f) {
    if(f.kind==FoldoutKind::Introduction)return FoldoutGesture::Signal;
    switch(f.objective){
        case 0:case 5:case 6:return FoldoutGesture::Impact;
        case 1:case 9:return FoldoutGesture::Protection;
        case 2:case 4:case 8:return FoldoutGesture::Transit;
        default:return FoldoutGesture::Signal;
    }
}
inline float foldoutShotDuration(const FoldoutFrame &f) {
    return std::max(3.f,(f.duration-(f.visualOnly?3.2f:5.5f))/3.f);
}
inline float foldoutShotStart(const FoldoutFrame &f,int panel) {
    return (f.visualOnly?1.5f:4.f)+float(panel)*foldoutShotDuration(f);
}
inline FoldoutLayout foldoutLayout(const FoldoutFrame &f) {
    if(!f.visualOnly)return FoldoutLayout::Triptych;
    const int variant=std::clamp(f.motif,0,7)+2*std::clamp(f.approach,0,2)+3*std::clamp(int(f.kind),0,2)+std::clamp(f.objective,0,9);
    return FoldoutLayout(variant%6);
}
inline FoldoutAction foldoutAction(const FoldoutFrame &f,int panel) {
    const int priority=std::clamp(f.approach,0,2);
    if(f.kind==FoldoutKind::Introduction)
        return panel==0?FoldoutAction::Command:panel==1?FoldoutAction::Launch:FoldoutAction::Scan;
    if(f.kind==FoldoutKind::Ending){
        if(panel==0)return priority==0?FoldoutAction::Guard:priority==1?FoldoutAction::Scan:FoldoutAction::Command;
        return panel==1?(priority==2?FoldoutAction::Guard:FoldoutAction::Command):FoldoutAction::Launch;
    }
    if(panel==1)return priority==0?FoldoutAction::Guard:priority==1?FoldoutAction::Scan:FoldoutAction::Command;
    if(panel==2)return f.objective==2||f.objective==8?FoldoutAction::Launch:FoldoutAction::Command;
    switch(f.objective){
        case 0:case 5:case 6:return priority==2?FoldoutAction::Melee:FoldoutAction::Rifle;
        case 1:case 9:return FoldoutAction::Guard;
        case 2:case 8:return FoldoutAction::Launch;
        case 3:return FoldoutAction::Scan;
        case 4:return FoldoutAction::Carry;
        default:return FoldoutAction::Command;
    }
}
inline FoldoutCamera foldoutCamera(FoldoutAction action,int cel) {
    const int view=action==FoldoutAction::Launch?std::clamp(cel,0,17)/6:std::clamp(cel,0,2);
    if(view==0)return FoldoutCamera::Wide;
    if(view==1){
        if(action==FoldoutAction::Scan)return FoldoutCamera::OverShoulder;
        if(action==FoldoutAction::Carry||action==FoldoutAction::Command)return FoldoutCamera::Profile;
        return FoldoutCamera::LowAngle;
    }
    if(action==FoldoutAction::Rifle||action==FoldoutAction::Scan)return FoldoutCamera::Detail;
    if(action==FoldoutAction::Melee)return FoldoutCamera::Profile;
    if(action==FoldoutAction::Launch)return FoldoutCamera::HighAngle;
    return FoldoutCamera::OverShoulder;
}
inline FoldoutGesture foldoutActionGesture(FoldoutAction action) {
    if(action==FoldoutAction::Rifle||action==FoldoutAction::Melee)return FoldoutGesture::Impact;
    if(action==FoldoutAction::Guard)return FoldoutGesture::Protection;
    if(action==FoldoutAction::Carry||action==FoldoutAction::Launch)return FoldoutGesture::Transit;
    return FoldoutGesture::Signal;
}
inline FoldoutCue foldoutCue(const FoldoutFrame &f,int panel,FoldoutAction action) {
    if(panel==1&&(f.collateral>0||f.legacyScars>0))return FoldoutCue::Damage;
    if(panel==1&&f.approach==1)return FoldoutCue::Archive;
    switch(action){
        case FoldoutAction::Guard:return FoldoutCue::Shield;
        case FoldoutAction::Scan:return f.approach==1?FoldoutCue::Archive:FoldoutCue::Relay;
        case FoldoutAction::Carry:return FoldoutCue::Payload;
        case FoldoutAction::Launch:case FoldoutAction::Command:return FoldoutCue::Route;
        default:return FoldoutCue::None;
    }
}
struct FoldoutGeometry {float x,y,z,width,height,roll;};
inline FoldoutGeometry foldoutGeometry(const FoldoutFrame &f,int panel) {
    const int p=std::clamp(panel,0,2);
    if(!f.visualOnly)return {float(p-1)*1.22f,.25f,p==1?-3.f:-3.12f,1.16f,.87f,0};
    using G=FoldoutGeometry;
    switch(foldoutLayout(f)){
        case FoldoutLayout::HeroInset:return std::array<G,3>{{{-.18f,.10f,-3.30f,2.70f,1.55f,0},{1.f,.45f,-2.93f,1.15f,.73f,-.055f},{.94f,-.48f,-2.90f,1.28f,.80f,.04f}}}[size_t(p)];
        case FoldoutLayout::SplitStack:return std::array<G,3>{{{-.82f,0,-3.18f,1.45f,1.75f,-.025f},{.89f,.47f,-3.03f,1.45f,.82f,.03f},{.89f,-.47f,-3.f,1.45f,.82f,-.02f}}}[size_t(p)];
        case FoldoutLayout::Letterbox:return std::array<G,3>{{{0,.52f,-3.20f,3.20f,.83f,0},{-.82f,-.46f,-3.02f,1.55f,.77f,-.025f},{.82f,-.46f,-3.f,1.55f,.77f,.025f}}}[size_t(p)];
        case FoldoutLayout::Diagonal:return std::array<G,3>{{{-1.12f,.16f,-3.14f,1.22f,1.20f,-.08f},{0,-.10f,-3.04f,1.30f,1.48f,.065f},{1.15f,.08f,-3.08f,1.16f,1.12f,-.055f}}}[size_t(p)];
        case FoldoutLayout::PortraitCascade:return std::array<G,3>{{{-.98f,.04f,-3.20f,1.03f,1.68f,-.045f},{0,.08f,-3.10f,1.10f,1.42f,.045f},{1.04f,-.02f,-3.f,1.03f,1.60f,-.025f}}}[size_t(p)];
        default:return std::array<G,3>{{{-1.18f,.02f,-3.12f,1.12f,.84f,-.04f},{0,.12f,-3.f,1.12f,.98f,.02f},{1.18f,-.08f,-3.16f,1.12f,.78f,.05f}}}[size_t(p)];
    }
}
inline FoldoutMotion sampleFoldoutMotion(const FoldoutFrame &f,int panel) {
    FoldoutMotion m;
    if(!validFoldoutSample(f,panel))return m;
    const float t=std::clamp(f.elapsed,0.f,f.duration);
    const float shotDuration=foldoutShotDuration(f);
    const float a=t-foldoutShotStart(f,panel);
    m.shotTime=a;m.action=foldoutAction(f,panel);m.camera=foldoutCamera(m.action,0);
    m.cue=foldoutCue(f,panel,m.action);
    if(a<0)return m;
    const int approach=std::clamp(f.approach,0,2),motif=std::clamp(f.motif,0,7);
    // First shot carries the mission action. Later shots read the earned
    // tradeoff and continuation, not second and third combat victories.
    m.gesture=f.kind==FoldoutKind::Introduction?FoldoutGesture::Signal:
        panel==1?FoldoutGesture::Signal:panel==2?FoldoutGesture::Transit:foldoutGesture(f);
    if(f.visualOnly)m.gesture=foldoutActionGesture(m.action);
    const bool impactGesture=m.gesture==FoldoutGesture::Impact;
    const float panelGain=panel==0?1.f:panel==1?.72f:.52f;
    const float direction=((motif+approach+panel)&1)?-1.f:1.f;
    m.focus=foldoutEase(a/.3f)*(1.f-foldoutEase((a-shotDuration+.7f)/.7f));
    m.anticipation=foldoutEase(a/.4f)*(1.f-foldoutEase((a-.4f)/.22f));
    m.drive=foldoutEase((a-.4f)/.45f);
    m.settle=foldoutEase((a-1.03f)/.62f);
    m.impact=foldoutEase((a-.80f)/.05f)*(1.f-foldoutEase((a-1.03f)/.47f));
    m.cel=a<.4f?0:a<1.03f?1:2;
    const float push=m.drive*(1.f-m.settle);
    const float anticipation=foldoutEase(a/.4f);
    // A mechanical anticipation, fast thrust, exact impact hold and recovery.
    // Only illustrations move: lettering, body anchor and eye cameras do not.
    if(a<.4f){
        m.foregroundX=-direction*.045f*anticipation;
        m.foregroundY=.025f*anticipation;
        m.foregroundScale=1.f-.025f*anticipation;
        m.foregroundRoll=-direction*.035f*anticipation;
    }else{
        m.foregroundX=direction*((-.045f+.145f*m.drive)*(1.f-m.settle)+.012f*m.settle);
        m.foregroundY=(.025f+.05f*m.drive)*(1.f-m.settle)+.028f*m.settle;
        m.foregroundScale=(.975f+.155f*m.drive)*(1.f-m.settle)+1.015f*m.settle;
        m.foregroundRoll=direction*(-.035f+.10f*m.drive)*(1.f-m.settle);
    }
    m.backgroundX=-direction*(.025f*m.drive*(1.f-m.settle)+.012f*m.settle);
    m.backgroundY=.012f*push;
    m.backgroundScale=1.f+.065f*m.drive*(1.f-m.settle)+.035f*m.settle;
    m.backgroundX*=panelGain;m.backgroundY*=panelGain;
    m.backgroundScale=1.f+(m.backgroundScale-1.f)*panelGain;
    const float gestureGain=panelGain*(impactGesture?1.f:m.gesture==FoldoutGesture::Protection?.72f:.6f);
    m.foregroundX*=gestureGain;m.foregroundY*=gestureGain;m.foregroundRoll*=gestureGain;
    m.foregroundScale=1.f+(m.foregroundScale-1.f)*gestureGain;
    m.echo=gestureGain*foldoutEase((a-.4f)/.12f)*(1.f-foldoutEase((a-.72f)/.16f));
    if(f.visualOnly){
        const float visualGain=panel==0?1.f:panel==1?.85f:.75f;
        m.anticipation=foldoutEase(a/.42f)*(1.f-foldoutEase((a-.42f)/.24f));
        m.drive=foldoutEase((a-.42f)/.50f);
        m.settle=foldoutEase((a-1.16f)/.79f);
        m.impact=foldoutEase((a-.87f)/.05f)*(1.f-foldoutEase((a-1.16f)/.58f));
        m.cel=a<.42f?0:a<1.30f?1:2;
        if(m.action==FoldoutAction::Launch){
            // The authored launch sheet contains eighteen distinct poses: six
            // compress/rise cels, six low-angle stride cels, six landing cels.
            // Keep the action held at frame 11 while its drive is fully loaded.
            m.anticipation=foldoutEase(a/.4f)*(1.f-foldoutEase((a-.4f)/.22f));
            m.drive=foldoutEase((a-.4f)/.45f);
            m.settle=foldoutEase((a-1.03f)/.62f);
            m.impact=foldoutEase((a-.80f)/.05f)*(1.f-foldoutEase((a-1.03f)/.47f));
            if(a<.4f)m.cel=std::min(5,int(foldoutEase(a/.4f)*6));
            else if(a<1.03f)m.cel=6+std::min(5,int(m.drive*6));
            else m.cel=12+std::min(5,int(m.settle*6));
        }
        m.camera=foldoutCamera(m.action,m.cel);
        const float ready=foldoutEase(a/(m.action==FoldoutAction::Launch?.4f:.42f)),push=m.drive*(1.f-m.settle);
        m.foregroundX=m.foregroundY=m.foregroundRoll=0;m.foregroundScale=1;
        switch(m.action){
            case FoldoutAction::Rifle:
                m.foregroundX=-direction*(.025f*ready*(1.f-m.drive)+.075f*push);
                m.foregroundY=.020f*push;m.foregroundRoll=-direction*.055f*push;m.foregroundScale=1+.05f*push;break;
            case FoldoutAction::Melee:
                m.foregroundX=direction*((-.05f*ready+.16f*m.drive)*(1.f-m.settle)+.010f*m.settle);
                m.foregroundY=.025f*push;m.foregroundRoll=std::clamp(direction*(-.07f*ready+.18f*m.drive)*(1.f-m.settle),-.15f,.15f);m.foregroundScale=1+.08f*push;break;
            case FoldoutAction::Guard:
                m.foregroundX=-direction*.035f*m.drive;m.foregroundY=-.040f*m.drive+.025f*m.settle;
                m.foregroundRoll=-direction*.03f*push;m.foregroundScale=1+.06f*push+.015f*m.settle;break;
            case FoldoutAction::Scan:
                m.foregroundX=direction*.012f*m.drive;m.foregroundY=.018f*push;
                m.foregroundScale=1+.025f*m.drive;break;
            case FoldoutAction::Carry:
                m.foregroundX=direction*.05f*m.drive;
                m.foregroundY=-.070f*ready*(1.f-m.drive)+.090f*push+.025f*m.settle;
                m.foregroundRoll=-direction*.04f*push;m.foregroundScale=1+.035f*m.drive;break;
            case FoldoutAction::Launch:
                m.foregroundX=direction*.11f*m.drive*(1.f-.3f*m.settle);
                m.foregroundY=-.025f*ready*(1.f-m.drive)+.145f*push-.010f*m.settle;
                m.foregroundRoll=-direction*.11f*push;m.foregroundScale=1-.08f*push;break;
            case FoldoutAction::Command:
                m.foregroundX=direction*.02f*m.drive;m.foregroundY=.012f*push;
                m.foregroundRoll=direction*.015f*push;m.foregroundScale=1+.02f*m.drive;break;
        }
        m.foregroundX*=visualGain;m.foregroundY*=visualGain;m.foregroundRoll*=visualGain;
        m.foregroundScale=1.f+(m.foregroundScale-1.f)*visualGain;
        m.backgroundX=-direction*.022f*push*visualGain;m.backgroundY=.012f*push*visualGain;
        m.backgroundScale=1+.06f*push*visualGain+.02f*m.settle;
        m.echo=(impactGesture?1.f:.55f)*visualGain*foldoutEase((a-.42f)/.12f)*(1.f-foldoutEase((a-.92f)/.18f));
        // Camera cuts follow the actual painted angle of each cel. These values
        // only reframe world art inside a panel; the tracked camera is untouched.
        switch(m.camera){
            case FoldoutCamera::Wide:m.cameraZoom=1.02f;break;
            case FoldoutCamera::LowAngle:m.cameraZoom=1.12f;m.cameraY=-.06f;m.cameraRoll=direction*.022f;break;
            case FoldoutCamera::Profile:m.cameraZoom=1.12f;m.cameraX=direction*.10f;m.cameraY=.015f;break;
            case FoldoutCamera::OverShoulder:m.cameraZoom=1.28f;m.cameraX=direction*.12f;m.cameraY=.055f;break;
            case FoldoutCamera::Detail:m.cameraZoom=1.48f;m.cameraX=direction*.08f;m.cameraY=.10f;break;
            case FoldoutCamera::HighAngle:m.cameraZoom=1.16f;m.cameraY=-.08f;break;
        }
    }
    m.captionOpacity=foldoutCaptionOpacity(f,panel);
    // Graphic effects are one short gesture per shot, not repeating flashes.
    const float burst=f.visualOnly?
        foldoutEase((a-.35f)/.15f)*(1.f-foldoutEase((a-1.25f)/.65f)):
        foldoutEase((a-.35f)/.15f)*(1.f-foldoutEase((a-1.08f)/.60f));
    if(burst<=.001f)return m;
    const float kindGain=f.kind==FoldoutKind::Introduction?.82f:f.kind==FoldoutKind::Intermission?.9f:1.f;
    const uint32_t seed=0x53c9d4e7u^uint32_t(motif+1)*0x9e3779b9u^
                        uint32_t(approach+1)*0x85ebca6bu^uint32_t(panel+1)*0xc2b2ae35u;
    auto noise=[&](int n){return foldoutNoise(seed^uint32_t(n+1)*0x27d4eb2du);};
    auto add=[&](FoldoutEffect e){
        if(m.effectCount>=m.effects.size())return;
        e.x=std::clamp(e.x,-.55f,.55f);e.x2=std::clamp(e.x2,-.55f,.55f);
        e.y=std::clamp(e.y,-.40f,.40f);e.y2=std::clamp(e.y2,-.40f,.40f);
        e.opacity=std::clamp(e.opacity*kindGain,0.f,1.f);
        if(e.opacity>.001f)m.effects[m.effectCount++]=e;
    };
    const int lines=impactGesture?(motif==6?32:24):m.gesture==FoldoutGesture::Signal?12:18;
    constexpr float tau=6.28318530718f;
    for(int i=0;i<lines;++i){
        const float angle=tau*(float(i)/float(lines)+.04f*noise(i));
        const float inner=.13f+.13f*noise(i+32),outer=.37f+.18f*noise(i+64);
        const float cs=std::cos(angle),sn=std::sin(angle);
        add({FoldoutEffectKind::SpeedLine,cs*inner,sn*inner*.7f+.035f,
             cs*outer,sn*outer*.7f+.035f,0,.002f+.003f*noise(i+96),burst*(impactGesture?.72f:.30f)});
    }
    // Protective rings, scan stipple and diagonal pressure marks vary by the
    // squad's chosen priority. They are symbolic staging, not invented hits.
    const int slashes=impactGesture?(approach==2?4:2):0;
    for(int i=0;i<slashes;++i){
        const float y=-.20f+.13f*float(i),x=-.37f+.12f*float(i);
        add({FoldoutEffectKind::InkSlash,x,y,x+.28f,y+.15f,0,.009f+.002f*i,burst*.8f});
    }
    const int rings=m.gesture==FoldoutGesture::Protection||m.gesture==FoldoutGesture::Signal||approach==0?3:1;
    const float expand=foldoutEase((a-.80f)/.7f);
    for(int i=0;i<rings;++i)
        add({FoldoutEffectKind::ShockRing,.025f,.035f,0,0,.055f+.25f*expand-.012f*i,
             .003f+.001f*i,m.impact*.75f});
    const float age=std::clamp(a-.75f,0.f,1.f);
    for(int i=0;impactGesture&&i<10;++i){
        const float x=(noise(i+140)-.5f)*.70f*age;
        const float y=.03f+(.08f+.20f*noise(i+160))*age-.12f*age*age;
        add({FoldoutEffectKind::Debris,x,y,0,0,.006f+.010f*noise(i+180),
             .004f,burst*.65f,age*(noise(i+200)-.5f)*7.f});
    }
    if(m.gesture==FoldoutGesture::Signal||approach==1||motif==2||motif==4||motif==7)
        for(int i=0;i<10;++i)
            add({FoldoutEffectKind::Halftone,(noise(i+220)-.5f)*.9f,
                 (noise(i+240)-.5f)*.66f,0,0,.004f+.006f*noise(i+260),.002f,burst*.45f});
    return m;
}
inline FoldoutPose sampleFoldout(const FoldoutFrame &frame,int panel) {
    FoldoutPose p;
    if(!validFoldoutSample(frame,panel))return p;
    const float t=std::clamp(frame.elapsed,0.f,frame.duration);
    if(frame.visualOnly){
        const auto g=foldoutGeometry(frame,panel);p.width=g.width;p.height=g.height;
        const float start=foldoutShotStart(frame,panel)-.95f;
        const float open=foldoutEase((t-start)/.85f),closing=foldoutEase((t-(frame.duration-1.2f))/1.2f);
        const float folded=1-open+closing*open;
        const float direction=g.x<-.1f?-1.f:g.x>.1f?1.f:panel%2?1.f:-1.f;
        const float foldYaw=direction*1.24f*folded;
        p.yaw=(g.x<-.1f?-.025f:g.x>.1f?.025f:0)+foldYaw;
        p.pitch=(panel==0?.10f:.16f)*folded;p.roll=g.roll*(1-folded);
        p.x=g.x+direction*(g.width*.5f*std::cos(foldYaw)-g.width*.5f);
        p.y=g.y-g.height*.5f*(1-std::cos(p.pitch));
        p.z=g.z-std::abs(g.width*.5f*std::sin(foldYaw))-g.height*.5f*std::sin(p.pitch);
        p.opacity=foldoutEase((t-start)/.25f)*(1-closing);p.captionOpacity=0;
        p.visible=p.opacity>.001f;return p;
    }
    const std::array<float,3> intro{.1f,.85f,1.6f},inter{.45f,.05f,.9f},ending{.1f,1.1f,.6f};
    const auto &starts=frame.kind==FoldoutKind::Intermission?inter:frame.kind==FoldoutKind::Ending?ending:intro;
    const float start=starts[size_t(panel)],open=foldoutEase((t-start)/2.2f);
    const float closing=foldoutEase((t-(frame.duration-1.2f))/1.2f);
    const float folded=1.f-open+closing*open;
    const float direction=float(panel-1);
    p.yaw=direction*(.08f+1.37f*folded);
    p.pitch=panel==1?.32f*folded:0.f;
    p.x=direction*(.64f+.58f*std::cos(p.yaw));
    p.y=.25f-(panel==1?.435f*(1.f-std::cos(p.pitch)):0.f);
    p.z=(panel==1?-3.f:-3.12f)-std::abs(.58f*std::sin(p.yaw))-(panel==1?.435f*std::sin(p.pitch):0.f);
    p.opacity=foldoutEase((t-start)/.45f)*(1.f-closing);
    p.captionOpacity=foldoutCaptionOpacity(frame,panel);
    p.visible=p.opacity>.001f;
    return p;
}
} // namespace iron
