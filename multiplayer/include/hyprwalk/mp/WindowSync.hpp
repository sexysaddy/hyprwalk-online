#pragma once
#include "WindowTypes.hpp"
#include <map>
#include <set>

namespace hyprwalk::mp {
enum class SharingMode { Off, Selected, All };
const char* sharingModeName(SharingMode) noexcept;
// Main-thread consent and receiving view model; neither networking nor Hyprland/GL calls.
class WindowSync {
  public:
    bool session(const Status&); // resets consent/IDs/settings on new world/peer or disconnect
    bool ready() const noexcept { return m_ready; }
    const std::string& generationToken() const noexcept { return m_token; }
    SharingMode mode() const noexcept { return m_mode; }
    const std::string& error() const noexcept { return m_error; }
    void setError(std::string error) { m_error = std::move(error); }
    void sources(std::vector<WindowMetadata>); // already filtered local top-levels only
    void revoke() noexcept; // fail closed on lock/away/exit/leave
    bool setMode(SharingMode, bool confirmedAll = false);
    bool select(std::uint64_t id, bool selected);
    bool selected(std::uint64_t id) const { return m_selected.contains(id); }
    WindowSnapshot publication() const;
    std::uint64_t publicationEpoch(std::uint64_t id) const noexcept;
    void videoBackend(bool available, std::string detail);
    bool videoAvailable() const noexcept { return m_videoAvailable; }
    bool videoEnabled() const noexcept { return m_videoEnabled; }
    void videoEnable(bool on);
    const std::string& videoSummary() const noexcept { return m_videoSummary; }
    std::string localVideo(std::uint64_t id) const;
    void videoStatus(std::uint64_t id, bool sending, std::string detail);
    bool receive(const ReceivedWindows&);
    const std::vector<WindowMetadata>& local() const noexcept { return m_local; }
    const std::vector<RemoteWindow>& remote() const noexcept { return m_remote; }
    const RemoteWindow* findRemote(std::uint64_t id) const;
    bool hide(std::uint64_t id, bool);
    bool choose(std::uint64_t id); // local UI selection, not a surface/application focus
    std::uint64_t chosen() const noexcept { return m_chosen; }
    std::uint64_t revision() const noexcept { return m_revision; }
  private:
    bool m_ready = false;
    std::map<std::uint64_t,std::uint64_t> m_publicationEpoch;
    std::uint64_t m_nextPublication = 0;
    void refreshPublicationEpochs();
    bool m_videoAvailable = false, m_videoEnabled = true;
    std::string m_videoSummary = "Unavailable - WebRTC backend not built";
    std::map<std::uint64_t,std::string> m_localVideo;
    Id m_session{}, m_localId{}, m_peer{}, m_generation{};
    std::string m_token, m_error;
    SharingMode m_mode = SharingMode::Off;
    std::set<std::uint64_t> m_selected;
    std::vector<WindowMetadata> m_local;
    std::vector<RemoteWindow> m_remote;
    std::uint64_t m_receivedRevision = 0, m_revision = 0, m_chosen = 0;
    RemoteWindow* mutableRemote(std::uint64_t);
};
} // namespace hyprwalk::mp
