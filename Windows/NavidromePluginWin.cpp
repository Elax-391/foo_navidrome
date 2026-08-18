#include "stdafx.h"
#include "BrowserWindow.h"
#include "Localization.h"
#include "SubsonicClientWin.h"
#include "MediaEnrichmentLogic.h"
#include "EsLyricBridge.h"
#include "ScrobbleService.h"
#include "ServerProfileConfig.h"
#include "ServerProfiles.h"
#include "ServerIdentity.h"
#include "TrackUriMetadata.h"
#include <SDK/cfg_var.h>
#include <SDK/album_art.h>
#include <SDK/album_art_helpers.h>
#include <SDK/initquit.h>
#include <SDK/play_callback.h>
#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <cctype>
#include <set>
#include <mutex>
#include <memory>
#include <map>
#include <optional>
#include <utility>
#include <vector>
#pragma comment(lib, "winhttp.lib")

static std::wstring u8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    if (!w.empty() && w.back() == 0) w.pop_back();
    return w;
}

static std::string wToU8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    if (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

// ---------------------------------------------------------------------------
// GUIDs — must match NavidromePlugin.mm so settings persist cross-platform
// ---------------------------------------------------------------------------
static constexpr GUID guid_cfg_server_url = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x01} };
static constexpr GUID guid_cfg_username   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x02} };
static constexpr GUID guid_cfg_password   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x03} };
static constexpr GUID guid_cfg_salt       = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x04} };
static constexpr GUID guid_prefs_page     = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x05} };
static constexpr GUID guid_mainmenu_group = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x06} };
static constexpr GUID guid_mainmenu_cmd   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x07} };
static constexpr GUID guid_cfg_custom_headers = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0a} };
static constexpr GUID guid_cfg_scrobble   = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0b} };
static constexpr GUID guid_cfg_stream_format = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0c} };
static constexpr GUID guid_cfg_max_bitrate = { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x0d} };

// ---------------------------------------------------------------------------
// Config vars
// ---------------------------------------------------------------------------
namespace navidrome {
    cfg_string cfg_server_url(guid_cfg_server_url, "http://localhost:4533/");
    cfg_string cfg_username  (guid_cfg_username,   "");
    cfg_string cfg_password  (guid_cfg_password,   "");
    cfg_string cfg_salt      (guid_cfg_salt,        "fb2k_navidrome");
    // Extra HTTP headers (one "Name: Value" per line) sent on every request —
    // API, cover art and audio stream. Used e.g. for Cloudflare Access
    // service-token headers when Navidrome sits behind a Zero Trust tunnel.
    cfg_string cfg_custom_headers(guid_cfg_custom_headers, "");
    // Keep the modern bool type and GUID aligned with the upstream/macOS
    // setting so its serialization remains stable across platforms.
    cfg_var_modern::cfg_bool cfg_scrobble(guid_cfg_scrobble, true);
    cfg_string cfg_stream_format(guid_cfg_stream_format, "");
    cfg_var_modern::cfg_int cfg_max_bitrate(guid_cfg_max_bitrate, 0);
}

// ---------------------------------------------------------------------------
// Custom HTTP headers editor — a standalone window opened from the prefs page.
// Multiline "Name: Value" per line; persisted to cfg_custom_headers. The
// "Cloudflare headers" button inserts the two CF Access service-token header
// names so the user only has to paste the id/secret values.
// ---------------------------------------------------------------------------
class NavidromeHeadersWindow : public CWindowImpl<NavidromeHeadersWindow> {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_HeadersWnd")

    static NavidromeHeadersWindow& get() { static NavidromeHeadersWindow inst; return inst; }

    void show() {
        if (!IsWindow()) {
            Create(nullptr, CWindow::rcDefault, navidrome::l10n::headersWindowTitle,
                   WS_OVERLAPPEDWINDOW, 0);
            SetWindowPos(nullptr, 0, 0, 520, 360,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
        } else {
            ShowWindow(SW_SHOW);
            SetForegroundWindow(*this);
        }
        loadText();
    }

    BEGIN_MSG_MAP(NavidromeHeadersWindow)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
        MSG_WM_GETMINMAXINFO(OnGetMinMaxInfo)
        COMMAND_ID_HANDLER_EX(IDC_CF,     OnCloudflare)
        COMMAND_ID_HANDLER_EX(IDC_SAVE,   OnSave)
        COMMAND_ID_HANDLER_EX(IDC_CANCEL, OnCancel)
    END_MSG_MAP()

private:
    enum { IDC_EDIT = 3001, IDC_CF = 3002, IDC_SAVE = 3003, IDC_CANCEL = 3004, IDC_HINT = 3005 };
    CEdit m_edit;

    LRESULT OnCreate(LPCREATESTRUCT) {
        HFONT f = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto setFont = [&](HWND h) { SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0); };

        HWND hint = CreateWindowW(L"STATIC",
            navidrome::l10n::headersHint,
            WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HINT)), nullptr, nullptr);
        setFont(hint);

        m_edit.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
            ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, 0, IDC_EDIT);
        m_edit.SetFont(f);

        auto mkBtn = [&](int id, const wchar_t* label) {
            HWND b = CreateWindowW(L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 10, 10, *this,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            setFont(b);
        };
        mkBtn(IDC_CF,     navidrome::l10n::cloudflareHeaders);
        mkBtn(IDC_SAVE,   navidrome::l10n::save);
        mkBtn(IDC_CANCEL, navidrome::l10n::cancel);
        return 0;
    }

    void OnGetMinMaxInfo(LPMINMAXINFO info) {
        if (info->ptMinTrackSize.x < 440) info->ptMinTrackSize.x = 440;
        if (info->ptMinTrackSize.y < 260) info->ptMinTrackSize.y = 260;
    }

    LRESULT OnSize(UINT, CSize sz) {
        const int pad = 10, btnH = 26, desiredCfBtnW = 190,
                  desiredActionBtnW = 80, hintH = 44;
        const int w = sz.cx > 0 ? sz.cx : 0;
        const int h = sz.cy > 0 ? sz.cy : 0;
        const int contentW = w > 2 * pad ? w - 2 * pad : 0;
        const int editH = h > hintH + btnH + 3 * pad + 4
            ? h - hintH - btnH - 3 * pad - 4
            : 0;
        ::SetWindowPos(GetDlgItem(IDC_HINT), nullptr, pad, pad, contentW, hintH,
                       SWP_NOZORDER);
        m_edit.SetWindowPos(nullptr, pad, pad + hintH + 4, contentW,
                            editH, SWP_NOZORDER);
        const int by = h > btnH + pad ? h - btnH - pad : 0;
        const int desiredButtonsW = desiredCfBtnW + 2 * desiredActionBtnW;
        const int availableButtonsW = w > 4 * pad ? w - 4 * pad : 0;
        const bool compact = availableButtonsW < desiredButtonsW;
        const int cfBtnW = compact ? availableButtonsW * desiredCfBtnW / desiredButtonsW
                                   : desiredCfBtnW;
        const int actionBtnW = compact ? (availableButtonsW - cfBtnW) / 2
                                       : desiredActionBtnW;
        ::SetWindowPos(GetDlgItem(IDC_CF),     nullptr, pad, by, cfBtnW, btnH, SWP_NOZORDER);
        ::SetWindowPos(GetDlgItem(IDC_CANCEL), nullptr, w - pad - actionBtnW, by, actionBtnW, btnH, SWP_NOZORDER);
        ::SetWindowPos(GetDlgItem(IDC_SAVE),   nullptr, w - 2 * pad - actionBtnW * 2, by, actionBtnW, btnH, SWP_NOZORDER);
        return 0;
    }

    void loadText() {
        std::wstring w = u8ToWide(navidrome::cfg_custom_headers.get().c_str());
        m_edit.SetWindowText(w.c_str());
    }

    std::string editTextU8() {
        int len = m_edit.GetWindowTextLength();
        std::wstring w(len + 1, L'\0');
        m_edit.GetWindowText(&w[0], len + 1);
        w.resize(len);
        return wToU8(w);
    }

    void OnSave(UINT, int, HWND) {
        navidrome::cfg_custom_headers.set(editTextU8().c_str());
        navidrome::CoverCache::instance().clear();

        // Trigger ESLyric bridge update when headers change
        auto ctx = navidrome::SubsonicClientWin::get().snapshot();
        std::string err = navidrome::EsLyricBridge::installOrUpdate(ctx);

        if (!err.empty()) {
            const auto message = navidrome::l10n::eslyricBridgeError(err);
            console::print(message.c_str());
        }

        ShowWindow(SW_HIDE);
    }

    void OnCancel(UINT, int, HWND) { ShowWindow(SW_HIDE); }

    // Append the two CF Access header names if they're not already present, so
    // the user just pastes the id/secret values after the colon.
    void OnCloudflare(UINT, int, HWND) {
        std::string text = editTextU8();
        std::string lower = text;
        for (char& c : lower) c = (char)tolower((unsigned char)c);
        auto ensure = [&](const char* headerName) {
            std::string needle = headerName;
            for (char& c : needle) c = (char)tolower((unsigned char)c);
            if (lower.find(needle) != std::string::npos) return;
            if (!text.empty() && text.back() != '\n') text += "\r\n";
            text += headerName;
            text += ": ";
            text += "\r\n";
            lower += needle;  // keep dedupe state consistent across both inserts
        };
        ensure("CF-Access-Client-Id");
        ensure("CF-Access-Client-Secret");
        m_edit.SetWindowText(u8ToWide(text).c_str());
        m_edit.SetFocus();
    }
};

