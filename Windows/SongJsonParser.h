#pragma once

#include "../SubsonicTypes.h"

#include <string>
#include <vector>

namespace navidrome {

// Parse one Subsonic/OpenSubsonic Child object. Optional or malformed fields
// are ignored so a valid song identity remains playable on older servers.
Song parseSongJson(const std::string& json,
                   const std::string& fallbackAlbumId = {},
                   const std::string& fallbackTitle = {});

// Locate a named song array anywhere in a Subsonic response and parse each
// Child object with the same escaping and validation rules as parseSongJson().
std::vector<Song> parseSongArrayJson(const std::string& json,
                                     const std::string& memberName = "song",
                                     const std::string& fallbackAlbumId = {},
                                     const std::string& fallbackTitle = {});

} // namespace navidrome
