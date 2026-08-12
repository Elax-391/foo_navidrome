#include "../MediaEnrichmentLogic.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* description) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
}

std::vector<std::uint8_t> bytes(const std::string& value) {
    return {value.begin(), value.end()};
}

void testIdentifiers() {
    using navidrome::resolveArtId;
    check(resolveArtId("navidrome://track/song?coverArt=cover%2Fone&id=ignored") ==
        "cover/one", "coverArt has priority and decodes once");
    check(resolveArtId("navidrome://track/song%252Fraw") == "song%2Fraw",
        "path id is decoded exactly once");
    check(resolveArtId("navidrome://track/%E4%B8%AD%E6%96%87%2Bplus+literal") ==
        u8"中文+plus+literal", "UTF-8, encoded plus and literal plus survive");
    check(resolveArtId("https://server/rest/stream.view?id=old%2Fid&u=user") ==
        "old/id", "legacy stream id is supported");
    check(resolveArtId("https://server/music.mp3").empty(), "unowned path has no id");
}

void testCoverUrl() {
    const auto url = navidrome::buildCoverArtUrl(" HTTPS://Example.COM/root/ ",
        "user name", "distinct-password-9", "salt-42", u8"封面/id+", 300);
    check(url.find("https://example.com/root/rest/getCoverArt.view?") == 0,
        "server identity is normalized");
    check(url.find("u=user%20name") != std::string::npos, "username is encoded");
    check(url.find("t=404424f3a47ba68fb27a01d8c4eea719") != std::string::npos,
        "MD5 token matches known vector");
    check(url.find("s=salt-42") != std::string::npos, "salt is present");
    check(url.find("id=%E5%B0%81%E9%9D%A2%2Fid%2B") != std::string::npos,
        "cover id is encoded exactly once");
    check(url.find("size=300") != std::string::npos, "requested size is present");
    check(url.find("distinct-password-9") == std::string::npos,
        "raw password is absent from URL");
}

void testClassification() {
    using navidrome::FetchClass;
    using navidrome::classifyBody;
    using navidrome::classifyHttpStatus;

    check(classifyHttpStatus(200) == FetchClass::Ok, "HTTP 200");
    check(classifyHttpStatus(401) == FetchClass::Auth, "HTTP 401");
    check(classifyHttpStatus(403) == FetchClass::Auth, "HTTP 403");
    check(classifyHttpStatus(404) == FetchClass::NotFound, "HTTP 404");
    check(classifyHttpStatus(503) == FetchClass::ServerError, "HTTP 5xx");

    check(classifyBody("image/jpeg", {0xff, 0xd8, 0xff, 0x00}) == FetchClass::Ok,
        "JPEG magic");
    check(classifyBody("application/octet-stream",
        {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a}) == FetchClass::Ok,
        "PNG magic");
    check(classifyBody("image/avif", bytes("unknown-format")) == FetchClass::Ok,
        "image MIME fallback");
    check(classifyBody("text/html", bytes("<html>error</html>")) ==
        FetchClass::InvalidContent, "non-image content");
    check(classifyBody("image/jpeg", bytes("12345"), 4) ==
        FetchClass::InvalidContent, "body above limit");
    check(classifyBody("application/json", bytes(
        R"({"subsonic-response":{"status":"failed","error":{"code":70}}})")) ==
        FetchClass::NotFound, "Subsonic JSON not-found");
    check(classifyBody("application/json", bytes(
        R"({"subsonic-response":{"status":"failed","error":{"code":40}}})")) ==
        FetchClass::Auth, "Subsonic JSON auth");
    check(classifyBody("text/xml", bytes(
        R"(<subsonic-response status="failed"><error code="44"/></subsonic-response>)")) ==
        FetchClass::Auth, "Subsonic XML auth");
    check(classifyBody("application/json", bytes(
        R"({"subsonic-response":{"status":"failed","error":{"code":10}}})")) ==
        FetchClass::ServerError, "other Subsonic error");
}

void testCache() {
    auto& cache = navidrome::CoverCache::instance();
    cache.clear();
    cache.put("HTTPS://EXAMPLE.COM/", "alice", "cover", {1, 2, 3});
    check(cache.get("https://example.com", "alice", "cover") ==
        std::vector<std::uint8_t>({1, 2, 3}), "cache normalizes server identity");
    check(cache.get("https://example.com", "bob", "cover").empty(),
        "cache separates users");
    cache.put("https://identity", "user\npart", "cover", {7});
    cache.put("https://identity", "user", "part\ncover", {8});
    check(cache.get("https://identity", "user\npart", "cover") ==
        std::vector<std::uint8_t>({7}), "cache key frames username field");
    check(cache.get("https://identity", "user", "part\ncover") ==
        std::vector<std::uint8_t>({8}), "cache key frames cover-id field");

    cache.clear();
    for (int index = 0; index < 32; ++index) {
        cache.put("https://server", "user", "cover-" + std::to_string(index),
            {static_cast<std::uint8_t>(index)});
    }
    check(!cache.get("https://server", "user", "cover-0").empty(),
        "cache hit refreshes LRU order");
    cache.put("https://server", "user", "cover-32", {32});
    check(cache.get("https://server", "user", "cover-1").empty(),
        "least recently used entry is evicted");
    check(!cache.get("https://server", "user", "cover-0").empty(),
        "recently touched entry survives eviction");
}

void testConfig() {
    const std::string password = "distinct-password-9";
    const auto config = navidrome::buildEsLyricConfigJs(
        " HTTPS://Example.COM/root/ ", "user\"name", password, "salt-42",
        {{"X-Access", "line1\r\nline2"}, {u8"中文", u8"值😀"}}, "1.3.0");
    check(config.find("export const config") != std::string::npos,
        "config module exports canonical object");
    check(config.find("https://example.com/root") != std::string::npos,
        "config normalizes server URL");
    check(config.find("404424f3a47ba68fb27a01d8c4eea719") != std::string::npos,
        "config derives known token");
    check(config.find(password) == std::string::npos,
        "config never contains raw password");
    check(config.find("user\\\"name") != std::string::npos,
        "config escapes quotes");
    check(config.find("line1\\r\\nline2") != std::string::npos,
        "config escapes line breaks");
    check(config.find("debug: false") != std::string::npos,
        "config defaults to quiet mode");
    check(config.find("componentVersion: \"1.3.0\"") != std::string::npos,
        "config exposes the caller's componentVersion (no hardcoded script version)");
    check(config == navidrome::buildEsLyricConfigJs(
        " HTTPS://Example.COM/root/ ", "user\"name", password, "salt-42",
        {{"X-Access", "line1\r\nline2"}, {u8"中文", u8"值😀"}}, "1.3.0"),
        "config generation is stable");
}

} // namespace

int main() {
    testIdentifiers();
    testCoverUrl();
    testClassification();
    testCache();
    testConfig();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All MediaEnrichment tests passed\n";
    return 0;
}