// ---------------------------------------------------------------------------
// Preferences page (programmatic window — no .rc file required)
// ---------------------------------------------------------------------------
struct NavidromePrefsDispatchState {
    HWND hwnd = nullptr;
    bool alive = false;
    std::string lastRouteReason;
    bool automaticRouteChange = false;
};

class NavidromePrefsInstance : public CWindowImpl<NavidromePrefsInstance>,
                               public preferences_page_instance {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_PrefsWnd")

    explicit NavidromePrefsInstance(preferences_page_callback::ptr cb) : m_cb(cb) {}

    // preferences_page_instance
    HWND      get_wnd() override { return m_hWnd; }
    t_uint32  get_state() override {
        // preferences_state has no "unchanged" constant — the unchanged state is 0.
        return m_changed ? preferences_state::changed | preferences_state::resettable
                         : 0;
    }
    void apply()  override {
        if (!saveSettings()) return;
        navidrome::ScrobbleCoordinator::get().setEnabled(
            navidrome::cfg_scrobble.get());
        navidrome::CoverCache::instance().clear();

        // Trigger ESLyric bridge update (design §3.5)
        auto ctx = navidrome::SubsonicClientWin::get().snapshot();
        std::string err = navidrome::EsLyricBridge::installOrUpdate(ctx);

        if (!err.empty()) {
            const auto message = navidrome::l10n::eslyricBridgeError(err);
            console::print(message.c_str());
        }

        m_profileChanged = false;
        m_settingsChanged = false;
        m_changed = false;
        SetDlgItemText(IDC_STATUS, navidrome::l10n::profileSaved);
        notifyCb();
    }
    void reset()  override {
        ++m_testRequestId;
        leaveAddMode();
        m_drafts.clear();
        navidrome::ServerProfile profile;
        const auto* active = formProfile(m_storedState.activeProfileId);
        profile.id = active ? active->id : "legacy-default";
        profile.name = u8"默认服务器";
        profile.routes.push_back({"route-1", "http://localhost:4533/"});
        profile.preferredRouteId = "route-1";
        profile.autoFailover = true;
        profile = navidrome::canonicalizeServerProfile(std::move(profile));
        navidrome::ServerProfileState resetState;
        resetState.activeProfileId = profile.id;
        resetState.profiles.push_back(profile);
        m_resetState = std::move(resetState);
        rebuildProfileList(profile.id);
        loadProfileControls(profile);
        ::EnableWindow(GetDlgItem(IDC_PROFILE), FALSE);
        ::EnableWindow(GetDlgItem(IDC_ADD_PROFILE), FALSE);
        ::EnableWindow(GetDlgItem(IDC_DELETE_PROFILE), FALSE);
        m_loading = true;
        CheckDlgButton(IDC_SCROBBLE, BST_CHECKED);
        m_format.SetCurSel(0);
        m_bitrate.SetCurSel(0);
        m_loading = false;
        m_settingsChanged = true;
        updateChangedState();
        notifyCb();
    }

    BEGIN_MSG_MAP(NavidromePrefsInstance)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MESSAGE_HANDLER_EX(WM_VSCROLL, OnRouteScroll)
        MESSAGE_HANDLER_EX(WM_TEST_RESULT, OnTestResult)
        MESSAGE_HANDLER_EX(WM_ROUTE_CHANGED, OnRuntimeRouteChanged)
        COMMAND_HANDLER_EX(IDC_PROFILE, CBN_SELCHANGE, OnProfileSelected)
        COMMAND_RANGE_HANDLER_EX(IDC_ROUTE_RADIO_BASE, IDC_ROUTE_RADIO_LAST,
                                 OnRoutePreferred)
        COMMAND_RANGE_HANDLER_EX(IDC_ROUTE_EDIT_BASE, IDC_ROUTE_EDIT_LAST,
                                 OnRouteEdit)
        COMMAND_RANGE_HANDLER_EX(IDC_ROUTE_TEST_BASE, IDC_ROUTE_TEST_LAST,
                                 OnRouteTest)
        COMMAND_HANDLER_EX(IDC_ADD_ROUTE, BN_CLICKED, OnAddRoute)
        COMMAND_HANDLER_EX(IDC_DELETE_ROUTE, BN_CLICKED, OnDeleteRoute)
        COMMAND_HANDLER_EX(IDC_MOVE_ROUTE_UP, BN_CLICKED, OnMoveRouteUp)
        COMMAND_HANDLER_EX(IDC_MOVE_ROUTE_DOWN, BN_CLICKED, OnMoveRouteDown)
        COMMAND_HANDLER_EX(IDC_AUTO_FAILOVER, BN_CLICKED, OnProfileChanged)
        COMMAND_HANDLER_EX(IDC_ADD_PROFILE, BN_CLICKED, OnAddProfile)
        COMMAND_HANDLER_EX(IDC_DELETE_PROFILE, BN_CLICKED, OnDeleteProfile)
        COMMAND_HANDLER_EX(IDC_NAME, EN_CHANGE, OnProfileChanged)
        COMMAND_HANDLER_EX(IDC_USER, EN_CHANGE, OnProfileChanged)
        COMMAND_HANDLER_EX(IDC_PASS, EN_CHANGE, OnProfileChanged)
        COMMAND_HANDLER_EX(IDC_HEADERS, BN_CLICKED, OnHeaders)
        COMMAND_HANDLER_EX(IDC_SCROBBLE, BN_CLICKED, OnChanged)
        COMMAND_HANDLER_EX(IDC_FORMAT, CBN_SELCHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_BITRATE, CBN_SELCHANGE, OnChanged)
    END_MSG_MAP()

private:
    enum { IDC_USER=1002, IDC_PASS=1003,
           IDC_STATUS=1005, IDC_HEADERS=1006, IDC_SCROBBLE=1007,
           IDC_FORMAT=1008, IDC_BITRATE=1009, IDC_PROFILE=1010,
           IDC_ROUTE_STATUS=1011, IDC_NAME=1012, IDC_AUTO_FAILOVER=1013,
           IDC_ADD_PROFILE=1014, IDC_DELETE_PROFILE=1015,
           IDC_ADD_ROUTE=1016, IDC_DELETE_ROUTE=1017,
           IDC_MOVE_ROUTE_UP=1018, IDC_MOVE_ROUTE_DOWN=1019,
           IDC_ROUTE_SCROLL=1020,
           IDC_ROUTE_RADIO_BASE=1100, IDC_ROUTE_RADIO_LAST=1103,
           IDC_ROUTE_EDIT_BASE=1120, IDC_ROUTE_EDIT_LAST=1123,
           IDC_ROUTE_TEST_BASE=1140, IDC_ROUTE_TEST_LAST=1143 };

    struct TestResultPayload {
        std::uint64_t requestId = 0;
        std::string profileId;
        std::string routeId;
        std::size_t routeIndex = 0;
        bool ok = false;
        std::string error;
    };
    struct FormatOption {
        const wchar_t* label;
        const char* value;
    };
    static const FormatOption* formatOptions(std::size_t& count) {
        static const FormatOption options[] = {
            {navidrome::l10n::serverDefault, ""},
            {navidrome::l10n::originalFormat, "raw"},
            {L"MP3", "mp3"}, {L"Opus", "opus"}, {L"AAC", "aac"},
            {L"FLAC", "flac"}, {L"WAV", "wav"},
        };
        count = sizeof(options) / sizeof(options[0]);
        return options;
    }
    static const int* bitrateOptions(std::size_t& count) {
        static const int options[] = {0, 64, 96, 128, 192, 256, 320};
        count = sizeof(options) / sizeof(options[0]);
        return options;
    }
    // Posted from the background ping thread back to the UI thread (see OnTest).
    static constexpr UINT WM_TEST_RESULT = WM_USER + 200;
    static constexpr UINT WM_ROUTE_CHANGED = WM_USER + 201;
    static constexpr std::size_t kVisibleRouteRows = 4;

    LRESULT OnCreate(LPCREATESTRUCT) {
        m_dispatchState = std::make_shared<NavidromePrefsDispatchState>();
        m_dispatchState->hwnd = *this;
        m_dispatchState->alive = true;
        HFONT f = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto lbl = [&](const wchar_t* t, int x, int y, int w, int h) {
            HWND h2 = CreateWindowW(L"STATIC", t, WS_CHILD|WS_VISIBLE, x,y,w,h, *this, nullptr, nullptr, nullptr);
            SendMessageW(h2, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);
        };
        auto edit = [&](int id, int x, int y, int w, int h, bool pass=false) {
            DWORD sty = WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL|(pass?ES_PASSWORD:0);
            HWND h2 = CreateWindowW(L"EDIT", L"", sty, x,y,w,h, *this, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
            SendMessageW(h2, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);
        };
        constexpr int labelW = 100, editX = 112, editW = 270;
        lbl(navidrome::l10n::serverProfile, 8, 14, labelW, 18);
        m_profiles.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,
            0, IDC_PROFILE);
        m_profiles.SetWindowPos(nullptr, editX, 10, 190, 180, SWP_NOZORDER);
        m_profiles.SetFont(f);

        auto button = [&](int id, const wchar_t* text, int x, int y, int w) {
            HWND handle = CreateWindowW(L"BUTTON", text,
                WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, x, y, w, 24, *this,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                nullptr, nullptr);
            SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);
        };
        button(IDC_ADD_PROFILE, navidrome::l10n::addProfile, 308, 9, 58);
        button(IDC_DELETE_PROFILE, navidrome::l10n::deleteProfile, 370, 9, 58);

        lbl(navidrome::l10n::profileName, 8, 44, labelW, 18);
        edit(IDC_NAME, editX, 40, editW, 22);

        lbl(navidrome::l10n::serverRoute, 8, 74, labelW, 18);
        auto radio = [&](int id, int y, bool group) {
            DWORD style = WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTORADIOBUTTON;
            if (group) style |= WS_GROUP;
            HWND handle = CreateWindowW(L"BUTTON", L"", style,
                18, y, 82, 22, *this,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                nullptr, nullptr);
            SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);
            return handle;
        };
        for (std::size_t slot = 0; slot < kVisibleRouteRows; ++slot) {
            const int y = 100 + static_cast<int>(slot) * 30;
            m_routeRadios[slot] = radio(
                IDC_ROUTE_RADIO_BASE + static_cast<int>(slot), y,
                slot == 0);
            edit(IDC_ROUTE_EDIT_BASE + static_cast<int>(slot), editX, y,
                 editW, 22);
            m_routeEdits[slot] = GetDlgItem(
                IDC_ROUTE_EDIT_BASE + static_cast<int>(slot));
            button(IDC_ROUTE_TEST_BASE + static_cast<int>(slot),
                   navidrome::l10n::testRoute, 390, y - 1, 52);
            m_routeTests[slot] = GetDlgItem(
                IDC_ROUTE_TEST_BASE + static_cast<int>(slot));
        }
        m_routeScrollbar = CreateWindowW(
            L"SCROLLBAR", nullptr, WS_CHILD|WS_VISIBLE|SBS_VERT,
            446, 100, 16, 112, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ROUTE_SCROLL)),
            nullptr, nullptr);

        button(IDC_ADD_ROUTE, navidrome::l10n::addRoute, editX, 220, 72);
        button(IDC_DELETE_ROUTE, navidrome::l10n::deleteRoute, 188, 220, 72);
        button(IDC_MOVE_ROUTE_UP, navidrome::l10n::moveRouteUp, 264, 220, 50);
        button(IDC_MOVE_ROUTE_DOWN, navidrome::l10n::moveRouteDown, 318, 220, 50);

        HWND autoFailover = CreateWindowW(
            L"BUTTON", navidrome::l10n::automaticFailover,
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, editX, 252, 300, 20,
            *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_AUTO_FAILOVER)),
            nullptr, nullptr);
        SendMessageW(autoFailover, WM_SETFONT,
                     reinterpret_cast<WPARAM>(f), 0);

        HWND routeStatus = CreateWindowW(
            L"STATIC", L"", WS_CHILD|WS_VISIBLE|SS_LEFT,
            editX, 278, 330, 18, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ROUTE_STATUS)),
            nullptr, nullptr);
        SendMessageW(routeStatus, WM_SETFONT,
                     reinterpret_cast<WPARAM>(f), 0);

        lbl(navidrome::l10n::username, 8, 308, labelW, 18);
        edit(IDC_USER, editX, 304, editW, 22);
        lbl(navidrome::l10n::password, 8, 338, labelW, 18);
        edit(IDC_PASS, editX, 334, editW, 22, true);

        HWND st = CreateWindowW(L"STATIC", L"", WS_CHILD|WS_VISIBLE|SS_LEFT,
            editX, 366, 330, 18, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)),
            nullptr, nullptr);
        SendMessageW(st, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);

        button(IDC_HEADERS, navidrome::l10n::customHeaders, editX, 392, 140);

        HWND scr = CreateWindowW(L"BUTTON", navidrome::l10n::reportPlays,
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 8,426, 420,20, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SCROBBLE)), nullptr, nullptr);
        SendMessageW(scr, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);

        lbl(navidrome::l10n::streamFormat, 8, 460, labelW, 18);
        m_format.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,
            0, IDC_FORMAT);
        m_format.SetWindowPos(nullptr, editX, 456, 190, 180, SWP_NOZORDER);
        m_format.SetFont(f);
        std::size_t formatCount = 0;
        const auto* formats = formatOptions(formatCount);
        for (std::size_t index = 0; index < formatCount; ++index)
            m_format.AddString(formats[index].label);

        lbl(navidrome::l10n::maxBitrate, 8, 490, labelW, 18);
        m_bitrate.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,
            0, IDC_BITRATE);
        m_bitrate.SetWindowPos(nullptr, editX, 486, 190, 180, SWP_NOZORDER);
        m_bitrate.SetFont(f);
        std::size_t bitrateCount = 0;
        const auto* bitrates = bitrateOptions(bitrateCount);
        for (std::size_t index = 0; index < bitrateCount; ++index) {
            const auto label = bitrates[index] == 0
                ? std::wstring(navidrome::l10n::unlimitedBitrate)
                : std::to_wstring(bitrates[index]) + L" kbps";
            m_bitrate.AddString(label.c_str());
        }

        auto dispatch = m_dispatchState;
        m_connectionSubscription =
            navidrome::ServerConnectionHub::get().subscribe(
                [dispatch](const navidrome::ServerConnectionEvent& event) {
                    if (!dispatch || !dispatch->alive ||
                        !::IsWindow(dispatch->hwnd)) return;
                    dispatch->lastRouteReason = event.reason;
                    dispatch->automaticRouteChange = event.automatic;
                    ::PostMessageW(dispatch->hwnd, WM_ROUTE_CHANGED, 0, 0);
                });
        loadSettings();
        return 0;
    }

    void OnDestroy() {
        ++m_testRequestId;
        m_connectionSubscription.reset();
        if (m_dispatchState) {
            m_dispatchState->alive = false;
            m_dispatchState->hwnd = nullptr;
        }
        m_dispatchState.reset();
    }

    void OnHeaders(UINT, int, HWND) { NavidromeHeadersWindow::get().show(); }

    static const wchar_t* profileErrorText(navidrome::ServerProfileError error) {
        using Error = navidrome::ServerProfileError;
        switch (error) {
        case Error::EmptyName:
        case Error::UntrimmedName:
            return navidrome::l10n::profileNameRequired;
        case Error::DuplicateName:
        case Error::DuplicateId:
            return navidrome::l10n::profileNameDuplicate;
        case Error::MissingRouteUrl:
            return navidrome::l10n::profileUrlRequired;
        case Error::MissingCredentials:
            return navidrome::l10n::profileCredentialsRequired;
        case Error::CannotDeleteLastProfile:
            return navidrome::l10n::profileLastDeleteBlocked;
        case Error::TooManyRoutes:
            return navidrome::l10n::routeLimitReached;
        case Error::CannotDeleteLastRoute:
            return navidrome::l10n::routeLastDeleteBlocked;
        default:
            return navidrome::l10n::profilePersistenceFailed;
        }
    }

    void showProfileError(navidrome::ServerProfileError error) {
        const auto* text = profileErrorText(error);
        SetDlgItemText(IDC_STATUS, text);
        MessageBoxW(text, navidrome::l10n::profileInvalid,
                    MB_OK | MB_ICONWARNING);
    }

    std::string controlText(int id) const {
        const int length = ::GetWindowTextLengthW(GetDlgItem(id));
        std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
        GetDlgItemText(id, &value[0], length + 1);
        value.resize(static_cast<std::size_t>(length));
        return wToU8(value);
    }

    const navidrome::ServerProfileState& formState() const noexcept {
        return m_resetState ? *m_resetState : m_storedState;
    }

    const navidrome::ServerProfile* formProfile(
            const std::string& profileId) const noexcept {
        const auto& state = formState();
        const auto it = std::find_if(
            state.profiles.begin(), state.profiles.end(),
            [&](const navidrome::ServerProfile& profile) {
                return profile.id == profileId;
            });
        return it == state.profiles.end() ? nullptr : &*it;
    }

    static const wchar_t* routeLabel(std::size_t index) noexcept {
        static const wchar_t* labels[] = {
            navidrome::l10n::routeOne, navidrome::l10n::routeTwo,
            navidrome::l10n::routeThree, navidrome::l10n::routeFour,
            navidrome::l10n::routeFive, navidrome::l10n::routeSix,
            navidrome::l10n::routeSeven, navidrome::l10n::routeEight,
        };
        return index < std::size(labels) ? labels[index] : L"线路";
    }

    static std::size_t routeIndexById(
            const navidrome::ServerProfile& profile,
            const std::string& routeId) noexcept {
        const auto route = std::find_if(
            profile.routes.begin(), profile.routes.end(),
            [&](const navidrome::ServerRouteEntry& candidate) {
                return candidate.id == routeId;
            });
        return route == profile.routes.end()
            ? profile.routes.size()
            : static_cast<std::size_t>(
                  std::distance(profile.routes.begin(), route));
    }

    navidrome::ServerProfile formDraftBase() const {
        if (m_adding) return m_newProfileDraft;
        const std::string& profileId = formState().activeProfileId;
        const auto draft = m_drafts.find(profileId);
        if (draft != m_drafts.end()) return draft->second;
        const auto* stored = formProfile(profileId);
        return stored ? *stored : navidrome::ServerProfile{};
    }

    navidrome::ServerProfile draftProfile() const {
        auto draft = navidrome::canonicalizeServerProfile(formDraftBase());
        draft.id = m_adding ? m_newProfileId : formState().activeProfileId;
        draft.name = controlText(IDC_NAME);
        draft.username = controlText(IDC_USER);
        draft.password = controlText(IDC_PASS);
        draft.autoFailover =
            IsDlgButtonChecked(IDC_AUTO_FAILOVER) == BST_CHECKED;
        for (std::size_t slot = 0; slot < kVisibleRouteRows; ++slot) {
            const std::size_t index = m_routeScrollOffset + slot;
            if (index >= draft.routes.size()) continue;
            draft.routes[index].url = controlText(
                IDC_ROUTE_EDIT_BASE + static_cast<int>(slot));
            if (IsDlgButtonChecked(
                    IDC_ROUTE_RADIO_BASE + static_cast<int>(slot)) ==
                BST_CHECKED) {
                draft.preferredRouteId = draft.routes[index].id;
            }
        }
        return navidrome::canonicalizeServerProfile(std::move(draft));
    }

    void renderRouteRows(const navidrome::ServerProfile& source) {
        const auto profile = navidrome::canonicalizeServerProfile(source);
        if (profile.routes.size() <= kVisibleRouteRows) {
            m_routeScrollOffset = 0;
        } else {
            m_routeScrollOffset = (std::min)(
                m_routeScrollOffset,
                profile.routes.size() - kVisibleRouteRows);
        }
        const bool wasLoading = m_loading;
        m_loading = true;
        for (std::size_t slot = 0; slot < kVisibleRouteRows; ++slot) {
            const std::size_t index = m_routeScrollOffset + slot;
            const bool visible = index < profile.routes.size();
            for (HWND handle : {m_routeRadios[slot], m_routeEdits[slot],
                                m_routeTests[slot]}) {
                ::ShowWindow(handle, visible ? SW_SHOW : SW_HIDE);
            }
            if (!visible) continue;
            ::SetWindowTextW(m_routeRadios[slot], routeLabel(index));
            ::SetWindowTextW(m_routeEdits[slot],
                u8ToWide(profile.routes[index].url).c_str());
            CheckDlgButton(
                IDC_ROUTE_RADIO_BASE + static_cast<int>(slot),
                profile.routes[index].id == profile.preferredRouteId
                    ? BST_CHECKED : BST_UNCHECKED);
        }
        SCROLLINFO scroll = {};
        scroll.cbSize = sizeof(scroll);
        scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        scroll.nMin = 0;
        scroll.nMax = static_cast<int>(profile.routes.size()) - 1;
        scroll.nPage = static_cast<UINT>(kVisibleRouteRows);
        scroll.nPos = static_cast<int>(m_routeScrollOffset);
        ::SetScrollInfo(m_routeScrollbar, SB_CTL, &scroll, TRUE);
        ::EnableWindow(m_routeScrollbar,
                       profile.routes.size() > kVisibleRouteRows);
        m_loading = wasLoading;
    }

    void loadProfileControls(const navidrome::ServerProfile& source) {
        const auto profile = navidrome::canonicalizeServerProfile(source);
        m_loading = true;
        SetDlgItemText(IDC_NAME, u8ToWide(profile.name).c_str());
        SetDlgItemText(IDC_USER, u8ToWide(profile.username).c_str());
        SetDlgItemText(IDC_PASS, u8ToWide(profile.password).c_str());
        CheckDlgButton(IDC_AUTO_FAILOVER,
                       profile.autoFailover ? BST_CHECKED : BST_UNCHECKED);
        m_selectedRouteIndex = routeIndexById(
            profile, profile.preferredRouteId);
        if (m_selectedRouteIndex >= profile.routes.size())
            m_selectedRouteIndex = 0;
        m_routeScrollOffset = m_selectedRouteIndex >= kVisibleRouteRows
            ? m_selectedRouteIndex - kVisibleRouteRows + 1 : 0;
        renderRouteRows(profile);
        m_loading = false;
        updateRuntimeRouteStatus();
    }

    void rebuildProfileList(const std::string& selectedId) {
        const auto& state = formState();
        m_loading = true;
        m_profiles.ResetContent();
        int selectedIndex = 0;
        for (std::size_t index = 0; index < state.profiles.size();
             ++index) {
            m_profiles.AddString(
                u8ToWide(state.profiles[index].name).c_str());
            if (state.profiles[index].id == selectedId)
                selectedIndex = static_cast<int>(index);
        }
        m_profiles.SetCurSel(selectedIndex);
        m_loading = false;
    }

    static bool sameProfile(const navidrome::ServerProfile& left,
                            const navidrome::ServerProfile& right) {
        const auto canonicalLeft =
            navidrome::canonicalizeServerProfile(left);
        const auto canonicalRight =
            navidrome::canonicalizeServerProfile(right);
        return canonicalLeft.id == canonicalRight.id &&
               canonicalLeft.name == canonicalRight.name &&
               canonicalLeft.routes == canonicalRight.routes &&
               canonicalLeft.preferredRouteId ==
                   canonicalRight.preferredRouteId &&
               canonicalLeft.autoFailover == canonicalRight.autoFailover &&
               canonicalLeft.username == canonicalRight.username &&
               canonicalLeft.password == canonicalRight.password;
    }

    static const std::string& routeUrl(
            const navidrome::ServerProfile& profile,
            const std::string& routeId) noexcept {
        static const std::string empty;
        const auto route = std::find_if(
            profile.routes.begin(), profile.routes.end(),
            [&](const navidrome::ServerRouteEntry& candidate) {
                return candidate.id == routeId;
            });
        return route == profile.routes.end() ? empty : route->url;
    }

    static bool connectionDraftDiffers(
            const navidrome::ServerProfile& saved,
            const navidrome::ServerProfile& draft,
            const std::string& routeId) noexcept {
        return routeIndexById(saved, routeId) >= saved.routes.size() ||
               routeUrl(saved, routeId) != routeUrl(draft, routeId) ||
               saved.username != draft.username ||
               saved.password != draft.password;
    }

    void updateChangedState() {
        m_profileChanged = m_resetState.has_value() || !m_drafts.empty();
        m_changed = m_settingsChanged || m_profileChanged || m_adding;
    }

    void storeFormDraft(navidrome::ServerProfile draft) {
        draft = navidrome::canonicalizeServerProfile(std::move(draft));
        if (m_adding) {
            m_newProfileDraft = std::move(draft);
            updateChangedState();
            return;
        }
        if (m_resetState) {
            m_resetState->profiles[0] = std::move(draft);
            updateChangedState();
            return;
        }
        const auto* saved = formProfile(draft.id);
        if (!saved) return;
        if (sameProfile(*saved, draft))
            m_drafts.erase(draft.id);
        else
            m_drafts[draft.id] = std::move(draft);
        updateChangedState();
    }

    void stashCurrentDraft() {
        if (m_loading) return;
        storeFormDraft(draftProfile());
    }

    void syncDraftRoute(const std::string& profileId,
                        const std::string& routeId) {
        const auto draft = m_drafts.find(profileId);
        if (draft == m_drafts.end()) return;
        draft->second.preferredRouteId = routeId;
        draft->second = navidrome::canonicalizeServerProfile(
            std::move(draft->second));
        const auto* saved = formProfile(profileId);
        if (saved && sameProfile(draft->second, *saved))
            m_drafts.erase(draft);
    }

    void leaveAddMode() {
        m_adding = false;
        m_newProfileId.clear();
        m_newProfileDraft = {};
        m_routeScrollOffset = 0;
        m_selectedRouteIndex = 0;
        ::EnableWindow(GetDlgItem(IDC_PROFILE), TRUE);
        ::EnableWindow(GetDlgItem(IDC_ADD_PROFILE), TRUE);
        ::EnableWindow(GetDlgItem(IDC_DELETE_PROFILE), TRUE);
        SetDlgItemText(IDC_ADD_PROFILE, navidrome::l10n::addProfile);
        SetDlgItemText(IDC_DELETE_PROFILE,
                       navidrome::l10n::deleteProfile);
    }

    void reloadStoredProfile() {
        const auto& state = formState();
        const auto* active = formProfile(state.activeProfileId);
        if (!active) return;
        rebuildProfileList(active->id);
        const auto draft = m_drafts.find(active->id);
        loadProfileControls(draft == m_drafts.end() ? *active
                                                    : draft->second);
        SetDlgItemText(IDC_STATUS, L"");
    }

    void updateRuntimeRouteStatus() {
        const auto draft = draftProfile();
        const auto runtime =
            navidrome::ServerProfileConfig::get().runtimeSnapshot();
        const std::size_t preferred = routeIndexById(
            draft, draft.preferredRouteId);
        const std::size_t effective = runtime.profileId == draft.id
            ? routeIndexById(draft, runtime.effectiveRouteId)
            : draft.routes.size();
        std::wstring text = L"首选：";
        text += routeLabel(preferred < draft.routes.size() ? preferred : 0);
        text += L"；当前：";
        text += routeLabel(effective < draft.routes.size()
                               ? effective
                               : (preferred < draft.routes.size() ? preferred
                                                                 : 0));
        if (m_dispatchState && m_dispatchState->automaticRouteChange &&
            !m_dispatchState->lastRouteReason.empty()) {
            text += L"；";
            text += navidrome::l10n::automaticSwitchReason;
        }
        SetDlgItemText(IDC_ROUTE_STATUS, text.c_str());
    }

    LRESULT OnRuntimeRouteChanged(UINT, WPARAM, LPARAM) {
        updateRuntimeRouteStatus();
        return 0;
    }

    LRESULT OnRouteScroll(UINT, WPARAM wParam, LPARAM lParam) {
        if (reinterpret_cast<HWND>(lParam) != m_routeScrollbar)
            return 0;
        stashCurrentDraft();
        const auto draft = draftProfile();
        const std::size_t maximum = draft.routes.size() > kVisibleRouteRows
            ? draft.routes.size() - kVisibleRouteRows : 0;
        std::size_t position = m_routeScrollOffset;
        switch (LOWORD(wParam)) {
        case SB_LINEUP:
            if (position > 0) --position;
            break;
        case SB_LINEDOWN:
            if (position < maximum) ++position;
            break;
        case SB_PAGEUP:
            position = position > kVisibleRouteRows
                ? position - kVisibleRouteRows : 0;
            break;
        case SB_PAGEDOWN:
            position = (std::min)(maximum,
                                  position + kVisibleRouteRows);
            break;
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK:
            position = (std::min)(maximum,
                static_cast<std::size_t>(HIWORD(wParam)));
            break;
        default:
            return 0;
        }
        m_routeScrollOffset = position;
        renderRouteRows(draft);
        return 0;
    }

    void OnRouteEdit(UINT code, int id, HWND) {
        if (m_loading) return;
        const std::size_t slot = static_cast<std::size_t>(
            id - IDC_ROUTE_EDIT_BASE);
        const auto draft = draftProfile();
        const std::size_t index = m_routeScrollOffset + slot;
        if (index >= draft.routes.size()) return;
        if (code == EN_SETFOCUS) {
            m_selectedRouteIndex = index;
            return;
        }
        if (code == EN_CHANGE) OnProfileChanged(0, 0, nullptr);
    }

    void OnRoutePreferred(UINT, int id, HWND) {
        if (m_loading) return;
        stashCurrentDraft();
        const auto draft = draftProfile();
        const std::size_t index = m_routeScrollOffset +
            static_cast<std::size_t>(id - IDC_ROUTE_RADIO_BASE);
        if (index >= draft.routes.size()) return;
        m_selectedRouteIndex = index;
        activateRoute(draft.routes[index].id);
    }

    void OnRouteTest(UINT, int id, HWND) {
        if (m_loading) return;
        stashCurrentDraft();
        const auto draft = draftProfile();
        const std::size_t index = m_routeScrollOffset +
            static_cast<std::size_t>(id - IDC_ROUTE_TEST_BASE);
        if (index >= draft.routes.size()) return;
        m_selectedRouteIndex = index;
        testRoute(index);
    }

    void OnAddRoute(UINT, int, HWND) {
        ++m_testRequestId;
        stashCurrentDraft();
        auto draft = draftProfile();
        if (draft.routes.size() >= navidrome::kMaxServerRoutesPerProfile) {
            showProfileError(navidrome::ServerProfileError::TooManyRoutes);
            return;
        }
        const std::string routeId =
            navidrome::ServerProfileConfig::generateProfileId();
        if (routeId.empty()) {
            showProfileError(navidrome::ServerProfileError::AllocationFailure);
            return;
        }
        draft.routes.push_back({routeId, {}});
        storeFormDraft(draft);
        m_selectedRouteIndex = draft.routes.size() - 1;
        m_routeScrollOffset = draft.routes.size() > kVisibleRouteRows
            ? draft.routes.size() - kVisibleRouteRows : 0;
        renderRouteRows(draft);
        updateRuntimeRouteStatus();
        notifyCb();
    }

    void OnDeleteRoute(UINT, int, HWND) {
        ++m_testRequestId;
        stashCurrentDraft();
        auto draft = draftProfile();
        if (draft.routes.size() <= 1) {
            showProfileError(
                navidrome::ServerProfileError::CannotDeleteLastRoute);
            return;
        }
        m_selectedRouteIndex = (std::min)(
            m_selectedRouteIndex, draft.routes.size() - 1);
        const std::string routeId =
            draft.routes[m_selectedRouteIndex].id;
        const bool preferred = draft.preferredRouteId == routeId;
        draft.routes.erase(draft.routes.begin() +
                           static_cast<std::ptrdiff_t>(m_selectedRouteIndex));
        m_selectedRouteIndex = (std::min)(m_selectedRouteIndex,
                                          draft.routes.size() - 1);
        if (preferred)
            draft.preferredRouteId = draft.routes[m_selectedRouteIndex].id;
        draft = navidrome::canonicalizeServerProfile(std::move(draft));
        storeFormDraft(draft);
        renderRouteRows(draft);
        updateRuntimeRouteStatus();
        notifyCb();
    }

    void moveSelectedRoute(int direction) {
        ++m_testRequestId;
        stashCurrentDraft();
        auto draft = draftProfile();
        if (draft.routes.empty()) return;
        m_selectedRouteIndex = (std::min)(
            m_selectedRouteIndex, draft.routes.size() - 1);
        const auto target = static_cast<std::ptrdiff_t>(m_selectedRouteIndex) +
            direction;
        if (target < 0 || target >= static_cast<std::ptrdiff_t>(
                                    draft.routes.size())) return;
        std::iter_swap(draft.routes.begin() +
                           static_cast<std::ptrdiff_t>(m_selectedRouteIndex),
                       draft.routes.begin() + target);
        m_selectedRouteIndex = static_cast<std::size_t>(target);
        if (m_selectedRouteIndex < m_routeScrollOffset)
            m_routeScrollOffset = m_selectedRouteIndex;
        else if (m_selectedRouteIndex >=
                 m_routeScrollOffset + kVisibleRouteRows)
            m_routeScrollOffset = m_selectedRouteIndex -
                kVisibleRouteRows + 1;
        storeFormDraft(draft);
        renderRouteRows(draft);
        updateRuntimeRouteStatus();
        notifyCb();
    }

    void OnMoveRouteUp(UINT, int, HWND) { moveSelectedRoute(-1); }
    void OnMoveRouteDown(UINT, int, HWND) { moveSelectedRoute(1); }

    void loadSettings() {
        auto& profileConfig = navidrome::ServerProfileConfig::get();
        profileConfig.initialize();
        m_storedState = profileConfig.state();
        m_drafts.clear();
        m_resetState.reset();
        leaveAddMode();
        reloadStoredProfile();
        m_loading = true;
        CheckDlgButton(IDC_SCROBBLE,
            navidrome::cfg_scrobble.get() ? BST_CHECKED : BST_UNCHECKED);
        const std::string configuredFormat = navidrome::cfg_stream_format.get().c_str();
        std::size_t formatCount = 0;
        const auto* formats = formatOptions(formatCount);
        int formatIndex = 0;
        for (std::size_t index = 0; index < formatCount; ++index) {
            if (configuredFormat == formats[index].value) {
                formatIndex = static_cast<int>(index);
                break;
            }
        }
        m_format.SetCurSel(formatIndex);
        const int configuredBitrate = static_cast<int>(navidrome::cfg_max_bitrate.get());
        std::size_t bitrateCount = 0;
        const auto* bitrates = bitrateOptions(bitrateCount);
        int bitrateIndex = 0;
        for (std::size_t index = 0; index < bitrateCount; ++index) {
            if (configuredBitrate == bitrates[index]) {
                bitrateIndex = static_cast<int>(index);
                break;
            }
        }
        m_bitrate.SetCurSel(bitrateIndex);
        m_loading = false;
        m_profileChanged = false;
        m_settingsChanged = false;
        m_changed = false;
        if (profileConfig.recoveredFromInvalidDocument())
            SetDlgItemText(IDC_STATUS,
                           navidrome::l10n::profileRecoveredWarning);
    }

    bool saveSettings() {
        if (m_adding || m_profileChanged ||
            navidrome::ServerProfileConfig::get()
                .recoveredFromInvalidDocument()) {
            stashCurrentDraft();
            navidrome::ServerProfileState candidate =
                m_resetState ? *m_resetState : m_storedState;
            if (m_resetState) {
                candidate.profiles[0] = draftProfile();
                candidate.profiles[0].name =
                    navidrome::trimServerProfileName(
                        candidate.profiles[0].name);
            } else {
                for (auto& profile : candidate.profiles) {
                    const auto draft = m_drafts.find(profile.id);
                    if (draft == m_drafts.end()) continue;
                    profile = draft->second;
                    profile.name =
                        navidrome::trimServerProfileName(profile.name);
                }
            }

            const auto validation =
                navidrome::validateServerProfileState(candidate);
            if (!validation) {
                showProfileError(validation.error);
                return false;
            }
            if (m_adding) {
                const auto added = navidrome::addServerProfile(
                    candidate, draftProfile());
                if (!added) {
                    showProfileError(added.error);
                    return false;
                }
                candidate = added.value;
            }
            const auto projection =
                navidrome::projectActiveConnection(candidate);
            if (!projection) {
                showProfileError(projection.error);
                return false;
            }
            const auto committed =
                navidrome::ServerProfileConfig::get().commitState(
                    candidate);
            if (!committed) {
                showProfileError(committed.error);
                return false;
            }
            m_storedState = navidrome::ServerProfileConfig::get().state();
            m_drafts.clear();
            m_resetState.reset();
            leaveAddMode();
            reloadStoredProfile();
        }
        navidrome::cfg_scrobble.set(
            IsDlgButtonChecked(IDC_SCROBBLE) == BST_CHECKED);
        std::size_t formatCount = 0;
        const auto* formats = formatOptions(formatCount);
        const int formatIndex = m_format.GetCurSel();
        if (formatIndex >= 0 && static_cast<std::size_t>(formatIndex) < formatCount)
            navidrome::cfg_stream_format.set(formats[formatIndex].value);
        std::size_t bitrateCount = 0;
        const auto* bitrates = bitrateOptions(bitrateCount);
        const int bitrateIndex = m_bitrate.GetCurSel();
        if (bitrateIndex >= 0 && static_cast<std::size_t>(bitrateIndex) < bitrateCount)
            navidrome::cfg_max_bitrate.set(bitrates[bitrateIndex]);
        return true;
    }

    void OnChanged(UINT, int, HWND) {
        if (m_loading) return;
        m_settingsChanged = true;
        m_changed = true;
        notifyCb();
    }

    void OnProfileChanged(UINT, int, HWND) {
        if (m_loading) return;
        ++m_testRequestId;
        stashCurrentDraft();
        updateChangedState();
        notifyCb();
    }

    void OnProfileSelected(UINT, int, HWND) {
        if (m_loading || m_adding) return;
        if (m_resetState) return;
        stashCurrentDraft();
        const int index = m_profiles.GetCurSel();
        if (index < 0 || static_cast<std::size_t>(index) >=
                             m_storedState.profiles.size()) {
            return;
        }
        const std::string profileId =
            m_storedState.profiles[static_cast<std::size_t>(index)].id;
        const auto status =
            navidrome::ServerProfileConfig::get().selectProfile(profileId);
        if (!status) {
            showProfileError(status.error);
            const auto* saved = formProfile(m_storedState.activeProfileId);
            if (saved) syncDraftRoute(
                saved->id,
                navidrome::canonicalizeServerProfile(*saved)
                    .preferredRouteId);
            reloadStoredProfile();
            return;
        }
        ++m_testRequestId;
        m_storedState = navidrome::ServerProfileConfig::get().state();
        updateChangedState();
        reloadStoredProfile();
        notifyCb();
    }

    bool commitCurrentDraftAndRoute(const std::string& routeId) {
        const auto draftIt = m_drafts.find(m_storedState.activeProfileId);
        if (draftIt == m_drafts.end()) return false;

        auto candidate = m_storedState;
        const auto profileIt = std::find_if(
            candidate.profiles.begin(), candidate.profiles.end(),
            [&](const navidrome::ServerProfile& profile) {
                return profile.id == candidate.activeProfileId;
            });
        if (profileIt == candidate.profiles.end()) return false;
        *profileIt = draftIt->second;
        profileIt->preferredRouteId = routeId;
        *profileIt = navidrome::canonicalizeServerProfile(
            std::move(*profileIt));
        profileIt->name = navidrome::trimServerProfileName(profileIt->name);

        const auto validation = navidrome::validateServerProfileState(candidate);
        if (!validation) {
            showProfileError(validation.error);
            return false;
        }
        const auto projection = navidrome::projectActiveConnection(candidate);
        if (!projection) {
            showProfileError(projection.error);
            return false;
        }
        const auto committed = navidrome::ServerProfileConfig::get()
            .commitState(candidate);
        if (!committed) {
            showProfileError(committed.error);
            return false;
        }
        m_storedState = navidrome::ServerProfileConfig::get().state();
        m_drafts.erase(draftIt);
        return true;
    }

    void activateRoute(const std::string& routeId) {
        if (m_loading) return;
        ++m_testRequestId;
        if (m_adding || m_resetState) {
            OnProfileChanged(0, 0, nullptr);
            return;
        }

        stashCurrentDraft();
        const auto* stored = formProfile(m_storedState.activeProfileId);
        if (!stored) return;
        const auto saved = navidrome::canonicalizeServerProfile(*stored);
        if (saved.preferredRouteId == routeId) {
            updateChangedState();
            notifyCb();
            return;
        }

        const auto draftIt = m_drafts.find(saved.id);
        if (draftIt != m_drafts.end() &&
            connectionDraftDiffers(saved, draftIt->second, routeId)) {
            const int answer = MessageBoxW(
                navidrome::l10n::saveAndSwitchRoutePrompt,
                navidrome::l10n::saveAndSwitchRouteTitle,
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1);
            if (answer != IDYES) {
                syncDraftRoute(saved.id, saved.preferredRouteId);
                reloadStoredProfile();
                updateChangedState();
                notifyCb();
                return;
            }
            if (!commitCurrentDraftAndRoute(routeId)) {
                syncDraftRoute(saved.id, saved.preferredRouteId);
                reloadStoredProfile();
                return;
            }
        } else {
            const auto status = navidrome::ServerProfileConfig::get()
                .selectRoute(m_storedState.activeProfileId, routeId);
            if (!status) {
                showProfileError(status.error);
                syncDraftRoute(saved.id, saved.preferredRouteId);
                reloadStoredProfile();
                return;
            }
            m_storedState = navidrome::ServerProfileConfig::get().state();
            syncDraftRoute(m_storedState.activeProfileId, routeId);
        }

        updateChangedState();
        reloadStoredProfile();
        notifyCb();
    }

    void OnAddProfile(UINT, int, HWND) {
        ++m_testRequestId;
        if (m_adding) {
            leaveAddMode();
            updateChangedState();
            reloadStoredProfile();
            notifyCb();
            return;
        }
        if (m_resetState) return;
        stashCurrentDraft();

        m_newProfileId = navidrome::ServerProfileConfig::generateProfileId();
        if (m_newProfileId.empty()) {
            showProfileError(navidrome::ServerProfileError::AllocationFailure);
            return;
        }
        const std::string routeId =
            navidrome::ServerProfileConfig::generateProfileId();
        if (routeId.empty()) {
            showProfileError(navidrome::ServerProfileError::AllocationFailure);
            return;
        }
        m_adding = true;
        m_newProfileDraft = {};
        m_newProfileDraft.id = m_newProfileId;
        m_newProfileDraft.routes.push_back({routeId, {}});
        m_newProfileDraft.preferredRouteId = routeId;
        m_newProfileDraft.autoFailover = true;
        m_newProfileDraft = navidrome::canonicalizeServerProfile(
            std::move(m_newProfileDraft));
        ::EnableWindow(GetDlgItem(IDC_PROFILE), FALSE);
        ::EnableWindow(GetDlgItem(IDC_DELETE_PROFILE), TRUE);
        SetDlgItemText(IDC_ADD_PROFILE,
                       navidrome::l10n::cancelAddProfile);
        SetDlgItemText(IDC_DELETE_PROFILE, navidrome::l10n::saveProfile);
        loadProfileControls(m_newProfileDraft);
        SetDlgItemText(IDC_STATUS, navidrome::l10n::newProfileHint);
        updateChangedState();
        notifyCb();
    }

    bool saveNewProfileImmediately() {
        const auto added = navidrome::addServerProfile(
            m_storedState, draftProfile());
        if (!added) {
            showProfileError(added.error);
            return false;
        }
        const auto status =
            navidrome::ServerProfileConfig::get().commitState(added.value);
        if (!status) {
            showProfileError(status.error);
            return false;
        }
        ++m_testRequestId;
        m_storedState = navidrome::ServerProfileConfig::get().state();
        leaveAddMode();
        reloadStoredProfile();
        updateChangedState();
        SetDlgItemText(IDC_STATUS, navidrome::l10n::profileSaved);
        notifyCb();
        return true;
    }

    void OnDeleteProfile(UINT, int, HWND) {
        if (m_adding) {
            saveNewProfileImmediately();
            return;
        }
        if (m_resetState) return;
        if (MessageBoxW(navidrome::l10n::deleteProfileConfirm,
                        navidrome::l10n::deleteProfileTitle,
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
            return;
        }
        const auto deleted = navidrome::deleteServerProfile(
            m_storedState, m_storedState.activeProfileId);
        if (!deleted) {
            showProfileError(deleted.error);
            return;
        }
        const auto status =
            navidrome::ServerProfileConfig::get().commitState(deleted.value);
        if (!status) {
            showProfileError(status.error);
            return;
        }
        m_drafts.erase(m_storedState.activeProfileId);
        ++m_testRequestId;
        m_storedState = navidrome::ServerProfileConfig::get().state();
        const auto* active = formProfile(m_storedState.activeProfileId);
        if (active) {
            syncDraftRoute(
                m_storedState.activeProfileId,
                navidrome::canonicalizeServerProfile(*active)
                    .preferredRouteId);
        }
        updateChangedState();
        reloadStoredProfile();
        notifyCb();
    }
    void notifyCb() { if (m_cb.is_valid()) m_cb->on_state_changed(); }

    static std::wstring routePrefix(std::size_t routeIndex) {
        std::wstring prefix(routeLabel(routeIndex));
        prefix += L"：";
        return prefix;
    }

    void testRoute(std::size_t routeIndex) {
        const auto draft = draftProfile();
        if (routeIndex >= draft.routes.size()) return;
        const auto& route = draft.routes[routeIndex];
        const std::string& url = route.url;
        if (url.empty()) {
            const auto* message = navidrome::l10n::routeUrlTestRequired;
            SetDlgItemText(IDC_STATUS, message);
            MessageBoxW(message, navidrome::l10n::profileInvalid,
                        MB_OK | MB_ICONWARNING);
            return;
        }
        auto context = navidrome::SubsonicClientWin::get().snapshot();
        context.serverUrl = url;
        context.username = draft.username;
        context.password = draft.password;
        context.autoFailover = false;
        context.routeCandidates.clear();
        const std::uint64_t requestId = ++m_testRequestId;
        auto dispatch = m_dispatchState;
        std::wstring testingText(routePrefix(routeIndex));
        testingText += navidrome::l10n::testing;
        SetDlgItemText(IDC_STATUS, testingText.c_str());
        std::thread([dispatch, requestId, profileId = draft.id,
                     routeId = route.id, routeIndex,
                     context = std::move(context)]() {
            auto payload = std::make_shared<TestResultPayload>();
            payload->requestId = requestId;
            payload->profileId = profileId;
            payload->routeId = routeId;
            payload->routeIndex = routeIndex;
            payload->ok = navidrome::SubsonicClientWin::get().ping(
                context, payload->error);
            fb2k::inMainThread([dispatch, payload]() {
                if (!dispatch || !dispatch->alive ||
                    !::IsWindow(dispatch->hwnd)) return;
                ::SendMessageW(dispatch->hwnd, WM_TEST_RESULT, 0,
                               reinterpret_cast<LPARAM>(payload.get()));
            });
        }).detach();
    }

    LRESULT OnTestResult(UINT, WPARAM, LPARAM lParam) {
        const auto* payload = reinterpret_cast<const TestResultPayload*>(lParam);
        if (!payload || payload->requestId != m_testRequestId)
            return 0;
        const auto draft = draftProfile();
        if (payload->profileId != draft.id) return 0;
        if (payload->routeIndex >= draft.routes.size() ||
            draft.routes[payload->routeIndex].id != payload->routeId) {
            return 0;
        }
        const char* errorText = !payload->error.empty()
            ? payload->error.c_str()
            : navidrome::l10n::failedUtf8;
        std::wstring status(routePrefix(payload->routeIndex));
        if (payload->ok) {
            status += navidrome::l10n::connected;
        } else {
            const pfc::stringcvt::string_wide_from_utf8 error(errorText);
            status += error.get_ptr();
        }
        SetDlgItemText(IDC_STATUS, status.c_str());
        return 0;
    }

    CComboBox m_profiles, m_format, m_bitrate;
    preferences_page_callback::ptr m_cb;
    navidrome::ServerProfileState m_storedState;
    std::map<std::string, navidrome::ServerProfile> m_drafts;
    std::optional<navidrome::ServerProfileState> m_resetState;
    navidrome::ServerConnectionSubscription m_connectionSubscription;
    std::shared_ptr<NavidromePrefsDispatchState> m_dispatchState;
    std::array<HWND, kVisibleRouteRows> m_routeRadios{};
    std::array<HWND, kVisibleRouteRows> m_routeEdits{};
    std::array<HWND, kVisibleRouteRows> m_routeTests{};
    HWND m_routeScrollbar = nullptr;
    navidrome::ServerProfile m_newProfileDraft;
    std::string m_newProfileId;
    std::size_t m_routeScrollOffset = 0;
    std::size_t m_selectedRouteIndex = 0;
    std::uint64_t m_testRequestId = 0;
    bool m_changed = false;
    bool m_loading = false;
    bool m_adding = false;
    bool m_profileChanged = false;
    bool m_settingsChanged = false;
};

