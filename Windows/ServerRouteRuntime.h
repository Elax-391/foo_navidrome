#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace navidrome {

struct RouteCandidate {
    std::string routeId;
    std::string serverUrl;
};

struct RoutePlan {
    std::uint64_t revision = 0;
    std::string profileId;
    std::string preferredRouteId;
    bool autoFailover = true;
    std::vector<RouteCandidate> candidates;
};

struct RoutePlanSnapshot {
    std::uint64_t revision = 0;
    std::string profileId;
    std::string preferredRouteId;
    std::string effectiveRouteId;
    bool autoFailover = true;
    std::vector<RouteCandidate> candidates;
};

enum class RouteFailureKind {
    Resolve,
    Connect,
    Timeout,
    TlsHandshake,
    InvalidUrl,
    HttpResponse,
    SubsonicResponse,
    Cancelled,
    Other,
};

enum class RouteFailoverStatus {
    NotEligible,
    Disabled,
    StalePlan,
    NoAlternate,
    CoolingDown,
    Switched,
    AllFailed,
};

struct RouteFailoverResult {
    RouteFailoverStatus status = RouteFailoverStatus::NotEligible;
    std::string routeId;
    std::string reason;
};

class ServerRouteRuntime {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    using Probe = std::function<bool(const RouteCandidate&, std::string&)>;

    explicit ServerRouteRuntime(Clock clock = {});

    void install(RoutePlan plan);
    RoutePlanSnapshot snapshot() const;
    RouteFailoverResult failover(const RoutePlanSnapshot& snapshot,
                                 RouteFailureKind failure,
                                 const Probe& probe);

    static bool isEligible(RouteFailureKind failure) noexcept;
    static constexpr std::chrono::seconds cooldown() noexcept {
        return std::chrono::seconds(30);
    }

private:
    struct State {
        RoutePlan plan;
        std::string effectiveRouteId;
        std::chrono::steady_clock::time_point cooldownUntil{};
        bool probing = false;
        RouteFailoverResult lastResult;
    };

    static std::vector<RouteCandidate> orderedCandidates(
        const RoutePlan& plan);
    std::chrono::steady_clock::time_point now() const;

    mutable std::mutex m_mutex;
    std::condition_variable m_probeDone;
    State m_state;
    Clock m_clock;
};

} // namespace navidrome
