#include "stdafx.h"
#include "SubsonicClientWin.h"
#include "Localization.h"
#include "SongJsonParser.h"
#include "SubsonicRequestLogic.h"
#include <algorithm>
#include <chrono>
#include <SDK/cfg_var.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

// Config vars defined in NavidromePluginWin.cpp
namespace navidrome {
    extern cfg_string cfg_server_url;
    extern cfg_string cfg_username;
    extern cfg_string cfg_password;
    extern cfg_string cfg_salt;
    extern cfg_string cfg_custom_headers;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Force modern TLS on a WinHTTP session. WinHTTP's legacy default negotiates
// SSL3 / TLS1.0, which Cloudflare and most modern endpoints reject (handshake
// fails with ERROR_WINHTTP_SECURE_CHANNEL_ERROR, 12157). We offer only TLS
// 1.2 + 1.3 — secure and correct for real Windows schannel.
//
// NOTE (Wine only): a server configured with Minimum TLS Version = 1.3 still
// fails under Wine, because Wine's gnutls-backed schannel mis-negotiates when
// 1.2 and 1.3 are both offered (server replies fatal alert 70, protocol
// version). Real Windows schannel handles this fine; the workaround for Wine
// testing is to set the Cloudflare zone's Minimum TLS Version to 1.2.
static void applySecureProtocols(HINTERNET hSession) {
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(hSession, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &protocols, sizeof(protocols));
}

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty() && w.back() == 0) w.pop_back();
    return w;
}

static std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    if (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

static std::string md5hex(const std::string& input) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return "";
    CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash);
    CryptHashData(hHash, reinterpret_cast<const BYTE*>(input.c_str()),
                  static_cast<DWORD>(input.size()), 0);
    DWORD len = 16;
    BYTE  digest[16] = {};
    CryptGetHashParam(hHash, HP_HASHVAL, digest, &len, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    char hex[33];
    for (int i = 0; i < 16; i++) sprintf_s(hex + i * 2, 3, "%02x", digest[i]);
    return std::string(hex, 32);
}

static std::string checkedResponse(const std::string& body, std::string& outError) {
    const auto response = navidrome::parseSubsonicResponseJson(body);
    if (!response.valid) {
        outError = navidrome::l10n::invalidResponse;
        return {};
    }
    if (!response.ok) {
        outError = response.error && !response.error->message.empty()
            ? response.error->message : navidrome::l10n::unknownSubsonicError;
        return {};
    }
    return response.payloadJson;
}

// ---------------------------------------------------------------------------
// SubsonicClientWin
// ---------------------------------------------------------------------------

navidrome::SubsonicClientWin& navidrome::SubsonicClientWin::get() {
    static SubsonicClientWin inst;
    return inst;
}

bool navidrome::SubsonicClientWin::isConfigured() const {
    return cfg_server_url.get().length() > 0 &&
           cfg_username.get().length()   > 0 &&
           cfg_password.get().length()   > 0;
}

static bool indicatesUnsupportedSearch(const std::string& error) {
    std::string value = error;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value.find("404") != std::string::npos ||
           value.find("not found") != std::string::npos ||
           value.find("unknown endpoint") != std::string::npos ||
           value.find("unknown method") != std::string::npos ||
           value.find("not implemented") != std::string::npos ||
           value.find("unsupported") != std::string::npos;
}

navidrome::SubsonicRequestContext navidrome::SubsonicClientWin::snapshot() const {
    SubsonicRequestContext context;
    context.serverUrl = cfg_server_url.get().c_str();
    context.username = cfg_username.get().c_str();
    context.password = cfg_password.get().c_str();
    context.salt = cfg_salt.get().length() > 0
        ? cfg_salt.get().c_str() : "fb2k_navidrome";
    context.customHeaders = cfg_custom_headers.get().c_str();
    return context;
}

navidrome::OrderedParameters navidrome::SubsonicClientWin::authParameters(
        const SubsonicRequestContext& context) const {
    return {
        {"u", context.username},
        {"t", md5hex(context.password + context.salt)},
        {"s", context.salt},
        {"v", "1.16.1"},
        {"c", "foo_navidrome"},
        {"f", "json"},
    };
}

