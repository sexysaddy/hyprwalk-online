#include <hyprwalk/mp/Multiplayer.hpp>
#include <hyprwalk/mp/PlayerSync.hpp>
#include <hyprwalk/mp/WindowSync.hpp>
#ifdef HYPRWALK_FORK_MAP
#include "multiplayer_map.hpp"
#endif
#include <charconv>
#include <csignal>
#include <cmath>
#include <iostream>
#include <poll.h>
using namespace hyprwalk::mp;
namespace {
volatile std::sig_atomic_t interrupted=0;
void interrupt(int){interrupted=1;}
struct Adapter final:IAdapter {
    std::string name; MapDescriptor map; PlayerSync sync; std::optional<WorldRequest> request;
    unsigned received=0,sampled=0; bool ready=false;
    WindowSync windows;
    std::optional<std::chrono::steady_clock::time_point> worldStarted;
    bool sawTwo=false,sawMoved=false,sawClosed=false,preferencesChecked=false,windowFailure=false;
    void onWindows(const ReceivedWindows& packet) noexcept override {
        try {
            if (!windows.receive(packet)) {windowFailure=true;return;}
            const auto& remote=windows.remote();
            if(remote.size()==2) {
                sawTwo=true;
                if(remote.front().metadata.geometry.width==3 && remote.front().metadata.geometry.origin[0]==4) sawMoved=true;
                if(!preferencesChecked) {
                    auto id=remote.front().metadata.id;
                    const auto ownBefore=windows.publication().windows;
                    windows.hide(id,true);
                    preferencesChecked=windows.findRemote(id)->receive.hidden && !windows.remote()[1].receive.hidden && windows.publication().windows==ownBefore;
                    windows.hide(id,false);
                }
            }
            if(sawTwo && sawMoved && remote.size()==1)sawClosed=true;
            for(const auto& w:remote) if(w.metadata.title.find("PRIVATE")!=std::string::npos || w.metadata.id==3)windowFailure=true;
        } catch(...) {windowFailure=true;}
    }
    void publish(Multiplayer& core) {
        if(!windows.ready())return;
        const auto now=std::chrono::steady_clock::now();const bool first=!worldStarted;
        if(first)worldStarted=now;
        const auto age=std::chrono::duration<float>(now-*worldStarted).count();
        WindowMetadata a;a.id=1;a.application="org.example."+name;a.title=name+" window one";a.geometry.pixelWidth=800;a.geometry.pixelHeight=400;
        WindowMetadata b=a;b.id=2;b.title=name+" window two";
        WindowMetadata privateWindow=a;privateWindow.id=3;privateWindow.title="PRIVATE NEVER SENT";
        if(age>.6f){a.geometry.width=3;a.geometry.origin[0]=4;}
        std::vector<WindowMetadata> sources{a,privateWindow};if(age<1.2f)sources.push_back(b);
        windows.sources(std::move(sources));
        if(first){windows.setMode(SharingMode::Selected);windows.select(1,true);windows.select(2,true);}
        if(!core.submitWindows(windows.publication()))windowFailure=true;
    }
    explicit Adapter(std::string n):name(std::move(n)){
#ifdef HYPRWALK_FORK_MAP
        hyprwalk::SScreenSpec spec;spec.aspect=name=="egor"?21.f/9.f:4.f/3.f;
        map=hyprwalk::multiplayerMapDescriptor(hyprwalk::buildWorld(spec),1.f,spec);
#else
        map.collision[0]=42; // Synthetic map for stand-alone control/UDP tests, NOT a Hyprwalk world.
#endif
    }
    LocalInfo localInfo()const override{return{name};}
    std::optional<MapDescriptor> localMap()const override{return map;}
    void onState(const Status& s)noexcept override{
        windows.session(s);
        if(!windows.ready())worldStarted.reset();
        ready=s.world==WorldState::Ready&&s.udp.state==UdpState::Ready&&s.state==State::Connected;
        if(!ready)sync.reset();
        std::cout<<"STATUS "<<formatStatus(s)<<std::endl;
    }
    void onDiagnostic(const Diagnostic& d)noexcept override{std::cout<<"EVENT "<<d.text<<std::endl;}
    void onWorldRequest(const WorldRequest& r)noexcept override{request=r;}
    void onPlayerSnapshot(const ReceivedPlayer& p)noexcept override{if(ready&&sync.push(p))++received;}
    void apply(Multiplayer& core){
        if(!request)return;
        const auto r=*request;request.reset();bool applied=true;
#ifdef HYPRWALK_FORK_MAP
        hyprwalk::SScreenSpec spec;spec.aspect=r.map.aspect;spec.height=r.map.height;spec.centerY=r.map.centerY;
        const auto world=hyprwalk::buildWorld(spec);
        applied=hyprwalk::multiplayerCourtyardCollision(world)==r.map.collision;
#endif
        if(applied)map=r.map;
        core.confirmWorld({r.ticket,applied,applied?"":"actual fork courtyard collision differs"});
    }
};
}
int main(int argc,char** argv){
    if(argc<5||argc>6||(std::string(argv[1])!="host"&&std::string(argv[1])!="join")){
        std::cerr<<"usage: window_cli host|join <numeric-ip> <port> <name> [--once]\n";return 2;}
    unsigned port=0;const std::string text=argv[3];auto parsed=std::from_chars(text.data(),text.data()+text.size(),port);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||port>65535)return 2;
    const bool once=argc==6&&std::string(argv[5])=="--once";if(argc==6&&!once)return 2;
    std::signal(SIGINT,interrupt);std::signal(SIGTERM,interrupt);
    Adapter adapter(argv[4]);Settings settings;settings.enableUdp=settings.enableWorld=settings.enableWindows=true;
    Multiplayer core(adapter,settings);std::string error;Endpoint endpoint{argv[2],static_cast<std::uint16_t>(port)};
    if(!(std::string(argv[1])=="host"?core.host(endpoint,error):core.join(endpoint,error))){std::cerr<<error<<'\n';return 1;}
    using Clock=std::chrono::steady_clock;const auto start=Clock::now();
    bool sentDone=false,success=false,leaving=false,failed=false;
    while(!core.finished()){
        core.dispatch();adapter.apply(core);adapter.publish(core);
        const auto now=Clock::now();const float t=std::chrono::duration<float>(now-start).count();
        PlayerSnapshot p;p.active=true;p.feet={std::sin(t),std::max(0.f,std::sin(t*2)),3.f-std::cos(t)};
        p.velocity={std::cos(t),p.feet.y>0?2.f*std::cos(t*2):0.f,std::sin(t)};
        p.yaw=t;p.bodyYaw=t-0.2f;p.pitch=0.3f*std::sin(t);p.onGround=p.feet.y==0;
        p.running=t>0.4f;p.crouched=t>0.7f;p.flying=t>1.f;core.submitPlayer(p);
        if(const auto sample=adapter.sync.sample(now)){if(!validPlayerSnapshot(sample->player)){failed=true;break;}++adapter.sampled;}
        if(adapter.windowFailure){failed=true;break;}
        if(!sentDone&&adapter.received>=35&&adapter.sampled>=20&&adapter.sawTwo&&adapter.sawMoved&&adapter.sawClosed&&adapter.preferencesChecked){if(core.sendTest("window-stream-complete",error))sentDone=true;}
        const auto s=core.status();
        if(!success&&sentDone&&s.testsAcknowledged>=2&&s.lastMessage=="window-stream-complete"){
            success=true;std::cout<<"WINDOWS_OK "<<formatStatus(s)<<std::endl;
            std::cout<<"SAMPLES received="<<adapter.received<<" rendered="<<adapter.sampled<<std::endl;
        }
        if(!leaving&&(interrupted||(once&&success))){core.leave();leaving=true;}
        if(once&&t>15){failed=true;break;}
        pollfd fd{core.notificationFd(),POLLIN,0};::poll(&fd,1,4);
    }
    core.dispatch();const auto final=core.status();
    std::cout<<"FINAL "<<formatStatus(final)<<std::endl;
    core.shutdown();return failed||final.state==State::Error||(once&&!success)?1:0;
}
