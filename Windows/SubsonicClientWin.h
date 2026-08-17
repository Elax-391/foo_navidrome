#pragma once
#include "../SubsonicTypes.h"
#include "MediaEnrichmentLogic.h"
#include "SubsonicRequestLogic.h"
#include <optional>
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
    SearchResults        search(const SubsonicRequestContext& context,
                                const std::string& query, std::string& outError);

    std::vector<Album> getAlbumList(const SubsonicRequestContext& context,
                                    AlbumListKind kind, std::size_t count,
                                    std::string& outError);
    StarredResults getStarred(const SubsonicRequestContext& context,
                              std::string& outError);
    std::vector<Genre> getGenres(const SubsonicRequestContext& context,
                                 std::string& outError);
    std::vector<Song> getSongsForGenre(const SubsonicRequestContext& context,
                                       const std::string& genre,
                                       std::size_t count,
                                       std::string& outError);
    bool setFavorite(const SubsonicRequestContext& context, FavoriteKind kind,
                     const std::string& id, bool favorite,
                     std::string& outError);
    bool setRating(const SubsonicRequestContext& context, const std::string& songId,
                   int rating, std::string& outError);
    std::vector<ServerPlaylist> getPlaylists(
        const SubsonicRequestContext& context, std::string& outError);
    ServerPlaylistDetails getPlaylist(const SubsonicRequestContext& context,
                                      const std::string& playlistId,
                                      std::string& outError);
    OpenSubsonicCapabilities getOpenSubsonicCapabilities(
        const SubsonicRequestContext& context, std::string& outError);
    PlaylistWriteResult createOrReplacePlaylist(
        const SubsonicRequestContext& context,
        const std::optional<std::string>& playlistId, const std::string& name,
        const std::vector<std::string>& orderedSongIds, bool formPostAdvertised,
        std::string& outError);
    PlaylistWriteResult updatePlaylist(
        const SubsonicRequestContext& context, const std::string& playlistId,
        const std::vector<std::string>& songIdsToAdd,
        const std::vector<std::size_t>& songIndicesToRemove,
        bool formPostAdvertised,
        std::string& outError);
    bool renamePlaylist(const SubsonicRequestContext& context,
                        const std::string& playlistId,
                        const std::string& name,
                        std::string& outError);
    bool deletePlaylist(const SubsonicRequestContext& context,
                        const std::string& playlistId,
                        std::string& outError);
    bool scrobble(const SubsonicRequestContext& context, const std::string& songId,
                  bool submission, std::string& outError);

    std::string streamURL(const std::string& songId);
    std::string downloadURL(const SubsonicRequestContext& context,
                            const std::string& songId) const;
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
    bool httpDownloadToFile(const SubsonicRequestContext& context,
                            const std::string& url,
                            const std::wstring& destPath,
                            std::string& outError) const;

private:
    SubsonicClientWin() = default;

    struct HttpRequestProfile {
        int resolveTimeoutMs = 15000;
        int connectTimeoutMs = 15000;
        int sendTimeoutMs = 15000;
        int receiveTimeoutMs = 30000;
        int overallTimeoutMs = 0;
        std::size_t maxResponseBytes = 4 * 1024 * 1024;
        bool disableRedirects = false;
    };

    OrderedParameters authParameters(const SubsonicRequestContext& context) const;
    std::string buildURL(const SubsonicRequestContext& context,
                         const std::string& endpoint,
                         const OrderedParameters& parameters = {}) const;
    std::string request(const SubsonicRequestContext& context,
                        const std::string& endpoint,
                        const OrderedParameters& parameters,
                        RequestMethod method, std::string& outError) const;
    std::string request(const SubsonicRequestContext& context,
                        const std::string& endpoint,
                        const OrderedParameters& parameters,
                        RequestMethod method, std::string& outError,
                        const HttpRequestProfile& profile) const;
    // Synchronous HTTP GET; returns body or "" on error (sets outError).
    std::string httpRequest(const SubsonicRequestContext& context,
                            const std::string& url, RequestMethod method,
                            const std::string& body,
                            std::string& outError,
                            const HttpRequestProfile& profile) const;
};

} // namespace navidrome
