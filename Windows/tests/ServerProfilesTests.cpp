#include "../ServerProfiles.h"
#include "../ServerConnectionHub.h"
#include "../ServerRouteRuntime.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, const char* description) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
}

navidrome::ServerProfile profile(std::string id, std::string name,
                                 std::string publicUrl,
                                 std::string internalUrl = {}) {
    navidrome::ServerProfile value;
    value.id = std::move(id);
    value.name = std::move(name);
    value.routes.push_back({"route-1", publicUrl});
    if (!internalUrl.empty()) value.routes.push_back({"route-2", internalUrl});
    value.preferredRouteId = value.routes.front().id;
    value.publicUrl = std::move(publicUrl);
    value.internalUrl = std::move(internalUrl);
    value.username = "alice";
    value.password = "secret";
    return value;
}

void field(std::string& output, const std::string& value) {
    output += std::to_string(value.size());
    output.push_back('\n');
    output += value;
    output.push_back('\n');
}

std::string legacyDocument() {
    std::string output = "NDSP/1\n";
    field(output, "home");
    output += "1\n";
    field(output, "home");
    field(output, "Home");
    field(output, "https://legacy");
    field(output, "http://lan");
    field(output, "alice");
    field(output, "secret");
    output += "1\n";
    return output;
}

navidrome::ServerProfileState stateWithTwoProfiles() {
    navidrome::ServerProfileState state;
    state.profiles.push_back(profile("home", "Home", "https://public/home",
                                    "http://lan/home"));
    state.profiles.push_back(profile("office", u8"办公室", "https://office"));
    state.activeProfileId = "home";
    return state;
}

bool sameProfile(const navidrome::ServerProfile& left,
                 const navidrome::ServerProfile& right) {
    return left.id == right.id && left.name == right.name &&
           left.routes == right.routes &&
           left.preferredRouteId == right.preferredRouteId &&
           left.autoFailover == right.autoFailover &&
           left.publicUrl == right.publicUrl &&
           left.internalUrl == right.internalUrl &&
           left.username == right.username &&
           left.password == right.password &&
           left.selectedRoute == right.selectedRoute;
}

