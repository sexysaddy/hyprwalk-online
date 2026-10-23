#include <hyprwalk/mp/PlayerSync.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hyprwalk::mp {
namespace {
constexpr float Pi = 3.14159265358979323846f;
double micros(PlayerSync::Time t) { return std::chrono::duration<double, std::micro>(t.time_since_epoch()).count(); }
float magnitude(Vec3 v) { return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); }
float distance(Vec3 a, Vec3 b) { return magnitude({a.x-b.x,a.y-b.y,a.z-b.z}); }
float mix(float a, float b, float t) { return a + (b-a)*t; }
Vec3 mix(Vec3 a, Vec3 b, float t) { return {mix(a.x,b.x,t),mix(a.y,b.y,t),mix(a.z,b.z,t)}; }
float angle(float a, float b, float t) { return std::remainder(a + std::remainder(b-a, 2*Pi)*t, 2*Pi); }
}
PlayerSync::PlayerSync(InterpolationSettings settings) : m_settings(settings) {
    if (settings.delay.count() < 0 || settings.delay > std::chrono::milliseconds(500) ||
        settings.extrapolationLimit.count() < 0 || settings.extrapolationLimit > std::chrono::milliseconds(250) ||
        settings.staleAfter < settings.delay + settings.extrapolationLimit || settings.staleAfter > std::chrono::seconds(5) ||
        settings.staleAfter.count() <= 0 || !std::isfinite(settings.teleportDistance) || settings.teleportDistance <= 0)
        throw std::invalid_argument("invalid player interpolation settings");
}
void PlayerSync::reset() noexcept {
    m_size = m_offsetCount = m_offsetNext = 0; m_sequences.reset(); m_lastArrival = {};
    m_clockOffsetUs = 0; ++m_revision;
}
bool PlayerSync::push(const ReceivedPlayer& p) noexcept {
    if (!validPlayerSnapshot(p.player) || p.senderTimeUs > static_cast<std::uint64_t>(INT64_MAX)) return false;
    bool fresh = m_size == 0 || udpwire::newer(p.sequence, m_samples[m_size-1].sequence);
    if (m_size) {
        const auto& previous = m_samples[m_size-1];
        if (p.receivedAt < m_lastArrival) return false;
        if (fresh && p.senderTimeUs <= previous.senderTimeUs) return false;
        if (!fresh && (p.senderTimeUs >= previous.senderTimeUs || p.player.teleport != previous.player.teleport ||
                       p.player.active != previous.player.active || p.senderTimeUs < m_samples[0].senderTimeUs)) return false;
        // Reject a forged clock leap without moving the interpolation timeline.
        if (fresh && p.receivedAt-m_lastArrival < m_settings.staleAfter &&
            p.senderTimeUs-previous.senderTimeUs > 2'000'000) return false;
    }
    const auto verdict = m_sequences.accept(p.sequence);
    if (verdict == udpwire::SequenceWindow::Result::Duplicate || verdict == udpwire::SequenceWindow::Result::TooOld) return false;
    if (fresh && m_size) {
        const auto& previous = m_samples[m_size-1];
        const float dt = static_cast<float>(p.senderTimeUs-previous.senderTimeUs)*1.e-6f;
        const float plausibleDistance = (magnitude(previous.player.velocity)+magnitude(p.player.velocity))*dt+2.f;
        if (p.player.teleport != previous.player.teleport || p.player.active != previous.player.active ||
            p.receivedAt-m_lastArrival >= m_settings.staleAfter ||
            distance(p.player.feet,previous.player.feet) > std::max(m_settings.teleportDistance, plausibleDistance)) {
            // Preserve the replay window. Old packets must not interpolate across a teleport.
            m_size = m_offsetCount = m_offsetNext = 0;
        }
    }
    if (fresh) {
        const double observed = micros(p.receivedAt)-static_cast<double>(p.senderTimeUs);
        m_offsets[m_offsetNext] = observed; m_offsetNext = (m_offsetNext+1)%Capacity;
        m_offsetCount = std::min(Capacity,m_offsetCount+1);
        const double target = *std::min_element(m_offsets.begin(),m_offsets.begin()+m_offsetCount);
        if (!m_size) m_clockOffsetUs = observed;
        else m_clockOffsetUs += std::clamp(target-m_clockOffsetUs,-1000.,1000.); // Drift correction, not jitter following.
        m_lastArrival = p.receivedAt;
    }
    if (m_size == Capacity) { std::move(m_samples.begin()+1,m_samples.end(),m_samples.begin()); --m_size; }
    auto begin = m_samples.begin(), end = begin+m_size;
    auto it = std::lower_bound(begin,end,p.senderTimeUs,[](const ReceivedPlayer& a, std::uint64_t t) { return a.senderTimeUs < t; });
    if (it != end && it->senderTimeUs == p.senderTimeUs) return false;
    std::move_backward(it,end,end+1); *it = p; ++m_size; ++m_revision;
    return true;
}
std::optional<SampledPlayer> PlayerSync::sample(Time now) const noexcept {
    if (!m_size || !m_samples[m_size-1].player.active || now-m_lastArrival >= m_settings.staleAfter) return {};
    const double target = micros(now)-m_clockOffsetUs-static_cast<double>(m_settings.delay.count())*1000.;
    if (target <= static_cast<double>(m_samples[0].senderTimeUs)) return SampledPlayer{m_samples[0].player};
    for (std::size_t j=1;j<m_size;++j) {
        const auto& a = m_samples[j-1]; const auto& b = m_samples[j];
        if (target > static_cast<double>(b.senderTimeUs)) continue;
        const float t = static_cast<float>((target-static_cast<double>(a.senderTimeUs))/static_cast<double>(b.senderTimeUs-a.senderTimeUs));
        PlayerSnapshot p = t < 1.f ? a.player : b.player; // Discrete movement flags are never numerically blended.
        p.feet = mix(a.player.feet,b.player.feet,t); p.velocity = mix(a.player.velocity,b.player.velocity,t);
        p.yaw = angle(a.player.yaw,b.player.yaw,t); p.bodyYaw = angle(a.player.bodyYaw,b.player.bodyYaw,t);
        p.pitch = mix(a.player.pitch,b.player.pitch,t);
        return SampledPlayer{p};
    }
    auto p = m_samples[m_size-1].player;
    const double ahead = (target-static_cast<double>(m_samples[m_size-1].senderTimeUs))*1.e-6;
    const double limit = static_cast<double>(m_settings.extrapolationLimit.count())*0.001;
    const float dt = static_cast<float>(std::clamp(ahead,0.,limit));
    p.feet.x += p.velocity.x*dt; p.feet.y += p.velocity.y*dt; p.feet.z += p.velocity.z*dt;
    return SampledPlayer{p,dt > 0, ahead > limit};
}
} // namespace hyprwalk::mp
