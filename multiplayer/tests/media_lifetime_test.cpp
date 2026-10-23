// White-box tests of EXACT production media gates and GObject closures. This is
// NOT an ICE/DTLS/SRTP connection test. No synthetic runtime plugin is installed.
#define HYPRWALK_MEDIA_INTERNAL_TEST
#include "../media/gstreamer/MediaSession.cpp"
#include <iostream>
#include <source_location>
namespace hyprwalk::mp::media {
struct MediaSessionTestAccess {
 inline static unsigned buffers=0,lists=0;
 static void check(bool b,const std::source_location at=std::source_location::current()){
  if(!b)throw std::runtime_error("media lifetime check at "+std::to_string(at.line()));
 }
 static VideoTrackKey key(const VideoContext& c,bool sender,std::uint64_t pub=1){return {{c.session,sender?c.local:c.peer,c.world,1},pub,pub};}
 static std::shared_ptr<VideoFrame> pixels(const VideoTrackKey& k){auto f=std::make_shared<VideoFrame>();f->key=k;f->width=f->height=2;f->serial=1;f->time=Clock::now();f->rgba.assign(16,255);return f;}
 static void run(){
  MediaSession media;media.shutdown(); // stop any real worker before inspecting policy state
  auto& m=*media.m;
  // Exercise the EXACT production sender bin even without libnice. This detects
  // parser/caps errors that a separate codec test pipeline would not catch.
  GError* parseError=nullptr;
  auto* bin=gst_parse_bin_from_description(senderPipeline(VideoSettings{}).c_str(),TRUE,&parseError);
  if(parseError){const auto why=errorText(parseError);if(bin)gst_object_unref(bin);throw std::runtime_error(why);}
  check(bin);auto* ghost=gst_element_get_static_pad(bin,"src");check(ghost);
  auto* app=gst_bin_get_by_name(GST_BIN(bin),"source");check(app);
  gst_object_unref(app);gst_object_unref(ghost);gst_object_unref(bin);
  std::cout<<"PASS exact production appsrc/VP8/RTP sender bin and ghost src with REAL GStreamer\n";

  parseError=nullptr;bin=gst_parse_bin_from_description(receiverPipeline(),TRUE,&parseError);
  if(parseError){const auto why=errorText(parseError);if(bin)gst_object_unref(bin);throw std::runtime_error(why);}
  check(bin);ghost=gst_element_get_static_pad(bin,"sink");check(ghost);gst_object_unref(ghost);gst_object_unref(bin);
  std::cout<<"PASS exact production VP8 receiver bin and keyframe recovery properties\n";
  {
   // REAL GstPad flow verifies BOTH buffers and buffer lists after permission revocation.
   using Impl=MediaSession::Impl;using Ref=Impl::CallbackRef;Impl::Track track;
   track.owner=&m;track.permit=std::make_shared<std::atomic<bool>>(true);track.callbacks->track=&track;
   auto* source=gst_pad_new("source",GST_PAD_SRC);auto* sink=gst_pad_new("sink",GST_PAD_SINK);check(source&&sink);
   gst_pad_set_chain_function_full(sink,[](GstPad*,GstObject*,GstBuffer* b){++buffers;gst_buffer_unref(b);return GST_FLOW_OK;},nullptr,nullptr);
   gst_pad_set_chain_list_function_full(sink,[](GstPad*,GstObject*,GstBufferList* b){++lists;gst_buffer_list_unref(b);return GST_FLOW_OK;},nullptr,nullptr);
   check(gst_pad_set_active(source,true)&&gst_pad_set_active(sink,true)&&gst_pad_link(source,sink)==GST_PAD_LINK_OK);
   auto id=gst_pad_add_probe(sink,MediaBufferProbe,Impl::packetGate,new Ref(track.callbacks),Impl::releaseProbeData);check(id);
   check(gst_pad_push_event(source,gst_event_new_stream_start("gate-test")));
   auto* caps=gst_caps_new_simple("application/x-rtp","media",G_TYPE_STRING,"video",nullptr);check(gst_pad_push_event(source,gst_event_new_caps(caps)));gst_caps_unref(caps);
   auto* segment=gst_segment_new();gst_segment_init(segment,GST_FORMAT_TIME);check(gst_pad_push_event(source,gst_event_new_segment(segment)));gst_segment_free(segment);
   auto sendBuffer=[&]{check(gst_pad_push(source,gst_buffer_new_allocate(nullptr,4,nullptr))==GST_FLOW_OK);};
   auto sendList=[&]{auto* list=gst_buffer_list_new();gst_buffer_list_insert(list,-1,gst_buffer_new_allocate(nullptr,4,nullptr));check(gst_pad_push_list(source,list)==GST_FLOW_OK);};
   sendBuffer();sendList();check(buffers==1&&lists==1);
   track.permit->store(false);sendBuffer();sendList();check(buffers==1&&lists==1);
   {std::lock_guard guard(track.callbacks->mutex);track.callbacks->track=nullptr;}
   sendBuffer();sendList();check(buffers==1&&lists==1);
   gst_pad_remove_probe(sink,id);gst_pad_unlink(source,sink);gst_pad_set_active(source,false);gst_pad_set_active(sink,false);gst_object_unref(source);gst_object_unref(sink);
   std::cout<<"PASS REAL GstPad: buffers AND buffer lists blocked immediately on revoke and closed lifetime gate\n";
  }
  // TEST ONLY: pure admission methods can be exercised when the ICE plugin is missing.
  // No pipeline, encoder or networking is run under this availability override.
  m.availability.available=true;m.abort=false;m.fault.clear();
  for(bool reversed:{false,true}){
   VideoContext c;c.session[0]=1;c.local[0]=reversed?3:2;c.peer[0]=reversed?2:3;c.world[0]=4;c.enabled=true;
   const auto tx=key(c,true),rx=key(c,false);c.sending={VideoPublication{tx.window,1,128,64}};c.receiving={rx.window};
   media.configure(c);
   auto permit=std::make_shared<std::atomic<bool>>(true);m.permits[tx]=permit;
   check(media.submit(pixels(tx)));check(media.captures().size()==1);
   media.revoke(tx.window);check(!permit->load()&&!media.submit(pixels(tx))&&media.captures().empty()&&m.inputs.empty());
   media.configure(c);m.permits[tx]=std::make_shared<std::atomic<bool>>(true);
   c.sending[0].publication=2;media.configure(c);check(!media.submit(pixels(tx)));
   auto fresh=key(c,true,2);m.permits[fresh]=std::make_shared<std::atomic<bool>>(true);check(media.submit(pixels(fresh)));
   VideoSignal offer;offer.key=rx;offer.sender=c.peer;offer.serial=1;offer.kind=VideoSignalKind::Offer;offer.body="v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\n";
   check(media.signal(offer));m.permits[rx]=std::make_shared<std::atomic<bool>>(true);m.frames[rx.window]=pixels(rx);check(bool(media.frame(rx.window)));
   auto next=offer;next.serial=2;next.key.publication=2;next.key.stream=3;check(media.signal(next));check(!media.frame(rx.window)&&!m.permits[rx]->load());
   auto old=offer;old.serial=3;check(!media.signal(old));
   m.permits[next.key]=std::make_shared<std::atomic<bool>>(true);m.frames[rx.window]=pixels(next.key);
   VideoSignal stop=offer;stop.serial=4;stop.kind=VideoSignalKind::Stop;stop.body="old stop";check(media.signal(stop));check(bool(media.frame(rx.window)));
   stop.key=next.key;stop.serial=5;check(media.signal(stop));check(!media.frame(rx.window));
   next.serial=6;next.key.publication=3;next.key.stream=4;check(media.signal(next));m.permits[next.key]=std::make_shared<std::atomic<bool>>(true);
   auto late=pixels(next.key);late->time-=2s;m.frames[rx.window]=late;check(!media.frame(rx.window));
   c.world[1]=1;media.configure(c);next.serial=7;check(!media.signal(next)&&m.frames.empty()&&m.remoteGrants.empty());
   c.enabled=false;media.configure(c);check(m.inputs.empty()&&media.captures().empty()&&media.images().empty());
  }
  std::cout<<"PASS exact media admission: both owner IDs, immediate revoke, regrant, late Stop/SDP/frame, world reset and stale pixels\n";
  // Actual GObject on-ice-candidate closures: emission races closure teardown. The
  // object is kept alive by the emitter, while production destroy detaches Track.
  for(unsigned cycle=0;cycle<100;++cycle){
   typename MediaSession::Impl::Track track;track.owner=&m;track.key.window.session[0]=1;track.key.window.owner[0]=2;track.key.window.generation[0]=4;track.key.window.window=1;track.key.publication=track.key.stream=1;
   track.permit=std::make_shared<std::atomic<bool>>(true);track.callbacks->track=&track;
   track.rtc=gst_element_factory_make("webrtcbin",nullptr);check(track.rtc);
   using Impl=MediaSession::Impl;using Ref=Impl::CallbackRef;
   track.iceHandler=g_signal_connect_data(track.rtc,"on-ice-candidate",G_CALLBACK(Impl::iceCandidate),new Ref(track.callbacks),Impl::releaseSignalData,static_cast<GConnectFlags>(0));check(track.iceHandler);
   auto* held=static_cast<GstElement*>(gst_object_ref(track.rtc));std::atomic<bool> started=false,done=false;
   std::thread emitter([&]{started=true;while(!done.load())g_signal_emit_by_name(held,"on-ice-candidate",0U,"candidate:1 1 UDP 1 127.0.0.1 45000 typ host");});
   while(!started.load())std::this_thread::yield();
   auto ref=track.callbacks;m.destroy(track);check(!ref->track);
   Impl::iceCandidate(nullptr,0,const_cast<char*>("candidate:late"),&ref);
   check(Impl::packetGate(nullptr,nullptr,&ref)==GST_PAD_PROBE_DROP);
   done=true;emitter.join();gst_object_unref(held);
  }
  std::cout<<"PASS 100 real GObject closure/late-callback teardown races; NOT ICE transport\n";
  VideoContext c;c.session[0]=1;c.local[0]=2;c.peer[0]=3;c.world[0]=4;c.enabled=true;const auto rx=key(c,false);c.receiving={rx.window};media.configure(c);
  VideoSignal signal;signal.key=rx;signal.sender=c.peer;signal.kind=VideoSignalKind::Failure;signal.body="unavailable";
  for(std::uint64_t i=1;i<=MaxVideoSignalQueue;++i){signal.serial=i;check(media.signal(signal));}
  signal.serial=MaxVideoSignalQueue+1;check(!media.signal(signal)&&!media.error().empty()&&media.images().empty()&&media.captures().empty());
  media.shutdown();std::cout<<"PASS media queue overflow fails closed, shutdown without live callbacks\n";
 }
};
}
int main(){try{hyprwalk::mp::media::MediaSessionTestAccess::run();return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
