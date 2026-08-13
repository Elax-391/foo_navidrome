#pragma once

// Pure helpers for normalizing OpenSubsonic Child metadata.  This header has
// no Windows or foobar2000 SDK dependency and is safe to use at JSON boundaries
// and in URI/playlist projections.

#include "../SubsonicTypes.h"

#include <cmath>
#include <string>
#include <vector>

namespace navidrome {

inline bool validFinite(double value) {
    return std::isfinite(value);
}

inline bool validPeak(double value) {
    return validFinite(value) && value > 0.0;
}

inline bool validReplayGain(const Song::ReplayGain& gain) {
    if (gain.trackGain && !validFinite(*gain.trackGain)) return false;
    if (gain.albumGain && !validFinite(*gain.albumGain)) return false;
    if (gain.baseGain && !validFinite(*gain.baseGain)) return false;
    if (gain.fallbackGain && !validFinite(*gain.fallbackGain)) return false;
    if (gain.trackPeak && !validPeak(*gain.trackPeak)) return false;
    if (gain.albumPeak && !validPeak(*gain.albumPeak)) return false;
    return true;
}

inline void normalizeReplayGain(Song::ReplayGain& gain) {
    if (gain.trackGain && !validFinite(*gain.trackGain)) gain.trackGain.reset();
    if (gain.albumGain && !validFinite(*gain.albumGain)) gain.albumGain.reset();
    if (gain.baseGain && !validFinite(*gain.baseGain)) gain.baseGain.reset();
    if (gain.fallbackGain && !validFinite(*gain.fallbackGain)) gain.fallbackGain.reset();
    if (gain.trackPeak && !validPeak(*gain.trackPeak)) gain.trackPeak.reset();
    if (gain.albumPeak && !validPeak(*gain.albumPeak)) gain.albumPeak.reset();
}

inline bool isTranscoded(const Song& song) {
    return (song.transcodedSuffix && !song.transcodedSuffix->empty()) ||
           (song.transcodedContentType && !song.transcodedContentType->empty());
}

inline std::string effectiveCodec(const Song& song) {
    return song.transcodedSuffix && !song.transcodedSuffix->empty()
        ? *song.transcodedSuffix : song.suffix;
}

inline std::optional<std::string> effectiveContentType(const Song& song) {
    return song.transcodedContentType && !song.transcodedContentType->empty()
        ? song.transcodedContentType : song.contentType;
}

// Keep list order while removing empty values and exact duplicates. This is
// suitable for foobar2000 multi-value metadata projection.
inline std::vector<std::string> normalizeStringList(
    const std::vector<std::string>& values) {
    std::vector<std::string> result;
    for (const auto& value : values) {
        if (value.empty()) continue;
        bool duplicate = false;
        for (const auto& existing : result) {
            if (existing == value) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) result.push_back(value);
    }
    return result;
}

inline void normalizeSongLists(Song& song) {
    if (song.grouping && !song.grouping->empty()) song.groupings.push_back(*song.grouping);
    song.genres = normalizeStringList(song.genres);
    song.groupings = normalizeStringList(song.groupings);
    song.moods = normalizeStringList(song.moods);
    normalizeReplayGain(song.replayGain);
}

} // namespace navidrome
