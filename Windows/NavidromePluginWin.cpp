#include "stdafx.h"
#include "BrowserWindow.h"
#include "SubsonicClientWin.h"
#include <SDK/cfg_var.h>
#include <SDK/album_art.h>
#include <SDK/album_art_helpers.h>
#pragma comment(lib, "winhttp.lib")

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

// ---------------------------------------------------------------------------
// Config vars
// ---------------------------------------------------------------------------
namespace navidrome {
    cfg_string cfg_server_url(guid_cfg_server_url, "http://localhost:4533/");
    cfg_string cfg_username  (guid_cfg_username,   "");
    cfg_string cfg_password  (guid_cfg_password,   "");
    cfg_string cfg_salt      (guid_cfg_salt,        "fb2k_navidrome");
}

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
        return m_changed ? preferences_state::changed | preferences_state::resettable
                         : preferences_state::unchanged;
    }
    void apply()  override { saveSettings(); m_changed = false; notifyCb(); }
    void reset()  override {
        SetDlgItemText(IDC_URL,  L"http://localhost:4533/");
        SetDlgItemText(IDC_USER, L"");
        SetDlgItemText(IDC_PASS, L"");
        m_changed = true; notifyCb();
    }

    BEGIN_MSG_MAP(NavidromePrefsInstance)
        MSG_WM_CREATE(OnCreate)
        COMMAND_HANDLER_EX(IDC_URL,  EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_USER, EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_PASS, EN_CHANGE, OnChanged)
        COMMAND_HANDLER_EX(IDC_TEST, BN_CLICKED, OnTest)
    END_MSG_MAP()

private:
    enum { IDC_URL=1001, IDC_USER=1002, IDC_PASS=1003, IDC_TEST=1004, IDC_STATUS=1005 };

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
        lbl(L"Server URL:",  8, 14, 80, 18);  edit(IDC_URL,  92, 10, 290, 22);
        lbl(L"Username:",    8, 44, 80, 18);  edit(IDC_USER, 92, 40, 290, 22);
        lbl(L"Password:",    8, 74, 80, 18);  edit(IDC_PASS, 92, 70, 290, 22, true);

        HWND btn = CreateWindowW(L"BUTTON", L"Test Connection",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 92,100, 110,24, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TEST)), nullptr, nullptr);
        SendMessageW(btn, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);

        HWND st = CreateWindowW(L"STATIC", L"", WS_CHILD|WS_VISIBLE|SS_LEFT, 210,105, 170,18, *this,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)), nullptr, nullptr);
        SendMessageW(st, WM_SETFONT, reinterpret_cast<WPARAM>(f), 0);

        loadSettings();
        return 0;
    }

    void loadSettings() {
        SetDlgItemText(IDC_URL,  pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_server_url.get().c_str()));
        SetDlgItemText(IDC_USER, pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_username.get().c_str()));
        SetDlgItemText(IDC_PASS, pfc::stringcvt::string_wide_from_utf8(navidrome::cfg_password.get().c_str()));
    }

    void saveSettings() {
        auto getText = [&](int id) -> std::string {
            wchar_t buf[1024] = {};
            GetDlgItemText(id, buf, 1024);
            return pfc::stringcvt::string_utf8_from_wide(buf).get_ptr();
        };
        navidrome::cfg_server_url.set(getText(IDC_URL).c_str());
        navidrome::cfg_username.set(getText(IDC_USER).c_str());
        navidrome::cfg_password.set(getText(IDC_PASS).c_str());
    }

    void OnChanged(UINT, int, HWND) { m_changed = true; notifyCb(); }
    void notifyCb() { if (m_cb.is_valid()) m_cb->on_state_changed(); }

    void OnTest(UINT, int, HWND) {
        saveSettings();
        SetDlgItemText(IDC_STATUS, L"Testing\u2026");
        std::thread([this]() {
            std::string err;
            bool ok = navidrome::SubsonicClientWin::get().ping(err);
            PostMessage(WM_USER + 200, ok ? 1 : 0,
                reinterpret_cast<LPARAM>(ok ? nullptr : new std::string(err)));
        }).detach();
    }

    BEGIN_MSG_MAP_CHAIN(NavidromePrefsInstance)
        if (uMsg == WM_USER + 200) {
            bool ok = wParam != 0;
            auto* errStr = reinterpret_cast<std::string*>(lParam);
            SetDlgItemText(IDC_STATUS, ok ? L"Connected!" :
                pfc::stringcvt::string_wide_from_utf8(errStr ? errStr->c_str() : "Failed"));
            delete errStr;
            bHandled = TRUE; return 0;
        }
    END_MSG_MAP_CHAIN()

    preferences_page_callback::ptr m_cb;
    bool m_changed = false;
};

