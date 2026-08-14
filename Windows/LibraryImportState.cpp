#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "LibraryImportState.h"
#include "Localization.h"
#if !defined(NAVIDROME_IMPORT_STATE_PURE_TEST)
#include <timeapi.h>
#include <helpers/foobar2000+atl.h>
#endif
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <unordered_set>
#include <utility>

namespace {

constexpr std::array<std::uint8_t, 8> kMagic = {'F','N','I','M','P','0','1','\0'};
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint32_t kMaxStringBytes = 8192;
constexpr std::uint32_t kMaxSongIds = 5000000;
constexpr std::uint32_t kMaxTailAnchors = 64;

std::mutex g_leaseMutex;
std::set<std::string> g_activeIdentities;
#if defined(NAVIDROME_IMPORT_STATE_PURE_TEST)
std::wstring g_testStateDirectory;
#endif

std::string hex64(std::uint64_t value) {
    char buffer[17] = {};
    sprintf_s(buffer, "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

std::string hashBytes(const std::uint8_t* data, std::size_t size) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hex64(hash);
}

std::string hashString(const std::string& value) {
    return hashBytes(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    value.data(), static_cast<int>(value.size()),
                                    nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                        value.data(), static_cast<int>(value.size()),
                        result.data(), count);
    return result;
}

std::wstring stateDirectory() {
#if defined(NAVIDROME_IMPORT_STATE_PURE_TEST)
    return g_testStateDirectory;
#else
    auto profile = core_api::pathInProfile("foo_navidrome");
    auto native = filesystem::g_get_native_path(profile.c_str());
    return utf8ToWide(native.c_str());
#endif
}

std::wstring statePathForIdentity(const std::string& identity) {
    auto directory = stateDirectory();
    if (directory.empty()) return {};
    if (!directory.empty() && directory.back() != L'\\' && directory.back() != L'/')
        directory.push_back(L'\\');
    return directory + L"import-" + utf8ToWide(hashString(identity)) + L".bin";
}

std::vector<std::uint8_t> readFile(const std::wstring& path, bool& exists,
                                   std::string& outError) {
    exists = false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND)
            outError = navidrome::l10n::stateReadError(code);
        return {};
    }
    exists = true;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        size.QuadPart > 512LL * 1024 * 1024) {
        outError = navidrome::l10n::stateTooLarge;
        CloseHandle(file);
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    bool ok = bytes.empty() || (ReadFile(file, bytes.data(),
        static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size());
    DWORD code = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok) {
        outError = navidrome::l10n::stateReadError(code);
        return {};
    }
    return bytes;
}

std::string generationForPath(const std::wstring& path, std::string& outError) {
    bool exists = false;
    auto bytes = readFile(path, exists, outError);
    if (!outError.empty()) return {};
    return exists ? hashBytes(bytes.data(), bytes.size()) : "missing";
}

bool writeFileDurable(const std::wstring& path,
                      const std::vector<std::uint8_t>& bytes,
                      std::string& outError) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        outError = navidrome::l10n::stateWriteError(GetLastError());
        return false;
    }
    DWORD written = 0;
    bool ok = (bytes.empty() || (WriteFile(file, bytes.data(),
        static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size())) &&
        FlushFileBuffers(file);
    DWORD code = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok) outError = navidrome::l10n::stateWriteError(code);
    return ok;
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
}

void appendU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
}

bool appendString(std::vector<std::uint8_t>& out, const std::string& value,
                  std::string& outError) {
    if (value.size() > kMaxStringBytes) {
        outError = navidrome::l10n::stateFieldTooLarge;
        return false;
    }
    appendU32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    return true;
}

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : m_bytes(bytes) {}

    bool bytes(void* target, std::size_t count) {
        if (count > m_bytes.size() - m_pos) return false;
        memcpy(target, m_bytes.data() + m_pos, count);
        m_pos += count;
        return true;
    }
    bool u32(std::uint32_t& value) {
        value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            std::uint8_t byte = 0;
            if (!bytes(&byte, 1)) return false;
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }
    bool u64(std::uint64_t& value) {
        value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            std::uint8_t byte = 0;
            if (!bytes(&byte, 1)) return false;
            value |= static_cast<std::uint64_t>(byte) << shift;
        }
        return true;
    }
    bool string(std::string& value) {
        std::uint32_t count = 0;
        if (!u32(count) || count > kMaxStringBytes || count > m_bytes.size() - m_pos)
            return false;
        value.assign(reinterpret_cast<const char*>(m_bytes.data() + m_pos), count);
        m_pos += count;
        return true;
    }
    bool done() const { return m_pos == m_bytes.size(); }

