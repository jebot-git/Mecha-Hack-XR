#include "rpg/network.hpp"
#include "net/voice.hpp"
#include "net/radio_damage.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>
using namespace iron;
using namespace iron::rpg;
static double energy(std::span<const float> pcm){double sum=0;for(float f:pcm){assert(std::isfinite(f)&&std::abs(f)<=.25f);sum+=f*f;}return sum/pcm.size();}
int main(){
    std::vector<float> clean(48000*2);
    for(size_t i=0;i<clean.size()/2;++i){float t=float(i)/48000;clean[i*2]=clean[i*2+1]=.09f*std::sin(2*pi*330*t)+.025f*std::sin(2*pi*1450*t);}
    avp::voice::RadioDamage healthy,damaged,split;
    auto bypass=clean;healthy.process(bypass,2,false);assert(bypass==clean);
    auto whole=clean,blocks=clean;damaged.process(whole,2,true);
    for(size_t i=0;i<blocks.size();i+=1920)split.process(std::span<float>(blocks).subspan(i,std::min(size_t(1920),blocks.size()-i)),2,true);
    assert(whole==blocks&&damaged.processedFrames()==48000&&damaged.staticFrames()>12000);
    assert(energy(whole)>.00001&&whole!=clean);
    // Static-only windows carry almost no correlation with the source speech.
    double xy=0,xx=0,yy=0;
    for(size_t i=0;i<12000;++i){xy+=clean[i*2]*whole[i*2];xx+=clean[i*2]*clean[i*2];yy+=whole[i*2]*whole[i*2];}
    assert(std::abs(xy/std::sqrt(xx*yy))<.08);
    std::array<float,1920> silence{};avp::voice::RadioDamage quiet;quiet.process(silence,2,true);
    assert(energy(silence)==0);damaged.process(bypass,2,false);assert(bypass==clean&&damaged.processedFrames()==0);
    // Real RPG welcome credentials, authenticated UDP registration and Opus
    // payload relay. No microphone or audio device is opened by this test.
    Server server("127.0.0.1",0,723);
    Client a("127.0.0.1:"+std::to_string(server.port()),"Radio A"),b("127.0.0.1:"+std::to_string(server.port()),"Radio B");
    auto pump=[&](){a.update(.0167f);b.update(.0167f);server.step();std::this_thread::sleep_for(std::chrono::milliseconds(1));};
    for(int n=0;n<300&&(!a.id||!b.id||b.session.members.size()!=2);++n)pump();
    assert(a.id&&b.id&&a.id!=b.id&&server.voicePort()!=0);
    assert(!a.voiceCredentials.empty()&&!b.voiceCredentials.empty());
    assert(a.participants().size()==2&&b.participants().size()==2);
    avp::voice::Transport tx,rx;tx.connect(a.voiceHost,a.voiceCredentials);rx.connect(b.voiceHost,b.voiceCredentials);
    uint64_t epoch=server.session.epoch;
    for(int n=0;n<300&&(!tx.ready()||!rx.ready());++n){tx.poll(epoch);rx.poll(epoch);pump();}
    assert(tx.ready()&&rx.ready());
    avp::voice::SpeechCodec encoder,decoder;std::array<float,avp::voice::frameSamples> frame;
    for(size_t i=0;i<frame.size();++i)frame[i]=clean[i*2];
    avp::voice::RadioDamage fault;fault.process(frame,1,true);
    auto encoded=encoder.encode(frame);assert(tx.send(avp::voice::Channel::Radio,epoch,encoded));
    bool received=false;
    for(int n=0;n<300&&!received;++n){
        pump();tx.poll(epoch);
        for(auto &packet:rx.poll(epoch))if(packet.sender==a.id){
            assert(packet.channel==avp::voice::Channel::Radio&&packet.epoch==epoch);
            auto decoded=decoder.decode(packet.opus);double power=0;for(float v:decoded){assert(std::isfinite(v));power+=v*v;}
            assert(power>.0001);received=true;
        }
    }
    assert(received);
    std::cout<<"Healthy bypass, sample-clock fault/static replacement and authenticated RPG Opus team radio passed\n";
}
