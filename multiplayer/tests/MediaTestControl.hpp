#pragma once
// TEST DRIVER ONLY. Never used by the plugin. Delays SDP at the transport boundary
// without altering SDP, ICE callbacks, pipeline state or promise completion.
#include <hyprwalk/mp/VideoSignaling.hpp>
#include <chrono>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
namespace hyprwalk::mp::test {
inline bool mediaReady(const Status& s) noexcept {
    // Media uses TCP/world/WINDOWS/VIDEO. UDP readiness is tested separately.
    return s.state==State::Connected&&s.world==WorldState::Ready&&
        (s.capabilities&(WindowsCapability|VideoCapability))==(WindowsCapability|VideoCapability);
}
class DelayedAnswers {
public:
    using Clock=std::chrono::steady_clock;
    struct Held {VideoSignal signal;Clock::time_point until;bool early=false;};
    explicit DelayedAnswers(std::chrono::milliseconds delay):m_delay(delay){}
    std::vector<VideoSignal> route(VideoSignal signal,Clock::time_point now) {
        const auto key=signal.key;
        if(signal.kind==VideoSignalKind::Answer&&m_delay.count()>0){
            if(m_held.size()>=MaxVideoStreams||m_held.contains(key))throw std::runtime_error("driver: duplicate/too many delayed answers");
            m_held.emplace(key,Held{std::move(signal),now+m_delay,false});return {};
        }
        if(signal.kind==VideoSignalKind::Ice){
            if(auto it=m_held.find(key);it!=m_held.end()){
                it->second.early=true;m_early.insert(key);++m_earlyCount;
                if(signal.body.starts_with("0|candidate:")||signal.body.starts_with("0|a=candidate:"))m_earlyCandidates.insert(key);
            }
        }
        if(signal.kind==VideoSignalKind::Stop||signal.kind==VideoSignalKind::Failure)m_held.erase(key);
        return {std::move(signal)};
    }
    std::vector<VideoSignal> poll(Clock::time_point now) {
        std::vector<VideoSignal> out;
        for(auto it=m_held.begin();it!=m_held.end();){
            if(now<it->second.until){++it;continue;}
            // Release unconditionally. Absence of early ICE is a separate failed
            // assertion, not permission to deadlock the SDP handshake forever.
            if(!it->second.early)++m_withoutEarly;
            out.push_back(std::move(it->second.signal));it=m_held.erase(it);++m_released;
        }
        return out;
    }
    void clear(){m_held.clear();m_early.clear();m_earlyCandidates.clear();m_earlyCount=m_released=m_withoutEarly=0;}
    std::size_t pending()const noexcept{return m_held.size();}
    std::size_t earlyTracks()const noexcept{return m_early.size();}
    std::size_t earlyCandidateTracks()const noexcept{return m_earlyCandidates.size();}
    bool hadEarlyCandidate(const VideoTrackKey& key)const{return m_earlyCandidates.contains(key);}
    unsigned earlyCount()const noexcept{return m_earlyCount;}
    unsigned released()const noexcept{return m_released;}
    unsigned withoutEarly()const noexcept{return m_withoutEarly;}
private:
    std::chrono::milliseconds m_delay;
    std::map<VideoTrackKey,Held> m_held;
    std::set<VideoTrackKey> m_early,m_earlyCandidates;
    unsigned m_earlyCount=0,m_released=0,m_withoutEarly=0;
};
} // namespace hyprwalk::mp::test
