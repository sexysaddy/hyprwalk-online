#include <hyprwalk/mp/WindowSync.hpp>
#include <hyprwalk/mp/WindowMenu.hpp>
#include <hyprwalk/mp/Multiplayer.hpp>
#include <hyprwalk/mp/Protocol.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
using namespace hyprwalk::mp;
using namespace std::chrono_literals;
namespace {
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #x);}while(0)
Id id(unsigned v){Id a{};a[0]=static_cast<std::uint8_t>(v);return a;}
WindowMetadata win(std::uint64_t n,std::string app="app.test",std::string title="Window"){return {n,std::move(app),std::move(title),{{0,2,0},{1,0,0},{0,-1,0},2,1,800,400}};}
Status ready(){Status s;s.state=State::Connected;s.world=WorldState::Ready;s.capabilities=15;s.session=id(1);s.localId=id(2);s.peerId=id(3);s.worldTicket=id(4);return s;}
WindowSnapshot incoming(std::vector<WindowMetadata> v={win(1)},std::uint64_t seq=1){return {id(1),id(3),id(4),seq,std::move(v)};}
void codec(){auto src=incoming({win(1),win(2,"app.example","A <not markup> title")});
    auto b=encodeWindows(src);WindowSnapshot out;std::string error;CHECK(decodeWindows(b,out,error));CHECK(out.windows==src.windows&&out.owner==src.owner);
    CHECK(b[0]==0&&b[1]==1&&b[60]==2); // explicit wire version and count
    wire::Message msg;msg.type=wire::Type::Windows;msg.windows=src;auto frame=wire::encode(msg);unsigned visits=0;wire::Decoder decoder;
    for(auto byte:frame)CHECK(decoder.feed({&byte,1},[&](wire::Frame&& f){wire::Message result;CHECK(wire::decode(f,result,error));CHECK(result.windows.windows==src.windows);++visits;return true;},error));
    CHECK(visits==1);
}
void malformed(){auto b=encodeWindows(incoming());WindowSnapshot out;std::string error;
    for(std::size_t n=0;n<b.size();++n)CHECK(!decodeWindows(std::span(b).first(n),out,error));
    auto bad=b;bad.push_back(0);CHECK(!decodeWindows(bad,out,error));bad=b;bad[1]=2;CHECK(!decodeWindows(bad,out,error));
    bad=b;bad[3]=1;CHECK(!decodeWindows(bad,out,error));bad=b;bad[60]=17;CHECK(!decodeWindows(bad,out,error));
    bad=b;std::fill(bad.begin()+4,bad.begin()+20,0);CHECK(!decodeWindows(bad,out,error));
    auto s=incoming();s.windows[0].geometry.width=std::numeric_limits<float>::quiet_NaN();CHECK(!validWindowSnapshot(s,error));
    s=incoming();s.windows[0].geometry.right={1,1,0};CHECK(!validWindowSnapshot(s,error));s=incoming();s.windows[0].id=0;CHECK(!validWindowSnapshot(s,error));
    s=incoming({win(1),win(1)});CHECK(!validWindowSnapshot(s,error));s=incoming({win(2),win(1)});CHECK(!validWindowSnapshot(s,error));
    s=incoming();s.revision=0;CHECK(!validWindowSnapshot(s,error));
}
void maximum(){std::vector<WindowMetadata> v;for(unsigned n=1;n<=16;++n)v.push_back(win(n,std::string(64,'a'),std::string(120,'t')));
    auto b=encodeWindows(incoming(v));CHECK(b.size()==3965&&b.size()<=wire::MaxPayload);v.push_back(win(17));std::string error;CHECK(!validWindowSnapshot(incoming(v),error));
}
void utf8(){CHECK(validWindowText("\xd0\x9e\xd0\xba\xd0\xbd\xd0\xbe",120));CHECK(!validWindowText("bad\nname",120));CHECK(!validWindowText("\xc0\xaf",120));
    CHECK(!validWindowText("\xed\xa0\x80",120));CHECK(!validWindowText("\xe2\x80\xae",120));
    CHECK(cleanWindowText("\xd0\x9e\xd0\xba",3)=="\xd0\x9e");CHECK(applicationKey(std::string(100,'x')).size()==64);
    CHECK(applicationKey(std::string(99,'x')+"y")!=applicationKey(std::string(100,'x')));CHECK(applicationKey("").empty());
}
void offPrivacy(){WindowSync s;s.session(ready());s.sources({win(1,"private-app","PRIVATE_SECRET_TITLE")});CHECK(s.mode()==SharingMode::Off&&s.publication().windows.empty());
    const auto b=encodeWindows(s.publication());std::string bytes(b.begin(),b.end());CHECK(bytes.find("PRIVATE")==std::string::npos&&bytes.find("private-app")==std::string::npos);
    CHECK(!s.setMode(SharingMode::All));CHECK(s.publication().windows.empty());
}
void selectedOnly(){WindowSync s;s.session(ready());s.sources({win(1),win(2,"PRIVATE", "SECRET")});CHECK(s.setMode(SharingMode::Selected));CHECK(s.select(1,true));CHECK(s.publication().windows.size()==1);
    s.sources({win(1),win(2),win(3)});CHECK(s.publication().windows.size()==1);CHECK(s.select(1,false));CHECK(s.publication().windows.empty());
    CHECK(s.select(2,true));s.sources({win(1),win(3)});CHECK(s.publication().windows.empty()&&!s.selected(2));
}
void allConsent(){WindowSync s;s.session(ready());s.sources({win(1)});CHECK(!s.setMode(SharingMode::All));CHECK(s.setMode(SharingMode::All,true));
    s.sources({win(1),win(2)});CHECK(s.publication().windows.size()==2);s.revoke();CHECK(s.publication().windows.empty());
    CHECK(s.setMode(SharingMode::All,true));std::vector<WindowMetadata> v;for(unsigned i=1;i<=17;++i)v.push_back(win(i));s.sources(v);
    CHECK(s.mode()==SharingMode::Off&&s.publication().windows.empty()&&!s.error().empty());
}
void receiveLifecycle(){WindowSync s;s.session(ready());CHECK(s.receive({incoming(),false}));CHECK(s.remote().size()==1);
    auto moved=win(1);moved.geometry.origin[0]=8;moved.geometry.width=3;
    CHECK(s.receive({incoming({moved},2),false}));CHECK(s.remote()[0].metadata.geometry.origin[0]==8&&s.remote()[0].metadata.geometry.width==3);
    CHECK(!s.receive({incoming(),false}));CHECK(s.receive({incoming({},3),false}));CHECK(s.remote().empty());
    CHECK(!s.receive({incoming(),false}));CHECK(s.receive({incoming({win(2)},4),false}));CHECK(s.remote()[0].key.window==2);
}
void identity(){WindowSync s;s.session(ready());auto snap=incoming();snap.owner=id(9);CHECK(!s.receive({snap,false}));snap=incoming();snap.generation=id(8);CHECK(!s.receive({snap,false}));
    snap=incoming();snap.session=id(7);CHECK(!s.receive({snap,false}));
    CHECK(s.receive({incoming(),false}));auto newSession=ready();newSession.worldTicket=id(10);CHECK(s.session(newSession));CHECK(s.remote().empty()&&s.mode()==SharingMode::Off);
    CHECK(!s.receive({incoming(),false}));
}
void noInput(){WindowSync s;s.session(ready());s.receive({incoming(),false});auto before=encodeWindows(s.publication());s.hide(1,true);CHECK(encodeWindows(s.publication())==before);
    // Window sharing carries metadata and video only; visibility stays receiver-local.
    const auto page=windowMenuPage("online-windows:item:"+s.generationToken()+":1",s,"owner");
    CHECK(page.items.size()==5);for(auto& item:page.items)CHECK(item.label.find("Keyboard")==std::string::npos);
}
void ray(){RemoteWindow w{{id(1),id(3),id(4),1},win(1),{}};
    auto h=hitRemoteWindow(w,{1,1.5f,3},{0,0,-1});CHECK(h&&h->distance==3);
    CHECK(!hitRemoteWindow(w,{1,2.1f,3},{0,0,-1}));CHECK(!hitRemoteWindow(w,{1,1.5f,-3},{0,0,1}));
    w.receive.hidden=true;CHECK(!hitRemoteWindow(w,{1,1.5f,3},{0,0,-1}));
}
void staleMenu(){WindowSync s;s.session(ready());s.sources({win(1)});auto g=s.generationToken();CHECK(windowMenuAction(g+":selected:0",s).empty());CHECK(windowMenuAction(g+":select:1",s).empty());
    auto state=ready();state.worldTicket=id(6);s.session(state);CHECK(!windowMenuAction(g+":all:0",s).empty());CHECK(s.mode()==SharingMode::Off);
    CHECK(windowMenuPage("online-windows:all:"+g,s,"peer").items[0].label=="Session changed");
}
void menus(){WindowSync s;s.session(ready());std::vector<WindowMetadata> v;for(unsigned i=1;i<=25;++i)v.push_back(win(i));s.sources(v);
    const auto g=s.generationToken();
    const auto root=windowMenuPage("online-windows",s,"peer");CHECK(root.items.size()==5&&root.items.back().page=="online-windows:debug");
    const auto debug=windowMenuPage("online-windows:debug",s,"peer");CHECK(debug.items.size()==3&&debug.items[2].label=="Window Video");
    for(const auto& item:root.items)CHECK(item.label!="Stop Sharing"&&item.label!="Audio");
    CHECK(windowMenuPage("online-windows:local:"+g+":0",s,"peer").items.size()==7);
    CHECK(windowMenuPage("online-windows:local:"+g+":1",s,"peer").items.size()==8);
    auto all=windowMenuPage("online-windows:all:"+g,s,"peer");CHECK(all.items[0].label=="Confirm All Windows"&&all.items[1].label=="Cancel");
    OnlineMenu online;CHECK(online.page("online",ready(),"local").items.back().page=="online-windows");
    CHECK(windowMenuDetails("media",s,"peer").find("replaces the placeholder")!=std::string::npos);
}
void writeQueue(){wire::Message a;a.type=wire::Type::Windows;a.windows=incoming();auto bytes=wire::encode(a);wire::WriteQueue q;CHECK(q.push(bytes));q.discardUnsent(wire::Type::Windows);CHECK(q.empty());
    CHECK(q.push(bytes));std::string error;std::vector<std::uint8_t> output;
    q.flush([&](const std::uint8_t* p,std::size_t n)->ssize_t{const auto used=std::min<std::size_t>(3,n);output.insert(output.end(),p,p+used);return used;},error,3);
    q.discardUnsent(wire::Type::Windows);CHECK(q.bytes()==bytes.size()-3); // cannot corrupt a partial frame
    CHECK(q.push(wire::encode(a)));q.discardUnsent(wire::Type::Windows);CHECK(q.bytes()==bytes.size()-3);
    q.flush([&](const std::uint8_t* p,std::size_t n)->ssize_t{output.insert(output.end(),p,p+n);return n;},error);CHECK(output==bytes);
}
struct Adapter:IAdapter{
    std::string name;WindowSync windows;std::optional<WorldRequest> pending;unsigned players=0;
    explicit Adapter(std::string n):name(std::move(n)){}
    LocalInfo localInfo()const override{return{name};}
    std::optional<MapDescriptor> localMap()const override{MapDescriptor m;m.collision[0]=42;return m;}
    void onState(const Status& s)noexcept override{try{windows.session(s);}catch(...){}}
    void onWorldRequest(const WorldRequest& r)noexcept override{pending=r;}
    void onPlayerSnapshot(const ReceivedPlayer&)noexcept override{++players;}
    void onWindows(const ReceivedWindows& r)noexcept override{try{windows.receive(r);}catch(...){}}
    void onDiagnostic(const Diagnostic&)noexcept override{}
};
Settings options(){Settings s;s.enableUdp=s.enableWorld=s.enableWindows=true;return s;}
struct Pair{
    Adapter a,b;Multiplayer h,c;
    explicit Pair(bool reverse=false,Settings cs=options()):a(reverse?"stepan":"egor"),b(reverse?"egor":"stepan"),h(a,options()),c(b,cs){}
    void wait(const std::function<bool()>& f,std::chrono::milliseconds limit=4000ms){auto end=std::chrono::steady_clock::now()+limit;do{
        h.dispatch();c.dispatch();if(a.pending){h.confirmWorld({a.pending->ticket,true,{}});a.pending.reset();}if(b.pending){c.confirmWorld({b.pending->ticket,true,{}});b.pending.reset();}
        a.windows.session(h.status());b.windows.session(c.status());
        if(f())return;
        std::this_thread::sleep_for(1ms);
    }while(std::chrono::steady_clock::now()<end);throw std::runtime_error("wait timeout: "+formatStatus(h.status())+" / "+formatStatus(c.status()));}
    void start(){std::string error;CHECK(h.host({"127.0.0.1",0},error));wait([&]{return h.status().state==State::Listening;});CHECK(c.join({"127.0.0.1",h.status().endpoint.port},error));wait([&]{return h.status().world==WorldState::Ready&&c.status().world==WorldState::Ready&&h.status().udp.state==UdpState::Ready&&c.status().udp.state==UdpState::Ready;});}
    void submit(){CHECK(h.submitWindows(a.windows.publication()));CHECK(c.submitWindows(b.windows.publication()));}
};
void duplex(bool reverse){Pair p(reverse);p.start();p.a.windows.sources({win(1,"owner-a","A PUBLIC"),win(2,"hidden-a","A PRIVATE")});p.b.windows.sources({win(1,"owner-b","B PUBLIC")});
    p.submit();p.wait([&]{return p.h.status().windowsReceived&&p.c.status().windowsReceived;});CHECK(p.a.windows.remote().empty()&&p.b.windows.remote().empty());
    CHECK(p.a.windows.setMode(SharingMode::Selected));CHECK(p.a.windows.select(1,true));CHECK(p.b.windows.setMode(SharingMode::All,true));p.submit();
    p.wait([&]{return p.a.windows.remote().size()==1&&p.b.windows.remote().size()==1;});
    CHECK(p.a.windows.remote()[0].metadata.title=="B PUBLIC"&&p.b.windows.remote()[0].metadata.title=="A PUBLIC");
    CHECK(p.a.windows.remote()[0].key.owner!=p.b.windows.remote()[0].key.owner); // both window id 1, disjoint owners
    CHECK(p.a.windows.hide(1,true));p.submit();
    CHECK(p.a.windows.remote()[0].receive.hidden&&!p.b.windows.remote()[0].receive.hidden);
    CHECK(p.a.windows.remote()[0].receive.hidden);
    auto moved=win(1,"owner-a","RENAMED");moved.geometry.origin[0]=42;moved.geometry.width=4;p.a.windows.sources({moved,win(2)});p.submit();
    p.wait([&]{return p.b.windows.remote()[0].metadata.title=="RENAMED";});CHECK(p.b.windows.remote()[0].metadata.geometry.width==4);
    PlayerSnapshot player;player.active=true;player.feet={1,2,3};CHECK(p.h.submitPlayer(player)&&p.c.submitPlayer(player));p.wait([&]{return p.a.players&&p.b.players;});
    auto stale=p.a.windows.publication();p.a.windows.revoke();p.submit();p.wait([&]{return p.b.windows.remote().empty();});CHECK(p.a.windows.remote().size()==1);
    p.c.leave();p.wait([&]{return p.h.status().state==State::Listening&&p.c.finished();});CHECK(p.a.windows.remote().empty()&&p.b.windows.remote().empty());CHECK(p.c.reap());
    std::string e;CHECK(p.c.join({"127.0.0.1",p.h.status().endpoint.port},e));p.wait([&]{return p.h.status().world==WorldState::Ready&&p.c.status().world==WorldState::Ready;});
    CHECK(p.h.status().worldTicket!=stale.generation);CHECK(!p.h.submitWindows(stale));CHECK(p.a.windows.mode()==SharingMode::Off&&p.b.windows.mode()==SharingMode::Off);
    p.submit();CHECK(p.a.windows.remote().empty()&&p.b.windows.remote().empty());
    p.h.shutdown();p.c.shutdown();p.a.windows.session(p.h.status());p.b.windows.session(p.c.status());CHECK(p.a.windows.remote().empty()&&p.b.windows.remote().empty());
}
void oldPeer(){auto settings=options();settings.enableWindows=false;Pair p(false,settings);p.start();CHECK(p.h.status().capabilities==7&&p.c.status().capabilities==7);
    CHECK(!p.h.submitWindows(p.a.windows.publication()));std::string e;CHECK(p.h.sendTest("still control",e));p.wait([&]{return p.h.status().testsAcknowledged>=2;});
}
void scopedSubmit(){Pair p;p.start();auto a=p.a.windows.publication();a.owner=p.c.status().localId;CHECK(!p.h.submitWindows(a));a=p.a.windows.publication();a.generation=id(120);CHECK(!p.h.submitWindows(a));}
void resets(){WindowSync s;s.session(ready());for(unsigned i=1;i<2000;++i){CHECK(s.receive({incoming({win(i)},2*i),false}));CHECK(s.remote().size()==1);CHECK(s.receive({incoming({},2*i+1),false}));CHECK(s.remote().empty());}s.receive({incoming({win(9999)},5000),false});s.session({});CHECK(s.remote().empty());}
void expired(){WindowSync s;s.session(ready());s.receive({incoming(),false});CHECK(s.receive({incoming({},1),true}));CHECK(s.remote().empty());CHECK(!s.receive({incoming(),false}));CHECK(s.receive({incoming({win(2)},2),false}));}
void localLease(){Pair p;p.start();p.a.windows.sources({win(1)});CHECK(p.a.windows.setMode(SharingMode::All,true));p.submit();
    p.wait([&]{return p.b.windows.remote().size()==1;});
    // Keep TCP/UDP healthy, but never submit the publisher's source catalog again.
    p.wait([&]{return p.b.windows.remote().empty()&&p.h.status().publishedWindows==0;},4500ms);
    CHECK(p.h.status().state==State::Connected&&p.h.status().world==WorldState::Ready);
}
void lifecycle(){const auto fds=[](){return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),std::filesystem::directory_iterator{});};const auto before=fds();
    for(unsigned i=0;i<8;++i){Pair p(i%2);p.start();p.a.windows.sources({win(1)});CHECK(p.a.windows.setMode(SharingMode::All,true));p.submit();p.wait([&]{return p.b.windows.remote().size()==1;});p.h.shutdown();p.c.shutdown();}
    CHECK(fds()==before);
}
}
int main(){unsigned passed=0;std::vector<std::pair<const char*,std::function<void()>>> tests{
{"wire roundtrip and bytewise TCP",codec},{"reject malformed/version/IDs/geometry",malformed},{"max 16 windows fit one envelope",maximum},{"UTF-8 and app identity",utf8},
{"Off reveals no metadata",offPrivacy},{"Selected never publishes new sources",selectedOnly},{"All explicit confirmation and fail-closed capacity",allConsent},
{"move/resize/rename/withdraw/replay",receiveLifecycle},{"session/owner/world identity",identity},
{"receiver preferences do not publish input",noInput},{"remote panel ray geometry",ray},{"stale menu tokens rejected",staleMenu},
{"menu pagination/confirmation and retained Online",menus},{"withdraw queued but not partial TCP frames",writeQueue},
{"egor host simultaneous bidirectional metadata",[]{duplex(false);}}, {"stepan host simultaneous bidirectional metadata",[]{duplex(true);}},
{"old 3B peer retains TCP/UDP without windows",oldPeer},{"submit cannot cross owner/generation",scopedSubmit},{"2000 replacements no stale object accumulation",resets},
{"expired windows hidden and old revision rejected",expired},{"active shutdown cycles and fd cleanup",lifecycle},{"source lease withdraws without TCP disconnect",localLease}};
for(auto& [name,test]:tests)try{test();++passed;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';return 1;}
std::cout<<passed<<"/"<<tests.size()<<" window groups passed\n";
}
