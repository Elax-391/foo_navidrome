#include "../BrowserExtrasLogic.h"
#include "../BrowserMutationHub.h"
#include "../TrackUriMetadata.h"

#include <iostream>
#include <optional>

namespace {
int failures = 0;
void check(bool condition, const char* description) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
}
}

int main() {
    std::cout << "BrowserExtrasTests starting\n";

    navidrome::Song labelSong;
    labelSong.title = "Track";
    labelSong.track = 3;
    labelSong.starred = "1";
    labelSong.userRating = 4;
    check(navidrome::formatSongLabel(labelSong, "Fallback") ==
          u8"★ 3. Track  [4/5 星]", "song label combines stable metadata");
    check(navidrome::isBrowserPlayable(navidrome::BrowserNodeKind::SmartList) &&
          !navidrome::isBrowserPlayable(navidrome::BrowserNodeKind::NavigationGroup) &&
          navidrome::isBrowserContainer(navidrome::BrowserNodeKind::ServerPlaylist),
          "node action eligibility is type based");
    const auto navigation = navidrome::groupedNavigationDefaults();
    check(navigation.groups == std::vector<navidrome::NavigationGroupKind>({
              navidrome::NavigationGroupKind::SmartLists,
              navidrome::NavigationGroupKind::ServerPlaylists,
              navidrome::NavigationGroupKind::Artists}) &&
          navigation.smartLists.size() == 7 &&
          navigation.smartLists.front() == navidrome::SmartListKind::StarredSongs &&
          navigation.smartLists.back() == navidrome::SmartListKind::RandomAlbums,
          "grouped navigation order is stable and label independent");
    check(navidrome::favoriteSmartListKind(navidrome::FavoriteKind::Song) ==
              navidrome::SmartListKind::StarredSongs &&
          navidrome::favoriteSmartListKind(navidrome::FavoriteKind::Album) ==
              navidrome::SmartListKind::StarredAlbums &&
          navidrome::favoriteSmartListKind(navidrome::FavoriteKind::Artist) ==
              navidrome::SmartListKind::StarredArtists,
          "favorite mutations target the matching smart list");

    navidrome::Song first;
    first.id = "a";
    navidrome::Song second;
    second.id = u8"曲/目";
    const auto mapping = navidrome::mapActivePlaylistUris({
        navidrome::buildTrackURI(first), "C:\\music\\local.flac",
        navidrome::buildTrackURI(second), navidrome::buildTrackURI(first),
        "navidrome://track/", ""
    });
    check(mapping.orderedSongIds == std::vector<std::string>({"a", u8"曲/目", "a"}) &&
          mapping.skippedUnsupported == 1 && mapping.skippedMalformed == 1 &&
          mapping.skippedEmpty == 1,
          "active playlist mapping preserves order and duplicates with skip reasons");

    const std::vector<navidrome::ServerPlaylist> catalog = {
        {"p1", "Mix", "alice", "", "", "", "", 3, 0.0, std::nullopt},
        {"p2", "Mix", "bob", "", "", "", "", 4, 0.0, std::nullopt},
        {"p3", "Mix (2)", "alice", "", "", "", "", 1, 0.0, std::nullopt},
        {"p4", "Mix (4)", "alice", "", "", "", "", 1, 0.0, std::nullopt},
    };
    check(navidrome::exactNameMatches(catalog, "Mix").size() == 2 &&
          navidrome::exactNameMatches(catalog, "mix").empty(),
          "same-name matching is exact and stable");
    check(navidrome::firstAvailableNumberedName("Mix", catalog) == "Mix (3)",
          "numbered copy chooses the first gap");

    const auto replace = navidrome::makeUploadPlan(
        "Mix", mapping, navidrome::UploadChoice::Replace,
        std::optional<std::string>("p2"), catalog, 6);
    check(replace.status == navidrome::UploadPlanStatus::Ready &&
          replace.targetPlaylistId == "p2" && replace.orderedSongIds == mapping.orderedSongIds &&
          replace.skippedCount == 3, "replace plan preserves mapping evidence");
    check(navidrome::makeUploadPlan(
              "Mix", mapping, navidrome::UploadChoice::Replace,
              std::optional<std::string>("missing"), catalog, 6).status ==
          navidrome::UploadPlanStatus::Invalid,
          "replace rejects an id outside exact-name matches");
    const auto copy = navidrome::makeUploadPlan(
        "Mix", mapping, navidrome::UploadChoice::NumberedCopy,
        std::nullopt, catalog, 6);
    check(copy.status == navidrome::UploadPlanStatus::Ready &&
          copy.targetName == "Mix (3)", "numbered copy plan is collision free");
    check(navidrome::makeUploadPlan(
              "Mix", mapping, navidrome::UploadChoice::Create,
              std::nullopt, catalog, 6).status ==
          navidrome::UploadPlanStatus::Invalid,
          "create cannot bypass an exact-name collision");
    check(navidrome::makeUploadPlan(
              "Mix", mapping, navidrome::UploadChoice::Cancel,
              std::nullopt, catalog, 6).status ==
          navidrome::UploadPlanStatus::Cancelled,
          "cancel never creates a write target");

    navidrome::WriteEvidence evidence;
    evidence.mutationAttempted = true;
    evidence.requestedIds = {"a", "b", "a"};
    evidence.verifiedCurrentIds = std::vector<std::string>({"a", "b", "a"});
    check(navidrome::classifyWriteOutcome(evidence) ==
          navidrome::BrowserWriteOutcome::Complete,
          "exact verification wins over transport ambiguity");
    evidence.verifiedCurrentIds = std::vector<std::string>({"a", "a", "b"});
    check(navidrome::classifyWriteOutcome(evidence) ==
          navidrome::BrowserWriteOutcome::Partial,
          "same count with different order remains partial");
    evidence.originalIds = std::vector<std::string>({"old"});
    evidence.restorationAttempted = true;
    evidence.verifiedRestoredIds = evidence.originalIds;
    check(navidrome::classifyWriteOutcome(evidence) ==
          navidrome::BrowserWriteOutcome::Restored,
          "verified restoration has a distinct outcome");
    evidence.verifiedRestoredIds.reset();
    check(navidrome::classifyWriteOutcome(evidence) ==
          navidrome::BrowserWriteOutcome::Unknown,
          "unverified restoration makes the final state unknown");
    evidence.verifiedRestoredIds = std::vector<std::string>({"different"});
    check(navidrome::classifyWriteOutcome(evidence) ==
          navidrome::BrowserWriteOutcome::Partial,
          "verified restoration mismatch remains partial");
    check(navidrome::shouldRefreshPlaylistCatalog(
              navidrome::BrowserWriteOutcome::Complete) &&
          navidrome::shouldRefreshPlaylistCatalog(
              navidrome::BrowserWriteOutcome::Partial) &&
          navidrome::shouldRefreshPlaylistCatalog(
              navidrome::BrowserWriteOutcome::Unknown) &&
          !navidrome::shouldRefreshPlaylistCatalog(
              navidrome::BrowserWriteOutcome::Restored) &&
          !navidrome::shouldRefreshPlaylistCatalog(
              navidrome::BrowserWriteOutcome::FailedNoChange),
          "catalog refresh follows outcomes that may change server state");

    std::vector<navidrome::BrowserMutationEvent> received;
    auto subscription = navidrome::BrowserMutationHub::get().subscribe(
        [&](const navidrome::BrowserMutationEvent& event) { received.push_back(event); });
    navidrome::BrowserMutationEvent favorite;
    favorite.identity = "server\nuser";
    favorite.entityKind = navidrome::FavoriteKind::Song;
    favorite.entityId = "song";
    favorite.favorite = true;
    const auto firstRevision = navidrome::BrowserMutationHub::get().publish(favorite);
    favorite.favorite = false;
    const auto secondRevision = navidrome::BrowserMutationHub::get().publish(favorite);
    check(received.size() == 2 && received[0].revision == firstRevision &&
          received[1].revision == secondRevision && secondRevision > firstRevision,
          "mutation hub assigns monotonic revisions");
    check(navidrome::shouldApplyBrowserMutation(
              received[1], "server\nuser", firstRevision) &&
          !navidrome::shouldApplyBrowserMutation(
              received[1], "other\nuser", firstRevision) &&
          !navidrome::shouldApplyBrowserMutation(
              received[1], "server\nuser", secondRevision),
          "mutation application is identity scoped and revision ordered");
    subscription.reset();
    navidrome::BrowserMutationHub::get().publish(favorite);
    check(received.size() == 2, "reset subscription blocks later delivery");

    int selfDeliveryCount = 0;
    navidrome::BrowserMutationSubscription selfRemoving;
    selfRemoving = navidrome::BrowserMutationHub::get().subscribe(
        [&](const navidrome::BrowserMutationEvent&) {
            ++selfDeliveryCount;
            selfRemoving.reset();
        });
    navidrome::BrowserMutationHub::get().publish(favorite);
    navidrome::BrowserMutationHub::get().publish(favorite);
    check(selfDeliveryCount == 1,
          "subscription can be removed safely during event delivery");

    if (failures != 0) return 1;
    std::cout << "All Browser extras tests passed\n";
    return 0;
}
