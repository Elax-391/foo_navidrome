#pragma once

#include "../SubsonicTypes.h"

#include <string>
#include <optional>
#include <vector>

namespace navidrome {

struct SubsonicError {
    std::optional<int> code;
    std::string message;
};

struct ParsedSubsonicResponse {
    bool valid = false;
    bool ok = false;
    std::string version;
    std::string type;
    std::string serverVersion;
    bool openSubsonic = false;
    std::optional<SubsonicError> error;
    std::string payloadJson;
};

ParsedSubsonicResponse parseSubsonicResponseJson(const std::string& json);

Artist parseArtistJson(const std::string& json,
                       const std::string& fallbackName = {});
std::vector<Artist> parseArtistArrayJson(const std::string& json,
                                         const std::string& memberName = "artist",
                                         const std::string& fallbackName = {});

Album parseAlbumJson(const std::string& json,
                     const std::string& fallbackArtistId = {},
                     const std::string& fallbackName = {});
std::vector<Album> parseAlbumArrayJson(const std::string& json,
                                       const std::string& memberName = "album",
                                       const std::string& fallbackArtistId = {},
                                       const std::string& fallbackName = {});

ServerPlaylist parsePlaylistJson(const std::string& json);
std::vector<ServerPlaylist> parsePlaylistArrayJson(
    const std::string& json, const std::string& memberName = "playlist");
std::vector<Genre> parseGenreArrayJson(
    const std::string& json, const std::string& memberName = "genre");

std::vector<MusicFolder> parseMusicFolderArrayJson(
    const std::string& json, const std::string& memberName = "musicFolder");
ScanStatus parseScanStatusJson(const std::string& json);

std::vector<OpenSubsonicExtension> parseOpenSubsonicExtensionsJson(
    const std::string& json,
    const std::string& memberName = "openSubsonicExtension");

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
