#include "voice.hpp"
#include "audio.hpp"
#include "coop.hpp"
#include <cmath>
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace avp::voice;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main(int argc,char** argv){try{
    ActivityGate gate;std::array<float,frameSamples> silence{},speech{};
    for(int i=0;i<frameSamples;++i)speech[i]=.12f*std::sin(6.2831853f*700*i/sampleRate);
    for(int i=0;i<30;++i)check(!gate.process(silence),"silence gate");check(gate.process(speech),"speech opens gate");
    for(int i=0;i<30;++i)gate.process(silence);check(!gate.process(silence),"release closes gate");
    Controls controls;controls.update(true,false,{},{},{},{},false);check(controls.channel==Channel::Radio&&controls.captureAllowed,"all communication is radio");
    SpeechCodec codec;auto packet=codec.encode(speech);check(validOpus(packet),"Opus encode");auto decoded=codec.decode(packet);float energy=0;for(float f:decoded)energy+=f*f;check(energy>0,"Opus playback");
    auto response=[](float hz){RadioFilter filter;float energy=0;for(int i=0;i<48000;++i){float out=filter.sample(.05f*std::sin(6.2831853f*hz*i/48000));if(i>24000)energy+=out*out;}return energy;};
    check(response(700)>response(60)*10&&response(700)>response(12000)*10,"team radio bandpass");
    iron::Audio audio("/nonexistent/voice-test-assets");check(audio.available(),"shared game audio output");std::array<float,frameSamples*2> stereo{};audio.queueVoice(stereo);iron::World scene;audio.update(scene,false);
    // Wait for the backend's first callback rather than assuming a 35 ms startup.
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    while(audio.voiceBytes()>=sizeof(stereo)&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    check(audio.voiceBytes()<sizeof(stereo),"voice mixed while in lobby/menu");
    Relay relay("127.0.0.1",0);Transport a,b,c;std::array<Participant,3> people{{{1,{0,0,0},true,10},{2,{10000,0,10000},true,10},{3,{0,0,0},true,11}}};
    a.connect("127.0.0.1",relay.admit(1));b.connect("127.0.0.1",relay.admit(2));c.connect("127.0.0.1",relay.admit(3));
    auto pump=[&]{relay.poll(people,1);std::this_thread::sleep_for(std::chrono::milliseconds(2));};
    a.poll(1);b.poll(1);c.poll(1);pump();a.poll(1);b.poll(1);c.poll(1);check(a.ready()&&b.ready()&&c.ready(),"voice authentication");
    a.send(Channel::Radio,1,packet);pump();auto received=b.poll(1);check(received.size()==1&&c.poll(1).empty(),"global within lobby, isolated between lobbies");
    check(received[0].channel==Channel::Radio,"radio filter routing");
    a.send(Channel::Proximity,1,packet);pump();check(b.poll(1).empty(),"proximity disabled server side");
    // Alive/dead is deliberately absent from voice membership; spectators stay active.
    people[1].position={20000,1000,0};a.send(Channel::Radio,1,packet);pump();check(b.poll(1).size()==1,"spectator location never restricts voice");
    people[0].position={-1548.8f,0,2816};a.send(Channel::Radio,1,packet);pump();auto deployed=b.poll(1);check(deployed.size()==1&&deployed[0].position.x==people[0].position.x&&deployed[0].position.z==people[0].position.z,"deployed sender world metres survive radio transport");
    relay.remove(1);a.send(Channel::Radio,1,packet);pump();check(b.poll(1).empty(),"departed member loses voice credentials");
    Playback playback;playback.receive(received[0],0);std::array<float,frameSamples*2> output{};playback.mix(output.data(),{0,0,0},0,.08,1);
    if(argc>1){
        iron::coop::Server lobby(argv[1],"127.0.0.1",0);iron::Progression campaign;campaign.rules.load(argv[1]);
        std::string endpoint="127.0.0.1:"+std::to_string(lobby.port());iron::coop::Client host(endpoint,true,campaign);
        for(int i=0;i<200&&host.code.empty();++i){host.update(iron::coop::tick);lobby.step();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        check(!host.code.empty(),"voice test lobby created before mission selection");iron::coop::Client guest(endpoint,false,campaign,host.code);
        for(int i=0;i<200&&guest.voiceCredentials.empty();++i){host.update(iron::coop::tick);guest.update(iron::coop::tick);lobby.step();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        Transport microphone,speaker;microphone.connect(host.voiceHost,host.voiceCredentials);speaker.connect(guest.voiceHost,guest.voiceCredentials);
        auto lobbyPump=[&](){host.update(iron::coop::tick);guest.update(iron::coop::tick);microphone.poll(1);lobby.step();std::this_thread::sleep_for(std::chrono::milliseconds(2));};
        for(int i=0;i<80&&(!microphone.ready()||!speaker.ready());++i){lobbyPump();speaker.poll(1);}
        check(microphone.ready()&&speaker.ready(),"voice authenticates in undeployed lobby");microphone.send(Channel::Radio,1,packet);bool heard=false;
        for(int i=0;i<60&&!heard;++i){lobbyPump();heard=!speaker.poll(1).empty();}
        check(heard&&host.phase()==iron::coop::Phase::Selecting&&guest.phase()==iron::coop::Phase::Selecting&&host.round==0,"team radio works before any mission is selected/deployed");
    }
    std::cout<<"Voice activity, Opus, team radio and lobby isolation checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
