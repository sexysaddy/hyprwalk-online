#include <hyprwalk/mp/AsyncLocalAsset.hpp>
#include <poll.h>
#include <fcntl.h>
#include <dirent.h>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
using namespace hyprwalk::mp;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
struct Request { std::string path; int value = 0; };
struct Result { Request req; std::string error; int value = 0; };
std::atomic<int> calls{0}, started{0};
std::atomic<bool> gate{false};
std::thread::id owner;
Result load(const Request& req, const std::atomic<bool>& cancel) {
    CHECK(std::this_thread::get_id() != owner);
    ++calls; started = req.value;
    if (req.path == "exception") throw std::runtime_error("expected load failure");
    if (req.path == "nonstd") throw 7;
    if (req.path == "gate") while (!gate && !cancel) std::this_thread::sleep_for(1ms);
    if (cancel) throw 1;
    return {req, "", req.value};
}
using Loader = AsyncLocalAsset<Request, Result>;
template<class F> void wait(F f) {
    auto end = std::chrono::steady_clock::now() + 3s;
    while (!f()) { CHECK(std::chrono::steady_clock::now() < end); std::this_thread::sleep_for(1ms); }
}
auto take(Loader& l) {
    std::optional<Loader::Delivery> value;
    wait([&] { value = l.take(); return value.has_value(); });
    return std::move(*value);
}
int count(const char* path) { int n=0; DIR* d=opendir(path); CHECK(d); while(auto* e=readdir(d)) if(e->d_name[0]!='.') ++n; closedir(d); return n; }
int main() {
 owner = std::this_thread::get_id(); int passed=0;
 auto test=[&](const char* name, auto fn) { try { calls=0; started=0; gate=false; fn(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
 catch(const std::exception& e) { std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; throw; } };
 try {
 test("owner-only delivery and eventfd", []{ Loader l(load); auto gen=l.request({"ok",4}); pollfd p{l.fd(),POLLIN,0}; CHECK(poll(&p,1,3000)==1); auto r=take(l); CHECK(r.generation==gen&&r.value.value==4); CHECK(!l.take()); });
 test("superseded active result never delivered", []{ Loader l(load); l.request({"gate",1}); wait([]{return started==1;}); auto gen=l.request({"ok",2}); auto r=take(l); CHECK(r.generation==gen&&r.value.value==2); });
 test("cancel is nonblocking and discards result", []{ Loader l(load); l.request({"gate",1}); wait([]{return started==1;}); auto a=std::chrono::steady_clock::now(); l.cancel(); CHECK(std::chrono::steady_clock::now()-a<100ms); wait([&]{return !l.busy();}); CHECK(!l.take()); });
 test("latest pending only, bounded queue", []{ Loader l(load); l.request({"gate",1}); wait([]{return started==1;}); for(int i=2;i<300;++i)l.request({"gate",i}); gate=true; auto r=take(l); CHECK(r.value.value==299); CHECK(!l.take()); });
 test("returned-but-unconsumed result discarded", []{ Loader l(load); l.request({"ok",1}); pollfd p{l.fd(),POLLIN,0};CHECK(poll(&p,1,3000)==1); l.cancel(); CHECK(!l.take()); });
 test("standard exception diagnostic", []{ Loader l(load);l.request({"exception",1});CHECK(take(l).value.error=="expected load failure"); });
 test("nonstandard exception diagnostic", []{ Loader l(load);l.request({"nonstd",1});CHECK(!take(l).value.error.empty()); });
 test("stop active, join and close", []{ Loader l(load);l.request({"gate",1});wait([]{return started==1;});int fd=l.fd();l.stop();wait([&]{return l.finished();});l.shutdown();CHECK(l.fd()==-1);CHECK(fcntl(fd,F_GETFD)==-1&&errno==EBADF); });
 test("idle stop notification", []{ Loader l(load);l.stop();pollfd p{l.fd(),POLLIN,0};CHECK(poll(&p,1,3000)==1);wait([&]{return l.finished();});CHECK(!l.take()); });
 test("reject requests after stop", []{ Loader l(load);l.stop();bool caught=false;try{l.request({"ok",1});}catch(const std::logic_error&){caught=true;}CHECK(caught); });
 test("multiple loads reuse descriptor and worker", []{ Loader l(load);int fd=l.fd();for(int i=1;i<=40;++i){l.request({"ok",i});CHECK(take(l).value.value==i);CHECK(l.fd()==fd);} });
 test("60 active destruction cycles: fd/thread cleanup", []{int fds=count("/proc/self/fd"),tasks=count("/proc/self/task");for(int i=0;i<60;++i){Loader l(load);l.request({"gate",i});}wait([&]{return count("/proc/self/task")==tasks;});CHECK(count("/proc/self/fd")==fds); });
 std::cout<<passed<<"/12 AsyncLocalAsset groups passed\n";
 } catch(...) { return 1; }
}