std::string navidrome::SubsonicClientWin::buildURL(
        const SubsonicRequestContext& context, const std::string& endpoint,
        const OrderedParameters& parameters) const {
    std::string base = context.serverUrl;
    while (!base.empty() && base.back() == '/') base.pop_back();
    auto allParameters = authParameters(context);
    allParameters.insert(allParameters.end(), parameters.begin(), parameters.end());
    return base + "/rest/" + endpoint + "?" + encodeFormParameters(allParameters);
}

std::string navidrome::SubsonicClientWin::request(
        const SubsonicRequestContext& context, const std::string& endpoint,
        const OrderedParameters& parameters, RequestMethod method,
        std::string& outError) const {
    return request(context, endpoint, parameters, method, outError,
                   HttpRequestProfile{});
}

std::string navidrome::SubsonicClientWin::request(
        const SubsonicRequestContext& context, const std::string& endpoint,
        const OrderedParameters& parameters, RequestMethod method,
        std::string& outError, const HttpRequestProfile& profile) const {
    if (method == RequestMethod::Get) {
        return httpRequest(context, buildURL(context, endpoint, parameters), method,
                           {}, outError, profile);
    }

    std::string base = context.serverUrl;
    while (!base.empty() && base.back() == '/') base.pop_back();
    auto allParameters = authParameters(context);
    allParameters.insert(allParameters.end(), parameters.begin(), parameters.end());
    return httpRequest(context, base + "/rest/" + endpoint, method,
                        encodeFormParameters(allParameters), outError, profile);
}

std::vector<std::string> navidrome::SubsonicClientWin::customHeaderLines() {
    return navidrome::parseHeaderLines(cfg_custom_headers.get().c_str());
}

std::wstring navidrome::SubsonicClientWin::customHeadersWide() {
    std::string joined;
    for (const auto& line : customHeaderLines()) {
        if (!joined.empty()) joined += "\r\n";
        joined += line;
    }
    return joined.empty() ? std::wstring() : toWide(joined);
}

std::string navidrome::SubsonicClientWin::generateToken(const std::string& password,
                                                         const std::string& salt) {
    return md5hex(password + salt);
}

