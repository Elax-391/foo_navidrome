#pragma once

#include "../SubsonicTypes.h"
#include "MediaEnrichmentLogic.h"
#include "SongMetadata.h"

#include <cmath>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace navidrome {

inline constexpr const char* kTrackUriPrefix = "navidrome://track/";
inline constexpr size_t kTrackUriPrefixLength = 18;

inline std::string buildTrackURI(const Song& song) {
    if (song.id.empty()) return {};
    std::vector<std::string> query;
    auto text = [&](const char* key, const std::string& value) {
        if (!value.empty()) query.push_back(std::string(key) + "=" + uriEncode(value));
    };
    auto optionalText = [&](const char* key, const std::optional<std::string>& value) {
        if (value) text(key, *value);
    };
    auto number = [&](const char* key, const auto& value, auto valid) {
        if (value && valid(*value)) query.push_back(std::string(key) + "=" + std::to_string(*value));
    };
    auto nonNegative = [](const auto& value) { return value >= 0; };
    auto positive = [](const auto& value) { return value > 0; };
    auto finite = [](double value) { return std::isfinite(value); };
    text("title", song.title); text("artist", song.artist); text("album", song.album);
    if (song.track > 0) query.push_back("tracknumber=" + std::to_string(song.track));
    if (song.year > 0) query.push_back("date=" + std::to_string(song.year));
    if (song.duration > 0 && std::isfinite(song.duration))
        query.push_back("duration=" + std::to_string(song.duration));
    text("coverArt", !song.coverArtId.empty() ? song.coverArtId
                                              : song.coverArt.value_or(""));
    text("suffix", song.suffix);
    text("artistId", song.artistId); text("albumId", song.albumId);
    text("created", song.created);
    optionalText("albumArtist", song.albumArtist); optionalText("displayArtist", song.displayArtist);
    optionalText("sortName", song.sortName); optionalText("composer", song.composer);
    optionalText("displayComposer", song.displayComposer); optionalText("comment", song.comment);
    optionalText("isrc", song.isrc); optionalText("mbid", song.musicBrainzId);
    optionalText("mbartistid", song.musicBrainzArtistId); optionalText("mbalbumid", song.musicBrainzAlbumId);
    optionalText("mbreleaseartistid", song.musicBrainzReleaseArtistId);
    optionalText("explicit", song.explicitStatus); optionalText("parent", song.parent);
    optionalText("path", song.path); optionalText("starred", song.starred);
    optionalText("played", song.played); optionalText("contentType", song.contentType);
    optionalText("transcodedSuffix", song.transcodedSuffix);
    optionalText("transcodedContentType", song.transcodedContentType);
    for (const auto& value : song.genres) text("genre", value);
    bool wroteGrouping = false;
    for (const auto& value : song.groupings) {
        text("grouping", value);
        if (song.grouping && value == *song.grouping) wroteGrouping = true;
    }
    if (song.grouping && !wroteGrouping) text("grouping", *song.grouping);
    for (const auto& value : song.moods) text("mood", value);
    number("playCount", song.playCount, nonNegative); number("discnumber", song.discNumber, positive);
    number("bpm", song.bpm, positive); number("size", song.size, nonNegative);
    number("bitrate", song.bitRate, positive); number("bitdepth", song.bitDepth, positive);
    number("samplerate", song.samplingRate, positive); number("channels", song.channelCount, positive);
    number("rgTrackGain", song.replayGain.trackGain, finite);
    number("rgAlbumGain", song.replayGain.albumGain, finite);
    number("rgTrackPeak", song.replayGain.trackPeak, positive);
    number("rgAlbumPeak", song.replayGain.albumPeak, positive);
    number("rgBaseGain", song.replayGain.baseGain, finite);
    number("rgFallbackGain", song.replayGain.fallbackGain, finite);

    std::string uri = std::string(kTrackUriPrefix) + uriEncode(song.id);
    for (const auto& value : query) uri += (uri.find('?') == std::string::npos ? "?" : "&") + value;
    return uri;
}

