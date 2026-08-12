#pragma once

#include "SubsonicClientWin.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace navidrome {

struct LibraryImportState {
    std::string identity;
    std::string serverType;
    std::string serverVersion;
    std::string libraryFingerprint;
    std::uint64_t cursorCount = 0;
    std::vector<std::string> knownSongIds;
    std::vector<std::string> tailAnchors;
};

struct LoadedImportState {
    bool exists = false;
    bool valid = false;
    LibraryImportState state;
    std::string generation;
    std::wstring finalPath;
    std::string error;
};

std::string normalizeServerUrl(const std::string& url);
std::string importIdentity(const SubsonicRequestContext& context);
std::string makeLibraryFingerprint(const std::vector<MusicFolder>& folders);

std::vector<std::uint8_t> encodeImportState(const LibraryImportState& state,
                                             std::string& outError);
bool decodeImportState(const std::vector<std::uint8_t>& bytes,
                       LibraryImportState& outState, std::string& outError);

LoadedImportState loadImportState(const SubsonicRequestContext& context);

#if defined(NAVIDROME_IMPORT_STATE_PURE_TEST)
void setImportStateTestDirectory(const std::wstring& directory);
#endif

class PreparedImportState {
public:
    PreparedImportState() = default;
    ~PreparedImportState();
    PreparedImportState(const PreparedImportState&) = delete;
    PreparedImportState& operator=(const PreparedImportState&) = delete;
    PreparedImportState(PreparedImportState&& other) noexcept;
    PreparedImportState& operator=(PreparedImportState&& other) noexcept;

    static std::unique_ptr<PreparedImportState> create(
        const LoadedImportState& loaded, const LibraryImportState& state,
        std::uint64_t operationId, std::string& outError);

    bool commit(std::string& outError);
    bool isReady() const { return !m_tempPath.empty(); }

private:
    std::wstring m_tempPath;
    std::wstring m_finalPath;
    std::string m_expectedGeneration;
};

class ImportLease {
public:
    ~ImportLease();
    ImportLease(const ImportLease&) = delete;
    ImportLease& operator=(const ImportLease&) = delete;

    static std::shared_ptr<ImportLease> tryAcquire(const std::string& identity);

private:
    explicit ImportLease(std::string identity) : m_identity(std::move(identity)) {}
    std::string m_identity;
};

} // namespace navidrome
