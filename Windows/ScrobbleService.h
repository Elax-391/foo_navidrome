#pragma once

#include "ScrobbleLogic.h"
#include "SubsonicClientWin.h"

#include <string>

namespace navidrome {

class ScrobbleCoordinator {
public:
    static ScrobbleCoordinator& get();

    void setEnabled(bool enabled) noexcept;
    void onNewTrack(std::string songId, double durationSeconds,
                    SubsonicRequestContext context,
                    std::string identity, double monotonicSeconds) noexcept;
    void onTime(double mediaPositionSeconds, double monotonicSeconds) noexcept;
    void onPause(bool paused, double monotonicSeconds) noexcept;
    void onSeek(double mediaPositionSeconds, double monotonicSeconds) noexcept;
    void onStop(ScrobbleStopReason reason, double monotonicSeconds) noexcept;
    void shutdown() noexcept;

private:
    ScrobbleCoordinator();
    ~ScrobbleCoordinator();
    ScrobbleCoordinator(const ScrobbleCoordinator&) = delete;
    ScrobbleCoordinator& operator=(const ScrobbleCoordinator&) = delete;

    void dispatch(const std::optional<ScrobbleDecision>& decision) noexcept;

    class Impl;
    Impl* m_impl;
};

} // namespace navidrome
