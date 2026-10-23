#include "../media/gstreamer/IceSignaling.hpp"
#include <hyprwalk/mp/VideoSignaling.hpp>
#include <hyprwalk/mp/Protocol.hpp>
#include <functional>
#include <iostream>
#include <vector>

using namespace hyprwalk::mp;
using namespace hyprwalk::mp::media::detail;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #x); } while(0)
const std::string candidate = "0|candidate:1 1 UDP 2122260223 10.109.150.168 45000 typ host";
void rejects(const std::string& body) { bool threw=false;try{(void)parseIce(body);}catch(const std::runtime_error&){threw=true;}CHECK(threw); }
Id id(unsigned n){Id x{};x[0]=static_cast<std::uint8_t>(n);return x;}
VideoSignal signal(std::string body){VideoSignal s;s.kind=VideoSignalKind::Ice;s.key={{id(1),id(2),id(4),1},1,1};s.sender=id(2);s.serial=1;s.body=std::move(body);return s;}
void normal(){auto c=parseIce(candidate);CHECK(!c.end()&&c.wire()==candidate);}
void end(){CHECK(parseIce("0|").end()&&parseIce("0|").wire()=="0|");}
void attribute(){CHECK(parseIce("0|a="+candidate.substr(2)).wire()==candidate);rejects("0|a=");rejects("0|a=end-of-candidates");}
void addresses(){for(const auto* s:{"0|candidate:12 1 UDP 123 2001:db8::1 5000 typ host","0|candidate:13 1 udp 123 node-1.local 5001 typ host","0|candidate:14 1 TCP 123 10.109.150.60 9 typ host tcptype active","0|candidate:15 1 UDP 123 203.0.113.4 5002 typ srflx raddr 10.0.0.1 rport 4000","0|candidate:16 2 UDP 123 192.0.2.2 6001 typ relay"})CHECK(!parseIce(s).end());}
void mlines(){for(const auto* s:{"", "|", "2|", "00|", "-1|", "4294967296|", "0", "0 |candidate:x", "0|candidate:", "0|nope"})rejects(s);rejects("1|");}
void rejectsNonVideoMline(){rejects("1|");rejects("1|candidate:2 1 UDP 2122260223 10.109.150.168 45001 typ host");IceQueue q(8);CHECK(q.push(candidate));CHECK(q.push("0|"));q.ready();CHECK(q.pop()->wire()==candidate);CHECK(q.pop()->end());}
void controls(){for(char ch:{'\0','\n','\r','\t','\x1b','\x7f',static_cast<char>(0xff),'|'})rejects(candidate+ch);}
void fields(){for(const auto* s:{"0|candidate:x 1 UDP", "0|candidate:x 0 UDP 1 127.0.0.1 2 typ host", "0|candidate:x 1 BAD 1 127.0.0.1 2 typ host", "0|candidate:x 1 UDP 4294967296 127.0.0.1 2 typ host", "0|candidate:x 1 UDP 1 127.0.0.1 65536 typ host", "0|candidate:x 1 UDP 1 127.0.0.1 2 type host", "0|candidate:x 1 UDP 1 127.0.0.1 2 typ bad", "0|candidate:x 1 UDP 1 127.0.0.1 2 typ host incomplete", "0|candidate:$ 1 UDP 1 127.0.0.1 2 typ host"})rejects(s);}
void length(){std::string body=candidate+" extra ";body.append(MaxIceBody-body.size(),'x');CHECK(body.size()==MaxIceBody&&!parseIce(body).end());rejects(body+'x');}
void repeatedEnd(){IceQueue q(128);q.ready();CHECK(q.push("0|"));for(unsigned n=0;n<300;++n)CHECK(!q.push("0|"));CHECK(q.candidates()==0&&q.pending()==1&&q.pop()->end()&&!q.pop());}
void onlyEnd(){IceQueue q(128);CHECK(q.push("0|")&&!q.pop());q.ready();CHECK(q.pop()->end()&&q.endDelivered()&&q.candidates()==0);}
void quota(){IceQueue q(128);for(unsigned n=0;n<128;++n)CHECK(q.push(candidate));CHECK(q.push("0|")&&q.pending()==129&&q.candidates()==128);q.ready();for(unsigned n=0;n<128;++n)CHECK(!q.pop()->end());CHECK(q.pop()->end()&&!q.pop());}
void overflow(){IceQueue q(128);for(unsigned n=0;n<128;++n)q.push(candidate);bool threw=false;try{q.push(candidate);}catch(const std::runtime_error&){threw=true;}CHECK(threw&&q.candidates()==128&&q.pending()==128);CHECK(q.push("0|"));}
void localQuota(){IceQueue q(MaxLocalIceCandidates);for(unsigned n=0;n<64;++n)q.push(candidate);CHECK(q.push("0|")&&q.pending()==65&&q.candidates()==64);}
void beforeSdp(){IceQueue q(128);q.push(candidate);q.push("0|");for(unsigned n=0;n<100;++n)CHECK(!q.pop());q.ready();CHECK(q.pop()->wire()==candidate);CHECK(q.pop()->end());}
void stop(){IceQueue q(128);q.push(candidate);q.push("0|");q.stop();q.ready();CHECK(q.pending()==0&&!q.pop()&&!q.push(candidate)&&!q.push("0|"));}
void late(){IceQueue q(128);q.ready();q.push("0|");CHECK(q.pop()->end());CHECK(!q.push(candidate)&&q.candidates()==0&&!q.pop());rejects("0|junk");}
void independent(){IceQueue a(1),b(128);a.ready();b.ready();a.push(candidate);bool threw=false;try{a.push(candidate);}catch(...){threw=true;}CHECK(threw);b.push(candidate);b.push("0|");a.stop();CHECK(b.pop()->wire()==candidate&&b.pop()->end());}
void reconnect(){for(unsigned n=0;n<200;++n){IceQueue q(128);CHECK(!q.endSeen()&&q.candidates()==0);q.push(candidate);q.push("0|");q.ready();CHECK(!q.pop()->end()&&q.pop()->end());q.stop();CHECK(q.pending()==0);}}
void wireRoundtrip(){for(const auto& body:{candidate,std::string("0|")}){auto s=signal(body);std::string error;CHECK(validVideoSignal(s,error));auto chunks=fragmentVideoSignal(s);CHECK(chunks.size()==1);VideoAssembler a;auto out=a.accept(chunks[0],{},error);CHECK(out&&error.empty()&&out->body==body&&out->key==s.key);CHECK(parseIce(out->body).wire()==body);}}
void tcpRoundtrip(){wire::Message msg;msg.type=wire::Type::Video;msg.videoChunk=fragmentVideoSignal(signal("0|"))[0];auto bytes=wire::encode(msg);wire::Decoder decoder;VideoAssembler assembler;std::string error;unsigned count=0;for(auto byte:bytes)CHECK(decoder.feed({&byte,1},[&](wire::Frame&& f){wire::Message out;CHECK(wire::decode(f,out,error));auto s=assembler.accept(out.videoChunk,{},error);CHECK(s&&s->body=="0|"&&parseIce(s->body).end());++count;return true;},error));CHECK(count==1);}
void scope(){auto s=signal("0|");Status st;st.state=State::Connected;st.world=WorldState::Ready;st.capabilities=31;st.session=id(1);st.localId=id(3);st.peerId=id(2);st.worldTicket=id(4);CHECK(videoScope(s,st,true));s.key.window.generation=id(5);CHECK(!videoScope(s,st,true));s=signal("0|");s.key.window.session=id(6);CHECK(!videoScope(s,st,true));s=signal("0|");s.sender=id(7);CHECK(!videoScope(s,st,true));}
void wireReplay(){VideoAssembler a;auto s=signal("0|");auto b=fragmentVideoSignal(s)[0];std::string error;CHECK(a.accept(b,{},error));CHECK(!a.accept(b,{},error)&&!error.empty());a.reset();CHECK(a.accept(b,{},error));}
int main(){unsigned count=0;try{for(auto [name,f]:std::vector<std::pair<const char*,std::function<void()>>>{{"candidate",normal},{"end-of-candidates",end},{"SDP attribute normalization",attribute},{"IPv4 IPv6 FQDN TCP srflx relay",addresses},{"mline/prefix",mlines},{"reject non-video ICE m-lines",rejectsNonVideoMline},{"control characters",controls},{"malformed fields",fields},{"2048-byte boundary",length},{"duplicate EOC",repeatedEnd},{"EOC with no candidates",onlyEnd},{"128 candidates plus reserved EOC",quota},{"candidate overflow",overflow},{"local 64 plus EOC",localQuota},{"queue before SDP",beforeSdp},{"stop clears queue",stop},{"post-EOC same generation",late},{"video-only ICE queue errors",independent},{"200 reconnect queue lifetimes",reconnect},{"type16 EOC roundtrip",wireRoundtrip},{"partial TCP EOC",tcpRoundtrip},{"scope validation",scope},{"signal replay/reset",wireReplay}}){f();++count;std::cout<<"PASS "<<name<<'\n';}std::cout<<count<<" ICE groups passed\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
