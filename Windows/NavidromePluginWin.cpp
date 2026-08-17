#include "stdafx.h"
#include "BrowserWindow.h"
#include "Localization.h"
#include "SubsonicClientWin.h"
#include "MediaEnrichmentLogic.h"
#include "EsLyricBridge.h"
#include "ScrobbleService.h"
#include "ServerIdentity.h"
#include "TrackUriMetadata.h"
#include <SDK/cfg_var.h>
#include <SDK/album_art.h>
#include <SDK/album_art_helpers.h>
#include <SDK/initquit.h>
#include <SDK/play_callback.h>
#include <chrono>
#include <cmath>
#include <string>
#include <cctype>
#include <set>
#include <mutex>
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
        saveSettings();
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

        m_changed = false;
        notifyCb();
    }
    void reset()  override {
        m_loading = true;
        SetDlgItemText(IDC_URL,  L"http://localhost:4533/");
        SetDlgItemText(IDC_USER, L"");
        SetDlgItemText(IDC_PASS, L"");
        CheckDlgButton(IDC_SCROBBLE, BST_CHECKED);
        m_format.SetCurSel(0);
        m_bitrate.SetCurSel(0);
        m_loading = false;
        m_changed = true; notifyCb();
    }

    BEGIN_MSG_MAP(NavidromePrefsInstance)
        MSG_WM_CREATE(OnCreate)
        MESSAGE_HANDLER_EX(WM_TEST_RESULT, OnTestResult)
        COMMAND_HANDLER_EX(IDC_URL,  EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_USER, EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_PASS, EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_TEST, BN_CLICKED, OnTest)
        COMMAND_HANDLER_EX(IDC_HEADERS, BN_CLICKED, OnHeaders)
        COMMAND_HANDLER_EX(IDC_SCROBBLE, BN_CLICKED, OnChanged)
        COMMAND_HANDLER_EX(IDC_FORMAT, CBN_SELCHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_BITRATE, CBN_SELCHANGE, OnChanged)
    END_MSG_MAP()

private:
    enum { IDC_URL=1001, IDC_USER=1002, IDC_PASS=1003, IDC_TEST=1004,
           IDC_STATUS=1005, IDC_HEADERS=1006, IDC_SCROBBLE=1007,
           IDC_FORMAT=1008, IDC_BITRATE=1009 };
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

    LRESULT OnCreate(LPCREATESTRUCT) {
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
        lbl(navidrome::l10n::serverUrl, 8, 14, labelW, 18); edit(IDC_URL, editX, 10, editW, 22);
        lbl(navidrome::l10n::username, 8, 44, labelW, 18); edit(IDC_USER, editX, 40, editW, 22);
        lbl(navidrome::l10n::password, 8, 74, labelW, 18); edit(IDC_PASS, editX, 70, editW, 22, true);

        HWND btn = CreateWindowW(L"BUTTON", navidrome::l10n::testConnection,
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, editX,100, 120,24, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TEST)), nullptr, nullptr);
        SendMessageW(btn, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);

        HWND st = CreateWindowW(L"STATIC", L"", WS_CHILD|WS_VISIBLE|SS_LEFT, editX,130, editW,18, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)), nullptr, nullptr);
        SendMessageW(st, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);

        HWND hdr = CreateWindowW(L"BUTTON", navidrome::l10n::customHeaders,
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, editX,156, 140,24, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HEADERS)), nullptr, nullptr);
        SendMessageW(hdr, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);

        HWND scr = CreateWindowW(L"BUTTON", navidrome::l10n::reportPlays,
            WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 8,190, 374,20, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SCROBBLE)), nullptr, nullptr);
        SendMessageW(scr, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);

        lbl(navidrome::l10n::streamFormat, 8, 224, labelW, 18);
        m_format.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,
            0, IDC_FORMAT);
        m_format.SetWindowPos(nullptr, editX, 220, 190, 180, SWP_NOZORDER);
        m_format.SetFont(f);
        std::size_t formatCount = 0;
        const auto* formats = formatOptions(formatCount);
        for (std::size_t index = 0; index < formatCount; ++index)
            m_format.AddString(formats[index].label);

        lbl(navidrome::l10n::maxBitrate, 8, 254, labelW, 18);
        m_bitrate.Create(*this, CWindow::rcDefault, nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|CBS_DROPDOWNLIST,
            0, IDC_BITRATE);
        m_bitrate.SetWindowPos(nullptr, editX, 250, 190, 180, SWP_NOZORDER);
        m_bitrate.SetFont(f);
        std::size_t bitrateCount = 0;
        const auto* bitrates = bitrateOptions(bitrateCount);
        for (std::size_t index = 0; index < bitrateCount; ++index) {
            const auto label = bitrates[index] == 0
                ? std::wstring(navidrome::l10n::unlimitedBitrate)
                : std::to_wstring(bitrates[index]) + L" kbps";
            m_bitrate.AddString(label.c_str());
        }

        loadSettings();
        return 0;
    }

    void OnHeaders(UINT, int, HWND) { NavidromeHeadersWindow::get().show(); }

    void loadSettings() {
        m_loading = true;
        SetDlgItemText(IDC_URL,  pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_server_url.get().c_str()));
        SetDlgItemText(IDC_USER, pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_username.get().c_str()));
        SetDlgItemText(IDC_PASS, pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_password.get().c_str()));
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
        m_changed = false;
    }

    void saveCredentials() {
        auto getText = [&](int id) -> std::string {
            wchar_t buf[1024] = {};
            GetDlgItemText(id, buf, 1024);
            return pfc::stringcvt::string_utf8_from_wide(buf).get_ptr();
        };
        navidrome::cfg_server_url.set(getText(IDC_URL).c_str());
        navidrome::cfg_username.set(getText(IDC_USER).c_str());
        navidrome::cfg_password.set(getText(IDC_PASS).c_str());
    }

    void saveSettings() {
        saveCredentials();
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
    }

    void OnChanged(UINT, int, HWND) {
        if (m_loading) return;
        m_changed = true;
        notifyCb();
    }
    void notifyCb() { if (m_cb.is_valid()) m_cb->on_state_changed(); }

    void OnTest(UINT, int, HWND) {
        // Preserve existing test behavior for credentials without applying the
        // staged scrobble checkbox before the user presses Apply.
        saveCredentials();
        SetDlgItemText(IDC_STATUS, navidrome::l10n::testing);
        std::thread([this]() {
            std::string err;
            bool ok = navidrome::SubsonicClientWin::get().ping(err);
            PostMessage(WM_TEST_RESULT, ok ? 1 : 0,
                reinterpret_cast<LPARAM>(ok ? nullptr : new std::string(err)));
        }).detach();
    }

    // Runs on the UI thread; lParam owns a heap std::string with the error text
    // (null on success). Registered via MESSAGE_HANDLER_EX in the message map.
    LRESULT OnTestResult(UINT, WPARAM wParam, LPARAM lParam) {
        bool ok = wParam != 0;
        auto* errStr = reinterpret_cast<std::string*>(lParam);
        const char* errorText = errStr && !errStr->empty()
            ? errStr->c_str()
            : navidrome::l10n::failedUtf8;
        SetDlgItemText(IDC_STATUS, ok ? navidrome::l10n::connected :
            pfc::stringcvt::string_wide_from_utf8(errorText));
        delete errStr;
        return 0;
    }

    CComboBox m_format, m_bitrate;
    preferences_page_callback::ptr m_cb;
    bool m_changed = false;
    bool m_loading = false;
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
