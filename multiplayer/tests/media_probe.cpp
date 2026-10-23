#include "../media/gstreamer/MediaSession.hpp"
#include <filesystem>
#include <iostream>
#include <thread>
using namespace hyprwalk::mp;
int main(){
    const auto availability=media::MediaSession::probe();
    std::cout<<"GStreamer "<<availability.version<<"\n"<<availability.detail<<"\n";
    auto count=[](){return std::distance(std::filesystem::directory_iterator("/proc/self/fd"),std::filesystem::directory_iterator{});};
    const auto before=count();
    for(unsigned i=0;i<20;++i){media::MediaSession session;if(!availability.available&&session.notificationFd()!=-1)return 1;session.stop();session.shutdown();}
    if(count()!=before){std::cerr<<"FD leak after media create/stop/shutdown\n";return 1;}
    std::cout<<"PASS 20 create/stop/shutdown cycles; availability/fd behavior verified\n";
    return 0;
}