class NavidromePrefsPageFactory : public preferences_page_v3 {
public:
    preferences_page_instance::ptr instantiate(HWND parent,
        preferences_page_callback::ptr cb) override {
        auto inst = fb2k::service_new<NavidromePrefsInstance>(cb);
        inst->Create(parent);
        return inst;
    }
    const char* get_name() override { return "Navidrome"; }
    GUID        get_guid() override { return guid_prefs_page; }
    GUID        get_parent_guid() override { return preferences_page::guid_tools; }
};
FB2K_SERVICE_FACTORY(NavidromePrefsPageFactory);

// ---------------------------------------------------------------------------
// Media Library preferences sub-page — makes "Navidrome" appear under
// Preferences > Media Library (parity with the macOS build, which parents a
// page to guid_media_library). The macOS page embeds the browser directly;
// on Windows the browser is a standalone top-level window, so this page just
// hosts an "Open Navidrome Browser" button that surfaces it.
// ---------------------------------------------------------------------------
class NavidromeLibraryPrefsInstance : public CWindowImpl<NavidromeLibraryPrefsInstance>,
                                      public preferences_page_instance {
public:
    DECLARE_WND_CLASS(L"foo_navidrome_LibPrefsWnd")

    explicit NavidromeLibraryPrefsInstance(preferences_page_callback::ptr cb) : m_cb(cb) {}

    // Nothing editable on this page — it's a launcher, so it's never "changed".
    HWND      get_wnd() override { return m_hWnd; }
    t_uint32  get_state() override { return 0; }
    void      apply() override {}
    void      reset() override {}

    BEGIN_MSG_MAP(NavidromeLibraryPrefsInstance)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_SIZE(OnSize)
    END_MSG_MAP()

private:
    // Embed the browser inline, filling the page — parity with the macOS build
    // (which mounts the browser view controller directly in this sub-page). A
    // fresh BrowserWindow instance owned by this page, distinct from the
    // standalone-window singleton used by the File menu / library_viewer.
    LRESULT OnCreate(LPCREATESTRUCT) {
        m_browser.createEmbedded(*this);
        return 0;
    }

    void OnSize(UINT, CSize sz) {
        if (m_browser.IsWindow())
            m_browser.SetWindowPos(nullptr, 0, 0, sz.cx, sz.cy, SWP_NOZORDER);
    }

    BrowserWindow                  m_browser;
    preferences_page_callback::ptr m_cb;
};

