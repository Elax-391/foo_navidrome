#include "../ServerIdentity.h"
#include "../SongJsonParser.h"
#include "../SubsonicRequestLogic.h"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool condition, const char* description) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
}
}

int main() {
    std::cout << "SubsonicExtrasTests starting\n";

    const auto response = navidrome::parseSubsonicResponseJson(R"json({
        "subsonic-response":{"status":"ok","version":"1.16.1",
        "type":"navidrome","serverVersion":"0.58.0","openSubsonic":true,
        "ignored":{"nested":[1,2,3]}}})json");
    check(response.valid && response.ok && response.type == "navidrome" &&
          response.serverVersion == "0.58.0" && response.openSubsonic,
          "successful response metadata parses");

    const auto failed = navidrome::parseSubsonicResponseJson(R"json({
        "subsonic-response":{"status":"failed","error":{"code":50,
        "message":"Not allowed \\ \"here\""}}})json");
    check(failed.valid && !failed.ok && failed.error && failed.error->code == 50 &&
          failed.error->message == "Not allowed \\ \"here\"",
          "structured Subsonic error parses escaped text");
    check(!navidrome::parseSubsonicResponseJson("{}").valid,
          "missing response wrapper is invalid");

    const auto artists = navidrome::parseArtistArrayJson(R"json({
        "indexes":{"index":[
          {"artist":[{"id":"a1","name":"A","albumCount":2,
                       "starred":"2026-08-14T00:00:00Z"}]},
          {"artist":{"id":"a2","name":"\u4e59","albumCount":-1}}
        ]}})json", "artist", "Unknown");
    check(artists.size() == 2 && artists[0].starred &&
          artists[1].name == u8"乙" && artists[1].albumCount == 0,
          "all artist index groups share robust parsing");
    const auto nonDuplicated = navidrome::parseArtistArrayJson(
        R"json({"ignored":{"id":"same"},"artist":{"id":"same"}})json");
    check(nonDuplicated.size() == 1 && nonDuplicated[0].id == "same",
          "member scanning advances from the located value, not equal prior text");

    const auto albums = navidrome::parseAlbumArrayJson(R"json({"album":[
        {"id":"al1","name":"Album","artistId":"artist-1","year":2026,
         "songCount":10,"starred":"now"},
        {"id":"al2","name":"Fallback","year":"bad","songCount":-1}
    ]})json", "album", "fallback-artist", "Unknown");
    check(albums.size() == 2 && albums[0].artistId == "artist-1" &&
          albums[0].starred && albums[1].artistId == "fallback-artist" &&
          albums[1].year == 0 && albums[1].songCount == 0,
          "album parsing validates optionals and applies fallback identity");

    const auto rated = navidrome::parseSongJson(
        R"json({"id":"song","title":"Song","userRating":5})json");
    const auto invalidRating = navidrome::parseSongJson(
        R"json({"id":"song","title":"Song","userRating":6})json");
    check(rated.userRating == 5 && !invalidRating.userRating,
          "song rating accepts only Subsonic range");

    const auto playlists = navidrome::parsePlaylistArrayJson(R"json({
        "playlists":{"playlist":{"id":"p1","name":"List","owner":"user",
        "comment":"A & B","songCount":3,"duration":12.5,"public":true,
        "created":"now","changed":"later","coverArt":"pl-p1"}}})json");
    check(playlists.size() == 1 && playlists[0].id == "p1" &&
          playlists[0].songCount == 3 && playlists[0].duration == 12.5 &&
          playlists[0].isPublic == true && playlists[0].coverArtId == "pl-p1",
          "playlist object and optional fields parse");

    const auto genres = navidrome::parseGenreArrayJson(R"json({
        "genres":{"genre":[
          {"value":"Rock","songCount":12,"albumCount":3},
          {"value":"","songCount":-1,"albumCount":-2}
        ]}})json");
    check(genres.size() == 2 && genres[0].name == "Rock" &&
          genres[0].songCount == 12 && genres[0].albumCount == 3 &&
          genres[1].songCount == 0 && genres[1].albumCount == 0,
          "genre parsing normalizes Subsonic value and validates counts");

    check(navidrome::streamTranscodeParams("opus", 192) ==
              "&format=opus&maxBitRate=192" &&
          navidrome::streamTranscodeParams("", 0).empty() &&
          navidrome::effectiveStreamSuffix("raw", "flac") == "flac" &&
          navidrome::effectiveStreamSuffix("mp3", "flac") == "mp3",
          "stream preferences build parameters and decoder suffixes consistently");
    check(navidrome::sanitizeFileName("A/B: C?. ") == "A_B_ C_" &&
          navidrome::trackIdFromURI("navidrome://track/a%2Fb?title=x") == "a/b",
          "shared download and URI helpers sanitize and decode stable values");

    const auto extensions = navidrome::parseOpenSubsonicExtensionsJson(R"json({
        "openSubsonicExtensions":{"openSubsonicExtension":[
          {"name":"formPost","versions":[1,2]},
          {"name":"unknown","versions":["bad",3]}
        ]}})json");
    check(extensions.size() == 2 && extensions[0].name == "formPost" &&
          extensions[0].versions.size() == 2 &&
          extensions[1].versions.size() == 1 && extensions[1].versions[0] == 3,
          "OpenSubsonic extensions retain names and valid versions");
    const auto overflowingExtension = navidrome::parseOpenSubsonicExtensionsJson(
        R"json({"openSubsonicExtension":{"name":"formPost",
        "versions":[999999999999999999999]}})json");
    check(overflowingExtension.size() == 1 &&
          overflowingExtension[0].versions.empty(),
          "overflowing extension versions remain absent");
    const auto directExtensions = navidrome::parseOpenSubsonicExtensionsJson(R"json({
        "openSubsonicExtensions":[{"name":"formPost","versions":[1]}]
    })json");
    check(directExtensions.size() == 1 && directExtensions[0].name == "formPost",
          "direct OpenSubsonic extension arrays parse");

    navidrome::OrderedParameters repeated = {
        {"songId", "a"}, {"songId", u8"曲/目 & = ?"}, {"songId", "a"}
    };
    check(navidrome::encodeFormParameters(repeated) ==
          "songId=a&songId=%E6%9B%B2%2F%E7%9B%AE%20%26%20%3D%20%3F&songId=a",
          "ordered encoding preserves duplicate keys and UTF-8 order");
    check(std::string(navidrome::albumListKindParameter(
              navidrome::AlbumListKind::Starred)) == "starred",
          "album list enum maps to endpoint parameter");

    const std::vector<std::string> ids = {"one", "two", "one"};
    const auto postPlan = navidrome::planPlaylistWrite(
        std::optional<std::string>("playlist"), "", ids, true, 24);
    check(postPlan.mode == navidrome::PlaylistWriteMode::SingleFormPost &&
          postPlan.initialParameters.size() == 4 &&
          postPlan.initialParameters[1].second == "one" &&
          postPlan.initialParameters[3].second == "one",
          "formPost writes stay single and retain duplicates");

    const auto getPlan = navidrome::planPlaylistWrite(
        std::nullopt, "New list", {"one"}, false, 128);
    check(getPlan.mode == navidrome::PlaylistWriteMode::SingleGet &&
          getPlan.initialParameters[0].first == "name",
          "short legacy write uses one GET");

    const auto incremental = navidrome::planPlaylistWrite(
        std::optional<std::string>("playlist"), "", ids, false, 40);
    std::vector<std::string> flattened;
    for (const auto& batch : incremental.appendBatches) {
        check(navidrome::encodeFormParameters(batch).size() <= 40 &&
              !batch.empty() && batch[0] == navidrome::OrderedParameters::value_type(
                  "playlistId", "playlist"),
              "incremental batch stays under limit");
        for (const auto& parameter : batch) {
            if (parameter.first == "songIdToAdd") flattened.push_back(parameter.second);
        }
    }
    check(incremental.mode == navidrome::PlaylistWriteMode::IncrementalGet &&
          flattened == ids,
          "long legacy write produces ordered append batches");
    const auto longNewList = navidrome::planPlaylistWrite(
        std::nullopt, "New list", ids, false, 24);
    check(longNewList.mode == navidrome::PlaylistWriteMode::Invalid,
          "long GET creation requires a server-issued playlist id before batching");

    const auto impossible = navidrome::planPlaylistWrite(
        std::optional<std::string>("playlist"), "", {std::string(100, 'x')},
        false, 16);
    check(impossible.mode == navidrome::PlaylistWriteMode::Invalid &&
          !impossible.error.empty(),
          "oversized single id is reported as invalid");

    check(navidrome::normalizeServerUrl("  HTTPS://Example.COM/api///  ") ==
          "https://example.com/api" &&
          navidrome::normalizeServerUrl("HTTPS://Example.COM?Token=AbC") ==
          "https://example.com?Token=AbC" &&
          navidrome::serverAccountIdentity("HTTPS://Example.COM/", "User") ==
          "https://example.com\nUser",
          "server identity preserves import normalization semantics");

    if (failures != 0) return 1;
    std::cout << "All Subsonic extras tests passed\n";
    return 0;
}
