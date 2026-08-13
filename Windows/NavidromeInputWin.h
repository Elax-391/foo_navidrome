#pragma once
#include <string>
#include "../SubsonicTypes.h"

namespace navidrome {

// Build a navidrome://track/<id>?title=...&artist=...&album=...&tracknumber=N&
// date=YYYY&duration=SEC&coverArt=...&suffix=mp3 URI. Metadata is embedded so
// playlists render without a network round-trip; the input handler resolves the
// real HTTP stream (with custom headers) at decode time. Mirrors the macOS
// NavidromeMakeTrackURIWithFields builder. Returns "" if id is empty.
std::string makeTrackURI(const std::string& id,
                         const std::string& title,
                         const std::string& artist,
                         const std::string& album,
                         int track,
                         int year,
                         double duration,
                         const std::string& coverArtId,
                         const std::string& suffix);

std::string makeTrackURI(const Song& song);

} // namespace navidrome