class NavidromeLibraryPrefsFactory : public preferences_page_v3 {
public:
    preferences_page_instance::ptr instantiate(HWND parent,
        preferences_page_callback::ptr cb) override {
        auto inst = fb2k::service_new<NavidromeLibraryPrefsInstance>(cb);
        inst->Create(parent);
        return inst;
    }
    const char* get_name() override { return "Navidrome"; }
    // Match macOS guid_library_prefs (…01,0x09) for cross-platform tidiness.
    GUID        get_guid() override {
        return { 0xa1b2c3d4,0x1111,0x2222,{0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x01,0x09} };
    }
    GUID        get_parent_guid() override { return preferences_page::guid_media_library; }
};
FB2K_SERVICE_FACTORY(NavidromeLibraryPrefsFactory);

// ---------------------------------------------------------------------------
// Main menu: File > Open Navidrome Browser
// ---------------------------------------------------------------------------
class NavidromeMenuCmd : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return 1; }
    GUID     get_command(t_uint32 i) override {
        if (i == 0) return guid_mainmenu_cmd;
        throw pfc::exception_invalid_params();
    }
    void get_name(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = navidrome::l10n::menuOpenBrowser; return; }
        throw pfc::exception_invalid_params();
    }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        if (i == 0) { out = navidrome::l10n::menuBrowseDescription; return true; }
        return false;
    }
    GUID     get_parent() override { return mainmenu_groups::file; }
    t_uint32 get_sort_priority() override { return 0xFF; }
    bool     get_display(t_uint32 i, pfc::string_base& out, t_uint32& flags) override {
        get_name(i, out); flags = 0; return true;
    }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        if (i != 0) throw pfc::exception_invalid_params();
        fb2k::inMainThread([] { BrowserWindow::get().show(); });
    }
};
FB2K_SERVICE_FACTORY(NavidromeMenuCmd);

