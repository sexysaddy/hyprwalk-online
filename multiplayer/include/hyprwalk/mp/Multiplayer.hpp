#pragma once
#include "Adapter.hpp"
#include <memory>

namespace hyprwalk::mp {
class Multiplayer {
  public:
    explicit Multiplayer(IAdapter& adapter, Settings settings = {});
    ~Multiplayer();
    Multiplayer(const Multiplayer&) = delete;
    Multiplayer& operator=(const Multiplayer&) = delete;

    // Control methods and dispatch/reap/shutdown are main-thread-only. Status is a snapshot.
    // A true result means startup was queued, NOT that bind/connect/handshake succeeded.
    bool host(const Endpoint& endpoint, std::string& error);
    bool join(const Endpoint& endpoint, std::string& error);
    bool sendTest(const std::string& text, std::string& error);
    void leave() noexcept; // Asynchronous: wake worker, attempt bounded BYE flush, then close.
    Status status() const;
    // Replace the latest physical state, never queue frames or wake the worker at render FPS.
    // No socket work. Owning thread only; false for invalid state or inactive UDP session.
    bool submitPlayer(const PlayerSnapshot& player);
    // Main thread: acknowledge actual map application, never merely receipt of the offer.
    bool confirmWorld(const WorldConfirmation& confirmation);
    // Latest consent-filtered complete set; strictly scoped to this world/owner.
    // Empty set revokes all. Main thread, no socket I/O. Revision is assigned by worker.
    bool submitWindows(const WindowSnapshot& publication);
    bool sendVideoSignal(VideoSignal signal, std::string& error);

    // Lazily created by host/join, stable until reap/shutdown. Register with wl_event_loop_add_fd.
    int notificationFd() const noexcept;
    bool dispatch(std::size_t maxEvents = 64); // True when finished AND all queued callbacks delivered.
    bool finished() const noexcept;
    // Remove the event-loop source BEFORE calling reap or shutdown (they close notificationFd).
    bool reap() noexcept; // Only joins an already-finished worker; retains final status.
    void shutdown() noexcept; // Immediate cancellation + join, no network/peer wait. Idempotent.

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace hyprwalk::mp
