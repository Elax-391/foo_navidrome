#pragma once

#include "LibraryImportState.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace navidrome {

enum class LibraryImportMode {
    FastTail,
    FullReconcile,
    RecursiveFallback,
};

struct LibraryImportProgress {
    std::size_t completed = 0;
    std::size_t total = 0;
    std::size_t scanned = 0;
    std::size_t added = 0;
    std::size_t failed = 0;
    bool recursive = false;
};

struct LibraryImportResult {
    LibraryImportMode mode = LibraryImportMode::FullReconcile;
    std::vector<Song> candidates;
    std::unique_ptr<PreparedImportState> preparedState;
    std::shared_ptr<ImportLease> lease;
    std::string error;
    std::size_t scanned = 0;
    bool cancelled = false;
};

using LibraryImportProgressCallback =
    std::function<void(const LibraryImportProgress&)>;

LibraryImportResult runLibraryImport(
    const SubsonicRequestContext& context,
    bool forceFull,
    std::uint64_t operationId,
    const std::shared_ptr<std::atomic_bool>& cancel,
    LibraryImportProgressCallback progress);

} // namespace navidrome
