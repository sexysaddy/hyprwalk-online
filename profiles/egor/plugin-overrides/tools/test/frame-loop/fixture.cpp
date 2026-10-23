// Controlled event-loop fixture. The exact production hook/timer/cadence bodies are inserted by run.py.
// Hyprland services are explicit doubles. Only the clock expression is redirected to simulated time.
#include "performance.hpp"
#include "math3d.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <memory>
#include <ranges>
#include <vector>
#include <functional>
using namespace hyprwalk;
using namespace std::chrono;
using TP=performance::TimePoint;
static TP clockNow{};
TP testNow(){return clockNow;}
namespace Time { TP steadyNow(){return testNow();} }
struct Vector2D {
    double x=0,y=0;
    Vector2D operator-(Vector2D b)const{return{x-b.x,y-b.y};}
    Vector2D operator*(double n)const{return{x*n,y*n};}
};
struct CBox {double x,y,w,h; CBox(double X,double Y,double W,double H):x(X),y(Y),w(W),h(H){} CBox(Vector2D a,Vector2D b):CBox(a.x,a.y,b.x,b.y){} CBox():CBox(0,0,0,0){} };
struct CRegion {double width=0,height=0; CRegion()=default; CRegion(CBox b):width(b.w),height(b.h){} void add(const CRegion& b){width=std::max(width,b.width);height=std::max(height,b.height);} };
struct Monitor {Vector2D m_transformedSize{1920,1080}; double m_scale=1; bool pending=false;};
using PHLMONITOR=std::shared_ptr<Monitor>;
struct Surface {int feedback=0; void presentFeedback(TP,const PHLMONITOR&){++feedback;}};
struct SurfaceRef {std::weak_ptr<Surface> surface;};
struct Panel {std::vector<SurfaceRef> surfaces;};
struct C3DElement {};
struct CTexPassElement {struct SRenderData {std::shared_ptr<int> tex; CBox box; float a=0;}; explicit CTexPassElement(SRenderData&&){} };
template<class T,class...A> auto makeUnique(A&&...a){return std::make_unique<T>(std::forward<A>(a)...);}
struct Pass {int count=0; template<class T>void add(std::unique_ptr<T>){++count;}};
struct Renderer {
    struct {std::weak_ptr<Monitor> pMonitor; CRegion damage,finalDamage;} m_renderData;
    Pass m_renderPass; int requests=0;
    void damageMonitor(const PHLMONITOR& m){++requests; m->pending=true;}
} renderer;
auto* g_pHyprRenderer=&renderer;
struct Lock {bool locked=false; bool isSessionLocked(){return locked;}} sessionLock;
auto* g_pSessionLockManager=&sessionLock;
struct Seat {struct{bool pointerFocus=false;} m_state;} seat;
auto* g_pSeatManager=&seat;
namespace Desktop {struct Focus {PHLMONITOR monitor(){return {};}}; Focus* focusState(){static Focus f;return &f;} }
namespace Notification {struct Overlay {void draw(const PHLMONITOR&){} }; Overlay* overlay(){static Overlay o;return &o;} }
namespace ErrorOverlay {struct Overlay {void draw(){} }; Overlay* overlay(){static Overlay o;return &o;} }
namespace Config {using INTEGER=int;}
template<class T>struct CConfigValue {explicit CConfigValue(const char*){} T operator*(){return 0;}};
namespace hypr {struct Pointer {std::shared_ptr<int> getCurrentCursorTexture(){return {};} Vector2D cursorSizeLogical(){return{};} Vector2D hotspot(){return{};} };Pointer* pointer(){static Pointer p;return &p;} }
template<class...T>void logf(const char*,T&&...){}
struct wl_event_source {TP due{}; bool fail=false; int updates=0;};
int wl_event_source_timer_update(wl_event_source* t,int ms){++t->updates;if(t->fail && ms>0)return -1;t->due=ms?testNow()+milliseconds(ms):TP{};return 0;}
enum eRenderStage {RENDER_PRE_WINDOWS,RENDER_LAST_MOMENT,RENDER_POST};
enum {MODE_OFF,MODE_ACTIVE,MODE_ENTERING};
class CDesktop3D {
public:
    int m_mode=MODE_ACTIVE, cap=60, updates=0;
    std::weak_ptr<Monitor> m_monitor;
    std::vector<Panel> m_panels;
    TP m_frameTime{},m_frameTimerDue{};
    performance::SFrameCadence m_frameCadence;
    // Legacy POST regression only reaches this stub when the monitor reference is wrongly kept alive.
    struct {int delayMs(TP)const{return 16;}} m_framePacer;
    bool m_frameTimerFailed=false,m_renderThisFrame=false,m_renderedSceneThisFrame=false;
    float m_updateMs=0,m_settingsSaveDelay=0;
    wl_event_source timer;
    wl_event_source* m_frameTimer=&timer;
    struct {bool shown=false;bool visible()const{return shown;}bool open()const{return shown;}} m_menu;
    struct {bool moving=false;bool playing()const{return moving;}} m_viewmodelVideo;
    struct {bool active=false;bool on()const{return active;}} m_mic;
    float m_scriptWalk=0,m_emoteView=0,m_fpTouch=0;
    bool m_fpPress=false; int m_fpTap=-1;
    struct {bool input=false;Vector2D pointer;float closing=0;} m_shell;
    struct {bool on=false;float t=0;} m_play;
    struct {bool surface=false;} m_drag;
    struct {bool key=false;} m_hold;
    bool m_lipsync=false,m_avatar=false,m_thirdPerson=false;
    bool fpBody()const{return false;}
    struct {V3 vel;bool onGround=true;} m_body;
    float m_camBoom=0,m_camDist=0;
    std::array<bool,768> m_keys{};
    float settingFloat(const char*)const{return cap;}
    void update(){++updates;}
    void drawShell(const PHLMONITOR&){}
    // Exit's timing-relevant prefix is tested separately; unrelated compositor cleanup is outside this fixture.
    void exitNow(){cancelFrameTimer();m_frameCadence.reset();m_mode=MODE_OFF;m_renderThisFrame=false;m_renderedSceneThisFrame=false;}
    int fpsLimit()const;
    bool needsContinuousFrame()const;
    void cancelFrameTimer();
    void scheduleFrameTimer(int);
    void requestFrame();
    void onRenderStage(eRenderStage);
    void legacyStage(eRenderStage);
    static int timerCallback(void*);
};
// INSERT METHODS
void dispatchTimers(CDesktop3D& d) {
    if(d.timer.due!=TP{} && d.timer.due<=testNow()) {
        d.timer.due={}; assert(CDesktop3D::timerCallback(&d)==0);
    }
}
void frame(CDesktop3D& d,const PHLMONITOR& mon) {
    renderer.m_renderData.pMonitor=mon;
    renderer.m_renderData.damage=CRegion{CBox{0,0,4,4}};
    renderer.m_renderData.finalDamage=CRegion{CBox{0,0,4,4}};
    const int before=d.updates;
    d.onRenderStage(RENDER_LAST_MOMENT);
    assert(d.updates-before<=1);
    assert(renderer.m_renderData.damage.width==1920 && renderer.m_renderData.damage.height==1080);
    assert(renderer.m_renderData.finalDamage.width==1920 && renderer.m_renderData.finalDamage.height==1080);
    // Execute the pass afterwards, then reproduce Hyprland 0.56.2 OpenGL::end's reference reset.
    d.m_renderedSceneThisFrame=d.m_renderThisFrame;
    renderer.m_renderData.pMonitor.reset();
    d.onRenderStage(RENDER_POST);
    assert(!d.m_renderedSceneThisFrame);
}
int main() {
    clockNow=TP{}+seconds(1);
    auto mon=std::make_shared<Monitor>();auto surface=std::make_shared<Surface>();
    CDesktop3D d; d.m_monitor=mon;d.cap=0;d.m_keys[20]=true;d.m_panels={{{{surface}}}};
    frame(d,mon);
    assert(mon->pending && d.updates==1 && surface->feedback==1 && d.timer.due==TP{});
    mon->pending=false;
    d.m_renderedSceneThisFrame=true;
    d.legacyStage(RENDER_POST);
    assert(!mon->pending && d.m_renderedSceneThisFrame); // exact previous POST body skips the continuation
    std::puts("PASS: previous POST regression reproduced with cleared monitor; current LAST_MOMENT schedules before reset and POST sends feedback");
    int cases=0;
    for(double hz:{59.94,60.,75.,120.,144.,165.,240.}) for(int cap:performance::FPS_LIMITS) {
        CDesktop3D a; a.m_monitor=mon;a.cap=cap;a.m_keys[20]=true;mon->pending=true;
        for(int tick=0;tick<(int)std::ceil(hz*5);++tick) {
            clockNow=TP{}+seconds(20+cases*10)+nanoseconds(std::llround(tick*1e9/hz));
            dispatchTimers(a);
            if(mon->pending) {mon->pending=false;frame(a,mon);}
        }
        const double expected=cap?std::min(hz,(double)cap):hz;
        assert(std::abs(a.updates/5.-expected)<=.25);
        // No ambient surface damage: settle and drain any one already-requested frame.
        a.m_keys.fill(false);
        for(int tick=0;tick<10;++tick){clockNow+=milliseconds(40);dispatchTimers(a);if(mon->pending){mon->pending=false;frame(a,mon);}}
        assert(!a.needsContinuousFrame() && a.timer.due==TP{} && !mon->pending);
        const int settled=a.updates;
        for(int tick=0;tick<30;++tick){clockNow+=milliseconds(100);dispatchTimers(a);assert(!mon->pending);}
        assert(a.updates==settled);
        a.requestFrame();assert(mon->pending);mon->pending=false;frame(a,mon);assert(a.updates==settled+1);
        ++cases;
    }
    CDesktop3D b;b.m_monitor=mon;
    clockNow=TP{}+seconds(1000);b.scheduleFrameTimer(10);const auto deadline=b.timer.due;
    clockNow+=milliseconds(3);b.scheduleFrameTimer(20);assert(b.timer.due==deadline);
    b.scheduleFrameTimer(2);assert(b.timer.due==clockNow+milliseconds(2));
    b.cancelFrameTimer();assert(b.timer.due==TP{} && b.m_frameTimerDue==TP{});
    b.timer.fail=true;b.scheduleFrameTimer(5);assert(b.m_frameTimerFailed && b.fpsLimit()==0 && mon->pending);
    b.exitNow();b.requestFrame();assert(!b.needsContinuousFrame() && b.timer.due==TP{});
    const int old=b.updates;renderer.m_renderData.pMonitor=mon;b.onRenderStage(RENDER_LAST_MOMENT);assert(b.updates==old);
    CDesktop3D other;other.m_monitor=std::make_shared<Monitor>(); // expired/other owner cannot be drawn
    other.onRenderStage(RENDER_LAST_MOMENT);assert(other.updates==0);
    CDesktop3D idle; assert(!idle.needsContinuousFrame());
    idle.m_menu.shown=true;assert(idle.needsContinuousFrame());idle.m_menu.shown=false;
    idle.m_viewmodelVideo.moving=true;assert(idle.needsContinuousFrame());idle.m_viewmodelVideo.moving=false;
    idle.m_avatar=true;idle.m_thirdPerson=true;assert(idle.needsContinuousFrame());idle.m_avatar=false;
    idle.m_body.onGround=false;assert(idle.needsContinuousFrame());idle.m_body.onGround=true;
    assert(!idle.needsContinuousFrame());
    std::printf("PASS: %d event-driven cap/display combinations; whole-frame damage even on cached frames; idle drains/stays idle/wakes; timer coalescing/failure/exit; animation criteria\n",cases);
}