// ---------------------------------------------------------------------------
// Cover art extractor (navidrome:// and legacy /rest/stream.view URLs)
// ---------------------------------------------------------------------------
namespace {
    // Session-deduped console diagnostics for non-not-found cover failures
    std::mutex g_coverDiagMutex;
    std::set<std::pair<navidrome::FetchClass, std::string>> g_coverDiagSeen;

    void logCoverError(navidrome::FetchClass cls, const std::string& id) {
        using namespace navidrome;
        if (cls == FetchClass::NotFound) return; // not-found is silent (normal)

        {
            std::lock_guard<std::mutex> lock(g_coverDiagMutex);
            if (!g_coverDiagSeen.insert({cls, id}).second) return; // already logged
        }

        const char* msg = "";
        switch (cls) {
            case FetchClass::Auth:           msg = u8"封面获取：认证失败"; break;
            case FetchClass::ServerError:    msg = u8"封面获取：服务器错误"; break;
            case FetchClass::Transport:      msg = u8"封面获取：网络传输错误"; break;
            case FetchClass::InvalidContent: msg = u8"封面获取：无效内容"; break;
            default: break;
        }
        if (*msg) {
            console::print(msg);
        }
    }
}

class NavidromeArtInstance : public album_art_extractor_instance_v2 {
public:
    NavidromeArtInstance(const std::string& coverId,
                         const navidrome::SubsonicRequestContext& ctx)
        : m_id(coverId), m_context(ctx) {}

