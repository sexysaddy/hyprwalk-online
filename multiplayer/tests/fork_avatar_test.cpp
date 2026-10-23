#include "remote_avatar.hpp"
#include <dirent.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>
using namespace hyprwalk;
using namespace std::chrono_literals;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
template<class F> void wait(F f) {
 auto end=std::chrono::steady_clock::now()+10s;
 while(!f()) { CHECK(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(1ms); }
}
void loaded(CRemoteAvatar& r) { wait([&]{r.take();return r.model()||!r.error().empty();}); if(!r.error().empty())throw std::runtime_error(r.error());CHECK(r.model()); }
int count(const char* path) { int n=0;DIR* d=opendir(path);CHECK(d);while(auto* e=readdir(d))if(e->d_name[0]!='.')++n;closedir(d);return n; }
bool different(const std::vector<float>& a,const std::vector<float>& b) { if(a.size()!=b.size())return true;for(size_t i=0;i<a.size();++i)if(std::abs(a[i]-b[i])>1e-5f)return true;return false; }
std::atomic<int> loadCalls{0};
SAvatarResult counted(const SAvatarRequest& q,const std::atomic<bool>& c) { ++loadCalls; return loadAvatar(q,c); }
std::atomic<bool> began{false};
SAvatarResult blocked(const SAvatarRequest& q,const std::atomic<bool>& c) { began=true;while(!c)std::this_thread::sleep_for(1ms);return {q,{},"cancelled",{}}; }
int main(int argc,char** argv) {
 if(argc!=2) return 2;
 const std::string base=argv[1];SAvatarRequest req;req.path=base+"/humanoid.glb";
 std::atomic<bool> cancel=false;auto original=loadAvatar(req,cancel);
 if(!original.model){std::cerr<<original.error<<'\n';return 1;}CHECK(original.model->humanoid);
 auto local=original.model;CAvatarAnimator localAnim;localAnim.reset(local);localAnim.setShape(0,.8f);
 SAvatarMotion idle;idle.dt=1.f/60;localAnim.update(idle);
 const auto localJoints=localAnim.joints(),localMorph=localAnim.morphWeights();
 local->releaseCpuData();CHECK(local->vertices.empty()&&local->indices.empty());
 int passed=0;
 auto test=[&](const char* label,auto fn){fn();++passed;std::cout<<"PASS "<<label<<'\n';};
 try {
 CRemoteAvatar remote(counted);remote.select(req,1);loaded(remote);
 test("independent real local load after local CPU release",[&]{CHECK(remote.model()!=local);CHECK(!remote.model()->vertices.empty());CHECK(!remote.model()->images.empty());CHECK(!remote.model()->images[0].rgba.empty());CHECK(remote.model()->path==local->path);});
 auto now=CRemoteAvatar::Clock::now();mp::PlayerSnapshot p;p.active=true;p.onGround=true;p.feet={10,2,30};p.yaw=.8f;p.bodyYaw=.3f;p.pitch=.4f;
 auto step=[&](int n=1){for(int i=0;i<n;++i){now+=16667us;remote.pose(&p,now,true);}};
 test("physical feet and owner body yaw, not a camera",[&]{step();CHECK(std::abs(remote.transform().m[12]-10)<1e-5f);CHECK(std::abs(remote.transform().m[14]-30)<1e-5f);CHECK(std::abs(remote.transform().m[13]-remote.animator().lift()-2)<1e-4f);CHECK(std::abs(remote.motion().lookYaw-.5f)<1e-5f);CHECK(!remote.motion().fp.on);});
 test("remote/local joints, morphs and animator independent",[&]{p.velocity={0,0,-2};step(25);CHECK(localAnim.joints()==localJoints&&localAnim.morphWeights()==localMorph);CHECK(different(remote.animator().joints(),localJoints));CHECK(different(remote.animator().morphWeights(),localMorph));CHECK(remote.animator().joints().data()!=localAnim.joints().data());});
 test("walking advances actual procedural pose",[&]{auto a=remote.animator().joints();step(15);CHECK(different(a,remote.animator().joints()));CHECK(remote.animator().playing().find("procedural")!=std::string::npos);});
 test("run velocity and flag",[&]{p.velocity={0,0,-5};p.running=true;step(25);CHECK(remote.motion().run&&std::abs(remote.motion().speed-5)<1e-6f);CHECK(remote.animator().playing().find("run")!=std::string::npos);});
 test("crouch uses existing humanoid animation",[&]{p.velocity={};p.running=false;step(35);auto a=remote.animator().joints();p.crouched=true;step(35);CHECK(remote.motion().crouched&&different(a,remote.animator().joints()));});
 test("jump, fall and flight are independent remote states",[&]{p.crouched=false;p.onGround=false;p.velocity.y=4;step(4);CHECK(remote.motion().vy==4&&!remote.motion().onGround);auto a=remote.animator().joints();p.velocity.y=-3;step(30);CHECK(different(a,remote.animator().joints()));p.flying=true;p.velocity={1,2,3};step(30);CHECK(remote.motion().flying&&!remote.motion().fp.on);});
 test("all pose matrices finite",[&]{for(float x:remote.animator().joints())CHECK(std::isfinite(x));for(float x:remote.transform().m)CHECK(std::isfinite(x));});
 test("stale hide and teleport reset",[&]{remote.pose(nullptr,now,true);p.teleport++;p.feet={-20,5,6};step();CHECK(std::abs(remote.transform().m[12]+20)<1e-5f);CHECK(std::abs(remote.transform().m[13]-remote.animator().lift()-5)<1e-4f);CHECK(remote.motion().dt<=1.f/60+.001f);});
 test("angles wrap and timestep clamps",[&]{p.bodyYaw=3.13f;p.yaw=-3.13f;now+=10s;remote.pose(&p,now,false);CHECK(std::abs(remote.motion().lookYaw)<.03f);CHECK(remote.motion().dt<=.05f);CHECK(!remote.animator().physics());});
 test("same selection does not reload per frame",[&]{auto model=remote.model();int calls=loadCalls;for(int i=0;i<100;++i)remote.select(req,1);CHECK(remote.model()==model&&loadCalls==calls);});
 test("fresh reload restores previously released CPU data",[&]{auto old=remote.model();old->releaseCpuData();remote.select(req,2);CHECK(!remote.model());loaded(remote);CHECK(remote.model()!=old&&!remote.model()->vertices.empty());});
 test("real named clips for non-humanoid model",[&]{SAvatarRequest q;q.path=base+"/clips.glb";remote.select(q,3);loaded(remote);CHECK(!remote.model()->humanoid&&remote.model()->clips.size()==5);p={};p.active=true;p.onGround=true;step(2);CHECK(remote.animator().playing().find("Idle")!=std::string::npos);p.velocity.z=-1;step(30);CHECK(remote.animator().playing().find("Walk")!=std::string::npos);p.velocity.z=-5;p.running=true;step(30);CHECK(remote.animator().playing().find("Run")!=std::string::npos);p.onGround=false;p.velocity.y=4;step(3);CHECK(remote.animator().playing().find("Jump")!=std::string::npos);p.velocity.y=-3;step(80);CHECK(remote.animator().playing().find("Fall")!=std::string::npos);});
 test("missing file produces explicit fallback",[&]{SAvatarRequest q;q.path=base+"/not-present.glb";remote.select(q,4);wait([&]{remote.take();return !remote.error().empty();});CHECK(!remote.model());});
 test("none clears model and animation",[&]{remote.select({},5);CHECK(!remote.model()&&remote.animator().joints().empty());});
 test("disconnect cancels blocked load without waiting",[&]{CRemoteAvatar r(blocked);r.select(req,1);wait([]{return began.load();});auto start=CRemoteAvatar::Clock::now();r.stop();CHECK(CRemoteAvatar::Clock::now()-start<100ms);wait([&]{return r.finished();});r.take();CHECK(!r.model());});
 test("ten reload/disconnect cycles release ownership and fd/thread resources",[&]{int fds=count("/proc/self/fd"),threads=count("/proc/self/task");for(int i=0;i<10;++i){std::weak_ptr<SAvatarModel> weak;{CRemoteAvatar r;r.select(req,i);loaded(r);weak=r.model();r.stop();wait([&]{return r.finished();});CHECK(!r.model());}CHECK(weak.expired());}wait([&]{return count("/proc/self/task")==threads;});CHECK(count("/proc/self/fd")==fds);});
 test("local animation remains unchanged after all remote lifecycles",[&]{CHECK(localAnim.joints()==localJoints);CHECK(localAnim.morphWeights()==localMorph);CHECK(local->vertices.empty());localAnim.update(idle);CHECK(!localAnim.joints().empty());});
 remote.stop();wait([&]{return remote.finished();});
 std::cout<<passed<<"/18 real fork avatar groups passed (no Hyprland or GPU in this executable)\n";
 } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n';return 1; }
}
