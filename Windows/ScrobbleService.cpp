#include "ScrobbleService.h"

#include "ScrobbleWorker.h"

#include <memory>
#include <utility>

class navidrome::ScrobbleCoordinator::Impl {
public:
    Impl()
        : worker([](const ScrobbleWorkItem& item) {
              std::string ignoredError;
              SubsonicClientWin::get().scrobble(
                  item.context, item.songId, item.submission, ignoredError);
          }) {}

    ScrobbleSession session;
    ScrobbleWorker worker;
    SubsonicRequestContext context;
    std::string identity;
    double lastMediaPositionSeconds = 0.0;
    bool shutdown = false;
};

navidrome::ScrobbleCoordinator& navidrome::ScrobbleCoordinator::get() {
    static ScrobbleCoordinator instance;
    return instance;
}

navidrome::ScrobbleCoordinator::ScrobbleCoordinator()
    : m_impl(new Impl()) {}

navidrome::ScrobbleCoordinator::~ScrobbleCoordinator() {
    shutdown();
    delete m_impl;
}

void navidrome::ScrobbleCoordinator::setEnabled(bool enabled) noexcept {
    try {
        if (m_impl->shutdown) return;
        m_impl->session.setEnabled(enabled);
        if (!enabled) {
            m_impl->context = {};
            m_impl->identity.clear();
            m_impl->lastMediaPositionSeconds = 0.0;
        }
        m_impl->worker.setEnabled(enabled);
    } catch (...) {}
}

void navidrome::ScrobbleCoordinator::onNewTrack(
        std::string songId, double durationSeconds,
        SubsonicRequestContext context, std::string identity,
        double monotonicSeconds) noexcept {
    try {
        if (m_impl->shutdown) return;
        m_impl->context = std::move(context);
        m_impl->identity = std::move(identity);
        m_impl->lastMediaPositionSeconds = 0.0;
        dispatch(m_impl->session.startTrack(
            std::move(songId), durationSeconds, 0.0, monotonicSeconds));
        if (!m_impl->session.active()) {
            m_impl->context = {};
            m_impl->identity.clear();
        }
    } catch (...) {
        m_impl->session.startTrack({}, 0.0, 0.0, monotonicSeconds);
        m_impl->context = {};
        m_impl->identity.clear();
    }
}

void navidrome::ScrobbleCoordinator::onTime(
        double mediaPositionSeconds, double monotonicSeconds) noexcept {
    try {
        m_impl->lastMediaPositionSeconds = mediaPositionSeconds;
        dispatch(m_impl->session.onTime(mediaPositionSeconds, monotonicSeconds));
    } catch (...) {}
}

void navidrome::ScrobbleCoordinator::onPause(
        bool paused, double monotonicSeconds) noexcept {
    try {
        dispatch(m_impl->session.onPause(
            paused, m_impl->lastMediaPositionSeconds, monotonicSeconds));
    } catch (...) {}
}

void navidrome::ScrobbleCoordinator::onSeek(
        double mediaPositionSeconds, double monotonicSeconds) noexcept {
    try {
        m_impl->lastMediaPositionSeconds = mediaPositionSeconds;
        m_impl->session.onSeek(mediaPositionSeconds, monotonicSeconds);
    } catch (...) {}
}

void navidrome::ScrobbleCoordinator::onStop(
        ScrobbleStopReason reason, double monotonicSeconds) noexcept {
    try {
        if (reason == ScrobbleStopReason::ShuttingDown) {
            shutdown();
            return;
        }
        dispatch(m_impl->session.onStop(
            reason, m_impl->lastMediaPositionSeconds, monotonicSeconds));
        m_impl->context = {};
        m_impl->identity.clear();
        m_impl->lastMediaPositionSeconds = 0.0;
    } catch (...) {}
}

void navidrome::ScrobbleCoordinator::shutdown() noexcept {
    try {
        if (m_impl == nullptr || m_impl->shutdown) return;
        m_impl->shutdown = true;
        m_impl->session.shutdown();
        m_impl->context = {};
        m_impl->identity.clear();
        m_impl->worker.shutdown();
    } catch (...) {}
}

void navidrome::ScrobbleCoordinator::dispatch(
        const std::optional<ScrobbleDecision>& decision) noexcept {
    if (!decision || m_impl->shutdown || m_impl->identity.empty()) return;
    try {
        ScrobbleWorkItem item;
        item.sessionId = decision->sessionId;
        item.songId = decision->songId;
        item.identity = m_impl->identity;
        item.context = m_impl->context;
        item.submission = decision->submission == ScrobbleSubmission::Completed;
        m_impl->worker.enqueue(std::move(item));
    } catch (...) {}
}