    album_art_data_ptr query(const GUID& what, abort_callback& abort) override {
        if (what != album_art_ids::cover_front) throw exception_album_art_not_found();

        // Check cache first
        auto cached = navidrome::CoverCache::instance().get(
            m_context.serverUrl, m_context.username, m_id);
        if (!cached.empty()) {
            return album_art_data_impl::g_create(cached.data(), cached.size());
        }

        // Fetch from server
        std::string url = navidrome::SubsonicClientWin::get().coverArtURL(
            m_context, m_id, 0);
        static constexpr std::size_t kMaxCoverBytes = 20 * 1024 * 1024; // 20 MB

        auto result = navidrome::SubsonicClientWin::get().httpGetBinary(
            m_context, url, kMaxCoverBytes, abort);

        if (result.cls == navidrome::FetchClass::Aborted) {
            throw exception_aborted();
        }

        if (result.cls != navidrome::FetchClass::Ok) {
            logCoverError(result.cls, m_id);
            throw exception_album_art_not_found();
        }

        // Cache success
        navidrome::CoverCache::instance().put(
            m_context.serverUrl, m_context.username, m_id, result.body);

        return album_art_data_impl::g_create(result.body.data(), result.body.size());
    }

    album_art_path_list::ptr query_paths(const GUID&, abort_callback&) override {
        throw exception_album_art_not_found();
    }

private:
    std::string m_id;
    navidrome::SubsonicRequestContext m_context;
};

