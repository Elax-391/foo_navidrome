#pragma once
#include "../SubsonicTypes.h"
#include <string>
#include <vector>

namespace navidrome {

// Windows Subsonic API client (WinHTTP-based).
// Mirrors the ObjC SubsonicClient used on macOS.
class SubsonicClientWin {
public:
    static SubsonicClientWin& get();

    bool isConfigured() const;
    bool ping(std::string& outError);

    std::vector<Artist>  getArtists(std::string& outError);
    std::vector<Album>   getAlbumsForArtist(const std::string& artistId, std::string& outError);
    std::vector<Song>    getSongsForAlbum(const std::string& albumId, std::string& outError);
    SearchResults        search(const std::string& query, std::string& outError);

    std::string streamURL(const std::string& songId);
    std::string coverArtURL(const std::string& id, int size = 0);

private:
    SubsonicClientWin() = default;

    std::string authParams() const;
    std::string buildURL(const std::string& endpoint, const std::string& extra = "") const;
    // Synchronous HTTP GET; returns body or "" on error (sets outError).
    std::string httpGet(const std::string& url, std::string& outError) const;
};

} // namespace navidrome
