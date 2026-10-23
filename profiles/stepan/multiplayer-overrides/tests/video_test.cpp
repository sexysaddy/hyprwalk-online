#include <hyprwalk/mp/VideoSignaling.hpp>
#include <hyprwalk/mp/WindowMenu.hpp>
#include <hyprwalk/mp/Multiplayer.hpp>
#include <hyprwalk/mp/Protocol.hpp>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <thread>
using namespace hyprwalk::mp;
using namespace std::chrono_literals;
namespace {
#define CHECK(x) do { if(!(x))throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #x); } while(0)
Id id(unsigned n){Id v{};v[0]=static_cast<std::uint8_t>(n);return v;}
WindowMetadata win(unsigned n=1){return {n,"app.test","Only permitted",{{0,2,0},{1,0,0},{0,-1,0},2,1,800,400}};}
Status state(){Status s;s.state=State::Connected;s.world=WorldState::Ready;s.capabilities=31;s.session=id(1);s.localId=id(2);s.peerId=id(3);s.worldTicket=id(4);return s;}
VideoSignal offer(std::size_t bytes=16){VideoSignal v;v.key={{id(1),id(2),id(4),1},1,1};v.sender=id(2);v.serial=1;v.body=std::string(bytes,'x');return v;}
void codec(){auto v=offer(12000);v.body.replace(0,5,"v=0\r\n");VideoAssembler a;std::string e;std::optional<VideoSignal> out;
    auto chunks=fragmentVideoSignal(v);CHECK(chunks.size()==4);for(auto& b:chunks){CHECK(b.size()<=wire::MaxPayload);out=a.accept(b,VideoAssembler::Clock::now(),e);CHECK(e.empty());}
    CHECK(out&&out->body==v.body&&out->key==v.key&&a.buffered()==0);
}
void maximum(){auto v=offer(MaxVideoSignal);auto chunks=fragmentVideoSignal(v);CHECK(chunks.size()==16);VideoAssembler a;std::string e;std::optional<VideoSignal> out;
    for(auto& b:chunks){out=a.accept(b,VideoAssembler::Clock::now(),e);CHECK(e.empty()&&a.buffered()<=MaxVideoSignal);}CHECK(out&&out->body==v.body);
    v.body+='x';CHECK(!validVideoSignal(v,e));
}
void fragmentedTcp(){auto v=offer(10000);wire::Decoder tcp;VideoAssembler assembler;std::string error;unsigned done=0;
    for(auto& chunk:fragmentVideoSignal(v)){wire::Message m;m.type=wire::Type::Video;m.videoChunk=chunk;auto encoded=wire::encode(m);
        for(auto byte:encoded)CHECK(tcp.feed({&byte,1},[&](wire::Frame&& f){wire::Message out;CHECK(wire::decode(f,out,error));CHECK(out.type==wire::Type::Video);auto result=assembler.accept(out.videoChunk,VideoAssembler::Clock::now(),error);CHECK(error.empty());if(result)++done;return true;},error));}
    CHECK(done==1);
}
void interleavedControl(){auto v=offer(5000);auto chunks=fragmentVideoSignal(v);VideoAssembler a;std::string e;
    CHECK(!a.accept(chunks[0],VideoAssembler::Clock::now(),e)&&e.empty());
    wire::Message ping;ping.type=wire::Type::Ping;ping.sequence=2;CHECK(!wire::encode(ping).empty());
    CHECK(a.accept(chunks[1],VideoAssembler::Clock::now(),e)&&e.empty());
}
void malformed(){auto bytes=fragmentVideoSignal(offer())[0];VideoChunk out;std::string e;
    for(std::size_t n=0;n<bytes.size();++n)CHECK(!decodeVideoChunk(std::span(bytes).first(n),out,e));
    auto b=bytes;b[1]=2;CHECK(!decodeVideoChunk(b,out,e));b=bytes;b[3]=1;CHECK(!decodeVideoChunk(b,out,e));b=bytes;b.push_back(0);CHECK(!decodeVideoChunk(b,out,e));
    b=bytes;std::fill(b.begin()+4,b.begin()+20,0);CHECK(!decodeVideoChunk(b,out,e));b=bytes;b[2]=99;CHECK(!decodeVideoChunk(b,out,e));
    b=bytes;b[104]=0x7f;CHECK(!decodeVideoChunk(b,out,e));
}
void ordering(){auto chunks=fragmentVideoSignal(offer(9000));std::string e;VideoAssembler a;CHECK(!a.accept(chunks[1],{},e)&&!e.empty());a.reset();
    CHECK(!a.accept(chunks[0],{},e)&&e.empty());CHECK(!a.accept(chunks[2],{},e)&&!e.empty());a.reset();
    CHECK(!a.accept(chunks[0],{},e)&&e.empty());CHECK(!a.accept(chunks[0],{},e)&&!e.empty());
}
void replay(){VideoAssembler a;std::string e;auto b=fragmentVideoSignal(offer())[0];CHECK(a.accept(b,{},e));CHECK(!a.accept(b,{},e)&&!e.empty());a.reset();CHECK(a.accept(b,{},e));}
void timeout(){VideoAssembler a;std::string e;auto chunks=fragmentVideoSignal(offer(10000));const auto t=VideoAssembler::Clock::now();CHECK(!a.accept(chunks[0],t,e)&&e.empty());CHECK(!a.expire(t+4s));CHECK(a.expire(t+5s));CHECK(a.buffered()==0);CHECK(!a.accept(chunks[1],t+6s,e)&&!e.empty());}
void mixing(){auto chunks=fragmentVideoSignal(offer(9000));VideoAssembler a;std::string e;CHECK(!a.accept(chunks[0],{},e));auto v=offer(9000);v.key.publication=2;auto other=fragmentVideoSignal(v);CHECK(!a.accept(other[1],{},e)&&!e.empty());CHECK(a.buffered()==0);}
void identities(){auto v=offer();std::string e;CHECK(validVideoSignal(v,e));v.sender=id(3);CHECK(!validVideoSignal(v,e));v.kind=VideoSignalKind::Answer;CHECK(validVideoSignal(v,e));v.key.publication=0;CHECK(!validVideoSignal(v,e));v=offer();v.key.stream=0;CHECK(!validVideoSignal(v,e));}
void textLimits(){auto v=offer();std::string e;v.body="a\nb\tc\r";CHECK(validVideoSignal(v,e));v.kind=VideoSignalKind::Ice;CHECK(!validVideoSignal(v,e));v.body="0|candidate:1 1 UDP 1 127.0.0.1 10 typ host";CHECK(validVideoSignal(v,e));v.body=std::string(2049,'a');CHECK(!validVideoSignal(v,e));v=offer();v.body="bad\x1b";CHECK(!validVideoSignal(v,e));v.body=std::string("nul\0",4);CHECK(!validVideoSignal(v,e));}
void scopes(){auto v=offer();auto s=state();CHECK(videoScope(v,s,false));CHECK(!videoScope(v,s,true));v.sender=s.peerId;CHECK(videoScope(v,s,true));v.key.window.owner=id(99);CHECK(!videoScope(v,s,true));v=offer();s.worldTicket=id(5);CHECK(!videoScope(v,s,false));s=state();s.capabilities=15;CHECK(!videoScope(v,s,false));s=state();s.world=WorldState::WaitingPeer;CHECK(!videoScope(v,s,false));}
void frameValidation(){VideoFrame f;f.key=offer().key;f.width=64;f.height=32;f.rgba.resize(64*32*4);CHECK(validVideoFrame(f));f.rgba.pop_back();CHECK(!validVideoFrame(f));f.rgba.push_back(0);f.width=63;CHECK(!validVideoFrame(f));f.width=4000000;CHECK(!validVideoFrame(f));f.width=1201;f.height=501;f.rgba.resize(1201*501*4);CHECK(validVideoFrame(f));f.key.publication=0;CHECK(!validVideoFrame(f));}
void sizing(){
    VideoSettings s;CHECK(validVideoSettings(s));
    CHECK(s.width==8192&&s.height==8192&&s.fps==0&&s.bitrate==2500000);
    CHECK((fitVideoSize(1200,500,s)==std::pair{1200U,500U}));
    CHECK((fitVideoSize(1201,501,s)==std::pair{1201U,501U}));
    CHECK((fitVideoSize(3840,2160,s)==std::pair{3840U,2160U}));
    CHECK((fitVideoSize(16384,8192,s)==std::pair{8192U,4096U}));
    CHECK((fitVideoSize(200,800,s)==std::pair{200U,800U}));
    CHECK((fitVideoSize(17,9,s)==std::pair{17U,9U}));
    CHECK((fitVideoSize(0,80,s)==std::pair{0U,0U}));
    CHECK(targetVideoBitrate(s,1920,1080)==2500000U);
    CHECK(targetVideoBitrate(s,3840,2160)==2500000U);
    CHECK(targetVideoBitrate(s,1200,500)>=1340000U&&targetVideoBitrate(s,1200,500)<=1350000U);
    CHECK(targetVideoBitrate(s,640,360)>=830000U&&targetVideoBitrate(s,640,360)<=840000U);
    CHECK(targetVideoBitrate(s,640,360)<targetVideoBitrate(s,1200,500));
    s.bitrate=200000U;CHECK(targetVideoBitrate(s,1920,1080)==200000U);
    s.bitrate=2500000U;
    CHECK(s.streams==MaxVideoStreams);s.streams=3;CHECK(validVideoSettings(s));
    s.streams=static_cast<unsigned>(MaxVideoStreams+1);CHECK(!validVideoSettings(s));
    s={};s.fps=0;CHECK(validVideoSettings(s));s.fps=30;CHECK(validVideoSettings(s));s.fps=31;CHECK(!validVideoSettings(s));s={};s.bitrate=1;CHECK(!validVideoSettings(s));
}
void incarnations(){WindowSync s;s.session(state());s.sources({win(1),win(2)});CHECK(s.publicationEpoch(1)==0);CHECK(s.setMode(SharingMode::Selected));CHECK(s.select(1,true));const auto a=s.publicationEpoch(1);CHECK(a&&!s.publicationEpoch(2));
    auto moved=win(1);moved.geometry.width=3;s.sources({moved,win(2)});CHECK(s.publicationEpoch(1)==a);
    CHECK(s.select(1,false));CHECK(!s.publicationEpoch(1));CHECK(s.select(1,true));CHECK(s.publicationEpoch(1)>a);
    const auto b=s.publicationEpoch(1);s.revoke();CHECK(s.setMode(SharingMode::Selected));CHECK(s.select(1,true));CHECK(s.publicationEpoch(1)>b);
}
void recreate(){WindowSync s;s.session(state());s.sources({win()});CHECK(s.setMode(SharingMode::All,true));const auto a=s.publicationEpoch(1);s.sources({});CHECK(!s.publicationEpoch(1));s.sources({win()});CHECK(s.publicationEpoch(1)>a);auto next=state();next.worldTicket=id(9);s.session(next);CHECK(!s.publicationEpoch(1)&&s.mode()==SharingMode::Off);}
void toggles(){WindowSync s;s.session(state());s.sources({win()});s.setMode(SharingMode::All,true);const auto a=s.publicationEpoch(1);const auto before=encodeWindows(s.publication());s.videoEnable(false);CHECK(s.publicationEpoch(1)>a);const auto b=s.publicationEpoch(1);CHECK(encodeWindows(s.publication())==before&&s.localVideo(1)=="Video disabled");s.videoEnable(true);CHECK(s.publicationEpoch(1)>b);CHECK(encodeWindows(s.publication())==before);}
void menuStates(){WindowSync s;s.session(state());s.sources({win()});s.setMode(SharingMode::All,true);s.videoBackend(true,"VP8 ready");s.videoStatus(1,true,"Connecting");CHECK(s.localVideo(1)=="Connecting");
    auto menu=windowMenuPage("online-windows:video",s,"peer");CHECK(menu.items.size()==3);CHECK(windowMenuAction(menu.items[0].target,s).empty());CHECK(!s.videoEnabled());CHECK(windowMenuDetails("media",s,"peer").find("Opus")==std::string::npos);
    const auto root=windowMenuPage("online-windows",s,"peer");
    CHECK(root.items.size()==5&&root.items.back().page=="online-windows:debug");
    const auto debug=windowMenuPage("online-windows:debug",s,"peer");
    CHECK(debug.items.size()==3&&debug.items[2].label=="Window Video"&&debug.items[2].page=="online-windows:video");
    for(const auto& item:root.items)CHECK(item.label!="Stop Sharing"&&item.label!="Audio");
    s.videoEnable(true);CHECK(windowMenuPage("online-windows:all:"+s.generationToken(),s,"peer").items[0].hint.find("Live video")!=std::string::npos);
    CHECK(windowMenuDetails("privacy",s,"peer").find("live image")!=std::string::npos);OnlineMenu old;CHECK(old.page("online",state(),"local").items.size()==9);
}
void receiverPreferences(){WindowSync s;s.session(state());s.receive({{id(1),id(3),id(4),1,{win(1),win(2)}},false});s.hide(1,true);s.videoStatus(1,false,"Streaming");CHECK(s.findRemote(1)->receive.hidden&&!s.findRemote(2)->receive.hidden);s.hide(1,false);CHECK(!s.findRemote(1)->receive.hidden);s.videoEnable(false);CHECK(s.findRemote(1)->video=="Streaming");}
struct Adapter:IAdapter{
    std::string name;WindowSync windows;std::optional<WorldRequest> pending;std::vector<VideoSignal> signals;unsigned players=0;
    explicit Adapter(std::string n):name(std::move(n)){}
    LocalInfo localInfo()const override{return{name};}
    std::optional<MapDescriptor> localMap()const override{MapDescriptor m;m.collision[0]=42;return m;}
    void onState(const Status& s)noexcept override{try{windows.session(s);}catch(...){}}
    void onWorldRequest(const WorldRequest& r)noexcept override{pending=r;}
    void onPlayerSnapshot(const ReceivedPlayer&)noexcept override{++players;}
    void onWindows(const ReceivedWindows& r)noexcept override{try{windows.receive(r);}catch(...){}}
    void onVideoSignal(const VideoSignal& v)noexcept override{try{signals.push_back(v);}catch(...){}}
    void onDiagnostic(const Diagnostic&)noexcept override{}
};
Settings options(bool video=true,bool windows=true){Settings s;s.enableUdp=s.enableWorld=true;s.enableWindows=windows;s.enableVideo=video;return s;}
struct Pair {
    Adapter a,b;Multiplayer h,c;
    explicit Pair(bool reverse=false,Settings cs=options()):a(reverse?"stepan":"egor"),b(reverse?"egor":"stepan"),h(a,options()),c(b,cs){}
    void wait(const std::function<bool()>& f,std::chrono::milliseconds limit=4000ms){auto end=std::chrono::steady_clock::now()+limit;do{h.dispatch();c.dispatch();if(a.pending){h.confirmWorld({a.pending->ticket,true,{}});a.pending.reset();}if(b.pending){c.confirmWorld({b.pending->ticket,true,{}});b.pending.reset();}a.windows.session(h.status());b.windows.session(c.status());if(f())return;std::this_thread::sleep_for(1ms);}while(std::chrono::steady_clock::now()<end);throw std::runtime_error("timeout: "+formatStatus(h.status())+" / "+formatStatus(c.status()));}
    void start(){std::string e;CHECK(h.host({"127.0.0.1",0},e));wait([&]{return h.status().state==State::Listening;});CHECK(c.join({"127.0.0.1",h.status().endpoint.port},e));wait([&]{return h.status().world==WorldState::Ready&&c.status().world==WorldState::Ready&&h.status().udp.state==UdpState::Ready&&c.status().udp.state==UdpState::Ready;});}
    VideoSignal message(bool host=true,std::size_t bytes=10000){const auto s=host?h.status():c.status();auto v=offer(bytes);v.key.window={s.session,s.localId,s.worldTicket,1};return v;}
};
void duplex(bool reverse){Pair p(reverse);p.start();CHECK(p.h.status().capabilities==31&&p.c.status().capabilities==31);std::string e;auto a=p.message(),b=p.message(false,MaxVideoSignal);CHECK(p.h.sendVideoSignal(a,e));CHECK(p.c.sendVideoSignal(b,e));p.wait([&]{return p.a.signals.size()==1&&p.b.signals.size()==1;});CHECK(p.a.signals[0].body==b.body&&p.b.signals[0].body==a.body);CHECK(p.a.signals[0].key.window.owner!=p.b.signals[0].key.window.owner);
    auto answer=p.b.signals[0];answer.kind=VideoSignalKind::Answer;answer.body="v=0\r\n";CHECK(p.c.sendVideoSignal(answer,e));p.wait([&]{return p.a.signals.size()==2;});CHECK(p.a.signals.back().kind==VideoSignalKind::Answer);
    CHECK(p.h.sendTest("TCP retained with SDP chunks",e));p.wait([&]{return p.h.status().testsAcknowledged>=2;});CHECK(p.h.status().udp.state==UdpState::Ready&&p.c.status().udp.state==UdpState::Ready);
}
void downgrade(bool windows){Pair p(false,options(false,windows));p.start();CHECK(p.h.status().capabilities==(windows?15U:7U));std::string e;CHECK(!p.h.sendVideoSignal(p.message(),e));CHECK(p.h.sendTest("still working",e));p.wait([&]{return p.h.status().testsAcknowledged>=2;});}
void stale(){Pair p;p.start();auto v=p.message();std::string e;auto bad=v;bad.key.window.generation=id(90);CHECK(!p.h.sendVideoSignal(bad,e));bad=v;bad.key.window.owner=id(91);CHECK(!p.h.sendVideoSignal(bad,e));p.c.leave();p.wait([&]{return p.c.finished()&&p.h.status().state==State::Listening;});CHECK(p.c.reap());CHECK(p.c.join({"127.0.0.1",p.h.status().endpoint.port},e));p.wait([&]{return p.h.status().world==WorldState::Ready&&p.c.status().world==WorldState::Ready;});CHECK(!p.h.sendVideoSignal(v,e));CHECK(p.h.sendVideoSignal(p.message(),e));p.wait([&]{return p.b.signals.size()==1;});CHECK(p.b.signals[0].key.window.generation!=v.key.window.generation);}
void queueBounds(){Pair p;p.start();std::string e;unsigned rejected=0;for(unsigned n=0;n<100;++n)if(!p.h.sendVideoSignal(p.message(true,MaxVideoSignal),e))++rejected;CHECK(rejected>0);CHECK(p.h.status().state==State::Connected);}
void lifecycle(){const auto count=[](){return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),std::filesystem::directory_iterator{});};auto before=count();for(unsigned i=0;i<10;++i){Pair p(i%2);p.start();std::string e;p.h.sendVideoSignal(p.message(),e);p.h.shutdown();p.c.shutdown();}CHECK(count()==before);}
}
int main(){unsigned passed=0;std::vector<std::pair<const char*,std::function<void()>>> tests{
 {"SDP-sized chunk roundtrip",codec},{"48 KiB / 16-chunk bound",maximum},{"bytewise TCP framing",fragmentedTcp},{"control interleaved between fragments",interleavedControl},
 {"malformed lengths/version/reserved/IDs",malformed},{"ordered fragments only",ordering},{"replay and reset",replay},{"5-second reassembly timeout",timeout},{"mixed incarnation fragments rejected",mixing},
 {"publisher/receiver identity rules",identities},{"ASCII/SDP/ICE text limits",textLimits},{"session/owner/world/capability scope",scopes},{"frame pixel bounds",frameValidation},{"capture size and resource limits",sizing},
 {"Selected revoke/regrant distinct incarnation",incarnations},{"close/recreate/reconnect",recreate},{"video-only toggle preserves metadata",toggles},{"video menu and old indices",menuStates},{"hide remains receiver-local",receiverPreferences},
 {"Egor host simultaneous signaling",[]{duplex(false);}},{"Stepan host simultaneous signaling",[]{duplex(true);}},{"Stage3C capability downgrade",[]{downgrade(true);}},{"Stage3B capability downgrade",[]{downgrade(false);}},
 {"old signaling rejected after reconnect",stale},{"outgoing queue backpressure",queueBounds},{"active shutdown fd cleanup",lifecycle}};
 for(const auto& [name,test]:tests)try{test();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';return 1;}
 std::cout<<passed<<"/"<<tests.size()<<" video foundation groups passed\n";
}