inline bool parseTrackURI(const std::string& uri, Song& song) {
    if (uri.compare(0, kTrackUriPrefixLength, kTrackUriPrefix) != 0) return false;
    const auto separator = uri.find('?', kTrackUriPrefixLength);
    song = {};
    song.id = uriDecode(uri.substr(kTrackUriPrefixLength,
        separator == std::string::npos ? std::string::npos : separator - kTrackUriPrefixLength));
    if (song.id.empty()) return false;

    auto parseInt = [](const std::string& text, std::optional<int>& out,
                       bool positive = false, bool nonNegative = false) {
        errno = 0;
        char* end = nullptr; const long value = strtol(text.c_str(), &end, 10);
        if (!text.empty() && end != text.c_str() && *end == '\0' && errno != ERANGE &&
            value >= (std::numeric_limits<int>::min)() &&
            value <= (std::numeric_limits<int>::max)() &&
            (!positive || value > 0) && (!nonNegative || value >= 0))
            out = static_cast<int>(value);
    };
    auto parseInt64 = [](const std::string& text, std::optional<long long>& out) {
        errno = 0;
        char* end = nullptr; const long long value = strtoll(text.c_str(), &end, 10);
        if (!text.empty() && end != text.c_str() && *end == '\0' &&
            errno != ERANGE && value >= 0) out = value;
    };
    auto parseDouble = [](const std::string& text, std::optional<double>& out, bool positive = false) {
        char* end = nullptr; double value = strtod(text.c_str(), &end);
        if (!text.empty() && end != text.c_str() && *end == '\0' && std::isfinite(value) &&
            (!positive || value > 0.0)) out = value;
    };
    if (separator == std::string::npos) return true;
    size_t offset = separator + 1;
    while (offset <= uri.size()) {
        const auto amp = uri.find('&', offset);
        const auto pair = uri.substr(offset, amp == std::string::npos ? std::string::npos : amp - offset);
        const auto equals = pair.find('=');
        const auto key = pair.substr(0, equals);
        const auto value = equals == std::string::npos ? std::string{} : uriDecode(pair.substr(equals + 1));
        if (key == "title") song.title = value; else if (key == "artist") song.artist = value;
        else if (key == "album") song.album = value; else if (key == "albumArtist") song.albumArtist = value;
        else if (key == "displayArtist") song.displayArtist = value; else if (key == "sortName") song.sortName = value;
        else if (key == "composer") song.composer = value; else if (key == "displayComposer") song.displayComposer = value;
        else if (key == "comment") song.comment = value; else if (key == "isrc") song.isrc = value;
        else if (key == "mbid") song.musicBrainzId = value; else if (key == "mbartistid") song.musicBrainzArtistId = value;
        else if (key == "mbalbumid") song.musicBrainzAlbumId = value; else if (key == "mbreleaseartistid") song.musicBrainzReleaseArtistId = value;
        else if (key == "artistId") song.artistId = value; else if (key == "albumId") song.albumId = value;
        else if (key == "created") song.created = value;
        else if (key == "explicit") song.explicitStatus = value; else if (key == "parent") song.parent = value;
        else if (key == "path") song.path = value; else if (key == "starred") song.starred = value;
        else if (key == "played") song.played = value; else if (key == "contentType") song.contentType = value;
        else if (key == "transcodedSuffix") song.transcodedSuffix = value;
        else if (key == "transcodedContentType") song.transcodedContentType = value;
        else if (key == "coverArt") { song.coverArtId = value; song.coverArt = value; }
        else if (key == "suffix") song.suffix = value;
        else if (key == "genre") song.genres.push_back(value);
        else if (key == "grouping") {
            song.groupings.push_back(value);
            if (!song.grouping) song.grouping = value;
        }
        else if (key == "mood") song.moods.push_back(value); else if (key == "tracknumber") { std::optional<int> v; parseInt(value, v, true); if (v) song.track = *v; }
        else if (key == "date") { std::optional<int> v; parseInt(value, v, true); if (v) song.year = *v; }
        else if (key == "duration") { std::optional<double> v; parseDouble(value, v, true); if (v) song.duration = *v; }
        else if (key == "playCount") parseInt(value, song.playCount, false, true);
        else if (key == "discnumber") parseInt(value, song.discNumber, true);
        else if (key == "bpm") parseDouble(value, song.bpm, true); else if (key == "size") parseInt64(value, song.size);
        else if (key == "bitrate") parseInt(value, song.bitRate, true);
        else if (key == "bitdepth") parseInt(value, song.bitDepth, true);
        else if (key == "samplerate") parseInt(value, song.samplingRate, true);
        else if (key == "channels") parseInt(value, song.channelCount, true);
        else if (key == "rgTrackGain") parseDouble(value, song.replayGain.trackGain);
        else if (key == "rgAlbumGain") parseDouble(value, song.replayGain.albumGain);
        else if (key == "rgTrackPeak") parseDouble(value, song.replayGain.trackPeak, true);
        else if (key == "rgAlbumPeak") parseDouble(value, song.replayGain.albumPeak, true);
        else if (key == "rgBaseGain") parseDouble(value, song.replayGain.baseGain);
        else if (key == "rgFallbackGain") parseDouble(value, song.replayGain.fallbackGain);
        if (amp == std::string::npos) break;
        offset = amp + 1;
    }
    normalizeSongLists(song);
    return true;
}

} // namespace navidrome
