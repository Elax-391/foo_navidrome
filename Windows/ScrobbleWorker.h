#pragma once

#include "SubsonicClientWin.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace navidrome {

struct ScrobbleWorkItem {
    std::uint64_t sessionId = 0;
    std::string songId;
    std::string identity;
    SubsonicRequestContext context;
    bool submission = false;
};

// One ordered worker for all scrobble side effects. Disabling drops queued work
// that has not started; an in-flight request is allowed to finish its bounded
// transport operation before shutdown joins the worker.
class ScrobbleWorker {
public:
    using Executor = std::function<void(const ScrobbleWorkItem&)>;

    explicit ScrobbleWorker(Executor executor, bool enabled = true);
    ~ScrobbleWorker();

    ScrobbleWorker(const ScrobbleWorker&) = delete;
    ScrobbleWorker& operator=(const ScrobbleWorker&) = delete;

    bool enqueue(ScrobbleWorkItem item);
    void setEnabled(bool enabled);
    void shutdown() noexcept;

private:
    void run() noexcept;

    Executor m_executor;
    std::mutex m_mutex;
    std::mutex m_shutdownMutex;
    std::condition_variable m_changed;
    std::deque<ScrobbleWorkItem> m_queue;
    std::thread m_thread;
    bool m_enabled = true;
    bool m_stopping = false;
};

} // namespace navidrome