private:
    const std::vector<std::uint8_t>& m_bytes;
    std::size_t m_pos = 0;
};

} // namespace

std::string navidrome::importIdentity(const SubsonicRequestContext& context) {
    return serverAccountIdentity(context.serverUrl, context.username);
}

std::string navidrome::makeLibraryFingerprint(const std::vector<MusicFolder>& folders) {
    std::vector<std::string> ids;
    ids.reserve(folders.size());
    for (const auto& folder : folders) ids.push_back(folder.id);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::string joined;
    for (const auto& id : ids) joined += std::to_string(id.size()) + ":" + id + ";";
    return hashString(joined);
}

std::vector<std::uint8_t> navidrome::encodeImportState(
        const LibraryImportState& state, std::string& outError) {
    outError.clear();
    if (state.knownSongIds.size() > kMaxSongIds ||
        state.tailAnchors.size() > kMaxTailAnchors) {
        outError = navidrome::l10n::stateTooLarge;
        return {};
    }
    std::vector<std::string> known = state.knownSongIds;
    std::sort(known.begin(), known.end());
    if (std::adjacent_find(known.begin(), known.end()) != known.end()) {
        outError = navidrome::l10n::stateDuplicateId;
        return {};
    }

    std::vector<std::uint8_t> out(kMagic.begin(), kMagic.end());
    appendU32(out, kFormatVersion);
    if (!appendString(out, state.identity, outError) ||
        !appendString(out, state.serverType, outError) ||
        !appendString(out, state.serverVersion, outError) ||
        !appendString(out, state.libraryFingerprint, outError)) return {};
    appendU64(out, state.cursorCount);
    appendU32(out, static_cast<std::uint32_t>(known.size()));
    for (const auto& id : known)
        if (id.empty() || !appendString(out, id, outError)) {
            if (outError.empty()) outError = navidrome::l10n::stateEmptyId;
            return {};
        }
    appendU32(out, static_cast<std::uint32_t>(state.tailAnchors.size()));
    for (const auto& id : state.tailAnchors)
        if (id.empty() || !appendString(out, id, outError)) {
            if (outError.empty()) outError = navidrome::l10n::stateEmptyId;
            return {};
        }
    return out;
}

bool navidrome::decodeImportState(const std::vector<std::uint8_t>& bytes,
                                  LibraryImportState& outState,
                                  std::string& outError) {
    outError.clear();
    Reader reader(bytes);
    std::array<std::uint8_t, 8> magic{};
    std::uint32_t version = 0, knownCount = 0, tailCount = 0;
    LibraryImportState state;
    if (!reader.bytes(magic.data(), magic.size()) || magic != kMagic ||
        !reader.u32(version) || version != kFormatVersion ||
        !reader.string(state.identity) || !reader.string(state.serverType) ||
        !reader.string(state.serverVersion) || !reader.string(state.libraryFingerprint) ||
        !reader.u64(state.cursorCount) || !reader.u32(knownCount) ||
        knownCount > kMaxSongIds) {
        outError = navidrome::l10n::stateInvalid;
        return false;
    }
    std::unordered_set<std::string> seen;
    state.knownSongIds.reserve(knownCount);
    for (std::uint32_t i = 0; i < knownCount; ++i) {
        std::string id;
        if (!reader.string(id) || id.empty() || !seen.insert(id).second) {
            outError = navidrome::l10n::stateInvalid;
            return false;
        }
        state.knownSongIds.push_back(std::move(id));
    }
    if (!reader.u32(tailCount) || tailCount > kMaxTailAnchors) {
        outError = navidrome::l10n::stateInvalid;
        return false;
    }
    state.tailAnchors.reserve(tailCount);
    for (std::uint32_t i = 0; i < tailCount; ++i) {
        std::string id;
        if (!reader.string(id) || id.empty()) {
            outError = navidrome::l10n::stateInvalid;
            return false;
        }
        state.tailAnchors.push_back(std::move(id));
    }
    if (!reader.done() || state.identity.empty()) {
        outError = navidrome::l10n::stateInvalid;
        return false;
    }
    outState = std::move(state);
    return true;
}

