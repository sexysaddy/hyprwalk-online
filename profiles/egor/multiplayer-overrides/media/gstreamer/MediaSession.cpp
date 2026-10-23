#include "MediaSession.hpp"
#include "IceSignaling.hpp"
// The Track and receiver PCM probe types are parsed in video-only builds too.
// These two standalone helpers have no PipeWire SDK dependency.
#include <hyprwalk/mp/Protocol.hpp>
#define GST_USE_UNSTABLE_API
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/sdp/sdp.h>
#include <gst/webrtc/webrtc.h>
#include <gst/video/video-event.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstring>
#include <cstdlib>
#include <string_view>
#include <functional>
#include <iomanip>
#include <limits>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>

namespace hyprwalk::mp::media {
namespace {
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
using namespace std::chrono_literals;
void wake(int fd) noexcept {std::uint64_t one=1;if(fd>=0){const auto n=::write(fd,&one,sizeof(one));(void)n;}}
void drain(int fd) noexcept {std::uint64_t n;while(fd>=0&&::read(fd,&n,sizeof(n))==sizeof(n)){} }
std::string errorText(GError* error){std::string s=error&&error->message?error->message:"GStreamer operation failed";if(error)g_error_free(error);return s;}
bool contains(const std::vector<WindowKey>& list,const WindowKey& key){return std::find(list.begin(),list.end(),key)!=list.end();}
bool sameWorld(const VideoContext& a,const VideoContext& b){return a.session==b.session&&a.local==b.local&&a.peer==b.peer&&a.world==b.world;}
bool scoped(const VideoTrackKey& key,const VideoContext& c){return key.window.session==c.session&&key.window.generation==c.world&&(key.window.owner==c.local||key.window.owner==c.peer)&&key.publication&&key.stream;}
std::string frameSize(unsigned w,unsigned h){return std::to_string(w)+"x"+std::to_string(h);}
unsigned keyframeDistanceForInputFps(double measuredFps,unsigned fallbackFps) noexcept {
    if(!fallbackFps)fallbackFps=60U;
    double fps=measuredFps;
    if(!std::isfinite(fps)||fps<0.5)fps=static_cast<double>(fallbackFps);
    return static_cast<unsigned>(std::ceil(std::clamp(fps,1.0,120.0)));
}
constexpr auto MediaBufferProbe = static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST);
void countCandidateType(std::string_view candidate, unsigned& host, unsigned& srflx,
                        unsigned& relay, unsigned& prflx, unsigned& other) noexcept {
    if(candidate.empty())return;
    if(candidate.find(" typ host")!=std::string_view::npos)++host;
    else if(candidate.find(" typ srflx")!=std::string_view::npos)++srflx;
    else if(candidate.find(" typ relay")!=std::string_view::npos)++relay;
    else if(candidate.find(" typ prflx")!=std::string_view::npos)++prflx;
    else ++other;
}
const char* connectionStateName(int state) noexcept {
    switch(state){case 0:return "new";case 1:return "connecting";case 2:return "connected";
        case 3:return "disconnected";case 4:return "failed";case 5:return "closed";default:return "unknown";}
}
const char* iceStateName(int state) noexcept {
    switch(state){case 0:return "new";case 1:return "checking";case 2:return "connected";
        case 3:return "completed";case 4:return "failed";case 5:return "disconnected";
        case 6:return "closed";default:return "unknown";}
}
const char* gatheringStateName(int state) noexcept {
    switch(state){case 0:return "new";case 1:return "gathering";case 2:return "complete";default:return "unknown";}
}
void configureIceServers(GstElement* rtc) {
    // Optional and local-only: no public STUN/TURN provider or credentials are
    // hard-coded. Never include these URIs in diagnostics because TURN may carry
    // credentials. TURN is only needed when a direct ICE path cannot be formed.
    if(const char* stun=std::getenv("HYPRWALK_STUN_SERVER");stun&&*stun){
        auto* property=g_object_class_find_property(G_OBJECT_GET_CLASS(rtc),"stun-server");
        if(!property||!(property->flags&G_PARAM_WRITABLE))
            throw std::runtime_error("GStreamer webrtcbin has no writable stun-server property");
        g_object_set(rtc,"stun-server",stun,nullptr);
    }
    if(const char* turn=std::getenv("HYPRWALK_TURN_SERVER");turn&&*turn){
        if(!g_signal_lookup("add-turn-server",G_OBJECT_TYPE(rtc)))
            throw std::runtime_error("GStreamer webrtcbin does not support add-turn-server");
        gboolean added=FALSE;
        g_signal_emit_by_name(rtc,"add-turn-server",turn,&added);
        if(!added)throw std::runtime_error("GStreamer rejected HYPRWALK_TURN_SERVER URI (URI omitted for credential safety)");
    }
}
std::string senderPipeline(const VideoSettings& settings,unsigned width,unsigned height){
    std::ostringstream desc;
    // A parsed bin must end with an element. A bare final caps expression is
    // parsed as an element name ("application") rather than a trailing filter.
    const unsigned targetBitrate=targetVideoBitrate(settings,width,height);
    desc<<"appsrc name=source is-live=true format=time do-timestamp=true block=false max-buffers=1 leaky-type=downstream "
        <<"! queue max-size-buffers=1 max-size-time=0 max-size-bytes=0 leaky=downstream "
        <<"! videoconvert ! video/x-raw,format=I420 "
        // VP8 remains the interoperable WebRTC baseline, but use its realtime
        // low-CPU profile: one worker thread and a short keyframe recovery interval.
        <<"! vp8enc name=encoder deadline=1 cpu-used=10 threads=1 target-bitrate="<<targetBitrate
        // One second at the configured input rate. tick() tightens this to the
        // measured submit rate because static frames are intentionally throttled.
        <<" keyframe-max-dist="<<std::max(1U,settings.fps?settings.fps:60U)
        <<" ! rtpvp8pay pt=96 picture-id-mode=15-bit mtu=1000 "
        <<"! capsfilter caps=\"application/x-rtp,media=video,encoding-name=VP8,clock-rate=90000,payload=96,rtcp-fb-nack=(boolean)true,rtcp-fb-nack-pli=(boolean)true,rtcp-fb-ccm-fir=(boolean)true\"";
    return desc.str();
}
const char* receiverPipeline(){return "queue name=rx_rtp_queue max-size-buffers=512 max-size-time=0 max-size-bytes=4194304 leaky=no ! rtpvp8depay name=vp8_depay request-keyframe=true wait-for-keyframe=true ! vp8dec name=vp8_decoder threads=1 ! videoconvert ! video/x-raw,format=RGBA ! appsink name=decoded sync=false async=false emit-signals=false max-buffers=1 drop=true enable-last-sample=false";}
struct PromiseReady {std::atomic<bool> done{false};};

bool startsWith(std::string_view value, std::string_view prefix) noexcept {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}
const GstStructure* nestedStats(const GstStructure* root, const char* id) noexcept {
    if (!root || !id || !*id) return nullptr;
    const GValue* value = gst_structure_get_value(root, id);
    return value && GST_VALUE_HOLDS_STRUCTURE(value) ? gst_value_get_structure(value) : nullptr;
}
bool getNonnegativeUint(const GstStructure* structure, const char* field, guint& value) noexcept {
    if (!structure || !field) return false;
    if (gst_structure_get_uint(structure, field, &value)) return true;
    gint signedValue = 0;
    if (gst_structure_get_int(structure, field, &signedValue) && signedValue >= 0) {
        value = static_cast<guint>(signedValue);
        return true;
    }
    return false;
}
bool isVideoRtpStats(const GstStructure* root, const GstStructure* item) noexcept {
    if (!root || !item) return false;
    if (const char* kind = gst_structure_get_string(item, "kind")) {
        if (std::string_view(kind) != "video") return false;
    }

    // Do not infer VP8 from `kind=video` or a 90 kHz clock alone: RTX is also
    // video/90 kHz. Resolve codec-id and require the actual codec MIME type.
    // Payload numbers are dynamic SDP identifiers, not codec identities.
    const char* codecId = gst_structure_get_string(item, "codec-id");
    const auto* codec = nestedStats(root, codecId);
    if (!codec) return false;
    const char* mime = gst_structure_get_string(codec, "mime-type");
    if (!mime || g_ascii_strcasecmp(mime, "video/VP8") != 0) return false;
    guint clockRate = 0;
    return getNonnegativeUint(codec, "clock-rate", clockRate) && clockRate == 90000;
}
struct CompactRtpStats {
    bool found = false;
    guint64 packets = 0, bytes = 0, repaired = 0, discarded = 0;
    gint64 lost = 0;
    guint nack = 0, pli = 0, fir = 0;
    gdouble jitter = 0.0, rtt = 0.0, fractionLost = 0.0;
    bool hasPackets = false, hasBytes = false, hasLost = false;
    bool hasNack = false, hasPli = false, hasJitter = false, hasRtt = false, hasFractionLost = false;
};
void readCommonRtp(const GstStructure* item, CompactRtpStats& out) noexcept {
    out.found = true;
    out.hasPackets = gst_structure_get_uint64(item, "packets-received", &out.packets) ||
                     gst_structure_get_uint64(item, "packets-sent", &out.packets);
    out.hasBytes = gst_structure_get_uint64(item, "bytes-received", &out.bytes) ||
                   gst_structure_get_uint64(item, "bytes-sent", &out.bytes);
    out.hasLost = gst_structure_get_int64(item, "packets-lost", &out.lost);
    if (!out.hasLost) {
        // GStreamer has used both G_TYPE_INT and G_TYPE_INT64 for packet-loss
        // fields across stats implementations. Accept both representations.
        gint lost32 = 0;
        if (gst_structure_get_int(item, "packets-lost", &lost32)) {
            out.lost = lost32;
            out.hasLost = true;
        }
    }
    out.hasNack = gst_structure_get_uint(item, "nack-count", &out.nack);
    out.hasPli = gst_structure_get_uint(item, "pli-count", &out.pli);
    (void)gst_structure_get_uint(item, "fir-count", &out.fir);
    out.hasJitter = gst_structure_get_double(item, "jitter", &out.jitter);
    if (!out.hasJitter) out.hasJitter = gst_structure_get_double(item, "jitter-buffer-delay", &out.jitter);
    out.hasFractionLost = gst_structure_get_double(item, "fraction-lost", &out.fractionLost);
    out.hasRtt = gst_structure_get_double(item, "round-trip-time", &out.rtt);
    (void)gst_structure_get_uint64(item, "packets-repaired", &out.repaired);
    (void)gst_structure_get_uint64(item, "packets-discarded", &out.discarded);
}
std::string summarizeWebRtcStats(const GstStructure* root, bool sending) {
    if (!root) return "WebRTC stats unavailable: empty report";
    CompactRtpStats outbound, inbound, remoteInbound;
    std::string selectedPairId, selectedPairState, selectedLocalId, selectedRemoteId;
    gdouble pairRtt = 0.0, availableBitrate = 0.0;
    bool hasPairRtt = false, hasAvailableBitrate = false;
    const gint fields = gst_structure_n_fields(root);
    for (gint i = 0; i < fields; ++i) {
        const char* name = gst_structure_nth_field_name(root, i);
        const auto* item = nestedStats(root, name);
        if (!name || !item) continue;
        const std::string_view field(name);
        // `get-stats` is queried for this exact webrtcbin pad. Still only parse
        // the direction represented by this track; ignore RTX and remote records.
        if (sending && !outbound.found && startsWith(field, "rtp-outbound-stream-stats_") && isVideoRtpStats(root, item)) {
            readCommonRtp(item, outbound);
        } else if (!sending && !inbound.found && startsWith(field, "rtp-inbound-stream-stats_") && isVideoRtpStats(root, item)) {
            readCommonRtp(item, inbound);
        } else if (sending && !remoteInbound.found && startsWith(field, "rtp-remote-inbound-stream-stats_") && isVideoRtpStats(root, item)) {
            readCommonRtp(item, remoteInbound);
        }
        if (startsWith(field, "transport-stats_")) {
            if (const char* id = gst_structure_get_string(item, "selected-candidate-pair-id")) selectedPairId = id;
        }
    }
    if (!selectedPairId.empty()) {
        if (const auto* pair = nestedStats(root, selectedPairId.c_str())) {
            if (const char* state = gst_structure_get_string(pair, "state")) selectedPairState = state;
            if (const char* id = gst_structure_get_string(pair, "local-candidate-id")) selectedLocalId = id;
            if (const char* id = gst_structure_get_string(pair, "remote-candidate-id")) selectedRemoteId = id;
            hasPairRtt = gst_structure_get_double(pair, "current-round-trip-time", &pairRtt);
            hasAvailableBitrate = gst_structure_get_double(pair, "available-outgoing-bitrate", &availableBitrate);
        }
    }
    auto candidateLabel = [&](const std::string& id) -> std::string {
        const auto* candidate = nestedStats(root, id.c_str());
        if (!candidate) return {};
        const char* type = gst_structure_get_string(candidate, "candidate-type");
        const char* protocol = gst_structure_get_string(candidate, "protocol");
        std::string value = type ? type : "?";
        value += '/'; value += protocol ? protocol : "?";
        return value;
    };
    std::ostringstream out;
    out << "WebRTC stats";
    bool any = false;
    if (outbound.found) {
        out << " video_out=";
        if (outbound.hasPackets) out << outbound.packets; else out << '?';
        out << "pkts/";
        if (outbound.hasBytes) out << outbound.bytes; else out << '?';
        out << "B nack=" << (outbound.hasNack ? std::to_string(outbound.nack) : "?")
            << " pli=" << (outbound.hasPli ? std::to_string(outbound.pli) : "?");
        any = true;
    }
    if (inbound.found) {
        out << " video_in=";
        if (inbound.hasPackets) out << inbound.packets; else out << '?';
        out << "pkts/";
        if (inbound.hasBytes) out << inbound.bytes; else out << '?';
        out << "B lost=" << (inbound.hasLost ? std::to_string(inbound.lost) : "?");
        if (inbound.hasLost && inbound.hasPackets) {
            const auto nonnegativeLost = static_cast<guint64>(std::max<gint64>(0, inbound.lost));
            const double denominator = static_cast<double>(std::max<guint64>(1, inbound.packets + nonnegativeLost));
            out << " loss_pct=" << std::fixed << std::setprecision(1) << (100.0 * static_cast<double>(nonnegativeLost) / denominator);
        }
        out << " repaired=" << inbound.repaired << " discarded=" << inbound.discarded
            << " nack=" << (inbound.hasNack ? std::to_string(inbound.nack) : "?")
            << " pli=" << (inbound.hasPli ? std::to_string(inbound.pli) : "?");
        if (inbound.hasJitter) out << " jitter_ms=" << std::fixed << std::setprecision(1) << inbound.jitter * 1000.0;
        any = true;
    }
    if (remoteInbound.found) {
        out << " peer_loss=" << (remoteInbound.hasLost ? std::to_string(remoteInbound.lost) : "?");
        if (remoteInbound.hasFractionLost) out << " peer_loss_pct=" << std::fixed << std::setprecision(1) << remoteInbound.fractionLost * 100.0;
        if (remoteInbound.hasRtt) out << " rtt_ms=" << std::fixed << std::setprecision(1) << remoteInbound.rtt * 1000.0;
        any = true;
    }
    if (!selectedPairState.empty() || !selectedPairId.empty()) {
        out << " pair=" << (selectedPairState.empty() ? "selected" : selectedPairState);
        const auto local = candidateLabel(selectedLocalId), remote = candidateLabel(selectedRemoteId);
        if (!local.empty() || !remote.empty()) out << " route=" << (local.empty() ? "?" : local) << "->" << (remote.empty() ? "?" : remote);
        if (hasPairRtt) out << " pair_rtt_ms=" << std::fixed << std::setprecision(1) << pairRtt * 1000.0;
        if (hasAvailableBitrate) out << " available_kbps=" << std::fixed << std::setprecision(0) << availableBitrate / 1000.0;
        any = true;
    }
    if (!any) out << " no VP8 RTP records yet fields=" << fields;
    return out.str().substr(0, 900);
}
}
Availability MediaSession::probe(){
    Availability out;GError* error=nullptr;
    if(!gst_init_check(nullptr,nullptr,&error)){out.detail=errorText(error);return out;}
    guint major=0,minor=0,micro=0,nano=0;gst_version(&major,&minor,&micro,&nano);
    out.version=std::to_string(major)+"."+std::to_string(minor)+"."+std::to_string(micro);
    if(major!=1||minor<22){out.detail="GStreamer >= 1.22 required; found "+out.version;return out;}
    for(const char* name:{"webrtcbin","nicesrc","nicesink","dtlssrtpenc","dtlssrtpdec","appsrc","appsink","queue","videoconvert","vp8enc","vp8dec","rtpvp8pay","rtpvp8depay"}){
        auto* factory=gst_element_factory_find(name);
        if(factory)gst_object_unref(factory);
        else {if(out.detail.empty())out.detail="Missing GStreamer elements: ";else out.detail+=", ";out.detail+=name;}
    }
    out.available=out.detail.empty();if(out.available)out.detail="VP8 WebRTC available (GStreamer "+out.version+")";
    return out;
}
struct MediaSession::Impl {
    Availability availability;
    VideoSettings settings;
    int fd=-1;
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> abort{false};
    std::thread worker;
    VideoContext desired;
    std::uint64_t revision=0,nextStream=0,lastSignal=0;
    std::string fault;
    std::map<VideoTrackKey,std::shared_ptr<std::atomic<bool>>> permits;
    std::map<VideoTrackKey,std::shared_ptr<const VideoFrame>> inputs;
    std::map<VideoTrackKey,std::uint64_t> inputDropped;
    std::map<WindowKey,std::shared_ptr<const VideoFrame>> frames;
    std::map<WindowKey,VideoTrackKey> remoteGrants;
    std::map<WindowKey,VideoStreamStatus> states;
    std::map<VideoTrackKey,std::string> stats;
    std::deque<VideoSignal> incoming,outgoing;
    std::size_t incomingBytes=0,outgoingBytes=0;
    // Metadata and media notifications can be observed in different main-thread
    // turns. Hold only scoped signaling, never a pipeline, until metadata grants it.
    struct DeferredSignal { VideoSignal signal; Time deadline; };
    std::deque<DeferredSignal> deferred;
    std::size_t deferredBytes=0;
    std::map<WindowKey,std::uint64_t> receiveFloor;
    // Publication epochs are globally monotone on the owner's WindowSync. A
    // removed metadata entry must not admit an older consent on reappearance.
    // Per-current-window floors + this scalar avoid an unbounded tombstone map.
    std::uint64_t retiredPublication=0;
    std::deque<std::string> lifecycle;
    std::uint64_t lifecycleSequence=0;
    Time traceStart=Clock::now();
    void traceLocked(const VideoTrackKey& key,bool sending,std::string_view event,std::uint64_t serial=0) noexcept {
        try {
        std::ostringstream line;
        line<<"event="<<++lifecycleSequence<<" t_ms="
            <<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-traceStart).count()
            <<" dir="<<(sending?"TX":"RX")<<" session="<<idString(key.window.session)
            <<" owner="<<idString(key.window.owner)<<" world="<<idString(key.window.generation)
            <<" window="<<key.window.window<<" publication="<<key.publication<<" stream="<<key.stream
            <<" serial="<<serial<<" "<<event;
        if(lifecycle.size()==128)lifecycle.pop_front();
        lifecycle.push_back(line.str().substr(0,480));
        } catch (...) {} // Diagnostics must not make noexcept teardown terminate.
    }
    void trace(const VideoTrackKey& key,bool sending,std::string_view event,std::uint64_t serial=0) {
        std::lock_guard lock(mutex);traceLocked(key,sending,event,serial);
    }
    bool queueRoom(std::size_t bytes) const {
        return incoming.size()+deferred.size()<MaxVideoSignalQueue &&
            bytes<=MaxVideoSignalQueueBytes-incomingBytes-deferredBytes;
    }
    void discardDeferred(const WindowKey& window) {
        std::erase_if(deferred,[&](const auto& item){
            if(item.signal.key.window!=window)return false;
            deferredBytes-=item.signal.body.size();return true;
        });
    }
    // Called under mutex. Keep the original global serial admission check at
    // ingress; replaying a held signal through signal() would reject it wrongly.
    void releaseDeferred(Time now) {
        for(auto it=deferred.begin();it!=deferred.end();){
            const auto& key=it->signal.key;
            const auto grant=remoteGrants.find(key.window);
            const auto floor=receiveFloor.find(key.window);
            const bool current=desired.enabled&&scoped(key,desired)&&grant!=remoteGrants.end()&&grant->second==key;
            const bool expired=now>=it->deadline;
            const bool authorized=current&&floor!=receiveFloor.end()&&key.publication>floor->second;
            if(!current||expired||(floor!=receiveFloor.end()&&!authorized)){
                traceLocked(key,false,expired?"reject=metadata-timeout":"reject=retired-deferred-signal",it->signal.serial);
                deferredBytes-=it->signal.body.size();it=deferred.erase(it);
            }else if(authorized){
                traceLocked(key,false,"metadata-barrier=released",it->signal.serial);
                deferredBytes-=it->signal.body.size();incomingBytes+=it->signal.body.size();
                incoming.push_back(std::move(it->signal));it=deferred.erase(it);
            }else ++it;
        }
        // Only current metadata or bounded deferred offers can retain a grant.
        std::erase_if(remoteGrants,[&](const auto& item){
            if(contains(desired.receiving,item.first))return false;
            if(std::any_of(deferred.begin(),deferred.end(),[&](const auto& d){return d.signal.key.window==item.first;}))return false;
            return true;
        });
    }
    // Tracks are worker-owned. No native/compositor/GL pointer is present here.
    struct Track;
    // GObject closures and pad probes own references to this gate. Closing it
    // drains an entered callback and makes late callbacks no-ops before Track dies.
    struct CallbackState { std::mutex mutex; Track* track=nullptr; };
    using CallbackRef=std::shared_ptr<CallbackState>;
    struct Track {
        CallbackRef callbacks=std::make_shared<CallbackState>();
        gulong iceHandler=0,padHandler=0,transceiverHandler=0;
        Impl* owner=nullptr;
        VideoTrackKey key;
        std::shared_ptr<std::atomic<bool>> permit;
        bool sending=false,offered=false,reported=false,failed=false;
        std::atomic<bool> answered{false};
        std::atomic<bool> padLinked{false};
        GstElement *pipeline=nullptr,*rtc=nullptr,*src=nullptr,*sink=nullptr,*decoder=nullptr,*encoder=nullptr,*rxQueue=nullptr;
        GstPad *gate=nullptr,*rxPad=nullptr,*encodedSrc=nullptr,*depaySrc=nullptr,*decoderSrc=nullptr;
        gulong probeId=0,rxProbeId=0,depayProbeId=0,decoderProbeId=0;
        std::atomic<bool> padLinkFailed{false};
        std::atomic<bool> padLinkMissingSink{false};
        std::atomic<int> padLinkResult{-1};
        std::atomic<std::uint64_t> rtpPackets{0},rtpBytes{0},depayBuffers{0},depayBytes{0},decoderBuffers{0},decoderBytes{0};
        std::string rxCaps;
        std::atomic<bool> nackEnabled{false},nackUnsupported{false};
        unsigned targetBitrate=0;
        Time lastKeyframeRequest{},lastKeyframeAck{};
        unsigned keyframeBackoffStep=0;
        std::uint64_t keyframeRequests=0,keyframeRequestsAccepted=0,keyframeRequestsUnhandled=0;
        unsigned keyframeMaxDist=0;
        std::mutex iceMutex;
        detail::IceQueue ice{detail::MaxLocalIceCandidates};
        detail::IceQueue remoteIce{detail::MaxRemoteIceCandidates};
        std::string iceError;
        bool localDescriptionSet=false,remoteDescriptionSet=false;
        Time lastSource{};
        Time started=Clock::now(),lastFrame{},lastAck{},lastReport{},lastStats{},rateAt=Clock::now();
        std::uint64_t count=0,dropped=0,rateCount=0;
        double fps=0;
        unsigned width=0,height=0;
        int connection=0,iceState=0;
        unsigned localHostCandidates=0,localSrflxCandidates=0,localRelayCandidates=0,localPrflxCandidates=0,localOtherCandidates=0;
        unsigned remoteHostCandidates=0,remoteSrflxCandidates=0,remoteRelayCandidates=0,remotePrflxCandidates=0,remoteOtherCandidates=0;
        std::string detail;
    };
    std::map<VideoTrackKey,std::unique_ptr<Track>> tracks;
    std::string iceDiagnostics(Track& t){
        if(t.rtc)g_object_get(t.rtc,"connection-state",&t.connection,"ice-connection-state",&t.iceState,nullptr);
        int gathering=0;if(t.rtc)g_object_get(t.rtc,"ice-gathering-state",&gathering,nullptr);
        std::size_t localCount=0;
        bool localEndSeen=false,localEndDelivered=false;
        unsigned lh=0,ls=0,lr=0,lp=0,lo=0;
        {std::lock_guard lock(t.iceMutex);localCount=t.ice.candidates();localEndSeen=t.ice.endSeen();localEndDelivered=t.ice.endDelivered();
         lh=t.localHostCandidates;ls=t.localSrflxCandidates;lr=t.localRelayCandidates;lp=t.localPrflxCandidates;lo=t.localOtherCandidates;}
        std::ostringstream out;
        out<<"connection="<<connectionStateName(t.connection)<<" ice="<<iceStateName(t.iceState)
           <<" gathering="<<gatheringStateName(gathering)
           <<" local_sdp="<<t.localDescriptionSet<<" remote_sdp="<<t.remoteDescriptionSet
           <<" local_candidates="<<localCount<<"(host="<<lh<<",srflx="<<ls<<",relay="<<lr<<",prflx="<<lp<<",other="<<lo<<")"
           <<" remote_candidates="<<t.remoteIce.candidates()<<"(host="<<t.remoteHostCandidates<<",srflx="<<t.remoteSrflxCandidates
           <<",relay="<<t.remoteRelayCandidates<<",prflx="<<t.remotePrflxCandidates<<",other="<<t.remoteOtherCandidates<<")"
           <<" local_eoc="<<(localEndDelivered?"sent":localEndSeen?"queued":"no")
           <<" remote_eoc="<<(t.remoteIce.endDelivered()?"applied":t.remoteIce.endSeen()?"queued":"no")
           <<" offered="<<t.offered<<" answered="<<t.answered
           <<" rtp_packets="<<t.rtpPackets.load(std::memory_order_relaxed)
           <<" rtp_bytes="<<t.rtpBytes.load(std::memory_order_relaxed)
           <<" pad_link="<<(t.sending?"tx":t.padLinked.load()?"linked":t.padLinkFailed.load()?"failed":"pending")
           <<" counter_point="<<(t.sending?"webrtc-sink-before-transport":"webrtc-src-pad")
           <<" keyframe_requests="<<t.keyframeRequests
           <<"(handled="<<t.keyframeRequestsAccepted<<",unhandled="<<t.keyframeRequestsUnhandled<<")"
           <<" keyframe_max_dist="<<t.keyframeMaxDist
           <<" rtcp_nack="<<(t.nackEnabled.load(std::memory_order_relaxed)?"enabled":t.nackUnsupported.load(std::memory_order_relaxed)?"unsupported":"pending");
        return out.str();
    }
    explicit Impl(VideoSettings s):availability(MediaSession::probe()),settings(s){
        if(!validVideoSettings(s))throw std::invalid_argument("invalid video limits");
        if(!availability.available)return;
        fd=eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK);if(fd<0)throw std::runtime_error("media eventfd failed");
        try{
            worker=std::thread([this]{run();});
        }catch(...){
            abort=true;
            cv.notify_one();
            if(worker.joinable())worker.join();
            ::close(fd);fd=-1;throw;
        }
    }
    ~Impl(){shutdown();}
    void notify(){wake(fd);}
    void setFault(std::string reason){
        {std::lock_guard lock(mutex);if(fault==reason&&!desired.enabled)return;fault=std::move(reason);desired.enabled=false;desired.sending.clear();desired.receiving.clear();
         for(auto& [k,p]:permits){(void)k;p->store(false);}inputs.clear();inputDropped.clear();frames.clear();remoteGrants.clear();deferred.clear();deferredBytes=0;receiveFloor.clear();++revision;}
        cv.notify_one();notify();
    }
    void emit(Track& t,VideoSignalKind kind,std::string body){
        if(body.size()>MaxVideoSignal)return;
        bool overflow=false;
        {std::lock_guard lock(mutex);
         if(!sameWorld(desired,VideoContext{t.key.window.session,desired.local,desired.peer,t.key.window.generation,false,{},{}}))return;
         if(kind!=VideoSignalKind::Stop&&kind!=VideoSignalKind::Failure&&!t.permit->load())return;
         overflow=outgoing.size()>=MaxVideoSignalQueue||body.size()>MaxVideoSignalQueueBytes-outgoingBytes;
         if(!overflow){VideoSignal s;s.key=t.key;s.sender=desired.local;s.kind=kind;s.body=std::move(body);outgoingBytes+=s.body.size();outgoing.push_back(std::move(s));
             if(kind==VideoSignalKind::Offer||kind==VideoSignalKind::Answer||kind==VideoSignalKind::Stop||kind==VideoSignalKind::Failure)
                 traceLocked(t.key,t.sending,"signal-queued kind="+std::to_string(static_cast<unsigned>(kind)));}}
        if(overflow)setFault("Media signaling output queue full; reconnect video");else notify();
    }
    static void releaseSignalData(gpointer data,GClosure*) noexcept {delete static_cast<CallbackRef*>(data);}
    static void releaseProbeData(gpointer data) noexcept {delete static_cast<CallbackRef*>(data);}
    static void iceCandidate(GstElement*,guint mline,gchar* candidate,gpointer data) noexcept {
        const auto state=*static_cast<CallbackRef*>(data);
        std::lock_guard guard(state->mutex);
        if(!state->track)return;
        auto& t=*state->track;
        if(!t.permit->load())return;
        try{
            // GStreamer 1.28 emits "" when gathering completes. NULL has the same
            // documented meaning on add-ice-candidate. Preserve it as wire "0|".
            if(mline>0)throw std::runtime_error("local ICE m-line must be video (0)");
            const std::string prefix=std::to_string(mline)+"|";
            const std::size_t n=candidate?::strnlen(candidate,detail::MaxIceBody):0;
            if(n>detail::MaxIceBody-prefix.size())throw std::runtime_error("local ICE message exceeds 2048 bytes");
            const std::string value(candidate?candidate:"",n);
            {std::lock_guard lock(t.iceMutex);
             if(t.ice.push(prefix+value)&&!value.empty())countCandidateType(value,t.localHostCandidates,t.localSrflxCandidates,
                 t.localRelayCandidates,t.localPrflxCandidates,t.localOtherCandidates);}
            t.owner->cv.notify_one();
        }catch(const std::exception& e){
            // Report on the worker and isolate this Track; do not throw through C.
            try{std::lock_guard lock(t.iceMutex);if(t.iceError.empty())t.iceError=e.what();}
            catch(...){t.permit->store(false);}
            t.owner->cv.notify_one();
        }catch(...){t.permit->store(false);}
    }
    static GstPadProbeReturn packetGate(GstPad*,GstPadProbeInfo* info,gpointer data) noexcept {
        const auto state=*static_cast<CallbackRef*>(data);
        std::lock_guard guard(state->mutex);
        auto* track=state->track;
        if(!track||!track->permit->load(std::memory_order_acquire))return GST_PAD_PROBE_DROP;
        const auto type=GST_PAD_PROBE_INFO_TYPE(info);
        if(type&GST_PAD_PROBE_TYPE_BUFFER){
            if(auto* buffer=GST_PAD_PROBE_INFO_BUFFER(info)){
                track->rtpPackets.fetch_add(1,std::memory_order_relaxed);
                track->rtpBytes.fetch_add(gst_buffer_get_size(buffer),std::memory_order_relaxed);
            }
        }else if(type&GST_PAD_PROBE_TYPE_BUFFER_LIST){
            if(auto* list=GST_PAD_PROBE_INFO_BUFFER_LIST(info)){
                const auto n=gst_buffer_list_length(list);
                std::uint64_t bytes=0;
                for(guint i=0;i<n;++i)if(auto* buffer=gst_buffer_list_get(list,i))bytes+=gst_buffer_get_size(buffer);
                track->rtpPackets.fetch_add(n,std::memory_order_relaxed);
                track->rtpBytes.fetch_add(bytes,std::memory_order_relaxed);
            }
        }
        return GST_PAD_PROBE_OK;
    }
    // `pad-added` can precede negotiation. Query caps describe what a pad could
    // carry, not what it actually carries; routing from the first query-caps
    // structure or from src_0/src_1 can silently connect audio RTP to the VP8
    // depayloader (or vice versa). Route only from fixed, current/CAPS-event data.
    static std::string rtpMediaFromCaps(const GstCaps* caps, std::string& encoding,
                                        int& payload, int& clockRate) {
        if(!caps||gst_caps_is_empty(caps)||gst_caps_is_any(caps)||!gst_caps_is_fixed(caps))return {};
        const auto* structure=gst_caps_get_structure(caps,0);
        if(!structure||!gst_structure_has_name(structure,"application/x-rtp"))return {};
        const auto* media=gst_structure_get_string(structure,"media");
        const auto* codec=gst_structure_get_string(structure,"encoding-name");
        if(!media||!codec)return {};
        encoding=codec;
        (void)gst_structure_get_int(structure,"payload",&payload);
        (void)gst_structure_get_int(structure,"clock-rate",&clockRate);
        return lowerAscii(std::string(media));
    }
    static bool linkIncomingRtpPad(Track& t,GstPad* pad,const GstCaps* caps) noexcept {
        try {
        std::string encoding;int payload=-1,clockRate=0;
        const auto media=rtpMediaFromCaps(caps,encoding,payload,clockRate);
        if(media.empty())return false; // Not negotiated yet; keep waiting for CAPS.
        const auto codec=lowerAscii(encoding);
        if(media!="video")return true; // Ignore non-video media sections.
        if(media=="video"){
            if(codec!="vp8"||clockRate!=90000){
                t.padLinkResult.store(static_cast<int>(GST_PAD_LINK_NOFORMAT),std::memory_order_release);
                t.padLinkFailed.store(true,std::memory_order_release);
                return true;
            }
            if(t.padLinked.load(std::memory_order_acquire))return true;
            auto* target=t.decoder?gst_element_get_static_pad(t.decoder,"sink"):nullptr;
            if(!target){t.padLinkMissingSink.store(true,std::memory_order_release);t.padLinkFailed.store(true,std::memory_order_release);return true;}
            const auto result=gst_pad_link(pad,target);gst_object_unref(target);
            t.padLinkResult.store(static_cast<int>(result),std::memory_order_release);
            if(result!=GST_PAD_LINK_OK){t.padLinkFailed.store(true,std::memory_order_release);return true;}
            t.rxPad=GST_PAD(gst_object_ref(pad));
            t.rxProbeId=gst_pad_add_probe(t.rxPad,MediaBufferProbe,packetGate,new CallbackRef(t.callbacks),releaseProbeData);
            t.rxCaps="video/"+encoding+"/pt="+std::to_string(payload)+"/rate="+std::to_string(clockRate);
            t.padLinked.store(true,std::memory_order_release);
            return true;
        }
        return true;
        } catch (...) {
            // Never let a C++ exception unwind through a GStreamer callback.
            t.padLinkResult.store(static_cast<int>(GST_PAD_LINK_REFUSED),std::memory_order_release);
            t.padLinkFailed.store(true,std::memory_order_release);
            return true;
        }
    }
    static GstPadProbeReturn incomingCapsProbe(GstPad* pad,GstPadProbeInfo* info,gpointer data) noexcept {
        if(!(GST_PAD_PROBE_INFO_TYPE(info)&GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM))return GST_PAD_PROBE_OK;
        auto* event=GST_PAD_PROBE_INFO_EVENT(info);
        if(!event||GST_EVENT_TYPE(event)!=GST_EVENT_CAPS)return GST_PAD_PROBE_OK;
        GstCaps* caps=nullptr;gst_event_parse_caps(event,&caps);
        const auto state=*static_cast<CallbackRef*>(data);
        try {
            std::lock_guard guard(state->mutex);
            if(!state->track||state->track->sending||!state->track->permit->load(std::memory_order_acquire))
                return GST_PAD_PROBE_REMOVE;
            // CAPS is the authoritative negotiation result. Link before allowing this
            // sticky event downstream so the depayloader receives caps from frame 1.
            return linkIncomingRtpPad(*state->track,pad,caps)?GST_PAD_PROBE_REMOVE:GST_PAD_PROBE_OK;
        } catch (...) { return GST_PAD_PROBE_REMOVE; }
    }
    static GstPadProbeReturn depayProbe(GstPad*,GstPadProbeInfo* info,gpointer data) noexcept {
        const auto state=*static_cast<CallbackRef*>(data);
        std::lock_guard guard(state->mutex);
        auto* track=state->track;
        if(!track||!track->permit->load(std::memory_order_acquire))return GST_PAD_PROBE_DROP;
        const auto type=GST_PAD_PROBE_INFO_TYPE(info);
        if(type&GST_PAD_PROBE_TYPE_BUFFER){
            if(auto* buffer=GST_PAD_PROBE_INFO_BUFFER(info)){track->depayBuffers.fetch_add(1,std::memory_order_relaxed);track->depayBytes.fetch_add(gst_buffer_get_size(buffer),std::memory_order_relaxed);}
        }else if(type&GST_PAD_PROBE_TYPE_BUFFER_LIST){
            if(auto* list=GST_PAD_PROBE_INFO_BUFFER_LIST(info)){const auto n=gst_buffer_list_length(list);std::uint64_t bytes=0;for(guint i=0;i<n;++i)if(auto* buffer=gst_buffer_list_get(list,i))bytes+=gst_buffer_get_size(buffer);track->depayBuffers.fetch_add(n,std::memory_order_relaxed);track->depayBytes.fetch_add(bytes,std::memory_order_relaxed);}
        }
        return GST_PAD_PROBE_OK;
    }
    static GstPadProbeReturn decoderProbe(GstPad*,GstPadProbeInfo* info,gpointer data) noexcept {
        const auto state=*static_cast<CallbackRef*>(data);
        std::lock_guard guard(state->mutex);
        auto* track=state->track;
        if(!track||!track->permit->load(std::memory_order_acquire))return GST_PAD_PROBE_DROP;
        const auto type=GST_PAD_PROBE_INFO_TYPE(info);
        if(type&GST_PAD_PROBE_TYPE_BUFFER){
            if(auto* buffer=GST_PAD_PROBE_INFO_BUFFER(info)){track->decoderBuffers.fetch_add(1,std::memory_order_relaxed);track->decoderBytes.fetch_add(gst_buffer_get_size(buffer),std::memory_order_relaxed);}
        }else if(type&GST_PAD_PROBE_TYPE_BUFFER_LIST){
            if(auto* list=GST_PAD_PROBE_INFO_BUFFER_LIST(info)){const auto n=gst_buffer_list_length(list);std::uint64_t bytes=0;for(guint i=0;i<n;++i)if(auto* buffer=gst_buffer_list_get(list,i))bytes+=gst_buffer_get_size(buffer);track->decoderBuffers.fetch_add(n,std::memory_order_relaxed);track->decoderBytes.fetch_add(bytes,std::memory_order_relaxed);}
        }
        return GST_PAD_PROBE_OK;
    }
    static void incomingPad(GstElement*,GstPad* pad,gpointer data) noexcept {
        const auto state=*static_cast<CallbackRef*>(data);
        try {
            std::lock_guard guard(state->mutex);
            if(!state->track)return;
            auto& t=*state->track;
            if(t.sending||!t.permit->load()||gst_pad_get_direction(pad)!=GST_PAD_SRC)return;
            GstCaps* caps=gst_pad_get_current_caps(pad);
            const bool routed=linkIncomingRtpPad(t,pad,caps);
            if(caps)gst_caps_unref(caps);
            if(routed)return;
            // No negotiated caps yet: do not guess from query_caps() or pad index.
            // Wait for the actual CAPS event before assigning this RTP stream.
            auto* probeData=new CallbackRef(t.callbacks);
            const auto id=gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                incomingCapsProbe,probeData,releaseProbeData);
            if(id==0){delete probeData;t.padLinkResult.store(static_cast<int>(GST_PAD_LINK_REFUSED),std::memory_order_release);t.padLinkFailed.store(true,std::memory_order_release);}
        } catch (...) {
            // C callbacks must not allow exceptions to cross the ABI boundary.
            std::lock_guard guard(state->mutex);
            if(state->track){state->track->padLinkResult.store(static_cast<int>(GST_PAD_LINK_REFUSED),std::memory_order_release);state->track->padLinkFailed.store(true,std::memory_order_release);}
        }
    }
    static bool setTransceiverNack(GstWebRTCRTPTransceiver* transceiver) noexcept {
        if (!transceiver) return false;
        auto* property = g_object_class_find_property(G_OBJECT_GET_CLASS(transceiver), "do-nack");
        if (!property || !(property->flags & G_PARAM_WRITABLE)) return false;
        g_object_set(transceiver, "do-nack", TRUE, nullptr);
        gboolean enabled = FALSE;
        g_object_get(transceiver, "do-nack", &enabled, nullptr);
        return enabled == TRUE;
    }
    static void onNewTransceiver(GstElement*, GstWebRTCRTPTransceiver* transceiver, gpointer data) noexcept {
        const auto state = *static_cast<CallbackRef*>(data);
        std::lock_guard guard(state->mutex);
        if (!state->track || !state->track->permit->load(std::memory_order_acquire)) return;
        auto& track = *state->track;
        if (setTransceiverNack(transceiver)) track.nackEnabled.store(true, std::memory_order_release);
        else track.nackUnsupported.store(true, std::memory_order_release);
    }
    void applyNack(Track& t, GstWebRTCRTPTransceiver* transceiver) noexcept {
        if (setTransceiverNack(transceiver)) t.nackEnabled.store(true, std::memory_order_release);
        else t.nackUnsupported.store(true, std::memory_order_release);
    }
    // All promises are local GStreamer operations, not waits for remote packets. A
    // five-second bound and interruptible polling prevent a stuck negotiation from
    // holding shutdown. Callback user data is owned by the GstPromise itself.
    GstPromise* promise(Track& t,const std::function<void(GstPromise*)>& operation,std::chrono::milliseconds limit=5000ms){
        auto* ready=new PromiseReady;
        auto* p=gst_promise_new_with_change_func([](GstPromise*,gpointer data){static_cast<PromiseReady*>(data)->done.store(true,std::memory_order_release);},ready,
            [](gpointer data){delete static_cast<PromiseReady*>(data);});
        operation(p);const auto until=Clock::now()+limit;
        while(!ready->done.load(std::memory_order_acquire)&&!abort.load()&&t.permit->load()&&Clock::now()<until)std::this_thread::sleep_for(1ms);
        if(!ready->done.load(std::memory_order_acquire))gst_promise_interrupt(p);
        if(gst_promise_wait(p)!=GST_PROMISE_RESULT_REPLIED){gst_promise_unref(p);throw std::runtime_error("GStreamer promise interrupted/timeout");}
        const auto* reply=gst_promise_get_reply(p);
        if(reply&&gst_structure_has_field(reply,"error")){
            GError* error=nullptr;gst_structure_get(reply,"error",G_TYPE_ERROR,&error,nullptr);const auto why=errorText(error);gst_promise_unref(p);throw std::runtime_error(why);
        }
        return p;
    }
    void flushRemoteIce(Track& t){
        if(!t.localDescriptionSet||!t.remoteDescriptionSet||!t.permit->load())return;
        t.remoteIce.ready();
        while(t.permit->load()){
            auto candidate=t.remoteIce.pop();if(!candidate)break;
            // Documented since before our minimum 1.22: "" marks end-of-candidates.
            // Do not pass "0|", "candidate:" or an SDP attribute as the end marker.
            g_signal_emit_by_name(t.rtc,"add-ice-candidate",candidate->mline,candidate->candidate.c_str());
        }
    }
    void setDescription(Track& t,GstWebRTCSessionDescription* d,bool local){
        auto* p=promise(t,[&](auto* p){g_signal_emit_by_name(t.rtc,local?"set-local-description":"set-remote-description",d,p);});gst_promise_unref(p);
        if(local)t.localDescriptionSet=true;else t.remoteDescriptionSet=true;
        trace(t.key,t.sending,local?"set-local-description=success":"set-remote-description=success");
        flushRemoteIce(t);
    }
    void createDescription(Track& t,bool offer){
        auto* p=promise(t,[&](auto* p){g_signal_emit_by_name(t.rtc,offer?"create-offer":"create-answer",nullptr,p);});
        GstWebRTCSessionDescription* d=nullptr;
        const auto* reply=gst_promise_get_reply(p);
        if(reply)gst_structure_get(reply,offer?"offer":"answer",GST_TYPE_WEBRTC_SESSION_DESCRIPTION,&d,nullptr);
        gst_promise_unref(p);if(!d)throw std::runtime_error("missing WebRTC session description");
        trace(t.key,t.sending,offer?"offer-created":"answer-created");
        try{
            setDescription(t,d,true);auto* text=gst_sdp_message_as_text(d->sdp);
            if(!text)throw std::runtime_error("SDP serialization failed");
            std::string s(text);g_free(text);
            if(s.size()>MaxVideoSignal)throw std::runtime_error("SDP exceeds 48 KiB limit");
            emit(t,offer?VideoSignalKind::Offer:VideoSignalKind::Answer,std::move(s));
            // SDP must be queued before its local trickle candidates/end marker.
            {std::lock_guard lock(t.iceMutex);t.ice.ready();}
        }catch(...){gst_webrtc_session_description_free(d);throw;}
        gst_webrtc_session_description_free(d);
    }
    static std::string lowerAscii(std::string value){
        std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        return value;
    }
    static bool mediaHasAttribute(const GstSDPMedia* media,const char* key){
        if(!media||!key)return false;
        const auto wanted=lowerAscii(key);
        for(guint i=0;i<gst_sdp_media_attributes_len(media);++i){
            const auto* attribute=gst_sdp_media_get_attribute(media,i);
            if(attribute&&attribute->key&&lowerAscii(attribute->key)==wanted)return true;
        }
        return false;
    }
    // RTP payload numbers are negotiated identifiers, not codec identities.
    // Match rtpmap entries instead of assuming the dynamic payload number is always 96.
    static bool mediaHasRtpCodec(const GstSDPMedia* media,std::string_view codec){
        if(!media)return false;
        for(guint f=0;f<gst_sdp_media_formats_len(media);++f){
            const auto* format=gst_sdp_media_get_format(media,f);
            if(!format)continue;
            for(guint i=0;i<gst_sdp_media_attributes_len(media);++i){
                const auto* attribute=gst_sdp_media_get_attribute(media,i);
                if(!attribute||!attribute->key||!attribute->value||lowerAscii(attribute->key)!="rtpmap")continue;
                const std::string_view mapping(attribute->value);
                const auto separator=mapping.find_first_of(" \t");
                if(separator==std::string_view::npos||mapping.substr(0,separator)!=format)continue;
                auto begin=separator;
                while(begin<mapping.size()&&(mapping[begin]==' '||mapping[begin]=='\t'))++begin;
                const auto encoding=lowerAscii(std::string(mapping.substr(begin)));
                if(codec=="vp8"&&encoding=="vp8/90000")return true;
            }
        }
        return false;
    }
    static GstWebRTCSessionDescription* parseDescription(const VideoSignal& s){
        GstSDPMessage* msg=nullptr;
        if(gst_sdp_message_new(&msg)!=GST_SDP_OK)throw std::runtime_error("SDP allocation failed");
        auto fail=[&](std::string_view reason){
            std::ostringstream detail;detail<<"Invalid WebRTC SDP: "<<reason<<"; m-lines="<<gst_sdp_message_medias_len(msg);
            for(guint i=0;i<gst_sdp_message_medias_len(msg);++i){
                const auto* media=gst_sdp_message_get_media(msg,i);
                if(!media)continue;
                const auto* kind=gst_sdp_media_get_media(media);const auto* proto=gst_sdp_media_get_proto(media);
                detail<<" ["<<i<<":"<<(kind?kind:"?")<<"/"<<(proto?proto:"?")<<" formats=";
                for(guint f=0;f<gst_sdp_media_formats_len(media);++f){if(f)detail<<',';const auto* format=gst_sdp_media_get_format(media,f);detail<<(format?format:"?");}
                detail<<" rtpmap=";
                bool firstMap=true;
                for(guint aidx=0;aidx<gst_sdp_media_attributes_len(media);++aidx){
                    const auto* attribute=gst_sdp_media_get_attribute(media,aidx);
                    if(!attribute||!attribute->key||!attribute->value||lowerAscii(attribute->key)!="rtpmap")continue;
                    if(!firstMap)detail<<',';
                    detail<<attribute->value;firstMap=false;
                    if(detail.tellp()>std::streampos(1500))break; // diagnostics remain bounded
                }
                detail<<']';
            }
            gst_sdp_message_free(msg);throw std::runtime_error(detail.str());
        };
        if(gst_sdp_message_parse_buffer(reinterpret_cast<const guint8*>(s.body.data()),static_cast<guint>(s.body.size()),msg)!=GST_SDP_OK)fail("parse failed");
        const auto count=gst_sdp_message_medias_len(msg);
        if(count!=1)fail("expected exactly one video m-line");
        const auto* video=gst_sdp_message_get_media(msg,0);
        const auto* videoKind=video?gst_sdp_media_get_media(video):nullptr;
        const auto* videoProto=video?gst_sdp_media_get_proto(video):nullptr;
        if(!videoKind||lowerAscii(videoKind)!="video"||!videoProto||lowerAscii(videoProto)!="udp/tls/rtp/savpf")
            fail("m-line 0 must be DTLS-SRTP video");
        if(!mediaHasRtpCodec(video,"vp8"))fail("m-line 0 has no offered payload mapped to VP8/90000");
        return gst_webrtc_session_description_new(s.kind==VideoSignalKind::Offer?GST_WEBRTC_SDP_TYPE_OFFER:GST_WEBRTC_SDP_TYPE_ANSWER,msg);
    }
    std::unique_ptr<Track> create(const VideoTrackKey& key,bool sending,unsigned width,unsigned height){
        auto t=std::make_unique<Track>();t->owner=this;t->key=key;t->sending=sending;t->width=width;t->height=height;
        t->targetBitrate=sending?targetVideoBitrate(settings,width,height):0;
        t->callbacks->track=t.get();t->permit=std::make_shared<std::atomic<bool>>(true);
        {std::lock_guard lock(mutex);
         bool allowed=desired.enabled&&fault.empty()&&scoped(key,desired);
         if(sending)allowed=allowed&&std::any_of(desired.sending.begin(),desired.sending.begin()+std::min(desired.sending.size(),std::size_t(settings.streams)),[&](const auto& x){return x.window==key.window&&x.publication==key.publication;});
         else {const auto grant=remoteGrants.find(key.window);
             allowed=allowed&&contains(desired.receiving,key.window)&&grant!=remoteGrants.end()&&grant->second==key;}
         if(!allowed)throw std::runtime_error("publication revoked before pipeline creation");
         permits[key]=t->permit;}
        try{
            GError* error=nullptr;
            t->pipeline=gst_pipeline_new(nullptr);
            auto* rtc=gst_element_factory_make("webrtcbin","rtc");
            if(!t->pipeline||!rtc){if(rtc)gst_object_unref(rtc);throw std::runtime_error("pipeline/webrtcbin allocation failed");}
            if(!gst_bin_add(GST_BIN(t->pipeline),rtc)){gst_object_unref(rtc);throw std::runtime_error("cannot add webrtcbin");}
            t->rtc=gst_bin_get_by_name(GST_BIN(t->pipeline),"rtc");
            configureIceServers(t->rtc);
            g_object_set(t->rtc,"bundle-policy",GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE,"latency",100U,nullptr);
            if(sending){
                auto* encoderBin=gst_parse_bin_from_description(senderPipeline(settings,width,height).c_str(),TRUE,&error);
                if(error||!encoderBin){if(encoderBin)gst_object_unref(encoderBin);throw std::runtime_error(errorText(error));}
                t->encoder=gst_bin_get_by_name(GST_BIN(encoderBin),"encoder");
                auto* output=gst_element_get_static_pad(encoderBin,"src");
                if(!t->encoder||!output){
                    if(output)gst_object_unref(output);
                    if(t->encoder){gst_object_unref(t->encoder);t->encoder=nullptr;}
                    gst_object_unref(encoderBin);
                    throw std::runtime_error("missing VP8 encoder or encoded output pad for keyframe recovery");
                }
                t->encodedSrc=GST_PAD(gst_object_ref(output));
                t->keyframeMaxDist=std::max(1U,settings.fps?settings.fps:60U);
                if(!gst_bin_add(GST_BIN(t->pipeline),encoderBin)){
                    gst_object_unref(output);gst_object_unref(t->encodedSrc);t->encodedSrc=nullptr;
                    gst_object_unref(t->encoder);t->encoder=nullptr;gst_object_unref(encoderBin);
                    throw std::runtime_error("cannot add encoder bin");
                }
                t->gate=gst_element_request_pad_simple(t->rtc,"sink_%u");
                const bool linked=t->gate&&gst_pad_link(output,t->gate)==GST_PAD_LINK_OK;
                gst_object_unref(output);
                if(!linked)throw std::runtime_error("cannot link encoded VP8 track");
            }
            t->iceHandler=g_signal_connect_data(t->rtc,"on-ice-candidate",G_CALLBACK(iceCandidate),new CallbackRef(t->callbacks),releaseSignalData,static_cast<GConnectFlags>(0));
            t->padHandler=g_signal_connect_data(t->rtc,"pad-added",G_CALLBACK(incomingPad),new CallbackRef(t->callbacks),releaseSignalData,static_cast<GConnectFlags>(0));
            if(g_signal_lookup("on-new-transceiver",G_OBJECT_TYPE(t->rtc)))
                t->transceiverHandler=g_signal_connect_data(t->rtc,"on-new-transceiver",G_CALLBACK(onNewTransceiver),new CallbackRef(t->callbacks),releaseSignalData,static_cast<GConnectFlags>(0));
            if(sending){
                t->src=gst_bin_get_by_name(GST_BIN(t->pipeline),"source");
                auto* caps=gst_caps_new_simple("video/x-raw","format",G_TYPE_STRING,"RGBA","width",G_TYPE_INT,static_cast<int>(width),"height",G_TYPE_INT,static_cast<int>(height),"framerate",GST_TYPE_FRACTION,static_cast<int>(settings.fps),1,nullptr);
                gst_app_src_set_caps(GST_APP_SRC(t->src),caps);gst_caps_unref(caps);
                g_object_set(t->src,"max-bytes",static_cast<guint64>(std::size_t(width)*height*4),nullptr);
                if(!t->gate)throw std::runtime_error("missing WebRTC outgoing track");
                GstWebRTCRTPTransceiver* transceiver=nullptr;
                g_object_get(t->gate,"transceiver",&transceiver,nullptr);
                if(!transceiver)throw std::runtime_error("missing video transceiver");
                g_object_set(transceiver,"direction",GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY,nullptr);
                applyNack(*t,transceiver);
                gst_object_unref(transceiver);

                t->probeId=gst_pad_add_probe(t->gate,MediaBufferProbe,packetGate,new CallbackRef(t->callbacks),releaseProbeData);
            }else{
                t->decoder=gst_parse_bin_from_description(receiverPipeline(),TRUE,&error);
                if(error||!t->decoder){if(t->decoder)gst_object_unref(t->decoder);t->decoder=nullptr;throw std::runtime_error(errorText(error));}
                if(!gst_bin_add(GST_BIN(t->pipeline),t->decoder)){gst_object_unref(t->decoder);t->decoder=nullptr;throw std::runtime_error("cannot add decoder bin");}
                t->sink=gst_bin_get_by_name(GST_BIN(t->decoder),"decoded");if(!t->sink)throw std::runtime_error("missing decoded appsink");
                t->rxQueue=gst_bin_get_by_name(GST_BIN(t->decoder),"rx_rtp_queue");
                if(!t->rxQueue)throw std::runtime_error("missing incoming RTP queue");
                auto* depay=gst_bin_get_by_name(GST_BIN(t->decoder),"vp8_depay");
                if(!depay)throw std::runtime_error("missing VP8 depayloader");
                t->depaySrc=gst_element_get_static_pad(depay,"src");gst_object_unref(depay);
                if(!t->depaySrc)throw std::runtime_error("missing VP8 depayloader output pad");
                t->depayProbeId=gst_pad_add_probe(t->depaySrc,MediaBufferProbe,depayProbe,new CallbackRef(t->callbacks),releaseProbeData);
                auto* vp8Decoder=gst_bin_get_by_name(GST_BIN(t->decoder),"vp8_decoder");
                if(!vp8Decoder)throw std::runtime_error("missing VP8 decoder");
                t->decoderSrc=gst_element_get_static_pad(vp8Decoder,"src");gst_object_unref(vp8Decoder);
                if(!t->decoderSrc)throw std::runtime_error("missing VP8 decoder output pad");
                t->decoderProbeId=gst_pad_add_probe(t->decoderSrc,MediaBufferProbe,decoderProbe,new CallbackRef(t->callbacks),releaseProbeData);
            }
            if(gst_element_set_state(t->pipeline,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE)throw std::runtime_error("GStreamer cannot start pipeline (check libnice/DTLS/VP8)");
            trace(key,sending,"slot=acquired pipeline=play-requested");
            // Wait for actual appsrc/RTP caps before offering. No synthetic pre-roll.
            return t;
        }catch(...){destroy(*t);throw;}
    }
    void destroy(Track& t) noexcept {
        t.permit->store(false,std::memory_order_release);
        {std::lock_guard guard(t.callbacks->mutex);t.callbacks->track=nullptr;}
        if(t.rtc&&t.iceHandler)g_signal_handler_disconnect(t.rtc,t.iceHandler);
        if(t.rtc&&t.padHandler)g_signal_handler_disconnect(t.rtc,t.padHandler);
        if(t.rtc&&t.transceiverHandler)g_signal_handler_disconnect(t.rtc,t.transceiverHandler);
        t.iceHandler=t.padHandler=t.transceiverHandler=0;
        {std::lock_guard lock(t.iceMutex);t.ice.stop();t.iceError.clear();}
        t.remoteIce.stop();t.localDescriptionSet=t.remoteDescriptionSet=false;
        // Pipeline NULL stops/join streaming tasks before freeing callback user data.
        if(t.pipeline)gst_element_set_state(t.pipeline,GST_STATE_NULL);
        if(t.gate){if(t.probeId)gst_pad_remove_probe(t.gate,t.probeId);if(t.rtc)gst_element_release_request_pad(t.rtc,t.gate);gst_object_unref(t.gate);t.gate=nullptr;}
        if(t.rxPad){if(t.rxProbeId)gst_pad_remove_probe(t.rxPad,t.rxProbeId);gst_object_unref(t.rxPad);t.rxPad=nullptr;}
        if(t.depaySrc){if(t.depayProbeId)gst_pad_remove_probe(t.depaySrc,t.depayProbeId);gst_object_unref(t.depaySrc);t.depaySrc=nullptr;}
        if(t.decoderSrc){if(t.decoderProbeId)gst_pad_remove_probe(t.decoderSrc,t.decoderProbeId);gst_object_unref(t.decoderSrc);t.decoderSrc=nullptr;}
        if(t.rxQueue){gst_object_unref(t.rxQueue);t.rxQueue=nullptr;}
        if(t.encodedSrc){gst_object_unref(t.encodedSrc);t.encodedSrc=nullptr;}
        if(t.src)gst_object_unref(t.src);
        if(t.sink)gst_object_unref(t.sink);
        if(t.encoder)gst_object_unref(t.encoder);
        if(t.rtc)gst_object_unref(t.rtc);
        if(t.pipeline)gst_object_unref(t.pipeline);
        t.src=t.sink=t.encoder=t.rtc=t.pipeline=t.decoder=nullptr;
        std::lock_guard lock(mutex);permits.erase(t.key);inputs.erase(t.key);inputDropped.erase(t.key);stats.erase(t.key);
        if(auto i=frames.find(t.key.window);i!=frames.end()&&i->second->key==t.key)frames.erase(i);
        traceLocked(t.key,t.sending,"slot=released callbacks=closed pipeline=null");
    }
    void retireClosed(const VideoContext& plan) {
        // Main-thread revocation may arrive after the worker's iteration snapshot.
        // Teardown closed connections before admitting a replacement, not a tick
        // later: even transient allocations stay within the two-slot budget.
        for(auto it=tracks.begin();it!=tracks.end();){
            auto& t=*it->second;
            if(!t.permit->load()||!plan.enabled||!scoped(t.key,plan)){
                if(t.sending&&!t.failed)emit(t,VideoSignalKind::Stop,"Publication stopped");
                destroy(t);it=tracks.erase(it);notify();
            }else ++it;
        }
    }
    void trackError(Track& t,std::string reason){
        const auto ice=iceDiagnostics(t);
        const auto detail=reason+" ["+ice+"]";
        trace(t.key,t.sending,"track-error "+cleanWindowText(detail,180));
        t.detail=detail;t.failed=true;
        emit(t,VideoSignalKind::Failure,cleanWindowText(detail,240));
        t.permit->store(false);
        {std::lock_guard lock(mutex);VideoStreamStatus s;s.key=t.key;s.sending=t.sending;s.state="Error";s.detail=detail;s.ice=ice;states[t.key.window]=std::move(s);frames.erase(t.key.window);}
        notify();
    }
    void onSignal(const VideoSignal& s){
        VideoContext c;{std::lock_guard lock(mutex);c=desired;}
        if(!c.enabled||!scoped(s.key,c)||s.sender!=c.peer)return;
        auto it=tracks.find(s.key);
        if(s.kind!=VideoSignalKind::Ice&&s.kind!=VideoSignalKind::Decoded)
            trace(s.key,s.key.window.owner==c.local,"signal-worker kind="+std::to_string(static_cast<unsigned>(s.kind)),s.serial);
        if(s.kind==VideoSignalKind::Offer){
            if(s.key.window.owner!=c.peer||!contains(c.receiving,s.key.window))return;
            {std::lock_guard lock(mutex);const auto grant=remoteGrants.find(s.key.window);if(grant==remoteGrants.end()||grant->second!=s.key)return;}
            retireClosed(c);it=tracks.find(s.key);
            if(it!=tracks.end())return; // immutable transceiver; a restart gets a new stream ID
            std::size_t receiving=0;for(const auto& [k,t]:tracks){(void)k;if(!t->sending&&t->permit->load())++receiving;}
            if(receiving>=settings.streams){
                Track rejected;rejected.key=s.key;rejected.permit=std::make_shared<std::atomic<bool>>(false);
                emit(rejected,VideoSignalKind::Failure,"Receiver video stream limit reached");
                std::lock_guard lock(mutex);auto& state=states[s.key.window];state.key=s.key;state.state="Unavailable";state.detail="Receiver video stream limit reached";notify();return;
            }
            auto* d=parseDescription(s);
            std::unique_ptr<Track> t;
            try{
                t=create(s.key,false,0,0);
                setDescription(*t,d,false);gst_webrtc_session_description_free(d);d=nullptr;
                createDescription(*t,false);t->answered=true;
            }catch(...){if(d)gst_webrtc_session_description_free(d);if(t)destroy(*t);throw;}
            tracks.emplace(s.key,std::move(t));return;
        }
        if(it==tracks.end()){
            if((s.kind==VideoSignalKind::Failure||s.kind==VideoSignalKind::Stop)&&s.key.window.owner==c.peer&&contains(c.receiving,s.key.window)){
                std::lock_guard lock(mutex);const auto grant=remoteGrants.find(s.key.window);
                if(grant==remoteGrants.end()||grant->second!=s.key)return;
                VideoStreamStatus state;state.key=s.key;state.state=s.kind==VideoSignalKind::Stop?"Video disabled":"Unavailable";
                state.detail=s.body;states[s.key.window]=std::move(state);notify();
            }
            return;
        }
        auto& t=*it->second;
        if(s.kind==VideoSignalKind::Stop){
            t.permit->store(false);std::lock_guard lock(mutex);
            auto& state=states[t.key.window];state.key=t.key;state.sending=t.sending;state.state="Video disabled";
            state.detail="Publisher stopped this video stream";state.connected=state.decoded=false;frames.erase(t.key.window);notify();return;
        }
        if(s.kind==VideoSignalKind::Failure){
            t.failed=true;t.detail="Peer video error: "+s.body;t.permit->store(false);
            std::lock_guard lock(mutex);VideoStreamStatus state;state.key=t.key;state.sending=t.sending;state.state="Error";state.detail=t.detail;states[t.key.window]=std::move(state);frames.erase(t.key.window);notify();return;
        }
        if(!t.permit->load())return;
        if(s.kind==VideoSignalKind::Answer&&t.sending&&!t.answered){
            if(!t.offered||!t.localDescriptionSet)throw std::runtime_error("Answer before successful local Offer");
            auto* d=parseDescription(s);
            try{
                setDescription(t,d,false);
            }catch(...){gst_webrtc_session_description_free(d);throw;}
            gst_webrtc_session_description_free(d);t.answered=true;
        }else if(s.kind==VideoSignalKind::Ice){
            if(t.remoteIce.candidates()==0&&!t.remoteDescriptionSet)
                trace(t.key,t.sending,"remote-ice=before-description queued",s.serial);
            const bool endMarker=s.body.size()>=2&&s.body[1]=='|'&&s.body.size()==2;
            const unsigned mline=s.body.empty()?0U:static_cast<unsigned>(s.body[0]-'0');
            if(endMarker&&!t.remoteIce.endSeen(mline))trace(t.key,t.sending,"remote-eoc-mline="+std::to_string(mline)+" received",s.serial);
            if(t.remoteIce.push(s.body)&&s.body.size()>2)
                countCandidateType(std::string_view(s.body).substr(2),t.remoteHostCandidates,t.remoteSrflxCandidates,
                    t.remoteRelayCandidates,t.remotePrflxCandidates,t.remoteOtherCandidates);
            flushRemoteIce(t);
        }else if(s.kind==VideoSignalKind::Decoded&&t.sending){t.reported=true;t.lastAck=Clock::now();t.lastKeyframeAck=t.lastAck;t.keyframeBackoffStep=0;}
    }
    void push(Track& t,const VideoFrame& frame){
        if(!t.permit->load()||!validVideoFrame(frame)||frame.key!=t.key)return;
        if(Clock::now()-frame.time>500ms||frame.time<=t.lastSource){++t.dropped;return;}
        t.lastSource=frame.time;
        // Capture is commit-driven. Do not rescan every RGBA byte on the media worker:
        // that doubled memory traffic for high-refresh game streams without adding
        // any information, because unchanged surfaces do not issue another readback.
        if(frame.width!=t.width||frame.height!=t.height){
            auto* caps=gst_caps_new_simple("video/x-raw","format",G_TYPE_STRING,"RGBA","width",G_TYPE_INT,static_cast<int>(frame.width),"height",G_TYPE_INT,static_cast<int>(frame.height),"framerate",GST_TYPE_FRACTION,static_cast<int>(settings.fps),1,nullptr);
            gst_app_src_set_caps(GST_APP_SRC(t.src),caps);gst_caps_unref(caps);t.width=frame.width;t.height=frame.height;
            g_object_set(t.src,"max-bytes",static_cast<guint64>(frame.rgba.size()),nullptr);
            const auto resizedTarget=targetVideoBitrate(settings,frame.width,frame.height);
            if(t.encoder&&resizedTarget!=t.targetBitrate){
                g_object_set(t.encoder,"target-bitrate",resizedTarget,nullptr);
                t.targetBitrate=resizedTarget;
            }
        }
        auto* b=gst_buffer_new_allocate(nullptr,frame.rgba.size(),nullptr);if(!b){++t.dropped;return;}
        gst_buffer_fill(b,0,frame.rgba.data(),frame.rgba.size());
        if(!t.permit->load()){gst_buffer_unref(b);return;}
        const auto flow=gst_app_src_push_buffer(GST_APP_SRC(t.src),b);
        if(flow!=GST_FLOW_OK){++t.dropped;if(flow!=GST_FLOW_FLUSHING)throw std::runtime_error("appsrc rejected frame");return;}
        ++t.count;t.lastFrame=Clock::now();
    }
    void pull(Track& t){
        auto* sample=gst_app_sink_try_pull_sample(GST_APP_SINK(t.sink),0);if(!sample)return;
        try{
            auto* caps=gst_sample_get_caps(sample);auto* buffer=gst_sample_get_buffer(sample);
            const auto* structure=caps?gst_caps_get_structure(caps,0):nullptr;
            int w=0,h=0;const char* format=structure?gst_structure_get_string(structure,"format"):nullptr;
            if(!buffer||!structure||!format||std::strcmp(format,"RGBA")||!gst_structure_get_int(structure,"width",&w)||!gst_structure_get_int(structure,"height",&h)||w<2||w>8192||h<2||h>8192)throw std::runtime_error("unsupported decoded frame caps");
            const auto size=std::size_t(w)*h*4;
            // Controlled videoconvert output must be tightly packed; reject, never guess,
            // any padded/multi-planar buffer. RGBA row bytes are inherently 4-aligned.
            if(gst_buffer_get_size(buffer)!=size)throw std::runtime_error("decoded RGBA stride is not tightly packed");
            auto frame=std::make_shared<VideoFrame>();frame->key=t.key;frame->width=w;frame->height=h;frame->rgba.resize(size);
            if(gst_buffer_extract(buffer,0,frame->rgba.data(),size)!=size)throw std::runtime_error("decoded buffer extraction failed");
            frame->time=Clock::now();frame->serial=++t.count;t.lastFrame=frame->time;t.width=w;t.height=h;
            {std::lock_guard lock(mutex);
             const auto grant=remoteGrants.find(t.key.window);
             if(t.permit->load()&&desired.enabled&&contains(desired.receiving,t.key.window)&&grant!=remoteGrants.end()&&grant->second==t.key)frames[t.key.window]=std::move(frame);}
            if(!t.reported||Clock::now()-t.lastAck>=1s){emit(t,VideoSignalKind::Decoded,frameSize(w,h));t.reported=true;t.lastAck=Clock::now();}
            notify();
        }catch(...){gst_sample_unref(sample);throw;}
        gst_sample_unref(sample);
    }
    bool requestKeyframe(Track& t,const char* reason) {
        if(!t.sending||!t.encodedSrc||!t.permit->load(std::memory_order_acquire))return false;
        const auto now=Clock::now();
        if(t.lastAck!=Time{}&&t.lastAck>t.lastKeyframeAck){t.lastKeyframeAck=t.lastAck;t.keyframeBackoffStep=0;}
        const auto backoff=1200ms*(1U<<std::min(t.keyframeBackoffStep,3U));
        if(t.lastKeyframeRequest!=Time{}&&now-t.lastKeyframeRequest<backoff)return false;
        t.lastKeyframeRequest=now;
        t.keyframeBackoffStep=std::min(t.keyframeBackoffStep+1U,3U);
        ++t.keyframeRequests;
        auto* event=gst_video_event_new_upstream_force_key_unit(GST_CLOCK_TIME_NONE,TRUE,static_cast<guint>(t.keyframeRequests));
        const gboolean accepted=event&&gst_pad_send_event(t.encodedSrc,event);
        if(accepted)++t.keyframeRequestsAccepted;else ++t.keyframeRequestsUnhandled;
        trace(t.key,t.sending,std::string("keyframe-request=")+(accepted?"accepted":"not-handled")+" reason="+reason);
        return accepted;
    }
    void tick(Track& t,const VideoContext& plan){
        if(t.sending&&!t.offered){
            auto* caps=gst_pad_get_current_caps(t.gate);
            if(caps){gst_caps_unref(caps);createDescription(t,true);t.offered=true;}
        }
        auto* bus=gst_element_get_bus(t.pipeline);
        while(auto* message=gst_bus_pop_filtered(bus,GST_MESSAGE_ERROR)){
            GError* error=nullptr;gchar* debug=nullptr;gst_message_parse_error(message,&error,&debug);if(debug)g_free(debug);gst_message_unref(message);
            const auto why=errorText(error);
            gst_object_unref(bus);throw std::runtime_error(why);
        }
        gst_object_unref(bus);
        g_object_get(t.rtc,"connection-state",&t.connection,"ice-connection-state",&t.iceState,nullptr);
        if(!t.sending&&t.padLinkFailed.load(std::memory_order_acquire)){
            if(t.padLinkMissingSink.load(std::memory_order_acquire))
                throw std::runtime_error("cannot link incoming WebRTC RTP pad: missing decoder sink pad");
            const int result=t.padLinkResult.load(std::memory_order_acquire);
            throw std::runtime_error("cannot link incoming WebRTC RTP pad to VP8 depayloader: "+
                std::string(gst_pad_link_get_name(static_cast<GstPadLinkReturn>(result))));
        }
        if(t.connection==4||t.connection==5||t.iceState==4)throw std::runtime_error("ICE/DTLS connection failed; inspect routes and candidate diagnostics");
        if(t.sending&&t.connection==2){
            // The very first keyframe may be encoded before ICE/DTLS is usable and
            // never reach the peer. Request a fresh one as soon as media connects;
            // repeat only while no decoder acknowledgement arrives, rate-limited.
            const auto now=Clock::now();
            const auto lastUseful=t.lastAck==Time{}?t.started:t.lastAck;
            const bool sourceActive=t.lastSource!=Time{}&&now-t.lastSource<=1500ms;
            // Do not keep requesting keyframes from an idle static desktop: there is
            // no new input buffer to encode until the source commits again. Active
            // publishers still get normal decode-ack recovery.
            if((!t.reported||(sourceActive&&now-lastUseful>1800ms))&&
               (t.lastKeyframeRequest==Time{}||now-t.lastKeyframeRequest>=1200ms))
                requestKeyframe(t,!t.reported?"connected-awaiting-first-decode":"decode-ack-timeout");
        }
        if(t.connection!=2&&Clock::now()-t.started>15s){
            if(t.sending&&!t.offered)throw std::runtime_error("Negotiation timeout: no local Offer/RTP caps");
            if(t.sending&&!t.answered)throw std::runtime_error("Signaling timeout: SDP Answer not received");
            if(!t.localDescriptionSet||!t.remoteDescriptionSet)throw std::runtime_error("Negotiation timeout: SDP description incomplete");
            if(!t.remoteIce.candidates())throw std::runtime_error("ICE timeout: no remote candidates after SDP; inspect signaling/gathering");
            throw std::runtime_error("ICE/DTLS connectivity timeout after SDP and remote ICE; inspect transport state");
        }
        // Bounded trickle draining leaves room for the other Track and for SDP.
        // The EOC reserved slot cannot overflow the 64-message outgoing queue.
        for(unsigned budget=0;budget<8;++budget){
            {std::lock_guard lock(mutex);if(outgoing.size()>=MaxVideoSignalQueue||
                MaxVideoSignalQueueBytes-outgoingBytes<detail::MaxIceBody)break;}
            std::optional<detail::IceCandidate> next;
            {std::lock_guard lock(t.iceMutex);if(!t.iceError.empty())throw std::runtime_error(t.iceError);next=t.ice.pop();}
            if(!next)break;
            auto candidate=next->wire();
            if(next->end())trace(t.key,t.sending,"local-eoc=queued");
            emit(t,VideoSignalKind::Ice,std::move(candidate));
        }
        if(!t.sending&&t.permit->load())pull(t);
        const auto now=Clock::now();
        if(now-t.rateAt>=1s){
            t.fps=double(t.count-t.rateCount)/std::chrono::duration<double>(now-t.rateAt).count();
            t.rateAt=now;t.rateCount=t.count;
            if(t.sending&&t.encoder){
                const auto wanted=keyframeDistanceForInputFps(t.fps,settings.fps);
                if(wanted!=t.keyframeMaxDist){
                    // Adapt keyframe spacing to actual submitted buffers, not the
                    // optional configured cap. Variable-rate sources can be either
                    // high cadence or completely idle.
                    g_object_set(t.encoder,"keyframe-max-dist",wanted,nullptr);
                    t.keyframeMaxDist=wanted;
                    trace(t.key,true,"keyframe-max-dist="+std::to_string(wanted)+" measured_fps="+std::to_string(t.fps));
                }
            }
        }
        if(now-t.lastReport>=250ms){
            VideoStreamStatus s;s.key=t.key;s.sending=t.sending;s.connected=t.connection==2;s.frames=t.count;s.dropped=t.dropped;s.width=t.width;s.height=t.height;s.fps=t.fps;
            s.decoded=t.reported&&(t.sending?now-t.lastAck<2s:true);
            s.state=!s.connected?"Connecting":s.decoded?"Streaming":"Waiting for frames";
            s.detail="VP8 "+frameSize(t.width,t.height)+" / "+std::to_string(t.count)+(t.sending?" submitted":" decoded")+" / "+std::to_string(t.dropped)+" dropped / rtp_"+(t.sending?"tx=":"rx=")+std::to_string(t.rtpPackets.load(std::memory_order_relaxed))+" packets "+std::to_string(t.rtpBytes.load(std::memory_order_relaxed))+" bytes";
            GstState currentState=GST_STATE_VOID_PENDING,pendingState=GST_STATE_VOID_PENDING;
            (void)gst_element_get_state(t.pipeline,&currentState,&pendingState,0);
            s.detail+=" / gst="+std::string(gst_element_state_get_name(currentState));
            if(pendingState!=GST_STATE_VOID_PENDING)s.detail+="->"+std::string(gst_element_state_get_name(pendingState));
            if(t.sending)s.detail+=" / target="+std::to_string(t.targetBitrate/1000U)+" kbps";
            if(!s.sending){
                s.detail+=" / pad="+(t.padLinked.load()?std::string("linked"):t.padLinkFailed.load()?std::string("failed"):std::string("pending"));
                std::string negotiatedCaps;{std::lock_guard callbackLock(t.callbacks->mutex);negotiatedCaps=t.rxCaps;}
                if(!negotiatedCaps.empty())s.detail+=" / caps="+negotiatedCaps;
                s.detail+=" / depay="+std::to_string(t.depayBuffers.load(std::memory_order_relaxed))+"buf/"+std::to_string(t.depayBytes.load(std::memory_order_relaxed))+"B";
                s.detail+=" / decode="+std::to_string(t.decoderBuffers.load(std::memory_order_relaxed))+"buf/"+std::to_string(t.decoderBytes.load(std::memory_order_relaxed))+"B";
                if(t.rxQueue){guint levelBuffers=0,levelBytes=0;guint64 levelTime=0;
                    g_object_get(t.rxQueue,"current-level-buffers",&levelBuffers,"current-level-bytes",&levelBytes,"current-level-time",&levelTime,nullptr);
                    s.detail+=" / queue="+std::to_string(levelBuffers)+"buf/"+std::to_string(levelBytes)+"B/"+std::to_string(levelTime/1000000ULL)+"ms";
                }
            }
            s.ice=iceDiagnostics(t);
            {std::lock_guard lock(mutex);states[t.key.window]=std::move(s);}
            t.lastReport=now;notify();
        }
        // Bounded real stats probe; never dump private SDP, addresses or ICE passwords.
        if(t.connection==2&&now-t.lastStats>=5s){
            t.lastStats=now;
            try {
                GstPad* selector=nullptr;
                {std::lock_guard callbackLock(t.callbacks->mutex);auto* candidate=t.sending?t.gate:t.rxPad;if(candidate)selector=GST_PAD(gst_object_ref(candidate));}
                if(!selector){std::lock_guard lock(mutex);stats[t.key]="WebRTC stats waiting for negotiated VP8 pad";}
                else {
                    try {
                        auto* p=promise(t,[&](auto* p){g_signal_emit_by_name(t.rtc,"get-stats",selector,p);},500ms);
                        const auto* reply=gst_promise_get_reply(p);
                        if(reply){
                            const auto summary=summarizeWebRtcStats(reply,t.sending);
                            std::lock_guard lock(mutex);stats[t.key]=summary;
                        }
                        gst_promise_unref(p);
                    } catch (...) {gst_object_unref(selector);throw;}
                    gst_object_unref(selector);
                }
            } catch (const std::exception& e) {std::lock_guard lock(mutex);stats[t.key]="Stats unavailable: "+std::string(e.what());}
        }
    }
    void run() noexcept {
        try{
            std::uint64_t applied=UINT64_MAX;
            while(!abort.load()){
                VideoContext plan;std::deque<VideoSignal> signals;std::map<VideoTrackKey,std::shared_ptr<const VideoFrame>> pending;std::map<VideoTrackKey,std::uint64_t> dropped;std::uint64_t version;
                {std::unique_lock lock(mutex);auto changed=[&]{return abort.load()||applied!=revision||!incoming.empty()||!inputs.empty();};
                 if(tracks.empty()&&deferred.empty())cv.wait(lock,changed);else cv.wait_for(lock,5ms,changed);
                 releaseDeferred(Clock::now());
                 plan=desired;version=revision;signals.swap(incoming);incomingBytes=0;pending.swap(inputs);dropped.swap(inputDropped);}
                if(abort.load())break;
                retireClosed(plan);
                if(version!=applied){
                    applied=version;
                    std::size_t slot=0;
                    for(const auto& source:plan.sending){
                        if(!plan.enabled||abort.load())break;
                        if(slot++>=settings.streams){
                            bool announce=false;VideoTrackKey deferred;
                            {std::lock_guard lock(mutex);auto& state=states[source.window];
                             announce=state.key.publication!=source.publication||state.state!="Unavailable";
                             if(announce){if(nextStream==UINT64_MAX)throw std::runtime_error("video stream sequence exhausted");state=VideoStreamStatus{};state.key={source.window,source.publication,++nextStream};state.sending=true;state.state="Unavailable";state.detail="Video source limit reached ("+std::to_string(settings.streams)+"); unpublish an earlier source";}
                             deferred=state.key;}
                            if(announce){Track stub;stub.key=deferred;stub.permit=std::make_shared<std::atomic<bool>>(false);emit(stub,VideoSignalKind::Failure,"Publisher video source limit reached; metadata remains published");}
                            continue;
                        }
                        bool exists=false;for(const auto& [k,t]:tracks)if(t->sending&&k.window==source.window&&k.publication==source.publication)exists=true;
                        {std::lock_guard lock(mutex);const auto old=states.find(source.window);if(old!=states.end()&&old->second.state=="Error"&&old->second.key.publication==source.publication)exists=true;}
                        if(exists)continue;
                        if(nextStream==UINT64_MAX)throw std::runtime_error("video stream sequence exhausted");
                        const VideoTrackKey key{source.window,source.publication,++nextStream};const auto size=fitVideoSize(source.width,source.height,settings);
                        try{auto t=create(key,true,size.first,size.second);auto* rawTrack=t.get();tracks.emplace(key,std::move(t));
                        notify();}
                        catch(const std::exception& e){
                            {std::lock_guard lock(mutex);if(version!=revision){
                                traceLocked(key,true,"creation-cancelled=configuration-changed");continue;}}
                            Track rejected;rejected.key=key;rejected.permit=std::make_shared<std::atomic<bool>>(false);
                            emit(rejected,VideoSignalKind::Failure,cleanWindowText(e.what(),240));
                            std::lock_guard lock(mutex);VideoStreamStatus s;s.key=key;s.sending=true;s.state="Error";s.detail=e.what();states[key.window]=std::move(s);notify();}
                    }
                }
                for(const auto& s:signals)try{onSignal(s);}catch(const std::exception& e){
                    if(auto it=tracks.find(s.key);it!=tracks.end())trackError(*it->second,e.what());
                    else {Track rejected;rejected.key=s.key;rejected.permit=std::make_shared<std::atomic<bool>>(false);
                        emit(rejected,VideoSignalKind::Failure,cleanWindowText(e.what(),240));
                        std::lock_guard lock(mutex);VideoStreamStatus state;state.key=s.key;state.state="Error";state.detail=e.what();states[s.key.window]=std::move(state);notify();}
                }
                for(const auto& [key,count]:dropped)if(auto it=tracks.find(key);it!=tracks.end())it->second->dropped+=count;
                for(auto& [key,frame]:pending)if(auto it=tracks.find(key);it!=tracks.end()&&it->second->sending&&it->second->permit->load())
                    try{push(*it->second,*frame);}catch(const std::exception& e){trackError(*it->second,e.what());}
                for(auto& [key,t]:tracks){(void)key;if(!t->permit->load())continue;try{tick(*t,plan);}catch(const std::exception& e){trackError(*t,e.what());}}
                {std::lock_guard lock(mutex);
                 std::erase_if(states,[&](const auto& x){return !desired.enabled||
                    (!contains(desired.receiving,x.first)&&std::none_of(desired.sending.begin(),desired.sending.end(),[&](const auto& p){return p.window==x.first;}));});
                 std::erase_if(stats,[&](const auto& x){const auto state=states.find(x.first.window);return state==states.end()||state->second.key!=x.first;});}
            }
        }catch(const std::exception& e){setFault(e.what());}catch(...){setFault("Unknown media worker failure");}
        for(auto& [key,t]:tracks){(void)key;destroy(*t);}tracks.clear();notify();
    }
    void shutdown() noexcept {
        abort=true;
        {std::lock_guard lock(mutex);for(auto& [k,p]:permits){(void)k;p->store(false);}inputs.clear();inputDropped.clear();frames.clear();deferred.clear();deferredBytes=0;}
        cv.notify_one();
        if(worker.joinable())worker.join();if(fd>=0){::close(fd);fd=-1;}
    }
};
MediaSession::MediaSession(VideoSettings s):m(std::make_unique<Impl>(s)){}
MediaSession::~MediaSession()=default;
const Availability& MediaSession::availability() const noexcept{return m->availability;}
const VideoSettings& MediaSession::settings() const noexcept{return m->settings;}
int MediaSession::notificationFd() const noexcept{return m->fd;}
void MediaSession::acknowledge() noexcept{drain(m->fd);}
void MediaSession::configure(VideoContext c){
    if(!m->availability.available)return;
    if(!wire::nonzero(c.session)||!wire::nonzero(c.local)||!wire::nonzero(c.peer)||!wire::nonzero(c.world)||c.local==c.peer)c.enabled=false;
    std::sort(c.sending.begin(),c.sending.end(),[](const auto& a,const auto& b){return a.window<b.window;});
    if(c.sending.size()>MaxSharedWindows)c.sending.resize(MaxSharedWindows);
    if(c.receiving.size()>MaxSharedWindows)c.receiving.resize(MaxSharedWindows);
    if(!c.enabled){c.sending.clear();c.receiving.clear();}
    {std::lock_guard lock(m->mutex);
     if(!m->fault.empty()){c.enabled=false;c.sending.clear();c.receiving.clear();}
     if(c==m->desired)return;
     const bool changed=!sameWorld(c,m->desired);
     for(auto& [key,permit]:m->permits){
         bool allowed=c.enabled&&!changed;
         if(key.window.owner==c.local)allowed=allowed&&std::any_of(c.sending.begin(),c.sending.begin()+std::min(c.sending.size(),std::size_t(m->settings.streams)),[&](const auto& w){return w.window==key.window&&w.publication==key.publication;});
         else allowed=allowed&&contains(c.receiving,key.window);
         if(!allowed)permit->store(false,std::memory_order_release);
     }
     if(changed){m->incoming.clear();m->outgoing.clear();m->incomingBytes=m->outgoingBytes=0;m->lastSignal=0;m->frames.clear();m->remoteGrants.clear();m->states.clear();m->stats.clear();
         m->deferred.clear();m->deferredBytes=0;m->receiveFloor.clear();m->retiredPublication=0;}
     for(auto it=m->receiveFloor.begin();it!=m->receiveFloor.end();){
         if(!c.enabled||!contains(c.receiving,it->first)){
             if(auto grant=m->remoteGrants.find(it->first);grant!=m->remoteGrants.end()){
                 m->retiredPublication=std::max(m->retiredPublication,grant->second.publication);
                 m->traceLocked(grant->second,false,"metadata=withdrawn");m->remoteGrants.erase(grant);
             }
             m->discardDeferred(it->first);it=m->receiveFloor.erase(it);
         }else ++it;
     }
     if(c.enabled)for(const auto& key:c.receiving)m->receiveFloor.try_emplace(key,m->retiredPublication);
     else {m->deferred.clear();m->deferredBytes=0;m->incoming.clear();m->incomingBytes=0;
         for(const auto& [key,grant]:m->remoteGrants){(void)key;m->retiredPublication=std::max(m->retiredPublication,grant.publication);}
         m->remoteGrants.clear();}
     std::erase_if(m->frames,[&](const auto& x){return !c.enabled||!contains(c.receiving,x.first);});
     std::erase_if(m->inputs,[&](const auto& x){const auto it=m->permits.find(x.first);return it==m->permits.end()||!it->second->load();});
     std::erase_if(m->states,[&](const auto& x){return !c.enabled||(!contains(c.receiving,x.first)&&std::none_of(c.sending.begin(),c.sending.end(),[&](const auto& y){return y.window==x.first;}));});
     std::erase_if(m->stats,[&](const auto& x){const auto state=m->states.find(x.first.window);return state==m->states.end()||state->second.key!=x.first;});
     m->desired=std::move(c);m->releaseDeferred(Clock::now());++m->revision;}
    m->cv.notify_one();m->notify();
}
void MediaSession::revoke(const WindowKey& key){
    {std::lock_guard lock(m->mutex);for(auto& [k,p]:m->permits)if(k.window==key)p->store(false);
     std::erase_if(m->desired.sending,[&](const auto& s){return s.window==key;});
     std::erase_if(m->inputs,[&](const auto& x){return x.first.window==key;});m->frames.erase(key);++m->revision;}
    m->cv.notify_one();m->notify();
}
bool MediaSession::signal(VideoSignal s){
    std::string why;if(!validVideoSignal(s,why))return false;
    bool accepted=false,overflow=false;
    {
        std::lock_guard lock(m->mutex);const auto& c=m->desired;
        const bool sending=s.key.window.owner==c.local;
        auto reject=[&](const char* reason){m->traceLocked(s.key,sending,reason,s.serial);return false;};
        if(!c.enabled||!m->fault.empty())return reject("reject=media-disabled");
        if(!scoped(s.key,c)||s.sender!=c.peer)return reject("reject=scope-or-sender");
        if(s.serial<=m->lastSignal)return reject("reject=stale-serial");
        m->lastSignal=s.serial;
        const bool owner=s.key.window.owner==c.peer;
        const bool metadata=owner&&contains(c.receiving,s.key.window);
        auto old=m->remoteGrants.find(s.key.window);
        const auto order=[](const VideoTrackKey& k){return std::pair(k.publication,k.stream);};
        if(s.kind==VideoSignalKind::Offer){
            if(!owner)return reject("reject=offer-from-nonowner");
            const auto floor=m->receiveFloor.find(s.key.window);
            const auto minimum=floor==m->receiveFloor.end()?m->retiredPublication:floor->second;
            if(s.key.publication<=minimum)return reject("reject=retired-publication");
            if(old!=m->remoteGrants.end()&&order(s.key)<=order(old->second))return reject("reject=stale-or-duplicate-offer");
            if(!metadata&&!m->remoteGrants.contains(s.key.window)&&m->remoteGrants.size()>=MaxSharedWindows*2)
                return reject("reject=pending-window-limit");
            if(!m->queueRoom(s.body.size()))return reject("reject=metadata-signaling-budget");
            m->discardDeferred(s.key.window);
            for(auto& [key,permit]:m->permits)if(key.window==s.key.window&&key!=s.key)permit->store(false);
            m->frames.erase(s.key.window);m->remoteGrants[s.key.window]=s.key;
        }else if(owner){
            // A terminal message can supersede an older incarnation without ever
            // allocating a decoder. ICE/frames require the exact current grant.
            if(s.kind==VideoSignalKind::Failure||s.kind==VideoSignalKind::Stop){
                if(old!=m->remoteGrants.end()&&order(s.key)<order(old->second)){
                    if(s.kind==VideoSignalKind::Stop){m->traceLocked(s.key,false,"ignore=stale-stop",s.serial);return true;}
                    return reject("reject=stale-terminal");
                }
                if(old==m->remoteGrants.end()&&!metadata&&m->remoteGrants.size()>=MaxSharedWindows*2)
                    return reject("reject=pending-window-limit");
                m->discardDeferred(s.key.window);
                std::erase_if(m->incoming,[&](const auto& item){
                    if(item.key.window!=s.key.window||order(item.key)>order(s.key)||
                       item.kind==VideoSignalKind::Stop||item.kind==VideoSignalKind::Failure)return false;
                    m->incomingBytes-=item.body.size();return true;
                });
                m->remoteGrants[s.key.window]=s.key;
                for(auto& [key,p]:m->permits)if(key.window==s.key.window&&order(key)<=order(s.key))p->store(false);
                m->frames.erase(s.key.window);
            }else if(old==m->remoteGrants.end()||old->second!=s.key)return reject("reject=no-current-offer");
        }else{
            const auto permit=m->permits.find(s.key);
            if(permit==m->permits.end()||!permit->second->load())return reject("reject=no-live-local-track");
        }
        if(s.kind==VideoSignalKind::Stop){
            for(auto& [key,p]:m->permits)if(key==s.key)p->store(false);
            if(auto it=m->frames.find(s.key.window);it!=m->frames.end()&&it->second->key==s.key)m->frames.erase(it);
        }
        if(!m->queueRoom(s.body.size()))overflow=true;
        else if(owner&&!metadata){
            auto until=Clock::now()+5s;
            for(const auto& item:m->deferred)if(item.signal.key==s.key){until=item.deadline;break;}
            if(s.kind==VideoSignalKind::Offer)m->traceLocked(s.key,false,"metadata-barrier=held",s.serial);
            m->deferredBytes+=s.body.size();m->deferred.push_back({std::move(s),until});accepted=true;
        }else{
            m->incomingBytes+=s.body.size();m->incoming.push_back(std::move(s));accepted=true;
        }
    }
    if(overflow)m->setFault("Media signaling input queue full");
    else if(accepted)m->cv.notify_one();
    return accepted;
}
bool MediaSession::submit(std::shared_ptr<const VideoFrame> frame){
    if(!frame||!validVideoFrame(*frame))return false;
    std::lock_guard lock(m->mutex);const auto it=m->permits.find(frame->key);
    if(!m->desired.enabled||!m->fault.empty()||frame->key.window.owner!=m->desired.local||it==m->permits.end()||!it->second->load())return false;
    if(Clock::now()-frame->time>500ms){++m->inputDropped[frame->key];return false;}
    if(auto old=m->inputs.find(frame->key);old!=m->inputs.end()){
        ++m->inputDropped[frame->key];if(old->second->time>frame->time)return false;
    }
    m->inputs[frame->key]=std::move(frame);m->cv.notify_one();return true;
}
std::vector<VideoTrackKey> MediaSession::captures() const {
    std::vector<VideoTrackKey> out;std::lock_guard lock(m->mutex);
    if(m->desired.enabled&&m->fault.empty())for(const auto& [key,p]:m->permits)if(key.window.owner==m->desired.local&&p->load())out.push_back(key);
    return out;
}
std::shared_ptr<const VideoFrame> MediaSession::frame(const WindowKey& key) const {
    std::lock_guard lock(m->mutex);auto it=m->frames.find(key);if(it==m->frames.end())return {};
    const auto permit=m->permits.find(it->second->key);return permit!=m->permits.end()&&permit->second->load()?it->second:nullptr;
}
std::vector<VideoTrackKey> MediaSession::images() const {
    std::vector<VideoTrackKey> out;std::lock_guard lock(m->mutex);
    for(const auto& [key,f]:m->frames){(void)key;const auto it=m->permits.find(f->key);if(it!=m->permits.end()&&it->second->load())out.push_back(f->key);}return out;
}
std::vector<VideoSignal> MediaSession::takeSignals(){
    std::vector<VideoSignal> out;std::lock_guard lock(m->mutex);out.reserve(m->outgoing.size());
    while(!m->outgoing.empty()){
        auto signal=std::move(m->outgoing.front());m->outgoing.pop_front();
        const auto permit=m->permits.find(signal.key);
        if(signal.kind!=VideoSignalKind::Stop&&signal.kind!=VideoSignalKind::Failure&&
           (permit==m->permits.end()||!permit->second->load()))continue;
        if(signal.kind==VideoSignalKind::Offer||signal.kind==VideoSignalKind::Answer)
            m->traceLocked(signal.key,signal.key.window.owner==m->desired.local,"signal-to-control kind="+std::to_string(static_cast<unsigned>(signal.kind)));
        out.push_back(std::move(signal));
    }
    m->outgoingBytes=0;return out;
}
std::vector<VideoStreamStatus> MediaSession::status() const {std::vector<VideoStreamStatus> out;std::lock_guard lock(m->mutex);for(const auto& [key,s]:m->states){(void)key;out.push_back(s);}return out;}
std::string MediaSession::error() const {std::lock_guard lock(m->mutex);return m->fault;}
std::string MediaSession::diagnostic() const {
    std::lock_guard lock(m->mutex);std::ostringstream out;out<<m->availability.detail<<"\n"<<m->fault<<"\nLimits: "<<m->settings.streams<<" send + "<<m->settings.streams<<" receive; "<<(m->settings.fps?std::to_string(m->settings.fps)+" fps cap":"variable source cadence (no FPS cap)")<<"; per-track target scales by source size up to "<<m->settings.bitrate<<" bps\n";
    for(const auto& [key,s]:m->states){out<<idString(key.owner)<<"/"<<key.window<<" "<<(s.sending?"TX ":"RX ")<<s.state<<": "<<s.detail<<" rate="<<s.fps<<" fps "<<s.ice<<"\n";if(auto it=m->stats.find(s.key);it!=m->stats.end())out<<it->second<<"\n";}
    out<<"Pending metadata signals="<<m->deferred.size()<<" bytes="<<m->deferredBytes<<"\n";
    for(const auto& event:m->lifecycle)out<<event<<"\n";
    return out.str();
}
void MediaSession::fail(std::string reason){m->setFault(std::move(reason));}
void MediaSession::stop() noexcept {try{VideoContext empty;{std::lock_guard lock(m->mutex);empty=m->desired;}empty.enabled=false;configure(std::move(empty));}catch(...){m->abort=true;m->cv.notify_one();}}
void MediaSession::shutdown() noexcept {m->shutdown();}
} // namespace hyprwalk::mp::media
