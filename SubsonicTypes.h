#pragma once
// Pure C++ types shared between all platform implementations.
// No ObjC, no Windows headers — safe to include anywhere.

#include <string>
#include <optional>
#include <vector>

namespace navidrome {

struct Artist {
    std::string id;
    std::string name;
    std::string coverArtId;
    int albumCount = 0;
    std::optional<std::string> starred;
};

struct Album {
    std::string id;
    std::string name;
    std::string artist;
    std::string artistId;
    std::string coverArtId;
    int year      = 0;
    int songCount = 0;
    std::optional<std::string> starred;
};

struct Song {
    // Legacy fields retained for source compatibility with existing callers.
    std::string id;
    std::string title;
    std::string artist;
    std::string artistId;
    std::string album;
    std::string albumId;
    std::string coverArtId;
    std::string suffix;
    std::string created;
    int    track    = 0;
    int    year     = 0;
    double duration = 0.0;

    // OpenSubsonic Child identity/catalog fields.
    std::optional<std::string> parent;
    std::optional<std::string> path;
    std::optional<std::string> coverArt;
    std::optional<std::string> contentType;
    std::optional<std::string> transcodedSuffix;
    std::optional<std::string> transcodedContentType;
    std::optional<std::string> starred;
    std::optional<std::string> played;
    std::optional<int> playCount;
    std::optional<int> userRating;

    // Extended tags. Lists preserve server ordering and multi-value tags.
    std::optional<std::string> albumArtist;
    std::optional<std::string> displayArtist;
    std::optional<std::string> sortName;
    std::optional<std::string> composer;
    std::optional<std::string> displayComposer;
    std::optional<std::string> comment;
    std::optional<std::string> isrc;
    std::optional<std::string> musicBrainzId;
    std::optional<std::string> musicBrainzArtistId;
    std::optional<std::string> musicBrainzAlbumId;
    std::optional<std::string> musicBrainzReleaseArtistId;
    std::optional<std::string> explicitStatus;
    std::optional<std::string> grouping;
    std::optional<int> discNumber;
    std::optional<double> bpm;
    std::vector<std::string> genres;
    std::vector<std::string> groupings;
    std::vector<std::string> moods;

    // Server-reported media facts. Optional values avoid fabricating defaults.
    std::optional<long long> size;
    std::optional<int> bitRate;
    std::optional<int> bitDepth;
    std::optional<int> samplingRate;
    std::optional<int> channelCount;

    struct ReplayGain {
        std::optional<double> trackGain;
        std::optional<double> albumGain;
        std::optional<double> trackPeak;
        std::optional<double> albumPeak;
        std::optional<double> baseGain;
        std::optional<double> fallbackGain;

        bool empty() const {
            return !trackGain && !albumGain && !trackPeak && !albumPeak &&
                   !baseGain && !fallbackGain;
        }
    } replayGain;
};

struct MusicFolder {
    std::string id;
    std::string name;
};

struct ServerInfo {
    std::string type;
    std::string version;
    bool openSubsonic = false;
};

struct ScanStatus {
    bool scanning = false;
    std::string lastScan;
};

struct SearchResults {
    std::vector<Artist> artists;
    std::vector<Album>  albums;
    std::vector<Song>   songs;
};

enum class FavoriteKind {
    Song,
    Album,
    Artist,
};

enum class AlbumListKind {
    Newest,
    Frequent,
    Recent,
    Random,
    Starred,
};

struct StarredResults {
    std::vector<Artist> artists;
    std::vector<Album> albums;
    std::vector<Song> songs;
};

struct ServerPlaylist {
    std::string id;
    std::string name;
    std::string owner;
    std::string comment;
    std::string coverArtId;
    std::string created;
    std::string changed;
    int songCount = 0;
    double duration = 0.0;
    std::optional<bool> isPublic;
};

struct ServerPlaylistDetails {
    ServerPlaylist playlist;
    std::vector<Song> songs;
};

struct OpenSubsonicExtension {
    std::string name;
    std::vector<int> versions;
};

struct OpenSubsonicCapabilities {
    bool formPost = false;
    std::vector<OpenSubsonicExtension> extensions;
};

enum class PlaylistWriteState {
    Accepted,
    Complete,
    Unchanged,
    Partial,
    Unknown,
    Failed,
};

struct PlaylistWriteResult {
    PlaylistWriteState state = PlaylistWriteState::Failed;
    ServerPlaylist playlist;
    std::size_t requestedCount = 0;
    std::size_t actualCount = 0;
    std::size_t addedCount = 0;
    std::size_t removedCount = 0;
    bool verified = false;
    bool restored = false;
    std::string error;
};

// Parse a multiline custom-headers blob (one "Name: Value" per line) into
// trimmed, non-empty header lines suitable for HTTP request headers. Blank
// lines and lines starting with '#' (treated as comments) are skipped.
// Shared by every platform so API calls and audio streaming send the same set
// (e.g. Cloudflare Access service-token headers for a Zero Trust tunnel).
inline std::vector<std::string> parseHeaderLines(const std::string& blob) {
    std::vector<std::string> out;
    std::string line;
    auto flush = [&]() {
        const char* ws = " \t\r\n";
        size_t b = line.find_first_not_of(ws);
        size_t e = line.find_last_not_of(ws);
        if (b != std::string::npos) {
            std::string trimmed = line.substr(b, e - b + 1);
            if (!trimmed.empty() && trimmed[0] != '#')
                out.push_back(trimmed);
        }
        line.clear();
    };
    for (char ch : blob) {
        if (ch == '\n') flush();
        else            line.push_back(ch);
    }
    flush();
    return out;
}

} // namespace navidrome
