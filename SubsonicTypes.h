#pragma once
// Pure C++ types shared between all platform implementations.
// No ObjC, no Windows headers — safe to include anywhere.

#include <string>
#include <vector>

namespace navidrome {

struct Artist {
    std::string id;
    std::string name;
    std::string coverArtId;
    int albumCount = 0;
};

struct Album {
    std::string id;
    std::string name;
    std::string artist;
    std::string artistId;
    std::string coverArtId;
    int year      = 0;
    int songCount = 0;
};

struct Song {
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
