#ifdef HYPRWALK_MULTIPLAYER
#include "remote_avatar.hpp"
#include <algorithm>
#include <cmath>

namespace hyprwalk {
void CRemoteAvatar::clearPose() {
    m_anim.reset(nullptr);
    m_model.reset();
    m_visible = false;
    m_last = {};
    m_motion = {};
    m_transform = M4::identity();
}
void CRemoteAvatar::select(const SAvatarRequest& request, std::uint64_t revision) {
    if (m_stopping) return;
    if (m_selected && m_revision == revision && m_request.path == request.path &&
        m_request.height == request.height) return;
    m_selected = true;
    m_revision = revision;
    m_request = request;
    clearPose();
    m_loader.cancel();
    m_generation = 0;
    m_error.clear();
    if (request.path.empty()) { m_loader.cancel(); m_generation = 0; return; }
    try { m_generation = m_loader.request(request); }
    catch (const std::exception& e) { m_error = e.what(); }
}
void CRemoteAvatar::take() {
    auto result = m_loader.take();
    if (m_loader.failed()) m_error = "local avatar worker failed";
    if (!result || m_stopping || result->generation != m_generation) return;
    auto& loaded = result->value;
    if (!loaded.model || !loaded.error.empty()) {
        m_error = loaded.error.empty() ? "local avatar model unavailable" : loaded.error;
        clearPose();
        return;
    }
    m_model = std::move(loaded.model);
    m_anim.reset(m_model);
    m_visible = false;
    m_last = {};
    m_error.clear();
}
void CRemoteAvatar::stop() {
    if (m_stopping) return;
    m_stopping = true;
    clearPose();
    m_loader.stop(); // Nonblocking cancellation; destructor MUST join before unload.
}
void CRemoteAvatar::pose(const mp::PlayerSnapshot* p, Clock::time_point now, bool physics,
                        const std::function<std::optional<float>(float,float,float)>& ground) {
    if (m_stopping || !m_model) return;
    if (!p || !p->active || !mp::validPlayerSnapshot(*p)) {
        m_visible = false;
        m_last = {};
        return;
    }
    const V3 feet{p->feet.x, p->feet.y, p->feet.z};
    const V3 old{m_previous.feet.x, m_previous.feet.y, m_previous.feet.z};
    const bool reset = !m_visible || p->teleport != m_previous.teleport || length(feet - old) > 8.f;
    if (reset) m_anim.reset(m_model); // springs/gait never interpolate across a teleport/reconnect
    float dt = reset ? 1.f / 60.f : std::chrono::duration<float>(now - m_last).count();
    dt = std::clamp(dt, 0.f, 0.05f); // no huge spring simulation after stalled/hidden rendering
    m_anim.setPhysics(physics);
    SAvatarMotion motion;
    motion.dt = dt;
    motion.vel = {p->velocity.x, 0, p->velocity.z};
    motion.wish = motion.vel; // remote keys are deliberately not replicated
    motion.speed = length(motion.vel);
    motion.vy = p->velocity.y;
    motion.onGround = p->onGround;
    motion.flying = p->flying;
    motion.crouched = p->crouched;
    motion.run = p->running;
    motion.lookYaw = std::clamp(std::remainder(p->yaw - p->bodyYaw, 6.28318530718f), -1.5f, 1.5f);
    motion.lookPitch = std::clamp(p->pitch, -1.1f, 1.1f);
    motion.world = M4::trs(feet, Quat::axisAngle({0,1,0}, -p->bodyYaw), {1,1,1});
    motion.ground = ground;
    // fp remains OFF; no local hand/face/gesture/mic controls affect this animator.
    m_anim.update(motion);
    m_transform = M4::trs(feet + V3{0, m_anim.lift(), 0}, Quat::axisAngle({0,1,0}, -p->bodyYaw), {1,1,1});
    m_motion = motion;
    m_motion.ground = {}; // do not retain callbacks to CDesktop3D between frames
    m_previous = *p;
    m_last = now;
    m_visible = true;
}
} // namespace hyprwalk
#endif
