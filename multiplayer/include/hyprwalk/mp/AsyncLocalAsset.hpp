#pragma once

#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

namespace hyprwalk::mp {
// A bounded, cancellable local-asset worker, NOT a network or graphics backend.
// Request and Result follow the existing loader convention: Result.req / .error.
// All public methods except finished()/failed() are owner/main-thread methods.
// One active task, one replaceable pending task, one result. No detached threads.
// The caller removes its event-loop source BEFORE shutdown() closes fd().
template<class Request, class Result>
class AsyncLocalAsset {
  public:
    using Load = Result (*)(const Request&, const std::atomic<bool>&);
    struct Delivery { std::uint64_t generation; Result value; };

    explicit AsyncLocalAsset(Load load) : m_load(load) {
        if (!load) throw std::invalid_argument("missing local asset loader");
        m_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (m_fd < 0) throw std::system_error(errno, std::generic_category(), "asset eventfd");
        try { m_worker = std::thread([this] { run(); }); }
        catch (...) { close(m_fd); m_fd = -1; throw; }
    }
    ~AsyncLocalAsset() { shutdown(); }
    AsyncLocalAsset(const AsyncLocalAsset&) = delete;
    AsyncLocalAsset& operator=(const AsyncLocalAsset&) = delete;

    int fd() const noexcept { return m_fd; }
    bool finished() const noexcept { return m_finished.load(std::memory_order_acquire); }
    bool failed() const noexcept { return m_failed.load(std::memory_order_acquire); }

    std::uint64_t request(const Request& request) {
        Task next{0, request, std::make_shared<std::atomic<bool>>(false)};
        std::lock_guard lock(m_mutex);
        if (m_stopping || finished()) throw std::logic_error("asset worker is stopping");
        next.generation = ++m_generation;
        if (m_activeCancel) *m_activeCancel = true;
        m_result.reset();
        m_pending = std::move(next);
        m_cv.notify_one();
        return m_generation;
    }
    // Cancels without waiting for disk I/O, parsing or the worker to finish.
    void cancel() {
        std::lock_guard lock(m_mutex);
        ++m_generation;
        m_pending.reset();
        m_result.reset();
        if (m_activeCancel) *m_activeCancel = true;
    }
    bool busy() const {
        std::lock_guard lock(m_mutex);
        return m_pending.has_value() || static_cast<bool>(m_activeCancel) || m_result.has_value();
    }
    std::optional<Delivery> take() {
        drain();
        std::lock_guard lock(m_mutex);
        auto out = std::move(m_result);
        m_result.reset();
        if (out && (out->generation != m_generation || m_stopping)) out.reset();
        return out;
    }
    // Main-thread disconnect is nonblocking. Exit notification uses the same fd.
    void stop() noexcept {
        {
            std::lock_guard lock(m_mutex);
            m_stopping = true;
            ++m_generation;
            m_pending.reset();
            m_result.reset();
            if (m_activeCancel) *m_activeCancel = true;
        }
        m_cv.notify_one();
    }
    // Mandatory join at plugin unload. The supplied loader must honor cancellation.
    // No wait for a network peer is involved. Calling this after finished() does not
    // wait for asset parsing, only the worker's final notification/return.
    void shutdown() noexcept {
        stop();
        if (m_worker.joinable()) m_worker.join();
        if (m_fd >= 0) { close(m_fd); m_fd = -1; }
    }

  private:
    struct Task {
        std::uint64_t generation;
        Request request;
        std::shared_ptr<std::atomic<bool>> cancel;
    };
    void notify() noexcept {
        const std::uint64_t one = 1;
        for (int tries = 0; tries < 8; ++tries) {
            if (write(m_fd, &one, sizeof(one)) >= 0 || errno != EINTR) break;
        }
    }
    void drain() noexcept {
        std::uint64_t value;
        for (int tries = 0; tries < 8; ++tries) {
            if (read(m_fd, &value, sizeof(value)) >= 0 || errno != EINTR) break;
        }
    }
    void run() noexcept {
        try {
            for (;;) {
                std::optional<Task> task;
                {
                    std::unique_lock lock(m_mutex);
                    m_cv.wait(lock, [this] { return m_stopping || m_pending.has_value(); });
                    if (m_stopping) break;
                    task = std::move(m_pending);
                    m_pending.reset();
                    m_activeCancel = task->cancel;
                }
                Result value;
                try { value = m_load(task->request, *task->cancel); }
                catch (const std::exception& e) { value.req = task->request; value.error = e.what(); }
                catch (...) { value.req = task->request; value.error = "local asset load cancelled or failed"; }
                bool published = false;
                {
                    std::lock_guard lock(m_mutex);
                    m_activeCancel.reset();
                    if (!m_stopping && !task->cancel->load() && task->generation == m_generation) {
                        m_result = Delivery{task->generation, std::move(value)};
                        published = true;
                    }
                }
                if (published) notify();
            }
        } catch (...) {
            m_failed.store(true, std::memory_order_release);
        }
        m_finished.store(true, std::memory_order_release);
        notify();
    }
    Load m_load;
    int m_fd = -1;
    std::thread m_worker;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stopping = false;
    std::uint64_t m_generation = 0;
    std::optional<Task> m_pending;
    std::optional<Delivery> m_result;
    std::shared_ptr<std::atomic<bool>> m_activeCancel;
    std::atomic<bool> m_finished{false}, m_failed{false};
};
} // namespace hyprwalk::mp
