#pragma once
#include <hyprwalk/mp/VideoTypes.hpp>
#include <memory>

namespace hyprwalk::mp::media {
struct ReadbackMetrics {
    std::uint64_t issued=0, completed=0, dropped=0, fenceTimeouts=0, bytes=0;
    double maxIssueUs=0, maxCopyUs=0;
    std::string error;
};
// Main GL-context thread ONLY. Takes an already composited, permitted single-window
// FBO, never a display/screen texture. No source pointers are stored. Two PBOs per key.
class Readback {
  public:
    Readback();
    ~Readback(); // owner MUST call clear() while its GL context is current
    Readback(const Readback&)=delete;
    Readback& operator=(const Readback&)=delete;
    void retain(const std::vector<VideoTrackKey>& permitted);
    void issue(const VideoTrackKey&, unsigned framebuffer, unsigned sourceWidth,
               unsigned sourceHeight, const VideoSettings&);
    std::vector<std::shared_ptr<const VideoFrame>> take();
    void clear() noexcept;
    const ReadbackMetrics& metrics() const noexcept;
  private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
} // namespace hyprwalk::mp::media