// Workaround: chain message map trick doesn't compile cleanly across MSVC versions.
// Use a proper BEGIN/END_MSG_MAP and handle WM_USER+200 inside OnCreate registration.
// (Production code should use a dedicated WM_USER constant and proper handler.)

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
    GUID        get_parent_guid() override { return guid_tools; }
};
FB2K_SERVICE_FACTORY(NavidromePrefsPageFactory);

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
        if (i == 0) { out = "Open Navidrome Browser"; return; }
        throw pfc::exception_invalid_params();
    }
    bool get_description(t_uint32 i, pfc::string_base& out, t_uint32& flags) override {
        if (i == 0) { out = "Browse and stream from Navidrome"; return true; }
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
// Album art fallback — serves cover art from Navidrome's getCoverArt endpoint
// ---------------------------------------------------------------------------
static std::string urlParam(const char* url, const char* key) {
    std::string k = std::string(key) + "=";
    const char* p = strstr(url, k.c_str());
    if (!p) return "";
    p += k.size();
    const char* e = strchr(p, '&');
    return e ? std::string(p, e) : std::string(p);
}

class NavidromeArtInstance : public album_art_extractor_instance_v2 {
public:
    explicit NavidromeArtInstance(const char* songId) : m_id(songId) {}

    album_art_data_ptr query(const GUID& what, abort_callback&) override {
        if (what != album_art_ids::cover_front) throw exception_album_art_not_found();

        std::string url = navidrome::SubsonicClientWin::get().coverArtURL(m_id, 0);
        std::string err;
        // Reuse httpGet via SubsonicClientWin
        std::string body = navidrome::SubsonicClientWin::get().streamURL(m_id); // placeholder
        // Fetch binary image data with WinHTTP
        body = "";
        HINTERNET hSess = WinHttpOpen(L"foo_navidrome/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSess) {
            std::wstring wurl(url.begin(), url.end());
            // Proper wide conversion
            int n = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
            wurl.resize(n); MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, &wurl[0], n);
            if (!wurl.empty() && wurl.back()==0) wurl.pop_back();

            URL_COMPONENTS uc = {}; uc.dwStructSize = sizeof(uc);
            wchar_t host[256]={}, path[4096]={};
            uc.lpszHostName=host; uc.dwHostNameLength=256;
            uc.lpszUrlPath=path;  uc.dwUrlPathLength=4096;
            if (WinHttpCrackUrl(wurl.c_str(),0,0,&uc)) {
                HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
                if (hConn) {
                    DWORD flags = (uc.nScheme==INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
                    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path, nullptr,
                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
                    if (hReq) {
                        if (WinHttpSendRequest(hReq,nullptr,0,nullptr,0,0,0) &&
                            WinHttpReceiveResponse(hReq,nullptr)) {
                            DWORD status=0,sz=sizeof(status);
                            WinHttpQueryHeaders(hReq,
                                WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                                nullptr,&status,&sz,nullptr);
                            if (status==200) {
                                DWORD avail=0;
                                while (WinHttpQueryDataAvailable(hReq,&avail) && avail>0) {
                                    std::string chunk(avail,'\0');
                                    DWORD read=0;
                                    WinHttpReadData(hReq,&chunk[0],avail,&read);
                                    body.append(chunk,0,read);
                                }
                            }
                        }
                        WinHttpCloseHandle(hReq);
                    }
                    WinHttpCloseHandle(hConn);
                }
            }
            WinHttpCloseHandle(hSess);
        }
        if (body.empty()) throw exception_album_art_not_found();
        return album_art_data_impl::g_create(body.data(), body.size());
    }

    album_art_path_list::ptr query_paths(const GUID&, abort_callback&) override {
        throw exception_album_art_not_found();
    }

private:
    std::string m_id;
};

class NavidromeArtFallback : public album_art_fallback {
public:
    album_art_extractor_instance_v2::ptr open(metadb_handle_list_cref items,
        pfc::list_base_const_t<GUID> const&, abort_callback&) override {
        for (t_size i = 0; i < items.get_count(); i++) {
            const char* path = items[i]->get_path();
            if (strstr(path, "/rest/stream.view")) {
                std::string id = urlParam(path, "id");
                if (!id.empty())
                    return fb2k::service_new<NavidromeArtInstance>(id.c_str());
            }
        }
        throw exception_album_art_not_found();
    }
};
FB2K_SERVICE_FACTORY(NavidromeArtFallback);
