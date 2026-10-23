// Exact production admission/lifetime gates. NO live pipeline, no ICE substitute.
// Run media-process-test separately for genuine libnice/DTLS/VP8 connectivity.
#define HYPRWALK_MEDIA_INTERNAL_TEST
#include "../media/gstreamer/MediaSession.cpp"
#include <iostream>
#include <source_location>
namespace hyprwalk::mp::media {
struct MediaSessionTestAccess {
    using Impl=MediaSession::Impl;
    inline static unsigned groups=0;
    static void check(bool b,const std::source_location at=std::source_location::current()){
        if(!b)throw std::runtime_error("admission regression line "+std::to_string(at.line()));
    }
    static void pass(const char* name){++groups;std::cout<<"PASS "<<name<<'\n';}
    static VideoContext context(){VideoContext c;c.session[0]=1;c.local[0]=2;c.peer[0]=3;c.world[0]=4;c.enabled=true;return c;}
    static VideoTrackKey key(const VideoContext& c,unsigned id=1,unsigned pub=1,unsigned stream=1,bool tx=false){return{{c.session,tx?c.local:c.peer,c.world,id},pub,stream};}
    static VideoSignal signal(const VideoContext& c,VideoTrackKey k,unsigned serial,VideoSignalKind kind=VideoSignalKind::Offer){
        VideoSignal s;s.key=k;s.sender=c.peer;s.serial=serial;s.kind=kind;
        s.body=kind==VideoSignalKind::Offer?"v=0\r\n":kind==VideoSignalKind::Ice?"0|":"test terminal";return s;
    }
    struct Fixture {
        MediaSession media;Impl& m;VideoContext c=context();
        Fixture():m(*media.m){media.shutdown();m.availability.available=true;m.abort=false;media.configure(c);}
        ~Fixture(){for(auto& [k,t]:m.tracks){(void)k;m.destroy(*t);}m.tracks.clear();}
        void receive(std::initializer_list<VideoTrackKey> list){c.receiving.clear();for(auto k:list)c.receiving.push_back(k.window);media.configure(c);}
        void clear(){m.incoming.clear();m.incomingBytes=0;}
        void fakeLive(VideoTrackKey k,bool sending){
            // A permit-only fixture, not a fake WebRTC object.
            auto t=std::make_unique<Impl::Track>();t->owner=&m;t->key=k;t->sending=sending;t->callbacks->track=t.get();
            t->permit=std::make_shared<std::atomic<bool>>(true);m.permits[k]=t->permit;m.tracks.emplace(k,std::move(t));
        }
    };
    static void run(){
        {
            Fixture f;auto k=key(f.c);
            check(f.media.signal(signal(f.c,k,1)));
            check(f.m.incoming.empty()&&f.m.deferred.size()==1&&f.m.tracks.empty()&&f.m.permits.empty());
            f.receive({k});check(f.m.deferred.empty()&&f.m.incoming.size()==1&&f.m.incoming.front().kind==VideoSignalKind::Offer);
            pass("Offer before metadata is held, released once, never allocates pipeline early");
        }
        {
            Fixture f;auto k=key(f.c);check(f.media.signal(signal(f.c,k,1)));
            check(f.media.signal(signal(f.c,k,2,VideoSignalKind::Ice)));
            f.receive({k});check(f.media.signal(signal(f.c,k,3,VideoSignalKind::Ice)));
            check(f.m.incoming.size()==3);unsigned serial=1;for(auto& s:f.m.incoming)check(s.serial==serial++);
            pass("held Offer and ICE precede newer ICE arriving after metadata");
        }
        {
            Fixture f;auto a=key(f.c),b=key(f.c,2,2,2);f.receive({a});
            check(f.media.signal(signal(f.c,b,1)));check(f.media.signal(signal(f.c,a,2)));
            check(f.m.incoming.size()==1&&f.m.incoming.front().key==a&&f.m.deferred.size()==1);
            pass("missing metadata for one window does not block the other window");
        }
        {
            Fixture f;auto k=key(f.c);check(f.media.signal(signal(f.c,k,1)));
            f.m.releaseDeferred(Clock::now()+6s);check(f.m.deferred.empty()&&f.m.remoteGrants.empty()&&f.m.deferredBytes==0);
            f.receive({k});check(f.m.incoming.empty());pass("metadata timeout expires without allocating or resurrecting a Track");
        }
        {
            Fixture f;auto k=key(f.c);check(f.media.signal(signal(f.c,k,1)));
            auto deadline=f.m.deferred.front().deadline;
            check(f.media.signal(signal(f.c,k,2,VideoSignalKind::Ice)));check(f.m.deferred.back().deadline==deadline);
            pass("trickle messages cannot extend the metadata deadline");
        }
        {
            Fixture f;auto k=key(f.c);check(f.media.signal(signal(f.c,k,1)));check(!f.media.signal(signal(f.c,k,1)));
            check(!f.media.signal(signal(f.c,k,2)));check(f.m.deferred.size()==1);pass("serial replay and same-incarnation duplicate Offer rejected");
        }
        {
            Fixture f;auto k=key(f.c);auto wrong=signal(f.c,k,1);wrong.sender=f.c.local;check(!f.media.signal(wrong));
            wrong=signal(f.c,k,1);wrong.key.window.generation[0]=9;check(!f.media.signal(wrong));
            check(f.m.lastSignal==0&&f.m.deferred.empty());pass("wrong sender/world never enters deferred queue or advances serial");
        }
        {
            Fixture f;auto k=key(f.c);check(!f.media.signal(signal(f.c,k,1,VideoSignalKind::Ice)));
            check(f.m.deferred.empty());pass("ICE without a scoped Offer is not buffered");
        }
        {
            Fixture f;auto k=key(f.c);check(f.media.signal(signal(f.c,k,1)));
            check(f.media.signal(signal(f.c,k,2,VideoSignalKind::Stop)));
            check(f.m.deferred.size()==1&&f.m.deferred.front().signal.kind==VideoSignalKind::Stop);
            check(!f.media.signal(signal(f.c,k,3)));f.receive({k});
            check(f.m.incoming.size()==1&&f.m.incoming.front().kind==VideoSignalKind::Stop);
            pass("Stop cancels held Offer/ICE and prevents same-key revival");
        }
        {
            Fixture f;auto k=key(f.c);f.receive({k});check(f.media.signal(signal(f.c,k,1)));
            check(f.media.signal(signal(f.c,k,2,VideoSignalKind::Stop)));
            check(f.m.incoming.size()==1&&f.m.incoming.front().kind==VideoSignalKind::Stop);
            pass("Stop before worker allocation removes queued Offer");
        }
        {
            Fixture f;auto k=key(f.c);f.receive({k});check(f.media.signal(signal(f.c,k,1)));f.clear();
            f.receive({});check(f.m.retiredPublication==1&&f.m.remoteGrants.empty());
            f.receive({k});check(!f.media.signal(signal(f.c,k,2)));
            auto fresh=key(f.c,1,2,2);check(f.media.signal(signal(f.c,fresh,3)));
            pass("metadata removal retires old consent; regrant requires fresh publication");
        }
        {
            Fixture f;auto a=key(f.c,1,1,1),b=key(f.c,2,9,2);f.receive({a,b});
            check(f.media.signal(signal(f.c,b,1)));f.clear();f.receive({a});
            check(f.m.retiredPublication==9);check(f.media.signal(signal(f.c,a,2)));
            pass("retirement does not invalidate an older still-published window");
        }
        {
            Fixture f;auto a=key(f.c),b=key(f.c,3,3,3);
            check(f.media.signal(signal(f.c,b,1,VideoSignalKind::Failure)));
            check(f.media.signal(signal(f.c,a,2)));f.receive({a,b});
            check(f.m.incoming.size()==2&&f.m.retiredPublication==0);
            pass("third-window Unavailable before metadata cannot poison first two offers");
        }
        {
            Fixture f;auto old=key(f.c),fresh=key(f.c,1,2,2);
            check(f.media.signal(signal(f.c,old,1)));check(f.media.signal(signal(f.c,fresh,2)));
            check(f.m.deferred.size()==1&&f.m.deferred.front().signal.key==fresh);
            check(f.media.signal(signal(f.c,old,3,VideoSignalKind::Stop)));check(f.m.remoteGrants.at(fresh.window)==fresh);
            pass("new incarnation replaces held old Offer and survives stale Stop");
        }
        {
            Fixture f;auto k=key(f.c);check(f.media.signal(signal(f.c,k,1)));f.c.world[0]=10;f.media.configure(f.c);
            check(f.m.deferred.empty()&&f.m.remoteGrants.empty()&&f.m.receiveFloor.empty()&&f.m.lastSignal==0);
            check(!f.media.signal(signal(context(),k,2)));auto fresh=key(f.c);check(f.media.signal(signal(f.c,fresh,1)));
            pass("world change clears pending data and resets serial for a new scoped session");
        }
        {
            Fixture f;auto k=key(f.c);check(f.media.signal(signal(f.c,k,1)));f.media.stop();
            check(f.m.deferred.empty()&&f.m.incoming.empty()&&f.media.images().empty()&&f.media.captures().empty());
            check(!f.media.signal(signal(f.c,k,2)));pass("stop disables admission and clears all pending metadata data");
        }
        {
            Fixture f;auto k=key(f.c);check(f.media.signal(signal(f.c,k,1)));
            for(unsigned n=2;n<=64;++n)check(f.media.signal(signal(f.c,k,n,VideoSignalKind::Ice)));
            check(f.m.deferred.size()==64);check(!f.media.signal(signal(f.c,k,65,VideoSignalKind::Ice)));
            check(f.m.deferred.size()<=64&&f.m.deferredBytes<=MaxVideoSignalQueueBytes);
            pass("combined incoming/deferred 64-message budget remains bounded");
        }
        {
            Fixture f;for(unsigned n=1;n<=5;++n){auto s=signal(f.c,key(f.c,n,n,n),n);s.body=std::string(MaxVideoSignal,'a');check(f.media.signal(std::move(s)));}
            auto s=signal(f.c,key(f.c,6,6,6),6);s.body=std::string(MaxVideoSignal,'a');check(!f.media.signal(std::move(s)));
            check(f.m.deferredBytes<=MaxVideoSignalQueueBytes);pass("48 KiB SDP and aggregate 256 KiB budget retained");
        }
        {
            Fixture f;auto a=key(f.c,1,1,1,true),b=key(f.c,2,2,2,true),c=key(f.c,3,3,3,true);
            f.c.sending={{a.window,1,128,64},{b.window,2,128,64},{c.window,3,128,64}};f.media.configure(f.c);
            f.fakeLive(a,true);f.fakeLive(b,true);f.c.sending.erase(f.c.sending.begin()+1);f.media.configure(f.c);
            check(f.m.permits[a]->load()&&!f.m.permits[b]->load());f.fakeLive(c,true);
            auto fresh=key(f.c,2,4,4,true);f.c.sending.push_back({fresh.window,4,128,64});f.media.configure(f.c);
            check(f.m.permits[a]->load()&&!f.m.permits[c]->load());
            pass("lexicographic two-slot policy: 1/2 -> 1/3 -> 1/2 without raising limit");
        }
        {
            Fixture f;auto k=key(f.c,1,1,1,true);f.c.sending={{k.window,1,128,64}};f.media.configure(f.c);f.fakeLive(k,true);
            auto& t=*f.m.tracks.at(k);f.m.emit(t,VideoSignalKind::Offer,"v=0\r\n");f.media.revoke(k.window);
            check(f.media.takeSignals().empty());pass("unsent SDP/ICE of a revoked permit is not returned to TCP");
        }
        {
            Fixture f;unsigned serial=0;
            for(unsigned n=1;n<=1000;++n){auto k=key(f.c,n,n,n);f.receive({k});check(f.media.signal(signal(f.c,k,++serial)));f.clear();f.receive({});}
            check(f.m.remoteGrants.empty()&&f.m.receiveFloor.empty()&&f.m.deferred.empty()&&f.m.lifecycle.size()<=128);
            check(f.m.retiredPublication==1000);pass("1000 metadata lifecycles retain no old ID map or unbounded log");
        }
        {
            Fixture f;auto k=key(f.c);f.receive({k});check(f.media.signal(signal(f.c,k,1)));f.clear();f.fakeLive(k,false);
            auto& t=*f.m.tracks.at(k);auto ref=t.callbacks;f.m.destroy(t);check(!ref->track&&f.m.permits.empty());
            f.m.tracks.clear();pass("destruction closes callback gate and erases only its exact permit");
        }
        {
            Fixture f;auto a=key(f.c),b=key(f.c,2,2,2);f.receive({a,b});f.fakeLive(a,false);f.fakeLive(b,false);
            auto closed=f.m.tracks.at(a)->callbacks;auto live=f.m.tracks.at(b)->callbacks;
            f.m.permits.at(a)->store(false);f.m.retireClosed(f.c);
            check(f.m.tracks.size()==1&&f.m.tracks.contains(b)&&!closed->track&&live->track);
            check(!f.m.permits.contains(a)&&f.m.permits.at(b)->load());
            pass("closed RX slot is fully retired before a replacement can be allocated");
        }
        std::cout<<groups<<"/23 admission/lifecycle groups passed (NO live ICE or pipeline)\n";
    }
};
}
int main(){try{hyprwalk::mp::media::MediaSessionTestAccess::run();return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
