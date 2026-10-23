// Exact production ICE/Track methods and a GObject action SPY (TEST DOUBLE).
// SDP-ready flags are controlled by the fixture and webrtcbin remains NULL.
// The spy records arguments and stops the default action: applying ICE to a NULL
// webrtcbin with pretend SDP would be an invalid test. No candidate is sent to
// libnice here. media-ice-process-test is the separate real connectivity test.
#define HYPRWALK_MEDIA_INTERNAL_TEST
#include "../media/gstreamer/MediaSession.cpp"
#include <iostream>
#include <source_location>
namespace hyprwalk::mp::media {
struct MediaSessionTestAccess {
    using Impl=MediaSession::Impl;
    inline static unsigned groups=0;
    static void check(bool value,const std::source_location at=std::source_location::current()) {
        if(!value)throw std::runtime_error("media ICE regression line "+std::to_string(at.line()));
    }
    static void pass(const char* text){++groups;std::cout<<"PASS "<<text<<'\n';}
    static VideoContext context(){VideoContext c;c.session[0]=1;c.local[0]=2;c.peer[0]=3;c.world[0]=4;c.enabled=true;return c;}
    static VideoTrackKey key(const VideoContext& c,unsigned window=1,unsigned pub=1){return{{c.session,c.local,c.world,window},pub,pub};}
    static VideoSignal ice(const VideoContext& c,const VideoTrackKey& k,std::string body){VideoSignal s;s.key=k;s.sender=c.peer;s.kind=VideoSignalKind::Ice;s.serial=1;s.body=std::move(body);return s;}
    static Impl::Track& add(Impl& m,const VideoTrackKey& k,std::vector<std::string>& applied){
        auto t=std::make_unique<Impl::Track>();t->owner=&m;t->key=k;t->sending=true;t->permit=std::make_shared<std::atomic<bool>>(true);t->callbacks->track=t.get();
        t->rtc=gst_element_factory_make("webrtcbin",nullptr);check(t->rtc);
        g_signal_connect_data(t->rtc,"add-ice-candidate",G_CALLBACK(+[](GstElement* rtc,guint line,gchar* text,gpointer data){
            auto& out=*static_cast<std::vector<std::string>*>(data);out.push_back(std::to_string(line)+"|"+(text?text:"<NULL>"));
            g_signal_stop_emission_by_name(rtc,"add-ice-candidate");
        }),&applied,nullptr,static_cast<GConnectFlags>(0));
        m.permits[k]=t->permit;auto& out=*t;m.tracks.emplace(k,std::move(t));return out;
    }
    static void erase(Impl& m,const VideoTrackKey& k){auto it=m.tracks.find(k);check(it!=m.tracks.end());m.destroy(*it->second);m.tracks.erase(it);}
    static void run(){
        MediaSession media;media.shutdown();auto& m=*media.m;
        // Test fixture only: no worker, no pipeline or ICE backend is substituted.
        m.availability.available=true;m.abort=false;
        auto c=context();c.sending={{key(c).window,1,128,64},{key(c,2).window,1,128,64}};media.configure(c);
        const std::string body="0|candidate:1 1 UDP 2122260223 10.109.150.60 45000 typ host";
        std::vector<std::string> a,b;a.reserve(512);b.reserve(512);
        auto& t=add(m,key(c),a);auto& second=add(m,key(c,2),b);
        auto ref=t.callbacks;
        Impl::iceCandidate(t.rtc,0U,const_cast<char*>(body.c_str()+2),&ref);
        Impl::iceCandidate(t.rtc,0U,const_cast<char*>(""),&ref);
        Impl::iceCandidate(t.rtc,0U,nullptr,&ref);
        check(t.ice.candidates()==1&&t.ice.pending()==2&&!t.ice.pop());
        t.ice.ready();check(t.ice.pop()->wire()==body&&t.ice.pop()->end()&&!t.ice.pop());
        pass("exact callback: candidate + empty/null EOC, SDP barrier, one reserved marker");

        m.onSignal(ice(c,t.key,body));m.onSignal(ice(c,t.key,"0|"));
        check(a.empty()&&t.remoteIce.pending()==2);
        t.localDescriptionSet=true;m.flushRemoteIce(t);check(a.empty());
        t.remoteDescriptionSet=true;m.flushRemoteIce(t);
        check(a.size()==2&&a[0]==body&&a[1]=="0|"&&t.remoteIce.endDelivered());
        pass("remote queue drains AFTER both SDP flags; add-ice-candidate SPY observes empty string");
        for(unsigned n=0;n<300;++n)m.onSignal(ice(c,t.key,"0|"));
        check(a.size()==2&&t.remoteIce.candidates()==1&&!t.failed);
        pass("300 duplicate EOC do not emit again or consume quota");

        second.localDescriptionSet=second.remoteDescriptionSet=true;
        m.onSignal(ice(c,second.key,"0|"));check(b.size()==1&&b[0]=="0|"&&second.remoteIce.candidates()==0);
        pass("EOC without prior candidates; second m-line-0 Track is independent");

        bool caught=false;try{m.onSignal(ice(c,t.key,"1|"));}catch(const std::exception& e){caught=true;m.trackError(t,e.what());}
        check(caught&&t.failed&&!t.permit->load()&&second.permit->load()&&m.fault.empty());
        pass("bad m-line fails only its Track, leaves other Track and MediaSession alive");
        auto stopped=ice(c,second.key,"stopped");stopped.kind=VideoSignalKind::Stop;m.onSignal(stopped);
        m.onSignal(ice(c,second.key,"bad"));check(b.size()==1&&!second.permit->load());
        const auto old=second.key;erase(m,t.key);erase(m,old);check(m.tracks.empty()&&m.permits.empty());
        m.onSignal(ice(c,old,body));check(m.tracks.empty()&&b.size()==1);
        pass("Stop/destroy discard late ICE and clear all Track queues/objects");

        auto& fresh=add(m,key(c,2,2),b);fresh.localDescriptionSet=fresh.remoteDescriptionSet=true;
        m.onSignal(ice(c,old,body));check(b.size()==1&&fresh.remoteIce.candidates()==0);
        m.onSignal(ice(c,fresh.key,body));check(b.size()==2&&fresh.remoteIce.candidates()==1);
        pass("old publication/stream cannot feed newly published Track");erase(m,fresh.key);

        c.world[0]=9;media.configure(c);
        auto& reconnected=add(m,key(c),a);reconnected.localDescriptionSet=reconnected.remoteDescriptionSet=true;
        m.onSignal(ice(context(),key(context()),body));check(reconnected.remoteIce.candidates()==0);
        m.onSignal(ice(c,reconnected.key,"0|"));check(a.back()=="0|"&&reconnected.remoteIce.endDelivered());
        erase(m,reconnected.key);pass("world reconnect rejects old generation and starts clean EOC state");

        auto& limit=add(m,key(c,3),a);
        for(unsigned n=0;n<128;++n)m.onSignal(ice(c,limit.key,body));
        m.onSignal(ice(c,limit.key,"0|"));check(limit.remoteIce.candidates()==128&&limit.remoteIce.pending()==129);
        limit.localDescriptionSet=limit.remoteDescriptionSet=true;m.flushRemoteIce(limit);
        check(limit.remoteIce.pending()==0&&a.back()=="0|");erase(m,limit.key);
        pass("128 real queued candidates plus EOC flush without quota failure");

        auto& overflow=add(m,key(c,4),a);for(unsigned n=0;n<128;++n)m.onSignal(ice(c,overflow.key,body));
        caught=false;try{m.onSignal(ice(c,overflow.key,body));}catch(const std::runtime_error&){caught=true;}
        check(caught&&overflow.remoteIce.pending()==128);erase(m,overflow.key);
        pass("129th ordinary candidate is rejected; allocation remains bounded");

        for(unsigned n=0;n<100;++n){auto& item=add(m,key(c,5,n+1),a);m.onSignal(ice(c,item.key,"0|"));erase(m,item.key);}
        check(m.tracks.empty()&&m.permits.empty());pass("100 Track/EOC destroy/recreate cycles");
        c.enabled=false;media.configure(c);check(media.captures().empty()&&media.images().empty());media.shutdown();
        std::cout<<groups<<"/11 exact-media ICE groups passed (NOT ICE connectivity)\n";
    }
};
}
int main(){try{hyprwalk::mp::media::MediaSessionTestAccess::run();return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
