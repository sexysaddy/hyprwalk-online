#pragma once
#include "Types.hpp"
#include "PlayerSnapshot.hpp"
#include "MapSync.hpp"
#include "WindowTypes.hpp"
#include "VideoSignaling.hpp"

namespace hyprwalk::mp {
// Only the owning/main thread calls this interface. The worker never holds or calls it.
// The adapter must outlive Multiplayer; shutdown() must finish before plugin unload.
class IAdapter {
  public:
    virtual ~IAdapter() = default;
    virtual LocalInfo localInfo() const = 0;
    virtual std::optional<MapDescriptor> localMap() const { return {}; }
    virtual void onWorldRequest(const WorldRequest&) noexcept {}
    virtual void onVideoSignal(const VideoSignal&) noexcept {}
    virtual void onWindows(const ReceivedWindows&) noexcept {}
    virtual void onState(const Status& status) noexcept = 0;
    // Optional, dispatched in the owning thread. Queue is bounded and cleared on disconnect.
    virtual void onPlayerSnapshot(const ReceivedPlayer&) noexcept {}
    virtual void onDiagnostic(const Diagnostic& diagnostic) noexcept = 0;
};
} // namespace hyprwalk::mp
