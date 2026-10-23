#include <hyprwalk/mp/WindowMenu.hpp>
#include <algorithm>
#include <charconv>

namespace hyprwalk::mp {
namespace {
constexpr std::string_view Root="online-windows";
std::vector<std::string> split(std::string_view t){std::vector<std::string> o;while(true){auto n=t.find(':');o.emplace_back(t.substr(0,n));if(n==t.npos)break;t.remove_prefix(n+1);if(o.size()>8)return {};}return o;}
std::uint64_t number(std::string_view t){std::uint64_t n=0;auto p=std::from_chars(t.data(),t.data()+t.size(),n);return p.ec==std::errc{}&&p.ptr==t.data()+t.size()?n:UINT64_MAX;}
OnlineItem link(std::string label,std::string hint,std::string page){OnlineItem i;i.label=std::move(label);i.hint=std::move(hint);i.page=std::move(page);return i;}
OnlineItem command(std::string label,std::string hint,std::string target,bool disabled=false,bool on=false){OnlineItem i;i.label=std::move(label);i.hint=std::move(hint);i.action=OnlineAction::WindowCommand;i.target=std::move(target);i.disabled=disabled;i.on=on;return i;}
OnlineItem details(std::string label,std::string hint,std::string target){auto i=command(std::move(label),std::move(hint),std::move(target));i.action=OnlineAction::WindowDetails;return i;}
std::string label(const WindowMetadata& w){return w.title.empty()?(w.application.empty()?"Untitled window":w.application):w.title;}
std::string token(const WindowSync& s,std::string_view verb,std::uint64_t id=0){return s.generationToken()+":"+std::string(verb)+":"+std::to_string(id);}
}
OnlinePage windowMenuPage(std::string_view page,const WindowSync& s,std::string_view owner){
    const auto root=std::string(Root);const auto generation=s.generationToken();
    if(page==Root)return {"Windows",{
        link("My Shared Windows",std::to_string(s.publication().windows.size())+" permitted windows",root+":shared:"+generation+":0"),
        link("Select Windows",s.videoAvailable()&&s.videoEnabled()?"Choose windows to share live video":std::to_string(s.local().size())+" metadata-only sources",root+":local:"+generation+":0"),
        link("Sharing Mode",sharingModeName(s.mode()),root+":mode"),
        link("Remote Windows",std::to_string(s.remote().size())+" from "+std::string(owner),root+":remote:"+generation+":0"),
        link("Debug","Privacy, errors and video",root+":debug")
    }};
    if(page==root+":mode")return {"Sharing Mode",{
        command("Off","Stop metadata and video capture",token(s,"off"),false,s.mode()==SharingMode::Off),
        command("Selected Windows",s.videoAvailable()&&s.videoEnabled()?"Selected titles + live image":"Selected metadata; new windows private",token(s,"selected"),!s.ready(),s.mode()==SharingMode::Selected),
        link("All Windows","Requires confirmation",root+":all:"+generation),
        details("Limits","16 published / 256 local","limits")
    }};
    if(page==root+":video")return {"Window Video",{
        command(s.videoEnabled()?"Stop publishing video":"Enable publishing video","Only my permitted sources; receiving unchanged",token(s,"video"),!s.videoAvailable(),s.videoEnabled()),
        details("Backend",s.videoSummary(),"media"),
        details("Limits","Variable source cadence; optional 1–30 FPS cap; max 8192x8192","video-limits")
    }};
    if(page==root+":debug")return {"Debug",{
        details("Errors",s.error().empty()?"None":"View window, video and sharing errors","errors"),
        details("Privacy","Sharing scope and consent","privacy"),
        link("Window Video",s.videoAvailable()?"VP8 available":"Video unavailable",root+":video")
    }};
    const auto p=split(page);
    if(p.size()<3 || p[0]!=Root)return {};
    if(p[2]!=generation||generation.empty())return {"Windows",{details("Session changed","Reconnect and reopen this page","session")}};
    if(p[1]=="all")return {"Publish ALL eligible windows?",{
        command("Confirm All Windows",s.videoAvailable()&&s.videoEnabled()?"Live video, including future windows":"Metadata, including future windows",token(s,"all"),!s.ready()),
        link("Cancel","Keep current sharing mode",root+":mode"),
        details("What is included?","Read before confirming","privacy")
    }};
    if(p.size()!=4)return {};
    const auto id=number(p[3]);
    if(p[1]=="item"){
        const auto* w=s.findRemote(id);
        if(!w)return {"Remote window closed",{link("Remote Windows","Refresh list",root+":remote:"+generation+":0")}};
        return {label(w->metadata),{
            command(w->receive.hidden?"Show":"Hide",w->receive.hidden?"Hidden locally":"Visible only for you",token(s,"hide",id)),
            command("Show controls above window","Receiver-local selection",token(s,"choose",id)),
            details("Owner",std::string(owner),token(s,"info",id)),
            details("Application",w->metadata.application.empty()?"Unknown":w->metadata.application,token(s,"info",id)),
            details("Video status",w->video,token(s,"info",id))
        }};
    }
    if(p[1]!="local"&&p[1]!="shared"&&p[1]!="remote")return {};
    std::vector<OnlineItem> rows;
    if(p[1]=="remote")for(const auto& w:s.remote())rows.push_back(link(label(w.metadata),w.receive.hidden?"Hidden locally":std::string(owner)+" - "+w.video,root+":item:"+generation+":"+std::to_string(w.key.window)));
    else for(const auto& w:s.local()){
        const bool published=s.mode()==SharingMode::All||(s.mode()==SharingMode::Selected&&s.selected(w.id));
        if(p[1]=="shared"&&!published)continue;
        rows.push_back(command(label(w),published?s.localVideo(w.id):s.videoAvailable()&&s.videoEnabled()?"Publish live video":"Publish metadata only",
            token(s,"select",w.id),!s.ready()||s.mode()!=SharingMode::Selected,published));
    }
    constexpr std::size_t PerPage=6;
    const auto count=std::max<std::size_t>(1,(rows.size()+PerPage-1)/PerPage);
    const auto index=std::min<std::uint64_t>(id,count-1);
    OnlinePage view{p[1]=="remote"?"Remote Windows":p[1]=="shared"?"My Shared Windows":"Select Windows",{}};
    for(std::size_t i=index*PerPage;i<std::min(rows.size(),(index+1)*PerPage);++i)view.items.push_back(rows[i]);
    if(view.items.empty())view.items.push_back(details("No windows",p[1]=="remote"?"Peer has not published":"Off by default; choose Selected or All","privacy"));
    if(index)view.items.push_back(link("Previous",std::to_string(index)+" / "+std::to_string(count),root+":"+p[1]+":"+generation+":"+std::to_string(index-1)));
    if(index+1<count)view.items.push_back(link("Next",std::to_string(index+2)+" / "+std::to_string(count),root+":"+p[1]+":"+generation+":"+std::to_string(index+1)));
    return view;
}
std::string windowMenuAction(std::string_view t,WindowSync& s){
    const auto p=split(t);
    if(p.size()!=3||p[0]!=s.generationToken())return "Session changed; reopen Windows.";
    const auto id=number(p[2]);bool ok=false;
    if(p[1]=="off")ok=s.setMode(SharingMode::Off);
    else if(p[1]=="selected")ok=s.setMode(SharingMode::Selected);
    else if(p[1]=="all")ok=s.setMode(SharingMode::All,true);
    else if(p[1]=="video"){s.videoEnable(!s.videoEnabled());ok=true;}
    else if(p[1]=="select")ok=s.select(id,!s.selected(id));
    else if(const auto* w=s.findRemote(id)){
        if(p[1]=="hide")ok=s.hide(id,!w->receive.hidden);
        else if(p[1]=="choose")ok=s.choose(id);
    }
    return ok?std::string{}:s.error().empty()?"Window is no longer available.":s.error();
}
std::string windowMenuDetails(std::string_view t,const WindowSync& s,std::string_view owner){
    if(t=="media")return s.videoSummary()+"\n\nVP8 WebRTC replaces the placeholder only after decoded frames arrive. Publishing video can be stopped without stopping received windows or metadata. Off/Selected/All consent always takes priority.\n\nFor stream and connection diagnostics: hyprctl hyprwalk multiplayer video stats";
    if(t=="video-limits")return "Capture follows actual Wayland surface commits by default (variable FPS); an optional explicit cap can be configured. Source dimensions are preserved up to the 8192x8192 safety ceiling. The encoder uses software VP8 and a maximum 2.5 Mbps target at 1920x1080 and larger; smaller sources use a resolution-scaled target with a 300 kbps floor. This target does not guarantee actual bitrate, and high-resolution or motion-heavy sources may need a larger configured cap. Up to 16 sending and 16 receiving sources are permitted, so many simultaneous streams can use substantial CPU and network bandwidth.\n\nOffline configuration: multiplayer video config <fps> <width> <height> <bitrate> <streams>. FPS 0 follows source cadence; explicit caps remain 1–30 FPS. Dimensions have a 64x64 minimum and 8192x8192 maximum, with a bitrate ceiling of 12 Mbps per stream. The 3D render FPS is independent from capture FPS.";
    if(t=="errors"){
        std::string text=s.error();
        const auto append=[&](const std::string& title,const std::string& state){if(state.starts_with("Error")||state.starts_with("Unavailable"))text+=(text.empty()?"":"\n")+title+": "+state;};
        for(const auto& w:s.publication().windows)append(label(w),s.localVideo(w.id));
        for(const auto& w:s.remote())append(label(w.metadata),w.video);
        if(s.videoSummary().starts_with("Error"))text+=(text.empty()?"":"\n")+s.videoSummary();
        return text.empty()?"No window errors.":text;
    }
    if(t=="limits")return "At most 16 windows per publisher, 256 local candidates. All stops sharing if the limit is exceeded; it never silently exposes an arbitrary subset.\n\nTitles: 120 UTF-8 bytes. App IDs: 64 bytes, longer IDs are hashed. No network file paths or input messages.";
    if(t=="privacy")return "Off is the default and stops metadata and local video capture. Selected publishes only individually chosen eligible windows. With video enabled and negotiated, selection includes the live image, title and application ID. Confirmed All also includes future eligible windows. Stop publishing video keeps metadata but stops images; enabling video starts images of already permitted windows.\n\nOnly mapped ordinary top-level panels currently in the owner's 3D scene are eligible. Layers, popups, shell, lock surfaces and noScreenShare windows are excluded.\n\nContent sensitivity cannot be inferred for every ordinary application. Use Selected and the compositor no-screen-share rule for private apps.\n\nLock/away/exit/disconnect reset consent to Off. Both Host and Join can publish and receive simultaneously.";
    if(t=="session")return "Window controls are scoped to a connected, admitted world and peer supporting the Windows capability. Reopen this page after reconnect.";
    const auto p=split(t);
    if(p.size()!=3||p[0]!=s.generationToken())return "Stale window selection.";
    const auto* w=s.findRemote(number(p[2]));if(!w)return "Window withdrawn or disconnected.";
    return label(w->metadata)+"\nOwner: "+std::string(owner)+"\nOwner ID: "+idString(w->key.owner)+"\nWindow: "+std::to_string(w->key.window)+"\nApplication: "+w->metadata.application+"\n\nNo remote input.\nVideo: "+w->video;
}
} // namespace hyprwalk::mp