class NavidromeArtExtractor : public album_art_extractor {
public:
    bool is_our_path(const char* p, const char*) override {
        if (!p) return false;
        // Match navidrome:// OR legacy /rest/stream.view
        return strncmp(p, "navidrome://", 12) == 0 || strstr(p, "/rest/stream.view") != nullptr;
    }

    album_art_extractor_instance_ptr open(file_ptr, const char* path,
                                          abort_callback&) override {
        std::string id = navidrome::resolveArtId(path);
        if (id.empty()) throw exception_album_art_not_found();

        auto ctx = navidrome::SubsonicClientWin::get().snapshot();
        return fb2k::service_new<NavidromeArtInstance>(id, ctx);
    }
};
FB2K_SERVICE_FACTORY(NavidromeArtExtractor);

// ---------------------------------------------------------------------------
// Playback reporting — callbacks stay on foobar's main thread and only update
// the pure session reducer / enqueue immutable work. Network I/O is owned by a
// single joined worker in ScrobbleCoordinator.
// ---------------------------------------------------------------------------
namespace {

double scrobbleMonotonicSeconds() noexcept {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

navidrome::ScrobbleStopReason mapStopReason(
        play_control::t_stop_reason reason) noexcept {
    switch (reason) {
    case play_control::stop_reason_eof:
        return navidrome::ScrobbleStopReason::EndOfFile;
    case play_control::stop_reason_starting_another:
        return navidrome::ScrobbleStopReason::StartingAnother;
    case play_control::stop_reason_shutting_down:
        return navidrome::ScrobbleStopReason::ShuttingDown;
    default:
        return navidrome::ScrobbleStopReason::User;
    }
}

class NavidromeScrobbleCallback : public play_callback_static {
public:
    unsigned get_flags() override {
        return flag_on_playback_new_track | flag_on_playback_stop |
               flag_on_playback_seek | flag_on_playback_pause |
               flag_on_playback_time;
    }

    void on_playback_new_track(metadb_handle_ptr track) noexcept override {
        try {
            auto& coordinator = navidrome::ScrobbleCoordinator::get();
            coordinator.setEnabled(navidrome::cfg_scrobble.get());

            navidrome::Song song;
            if (track.is_empty() || !navidrome::parseTrackURI(track->get_path(), song)) {
                coordinator.onNewTrack({}, 0.0, {}, {}, scrobbleMonotonicSeconds());
                return;
            }

            auto context = navidrome::SubsonicClientWin::get().snapshot();
            if (context.serverUrl.empty() || context.username.empty() ||
                context.password.empty()) {
                coordinator.onNewTrack({}, 0.0, {}, {}, scrobbleMonotonicSeconds());
                return;
            }
            const double duration = std::isfinite(track->get_length()) &&
                                    track->get_length() > 0.0
                ? track->get_length() : song.duration;
            auto identity = navidrome::serverAccountIdentity(
                context.serverUrl, context.username);
            coordinator.onNewTrack(std::move(song.id), duration,
                std::move(context), std::move(identity), scrobbleMonotonicSeconds());
        } catch (...) {}
    }

    void on_playback_time(double time) noexcept override {
        try {
            navidrome::ScrobbleCoordinator::get().onTime(
                time, scrobbleMonotonicSeconds());
        } catch (...) {}
    }

    void on_playback_seek(double time) noexcept override {
        try {
            navidrome::ScrobbleCoordinator::get().onSeek(
                time, scrobbleMonotonicSeconds());
        } catch (...) {}
    }

    void on_playback_pause(bool state) noexcept override {
        try {
            navidrome::ScrobbleCoordinator::get().onPause(
                state, scrobbleMonotonicSeconds());
        } catch (...) {}
    }

    void on_playback_stop(play_control::t_stop_reason reason) noexcept override {
        try {
            navidrome::ScrobbleCoordinator::get().onStop(
                mapStopReason(reason), scrobbleMonotonicSeconds());
        } catch (...) {}
    }

    void on_playback_starting(play_control::t_track_command, bool) noexcept override {}
    void on_playback_edited(metadb_handle_ptr) noexcept override {}
    void on_playback_dynamic_info(const file_info&) noexcept override {}
    void on_playback_dynamic_info_track(const file_info&) noexcept override {}
    void on_volume_change(float) noexcept override {}
};

play_callback_static_factory_t<NavidromeScrobbleCallback>
    g_navidromeScrobbleCallbackFactory;

} // namespace

// ---------------------------------------------------------------------------
// Init/quit — ESLyric bridge and joined scrobble-worker lifecycle
// ---------------------------------------------------------------------------
class NavidromeInitQuit : public initquit {
public:
    void on_init() override {
        // Profile migration/projection must complete before any subsystem takes
        // an immutable request snapshot.
        navidrome::ServerProfileConfig::get().initialize();
        navidrome::ScrobbleCoordinator::get().setEnabled(
            navidrome::cfg_scrobble.get());
        // Install/update ESLyric bridge on startup
        if (!navidrome::EsLyricBridge::isEsLyricInstalled()) {
            console::print(navidrome::l10n::eslyricBridgeNotInstalled);
            return;
        }
        auto ctx = navidrome::SubsonicClientWin::get().snapshot();
        std::string err = navidrome::EsLyricBridge::installOrUpdate(ctx);

        if (!err.empty()) {
            const auto message = navidrome::l10n::eslyricBridgeError(err);
            console::print(message.c_str());
        }
    }

    void on_quit() override {
        navidrome::ScrobbleCoordinator::get().shutdown();
    }
};
FB2K_SERVICE_FACTORY(NavidromeInitQuit);
