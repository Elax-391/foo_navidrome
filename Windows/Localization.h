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
inline constexpr wchar_t playNow[] = L"立即播放";
inline constexpr wchar_t refresh[] = L"刷新";

inline constexpr char loadingArtists[] = u8"正在加载艺术家…";
inline constexpr char loading[] = u8"加载中…";
inline constexpr char searching[] = u8"正在搜索…";
inline constexpr char loadingTracks[] = u8"正在加载歌曲…";
inline constexpr char selectAtLeastOne[] = u8"请至少选择一项";
inline constexpr char noSongsSelected[] = u8"未选择歌曲";

inline std::string artistCount(std::size_t count) {
    return std::to_string(count) + u8" 位艺术家";
}

inline std::string addedTracks(std::size_t count) {
    return u8"已添加 " + std::to_string(count) + u8" 首歌曲";
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
