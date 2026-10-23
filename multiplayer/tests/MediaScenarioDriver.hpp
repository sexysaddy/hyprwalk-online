#pragma once
// REAL WEBRTC TEST DRIVER ONLY. Synthetic pixels below are never compiled into
// the plugin. All media/promise/ICE/DTLS/VP8 work uses the production MediaSession.
#include "../media/gstreamer/MediaSession.hpp"
#include "MediaTestControl.hpp"
#include <hyprwalk/mp/Multiplayer.hpp>
#include <hyprwalk/mp/WindowSync.hpp>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <deque>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <thread>

namespace hyprwalk::mp::test {
using namespace std::chrono_literals;
using Clock=std::chrono::steady_clock;
struct MediaAdapter final:IAdapter {
    std::string name;WindowSync windows;std::optional<WorldRequest> world;
    std::deque<VideoSignal> signals;bool overflow=false;
    LocalInfo localInfo()const override{return{name};}
    std::optional<MapDescriptor> localMap()const override{MapDescriptor m;m.collision[0]=42;return m;}
    void onState(const Status& s)noexcept override{try{windows.session(s);}catch(...){overflow=true;}}
    void onWorldRequest(const WorldRequest& r)noexcept override{world=r;}
    void onWindows(const ReceivedWindows& w)noexcept override{try{windows.receive(w);}catch(...){overflow=true;}}
    void onVideoSignal(const VideoSignal& s)noexcept override{try{if(signals.size()==MaxVideoSignalQueue)overflow=true;else signals.push_back(s);}catch(...){overflow=true;}}
    void onDiagnostic(const Diagnostic& d)noexcept override{try{std::cout<<"CORE "<<d.text<<'\n';}catch(...){overflow=true;}}
};
struct MediaPhase {
    std::string name;
    std::vector<unsigned> allowed;
    bool square=false,metadataLate=false,injectError=false,requireError=false;
};
inline std::vector<MediaPhase> mediaPhases(const std::string& scenario){
    if(scenario=="one-way"||scenario=="duplex"||scenario=="early-ice"||scenario=="delayed-answer"||scenario=="duplicate-eoc"||scenario=="reconnect")return{{"establish",{1}}};
    if(scenario=="two-duplex")return{{"two-independent-streams",{1,2}}};
    if(scenario=="stop-one")return{{"two",{1,2}},{"stop-2-keep-1",{1}}};
    if(scenario=="regrant")return{{"two",{1,2}},{"withdraw-2",{1}},{"regrant-2",{1,2}}};
    if(scenario=="close-new")return{{"two",{1,2}},{"close-2-create-4",{1,4}}};
    if(scenario=="slot-transfer")return{{"two-plus-waiter",{1,2,3}},{"promote-3",{1,3}},{"regrant-2-preempts-3",{1,2,3}},{"promote-3-again",{1,3}}};
    if(scenario=="limit-three")return{{"three-metadata-three-video",{1,2,3}}};
    if(scenario=="metadata-late")return{{"two",{1,2}},{"withdraw-2",{1}},{"offer-before-metadata",{1,2},false,true}};
    if(scenario=="isolation")return{{"two",{1,2}},{"fail-1-keep-2",{1,2},false,false,true,true},{"retire-error",{2}},{"regrant-after-error",{1,2}}};
    if(scenario=="lifecycle")return{{"two-plus-waiter",{1,2,3}},{"resize",{1,2,3},true},{"promote-3",{1,3},true},{"regrant-2",{1,2,3},true},{"promote-3-again",{1,3},true},{"close-3-create-4",{1,4},true}};
    throw std::runtime_error("unknown media scenario: "+scenario);
}
inline std::vector<unsigned> videoSlots(std::vector<unsigned> ids,bool failed){
    std::sort(ids.begin(),ids.end());if(ids.size()>MaxVideoStreams)ids.resize(MaxVideoStreams);
    if(failed)std::erase(ids,1U);return ids;
}
inline unsigned parseUnsigned(std::string_view text){
    unsigned v=0;auto result=std::from_chars(text.data(),text.data()+text.size(),v);
    if(text.empty()||result.ec!=std::errc{}||result.ptr!=text.data()+text.size())throw std::runtime_error("invalid integer argument");return v;
}
inline std::shared_ptr<VideoFrame> testPixels(VideoTrackKey key,unsigned width,unsigned height,std::uint64_t serial,Clock::time_point now){
    auto f=std::make_shared<VideoFrame>();f->key=key;f->width=width;f->height=height;f->serial=serial;f->time=now;
    f->rgba.resize(std::size_t(width)*height*4,255);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){auto at=(std::size_t(y)*width+x)*4;
        f->rgba[at]=y<height/2?230:10;f->rgba[at+2]=y<height/2?10:230;
        f->rgba[at+1]=static_cast<std::uint8_t>(30+key.window.window*25);
        // Moving stripe away from the orientation sample points.
        if(x==serial%std::max(1U,width/4))f->rgba[at+1]=210;
    }
    return f;
}
inline std::uint64_t pixelDigest(const VideoFrame& f){std::uint64_t h=1469598103934665603ULL;for(auto b:f.rgba){h^=b;h*=1099511628211ULL;}return h;}
inline void checkPixels(const VideoFrame& f){
    const auto top=(std::size_t(f.height/4)*f.width+f.width/2)*4;
    const auto bottom=(std::size_t(3*f.height/4)*f.width+f.width/2)*4;
    if(!(f.rgba[top]>160&&f.rgba[top+2]<100&&f.rgba[bottom]<100&&f.rgba[bottom+2]>160))throw std::runtime_error("decoded color/orientation mismatch");
}
inline void runMediaRound(bool host,std::string ip,unsigned& port,const std::string& name,const std::string& scenario,unsigned round,Id& priorSession,bool earlyDefault){
    const auto phases=mediaPhases(scenario);
    MediaAdapter adapter;adapter.name=name;Settings settings;
    settings.enableUdp=settings.enableWorld=settings.enableWindows=settings.enableVideo=true;
    Multiplayer core(adapter,settings);VideoSettings video;video.width=320;video.height=240;video.bitrate=500000;
    media::MediaSession media(video);
    const bool early=earlyDefault||scenario=="early-ice"||scenario=="duplicate-eoc";
    const bool delay=early||scenario=="delayed-answer";
    DelayedAnswers answers(delay?500ms:0ms);
    std::string error;
    auto start=[&]{return host?core.host({ip,static_cast<std::uint16_t>(port)},error):core.join({ip,static_cast<std::uint16_t>(port)},error);};
    if(!start())throw std::runtime_error(error);
    auto finish=[&]{
        media.stop();media.shutdown();core.leave();const auto until=Clock::now()+1s;
        while(!core.finished()&&Clock::now()<until){core.dispatch();std::this_thread::sleep_for(2ms);}
        core.shutdown();
    };
    try{
        bool listening=false,started=false,phaseApplied=false,passSent=false,badSent=false,oldStopSent=false;
        auto overall=Clock::now()+45s,phaseAt=Clock::now(),nextSubmit=phaseAt,nextFrame=phaseAt,nextStats=phaseAt;
        unsigned phase=0,peerPhase=0,ownAckTarget=0,sentEoc=0,receivedEoc=0;
        bool peerHasPhase=false,udpSeen=false;
        std::uint64_t inputSerial=0;
        std::map<VideoTrackKey,std::uint64_t> seenSerial,decoded,baseline,digests,changes;
        std::map<unsigned,VideoTrackKey> firstKey,lastKey,oldTx;
        std::set<unsigned> selected;
        std::vector<VideoFrame> oldFrames;
        while(Clock::now()<overall){
            core.dispatch();if(adapter.overflow)throw std::runtime_error("test adapter queue overflow");
            if(adapter.world){core.confirmWorld({adapter.world->ticket,true,{}});adapter.world.reset();}
            const auto state=core.status();adapter.windows.session(state);
            if(host&&!listening&&state.state==State::Listening){port=state.endpoint.port;listening=true;std::cout<<"LISTEN "<<port<<" ROUND "<<round<<std::endl;}
            const auto now=Clock::now();const bool ready=mediaReady(state);udpSeen=udpSeen||state.udp.state==UdpState::Ready;
            const std::string prefix="HF3_PASS:"+std::to_string(round)+":";
            if(state.lastMessage.starts_with(prefix)){
                auto n=parseUnsigned(std::string_view(state.lastMessage).substr(prefix.size()));
                if(!peerHasPhase||n>peerPhase)peerPhase=n;peerHasPhase=true;
            }
            // The peer may send Bye in the same TCP read as the final TestAck.
            // Completion still requires our real-pixel assertion plus its PASS
            // and acknowledgement. Do not mistake that orderly teardown for a
            // world-readiness regression or erase already established evidence.
            if(started&&phaseApplied&&phase+1==phases.size()&&passSent&&peerHasPhase&&
               peerPhase>=phase&&state.testsAcknowledged>=ownAckTarget){
                const auto& complete=phases.at(phase);
                auto received=complete.allowed;if(scenario=="one-way"&&host)received.clear();
                std::cout<<"PASS phase="<<phase<<" name="<<complete.name<<" round="<<round<<" peer="<<name;
                for(auto id:videoSlots(received,complete.requireError)){const auto k=lastKey.at(id);
                    std::cout<<" RX["<<id<<",pub="<<k.publication<<",stream="<<k.stream<<",decoded="<<decoded[k]<<",changed="<<changes[k]<<"]";}
                std::cout<<" early_tracks="<<answers.earlyTracks()<<" early_candidate_tracks="<<answers.earlyCandidateTracks()<<" eoc_sent="<<sentEoc<<" eoc_received="<<receivedEoc<<std::endl;
                media.stop();for(auto& frame:oldFrames)if(media.submit(std::make_shared<VideoFrame>(frame)))throw std::runtime_error("old frame accepted after stop");
                if(!media.captures().empty()||!media.images().empty())throw std::runtime_error("stop did not close frame gates");
                finish();std::cout<<"WEBRTC_OK scenario="<<scenario<<" round="<<round<<" peer="<<name<<" actual VP8 pixels via ICE/DTLS/SRTP\n"<<std::flush;return;
            }
            if(state.state==State::Error)throw std::runtime_error(state.lastError);
            if(ready&&!started){
                if(priorSession==state.session)throw std::runtime_error("reconnect reused old session ID");
                priorSession=state.session;started=true;phaseAt=now;
                if(!adapter.windows.setMode(SharingMode::Selected))throw std::runtime_error("cannot select publication mode");
                std::cout<<"SCOPE round="<<round<<" session="<<idString(state.session)<<" local="<<idString(state.localId)<<std::endl;
            }
            if(!started){std::this_thread::sleep_for(2ms);continue;}
            if(!ready)throw std::runtime_error("world/video disappeared during scenario");
            const auto& plan=phases.at(phase);
            auto localIds=plan.allowed,peerIds=plan.allowed;
            if(scenario=="one-way"){if(host)peerIds.clear();else localIds.clear();}
            if(!phaseApplied){
                baseline=decoded;phaseAt=now;passSent=badSent=oldStopSent=false;nextSubmit=plan.metadataLate?now+400ms:now;
                std::vector<WindowMetadata> sources;
                for(auto n:localIds){const bool square=n==1&&plan.square;
                    sources.push_back({n,"test."+name,"SYNTHETIC TEST ONLY",{{float(n),2,0},{1,0,0},{0,-1,0},2,1,
                        static_cast<std::uint16_t>(square?96:128),static_cast<std::uint16_t>(square?96:64)}});}
                adapter.windows.sources(std::move(sources));
                for(auto id:localIds)if(!selected.contains(id)){
                    if(!adapter.windows.select(id,true))throw std::runtime_error("selection failed");
                }
                selected={localIds.begin(),localIds.end()};phaseApplied=true;
                std::cout<<"BEGIN phase="<<phase<<" name="<<plan.name<<std::endl;
            }
            if(now>=nextSubmit){
                if(!core.submitWindows(adapter.windows.publication()))throw std::runtime_error("window submission rejected");
                nextSubmit=now+100ms;
            }
            VideoContext context{state.session,state.localId,state.peerId,state.worldTicket,true,{},{}};
            for(const auto& w:adapter.windows.publication().windows)context.sending.push_back({{state.session,state.localId,state.worldTicket,w.id},adapter.windows.publicationEpoch(w.id),w.geometry.pixelWidth,w.geometry.pixelHeight});
            for(const auto& w:adapter.windows.remote())context.receiving.push_back(w.key);
            media.configure(std::move(context));media.acknowledge();
            while(!adapter.signals.empty()){
                auto signal=std::move(adapter.signals.front());adapter.signals.pop_front();
                if(signal.kind==VideoSignalKind::Ice&&signal.body=="0|")++receivedEoc;
                const auto serial=signal.serial;const auto kind=signal.kind;
                if(!media.signal(std::move(signal))&&kind==VideoSignalKind::Offer)
                    throw std::runtime_error("current Offer rejected at media boundary, serial="+std::to_string(serial));
            }
            auto send=[&](VideoSignal signal){
                if(signal.kind==VideoSignalKind::Ice&&signal.body=="0|"){
                    ++sentEoc;if(early){auto duplicate=signal;if(!core.sendVideoSignal(std::move(duplicate),error))throw std::runtime_error(error);}
                }
                if(!core.sendVideoSignal(std::move(signal),error))throw std::runtime_error(error);
            };
            for(auto& signal:media.takeSignals())for(auto& routed:answers.route(std::move(signal),now))send(std::move(routed));
            for(auto& signal:answers.poll(now))send(std::move(signal));
            if(early&&answers.withoutEarly())throw std::runtime_error("driver: Answer released safely, but early-ICE assertion failed (no early candidate)");
            if(now>=nextFrame){
                nextFrame=now+67ms;++inputSerial;
                PlayerSnapshot player;player.active=true;core.submitPlayer(player);
                for(auto k:media.captures()){
                    auto pub=adapter.windows.publication();auto it=std::find_if(pub.windows.begin(),pub.windows.end(),[&](const auto& w){return w.id==k.window.window;});
                    if(it==pub.windows.end())throw std::runtime_error("capture not in publication");
                    auto frame=testPixels(k,it->geometry.pixelWidth,it->geometry.pixelHeight,inputSerial,now);
                    if(oldFrames.size()<4)oldFrames.push_back(*frame);
                    if(!oldTx.contains(static_cast<unsigned>(k.window.window)))oldTx[static_cast<unsigned>(k.window.window)]=k;
                    media.submit(std::move(frame));
                }
            }
            if(plan.injectError&&!badSent){
                for(auto k:media.captures())if(k.window.window==1){
                    VideoSignal bad;bad.key=k;bad.kind=VideoSignalKind::Ice;bad.body="0|invalid-candidate-prefix";
                    if(!core.sendVideoSignal(std::move(bad),error))throw std::runtime_error(error);badSent=true;break;
                }
            }
            for(const auto& w:adapter.windows.remote())if(auto frame=media.frame(w.key)){
                if(frame->key.window.owner!=state.peerId||frame->key.window.generation!=state.worldTicket)throw std::runtime_error("wrong decoded scope");
                if(seenSerial[frame->key]==frame->serial)continue;
                seenSerial[frame->key]=frame->serial;++decoded[frame->key];checkPixels(*frame);
                const auto hash=pixelDigest(*frame);if(digests[frame->key]!=hash)++changes[frame->key];digests[frame->key]=hash;
                const auto id=static_cast<unsigned>(w.key.window);lastKey[id]=frame->key;firstKey.try_emplace(id,frame->key);
                if(id==1&&plan.square&&(frame->width!=96||frame->height!=96))continue;
            }
            // A late Stop for the old incarnation must not revoke a live regrant.
            if(plan.name.find("regrant")!=std::string::npos&&!oldStopSent){
                auto current=media.captures();
                for(auto k:current)if(oldTx.contains(static_cast<unsigned>(k.window.window))&&k.publication>oldTx.at(static_cast<unsigned>(k.window.window)).publication){
                    VideoSignal old;old.key=oldTx.at(static_cast<unsigned>(k.window.window));old.kind=VideoSignalKind::Stop;old.body="TEST stale Stop";
                    if(!core.sendVideoSignal(std::move(old),error))throw std::runtime_error(error);oldStopSent=true;break;
                }
            }
            bool passed=now-phaseAt>=800ms&&udpSeen;
            const auto expectRx=videoSlots(peerIds,plan.requireError),expectTx=videoSlots(localIds,plan.requireError);
            auto captures=media.captures();std::vector<unsigned> actualTx;for(auto k:captures)actualTx.push_back(static_cast<unsigned>(k.window.window));
            std::sort(actualTx.begin(),actualTx.end());passed=passed&&actualTx==expectTx;
            if(captures.size()>MaxVideoStreams||media.images().size()>MaxVideoStreams)throw std::runtime_error("configured video stream bound violated");
            std::vector<unsigned> actualMetadata;for(const auto& w:adapter.windows.remote())actualMetadata.push_back(static_cast<unsigned>(w.key.window));
            std::sort(actualMetadata.begin(),actualMetadata.end());std::sort(peerIds.begin(),peerIds.end());passed=passed&&actualMetadata==peerIds;
            for(auto id:expectRx){
                const auto* w=adapter.windows.findRemote(id);auto frame=w?media.frame(w->key):nullptr;
                if(!frame){passed=false;continue;}
                passed=passed&&decoded[frame->key]>=baseline[frame->key]+8&&changes[frame->key]>=2;
                if(early)passed=passed&&answers.hadEarlyCandidate(frame->key);
                if(id==1&&plan.square)passed=passed&&frame->width==96&&frame->height==96;
                if(plan.name.find("regrant-2")!=std::string::npos&&id==2)passed=passed&&frame->key.publication>firstKey[id].publication;
                if(plan.name=="regrant-after-error"&&id==1)passed=passed&&frame->key.publication>firstKey[id].publication;
            }
            for(const auto& k:media.images())if(std::find(expectRx.begin(),expectRx.end(),static_cast<unsigned>(k.window.window))==expectRx.end())passed=false;
            if(plan.requireError){
                bool txError=false,rxError=false;
                for(const auto& s:media.status())if(s.key.window.window==1&&(s.state=="Error"||s.state=="Unavailable")){if(s.sending)txError=true;else rxError=true;}
                passed=passed&&txError&&rxError;
            }
            if(plan.name=="three-metadata-three-video") {
                const auto* third=adapter.windows.findRemote(3);
                passed=passed&&third&&static_cast<bool>(media.frame(third->key));
            }
            if(early)passed=passed&&answers.earlyCandidateTracks()>=std::min<std::size_t>(MaxVideoStreams,peerIds.size())&&sentEoc&&receivedEoc;
            if(passed&&!passSent){
                ownAckTarget=static_cast<unsigned>(state.testsSent+1);
                if(!core.sendTest(prefix+std::to_string(phase),error))throw std::runtime_error(error);passSent=true;
            }
            if(phase+1<phases.size()&&passSent&&peerHasPhase&&peerPhase>=phase&&state.testsAcknowledged>=ownAckTarget){
                std::cout<<"PASS phase="<<phase<<" name="<<plan.name<<" round="<<round<<" peer="<<name;
                for(auto id:expectRx){const auto k=lastKey.at(id);std::cout<<" RX["<<id<<",pub="<<k.publication<<",stream="<<k.stream<<",decoded="<<decoded[k]<<",changed="<<changes[k]<<"]";}
                std::cout<<" early_tracks="<<answers.earlyTracks()<<" early_candidate_tracks="<<answers.earlyCandidateTracks()<<" eoc_sent="<<sentEoc<<" eoc_received="<<receivedEoc<<std::endl;
                ++phase;phaseApplied=false;
            }
            if(!media.error().empty())throw std::runtime_error(media.error());
            if(now-phaseAt>15s)throw std::runtime_error("scenario assertion timeout: "+plan.name);
            if(now>=nextStats){nextStats=now+3s;std::cout<<media.diagnostic()<<std::flush;}
            std::this_thread::sleep_for(2ms);
        }
        throw std::runtime_error("scenario overall deadline exceeded");
    }catch(...){std::cerr<<media.diagnostic()<<std::flush;finish();throw;}
}
inline int mediaScenarioMain(int argc,char** argv,bool earlyDefault){
    try{
        const auto available=media::MediaSession::probe();std::cout<<available.detail<<std::endl;
        if(!available.available){std::cout<<"SKIP: genuine libnice/WebRTC SDK/runtime required\n";return 77;}
        if(argc==2&&std::string_view(argv[1])=="probe")return 0;
        if(argc<5||argc>6)throw std::runtime_error("usage: media_cli host|join numeric-ip port name [scenario]");
        const bool host=std::string_view(argv[1])=="host";
        if(!host&&std::string_view(argv[1])!="join")throw std::runtime_error("invalid role");
        unsigned port=parseUnsigned(argv[3]);if(port>65535||(!host&&!port))throw std::runtime_error("invalid port");
        const std::string scenario=argc==6?argv[5]:(earlyDefault?"early-ice":"lifecycle");
        Id previous{};
        for(unsigned round=0;round<(scenario=="reconnect"?2U:1U);++round){
            if(round)std::this_thread::sleep_for(host?100ms:700ms);
            runMediaRound(host,argv[2],port,argv[4],scenario,round,previous,earlyDefault);
        }
        if(scenario=="reconnect")std::cout<<"RECONNECT_OK distinct session IDs in the same processes\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
} // namespace hyprwalk::mp::test