navidrome::LoadedImportState navidrome::loadImportState(
        const SubsonicRequestContext& context) {
    LoadedImportState result;
    result.finalPath = statePathForIdentity(importIdentity(context));
    if (result.finalPath.empty()) {
        result.error = navidrome::l10n::statePathError;
        return result;
    }
    bool exists = false;
    auto bytes = readFile(result.finalPath, exists, result.error);
    result.exists = exists;
    if (!result.error.empty()) return result;
    result.generation = exists ? hashBytes(bytes.data(), bytes.size()) : "missing";
    if (!exists) {
        result.valid = true;
        return result;
    }
    result.valid = decodeImportState(bytes, result.state, result.error) &&
        result.state.identity == importIdentity(context);
    if (!result.valid && result.error.empty()) result.error = navidrome::l10n::stateIdentityMismatch;
    return result;
}

navidrome::PreparedImportState::~PreparedImportState() {
    if (!m_tempPath.empty()) DeleteFileW(m_tempPath.c_str());
}

navidrome::PreparedImportState::PreparedImportState(PreparedImportState&& other) noexcept
    : m_tempPath(std::move(other.m_tempPath)),
      m_finalPath(std::move(other.m_finalPath)),
      m_expectedGeneration(std::move(other.m_expectedGeneration)) {
    other.m_tempPath.clear();
}

navidrome::PreparedImportState& navidrome::PreparedImportState::operator=(
        PreparedImportState&& other) noexcept {
    if (this != &other) {
        if (!m_tempPath.empty()) DeleteFileW(m_tempPath.c_str());
        m_tempPath = std::move(other.m_tempPath);
        m_finalPath = std::move(other.m_finalPath);
        m_expectedGeneration = std::move(other.m_expectedGeneration);
        other.m_tempPath.clear();
    }
    return *this;
}

std::unique_ptr<navidrome::PreparedImportState>
navidrome::PreparedImportState::create(
        const LoadedImportState& loaded, const LibraryImportState& state,
        std::uint64_t operationId, std::string& outError) {
    auto bytes = encodeImportState(state, outError);
    if (!outError.empty()) return {};
    auto directory = stateDirectory();
    if (directory.empty() ||
        (!CreateDirectoryW(directory.c_str(), nullptr) &&
         GetLastError() != ERROR_ALREADY_EXISTS)) {
        outError = navidrome::l10n::stateWriteError(GetLastError());
        return {};
    }
    auto prepared = std::unique_ptr<PreparedImportState>(new PreparedImportState());
    prepared->m_finalPath = loaded.finalPath;
    prepared->m_expectedGeneration = loaded.generation.empty() ? "missing" : loaded.generation;
    prepared->m_tempPath = loaded.finalPath + L".tmp." + std::to_wstring(operationId) +
        L"." + std::to_wstring(GetCurrentThreadId());
    if (!writeFileDurable(prepared->m_tempPath, bytes, outError)) return {};
    return prepared;
}

bool navidrome::PreparedImportState::commit(std::string& outError) {
    if (m_tempPath.empty()) {
        outError = navidrome::l10n::stateNotPrepared;
        return false;
    }
    auto currentGeneration = generationForPath(m_finalPath, outError);
    if (!outError.empty()) return false;
    if (currentGeneration != m_expectedGeneration) {
        outError = navidrome::l10n::stateChanged;
        return false;
    }
    if (!MoveFileExW(m_tempPath.c_str(), m_finalPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        outError = navidrome::l10n::stateCommitError(GetLastError());
        return false;
    }
    m_tempPath.clear();
    return true;
}

std::shared_ptr<navidrome::ImportLease> navidrome::ImportLease::tryAcquire(
        const std::string& identity) {
    std::lock_guard<std::mutex> lock(g_leaseMutex);
    if (!g_activeIdentities.insert(identity).second) return {};
    return std::shared_ptr<ImportLease>(new ImportLease(identity));
}

navidrome::ImportLease::~ImportLease() {
    std::lock_guard<std::mutex> lock(g_leaseMutex);
    g_activeIdentities.erase(m_identity);
}

#if defined(NAVIDROME_IMPORT_STATE_PURE_TEST)
void navidrome::setImportStateTestDirectory(const std::wstring& directory) {
    g_testStateDirectory = directory;
}
#endif
