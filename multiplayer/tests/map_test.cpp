#include <hyprwalk/mp/Multiplayer.hpp>
#include <hyprwalk/mp/Protocol.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <thread>
using namespace hyprwalk::mp;
using namespace std::chrono_literals;
namespace {
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #x);}while(0)
std::string hex(const Digest& d) {std::string s;for(auto b:d){s+="0123456789abcdef"[b>>4];s+="0123456789abcdef"[b&15];}return s;}
void text(ContentHash& hash,const std::string& s) {hash.update({reinterpret_cast<const std::uint8_t*>(s.data()),s.size()});}
MapDescriptor map(bool asset=false) {MapDescriptor m;m.collision[0]=42;if(asset){m.kind=MapKind::EmbeddedGlb;m.content[0]=10;}return m;}
void hashVectors() {
    ContentHash h;CHECK(hex(h.finish())=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    text(h,"abc");CHECK(hex(h.finish())=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(h.finish()==h.finish());text(h,"d");CHECK(hex(h.finish())=="88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589");
    h={};text(h,"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    CHECK(hex(h.finish())=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    h={};for(int i=0;i<1000;++i)text(h,std::string(1000,'a'));
    CHECK(hex(h.finish())=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    ContentHash a,b;a.floating(-0.f);b.floating(0.f);CHECK(a.finish()==b.finish());
    a={};b={};a.integer(0x0102030405060708ULL);const std::uint8_t bytes[]{1,2,3,4,5,6,7,8};b.update(bytes);CHECK(a.finish()==b.finish());
}
void codec() {
    auto m=map(true);m.aspect=21.f/9; m.spawn={1,2,3};m.desktopCenter={4,5,6};m.scale=0.0254f;
    auto b=encodeMap(m);CHECK(b.size()==128);MapDescriptor out;std::string e;CHECK(decodeMap(b,out,e));
    CHECK(out.aspect==m.aspect&&out.scale==m.scale&&out.spawn.y==2&&out.desktopCenter.z==6&&out.content==m.content);
    for(std::size_t n=0;n<b.size();++n)CHECK(!decodeMap(std::span(b).first(n),out,e));
    b.push_back(0);CHECK(!decodeMap(b,out,e));
    b=encodeMap(m);b[68]=0x7f;b[69]=0x80;b[70]=0;b[71]=0;CHECK(!decodeMap(b,out,e));
    m=map();m.desktopNormal={0,0,0};CHECK(!validMap(m,e));m=map();m.collision={};CHECK(!validMap(m,e));
    m=map();m.geometryVersion=2;CHECK(!validMap(m,e));m=map();m.kind=static_cast<MapKind>(999);CHECK(!validMap(m,e));
    for(auto type:{wire::Type::MapOffer,wire::Type::MapReady,wire::Type::MapReject}){
        wire::Message msg;msg.type=type;msg.session[0]=1;msg.participant[0]=2;msg.worldTicket[0]=3;msg.map=map();msg.text="no matching map";
        wire::Decoder d;unsigned calls=0;
        for(auto byte:wire::encode(msg))CHECK(d.feed({&byte,1},[&](wire::Frame&& f){wire::Message decoded;CHECK(wire::decode(f,decoded,e));
            CHECK(decoded.type==type&&decoded.worldTicket==msg.worldTicket);++calls;return true;},e));CHECK(calls==1);
    }
}
void matching() {
    auto a=map(),b=a;std::string e;b.aspect=21.f/9;b.height=3;b.spawn={12,13,14};b.collision[0]=17;
    CHECK(compatibleMap(a,b,e)); // Rebuild confirmation checks host collision later, not monitor-local geometry here.
    a=map(true);b=a;CHECK(compatibleMap(a,b,e));b.content[1]=1;CHECK(!compatibleMap(a,b,e)&&e.find("content")!=std::string::npos);
    b=a;b.scale=2;CHECK(!compatibleMap(a,b,e)&&e.find("scale")!=std::string::npos);
    b=a;b.collision[1]=1;CHECK(!compatibleMap(a,b,e)&&e.find("collision")!=std::string::npos);
    b=a;b.spawn={100,0,100};CHECK(compatibleMap(a,b,e));CHECK(!compatibleMap(map(),b,e));
}
struct Adapter:IAdapter {
    std::string name;MapDescriptor descriptor=map();std::optional<WorldRequest> pending;unsigned players=0,requests=0;
    explicit Adapter(std::string n):name(std::move(n)){}
    LocalInfo localInfo()const override{return{name};}
    std::optional<MapDescriptor> localMap()const override{return descriptor;}
    void onState(const Status&)noexcept override{}
    void onDiagnostic(const Diagnostic&)noexcept override{}
    void onWorldRequest(const WorldRequest& r)noexcept override{pending=r;++requests;}
    void onPlayerSnapshot(const ReceivedPlayer&)noexcept override{++players;}
};
Settings settings(){Settings s;s.enableUdp=s.enableWorld=true;s.worldTimeout=800ms;s.udpHandshakeTimeout=1500ms;return s;}
void wait(const std::function<bool()>& fn,Multiplayer& h,Multiplayer& c,std::chrono::milliseconds limit=2500ms){
    const auto end=std::chrono::steady_clock::now()+limit;
    while(!fn()){
        h.dispatch();c.dispatch();
        if(std::chrono::steady_clock::now()>end){std::cerr<<formatStatus(h.status())<<'\n'<<formatStatus(c.status())<<'\n';throw std::runtime_error("wait timeout");}
        std::this_thread::sleep_for(1ms);
    }
    h.dispatch();c.dispatch();
}
struct Pair {
    Adapter a,b; Multiplayer h,c;
    Pair(std::string hn="egor",std::string cn="stepan",Settings hs=settings(),Settings cs=settings()):a(hn),b(cn),h(a,hs),c(b,cs){}
    void start(){std::string e;CHECK(h.host({"127.0.0.1",0},e));wait([&]{return h.status().state==State::Listening;},h,c);
        CHECK(c.join({"127.0.0.1",h.status().endpoint.port},e));wait([&]{return h.status().testsAcknowledged&&c.status().testsAcknowledged;},h,c);}
    void requests(){wait([&]{return a.pending&&b.pending;},h,c);}
    void confirm(bool clientFirst=true){
        requests();CHECK(a.pending->ticket==b.pending->ticket);
        if(clientFirst){CHECK(c.confirmWorld({b.pending->ticket,true,{}}));CHECK(h.confirmWorld({a.pending->ticket,true,{}}));}
        else{CHECK(h.confirmWorld({a.pending->ticket,true,{}}));CHECK(c.confirmWorld({b.pending->ticket,true,{}}));}
        wait([&]{return h.status().world==WorldState::Ready&&c.status().world==WorldState::Ready&&
            h.status().udp.state==UdpState::Ready&&c.status().udp.state==UdpState::Ready;},h,c);
    }
};
void admission(bool reverse){
    Pair p(reverse?"stepan":"egor",reverse?"egor":"stepan");p.a.descriptor.aspect=21.f/9;p.b.descriptor.aspect=4.f/3;p.start();p.requests();
    CHECK(p.b.pending->map.aspect==p.a.descriptor.aspect);
    PlayerSnapshot s;s.active=true;s.feet={1,2,3};CHECK(!p.h.submitPlayer(s)&&!p.c.submitPlayer(s));
    // UDP may already be ready, but no physical state is admitted until both MAIN threads have applied the world.
    std::this_thread::sleep_for(70ms);CHECK(p.h.status().udp.playerSent==0&&p.c.status().udp.playerSent==0);
    p.confirm(reverse);CHECK(p.h.submitPlayer(s)&&p.c.submitPlayer(s));wait([&]{return p.a.players>0&&p.b.players>0;},p.h,p.c);
    const auto ticket=p.a.pending->ticket;p.c.leave();wait([&]{return p.h.status().state==State::Listening&&p.c.finished();},p.h,p.c);
    CHECK(p.c.reap());p.a.pending.reset();p.b.pending.reset();std::string e;
    CHECK(p.c.join({"127.0.0.1",p.h.status().endpoint.port},e));p.requests();CHECK(p.a.pending->ticket!=ticket);
    CHECK(!p.h.confirmWorld({ticket,true,{}})); // Stale completion is rejected before it reaches the worker.
    std::this_thread::sleep_for(20ms);p.h.dispatch();CHECK(p.h.status().world==WorldState::Applying);
    p.confirm();p.h.shutdown();p.c.shutdown();CHECK(p.h.status().world==WorldState::Disabled);
}
void incompatible(unsigned which){Pair p;p.a.descriptor=map(true);p.b.descriptor=p.a.descriptor;
    if(which==0)p.b.descriptor.content[1]=1;
    if(which==1)p.b.descriptor.scale=2;
    if(which==2)p.b.descriptor.collision[1]=1;
    if(which==3)p.b.descriptor=map();
    p.start();wait([&]{return p.h.status().world==WorldState::Rejected&&p.c.status().world==WorldState::Rejected;},p.h,p.c);
    CHECK(p.b.requests==0&&!p.h.status().worldError.empty());CHECK(p.h.status().state==State::Connected);
    std::string e;CHECK(p.h.sendTest("world rejection does not break TCP",e));wait([&]{return p.h.status().testsAcknowledged==2;},p.h,p.c);
}
void failedApply(){Pair p;p.start();p.requests();CHECK(p.h.confirmWorld({p.a.pending->ticket,true,{}}));
    CHECK(p.c.confirmWorld({p.b.pending->ticket,false,"courtyard collision differs after rebuild"}));
    wait([&]{return p.h.status().world==WorldState::Rejected&&p.c.status().world==WorldState::Rejected;},p.h,p.c);
    CHECK(p.h.status().udp.playerSent==0&&p.c.status().udp.playerSent==0);
}
void timeout(){Pair p;p.start();p.requests();CHECK(p.h.confirmWorld({p.a.pending->ticket,true,{}}));
    wait([&]{return p.h.status().world==WorldState::Rejected&&p.c.status().world==WorldState::Rejected;},p.h,p.c);
    CHECK(!p.c.confirmWorld({p.b.pending->ticket,true,{}}));
}
void missingCapability(){auto old=settings();old.enableWorld=false;Pair p("egor","stepan",settings(),old);p.start();
    wait([&]{return p.h.status().world==WorldState::Rejected;},p.h,p.c);CHECK(p.h.status().state==State::Connected&&p.b.requests==0);
}
void invalidLocal(){Adapter a("egor");a.descriptor.collision={};Multiplayer m(a,settings());std::string e;CHECK(!m.host({"127.0.0.1",0},e));
    CHECK(m.finished()&&m.notificationFd()==-1&&e.find("local map")!=std::string::npos);
}
}
int main(){unsigned passed=0;const std::vector<std::pair<const char*,std::function<void()>>> tests{
{"SHA-256 known vectors and canonical fields",hashVectors},{"map and TCP codecs",codec},{"map compatibility",matching},
{"egor host world admission and reconnect",[]{admission(false);}},{"stepan host world admission and reconnect",[]{admission(true);}},
{"reject content mismatch",[]{incompatible(0);}},{"reject scale mismatch",[]{incompatible(1);}},{"reject collision mismatch",[]{incompatible(2);}},
{"reject map kind mismatch",[]{incompatible(3);}},{"reject failed main-thread apply",failedApply},{"main-thread apply timeout",timeout},
{"old control peer cannot enter world",missingCapability},{"invalid local descriptor opens no sockets",invalidLocal}};
for(const auto& [name,run]:tests){try{run();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';return 1;}}
std::cout<<passed<<"/"<<tests.size()<<" groups passed\n";
}
