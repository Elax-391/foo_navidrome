#include "SubsonicRequestLogic.h"

#include <cstdio>

std::string navidrome::encodeSubsonicValue(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        const bool asciiAlphaNumeric = (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
        if (asciiAlphaNumeric || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            result.push_back(static_cast<char>(ch));
        } else {
            char escaped[4] = {};
            std::snprintf(escaped, sizeof(escaped), "%%%02X", ch);
            result += escaped;
        }
    }
    return result;
}

std::string navidrome::encodeFormParameters(const OrderedParameters& parameters) {
    std::string result;
    for (const auto& parameter : parameters) {
        if (!result.empty()) result.push_back('&');
        result += encodeSubsonicValue(parameter.first);
        result.push_back('=');
        result += encodeSubsonicValue(parameter.second);
    }
    return result;
}

const char* navidrome::albumListKindParameter(AlbumListKind kind) noexcept {
    static constexpr const char* values[] = {
        "newest", "frequent", "recent", "random", "starred"
    };
    const auto index = static_cast<int>(kind);
    return index >= 0 && index < static_cast<int>(sizeof(values) / sizeof(values[0]))
        ? values[index] : "newest";
}

navidrome::PlaylistWritePlan navidrome::planPlaylistWrite(
        const std::optional<std::string>& playlistId, const std::string& name,
        const std::vector<std::string>& orderedSongIds,
        bool formPostAdvertised, std::size_t conservativeParameterBudget) {
    PlaylistWritePlan plan;
    if ((!playlistId || playlistId->empty()) && name.empty()) {
        plan.error = "playlist id or name is required";
        return plan;
    }

    OrderedParameters complete;
    complete.emplace_back(playlistId && !playlistId->empty() ? "playlistId" : "name",
                          playlistId && !playlistId->empty() ? *playlistId : name);
    for (const auto& songId : orderedSongIds) complete.emplace_back("songId", songId);

    if (formPostAdvertised) {
        plan.mode = PlaylistWriteMode::SingleFormPost;
        plan.initialParameters = std::move(complete);
        return plan;
    }
    if (encodeFormParameters(complete).size() <= conservativeParameterBudget) {
        plan.mode = PlaylistWriteMode::SingleGet;
        plan.initialParameters = std::move(complete);
        return plan;
    }

    if (!playlistId || playlistId->empty()) {
        plan.error = "a long GET playlist creation must first obtain a playlist id";
        return plan;
    }

    plan.mode = PlaylistWriteMode::IncrementalGet;
    plan.initialParameters.emplace_back(
        "playlistId", *playlistId);
    if (encodeFormParameters(plan.initialParameters).size() > conservativeParameterBudget) {
        plan.mode = PlaylistWriteMode::Invalid;
        plan.initialParameters.clear();
        plan.error = "the encoded playlist id exceeds the GET limit";
        return plan;
    }

    OrderedParameters batch = {{"playlistId", *playlistId}};
    for (const auto& songId : orderedSongIds) {
        OrderedParameters candidate = batch;
        candidate.emplace_back("songIdToAdd", songId);
        if (encodeFormParameters(candidate).size() > conservativeParameterBudget) {
            if (batch.size() == 1) {
                plan.mode = PlaylistWriteMode::Invalid;
                plan.appendBatches.clear();
                plan.error = "one encoded song id exceeds the GET limit";
                return plan;
            }
            plan.appendBatches.push_back(std::move(batch));
            batch = {{"playlistId", *playlistId}};
            batch.emplace_back("songIdToAdd", songId);
            if (encodeFormParameters(batch).size() > conservativeParameterBudget) {
                plan.mode = PlaylistWriteMode::Invalid;
                plan.appendBatches.clear();
                plan.error = "one encoded song id exceeds the GET limit";
                return plan;
            }
        } else {
            batch = std::move(candidate);
        }
    }
    if (batch.size() > 1) plan.appendBatches.push_back(std::move(batch));
    return plan;
}
