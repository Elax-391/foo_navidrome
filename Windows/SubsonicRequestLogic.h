#pragma once

#include "../SubsonicTypes.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace navidrome {

using OrderedParameters = std::vector<std::pair<std::string, std::string>>;

enum class RequestMethod {
    Get,
    FormPost,
};

std::string encodeSubsonicValue(const std::string& value);
std::string encodeFormParameters(const OrderedParameters& parameters);

const char* albumListKindParameter(AlbumListKind kind) noexcept;

enum class PlaylistWriteMode {
    SingleGet,
    SingleFormPost,
    IncrementalGet,
    Invalid,
};

struct PlaylistWritePlan {
    PlaylistWriteMode mode = PlaylistWriteMode::Invalid;
    OrderedParameters initialParameters;
    std::vector<OrderedParameters> appendBatches;
    std::string error;
};

PlaylistWritePlan planPlaylistWrite(
    const std::optional<std::string>& playlistId,
    const std::string& name,
    const std::vector<std::string>& orderedSongIds,
    bool formPostAdvertised,
    std::size_t conservativeParameterBudget = 2560);

} // namespace navidrome
