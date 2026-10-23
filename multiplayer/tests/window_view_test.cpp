#include "remote_windows.hpp"
#include <hyprwalk/mp/WindowMenu.hpp>
#include <cairo.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace hyprwalk;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__func__)+":"+std::to_string(__LINE__)+": " #x);}while(0)
mp::Id id(int i){mp::Id a{};a[0]=i;return a;}
mp::Status status(){mp::Status s;s.state=mp::State::Connected;s.world=mp::WorldState::Ready;s.capabilities=15;s.session=id(1);s.localId=id(2);s.peerId=id(3);s.worldTicket=id(4);return s;}
mp::WindowMetadata window(uint64_t n){return {n,"org.example.Player",n==1?"Video player - <plain text>":"Second window",{{-1,2,0},{1,0,0},{0,-1,0},2,1,800,400}};}
mp::WindowSnapshot snapshot(std::vector<mp::WindowMetadata> w,uint64_t revision){return {id(1),id(3),id(4),revision,std::move(w)};}
void png(const SRemoteWindowImage& im,const std::filesystem::path& path){
 std::vector<uint32_t> argb(im.width*im.height);
 for(size_t p=0;p<argb.size();++p){auto i=p*4;argb[p]=(uint32_t(im.rgba[i+3])<<24)|(uint32_t(im.rgba[i])<<16)|(uint32_t(im.rgba[i+1])<<8)|im.rgba[i+2];}
 auto* surface=cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(argb.data()),CAIRO_FORMAT_ARGB32,im.width,im.height,im.width*4);
 CHECK(cairo_surface_write_to_png(surface,path.c_str())==CAIRO_STATUS_SUCCESS);cairo_surface_destroy(surface);
}
int main(int argc,char**argv){try{
 CHECK(argc==2);std::filesystem::create_directories(argv[1]);
 mp::WindowSync s;s.session(status());s.receive({snapshot({window(1),window(2)},1),false});
 CRemoteWindowView view;view.update(s,"stepan",0);CHECK(view.frames().size()==2);auto initial=view.frames()[0];
 CHECK(initial.panel->width==512&&initial.panel->height==256&&initial.panel->rgba.size()==512*256*4);
 png(*initial.panel,std::filesystem::path(argv[1])/"placeholder.png");
 for(uint64_t n=2;n<102;++n){auto w=window(1);w.geometry.origin[0]=float(n)/50;s.receive({snapshot({w,window(2)},n),false});view.update(s,"stepan",0);CHECK(view.frames()[0].panel==initial.panel);CHECK(view.frames()[0].geometry.origin[0]==float(n)/50);}
 s.hide(1,true);view.update(s,"stepan",0);CHECK(view.frames().size()==1&&view.frames()[0].key.window==2);
 s.hide(1,false);view.update(s,"stepan",0);CHECK(view.frames().size()==2);
 auto w=window(2);w.title="\\xd0\\x9e\\xd0\\xba\\xd0\\xbd\\xd0\\xbe / <b>not markup</b>";s.receive({snapshot({w},103),false});view.update(s,"egor",2);png(*view.frames()[0].panel,std::filesystem::path(argv[1])/"unicode-plain-text.png");
 for(uint64_t i=104;i<204;++i){s.receive({snapshot({window(i)},i),false});view.update(s,"peer",i);CHECK(view.frames().size()==1);s.hide(i,true);view.update(s,"peer",0);CHECK(view.frames().empty());}
 s.session({});view.update(s,"peer",0);CHECK(view.frames().empty());
 s.session(status());s.receive({snapshot({window(1)},1),false});view.update(s,"new peer",0);CHECK(view.frames()[0].key.window==1);
 view.clear();CHECK(view.frames().empty());
 std::cout<<"PASS actual Cairo/Pango video-only placeholder: pose updates reuse rasters, visibility is receiver-local, Unicode plain text, close/reconnect and bounded view data\\n";
 std::cout<<"NOT a Hyprland or GL test; images are CPU rasters from production remote_windows.cpp.\\n";
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\\n';return 1;}}
