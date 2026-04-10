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
    int    track    = 0;
    int    year     = 0;
    double duration = 0.0;
};

struct SearchResults {
    std::vector<Artist> artists;
    std::vector<Album>  albums;
    std::vector<Song>   songs;
};

} // namespace navidrome
