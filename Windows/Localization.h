#pragma once

#include <cstddef>
#include <string>

// Windows-only user-visible strings. Wide literals are passed directly to
// Win32 controls; UTF-8 literals are passed to foobar2000 APIs and status text.
namespace navidrome::l10n {

inline constexpr wchar_t headersWindowTitle[] = L"Navidrome — 自定义 HTTP 请求头";
inline constexpr wchar_t headersHint[] =
    L"每行一个请求头，格式为 Name: Value（例如用于 Cloudflare Zero Trust 隧道）。";
inline constexpr wchar_t cloudflareHeaders[] = L"插入 Cloudflare 请求头";
inline constexpr wchar_t save[] = L"保存";
inline constexpr wchar_t cancel[] = L"取消";

inline constexpr wchar_t serverUrl[] = L"服务器 URL：";
inline constexpr wchar_t username[] = L"用户名：";
inline constexpr wchar_t password[] = L"密码：";
inline constexpr wchar_t testConnection[] = L"测试连接";
inline constexpr wchar_t testing[] = L"测试中…";
inline constexpr wchar_t connected[] = L"已连接！";
inline constexpr char failedUtf8[] = u8"失败";
inline constexpr wchar_t customHeaders[] = L"自定义请求头…";

inline constexpr wchar_t browserTitle[] = L"Navidrome 浏览器";
inline constexpr wchar_t searchCue[] = L"搜索艺术家、专辑、歌曲…";
inline constexpr wchar_t addToPlaylist[] = L"添加到播放列表";
inline constexpr wchar_t addAllToPlaylist[] = L"添加全部";
inline constexpr wchar_t reconcileLibrary[] = L"完整核对";
inline constexpr wchar_t playNow[] = L"立即播放";
inline constexpr wchar_t refresh[] = L"刷新";

inline constexpr char loadingArtists[] = u8"正在加载艺术家…";
inline constexpr char loading[] = u8"加载中…";
inline constexpr char searching[] = u8"正在搜索…";
inline constexpr char loadingTracks[] = u8"正在加载歌曲…";
inline constexpr char queueBusy[] = u8"已有导入任务正在进行";
inline constexpr char accountImportBusy[] = u8"该 Navidrome 账户已有导入任务";
inline constexpr char libraryNotLoaded[] = u8"完整音乐库尚未加载";
inline constexpr char selectAtLeastOne[] = u8"请至少选择一项";
inline constexpr char noSongsSelected[] = u8"未选择歌曲";
inline constexpr char noSongsFound[] = u8"未找到可添加的歌曲";
inline constexpr char checkingNewTracks[] = u8"正在检查新增歌曲…";
inline constexpr char reconcilingLibrary[] = u8"正在完整核对音乐库…";
inline constexpr char libraryUpToDate[] = u8"没有新增歌曲，音乐库已是最新";
inline constexpr char libraryScanning[] = u8"Navidrome 正在扫描音乐库，请稍后重试";
inline constexpr char pagingUnstable[] = u8"音乐库在分页期间发生变化，请稍后重试";
inline constexpr char pagingInvalid[] = u8"歌曲分页顺序无效，未更新播放列表";
inline constexpr char emptySongId[] = u8"服务器返回了空歌曲 ID";
inline constexpr char duplicateSongId[] = u8"服务器返回了重复歌曲 ID";
inline constexpr char stateInvalid[] = u8"增量状态文件无效";
inline constexpr char stateTooLarge[] = u8"增量状态文件过大";
inline constexpr char stateFieldTooLarge[] = u8"增量状态字段过大";
inline constexpr char stateDuplicateId[] = u8"增量状态包含重复歌曲 ID";
inline constexpr char stateEmptyId[] = u8"增量状态包含空歌曲 ID";
inline constexpr char stateIdentityMismatch[] = u8"增量状态与当前账户不匹配";
inline constexpr char statePathError[] = u8"无法确定增量状态文件路径";
inline constexpr char stateNotPrepared[] = u8"增量状态尚未准备完成";
inline constexpr char stateChanged[] = u8"增量状态已被另一个任务更新";
inline constexpr char stateRollbackFailed[] = u8"状态提交失败，且无法撤销本次播放列表追加";

inline std::string artistCount(std::size_t count) {
    return std::to_string(count) + u8" 位艺术家";
}

inline std::string searchResultCount(std::size_t count) {
    return std::to_string(count) + u8" 条搜索结果";
}

inline std::string importProgress(std::size_t completed, std::size_t total,
                                  std::size_t songs, std::size_t failed) {
    std::string result = u8"正在导入：已处理 " + std::to_string(completed) +
        "/" + std::to_string(total) + u8" 位艺术家，已找到 " +
        std::to_string(songs) + u8" 首歌曲";
    if (failed > 0)
        result += u8"，" + std::to_string(failed) + u8" 个项目加载失败";
    return result;
}

inline std::string pageProgress(std::size_t scanned, std::size_t added) {
    return u8"正在检查：已扫描 " + std::to_string(scanned) + u8" 首，发现 " +
        std::to_string(added) + u8" 首新增歌曲";
}

inline std::string stateReadError(unsigned long code) {
    return u8"读取增量状态失败（错误码=" + std::to_string(code) + u8"）";
}

inline std::string stateWriteError(unsigned long code) {
    return u8"写入增量状态失败（错误码=" + std::to_string(code) + u8"）";
}

inline std::string stateCommitError(unsigned long code) {
    return u8"提交增量状态失败（错误码=" + std::to_string(code) + u8"）";
}

inline std::string stateCommitRolledBack(const std::string& message) {
    return u8"状态提交失败，已撤销本次播放列表追加：" + message;
}

inline std::string addedTracks(std::size_t count) {
    return u8"已添加 " + std::to_string(count) + u8" 首歌曲";
}

inline std::string addedTracksWithFailures(std::size_t count, std::size_t failed) {
    return addedTracks(count) + u8"，" + std::to_string(failed) +
        u8" 个项目加载失败";
}

inline std::string allItemsFailed(std::size_t failed) {
    return u8"未添加歌曲，" + std::to_string(failed) + u8" 个项目加载失败";
}

inline std::string recursiveImportFailed(std::size_t failed) {
    return u8"兼容模式未能完整读取音乐库，" + std::to_string(failed) +
        u8" 个请求失败";
}

inline std::string error(const std::string& message) {
    return u8"错误：" + message;
}

inline std::string httpError(unsigned long status) {
    return u8"HTTP 请求失败（状态码 " + std::to_string(status) + u8"）";
}

inline std::string requestError(unsigned long code) {
    return u8"请求失败（错误码=" + std::to_string(code) + u8"）";
}

inline constexpr char unknownSubsonicError[] = u8"未知的 Subsonic 错误";
inline constexpr char invalidResponse[] = u8"响应无效";
inline constexpr char invalidUrl[] = u8"URL 无效";
inline constexpr char winHttpOpenFailed[] = u8"WinHTTP 打开失败";
inline constexpr char connectFailed[] = u8"连接失败";
inline constexpr char unknownArtist[] = u8"未知艺术家";
inline constexpr char unknownAlbum[] = u8"未知专辑";
inline constexpr char unknownTitle[] = u8"未知标题";

inline constexpr char menuOpenBrowser[] = u8"打开 Navidrome 浏览器";
inline constexpr char menuBrowseDescription[] = u8"浏览并播放 Navidrome 中的音乐";

} // namespace navidrome::l10n
