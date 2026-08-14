#include "SongJsonParser.h"
#include "SongMetadata.h"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

void skipSpace(const std::string& text, size_t& offset) {
    while (offset < text.size() &&
           std::isspace(static_cast<unsigned char>(text[offset]))) ++offset;
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool readHex4(const std::string& text, size_t& offset, unsigned& value) {
    if (offset + 4 > text.size()) return false;
    value = 0;
    for (size_t i = 0; i < 4; ++i) {
        const int digit = hexValue(text[offset + i]);
        if (digit < 0) return false;
        value = (value << 4) | static_cast<unsigned>(digit);
    }
    offset += 4;
    return true;
}

void appendUtf8(std::string& output, unsigned codepoint) {
    if (codepoint <= 0x7F) output.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

bool readString(const std::string& text, size_t& offset, std::string& output) {
    if (offset >= text.size() || text[offset] != '"') return false;
    ++offset;
    output.clear();
    while (offset < text.size()) {
        const char value = text[offset++];
        if (value == '"') return true;
        if (value != '\\') {
            output.push_back(value);
            continue;
        }
        if (offset >= text.size()) return false;
        const char escape = text[offset++];
        switch (escape) {
        case '"': output.push_back('"'); break;
        case '\\': output.push_back('\\'); break;
        case '/': output.push_back('/'); break;
        case 'b': output.push_back('\b'); break;
        case 'f': output.push_back('\f'); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        case 'u': {
            unsigned codepoint = 0;
            if (!readHex4(text, offset, codepoint)) return false;
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF &&
                offset + 2 <= text.size() && text[offset] == '\\' &&
                text[offset + 1] == 'u') {
                size_t lowOffset = offset + 2;
                unsigned low = 0;
                if (readHex4(text, lowOffset, low) && low >= 0xDC00 && low <= 0xDFFF) {
                    codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                    offset = lowOffset;
                }
            }
            appendUtf8(output, codepoint);
            break;
        }
        default: return false;
        }
    }
    return false;
}

bool skipValue(const std::string& text, size_t& offset) {
    skipSpace(text, offset);
    if (offset >= text.size()) return false;
    if (text[offset] == '"') {
        std::string ignored;
        return readString(text, offset, ignored);
    }
    if (text[offset] == '{' || text[offset] == '[') {
        std::vector<char> closing;
        closing.push_back(text[offset++] == '{' ? '}' : ']');
        while (offset < text.size() && !closing.empty()) {
            if (text[offset] == '"') {
                std::string ignored;
                if (!readString(text, offset, ignored)) return false;
            } else if (text[offset] == '{') {
                closing.push_back('}');
                ++offset;
            } else if (text[offset] == '[') {
                closing.push_back(']');
                ++offset;
            } else if (text[offset] == closing.back()) {
                closing.pop_back();
                ++offset;
            } else {
                ++offset;
            }
        }
        return closing.empty();
    }
    while (offset < text.size() && text[offset] != ',' && text[offset] != '}' &&
           text[offset] != ']') ++offset;
    return true;
}

std::optional<std::string> memberValue(const std::string& object, const char* name) {
    size_t offset = 0;
    skipSpace(object, offset);
    if (offset >= object.size() || object[offset++] != '{') return std::nullopt;
    while (offset < object.size()) {
        skipSpace(object, offset);
        if (offset < object.size() && object[offset] == '}') return std::nullopt;
        std::string key;
        if (!readString(object, offset, key)) return std::nullopt;
        skipSpace(object, offset);
        if (offset >= object.size() || object[offset++] != ':') return std::nullopt;
        skipSpace(object, offset);
        const size_t start = offset;
        if (!skipValue(object, offset)) return std::nullopt;
        if (key == name) return object.substr(start, offset - start);
        skipSpace(object, offset);
        if (offset < object.size() && object[offset] == ',') ++offset;
    }
    return std::nullopt;
}

struct LocatedValue {
    std::string value;
    size_t end = 0;
};

std::optional<LocatedValue> locateNestedMemberValue(
        const std::string& json, const std::string& name, size_t startOffset = 0) {
    size_t offset = startOffset;
    while (offset < json.size()) {
        if (json[offset] != '"') {
            ++offset;
            continue;
        }

        std::string key;
        if (!readString(json, offset, key)) return std::nullopt;
        size_t valueOffset = offset;
        skipSpace(json, valueOffset);
        if (valueOffset >= json.size() || json[valueOffset] != ':') continue;
        ++valueOffset;
        skipSpace(json, valueOffset);
        if (key != name) {
            offset = valueOffset;
            continue;
        }

        const size_t valueStart = valueOffset;
        if (!skipValue(json, valueOffset)) return std::nullopt;
        return LocatedValue{json.substr(valueStart, valueOffset - valueStart), valueOffset};
    }
    return std::nullopt;
}

std::optional<std::string> nestedMemberValue(const std::string& json,
                                             const std::string& name) {
    const auto located = locateNestedMemberValue(json, name);
    return located ? std::optional<std::string>(located->value) : std::nullopt;
}

std::optional<std::string> stringMember(const std::string& object, const char* name) {
    const auto raw = memberValue(object, name);
    if (!raw) return std::nullopt;
    size_t offset = 0;
    skipSpace(*raw, offset);
    if (raw->compare(offset, 4, "null") == 0) return std::nullopt;
    std::string value;
    return readString(*raw, offset, value) && !value.empty()
        ? std::optional<std::string>(std::move(value)) : std::nullopt;
}

std::optional<double> doubleMember(const std::string& object, const char* name) {
    const auto raw = memberValue(object, name);
    if (!raw) return std::nullopt;
    char* end = nullptr;
    const double value = std::strtod(raw->c_str(), &end);
    while (end && *end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (!end || end == raw->c_str() || *end != '\0' || !std::isfinite(value))
        return std::nullopt;
    return value;
}

std::optional<int> intMember(const std::string& object, const char* name) {
    const auto value = doubleMember(object, name);
    if (!value || std::floor(*value) != *value ||
        *value < (std::numeric_limits<int>::min)() ||
        *value > (std::numeric_limits<int>::max)()) return std::nullopt;
    return static_cast<int>(*value);
}

std::optional<bool> boolMember(const std::string& object, const char* name) {
    const auto raw = memberValue(object, name);
    if (!raw) return std::nullopt;
    size_t offset = 0;
    skipSpace(*raw, offset);
    if (raw->compare(offset, 4, "true") == 0) return true;
    if (raw->compare(offset, 5, "false") == 0) return false;
    return std::nullopt;
}

std::optional<long long> int64Member(const std::string& object, const char* name) {
    const auto raw = memberValue(object, name);
    if (!raw) return std::nullopt;
    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(raw->c_str(), &end, 10);
    while (end && *end && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (errno == ERANGE || !end || end == raw->c_str() || *end != '\0')
        return std::nullopt;
    return value;
}

std::vector<std::string> metadataList(const std::string& object, const char* name) {
    std::vector<std::string> result;
    const auto raw = memberValue(object, name);
    if (!raw) return result;
    size_t offset = 0;
    skipSpace(*raw, offset);
    if (offset >= raw->size() || (*raw)[offset++] != '[') return result;
    while (offset < raw->size()) {
        skipSpace(*raw, offset);
        if (offset >= raw->size() || (*raw)[offset] == ']') break;
        const size_t start = offset;
        if (!skipValue(*raw, offset)) break;
        const auto item = raw->substr(start, offset - start);
        size_t itemOffset = 0;
        skipSpace(item, itemOffset);
        if (itemOffset < item.size() && item[itemOffset] == '"') {
            std::string value;
            if (readString(item, itemOffset, value) && !value.empty())
                result.push_back(std::move(value));
        } else if (itemOffset < item.size() && item[itemOffset] == '{') {
            auto value = stringMember(item, "name");
            if (value) result.push_back(std::move(*value));
        }
        skipSpace(*raw, offset);
        if (offset < raw->size() && (*raw)[offset] == ',') ++offset;
    }
    return result;
}

std::vector<int> integerList(const std::string& object, const char* name) {
    std::vector<int> result;
    const auto raw = memberValue(object, name);
    if (!raw) return result;
    size_t offset = 0;
    skipSpace(*raw, offset);
    if (offset >= raw->size() || (*raw)[offset++] != '[') return result;
    while (offset < raw->size()) {
        skipSpace(*raw, offset);
        if (offset >= raw->size() || (*raw)[offset] == ']') break;
        const size_t start = offset;
        if (!skipValue(*raw, offset)) break;
        const std::string item = raw->substr(start, offset - start);
        char* end = nullptr;
        errno = 0;
        const long value = std::strtol(item.c_str(), &end, 10);
        while (end && *end && std::isspace(static_cast<unsigned char>(*end))) ++end;
        if (errno != ERANGE && end && end != item.c_str() && *end == '\0' && value >= 0 &&
            value <= (std::numeric_limits<int>::max)()) {
            result.push_back(static_cast<int>(value));
        }
        skipSpace(*raw, offset);
        if (offset < raw->size() && (*raw)[offset] == ',') ++offset;
    }
    return result;
}

template<typename Value, typename Parser>
std::vector<Value> parseObjectMember(const std::string& json,
                                     const std::string& memberName,
                                     Parser parser) {
    std::vector<Value> result;
    size_t searchOffset = 0;
    while (searchOffset < json.size()) {
        const auto located = locateNestedMemberValue(json, memberName, searchOffset);
        if (!located) break;
        const auto& raw = located->value;
        searchOffset = located->end;

        size_t offset = 0;
        skipSpace(raw, offset);
        if (offset < raw.size() && raw[offset] == '{') {
            result.push_back(parser(raw));
            continue;
        }
        if (offset >= raw.size() || raw[offset++] != '[') continue;
        while (offset < raw.size()) {
            skipSpace(raw, offset);
            if (offset >= raw.size() || raw[offset] == ']') break;
            const size_t start = offset;
            if (!skipValue(raw, offset)) break;
            size_t itemOffset = start;
            skipSpace(raw, itemOffset);
            if (itemOffset < raw.size() && raw[itemOffset] == '{') {
                result.push_back(parser(raw.substr(start, offset - start)));
            }
            skipSpace(raw, offset);
            if (offset < raw.size() && raw[offset] == ',') ++offset;
        }
    }
    return result;
}

template<typename T, typename Validator>
std::optional<T> validated(std::optional<T> value, Validator validator) {
    return value && validator(*value) ? value : std::nullopt;
}

} // namespace

navidrome::ParsedSubsonicResponse navidrome::parseSubsonicResponseJson(
        const std::string& json) {
    ParsedSubsonicResponse result;
    const auto payload = nestedMemberValue(json, "subsonic-response");
    if (!payload) return result;
    result.valid = true;
    result.payloadJson = *payload;
    result.ok = stringMember(*payload, "status").value_or("") == "ok";
    result.version = stringMember(*payload, "version").value_or("");
    result.type = stringMember(*payload, "type").value_or("");
    result.serverVersion = stringMember(*payload, "serverVersion").value_or("");
    result.openSubsonic = boolMember(*payload, "openSubsonic").value_or(false);
    if (const auto errorObject = memberValue(*payload, "error")) {
        SubsonicError error;
        error.code = intMember(*errorObject, "code");
        error.message = stringMember(*errorObject, "message").value_or("");
        result.error = std::move(error);
    }
    return result;
}

navidrome::Artist navidrome::parseArtistJson(const std::string& json,
                                              const std::string& fallbackName) {
    Artist artist;
    artist.id = stringMember(json, "id").value_or("");
    artist.name = stringMember(json, "name").value_or(fallbackName);
    artist.coverArtId = stringMember(json, "coverArt").value_or("");
    artist.albumCount = validated(intMember(json, "albumCount"),
                                  [](int value) { return value >= 0; }).value_or(0);
    artist.starred = stringMember(json, "starred");
    return artist;
}

std::vector<navidrome::Artist> navidrome::parseArtistArrayJson(
        const std::string& json, const std::string& memberName,
        const std::string& fallbackName) {
    return parseObjectMember<Artist>(json, memberName, [&](const std::string& item) {
        return parseArtistJson(item, fallbackName);
    });
}

navidrome::Album navidrome::parseAlbumJson(const std::string& json,
                                            const std::string& fallbackArtistId,
                                            const std::string& fallbackName) {
    Album album;
    album.id = stringMember(json, "id").value_or("");
    album.name = stringMember(json, "name").value_or(fallbackName);
    album.artist = stringMember(json, "artist").value_or("");
    album.artistId = stringMember(json, "artistId").value_or(fallbackArtistId);
    album.coverArtId = stringMember(json, "coverArt").value_or("");
    album.year = validated(intMember(json, "year"),
                           [](int value) { return value > 0; }).value_or(0);
    album.songCount = validated(intMember(json, "songCount"),
                                [](int value) { return value >= 0; }).value_or(0);
    album.starred = stringMember(json, "starred");
    return album;
}

std::vector<navidrome::Album> navidrome::parseAlbumArrayJson(
        const std::string& json, const std::string& memberName,
        const std::string& fallbackArtistId, const std::string& fallbackName) {
    return parseObjectMember<Album>(json, memberName, [&](const std::string& item) {
        return parseAlbumJson(item, fallbackArtistId, fallbackName);
    });
}

navidrome::ServerPlaylist navidrome::parsePlaylistJson(const std::string& json) {
    ServerPlaylist playlist;
    playlist.id = stringMember(json, "id").value_or("");
    playlist.name = stringMember(json, "name").value_or("");
    playlist.owner = stringMember(json, "owner").value_or("");
    playlist.comment = stringMember(json, "comment").value_or("");
    playlist.coverArtId = stringMember(json, "coverArt").value_or("");
    playlist.created = stringMember(json, "created").value_or("");
    playlist.changed = stringMember(json, "changed").value_or("");
    playlist.songCount = validated(intMember(json, "songCount"),
                                   [](int value) { return value >= 0; }).value_or(0);
    playlist.duration = validated(doubleMember(json, "duration"),
                                  [](double value) { return value >= 0; }).value_or(0.0);
    playlist.isPublic = boolMember(json, "public");
    return playlist;
}

std::vector<navidrome::ServerPlaylist> navidrome::parsePlaylistArrayJson(
        const std::string& json, const std::string& memberName) {
    return parseObjectMember<ServerPlaylist>(json, memberName, parsePlaylistJson);
}

std::vector<navidrome::MusicFolder> navidrome::parseMusicFolderArrayJson(
        const std::string& json, const std::string& memberName) {
    return parseObjectMember<MusicFolder>(json, memberName, [](const std::string& item) {
        MusicFolder folder;
        const auto idString = stringMember(item, "id");
        if (idString) folder.id = *idString;
        else if (const auto id = intMember(item, "id")) folder.id = std::to_string(*id);
        folder.name = stringMember(item, "name").value_or("");
        return folder;
    });
}

navidrome::ScanStatus navidrome::parseScanStatusJson(const std::string& json) {
    ScanStatus status;
    const auto object = nestedMemberValue(json, "scanStatus");
    const std::string& source = object ? *object : json;
    status.scanning = boolMember(source, "scanning").value_or(false);
    status.lastScan = stringMember(source, "lastScan").value_or("");
    if (status.lastScan.empty()) {
        if (const auto scalar = int64Member(source, "lastScan"))
            status.lastScan = std::to_string(*scalar);
    }
    return status;
}

std::vector<navidrome::OpenSubsonicExtension>
navidrome::parseOpenSubsonicExtensionsJson(const std::string& json,
                                            const std::string& memberName) {
    auto result = parseObjectMember<OpenSubsonicExtension>(
        json, memberName, [](const std::string& item) {
            OpenSubsonicExtension extension;
            extension.name = stringMember(item, "name").value_or("");
            extension.versions = integerList(item, "versions");
            return extension;
        });
    if (!result.empty()) return result;
    return parseObjectMember<OpenSubsonicExtension>(
        json, "openSubsonicExtensions", [](const std::string& item) {
            OpenSubsonicExtension extension;
            extension.name = stringMember(item, "name").value_or("");
            extension.versions = integerList(item, "versions");
            return extension;
        });
}

navidrome::Song navidrome::parseSongJson(const std::string& json,
                                         const std::string& fallbackAlbumId,
                                         const std::string& fallbackTitle) {
    Song song;
    song.id = stringMember(json, "id").value_or("");
    song.title = stringMember(json, "title").value_or(fallbackTitle);
    song.artist = stringMember(json, "artist").value_or("");
    song.artistId = stringMember(json, "artistId").value_or("");
    song.album = stringMember(json, "album").value_or("");
    song.albumId = stringMember(json, "albumId").value_or(fallbackAlbumId);
    song.coverArtId = stringMember(json, "coverArt").value_or("");
    song.suffix = stringMember(json, "suffix").value_or("");
    song.created = stringMember(json, "created").value_or("");
    song.track = validated(intMember(json, "track"), [](int value) { return value > 0; }).value_or(0);
    song.year = validated(intMember(json, "year"), [](int value) { return value > 0; }).value_or(0);
    song.duration = validated(doubleMember(json, "duration"), [](double value) { return value > 0; }).value_or(0.0);

    song.parent = stringMember(json, "parent");
    song.path = stringMember(json, "path");
    song.coverArt = stringMember(json, "coverArt");
    song.contentType = stringMember(json, "contentType");
    song.transcodedSuffix = stringMember(json, "transcodedSuffix");
    song.transcodedContentType = stringMember(json, "transcodedContentType");
    song.starred = stringMember(json, "starred");
    song.played = stringMember(json, "played");
    song.userRating = validated(intMember(json, "userRating"),
                                [](int value) { return value >= 0 && value <= 5; });
    song.albumArtist = stringMember(json, "albumArtist");
    if (!song.albumArtist) song.albumArtist = stringMember(json, "displayAlbumArtist");
    song.displayArtist = stringMember(json, "displayArtist");
    song.sortName = stringMember(json, "sortName");
    song.composer = stringMember(json, "composer");
    song.displayComposer = stringMember(json, "displayComposer");
    song.comment = stringMember(json, "comment");
    song.isrc = stringMember(json, "isrc");
    song.musicBrainzId = stringMember(json, "musicBrainzId");
    song.musicBrainzArtistId = stringMember(json, "musicBrainzArtistId");
    song.musicBrainzAlbumId = stringMember(json, "musicBrainzAlbumId");
    song.musicBrainzReleaseArtistId = stringMember(json, "musicBrainzReleaseArtistId");
    song.explicitStatus = stringMember(json, "explicitStatus");
    song.grouping = stringMember(json, "grouping");

    song.playCount = validated(intMember(json, "playCount"), [](int value) { return value >= 0; });
    song.discNumber = validated(intMember(json, "discNumber"), [](int value) { return value > 0; });
    song.bpm = validated(doubleMember(json, "bpm"), [](double value) { return value > 0; });
    song.size = validated(int64Member(json, "size"), [](long long value) { return value >= 0; });
    song.bitRate = validated(intMember(json, "bitRate"), [](int value) { return value > 0; });
    song.bitDepth = validated(intMember(json, "bitDepth"), [](int value) { return value > 0; });
    song.samplingRate = validated(intMember(json, "samplingRate"), [](int value) { return value > 0; });
    song.channelCount = validated(intMember(json, "channelCount"), [](int value) { return value > 0; });

    song.genres = metadataList(json, "genres");
    if (song.genres.empty()) {
        const auto genre = stringMember(json, "genre");
        if (genre) song.genres.push_back(*genre);
    }
    song.groupings = metadataList(json, "groupings");
    song.moods = metadataList(json, "moods");

    if (const auto gainObject = memberValue(json, "replayGain")) {
        song.replayGain.trackGain = validated(doubleMember(*gainObject, "trackGain"), validFinite);
        song.replayGain.albumGain = validated(doubleMember(*gainObject, "albumGain"), validFinite);
        song.replayGain.trackPeak = validated(doubleMember(*gainObject, "trackPeak"), validPeak);
        song.replayGain.albumPeak = validated(doubleMember(*gainObject, "albumPeak"), validPeak);
        song.replayGain.baseGain = validated(doubleMember(*gainObject, "baseGain"), validFinite);
        song.replayGain.fallbackGain = validated(doubleMember(*gainObject, "fallbackGain"), validFinite);
    }
    normalizeSongLists(song);
    return song;
}

std::vector<navidrome::Song> navidrome::parseSongArrayJson(
        const std::string& json, const std::string& memberName,
        const std::string& fallbackAlbumId, const std::string& fallbackTitle) {
    return parseObjectMember<Song>(json, memberName, [&](const std::string& item) {
        return parseSongJson(item, fallbackAlbumId, fallbackTitle);
    });
}
