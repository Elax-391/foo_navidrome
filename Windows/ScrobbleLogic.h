#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace navidrome {

enum class ScrobbleSubmission {
    NowPlaying,
    Completed,
};

enum class ScrobbleStopReason {
    User,
    EndOfFile,
    StartingAnother,
    ShuttingDown,
};

struct ScrobbleDecision {
    std::uint64_t sessionId = 0;
    std::string songId;
    ScrobbleSubmission submission = ScrobbleSubmission::NowPlaying;
};

// Pure playback-session reducer. It counts only plausible forward media-time
// deltas and never treats an absolute playback position as listened time.
class ScrobbleSession {
public:
    explicit ScrobbleSession(bool enabled = true) noexcept;

    std::optional<ScrobbleDecision> startTrack(
        std::string songId, double durationSeconds,
        double mediaPositionSeconds, double monotonicSeconds);
    std::optional<ScrobbleDecision> onTime(
        double mediaPositionSeconds, double monotonicSeconds);
    std::optional<ScrobbleDecision> onPause(
        bool paused, double mediaPositionSeconds, double monotonicSeconds);
    void onSeek(double mediaPositionSeconds, double monotonicSeconds) noexcept;
    std::optional<ScrobbleDecision> onStop(
        ScrobbleStopReason reason, double mediaPositionSeconds,
        double monotonicSeconds);

    void setEnabled(bool enabled) noexcept;
    void shutdown() noexcept;

    bool enabled() const noexcept { return m_enabled; }
    bool active() const noexcept { return m_active; }
    bool completedSubmitted() const noexcept { return m_completedSubmitted; }
    std::uint64_t sessionId() const noexcept { return m_sessionId; }
    double listenedSeconds() const noexcept { return m_listenedSeconds; }
    double completionThresholdSeconds() const noexcept { return m_thresholdSeconds; }

private:
    void resetActiveSession() noexcept;
    void setBaseline(double mediaPositionSeconds, double monotonicSeconds) noexcept;
    void accumulate(double mediaPositionSeconds, double monotonicSeconds) noexcept;
    std::optional<ScrobbleDecision> completionDecision();

    bool m_enabled = true;
    bool m_shutdown = false;
    bool m_active = false;
    bool m_paused = false;
    bool m_baselineValid = false;
    bool m_completedSubmitted = false;
    std::uint64_t m_sessionId = 0;
    std::string m_songId;
    double m_thresholdSeconds = 240.0;
    double m_listenedSeconds = 0.0;
    double m_lastMediaPositionSeconds = 0.0;
    double m_lastMonotonicSeconds = 0.0;
};

} // namespace navidrome
