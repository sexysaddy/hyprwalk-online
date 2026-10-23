#pragma once
#include <hyprwalk/mp/VideoSignaling.hpp>
#include <memory>

namespace hyprwalk::mp::media {
struct Availability { bool available = false; std::string version, detail; };
// Pure value boundary: neither this public header nor Core includes GStreamer, GL,
// Hyprland or native window handles. All heavy media work runs on the owned worker.
class MediaSession {
  public:
    static Availability probe();
    explicit MediaSession(VideoSettings settings = {});
    ~MediaSession();
    MediaSession(const MediaSession&) = delete;
    MediaSession& operator=(const MediaSession&) = delete;
    const Availability& availability() const noexcept;
    const VideoSettings& settings() const noexcept;
    int notificationFd() const noexcept;
    void acknowledge() noexcept;
    void configure(VideoContext); // synchronously closes revoked frame/packet gates
    void revoke(const WindowKey&); // per-use source validity failure, never waits for network
    bool signal(VideoSignal);
    bool submit(std::shared_ptr<const VideoFrame>); // latest-only, no GStreamer/GL call
    std::vector<VideoTrackKey> captures() const;
    std::shared_ptr<const VideoFrame> frame(const WindowKey&) const;
    std::vector<VideoTrackKey> images() const;
    std::vector<VideoSignal> takeSignals();
    std::vector<VideoStreamStatus> status() const;
    std::string diagnostic() const;
    std::string error() const;
    void fail(std::string reason); // local fail-closed, leaves TCP/UDP untouched
    void stop() noexcept; // async; no new captures/frames admitted from this call onward
    void shutdown() noexcept; // mandatory join, call after removing the event source
  private:
#ifdef HYPRWALK_MEDIA_INTERNAL_TEST
    friend struct MediaSessionTestAccess; // white-box gate/closure tests; no production bypass
#endif
    struct Impl;
    std::unique_ptr<Impl> m;
};
} // namespace hyprwalk::mp::media
