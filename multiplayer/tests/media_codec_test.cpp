// Synthetic pixels ONLY in this standalone codec probe, never a production source.
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <cstring>
using namespace std::chrono_literals;
int main(){
 GstElement* pipe=nullptr;GstElement* src=nullptr;GstElement* sink=nullptr;
 try {
  GError* error=nullptr;if(!gst_init_check(nullptr,nullptr,&error))throw std::runtime_error("gst_init failed");
  // Same appsrc/software VP8/RTP/raw RGBA chain as the production backend, without
  // ICE/DTLS. This proves the codec/caps path, NOT WebRTC transport or Hyprland capture.
  pipe=gst_parse_launch("appsrc name=source is-live=true format=time do-timestamp=true block=false max-buffers=1 leaky-type=downstream ! queue max-size-buffers=1 max-size-time=0 max-size-bytes=0 leaky=downstream ! videoconvert ! video/x-raw,format=I420 ! vp8enc deadline=1 cpu-used=10 threads=1 target-bitrate=500000 keyframe-max-dist=30 ! rtpvp8pay pt=96 picture-id-mode=15-bit mtu=1200 ! queue max-size-buffers=512 max-size-time=0 max-size-bytes=4194304 leaky=no ! rtpvp8depay ! vp8dec threads=1 ! videoconvert ! video/x-raw,format=RGBA ! appsink name=decoded sync=false max-buffers=1 drop=true enable-last-sample=false",&error);
  if(error||!pipe)throw std::runtime_error(error?error->message:"pipeline allocation failed");
  src=gst_bin_get_by_name(GST_BIN(pipe),"source");sink=gst_bin_get_by_name(GST_BIN(pipe),"decoded");
  if(!src||!sink)throw std::runtime_error("missing appsrc/appsink");
  if(gst_element_set_state(pipe,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE)throw std::runtime_error("PLAYING failed");
  unsigned total=0;
  for(const auto [width,height]:std::vector<std::pair<int,int>>{{128,64},{64,128},{96,96}}){
   auto* caps=gst_caps_new_simple("video/x-raw","format",G_TYPE_STRING,"RGBA","width",G_TYPE_INT,width,"height",G_TYPE_INT,height,"framerate",GST_TYPE_FRACTION,15,1,nullptr);
   gst_app_src_set_caps(GST_APP_SRC(src),caps);gst_caps_unref(caps);
   unsigned matched=0;const auto end=std::chrono::steady_clock::now()+5s;
   while(matched<5&&std::chrono::steady_clock::now()<end){
    std::vector<unsigned char> rgba(std::size_t(width)*height*4,255);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto at=std::size_t(y*width+x)*4;rgba[at]=y<height/2?240:10;rgba[at+1]=20;rgba[at+2]=y<height/2?10:240;}
    auto* b=gst_buffer_new_allocate(nullptr,rgba.size(),nullptr);gst_buffer_fill(b,0,rgba.data(),rgba.size());if(gst_app_src_push_buffer(GST_APP_SRC(src),b)!=GST_FLOW_OK)throw std::runtime_error("push failed");
    auto* sample=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),100000000);
    if(sample){int w=0,h=0;const auto* desc=gst_caps_get_structure(gst_sample_get_caps(sample),0);gst_structure_get_int(desc,"width",&w);gst_structure_get_int(desc,"height",&h);
      auto* buffer=gst_sample_get_buffer(sample);std::vector<unsigned char> pixels(gst_buffer_get_size(buffer));gst_buffer_extract(buffer,0,pixels.data(),pixels.size());
      if(w==width&&h==height){if(pixels.size()!=rgba.size())throw std::runtime_error("RGBA packing mismatch");
       const auto top=std::size_t(width*(height/4)+width/2)*4,bottom=std::size_t(width*(3*height/4)+width/2)*4;
       if(!(pixels[top]>190&&pixels[top+2]<60&&pixels[bottom]<60&&pixels[bottom+2]>190))throw std::runtime_error("orientation/color mismatch");++matched;++total;}
      gst_sample_unref(sample);
    }
    std::this_thread::sleep_for(30ms);
   }
   if(matched<5)throw std::runtime_error("no decoded frames after resize");
   std::cout<<"PASS actual VP8 encode/RTP/decode "<<width<<"x"<<height<<" top/bottom colors; "<<matched<<" frames\n";
  }
  gst_element_set_state(pipe,GST_STATE_NULL);gst_object_unref(src);gst_object_unref(sink);gst_object_unref(pipe);
  std::cout<<"PASS "<<total<<" decoded frames; NO WebRTC/Hyprland claim\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';if(pipe)gst_element_set_state(pipe,GST_STATE_NULL);if(src)gst_object_unref(src);if(sink)gst_object_unref(sink);if(pipe)gst_object_unref(pipe);return 1;}
}
