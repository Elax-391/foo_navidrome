#import "stdafx.h"
#import "SubsonicClient.h"
#import "Mac/NavidromeBrowserController.h"
#import "Mac/NavidromePreferencesController.h"
#include <helpers/advconfig_impl.h>
#include <SDK/cfg_var.h>
#include <SDK/library_manager.h>

// ---------------------------------------------------------------------------
// GUIDs — replace with your own when forking this component
// ---------------------------------------------------------------------------
static constexpr GUID guid_cfg_server_url  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x01 } };
static constexpr GUID guid_cfg_username    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x02 } };
static constexpr GUID guid_cfg_password    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x03 } };
static constexpr GUID guid_cfg_salt        = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x04 } };
static constexpr GUID guid_prefs_page      = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x05 } };
static constexpr GUID guid_mainmenu_group  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x06 } };
static constexpr GUID guid_mainmenu_cmd    = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x07 } };
static constexpr GUID guid_library_viewer  = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x08 } };
static constexpr GUID guid_library_prefs   = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x09 } };
static constexpr GUID guid_cfg_custom_headers = { 0xa1b2c3d4, 0x1111, 0x2222, { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01, 0x0a } };

// ---------------------------------------------------------------------------
// Config variables (exported so SubsonicClient.mm can access them)
// ---------------------------------------------------------------------------
namespace navidrome {
    cfg_string cfg_server_url(guid_cfg_server_url, "http://navidrome.santirod.local:4533/");
    cfg_string cfg_username  (guid_cfg_username,   "");
    cfg_string cfg_password  (guid_cfg_password,   "");
    cfg_string cfg_salt      (guid_cfg_salt,        "fb2k_navidrome");
    // Extra HTTP headers (one "Name: Value" per line) sent on every request —
    // API, cover art and audio stream. Used e.g. for Cloudflare Access
    // service-token headers when Navidrome sits behind a Zero Trust tunnel.
    cfg_string cfg_custom_headers(guid_cfg_custom_headers, "");
}

// ---------------------------------------------------------------------------
// Preferences page (Mac)
// ---------------------------------------------------------------------------

namespace {

class preferences_page_navidrome : public preferences_page {
public:
    service_ptr instantiate() override {
        return fb2k::wrapNSObject([NavidromePreferencesController new]);
    }
    const char *get_name() override { return "Navidrome"; }
    GUID get_guid() override { return guid_prefs_page; }
    GUID get_parent_guid() override { return guid_tools; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome);

// ---------------------------------------------------------------------------
// Main menu: File > Open Navidrome Browser
// ---------------------------------------------------------------------------

class mainmenu_navidrome : public mainmenu_commands {
public:
    t_uint32 get_command_count() override { return 1; }

    GUID get_command(t_uint32 p_index) override {
        if (p_index == 0) return guid_mainmenu_cmd;
        throw pfc::exception_invalid_params();
    }

    void get_name(t_uint32 p_index, pfc::string_base &p_out) override {
        if (p_index == 0) { p_out = "Open Navidrome Browser"; return; }
        throw pfc::exception_invalid_params();
    }

    bool get_description(t_uint32 p_index, pfc::string_base &p_out) override {
        if (p_index == 0) {
            p_out = "Browse and stream music from your Navidrome server";
            return true;
        }
        return false;
    }

    GUID get_parent() override { return mainmenu_groups::file; }

    t_uint32 get_sort_priority() override { return 0xFF; }

    bool get_display(t_uint32 p_index, pfc::string_base &p_out, t_uint32 &p_flags) override {
        get_name(p_index, p_out);
        p_flags = 0;
        return true;
    }

    void execute(t_uint32 p_index, service_ptr_t<service_base> p_callback) override {
        if (p_index != 0) throw pfc::exception_invalid_params();
        NavidromeShowStandaloneBrowser();
    }
};

FB2K_SERVICE_FACTORY(mainmenu_navidrome);

// ---------------------------------------------------------------------------
// Library viewer — exposes the browser to the foobar Media Library system.
// On macOS the visible surface is the preferences sub-page below, registered
// under guid_media_library, which mirrors how Album List / ReFacets show up.
// ---------------------------------------------------------------------------

class library_viewer_navidrome : public library_viewer {
public:
    GUID get_preferences_page() override { return guid_library_prefs; }
    bool have_activate()        override { return true; }

    void activate() override { NavidromeShowStandaloneBrowser(); }

    GUID         get_guid() override { return guid_library_viewer; }
    const char * get_name() override { return "Navidrome"; }
};

static library_viewer_factory_t<library_viewer_navidrome> g_library_viewer_navidrome_factory;

} // namespace

// ---------------------------------------------------------------------------
// Media Library preferences sub-page — embeds the browser directly so users
// see Artists/Albums/Songs without opening a separate window. The page IS
// the browser. A fresh NavidromeBrowserController is created per page mount.
// ---------------------------------------------------------------------------

namespace {

class preferences_page_navidrome_library : public preferences_page {
public:
    service_ptr instantiate() override {
        return fb2k::wrapNSObject([NavidromeBrowserController new]);
    }
    const char *get_name() override { return "Navidrome"; }
    GUID get_guid() override { return guid_library_prefs; }
    GUID get_parent_guid() override { return guid_media_library; }
};

FB2K_SERVICE_FACTORY(preferences_page_navidrome_library);

} // namespace
