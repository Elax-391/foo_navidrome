#include "stdafx.h"
#include "SubsonicClientWin.h"
#include <SDK/cfg_var.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

// Config vars defined in NavidromePluginWin.cpp
namespace navidrome {
    extern cfg_string cfg_server_url;
    extern cfg_string cfg_username;
    extern cfg_string cfg_password;
    extern cfg_string cfg_salt;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

static std::string urlEncode(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += static_cast<char>(c);
        else { char buf[4]; sprintf_s(buf, "%%%02X", c); out += buf; }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Minimal JSON extraction (Subsonic-specific, not a general parser)
// ---------------------------------------------------------------------------

// Extract first string value for "key":"value"
static std::string jstr(const std::string& s, const std::string& key,
                        const std::string& def = "") {
    auto k = "\"" + key + "\":\"";
    auto p = s.find(k);
    if (p == std::string::npos) return def;
    p += k.size();
    std::string val;
    for (; p < s.size() && s[p] != '"'; ++p) {
        if (s[p] == '\\' && p + 1 < s.size()) { ++p; val += s[p]; }
        else val += s[p];
    }
    return val;
}

// Extract first integer for "key":123
static int jint(const std::string& s, const std::string& key, int def = 0) {
    auto k = "\"" + key + "\":";
    auto p = s.find(k);
    if (p == std::string::npos) return def;
    p += k.size();
    while (p < s.size() && s[p] == ' ') ++p;
    if (p >= s.size() || (!isdigit(static_cast<unsigned char>(s[p])) && s[p] != '-'))
        return def;
    return atoi(s.c_str() + p);
}

// Extract first double for "key":1.5
static double jdbl(const std::string& s, const std::string& key, double def = 0.0) {
    auto k = "\"" + key + "\":";
    auto p = s.find(k);
    if (p == std::string::npos) return def;
    p += k.size();
    while (p < s.size() && s[p] == ' ') ++p;
    if (p >= s.size()) return def;
    char* end = nullptr;
    double v = strtod(s.c_str() + p, &end);
    return (end == s.c_str() + p) ? def : v;
}

// Extract array of JSON objects for "key":[{...},{...}]
// Also handles single-object case "key":{...}
static std::vector<std::string> jarr(const std::string& s, const std::string& key) {
    std::vector<std::string> res;
    // Try array
    auto k = "\"" + key + "\":[";
    auto p = s.find(k);
    if (p != std::string::npos) {
        p += k.size();
        while (p < s.size()) {
            while (p < s.size() && s[p] != '{' && s[p] != ']') ++p;
            if (p >= s.size() || s[p] == ']') break;
            size_t st = p; int depth = 0;
            for (; p < s.size(); ++p) {
                if (s[p] == '"') {
                    ++p;
                    while (p < s.size() && !(s[p] == '"' && s[p-1] != '\\')) ++p;
                } else if (s[p] == '{') ++depth;
                else if (s[p] == '}') { if (--depth == 0) break; }
            }
            res.push_back(s.substr(st, p - st + 1));
            ++p;
        }
        return res;
    }
    // Try single object
    k = "\"" + key + "\":{";
    p = s.find(k);
    if (p != std::string::npos) {
        p += k.size() - 1;
        size_t st = p; int depth = 0;
        for (; p < s.size(); ++p) {
            if (s[p] == '"') { ++p; while (p < s.size() && !(s[p] == '"' && s[p-1] != '\\')) ++p; }
            else if (s[p] == '{') ++depth;
            else if (s[p] == '}') { if (--depth == 0) break; }
        }
        if (depth == 0) res.push_back(s.substr(st, p - st + 1));
    }
    return res;
}

// Check Subsonic status and return inner response object, or set error
static std::string checkResponse(const std::string& body, std::string& outError) {
    auto res = jstr(body, "status");
    if (res != "ok") {
        auto arr = jarr(body, "error");
        outError = arr.empty() ? "Unknown Subsonic error" : jstr(arr[0], "message", "Error");
        return "";
    }
    // Return everything inside "subsonic-response":{...}
    std::string k = "\"subsonic-response\":{";
    auto p = body.find(k);
    if (p == std::string::npos) { outError = "Invalid response"; return ""; }
    p += k.size() - 1;
    size_t st = p; int depth = 0;
    for (; p < body.size(); ++p) {
        if (body[p] == '"') { ++p; while (p < body.size() && !(body[p] == '"' && body[p-1] != '\\')) ++p; }
        else if (body[p] == '{') ++depth;
        else if (body[p] == '}') { if (--depth == 0) break; }
    }
    return body.substr(st, p - st + 1);
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

std::string navidrome::SubsonicClientWin::authParams() const {
    std::string user = cfg_username.get().c_str();
    std::string pass = cfg_password.get().c_str();
    std::string salt = cfg_salt.get().length() > 0 ? cfg_salt.get().c_str() : "fb2k_navidrome";
    std::string token = md5hex(pass + salt);
    return "u=" + urlEncode(user) + "&t=" + token + "&s=" + salt +
           "&v=1.16.1&c=foo_navidrome&f=json";
}

std::string navidrome::SubsonicClientWin::buildURL(const std::string& endpoint,
                                                    const std::string& extra) const {
    std::string base = cfg_server_url.get().c_str();
    while (!base.empty() && base.back() == '/') base.pop_back();
    std::string url = base + "/rest/" + endpoint + "?" + authParams();
    if (!extra.empty()) url += "&" + extra;
    return url;
}

std::string navidrome::SubsonicClientWin::httpGet(const std::string& urlStr,
                                                   std::string& outError) const {
    std::wstring wurl = toWide(urlStr);

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[4096] = {};
    uc.lpszHostName    = host; uc.dwHostNameLength    = 256;
    uc.lpszUrlPath     = path; uc.dwUrlPathLength     = 4096;

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        outError = "Invalid URL"; return "";
    }

    HINTERNET hSess = WinHttpOpen(L"foo_navidrome/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) { outError = "WinHttpOpen failed"; return ""; }
    WinHttpSetTimeouts(hSess, 0, 15000, 15000, 30000);

    HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); outError = "Connect failed"; return ""; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    std::string result;
    if (hReq) {
        if (WinHttpSendRequest(hReq, nullptr, 0, nullptr, 0, 0, 0) &&
            WinHttpReceiveResponse(hReq, nullptr)) {
            DWORD status = 0, sz = sizeof(status);
            WinHttpQueryHeaders(hReq,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                nullptr, &status, &sz, nullptr);
            if (status == 200) {
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                    std::string chunk(avail, '\0');
                    DWORD read = 0;
                    WinHttpReadData(hReq, &chunk[0], avail, &read);
                    result.append(chunk, 0, read);
                }
            } else {
                outError = "HTTP " + std::to_string(status);
            }
        } else {
            outError = "Request failed (err=" + std::to_string(GetLastError()) + ")";
        }
        WinHttpCloseHandle(hReq);
    }
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return result;
}

bool navidrome::SubsonicClientWin::ping(std::string& outError) {
    std::string body = httpGet(buildURL("ping.view"), outError);
    if (body.empty()) return false;
    auto root = checkResponse(body, outError);
    return !root.empty();
}

std::vector<navidrome::Artist> navidrome::SubsonicClientWin::getArtists(std::string& outError) {
    std::string body = httpGet(buildURL("getArtists.view"), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    std::vector<Artist> result;
    for (auto& idxObj : jarr(root, "index")) {
        for (auto& a : jarr(idxObj, "artist")) {
            Artist ar;
            ar.id         = jstr(a, "id");
            ar.name       = jstr(a, "name", "Unknown Artist");
            ar.coverArtId = jstr(a, "coverArt");
            ar.albumCount = jint(a, "albumCount");
            result.push_back(std::move(ar));
        }
    }
    return result;
}

std::vector<navidrome::Album>
navidrome::SubsonicClientWin::getAlbumsForArtist(const std::string& artistId,
                                                  std::string& outError) {
    std::string body = httpGet(buildURL("getArtist.view", "id=" + urlEncode(artistId)), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    std::vector<Album> result;
    for (auto& a : jarr(root, "album")) {
        Album al;
        al.id         = jstr(a, "id");
        al.name       = jstr(a, "name", "Unknown Album");
        al.artist     = jstr(a, "artist");
        al.artistId   = jstr(a, "artistId", artistId);
        al.coverArtId = jstr(a, "coverArt");
        al.year       = jint(a, "year");
        al.songCount  = jint(a, "songCount");
        result.push_back(std::move(al));
    }
    return result;
}

std::vector<navidrome::Song>
navidrome::SubsonicClientWin::getSongsForAlbum(const std::string& albumId,
                                                std::string& outError) {
    std::string body = httpGet(buildURL("getAlbum.view", "id=" + urlEncode(albumId)), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    std::vector<Song> result;
    for (auto& s : jarr(root, "song")) {
        Song so;
        so.id         = jstr(s, "id");
        so.title      = jstr(s, "title", "Unknown Title");
        so.artist     = jstr(s, "artist");
        so.artistId   = jstr(s, "artistId");
        so.album      = jstr(s, "album");
        so.albumId    = jstr(s, "albumId", albumId);
        so.coverArtId = jstr(s, "coverArt");
        so.suffix     = jstr(s, "suffix");
        so.track      = jint(s, "track");
        so.year       = jint(s, "year");
        so.duration   = jdbl(s, "duration");
        result.push_back(std::move(so));
    }
    return result;
}

navidrome::SearchResults
navidrome::SubsonicClientWin::search(const std::string& query, std::string& outError) {
    std::string params = "query=" + urlEncode(query) +
                         "&artistCount=20&albumCount=20&songCount=50";
    std::string body = httpGet(buildURL("search3.view", params), outError);
    if (body.empty()) return {};
    auto root = checkResponse(body, outError);
    if (root.empty()) return {};

    SearchResults r;
    for (auto& a : jarr(root, "artist")) {
        Artist ar; ar.id = jstr(a,"id"); ar.name = jstr(a,"name"); ar.coverArtId = jstr(a,"coverArt");
        r.artists.push_back(ar);
    }
    for (auto& a : jarr(root, "album")) {
        Album al; al.id = jstr(a,"id"); al.name = jstr(a,"name");
        al.artist = jstr(a,"artist"); al.artistId = jstr(a,"artistId"); al.coverArtId = jstr(a,"coverArt");
        r.albums.push_back(al);
    }
    for (auto& s : jarr(root, "song")) {
        Song so; so.id = jstr(s,"id"); so.title = jstr(s,"title");
        so.artist = jstr(s,"artist"); so.album = jstr(s,"album");
        so.albumId = jstr(s,"albumId"); so.coverArtId = jstr(s,"coverArt");
        so.track = jint(s,"track"); so.duration = jdbl(s,"duration");
        r.songs.push_back(so);
    }
    return r;
}

std::string navidrome::SubsonicClientWin::streamURL(const std::string& songId) {
    return buildURL("stream.view", "id=" + urlEncode(songId));
}

std::string navidrome::SubsonicClientWin::coverArtURL(const std::string& id, int size) {
    std::string extra = "id=" + urlEncode(id);
    if (size > 0) extra += "&size=" + std::to_string(size);
    return buildURL("getCoverArt.view", extra);
}
