#pragma once
#include "../SubsonicTypes.h"
#include "MediaEnrichmentLogic.h"
#include <string>
#include <vector>
#include <cstdint>

namespace navidrome {

struct SubsonicRequestContext {
    std::string serverUrl;
    std::string username;
    std::string password;
    std::string salt;
    std::string customHeaders;
};

// Windows Subsonic API client (WinHTTP-based).
// Mirrors the ObjC SubsonicClient used on macOS.
class SubsonicClientWin {
public:
    static SubsonicClientWin& get();

    bool isConfigured() const;
    SubsonicRequestContext snapshot() const;
    bool ping(std::string& outError);
    bool ping(const SubsonicRequestContext& context, std::string& outError);

    ServerInfo getServerInfo(const SubsonicRequestContext& context, std::string& outError);
    std::vector<MusicFolder> getMusicFolders(const SubsonicRequestContext& context,
                                              std::string& outError);
    ScanStatus getScanStatus(const SubsonicRequestContext& context, std::string& outError);
    std::vector<Song> getSongsPage(const SubsonicRequestContext& context,
                                   std::size_t offset, std::size_t count,
                                   std::string& outError,
                                   bool* outUnsupported = nullptr);

    std::vector<Artist>  getArtists(std::string& outError);
    std::vector<Artist>  getArtists(const SubsonicRequestContext& context,
                                    std::string& outError);
    std::vector<Album>   getAlbumsForArtist(const std::string& artistId, std::string& outError);
    std::vector<Album>   getAlbumsForArtist(const SubsonicRequestContext& context,
                                            const std::string& artistId,
                                            std::string& outError);
    std::vector<Song>    getSongsForAlbum(const std::string& albumId, std::string& outError);
    std::vector<Song>    getSongsForAlbum(const SubsonicRequestContext& context,
                                          const std::string& albumId,
                                          std::string& outError);
    SearchResults        search(const std::string& query, std::string& outError);

    std::string streamURL(const std::string& songId);
    std::string coverArtURL(const std::string& id, int size = 0);
    std::string coverArtURL(const SubsonicRequestContext& context,
                            const std::string& id, int size = 0) const;

    // User-configured extra HTTP headers ("Name: Value" lines) applied to every
    // request — API, cover art and audio stream. Shared so the WinHTTP clients
    // and the navidrome:// input handler all send the same set.
    static std::vector<std::string> customHeaderLines();
    // Same headers joined as a single CRLF-delimited wide string for
    // WinHttpAddRequestHeaders (empty if none configured).
    static std::wstring customHeadersWide();

    // Generate Subsonic token from password + salt (md5(password + salt))
    static std::string generateToken(const std::string& password, const std::string& salt);

    // Binary fetch for cover art (PRD C2-C4, design §2.3)
    struct BinaryFetchResult {
        FetchClass cls;
        uint32_t   httpStatus;
        std::string contentType;
        std::vector<uint8_t> body;
    };
    BinaryFetchResult httpGetBinary(const SubsonicRequestContext& context,
                                    const std::string& url,
                                    std::size_t maxBytes,
                                    class abort_callback& abort) const;

private:
    SubsonicClientWin() = default;

    std::string authParams(const SubsonicRequestContext& context) const;
    std::string buildURL(const SubsonicRequestContext& context,
                         const std::string& endpoint,
                         const std::string& extra = "") const;
    // Synchronous HTTP GET; returns body or "" on error (sets outError).
    std::string httpGet(const SubsonicRequestContext& context,
                        const std::string& url, std::string& outError) const;
};

} // namespace navidrome
