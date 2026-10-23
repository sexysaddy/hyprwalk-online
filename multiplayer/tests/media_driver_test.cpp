#include "MediaTestControl.hpp"
#include <iostream>
#include <source_location>
using namespace hyprwalk::mp;using namespace hyprwalk::mp::test;using namespace std::chrono_literals;
static void check(bool v,const std::source_location at=std::source_location::current()){if(!v)throw std::runtime_error("driver control line "+std::to_string(at.line()));}
int main(){try{
 unsigned groups=0;auto pass=[&](const char* s){++groups;std::cout<<"PASS "<<s<<'\n';};
 Status state;state.state=State::Connected;state.world=WorldState::Ready;state.capabilities=31;
 state.udp.state=UdpState::Probing;check(mediaReady(state));state.udp.state=UdpState::Ready;check(mediaReady(state));
 pass("media readiness independent of UDP transition");
 state.world=WorldState::Applying;check(!mediaReady(state));state.world=WorldState::Ready;state.capabilities=15;check(!mediaReady(state));
 pass("media still requires actual world/VIDEO permission");
 auto now=DelayedAnswers::Clock::now();VideoSignal answer;answer.key.window.window=1;answer.key.publication=answer.key.stream=1;answer.kind=VideoSignalKind::Answer;answer.body="SDP";
 DelayedAnswers delay(500ms);check(delay.route(answer,now).empty());check(delay.poll(now+499ms).empty());
 auto out=delay.poll(now+500ms);check(out.size()==1&&out[0].body=="SDP"&&delay.pending()==0&&delay.withoutEarly()==1);
 pass("Answer always released on deadline even without early ICE");
 delay.clear();delay.route(answer,now);auto ice=answer;ice.kind=VideoSignalKind::Ice;ice.body="0|candidate:1 1 UDP 1 127.0.0.1 45000 typ host";
 out=delay.route(ice,now+10ms);check(out.size()==1&&delay.earlyTracks()==1);out=delay.poll(now+500ms);check(out.size()==1&&delay.withoutEarly()==0);
 pass("ICE forwarded before delayed SDP without changing body or transport serial");
 delay.clear();auto b=answer;b.key.window.window=2;delay.route(answer,now);delay.route(b,now+20ms);delay.route(ice,now+10ms);
 check(delay.poll(now+500ms).size()==1&&delay.pending()==1);check(delay.poll(now+520ms).size()==1&&delay.withoutEarly()==1);
 pass("two independently delayed answers do not gate one another");
 delay.clear();delay.route(answer,now);auto stop=answer;stop.kind=VideoSignalKind::Stop;check(delay.route(stop,now).size()==1);check(delay.poll(now+1s).empty());
 pass("Stop cancels held answer and never sends it after revoke");
 delay.clear();delay.route(answer,now);auto fail=answer;fail.kind=VideoSignalKind::Failure;delay.route(fail,now);check(delay.pending()==0);
 pass("Failure cancels held answer");
 delay.clear();delay.route(answer,now);ice.body="0|";delay.route(ice,now+20ms);delay.route(ice,now+30ms);check(delay.earlyTracks()==1&&delay.earlyCount()==2);check(delay.poll(now+500ms).size()==1);
 pass("repeated EOC never releases multiple answers");
 check(delay.earlyCandidateTracks()==0);delay.clear();delay.route(answer,now);
 ice.body="0|candidate:1 1 UDP 1 127.0.0.1 45000 typ host";delay.route(ice,now+20ms);
 check(delay.earlyCandidateTracks()==1&&delay.hadEarlyCandidate(answer.key)&&!delay.hadEarlyCandidate(b.key));
 pass("ordinary early candidate evidence is distinct from EOC-only traffic");
 delay.clear();delay.route(answer,now);bool caught=false;try{delay.route(answer,now);}catch(const std::runtime_error&){caught=true;}check(caught&&delay.pending()==1);
 pass("duplicate pending key fails explicitly without silent emplace loss");
 delay.clear();check(delay.pending()==0&&delay.earlyTracks()==0&&delay.released()==0);
 pass("new session clears old delayed SDP and counters");
 std::cout<<groups<<"/11 pure driver-control groups passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
