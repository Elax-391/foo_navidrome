#include "ScrobbleWorker.h"

#include <utility>

navidrome::ScrobbleWorker::ScrobbleWorker(Executor executor, bool enabled)
    : m_executor(std::move(executor)), m_enabled(enabled) {
    // Start only after every member (including the state flags declared after
    // m_thread) has completed initialization.
    m_thread = std::thread([this] { run(); });
}

navidrome::ScrobbleWorker::~ScrobbleWorker() {
    shutdown();
}

bool navidrome::ScrobbleWorker::enqueue(ScrobbleWorkItem item) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_enabled || m_stopping) return false;
        m_queue.push_back(std::move(item));
    }
    m_changed.notify_one();
    return true;
}

void navidrome::ScrobbleWorker::setEnabled(bool enabled) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) return;
        m_enabled = enabled;
        if (!enabled) m_queue.clear();
    }
    m_changed.notify_all();
}

void navidrome::ScrobbleWorker::shutdown() noexcept {
    std::lock_guard<std::mutex> shutdownLock(m_shutdownMutex);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        m_enabled = false;
        m_queue.clear();
    }
    m_changed.notify_all();
    if (m_thread.joinable() && m_thread.get_id() != std::this_thread::get_id()) {
        m_thread.join();
    }
}

void navidrome::ScrobbleWorker::run() noexcept {
    for (;;) {
        ScrobbleWorkItem item;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_changed.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
            if (m_stopping) return;
            item = std::move(m_queue.front());
            m_queue.pop_front();
        }

        try {
            m_executor(item);
        } catch (...) {
            // Playback reporting is best-effort. Never retry an ambiguous write.
        }
    }
}
