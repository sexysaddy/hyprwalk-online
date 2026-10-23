// Uses actual menu.cpp/control.cpp and Cairo/Pango. Only SHudImage's renderer include
// and UNCALLED avatar methods are isolated from Hyprland; see evidence README.
#include "menu.hpp"
#include "control.hpp"
#include <cairo.h>
#include <pango/pangocairo.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace hyprwalk {
const char* expressionPresetName(int) { std::abort(); }
float CAvatarAnimator::slider(int) const { std::abort(); }
float CAvatarAnimator::sliderY(int) const { std::abort(); }
namespace gltf { std::string lower(std::string s) { std::ranges::transform(s, s.begin(), [](unsigned char c) { return (char)std::tolower(c); }); return s; } }
}
using namespace hyprwalk;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__func__) + ":" + std::to_string(__LINE__) + ": " #x); } while (0)
struct Fixture {
#ifdef HYPRWALK_MULTIPLAYER
    mp::OnlineMenu form;
    mp::Status status;
#endif
    int w=1920, h=1080; float scale=1;
    CActionMenu menu{[this](const std::string& id) {
        if (id == "main") return actionPage(id, {});
#ifdef HYPRWALK_MULTIPLAYER
        const auto p = form.page(id, status, "egor");
        SMenuPage out{.title=p.title};
        for (const auto& row:p.items)
            out.items.push_back({.label=row.label,.hint=row.hint,.page=row.page,.action=MA_ONLINE,.arg=static_cast<int>(row.action),
                                 .target=row.detail.empty()?"":id+"#"+row.label,.on=row.on,.disabled=row.disabled});
        return out;
#else
        return SMenuPage{};
#endif
    }};
    void draw() { menu.update(.15f,w,h,scale); }
    void save(const std::string& file) {
        draw(); const auto hud = menu.hud(); CHECK(hud.pixels && hud.w>0);
        cairo_surface_t* surface = cairo_image_surface_create_for_data((unsigned char*)hud.pixels->data(), CAIRO_FORMAT_ARGB32, hud.w, hud.h, hud.w*4);
        CHECK(cairo_surface_write_to_png(surface,file.c_str())==CAIRO_STATUS_SUCCESS); cairo_surface_destroy(surface);
    }
    void picked(const std::optional<SMenuItem>& it) {
#ifdef HYPRWALK_MULTIPLAYER
        if (!it) return;
        const auto a=static_cast<mp::OnlineAction>(it->arg);
        if (a==mp::OnlineAction::ToggleRole) CHECK(form.toggleRole(status).empty());
        else if(a==mp::OnlineAction::EditAddress || a==mp::OnlineAction::EditPort) {
            const auto field=a==mp::OnlineAction::EditAddress?mp::OnlineField::Address:mp::OnlineField::Port;
            menu.onlineEdit(field,it->label,field==mp::OnlineField::Address?form.address():form.port(),
                [this,field](const std::string& value) { return form.setField(field,value,status); });
        } else if (a==mp::OnlineAction::Details) menu.onlineMessage(it->label,form.details(it->target,status,"egor"));
#else
        (void)it;
#endif
    }
    void key(uint32_t code) { CHECK(menuKey(menu,code,[this](const auto& it){picked(it);})); draw(); }
    void button(uint32_t code) { CHECK(menuButton(menu,code,[this](const auto& it){picked(it);})); draw(); }
};
int main(int argc,char**argv) {
    try {
        const std::filesystem::path out=argc>1?argv[1]:"images"; std::filesystem::create_directories(out);
        Fixture f; CHECK(f.menu.show()); f.draw();
#ifdef HYPRWALK_MULTIPLAYER
        CHECK(f.menu.page().items.size()==10 && f.menu.page().items[8].label=="Avatars" && f.menu.page().items[9].label=="Online");
        f.save((out/"01-root.png").string());
        f.key(11); CHECK(f.menu.path()=="main/online"); CHECK(f.menu.page().items.size()==9);
        f.save((out/"02-online-host.png").string());
        f.key(2); CHECK(f.form.mode()==mp::Role::Client); // 1: Mode
        f.save((out/"03-online-join.png").string());
        f.key(3); CHECK(f.menu.onlineModal()); // 2: Server IP
        f.save((out/"04-ip-editor.png").string());
        f.key(11); CHECK(f.form.address()=="127.0.0.1"); // replaces selection with 0, not a root shortcut
        f.key(28); CHECK(f.menu.onlineModal()); // invalid IP remains in field
        f.save((out/"05-ip-error.png").string());
        f.key(1); CHECK(!f.menu.onlineModal() && f.menu.open()); CHECK(f.form.address()=="127.0.0.1");
        f.key(3);
        for(auto k:{2U,10U,3U,52U,2U,7U,9U,52U,11U,52U,3U}) f.key(k);
        f.key(28); CHECK(!f.menu.onlineModal() && f.form.address()=="192.168.0.2");
        f.key(4); CHECK(f.menu.onlineModal()); // 3: Port
        f.key(7);f.key(6);f.key(6);f.key(4);f.key(6); // 65535
        const double radius=.28*f.h/f.scale;
        f.menu.move(-.42*radius,.63*radius); f.button(0x110); // Apply with mouse
        CHECK(!f.menu.onlineModal() && f.form.port()=="65535");
        f.key(3); f.key(2); f.menu.hide(); CHECK(!f.menu.onlineModal() && f.form.address()=="192.168.0.2");
        CHECK(f.menu.show("online")); f.draw();
        f.key(7); CHECK(f.menu.path()=="main/online/online-status"); // 6: Status
        f.save((out/"06-status.png").string());
        f.key(14); CHECK(f.menu.path()=="main/online");
        f.key(8); CHECK(f.menu.path()=="main/online/online-players");
        f.save((out/"07-players-empty.png").string());
        f.button(0x111); CHECK(f.menu.path()=="main/online");
        f.status.state=mp::State::Connected; f.status.workerRunning=true; f.status.peerName="stepan"; f.status.localName="egor";
        f.status.udp.state=mp::UdpState::Ready; f.status.world=mp::WorldState::Ready; f.status.endpoint={"10.70.0.1",45000}; f.draw();
        CHECK(f.menu.page().items[3].disabled && !f.menu.page().items[4].disabled);
        f.key(8); f.save((out/"08-players-connected.png").string()); f.key(14);
        f.key(7); f.save((out/"09-status-connected.png").string());
        f.key(3); CHECK(f.menu.onlineModal()); f.save((out/"10-udp-details.png").string());
        f.key(1); CHECK(!f.menu.onlineModal());
        const auto serial=f.menu.hud().serial;
        for(int i=0;i<100;++i) { ++f.status.udp.playerReceived; f.draw(); }
        CHECK(f.menu.hud().serial==serial); // no Cairo rerasterization on packet-counter-only changes
        f.key(14); f.status.world=mp::WorldState::Rejected; f.status.worldError="GLB content differs. Install the identical embedded GLB on both machines, then reconnect.";
        f.draw(); f.key(9); CHECK(f.menu.onlineModal()); f.save((out/"11-world-error.png").string());
        f.menu.scroll(200); f.draw(); f.button(0x111); CHECK(!f.menu.onlineModal());
        f.key(1); CHECK(!f.menu.open());
        // Long IPv6, live commit rejection, cancellation and callback destruction.
        CHECK(f.menu.show("online")); f.status={};
        const std::string ipv6="ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255";
        f.menu.onlineEdit(mp::OnlineField::Address,"Server IP",ipv6,[](const std::string& ip){return mp::OnlineMenu::validateAddress(ip);});
        f.save((out/"12-ipv6-long.png").string()); f.key(1);
        f.key(4); f.key(2); f.status.state=mp::State::Connecting; f.key(28); CHECK(f.menu.onlineModal() && f.form.port()=="65535"); f.key(1);
        // Rendering at common dimensions/scales is real CPU rasterization, not Hyprland/GPU output.
        f.status={};
        for(auto dims:{std::pair{1280,720},std::pair{1920,1080},std::pair{2560,1440}}) {
            f.w=dims.first;f.h=dims.second;f.scale=f.w==2560?1.5f:1.f;
            f.menu.show(); f.draw(); CHECK(f.menu.hud().w>0);
            f.save((out/("root-"+std::to_string(f.w)+".png")).string());
        }
        int commits=0;
        for(int i=0;i<100;++i){f.menu.show("online");f.menu.onlineEdit(mp::OnlineField::Port,"Port","45000",[&](const std::string&){++commits;return std::string{};});f.menu.hide();}
        CHECK(commits==0);
        std::cout<<"PASS actual radial menu: root 0, 1-9 preserved, online pages, edit/cancel/validation, mouse Apply, status/players/errors, no per-packet redraw, 100 hide cycles\n";
#else
        CHECK(f.menu.page().items.size()==9);
        f.key(11); CHECK(f.menu.path()=="main"); CHECK(!f.menu.show("online"));
        f.save((out/"root-disabled.png").string()); f.key(1); CHECK(!f.menu.open());
        std::cout<<"PASS multiplayer disabled: original nine root items, no Online page, no Core linked\n";
#endif
        std::cout<<"REAL cairo "<<cairo_version_string()<<", Pango "<<pango_version_string()<<"; renderer HUD POD fixture, uncalled avatar trap stubs; no compositor/GL\n";
    } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
