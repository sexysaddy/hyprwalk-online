#include <hyprwalk/mp/PlayerSync.hpp>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace hyprwalk::mp;
using namespace std::chrono_literals;
namespace {
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #x); } while (0)
bool close(float a,float b) { return std::abs(a-b)<0.005f; }
PlayerSync::Time time(int ms) { return PlayerSync::Time{}+std::chrono::milliseconds(ms); }
ReceivedPlayer point(std::uint32_t seq,int ms,float x,int delay=20) {
    ReceivedPlayer p; p.sequence = seq; p.senderTimeUs = static_cast<std::uint64_t>(ms)*1000;
    p.receivedAt = time(ms+delay); p.player.active = true; p.player.feet.x = x; return p;
}
void interpolate() {
    PlayerSync s; CHECK(s.push(point(1,0,0))); CHECK(s.push(point(2,100,2)));
    auto p=s.sample(time(170)); CHECK(p && close(p->player.feet.x,1) && !p->extrapolated);
    CHECK(close(s.sample(time(110))->player.feet.x,0));
}
void anglesAndFlags() {
    PlayerSync s; auto a=point(1,0,0),b=point(2,100,1);
    a.player.yaw=a.player.bodyYaw=3.12413936f; b.player.yaw=b.player.bodyYaw=-3.12413936f;
    a.player.pitch=-0.5f; b.player.pitch=0.5f; b.player.running=b.player.crouched=b.player.flying=true;
    CHECK(s.push(a)&&s.push(b)); const auto p=s.sample(time(170)); CHECK(p);
    CHECK(close(std::abs(p->player.yaw),3.14159265f)); CHECK(close(std::abs(p->player.bodyYaw),3.14159265f));
    CHECK(close(p->player.pitch,0) && !p->player.crouched);
    CHECK(s.sample(time(220))->player.crouched && s.sample(time(220))->player.flying);
}
void extrapolate() {
    PlayerSync s; auto a=point(1,0,0); a.player.velocity={10,2,-1}; CHECK(s.push(a));
    auto p=s.sample(time(170)); CHECK(p && close(p->player.feet.x,0.5f) && close(p->player.feet.y,0.1f));
    p=s.sample(time(420)); CHECK(p && p->frozen && close(p->player.feet.x,1)); CHECK(!s.sample(time(520)));
}
void reordering() {
    PlayerSync s; CHECK(s.push(point(0xfffffffe,0,0))); CHECK(s.push(point(0,100,2)));
    auto late=point(0xffffffff,50,0.5f,100); CHECK(s.push(late)); CHECK(!s.push(late));
    CHECK(close(s.sample(time(170))->player.feet.x,0.5f));
    CHECK(s.push(point(1,150,3))); CHECK(s.size()==4);
    CHECK(!s.push(point(0xffff0000,20,0)));
}
void loss() {
    PlayerSync s; CHECK(s.push(point(1,0,0))); CHECK(s.push(point(5,200,2)));
    CHECK(close(s.sample(time(220))->player.feet.x,1)); // Missing samples are interpolated, not retransmitted.
}
void teleports() {
    PlayerSync s; auto a=point(1,0,0); CHECK(s.push(a)); auto b=point(2,100,50); CHECK(s.push(b));
    CHECK(s.size()==1 && close(s.sample(time(120))->player.feet.x,50));
    a=point(1,50,0,100); CHECK(!s.push(a));
    b=point(3,200,51); b.player.teleport=1; CHECK(s.push(b)); CHECK(s.size()==1);
    b=point(2,150,49,100); CHECK(!s.push(b));
}
void activeAndReset() {
    PlayerSync s; auto a=point(1,0,0); CHECK(s.push(a)); a=point(2,100,0); a.player.active=false;
    CHECK(s.push(a)); CHECK(!s.sample(time(120))); a=point(3,200,1); CHECK(s.push(a)); CHECK(s.sample(time(220)));
    s.reset(); CHECK(s.size()==0 && !s.sample(time(220))); CHECK(s.push(point(1,0,100))); CHECK(close(s.sample(time(30))->player.feet.x,100));
}
void boundsAndClock() {
    PlayerSync s;
    for (unsigned i=0;i<10000;++i) CHECK(s.push(point(i,static_cast<int>(i)*33,0,20+(i%4))));
    CHECK(s.size()==PlayerSync::Capacity);
    auto bad=point(10001,330033,0); bad.player.velocity.y=std::numeric_limits<float>::quiet_NaN(); CHECK(!s.push(bad));
    bad=point(10001,330033,0); bad.senderTimeUs+=1'000'000'000; CHECK(!s.push(bad));
    bad=point(10001,330033,0); CHECK(s.push(bad)); CHECK(s.sample(bad.receivedAt));
}
void gapRecovery() {
    PlayerSync s; CHECK(s.push(point(1,0,0))); CHECK(!s.sample(time(1000)));
    CHECK(s.push(point(2,1000,2))); CHECK(s.size()==1 && close(s.sample(time(1020))->player.feet.x,2));
}
void validFlight() {
    PlayerSync s; auto a=point(1,0,0),b=point(2,200,20); a.player.velocity.x=b.player.velocity.x=100;
    a.player.flying=b.player.flying=true; CHECK(s.push(a)&&s.push(b)); CHECK(s.size()==2);
    CHECK(close(s.sample(time(220))->player.feet.x,10));
}
void settingsCheck() {
    auto bad=InterpolationSettings{}; bad.staleAfter=1ms; bool threw=false;
    try { PlayerSync s(bad); } catch(const std::invalid_argument&) { threw=true; } CHECK(threw);
}
}
int main() {
    const std::vector<std::pair<std::string,std::function<void()>>> tests{
        {"linear position interpolation and initial hold",interpolate},{"shortest yaw/bodyYaw, pitch, discrete flags",anglesAndFlags},
        {"bounded extrapolation, freeze, stale removal",extrapolate},{"out-of-order samples, duplicates and sequence wrap",reordering},
        {"interpolation over packet loss",loss},{"automatic/explicit teleport and old generation",teleports},
        {"inactive player, reactivation and reconnect reset",activeAndReset},{"fixed capacity, invalid values, clock leap",boundsAndClock},
        {"long gap resets timing",gapRecovery},{"fast flight is not a false teleport",validFlight},{"settings validation",settingsCheck}};
    unsigned count=0;
    for(const auto& [name,test]:tests) { try { test(); ++count; std::cout<<"PASS "<<name<<'\n'; }
      catch(const std::exception& e) { std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n'; return 1; } }
    std::cout<<count<<'/'<<tests.size()<<" PlayerSync test groups passed\n";
}