bool sameState(const navidrome::ServerProfileState& left,
               const navidrome::ServerProfileState& right) {
    if (left.activeProfileId != right.activeProfileId ||
        left.profiles.size() != right.profiles.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.profiles.size(); ++index) {
        if (!sameProfile(left.profiles[index], right.profiles[index]))
            return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace navidrome;
    std::cout << "ServerProfilesTests starting\n";

    const auto migrated = migrateLegacyServerProfile(
        "https://legacy", u8"用户", u8"密码");
    check(migrated.profiles.size() == 1 &&
              migrated.activeProfileId == "legacy-default" &&
              migrated.profiles[0].id == "legacy-default" &&
              migrated.profiles[0].name == u8"默认服务器" &&
              migrated.profiles[0].publicUrl == "https://legacy" &&
              migrated.profiles[0].internalUrl.empty() &&
              migrated.profiles[0].routes.size() == 1 &&
              migrated.profiles[0].routes[0].id == "route-1" &&
              migrated.profiles[0].preferredRouteId == "route-1" &&
              migrated.profiles[0].selectedRoute == ServerRoute::Public,
          "legacy migration is deterministic and lossless");
    check(static_cast<bool>(validateServerProfileState(migrated)),
          "legacy migration permits empty legacy fields");

    const auto baseline = stateWithTwoProfiles();
    check(static_cast<bool>(validateServerProfileState(baseline)),
          "well formed state validates");
    auto invalid = baseline;
    invalid.profiles[1].id = "home";
    check(validateServerProfileState(invalid).error ==
              ServerProfileError::DuplicateId,
          "duplicate ids are rejected");
    invalid = baseline;
    invalid.profiles[1].name = " home ";
    check(validateServerProfileState(invalid).error ==
              ServerProfileError::UntrimmedName,
          "stored names must be trimmed");
    invalid.profiles[1].name = "hOmE";
    check(validateServerProfileState(invalid).error ==
              ServerProfileError::DuplicateName,
          "ASCII profile names are unique case-insensitively");
    invalid = baseline;
    invalid.activeProfileId = "missing";
    check(validateServerProfileState(invalid).error ==
              ServerProfileError::MissingActiveProfile,
          "active id must resolve");
    invalid = baseline;
    invalid.profiles[0].routes.clear();
    invalid.profiles[0].selectedRoute = static_cast<ServerRoute>(2);
    check(validateServerProfileState(invalid).error ==
              ServerProfileError::InvalidRoute,
          "invalid enum values are rejected");
    invalid = baseline;
    invalid.profiles[0].name.assign("bad\0name", 8);
    check(validateServerProfileState(invalid).error ==
              ServerProfileError::EmbeddedNul,
          "NUL cannot cross cfg_string storage");
    invalid = baseline;
    invalid.profiles[0].name.assign("\xc0\xaf", 2);
    check(validateServerProfileState(invalid).error ==
              ServerProfileError::InvalidUtf8,
          "invalid UTF-8 is rejected");

    auto rich = baseline;
    rich.profiles[0].password = "line 1\nline:2\\end";
    rich.profiles[0].name = u8"家里 🎵";
    rich.profiles[0].preferredRouteId = "route-2";
    rich.profiles[0].selectedRoute = ServerRoute::Internal;
    const auto encoded = serializeServerProfileState(rich);
    check(static_cast<bool>(encoded) &&
              encoded.value.compare(0, 7, "NDSP/2\n") == 0,
          "serializer emits the versioned header");
    const auto decoded = deserializeServerProfileState(encoded.value);
    check(static_cast<bool>(decoded) && sameState(decoded.value, rich),
          "length-prefixed UTF-8 document round trips delimiters");
    const auto reencoded = serializeServerProfileState(decoded.value);
    check(static_cast<bool>(reencoded) && reencoded.value == encoded.value,
          "serialization is canonical");

    check(deserializeServerProfileState("NDSP/3\n").error ==
              ServerProfileError::UnknownVersion,
          "unknown versions are distinguished");
    check(deserializeServerProfileState("garbage").error ==
              ServerProfileError::InvalidHeader,
          "invalid headers are rejected");
    check(deserializeServerProfileState(encoded.value + "x").error ==
              ServerProfileError::TrailingData,
          "trailing bytes are rejected");
    check(deserializeServerProfileState("NDSP/2\n01\nx\n1\n").error ==
              ServerProfileError::InvalidLength,
          "non-canonical leading-zero lengths are rejected");
    check(deserializeServerProfileState("NDSP/2\n99999999999\n").error ==
              ServerProfileError::InvalidLength,
          "oversized decimal lengths are rejected without overflow");
    check(deserializeServerProfileState(encoded.value.substr(
              0, encoded.value.size() - 1)).error ==
              ServerProfileError::Truncated,
          "truncated documents are rejected");
    check(deserializeServerProfileState(
              std::string(kMaxServerProfileDocumentBytes + 1, 'x')).error ==
              ServerProfileError::DocumentTooLarge,
          "document cap is checked before parsing");

    const auto projected = projectActiveConnection(rich);
    check(static_cast<bool>(projected) &&
              projected.value.profileId == "home" &&
              projected.value.routeId == "route-2" &&
              projected.value.routeIndex == 1 &&
              projected.value.route == ServerRoute::Internal &&
              projected.value.serverUrl == "http://lan/home" &&
              projected.value.username == "alice" &&
              projected.value.password == "line 1\nline:2\\end",
          "active connection projection copies the selected route");
    invalid = baseline;
    invalid.profiles[0].publicUrl.clear();
    invalid.profiles[0].routes[0].url.clear();
    check(static_cast<bool>(validateServerProfileState(invalid)) &&
              projectActiveConnection(invalid).error ==
                  ServerProfileError::MissingRouteUrl,
          "structural state permits blank routes but projection does not");

    const auto added = addServerProfile(
        baseline, profile("travel", " Travel ", "https://travel"));
    check(static_cast<bool>(added) && added.value.profiles.size() == 3 &&
              added.value.activeProfileId == "travel" &&
              added.value.profiles.back().name == "Travel",
          "add trims, persists and activates a complete profile");
    auto incomplete = profile("bad", "Bad", "https://bad");
    incomplete.password.clear();
    check(addServerProfile(baseline, incomplete).error ==
              ServerProfileError::MissingCredentials &&
              baseline.profiles.size() == 2,
          "failed add leaves the input unchanged");
    check(addServerProfile(baseline,
                           profile("third", "HOME", "https://third")).error ==
              ServerProfileError::DuplicateName,
          "add enforces normalized name uniqueness");

    auto updatedProfile = baseline.profiles[0];
    updatedProfile.name = " Renamed ";
    updatedProfile.publicUrl = "https://new";
    updatedProfile.routes[0].url = "https://new";
    const auto updated = updateServerProfile(baseline, updatedProfile);
    check(static_cast<bool>(updated) &&
              updated.value.profiles[0].name == "Renamed" &&
              updated.value.profiles[0].publicUrl == "https://new",
          "update replaces a matching profile and trims its name");
    const auto renamed = renameServerProfile(baseline, "office", " Work ");
    check(static_cast<bool>(renamed) &&
              renamed.value.profiles[1].name == "Work",
          "rename preserves the remaining profile fields");

    const auto internal = selectServerRoute(
        baseline, "home", ServerRoute::Internal);
    check(static_cast<bool>(internal) &&
              internal.value.activeProfileId == "home" &&
              internal.value.profiles[0].selectedRoute ==
                  ServerRoute::Internal,
          "route selection also activates the selected profile");
    check(selectServerRoute(baseline, "office", ServerRoute::Internal).error ==
              ServerProfileError::MissingRouteUrl,
          "blank route cannot become active");
    const auto office = selectServerProfile(baseline, "office");
    check(static_cast<bool>(office) && office.value.activeProfileId == "office",
          "profile selection uses the profile saved route");
    check(selectServerProfile(baseline, "missing").error ==
              ServerProfileError::ProfileNotFound,
          "missing profile selection is rejected");

    const auto deletedActive = deleteServerProfile(baseline, "home");
    check(static_cast<bool>(deletedActive) &&
              deletedActive.value.profiles.size() == 1 &&
              deletedActive.value.activeProfileId == "office",
          "deleting active profile chooses the next deterministic fallback");
    auto three = added.value;
    three.activeProfileId = "office";
    const auto deletedMiddle = deleteServerProfile(three, "office");
    check(static_cast<bool>(deletedMiddle) &&
              deletedMiddle.value.activeProfileId == "travel",
          "middle deletion chooses the profile now at the same index");
    check(deleteServerProfile(migrated, "legacy-default").error ==
              ServerProfileError::CannotDeleteLastProfile,
          "the last profile cannot be deleted");

    auto routed = baseline;
    for (int index = 3; index <= 8; ++index) {
        auto result = addServerRoute(
            routed, "home", {"route-" + std::to_string(index),
                              "https://route/" + std::to_string(index)});
        check(static_cast<bool>(result), "routes can be added up to eight");
        if (result) routed = std::move(result.value);
    }
    check(addServerRoute(routed, "home", {"route-9", "https://route/9"})
              .error == ServerProfileError::TooManyRoutes,
          "the ninth route is rejected");
    const auto moved = moveServerRoute(routed, "home", "route-8", -1);
    check(static_cast<bool>(moved) &&
              moved.value.profiles[0].routes[6].id == "route-8",
          "route order can be moved without changing route ids");
    const auto removed = deleteServerRoute(routed, "home", "route-2");
    check(static_cast<bool>(removed) &&
              removed.value.profiles[0].preferredRouteId == "route-1" &&
              removed.value.profiles[0].routes.size() == 7,
          "route deletion preserves a valid preferred route");
    auto oneRoute = baseline;
    oneRoute.profiles[0].routes.resize(1);
    oneRoute.profiles[0].preferredRouteId = "route-1";
    check(deleteServerRoute(oneRoute, "home", "route-1").error ==
              ServerProfileError::CannotDeleteLastRoute,
          "the last route cannot be deleted");

    const auto legacyDecoded = deserializeServerProfileState(legacyDocument());
    check(static_cast<bool>(legacyDecoded) &&
              legacyDecoded.value.profiles[0].routes.size() == 2 &&
              legacyDecoded.value.profiles[0].preferredRouteId == "route-2" &&
              legacyDecoded.value.profiles[0].autoFailover,
          "NDSP/1 documents migrate to numbered routes");

    auto clockNow = std::chrono::steady_clock::time_point{};
    ServerRouteRuntime runtime([&]() { return clockNow; });
    RoutePlan plan;
    plan.revision = 7;
    plan.profileId = "home";
    plan.preferredRouteId = "route-1";
    plan.candidates = {
        {"route-1", "https://one"},
        {"route-2", "https://two"},
        {"route-3", "https://three"},
    };
    runtime.install(plan);
    const auto routeSnapshot = runtime.snapshot();
    check(routeSnapshot.effectiveRouteId == "route-1" &&
              routeSnapshot.candidates.size() == 3,
          "runtime starts from the persisted preferred route");
    check(ServerRouteRuntime::isEligible(RouteFailureKind::Resolve) &&
              ServerRouteRuntime::isEligible(RouteFailureKind::Connect) &&
              ServerRouteRuntime::isEligible(RouteFailureKind::Timeout) &&
              ServerRouteRuntime::isEligible(RouteFailureKind::TlsHandshake) &&
              !ServerRouteRuntime::isEligible(RouteFailureKind::InvalidUrl) &&
              !ServerRouteRuntime::isEligible(RouteFailureKind::HttpResponse) &&
              !ServerRouteRuntime::isEligible(RouteFailureKind::Cancelled),
          "only qualified transport failures allow automatic failover");
    std::atomic<int> probeCount{0};
    RouteFailoverResult concurrentResults[4];
    std::thread failovers[4];
    for (std::size_t index = 0; index < 4; ++index) {
        failovers[index] = std::thread([&, index]() {
            concurrentResults[index] = runtime.failover(
                routeSnapshot, RouteFailureKind::Timeout,
                [&](const RouteCandidate& candidate, std::string&) {
                    ++probeCount;
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    return candidate.routeId == "route-2";
                });
        });
    }
    for (auto& thread : failovers) thread.join();
    check(probeCount == 1 &&
              runtime.snapshot().effectiveRouteId == "route-2",
          "concurrent failures share one successful probe");
    int switchedResults = 0;
    for (const auto& result : concurrentResults) {
        if (result.status == RouteFailoverStatus::Switched)
            ++switchedResults;
    }
    check(switchedResults == 4,
          "single-flight waiters receive the shared switch result");

    ServerRouteRuntime coolingRuntime([&]() { return clockNow; });
    coolingRuntime.install(plan);
    int failedProbeCount = 0;
    const auto coolingSnapshot = coolingRuntime.snapshot();
    const auto failedFailover = coolingRuntime.failover(
        coolingSnapshot, RouteFailureKind::Connect,
        [&](const RouteCandidate&, std::string& error) {
            ++failedProbeCount;
            error = "offline";
            return false;
        });
    const auto coolingResult = coolingRuntime.failover(
        coolingSnapshot, RouteFailureKind::Connect,
        [&](const RouteCandidate&, std::string&) {
            ++failedProbeCount;
            return false;
        });
    check(failedFailover.status == RouteFailoverStatus::AllFailed &&
              coolingResult.status == RouteFailoverStatus::CoolingDown &&
              failedProbeCount == 2,
          "failed route probes enter a thirty-second cooldown");
    clockNow += std::chrono::seconds(31);
    coolingRuntime.failover(
        coolingSnapshot, RouteFailureKind::Connect,
        [&](const RouteCandidate&, std::string&) {
            ++failedProbeCount;
            return false;
        });
    check(failedProbeCount == 4,
          "route probes resume after cooldown expires");
    check(coolingRuntime.failover(
              coolingSnapshot, RouteFailureKind::HttpResponse,
              [&](const RouteCandidate&, std::string&) {
                  ++failedProbeCount;
                  return true;
              }).status == RouteFailoverStatus::NotEligible &&
              failedProbeCount == 4,
          "HTTP and business failures never trigger failover");

    RoutePlan disabledPlan = plan;
    disabledPlan.revision = 8;
    disabledPlan.autoFailover = false;
    ServerRouteRuntime disabledRuntime;
    disabledRuntime.install(disabledPlan);
    int guardedProbeCount = 0;
    check(disabledRuntime.failover(
              disabledRuntime.snapshot(), RouteFailureKind::Connect,
              [&](const RouteCandidate&, std::string&) {
                  ++guardedProbeCount;
                  return true;
              }).status == RouteFailoverStatus::Disabled &&
              guardedProbeCount == 0,
          "disabled automatic failover never probes alternate routes");

    ServerRouteRuntime staleRuntime;
    staleRuntime.install(plan);
    const auto staleSnapshot = staleRuntime.snapshot();
    RoutePlan replacementPlan = plan;
    replacementPlan.revision = 9;
    staleRuntime.install(replacementPlan);
    check(staleRuntime.failover(
              staleSnapshot, RouteFailureKind::Connect,
              [&](const RouteCandidate&, std::string&) {
                  ++guardedProbeCount;
                  return true;
              }).status == RouteFailoverStatus::StalePlan &&
              guardedProbeCount == 0,
          "stale route revisions never probe or overwrite a newer plan");

    std::vector<ServerConnectionEvent> events;
    auto subscription = ServerConnectionHub::get().subscribe(
        [&](const ServerConnectionEvent& event) { events.push_back(event); });
    ServerConnectionEvent event;
    event.profileId = "home";
    event.route = ServerRoute::Internal;
    event.serverUrl = "http://lan/home";
    event.username = "alice";
    const auto firstRevision = ServerConnectionHub::get().publish(event);
    event.profileId = "office";
    const auto secondRevision = ServerConnectionHub::get().publish(event);
    check(events.size() == 2 && events[0].revision == firstRevision &&
              events[1].revision == secondRevision &&
              secondRevision > firstRevision &&
              events[0].profileId == "home" &&
              events[0].serverUrl == "http://lan/home",
          "connection hub publishes password-free monotonic events");
    subscription.reset();
    ServerConnectionHub::get().publish(event);
    check(events.size() == 2,
          "reset connection subscription blocks later delivery");

    int selfDeliveryCount = 0;
    ServerConnectionSubscription selfRemoving;
    selfRemoving = ServerConnectionHub::get().subscribe(
        [&](const ServerConnectionEvent&) {
            ++selfDeliveryCount;
            selfRemoving.reset();
        });
    ServerConnectionHub::get().publish(event);
    ServerConnectionHub::get().publish(event);
    check(selfDeliveryCount == 1,
          "connection subscription can reset during delivery");

    if (failures != 0) return 1;
    std::cout << "All server profile tests passed\n";
    return 0;
}
