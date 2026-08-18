#include "ServerRouteRuntime.h"

#include <algorithm>
#include <utility>

namespace navidrome {

ServerRouteRuntime::ServerRouteRuntime(Clock clock)
    : m_clock(std::move(clock)) {}

std::chrono::steady_clock::time_point ServerRouteRuntime::now() const {
    return m_clock ? m_clock() : std::chrono::steady_clock::now();
}

std::vector<RouteCandidate> ServerRouteRuntime::orderedCandidates(
        const RoutePlan& plan) {
    std::vector<RouteCandidate> ordered;
    ordered.reserve(plan.candidates.size());
    const auto preferred = std::find_if(
        plan.candidates.begin(), plan.candidates.end(),
        [&](const RouteCandidate& candidate) {
            return candidate.routeId == plan.preferredRouteId;
        });
    if (preferred != plan.candidates.end()) ordered.push_back(*preferred);
    for (const auto& candidate : plan.candidates) {
        if (preferred != plan.candidates.end() &&
            candidate.routeId == preferred->routeId) {
            continue;
        }
        ordered.push_back(candidate);
    }
    return ordered;
}

void ServerRouteRuntime::install(RoutePlan plan) {
    std::lock_guard lock(m_mutex);
    m_state.plan = std::move(plan);
    const auto ordered = orderedCandidates(m_state.plan);
    m_state.plan.candidates = ordered;
    m_state.effectiveRouteId = m_state.plan.preferredRouteId;
    if (m_state.effectiveRouteId.empty() && !ordered.empty())
        m_state.effectiveRouteId = ordered.front().routeId;
    m_state.cooldownUntil = {};
    m_state.lastResult = {};
}

RoutePlanSnapshot ServerRouteRuntime::snapshot() const {
    std::lock_guard lock(m_mutex);
    RoutePlanSnapshot snapshot;
    snapshot.revision = m_state.plan.revision;
    snapshot.profileId = m_state.plan.profileId;
    snapshot.preferredRouteId = m_state.plan.preferredRouteId;
    snapshot.effectiveRouteId = m_state.effectiveRouteId;
    snapshot.autoFailover = m_state.plan.autoFailover;
    snapshot.candidates = m_state.plan.candidates;
    return snapshot;
}

bool ServerRouteRuntime::isEligible(RouteFailureKind failure) noexcept {
    return failure == RouteFailureKind::Resolve ||
           failure == RouteFailureKind::Connect ||
           failure == RouteFailureKind::Timeout ||
           failure == RouteFailureKind::TlsHandshake;
}

RouteFailoverResult ServerRouteRuntime::failover(
        const RoutePlanSnapshot& snapshot, RouteFailureKind failure,
        const Probe& probe) {
    if (!isEligible(failure))
        return {RouteFailoverStatus::NotEligible, {}, "failure is not transport-level"};

    std::unique_lock lock(m_mutex);
    if (!m_state.plan.autoFailover)
        return {RouteFailoverStatus::Disabled, {}, "automatic failover disabled"};
    if (snapshot.revision != m_state.plan.revision ||
        snapshot.profileId != m_state.plan.profileId)
        return {RouteFailoverStatus::StalePlan, {}, "route plan is stale"};
    if (m_state.effectiveRouteId != snapshot.effectiveRouteId)
        return {RouteFailoverStatus::StalePlan, {}, "effective route changed"};
    if (m_state.plan.candidates.size() < 2)
        return {RouteFailoverStatus::NoAlternate, {}, "no alternate route"};

    const auto currentTime = now();
    if (currentTime < m_state.cooldownUntil)
        return {RouteFailoverStatus::CoolingDown, {}, "route probe cooling down"};
    if (m_state.probing) {
        m_probeDone.wait(lock, [&]() { return !m_state.probing; });
        return m_state.lastResult;
    }

    m_state.probing = true;
    const auto candidates = m_state.plan.candidates;
    const auto currentRoute = m_state.effectiveRouteId;
    const auto revision = m_state.plan.revision;
    lock.unlock();

    RouteFailoverResult result;
    for (const auto& candidate : candidates) {
        if (candidate.routeId == currentRoute) continue;
        std::string error;
        if (probe && probe(candidate, error)) {
            result.status = RouteFailoverStatus::Switched;
            result.routeId = candidate.routeId;
            result.reason = "automatic route failover";
            break;
        }
        if (!error.empty()) result.reason = std::move(error);
    }
    if (result.status != RouteFailoverStatus::Switched) {
        result.status = RouteFailoverStatus::AllFailed;
        if (result.reason.empty()) result.reason = "all alternate routes failed";
    }

    lock.lock();
    if (m_state.plan.revision != revision ||
        m_state.effectiveRouteId != currentRoute) {
        result.status = RouteFailoverStatus::StalePlan;
        result.routeId.clear();
    } else if (result.status == RouteFailoverStatus::Switched) {
        m_state.effectiveRouteId = result.routeId;
        m_state.cooldownUntil = {};
    } else {
        m_state.cooldownUntil = now() + cooldown();
    }
    m_state.lastResult = result;
    m_state.probing = false;
    lock.unlock();
    m_probeDone.notify_all();
    return result;
}

} // namespace navidrome
