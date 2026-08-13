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

std::optional<std::string> nestedMemberValue(const std::string& json,
                                             const std::string& name) {
    size_t offset = 0;
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

        const size_t start = valueOffset;
        if (!skipValue(json, valueOffset)) return std::nullopt;
        return json.substr(start, valueOffset - start);
    }
    return std::nullopt;
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

template<typename T, typename Validator>
std::optional<T> validated(std::optional<T> value, Validator validator) {
    return value && validator(*value) ? value : std::nullopt;
}

} // namespace

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
    std::vector<Song> result;
    const auto raw = nestedMemberValue(json, memberName);
    if (!raw) return result;

    size_t offset = 0;
    skipSpace(*raw, offset);
    if (offset < raw->size() && (*raw)[offset] == '{') {
        result.push_back(parseSongJson(*raw, fallbackAlbumId, fallbackTitle));
        return result;
    }
    if (offset >= raw->size() || (*raw)[offset++] != '[') return result;

    while (offset < raw->size()) {
        skipSpace(*raw, offset);
        if (offset >= raw->size() || (*raw)[offset] == ']') break;
        const size_t start = offset;
        if (!skipValue(*raw, offset)) break;
        size_t itemOffset = start;
        skipSpace(*raw, itemOffset);
        if (itemOffset < raw->size() && (*raw)[itemOffset] == '{') {
            result.push_back(parseSongJson(raw->substr(start, offset - start),
                                           fallbackAlbumId, fallbackTitle));
        }
        skipSpace(*raw, offset);
        if (offset < raw->size() && (*raw)[offset] == ',') ++offset;
    }
    return result;
}