std::string navidrome::SubsonicClientWin::httpRequest(
        const SubsonicRequestContext& context, const std::string& urlStr,
        RequestMethod method, const std::string& body,
        std::string& outError, const HttpRequestProfile& profile) const {
    const auto requestStarted = std::chrono::steady_clock::now();
    const auto overallTimedOut = [&]() {
        if (profile.overallTimeoutMs <= 0) return false;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - requestStarted);
        return elapsed.count() >= profile.overallTimeoutMs;
    };
    std::wstring wurl = toWide(urlStr);

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[8192] = {}, extraInfo[8192] = {};
    uc.lpszHostName    = host; uc.dwHostNameLength    = 256;
    uc.lpszUrlPath     = path; uc.dwUrlPathLength     = 8192;
    uc.lpszExtraInfo   = extraInfo; uc.dwExtraInfoLength = 8192;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        outError = navidrome::l10n::invalidUrl; return "";
    }

    HINTERNET hSess = WinHttpOpen(L"foo_navidrome/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) { outError = navidrome::l10n::winHttpOpenFailed; return ""; }
    if (!WinHttpSetTimeouts(hSess, profile.resolveTimeoutMs,
            profile.connectTimeoutMs, profile.sendTimeoutMs,
            profile.receiveTimeoutMs)) {
        outError = navidrome::l10n::requestError(GetLastError());
        WinHttpCloseHandle(hSess);
        return "";
    }
    applySecureProtocols(hSess);

    HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); outError = navidrome::l10n::connectFailed; return ""; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    const wchar_t* verb = method == RequestMethod::FormPost ? L"POST" : L"GET";
    std::wstring objectName(path, uc.dwUrlPathLength);
    objectName.append(extraInfo, uc.dwExtraInfoLength);
    HINTERNET hReq = WinHttpOpenRequest(hConn, verb, objectName.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    std::string result;
    if (!hReq) {
        outError = navidrome::l10n::requestError(GetLastError());
    } else {
        bool requestReady = true;
        if (profile.disableRedirects) {
            DWORD disabledFeatures = WINHTTP_DISABLE_REDIRECTS;
            if (!WinHttpSetOption(hReq, WINHTTP_OPTION_DISABLE_FEATURE,
                    &disabledFeatures, sizeof(disabledFeatures))) {
                outError = navidrome::l10n::requestError(GetLastError());
                requestReady = false;
            }
        }
        std::string joined;
        for (const auto& line : navidrome::parseHeaderLines(context.customHeaders)) {
            if (!joined.empty()) joined += "\r\n";
            joined += line;
        }
        std::wstring hdrs = joined.empty() ? std::wstring() : toWide(joined);
        if (!hdrs.empty())
            WinHttpAddRequestHeaders(hReq, hdrs.c_str(), (DWORD)-1,
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        if (method == RequestMethod::FormPost) {
            WinHttpAddRequestHeaders(
                hReq, L"Content-Type: application/x-www-form-urlencoded",
                (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }
        LPVOID requestBody = body.empty()
            ? WINHTTP_NO_REQUEST_DATA
            : const_cast<char*>(body.data());
        const DWORD bodyBytes = static_cast<DWORD>(body.size());
        if (requestReady &&
            WinHttpSendRequest(hReq, nullptr, 0, requestBody, bodyBytes, bodyBytes, 0) &&
            WinHttpReceiveResponse(hReq, nullptr)) {
            DWORD status = 0, sz = sizeof(status);
            WinHttpQueryHeaders(hReq,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                nullptr, &status, &sz, nullptr);
            bool responseTimedOut = overallTimedOut();
            DWORD avail = 0;
            while (!responseTimedOut &&
                   WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                if (result.size() > profile.maxResponseBytes ||
                    avail > profile.maxResponseBytes - result.size()) {
                    result.clear();
                    outError = navidrome::l10n::invalidResponse;
                    break;
                }
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(hReq, &chunk[0], avail, &read)) {
                    result.clear();
                    outError = navidrome::l10n::requestError(GetLastError());
                    break;
                }
                result.append(chunk, 0, read);
                responseTimedOut = overallTimedOut();
            }
            if (responseTimedOut) {
                result.clear();
                outError = navidrome::l10n::requestError(ERROR_WINHTTP_TIMEOUT);
            } else if (status != 200) {
                const auto response = parseSubsonicResponseJson(result);
                if (response.valid && response.error && !response.error->message.empty())
                    outError = response.error->message;
                else
                    outError = navidrome::l10n::httpError(status);
                result.clear();
            }
        } else if (requestReady) {
            outError = navidrome::l10n::requestError(GetLastError());
        }
        WinHttpCloseHandle(hReq);
    }
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return result;
}

bool navidrome::SubsonicClientWin::ping(std::string& outError) {
    return ping(snapshot(), outError);
}

bool navidrome::SubsonicClientWin::ping(
        const SubsonicRequestContext& context, std::string& outError) {
    std::string body = request(context, "ping.view", {}, RequestMethod::Get, outError);
    if (body.empty()) return false;
    auto root = checkedResponse(body, outError);
    return !root.empty();
}

navidrome::ServerInfo navidrome::SubsonicClientWin::getServerInfo(
        const SubsonicRequestContext& context, std::string& outError) {
    std::string body = request(context, "ping.view", {}, RequestMethod::Get, outError);
    if (body.empty()) return {};
    const auto response = parseSubsonicResponseJson(body);
    if (!response.valid || !response.ok) {
        checkedResponse(body, outError);
        return {};
    }
    ServerInfo result;
    result.type = response.type;
    result.version = response.serverVersion;
    result.openSubsonic = response.openSubsonic;
    return result;
}

std::vector<navidrome::MusicFolder>
navidrome::SubsonicClientWin::getMusicFolders(
        const SubsonicRequestContext& context, std::string& outError) {
    std::string body = request(context, "getMusicFolders.view", {},
                               RequestMethod::Get, outError);
    if (body.empty()) return {};
    auto root = checkedResponse(body, outError);
    if (root.empty()) return {};
    auto result = parseMusicFolderArrayJson(root);
    result.erase(std::remove_if(result.begin(), result.end(), [](const MusicFolder& folder) {
        return folder.id.empty();
    }), result.end());
    return result;
}

navidrome::ScanStatus navidrome::SubsonicClientWin::getScanStatus(
        const SubsonicRequestContext& context, std::string& outError) {
    std::string body = request(context, "getScanStatus.view", {},
                               RequestMethod::Get, outError);
    if (body.empty()) return {};
    auto root = checkedResponse(body, outError);
    if (root.empty()) return {};
    return parseScanStatusJson(root);
}

std::vector<navidrome::Song> navidrome::SubsonicClientWin::getSongsPage(
        const SubsonicRequestContext& context, std::size_t offset,
        std::size_t count, std::string& outError, bool* outUnsupported) {
    if (outUnsupported) *outUnsupported = false;
    const OrderedParameters params = {
        {"query", ""}, {"artistCount", "0"}, {"albumCount", "0"},
        {"songCount", std::to_string(count)},
        {"songOffset", std::to_string(offset)},
    };
    std::string body = request(context, "search3.view", params,
                               RequestMethod::Get, outError);
    if (body.empty()) {
        if (outUnsupported) *outUnsupported = indicatesUnsupportedSearch(outError);
        return {};
    }
    auto root = checkedResponse(body, outError);
    if (root.empty()) {
        if (outUnsupported) *outUnsupported = indicatesUnsupportedSearch(outError);
        return {};
    }

    return parseSongArrayJson(root, "song", {}, navidrome::l10n::unknownTitle);
}

std::vector<navidrome::Artist> navidrome::SubsonicClientWin::getArtists(std::string& outError) {
    return getArtists(snapshot(), outError);
}

std::vector<navidrome::Artist> navidrome::SubsonicClientWin::getArtists(
        const SubsonicRequestContext& context, std::string& outError) {
    std::string body = request(context, "getArtists.view", {},
                               RequestMethod::Get, outError);
    if (body.empty()) return {};
    auto root = checkedResponse(body, outError);
    if (root.empty()) return {};
    return parseArtistArrayJson(root, "artist", navidrome::l10n::unknownArtist);
}

std::vector<navidrome::Album>
navidrome::SubsonicClientWin::getAlbumsForArtist(const std::string& artistId,
                                                  std::string& outError) {
    return getAlbumsForArtist(snapshot(), artistId, outError);
}

std::vector<navidrome::Album>
navidrome::SubsonicClientWin::getAlbumsForArtist(
        const SubsonicRequestContext& context, const std::string& artistId,
        std::string& outError) {
    std::string body = request(context, "getArtist.view", {{"id", artistId}},
                               RequestMethod::Get, outError);
    if (body.empty()) return {};
    auto root = checkedResponse(body, outError);
    if (root.empty()) return {};
    return parseAlbumArrayJson(root, "album", artistId,
                               navidrome::l10n::unknownAlbum);
}

std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getSongsForAlbum(const std::string& albumId,
                                                std::string& outError) {
    return getSongsForAlbum(snapshot(), albumId, outError);
}

std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getSongsForAlbum(
        const SubsonicRequestContext& context, const std::string& albumId,
        std::string& outError) {
    std::string body = request(context, "getAlbum.view", {{"id", albumId}},
                               RequestMethod::Get, outError);
    if (body.empty()) return {};
    auto root = checkedResponse(body, outError);
    if (root.empty()) return {};

    return parseSongArrayJson(root, "song", albumId, navidrome::l10n::unknownTitle);
}

navidrome::SearchResults
navidrome::SubsonicClientWin::search(const std::string& query, std::string& outError) {
    return search(snapshot(), query, outError);
}

navidrome::SearchResults navidrome::SubsonicClientWin::search(
        const SubsonicRequestContext& context, const std::string& query,
        std::string& outError) {
    const OrderedParameters params = {
        {"query", query}, {"artistCount", "20"},
        {"albumCount", "20"}, {"songCount", "50"},
    };
    std::string body = request(context, "search3.view", params,
                               RequestMethod::Get, outError);
    if (body.empty()) return {};
    auto root = checkedResponse(body, outError);
    if (root.empty()) return {};

    SearchResults r;
    r.artists = parseArtistArrayJson(root);
    r.albums = parseAlbumArrayJson(root);
    r.songs = parseSongArrayJson(root, "song", {}, navidrome::l10n::unknownTitle);
    return r;
}

std::vector<navidrome::Album> navidrome::SubsonicClientWin::getAlbumList(
        const SubsonicRequestContext& context, AlbumListKind kind,
        std::size_t count, std::string& outError) {
    const OrderedParameters parameters = {
        {"type", albumListKindParameter(kind)},
        {"size", std::to_string(count)},
    };
    const auto body = request(context, "getAlbumList2.view", parameters,
                              RequestMethod::Get, outError);
    if (body.empty()) return {};
    const auto root = checkedResponse(body, outError);
    return root.empty() ? std::vector<Album>() : parseAlbumArrayJson(root);
}

navidrome::StarredResults navidrome::SubsonicClientWin::getStarred(
        const SubsonicRequestContext& context, std::string& outError) {
    const auto body = request(context, "getStarred2.view", {},
                              RequestMethod::Get, outError);
    if (body.empty()) return {};
    const auto root = checkedResponse(body, outError);
    if (root.empty()) return {};

    StarredResults results;
    results.artists = parseArtistArrayJson(root);
    results.albums = parseAlbumArrayJson(root);
    results.songs = parseSongArrayJson(root, "song", {}, navidrome::l10n::unknownTitle);
    for (auto& artist : results.artists) {
        if (!artist.starred) artist.starred = "1";
    }
    for (auto& album : results.albums) {
        if (!album.starred) album.starred = "1";
    }
    for (auto& song : results.songs) {
        if (!song.starred) song.starred = "1";
    }
    return results;
}

bool navidrome::SubsonicClientWin::setFavorite(
        const SubsonicRequestContext& context, FavoriteKind kind,
        const std::string& id, bool favorite, std::string& outError) {
    const char* key = "id";
    if (kind == FavoriteKind::Album) key = "albumId";
    else if (kind == FavoriteKind::Artist) key = "artistId";
    const auto body = request(context, favorite ? "star.view" : "unstar.view",
                              {{key, id}}, RequestMethod::Get, outError);
    return !body.empty() && !checkedResponse(body, outError).empty();
}

bool navidrome::SubsonicClientWin::setRating(
        const SubsonicRequestContext& context, const std::string& songId,
        int rating, std::string& outError) {
    if (rating < 0 || rating > 5) {
        outError = "rating must be between 0 and 5";
        return false;
    }
    const auto body = request(context, "setRating.view",
                              {{"id", songId}, {"rating", std::to_string(rating)}},
                              RequestMethod::Get, outError);
    return !body.empty() && !checkedResponse(body, outError).empty();
}

std::vector<navidrome::ServerPlaylist> navidrome::SubsonicClientWin::getPlaylists(
        const SubsonicRequestContext& context, std::string& outError) {
    const auto body = request(context, "getPlaylists.view", {},
                              RequestMethod::Get, outError);
    if (body.empty()) return {};
    const auto root = checkedResponse(body, outError);
    return root.empty() ? std::vector<ServerPlaylist>() : parsePlaylistArrayJson(root);
}

navidrome::ServerPlaylistDetails navidrome::SubsonicClientWin::getPlaylist(
        const SubsonicRequestContext& context, const std::string& playlistId,
        std::string& outError) {
    const auto body = request(context, "getPlaylist.view", {{"id", playlistId}},
                              RequestMethod::Get, outError);
    if (body.empty()) return {};
    const auto root = checkedResponse(body, outError);
    if (root.empty()) return {};

    ServerPlaylistDetails details;
    const auto playlists = parsePlaylistArrayJson(root);
    if (!playlists.empty()) details.playlist = playlists.front();
    details.songs = parseSongArrayJson(root, "entry", {}, navidrome::l10n::unknownTitle);
    return details;
}

navidrome::OpenSubsonicCapabilities
navidrome::SubsonicClientWin::getOpenSubsonicCapabilities(
        const SubsonicRequestContext& context, std::string& outError) {
    const auto body = request(context, "getOpenSubsonicExtensions.view", {},
                              RequestMethod::Get, outError);
    if (body.empty()) return {};
    const auto root = checkedResponse(body, outError);
    if (root.empty()) return {};

    OpenSubsonicCapabilities capabilities;
    capabilities.extensions = parseOpenSubsonicExtensionsJson(root);
    capabilities.formPost = std::any_of(
        capabilities.extensions.begin(), capabilities.extensions.end(),
        [](const OpenSubsonicExtension& extension) {
            return extension.name == "formPost";
        });
    return capabilities;
}

navidrome::PlaylistWriteResult navidrome::SubsonicClientWin::createOrReplacePlaylist(
        const SubsonicRequestContext& context,
        const std::optional<std::string>& playlistId, const std::string& name,
        const std::vector<std::string>& orderedSongIds, bool formPostAdvertised,
        std::string& outError) {
    PlaylistWriteResult result;
    result.requestedCount = orderedSongIds.size();
    result.addedCount = orderedSongIds.size();
    if ((!playlistId || playlistId->empty()) && name.empty()) {
        result.error = outError = "playlist id or name is required";
        return result;
    }

    constexpr std::size_t kConservativeGetLimit = 3072;
    const auto parameterBudget = [&](const char* endpoint) {
        const auto baseUrl = buildURL(context, endpoint);
        return baseUrl.size() + 1 < kConservativeGetLimit
            ? kConservativeGetLimit - baseUrl.size() - 1 : std::size_t(0);
    };
    const auto budget = (std::min)(parameterBudget("createPlaylist.view"),
                                   parameterBudget("updatePlaylist.view"));
    auto plan = planPlaylistWrite(playlistId, name, orderedSongIds,
                                  formPostAdvertised, budget);

    // Legacy GET servers need an identity before a long create can be batched.
    // Create the empty list first, then re-plan the deterministic replacement.
    if (plan.mode == PlaylistWriteMode::Invalid &&
        (!playlistId || playlistId->empty()) && !formPostAdvertised &&
        !orderedSongIds.empty()) {
        if (buildURL(context, "createPlaylist.view", {{"name", name}}).size() >
            kConservativeGetLimit) {
            result.error = outError = plan.error;
            return result;
        }
        const auto createBody = request(context, "createPlaylist.view", {{"name", name}},
                                        RequestMethod::Get, outError);
        const auto createRoot = createBody.empty()
            ? std::string() : checkedResponse(createBody, outError);
        if (createRoot.empty()) {
            result.error = outError;
            return result;
        }
        const auto created = parsePlaylistArrayJson(createRoot);
        if (created.empty() || created.front().id.empty()) {
            result.state = PlaylistWriteState::Unknown;
            result.error = outError = "server did not return the created playlist id";
            return result;
        }
        auto continued = createOrReplacePlaylist(
            context, std::optional<std::string>(created.front().id), name,
            orderedSongIds, false, outError);
        if (continued.state == PlaylistWriteState::Failed)
            continued.state = PlaylistWriteState::Partial;
        return continued;
    }
    if (plan.mode == PlaylistWriteMode::Invalid) {
        result.error = outError = plan.error;
        return result;
    }

    const RequestMethod initialMethod = plan.mode == PlaylistWriteMode::SingleFormPost
        ? RequestMethod::FormPost : RequestMethod::Get;
    const auto initialBody = request(context, "createPlaylist.view",
                                     plan.initialParameters, initialMethod, outError);
    const auto initialRoot = initialBody.empty()
        ? std::string() : checkedResponse(initialBody, outError);
    if (initialRoot.empty()) {
        result.error = outError;
        return result;
    }

    const auto playlists = parsePlaylistArrayJson(initialRoot);
    if (!playlists.empty()) result.playlist = playlists.front();
    if (result.playlist.id.empty() && playlistId) result.playlist.id = *playlistId;
    if (result.playlist.name.empty()) result.playlist.name = name;

    if (plan.mode == PlaylistWriteMode::IncrementalGet) {
        for (const auto& batch : plan.appendBatches) {
            const auto updateBody = request(context, "updatePlaylist.view", batch,
                                            RequestMethod::Get, outError);
            if (updateBody.empty() || checkedResponse(updateBody, outError).empty()) {
                result.state = PlaylistWriteState::Partial;
                result.error = outError;
                return result;
            }
        }
    }

    result.actualCount = result.playlist.songCount > 0
        ? static_cast<std::size_t>(result.playlist.songCount) : 0;
    result.state = PlaylistWriteState::Accepted;
    return result;
}

navidrome::PlaylistWriteResult navidrome::SubsonicClientWin::updatePlaylist(
        const SubsonicRequestContext& context, const std::string& playlistId,
        const std::vector<std::string>& songIdsToAdd,
        const std::vector<std::size_t>& songIndicesToRemove,
        bool formPostAdvertised,
        std::string& outError) {
    PlaylistWriteResult result;
    result.playlist.id = playlistId;
    result.addedCount = songIdsToAdd.size();
    result.removedCount = songIndicesToRemove.size();
    result.requestedCount = result.addedCount + result.removedCount;
    OrderedParameters parameters = {{"playlistId", playlistId}};
    for (const auto& songId : songIdsToAdd) parameters.emplace_back("songIdToAdd", songId);
    for (const auto index : songIndicesToRemove)
        parameters.emplace_back("songIndexToRemove", std::to_string(index));
    const RequestMethod method = formPostAdvertised
        ? RequestMethod::FormPost : RequestMethod::Get;
    if (method == RequestMethod::Get &&
        buildURL(context, "updatePlaylist.view", parameters).size() > 3072) {
        result.error = outError = "playlist update exceeds the conservative GET limit";
        return result;
    }
    const auto body = request(context, "updatePlaylist.view", parameters, method, outError);
    if (body.empty()) {
        result.error = outError;
        return result;
    }
    if (checkedResponse(body, outError).empty()) {
        result.error = outError;
        return result;
    }
    result.state = PlaylistWriteState::Accepted;
    return result;
}

bool navidrome::SubsonicClientWin::scrobble(
        const SubsonicRequestContext& context, const std::string& songId,
        bool submission, std::string& outError) {
    HttpRequestProfile profile;
    profile.resolveTimeoutMs = 2000;
    profile.connectTimeoutMs = 3000;
    profile.sendTimeoutMs = 3000;
    profile.receiveTimeoutMs = 4000;
    profile.overallTimeoutMs = 12000;
    profile.maxResponseBytes = 64 * 1024;
    profile.disableRedirects = true;
    const auto body = request(context, "scrobble.view",
                              {{"id", songId},
                               {"submission", submission ? "true" : "false"}},
                              RequestMethod::Get, outError, profile);
    return !body.empty() && !checkedResponse(body, outError).empty();
}

std::string navidrome::SubsonicClientWin::streamURL(const std::string& songId) {
    auto context = snapshot();
    return buildURL(context, "stream.view", {{"id", songId}});
}

std::string navidrome::SubsonicClientWin::coverArtURL(const std::string& id, int size) {
    auto context = snapshot();
    return coverArtURL(context, id, size);
}

std::string navidrome::SubsonicClientWin::coverArtURL(
        const SubsonicRequestContext& context, const std::string& id, int size) const {
    return buildCoverArtUrl(context.serverUrl, context.username, context.password,
        context.salt, id, size);
}

// ---------------------------------------------------------------------------
// Binary fetch for cover art (PRD C2-C4, design §2.3)
// ---------------------------------------------------------------------------
navidrome::SubsonicClientWin::BinaryFetchResult
navidrome::SubsonicClientWin::httpGetBinary(
        const SubsonicRequestContext& context,
        const std::string& urlStr,
        std::size_t maxBytes,
        abort_callback& abort) const {

    BinaryFetchResult result;
    result.cls = FetchClass::Transport;
    result.httpStatus = 0;

    std::wstring wurl = toWide(urlStr);

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        return result; // Transport
    }

    HINTERNET hSess = WinHttpOpen(L"foo_navidrome/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return result;

    WinHttpSetTimeouts(hSess, 0, 15000, 15000, 30000);
    applySecureProtocols(hSess);

    // Check abort before connect
    if (abort.is_aborting()) {
        WinHttpCloseHandle(hSess);
        result.cls = FetchClass::Aborted;
        return result;
    }

    HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
    if (!hConn) {
        WinHttpCloseHandle(hSess);
        return result;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    if (!hReq) {
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        return result;
    }

    // Apply custom headers
    std::string joined;
    for (const auto& line : navidrome::parseHeaderLines(context.customHeaders)) {
        if (!joined.empty()) joined += "\r\n";
        joined += line;
    }
    std::wstring hdrs = joined.empty() ? std::wstring() : toWide(joined);
    if (!hdrs.empty()) {
        WinHttpAddRequestHeaders(hReq, hdrs.c_str(), (DWORD)-1,
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }

    // Check abort before send
    if (abort.is_aborting()) {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        result.cls = FetchClass::Aborted;
        return result;
    }

    if (!WinHttpSendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, nullptr)) {
        WinHttpCloseHandle(hReq);
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSess);
        return result; // Transport
    }

    // Query status
    DWORD status = 0, sz = sizeof(status);
    if (!WinHttpQueryHeaders(hReq,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &status, &sz, nullptr)) {
        status = 0;
    }
    result.httpStatus = status;
    result.cls = classifyHttpStatus(status);

    // Query Content-Type
    wchar_t ctBuf[256] = {};
    DWORD ctLen = sizeof(ctBuf);
    if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_TYPE,
        nullptr, ctBuf, &ctLen, nullptr)) {
        result.contentType = toUtf8(ctBuf);
    }

    // Read body (only for 200)
    if (status == 200) {
        std::vector<uint8_t> body;
        bool readSucceeded = true;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(hReq, &avail)) {
                readSucceeded = false;
                break;
            }
            if (avail == 0) break;

            // Check abort between chunks
            if (abort.is_aborting()) {
                WinHttpCloseHandle(hReq);
                WinHttpCloseHandle(hConn);
                WinHttpCloseHandle(hSess);
                result.cls = FetchClass::Aborted;
                return result;
            }

            // Check size limit
            if (body.size() > maxBytes || avail > maxBytes - body.size()) {
                WinHttpCloseHandle(hReq);
                WinHttpCloseHandle(hConn);
                WinHttpCloseHandle(hSess);
                result.cls = FetchClass::InvalidContent;
                return result;
            }

            std::vector<uint8_t> chunk(avail);
            DWORD read = 0;
            if (!WinHttpReadData(hReq, chunk.data(), avail, &read)) {
                readSucceeded = false;
                break;
            }
            body.insert(body.end(), chunk.begin(), chunk.begin() + read);
        }

        if (abort.is_aborting()) {
            result.cls = FetchClass::Aborted;
        } else if (!readSucceeded) {
            result.cls = FetchClass::Transport;
        } else {
            result.cls = classifyBody(result.contentType, body, maxBytes);
            if (result.cls == FetchClass::Ok) result.body = std::move(body);
        }
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return result;
}
