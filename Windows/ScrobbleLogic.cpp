#include "ScrobbleLogic.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr double kUnknownDurationThresholdSeconds = 240.0;
constexpr double kMaximumThresholdSeconds = 240.0;
constexpr double kPlaybackClockToleranceSeconds = 1.5;

bool validTime(double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

} // namespace

navidrome::ScrobbleSession::ScrobbleSession(bool enabled) noexcept
    : m_enabled(enabled) {}

std::optional<navidrome::ScrobbleDecision>
navidrome::ScrobbleSession::startTrack(
        std::string songId, double durationSeconds,
        double mediaPositionSeconds, double monotonicSeconds) {
    resetActiveSession();
    if (m_shutdown) return std::nullopt;

    ++m_sessionId;
    if (!m_enabled || songId.empty()) return std::nullopt;

    m_active = true;
    m_songId = std::move(songId);
    m_thresholdSeconds = validTime(durationSeconds) && durationSeconds > 0.0
        ? (std::min)(durationSeconds * 0.5, kMaximumThresholdSeconds)
        : kUnknownDurationThresholdSeconds;
    setBaseline(mediaPositionSeconds, monotonicSeconds);
    return ScrobbleDecision{m_sessionId, m_songId, ScrobbleSubmission::NowPlaying};
}

std::optional<navidrome::ScrobbleDecision>
navidrome::ScrobbleSession::onTime(
        double mediaPositionSeconds, double monotonicSeconds) {
    if (!m_active || m_paused) return std::nullopt;
    accumulate(mediaPositionSeconds, monotonicSeconds);
    return completionDecision();
}

std::optional<navidrome::ScrobbleDecision>
navidrome::ScrobbleSession::onPause(
        bool paused, double mediaPositionSeconds, double monotonicSeconds) {
    if (!m_active || paused == m_paused) return std::nullopt;

    if (paused) {
        accumulate(mediaPositionSeconds, monotonicSeconds);
        m_paused = true;
        m_baselineValid = false;
        return completionDecision();
    }

    m_paused = false;
    setBaseline(mediaPositionSeconds, monotonicSeconds);
    return std::nullopt;
}

void navidrome::ScrobbleSession::onSeek(
        double mediaPositionSeconds, double monotonicSeconds) noexcept {
    if (!m_active) return;
    setBaseline(mediaPositionSeconds, monotonicSeconds);
}

std::optional<navidrome::ScrobbleDecision>
navidrome::ScrobbleSession::onStop(
        ScrobbleStopReason reason, double mediaPositionSeconds,
        double monotonicSeconds) {
    if (reason == ScrobbleStopReason::ShuttingDown) {
        shutdown();
        return std::nullopt;
    }
    if (!m_active) return std::nullopt;

    if (!m_paused) accumulate(mediaPositionSeconds, monotonicSeconds);
    auto decision = completionDecision();
    resetActiveSession();
    return decision;
}

void navidrome::ScrobbleSession::setEnabled(bool enabled) noexcept {
    if (m_shutdown) return;
    m_enabled = enabled;
    if (!enabled) resetActiveSession();
}

void navidrome::ScrobbleSession::shutdown() noexcept {
    m_shutdown = true;
    m_enabled = false;
    resetActiveSession();
}

void navidrome::ScrobbleSession::resetActiveSession() noexcept {
    m_active = false;
    m_paused = false;
    m_baselineValid = false;
    m_completedSubmitted = false;
    m_songId.clear();
    m_thresholdSeconds = kUnknownDurationThresholdSeconds;
    m_listenedSeconds = 0.0;
    m_lastMediaPositionSeconds = 0.0;
    m_lastMonotonicSeconds = 0.0;
}

void navidrome::ScrobbleSession::setBaseline(
        double mediaPositionSeconds, double monotonicSeconds) noexcept {
    m_baselineValid = validTime(mediaPositionSeconds) && validTime(monotonicSeconds);
    if (!m_baselineValid) return;
    m_lastMediaPositionSeconds = mediaPositionSeconds;
    m_lastMonotonicSeconds = monotonicSeconds;
}

void navidrome::ScrobbleSession::accumulate(
        double mediaPositionSeconds, double monotonicSeconds) noexcept {
    if (!validTime(mediaPositionSeconds) || !validTime(monotonicSeconds)) {
        m_baselineValid = false;
        return;
    }
    if (!m_baselineValid) {
        setBaseline(mediaPositionSeconds, monotonicSeconds);
        return;
    }

    const double mediaDelta = mediaPositionSeconds - m_lastMediaPositionSeconds;
    const double monotonicDelta = monotonicSeconds - m_lastMonotonicSeconds;
    if (mediaDelta > 0.0 && monotonicDelta >= 0.0 &&
        mediaDelta <= monotonicDelta + kPlaybackClockToleranceSeconds) {
        m_listenedSeconds += mediaDelta;
    }
    setBaseline(mediaPositionSeconds, monotonicSeconds);
}

std::optional<navidrome::ScrobbleDecision>
navidrome::ScrobbleSession::completionDecision() {
    if (!m_active || m_completedSubmitted ||
        m_listenedSeconds + 1e-9 < m_thresholdSeconds) {
        return std::nullopt;
    }
    m_completedSubmitted = true;
    return ScrobbleDecision{m_sessionId, m_songId, ScrobbleSubmission::Completed};
}
