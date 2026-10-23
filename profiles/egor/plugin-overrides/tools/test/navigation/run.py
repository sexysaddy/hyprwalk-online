#!/usr/bin/env python3
"""Compile and execute the real menu navigation, settings pages and INI methods.

No Hyprland headers are required. The script extracts the unchanged function
bodies from menu.cpp / main.cpp into a standalone C++ fixture. External video,
skybox, config and rendering services are stubs. This is a behavior test of the
extracted code, NOT a full main.cpp/plugin build, Cairo test or video decoder test.
Requires a C++23 compiler with std::format and ASan/UBSan (GCC 14+).
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
main = (ROOT / 'src/main.cpp').read_text()
menu = (ROOT / 'src/menu.cpp').read_text()
header = (ROOT / 'src/menu.hpp').read_text()


def between(text: str, start: str, end: str) -> str:
    a = text.index(start)
    return text[a:text.index(end, a)]


types = between(header, '    // a picture for an item', '    // what the Windows page does')
nav = between(menu, '    bool CActionMenu::show(', '    void CActionMenu::layout(')
keys = between(main, '    // evdev key codes', '    constexpr uint32_t BTN_LEFT_')
defaults = between(main, '    bool modifierKey(', '    // cuts at a UTF-8 boundary')
ranges = between(main, 'float CDesktop3D::settingMin(', 'float CDesktop3D::viewAspectRatio(')
configs = sorted(set(__import__('re').findall(r'g_cfg\w+', ranges)))
config_stubs = 'struct Config { float value() const { return 0; } };\n' + ''.join(f'Config* {k} = nullptr;\n' for k in configs)
constants = __import__('re').findall(r' : ([A-Z][A-Z_0-9]*);', ranges)
config_stubs += ''.join(f'constexpr float {k} = 1.f;\n' for k in dict.fromkeys(constants))
key_label = between(main, 'std::string CDesktop3D::keyLabel(', 'void CDesktop3D::loadSettings(')
setter = between(main, 'void CDesktop3D::setSettingFloat(', 'void CDesktop3D::setSettingBool(')
actions = 'std::string CDesktop3D::action(eMenuAction action) { switch(action) {\n' + between(main, '        case MA_RENDER_SCALE:', '        case MA_VIEWMODEL_PICK_VIDEO:') + 'default: return {}; } }\n'
storage = between(main, 'void CDesktop3D::loadSettings(', 'std::string CDesktop3D::chooseViewmodelVideo(')
pages = between(main, 'SMenuPage CDesktop3D::ownPage(', '    if (id == "apps" || id == "apps/all")')
pages = '\n'.join(line for line in pages.splitlines() if 'const auto icon = ' not in line) + '\n    return p;\n}\n'

preamble = r'''
#include "performance.hpp"
#include "viewport.hpp"
#include "menu_identity.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <numbers>
#include <optional>
#include <ranges>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace hyprwalk {
struct SHudImage;
constexpr float HALF_TURN = std::numbers::pi_v<float>, TAU = 2 * HALF_TURN;
constexpr float INNER = .34f, CURSOR_MAX = .97f;
constexpr auto MORE = "\x01more";
'''
fixture = r'''
void log(const std::string&) {}
template<class... T> void logf(std::format_string<T...>, T&&...) {}
struct CViewmodelVideo {
    static bool available() { return true; }
    void setLoop(bool) {}
    bool start(const std::string&, std::string&) { return true; }
    bool playing() const { return false; }
    bool active() const { return true; }
};
struct CDesktop3D {
    std::unordered_map<std::string,float> m_settingFloats;
    std::unordered_map<std::string,bool> m_settingBools;
    std::unordered_map<std::string,std::string> m_settingStrings;
    std::unordered_map<std::string,uint32_t> m_bindings = defaultBindings();
    std::array<bool,768> m_keys{};
    std::string m_bindingCapture;
    float m_settingsSaveDelay = 0.f;
    std::optional<float> m_sensOverride;
    int requestedFrames = 0;
    void requestFrame() { ++requestedFrames; }
    void setSettingFloat(const std::string&, float);
    std::string action(eMenuAction);
    CViewmodelVideo m_viewmodelVideo;
    struct { std::string name = "test map"; } m_world;
    bool loadSkyboxPixels(const std::string&, std::string&) { return true; }
    float settingMin(const std::string&) const;
    float settingMax(const std::string&) const;
    float settingFloat(const std::string&) const;
    float settingNormalized(const std::string&) const;
    bool settingBool(const std::string&) const;
    uint32_t binding(const std::string& k) const { return m_bindings.at(k); }
    int fpsLimit() const { return performance::normalizeFpsLimit((int)settingFloat("fps_limit")); }
    static std::string keyLabel(uint32_t);
    SMenuPage ownPage(const std::string&);
    void saveSettings();
    void loadSettings();
};
'''
tests = r'''
} // namespace hyprwalk
using namespace hyprwalk;
int main() {
    CDesktop3D d;
    assert(d.settingFloat("render_scale")==100.f);
    assert(d.settingFloat("aspect_ratio")==0.f);
    assert(d.settingFloat("scale_x")==100.f && d.settingFloat("scale_y")==100.f);
    assert(d.settingBool("graphics_antialiasing"));
    d.m_settingFloats["fov"]=103.f;
    d.m_settingFloats["scale_x"]=80.f; d.m_settingFloats["scale_y"]=120.f;
    for(int i=0;i<12;++i) {
        assert(d.action(MA_RENDER_SCALE)=="ok");
        assert(d.settingFloat("render_scale")==RENDER_SCALES[(i+1)%6]);
        assert(d.settingFloat("fov")==103.f);
    }
    for(int i=0;i<10;++i) {
        d.action(MA_ASPECT_RATIO);
        assert(d.settingFloat("aspect_ratio")==float((i+1)%5));
        assert(d.settingFloat("scale_x")==80.f && d.settingFloat("scale_y")==120.f);
        assert(d.settingFloat("fov")==103.f);
    }
    assert(d.requestedFrames==22);
    d.m_settingFloats["aspect_ratio"]=3.f;
    d.setSettingFloat("scale_x",.5f);
    assert(d.settingFloat("scale_x")==125.f && d.settingFloat("scale_y")==120.f);
    assert(d.settingFloat("aspect_ratio")==0.f && d.settingFloat("fov")==103.f);
    assert(d.m_settingsSaveDelay==.7f);
    d.m_settingFloats["aspect_ratio"]=2.f;
    d.setSettingFloat("scale_y",.2f);
    assert(d.settingFloat("aspect_ratio")==0.f && d.settingFloat("scale_y")==80.f);
    d.setSettingFloat("scale_x",NAN); assert(d.settingFloat("scale_x")==125.f);
    std::puts("PASS: actual Aspect/Render Scale actions cycle, request frames, preserve FOV/X/Y; actual X/Y dial setter selects custom aspect");
    CActionMenu m([&](const std::string& id) {
        if (id=="main") return SMenuPage{.title="Root",.items={{.label="Settings",.page="settings"}}};
        return d.ownPage(id);
    }, [&](const SMenuItem& item,float v,float) {
        d.setSettingFloat(item.target,v);
    });
    auto root=d.ownPage("settings");
    assert(root.items.size()==5);
    std::array<std::string,5> names{"Movement","Camera / Display","Graphics","Environment","General"};
    for (size_t i=0;i<names.size();++i) assert(root.items[i].label==names[i]);
    assert(d.ownPage("settings/movement").items.size()==11);
    assert(d.ownPage("settings/controls").items.size()==33);
    assert(d.ownPage("settings/controls/reset").items.size()==32);
    assert(d.ownPage("settings/camera").items.size()==8);
    assert(d.ownPage("settings/viewmodel").items.size()==11);
    assert(d.ownPage("settings/graphics").items.size()==4);
    assert(d.ownPage("settings/performance").items.size()==3);
    assert(m.show("settings"));
    m.pick(0); // movement
    assert(m.page().items.size()==8 && m.page().items.back().label=="More");
    m.pick(7); // second chunk
    const auto movement=m.path();
    assert(m.page().items.size()==4 && m.page().items.back().label=="Controls");
    m.pick(3);
    assert(m.page().title.starts_with("Controls"));
    int binds=0;
    for (;;) {
        const auto items=m.page().items;
        bool more=false;
        for (size_t i=0;i<items.size();++i) {
            if (items[i].action==MA_BIND_CAPTURE) {
                const auto it=m.pick((int)i); assert(it && it->target==items[i].target); ++binds;
            }
            if (items[i].label=="More") more=true;
        }
        if (!more) break;
        m.pick(7);
    }
    assert(binds==32);
    // Last Controls chunk includes the reset page. Back from reset returns here.
    const auto controls=m.path();
    m.pick((int)m.page().items.size()-1);
    assert(m.page().title.starts_with("Reset one binding"));
    m.back(); assert(m.path()==controls);
    while(m.path()!=movement) m.back();
    assert(m.open() && m.page().title.starts_with("Movement"));
    m.back(); m.back(); assert(m.page().title=="Hyprwalk Settings");
    m.pick(1); // camera
    const auto camera=m.path();
    m.pick(7); // viewmodel
    assert(m.page().title.starts_with("Viewmodel"));
    m.pick(7); m.back(); // More -> back to viewmodel chunk 1
    m.back(); assert(m.path()==camera);
    m.pick(0); assert(m.dial() && m.dial()->target=="fov");
    m.scroll(1);
    const float fov=d.m_settingFloats.at("fov");
    m.back(); assert(m.path()==camera && !m.dial());
    assert(d.m_settingFloats.at("fov")==fov);
    m.back(); m.pick(2); // graphics
    const auto graphics=m.path();
    m.pick(3); assert(m.page().title=="Performance");
    auto fps=m.pick(0); assert(fps && fps->action==MA_FPS_LIMIT);
    m.back(); assert(m.path()==graphics);
    // Mouse polar selection and scroll selection use the same real code path.
    m.back(); m.move(0.f,-200.f); assert(m.highlighted()==0); m.pick();
    assert(m.page().title.starts_with("Movement"));
    m.show("settings"); m.scroll(3); assert(m.highlighted()==2); m.pick();
    assert(m.page().title=="Graphics");
    const auto before=m.path();
    for(int slot:{-9,-2,4,8,999}) m.pick(slot);
    assert(m.path()==before);
    // Mixed input including short/long pages under address + undefined sanitizers.
    std::mt19937 rng(0x48595052);
    for(int i=0;i<20000;++i) {
        if(!m.open()) m.show("settings");
        switch(rng()%6) {
            case 0: m.pick((int)(rng()%13)-2); break;
            case 1: m.scroll((int)(rng()%7)-3); break;
            case 2: m.move((int)(rng()%401)-200,(int)(rng()%401)-200); m.pick(); break;
            case 3: m.back(); break;
            case 4: m.show("settings"); break;
            default: m.pick(-1); break;
        }
        assert(m.page().items.size()<=9);
    }
    std::puts("PASS: nested menu, More/back, all 32 bindings, reset page, dials, mouse/wheel/key picks, 20000 mixed inputs");

    // Exercise actual filesystem writing/parsing into a temporary XDG_CONFIG_HOME.
    for(int cap:performance::FPS_LIMITS) for(int aspect=0;aspect<5;++aspect) for(int scale:RENDER_SCALES) {
        CDesktop3D original;
        original.m_settingFloats={{"fps_limit",(float)cap},{"aspect_ratio",(float)aspect},{"render_scale",(float)scale},{"scale_x",80.f},{"scale_y",120.f},
            {"fov",103.f},{"mouse_sensitivity",.0012f},{"ground_friction",7.5f},
            {"skybox_rotation",45.f},{"chroma_threshold",.18f}};
        original.m_settingBools={{"bhop",true},{"air_strafing",false},{"graphics_shadows",false},
            {"graphics_dynamic_lighting",true},{"graphics_antialiasing",false},{"video_loop",true},
            {"video_keep_aspect",true},{"video_enabled",false},{"skybox_enabled",false}};
        original.m_settingStrings={{"skybox_path","/tmp/sky with spaces.png"},{"video_path","/tmp/hands.webm"}};
        std::swap(original.m_bindings["forward"],original.m_bindings["back"]);
        original.saveSettings();
        CDesktop3D restored; restored.loadSettings();
        assert(restored.m_settingFloats==original.m_settingFloats);
        assert(restored.m_settingBools==original.m_settingBools);
        assert(restored.m_settingStrings==original.m_settingStrings);
        assert(restored.m_bindings==original.m_bindings);
        assert(!std::filesystem::exists(std::filesystem::path(std::getenv("XDG_CONFIG_HOME"))/"hyprwalk/settings.ini.tmp"));
    }
    std::puts("PASS: actual saveSettings/loadSettings round-trip, 180 combinations (6 FPS x 5 aspects x 6 scales), FOV/graphics/paths/bind swaps");
}
'''
source = preamble + types + nav + keys + defaults + config_stubs + fixture + ranges + key_label + storage + setter + actions + pages + tests
with tempfile.TemporaryDirectory(prefix='hyprwalk-nav-') as td:
    td=Path(td)
    cpp=td/'navigation.cpp'; cpp.write_text(source)
    compiler=os.environ.get('CXX','c++')
    flags=['-std=c++23','-O1','-g','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',
           '-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
    subprocess.run([compiler,*flags,'-I'+str(ROOT/'src'),str(cpp),'-o',str(td/'navigation')],check=True)
    env=os.environ.copy(); env['XDG_CONFIG_HOME']=str(td/'config'); env['ASAN_OPTIONS']='detect_leaks=1'
    subprocess.run([str(td/'navigation')],check=True,env=env)
