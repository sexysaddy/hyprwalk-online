#pragma once
#ifdef HYPRWALK_MULTIPLAYER
#include "avatar.hpp"
#include <hyprwalk/mp/AsyncLocalAsset.hpp>
#include <hyprwalk/mp/PlayerSnapshot.hpp>

#include <chrono>

namespace hyprwalk {
// Local presentation only. The loader runs the receiving fork's loadAvatar().
// No model, emote, expression, microphone or user-input data is sent over the wire.
class CRemoteAvatar {
  public:
    using Clock = std::chrono::steady_clock;
    using Loader = mp::AsyncLocalAsset<SAvatarRequest, SAvatarResult>;
    explicit CRemoteAvatar(Loader::Load load = loadAvatar) : m_loader(load) {}
    int fd() const { return m_loader.fd(); }
    bool finished() const { return m_loader.finished(); }
    bool stopping() const { return m_stopping; }
    bool busy() const { return !m_stopping && m_loader.busy(); }
    const std::string& error() const { return m_error; }
    const std::shared_ptr<SAvatarModel>& model() const { return m_model; }
    const CAvatarAnimator& animator() const { return m_anim; }
    const M4& transform() const { return m_transform; }
    const SAvatarMotion& motion() const { return m_motion; }

    // revision changes on explicit reload or successful local-avatar replacement.
    // Separate disk load is intentional: local model->releaseCpuData() may already
    // have discarded vertices, indices and pixels. Never copy/share that model.
    void select(const SAvatarRequest& request, std::uint64_t revision);
    void take();
    void stop();
    // No GL here. The main-thread owner arranges GPU deletion in a current context.
    void pose(const mp::PlayerSnapshot* player, Clock::time_point now, bool physics,
              const std::function<std::optional<float>(float,float,float)>& ground = {});

  private:
    void clearPose();
    Loader m_loader;
    SAvatarRequest m_request;
    std::uint64_t m_revision = 0, m_generation = 0;
    bool m_selected = false, m_stopping = false, m_visible = false;
    std::shared_ptr<SAvatarModel> m_model;
    CAvatarAnimator m_anim;
    SAvatarMotion m_motion;
    M4 m_transform = M4::identity();
    std::string m_error;
    Clock::time_point m_last{};
    mp::PlayerSnapshot m_previous;
};
} // namespace hyprwalk
#endif
