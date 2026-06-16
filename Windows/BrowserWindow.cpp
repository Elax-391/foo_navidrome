#include "stdafx.h"
#include "BrowserWindow.h"
#include "SubsonicClientWin.h"
#include <SDK/playlist.h>
#include <SDK/metadb.h>
#include <SDK/playable_location.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

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
// Singleton
// ---------------------------------------------------------------------------
BrowserWindow& BrowserWindow::get() {
    static BrowserWindow inst;
    return inst;
}

void BrowserWindow::show() {
    if (!IsWindow()) {
        Create(nullptr, CWindow::rcDefault, L"Navidrome Browser",
               WS_OVERLAPPEDWINDOW, 0);
        SetWindowPos(nullptr, 0, 0, 580, 660,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
        loadArtists();
    } else {
        ShowWindow(SW_SHOW);
        SetForegroundWindow(*this);
    }
}

// ---------------------------------------------------------------------------
// Window messages
// ---------------------------------------------------------------------------
LRESULT BrowserWindow::OnCreate(LPCREATESTRUCT) {
    HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    // Search field
    m_search.Create(*this, CWindow::rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, IDC_SEARCH);
    m_search.SetFont(hFont);
    m_search.SetCueBannerText(L"Search artists, albums, songs\u2026");

    // Tree view
    m_tree.Create(*this, CWindow::rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES |
        TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
        0, IDC_TREE);
    m_tree.SetFont(hFont);

    // Buttons
    m_addBtn.Create(*this, CWindow::rcDefault, L"Add to Playlist",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_ADD);
    m_addBtn.SetFont(hFont);

    m_playBtn.Create(*this, CWindow::rcDefault, L"Play Now",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_PLAY);
    m_playBtn.SetFont(hFont);

    m_refreshBtn.Create(*this, CWindow::rcDefault, L"Refresh",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_REFRESH);
    m_refreshBtn.SetFont(hFont);

    // Status label
    m_status.Create(*this, CWindow::rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | SS_LEFT, 0, IDC_STATUS);
    m_status.SetFont(hFont);

    return 0;
}

void BrowserWindow::OnDestroy() {
    m_nodeMap.clear();
    m_rootNodes.clear();
}

LRESULT BrowserWindow::OnSize(UINT, CSize sz) {
    const int pad = 6, btnH = 26, searchH = 22, statusW = 200;
    int w = sz.cx, h = sz.cy;

    m_search.SetWindowPos(nullptr,
        pad, pad, w - 2*pad, searchH,
        SWP_NOZORDER);
    m_tree.SetWindowPos(nullptr,
        pad, pad + searchH + pad,
        w - 2*pad, h - searchH - btnH - 4*pad,
        SWP_NOZORDER);

    int btnY = h - pad - btnH;
    int btnW = 110;
    m_refreshBtn.SetWindowPos(nullptr, pad, btnY, 80, btnH, SWP_NOZORDER);
    m_status.SetWindowPos(nullptr,
        pad + 80 + pad, btnY + 4,
        w - 80 - 2*btnW - 4*pad, btnH, SWP_NOZORDER);
    m_playBtn.SetWindowPos(nullptr,
        w - pad - btnW, btnY, btnW, btnH, SWP_NOZORDER);
    m_addBtn.SetWindowPos(nullptr,
        w - pad - 2*btnW - pad, btnY, btnW, btnH, SWP_NOZORDER);
    return 0;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
void BrowserWindow::loadArtists() {
    setStatus("Loading artists\u2026");
    m_tree.DeleteAllItems();
    m_nodeMap.clear();
    m_rootNodes.clear();

    std::thread([this]() {
        auto* payload = new LoadedPayload{};
        std::string err;
        auto artists = navidrome::SubsonicClientWin::get().getArtists(err);
        payload->error = err;
        for (auto& a : artists) {
            auto n = std::make_shared<NavidromeNode>();
            n->type        = NavidromeNode::Artist;
            n->id          = a.id;
            n->displayName = a.name;
            n->coverArtId  = a.coverArtId;
            payload->nodes.push_back(n);
        }
        PostMessage(WM_NAVIDROME_LOADED, reinterpret_cast<WPARAM>(payload), 0);
    }).detach();
}

LRESULT BrowserWindow::OnNavidromeLoaded(UINT, WPARAM wParam, LPARAM, BOOL&) {
    auto* payload = reinterpret_cast<LoadedPayload*>(wParam);
    populateRoot(payload);
    delete payload;
    return 0;
}

LRESULT BrowserWindow::OnNavidromeChildren(UINT, WPARAM wParam, LPARAM, BOOL&) {
    auto* payload = reinterpret_cast<LoadedPayload*>(wParam);
    populateChildren(payload);
    delete payload;
    return 0;
}

void BrowserWindow::populateRoot(LoadedPayload* payload) {
    if (!payload->error.empty()) {
        setStatus("Error: " + payload->error); return;
    }
    m_rootNodes = payload->nodes;
    for (auto& n : m_rootNodes)
        insertNode(TVI_ROOT, n);
    setStatus(std::to_string(m_rootNodes.size()) + " artists");
}

void BrowserWindow::populateChildren(LoadedPayload* payload) {
    auto parent = payload->parent;
    if (!parent) return;

    // Remove placeholder "Loading..." item
    HTREEITEM hChild = m_tree.GetChildItem(parent->hItem);
    while (hChild) {
        HTREEITEM hNext = m_tree.GetNextSiblingItem(hChild);
        auto it = m_nodeMap.find(hChild);
        if (it != m_nodeMap.end() && it->second->type == NavidromeNode::Loading) {
            m_tree.DeleteItem(hChild);
            m_nodeMap.erase(it);
        }
        hChild = hNext;
    }

    parent->isLoading      = false;
    parent->childrenLoaded = true;
    parent->children       = payload->nodes;

    if (!payload->error.empty()) {
        auto errNode = std::make_shared<NavidromeNode>();
        errNode->type        = NavidromeNode::Error;
        errNode->displayName = "Error: " + payload->error;
        parent->children     = { errNode };
    }

    for (auto& child : parent->children)
        insertNode(parent->hItem, child);

    if (parent->children.empty()) {
        // No children — clear the expand button. WTL's CTreeViewCtrl has no
        // SetItemChildren; set cChildren via the TVITEM mask directly.
        TVITEM it   = {};
        it.mask     = TVIF_CHILDREN;
        it.hItem    = parent->hItem;
        it.cChildren = 0;
        m_tree.SetItem(&it);
    }
}

HTREEITEM BrowserWindow::insertNode(HTREEITEM hParent,
                                    std::shared_ptr<NavidromeNode> node) {
    std::string label = node->displayName;
    if (node->type == NavidromeNode::Song && node->track > 0)
        label = std::to_string(node->track) + ". " + label;

    TVINSERTSTRUCT tvi    = {};
    tvi.hParent           = hParent;
    tvi.hInsertAfter      = TVI_LAST;
    tvi.item.mask         = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    auto wlabel           = u8ToWide(label);
    tvi.item.pszText      = const_cast<LPWSTR>(wlabel.c_str());
    tvi.item.lParam       = reinterpret_cast<LPARAM>(node.get());
    // Show expand arrow for artists and albums
    tvi.item.cChildren    = (node->type == NavidromeNode::Song   ||
                              node->type == NavidromeNode::Error  ||
                              node->type == NavidromeNode::Loading) ? 0 : 1;

    HTREEITEM hItem = m_tree.InsertItem(&tvi);
    node->hItem = hItem;
    m_nodeMap[hItem] = node;
    return hItem;
}

std::shared_ptr<NavidromeNode> BrowserWindow::nodeForItem(HTREEITEM hItem) {
    auto it = m_nodeMap.find(hItem);
    return (it != m_nodeMap.end()) ? it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Tree events
// ---------------------------------------------------------------------------
LRESULT BrowserWindow::OnTreeExpanding(LPNMHDR pnmh) {
    auto* pnm = reinterpret_cast<LPNMTREEVIEW>(pnmh);
    if (pnm->action != TVE_EXPAND) return 0;

    auto node = nodeForItem(pnm->itemNew.hItem);
    if (!node || node->childrenLoaded || node->isLoading) return 0;
    node->isLoading = true;

    // Insert placeholder
    auto loadNode = std::make_shared<NavidromeNode>();
    loadNode->type        = NavidromeNode::Loading;
    loadNode->displayName = "Loading\u2026";
    insertNode(node->hItem, loadNode);

    std::thread([this, node]() {
        auto* payload  = new LoadedPayload{};
        payload->parent = node;
        std::string err;

        if (node->type == NavidromeNode::Artist) {
            for (auto& a : navidrome::SubsonicClientWin::get()
                               .getAlbumsForArtist(node->id, err)) {
                auto n = std::make_shared<NavidromeNode>();
                n->type        = NavidromeNode::Album;
                n->id          = a.id;
                n->displayName = a.name;
                n->subtitle    = a.artist;
                n->coverArtId  = a.coverArtId;
                payload->nodes.push_back(n);
            }
        } else if (node->type == NavidromeNode::Album) {
            for (auto& s : navidrome::SubsonicClientWin::get()
                               .getSongsForAlbum(node->id, err)) {
                auto n = std::make_shared<NavidromeNode>();
                n->type           = NavidromeNode::Song;
                n->id             = s.id;
                n->displayName    = s.title;
                n->subtitle       = s.artist;
                n->coverArtId     = s.coverArtId;
                n->track          = s.track;
                n->duration       = s.duration;
                n->childrenLoaded = true;
                payload->nodes.push_back(n);
            }
        }
        payload->error = err;
        PostMessage(WM_NAVIDROME_CHILDREN,
                    reinterpret_cast<WPARAM>(payload), 0);
    }).detach();

    return 0;
}

LRESULT BrowserWindow::OnTreeDblClick(LPNMHDR) {
    HTREEITEM hSel = m_tree.GetSelectedItem();
    if (!hSel) return 0;
    auto node = nodeForItem(hSel);
    if (!node) return 0;
    if (node->type == NavidromeNode::Song)
        enqueueNodes({ node }, true);
    else if (m_tree.GetItemState(hSel, TVIS_EXPANDED) & TVIS_EXPANDED)
        m_tree.Expand(hSel, TVE_COLLAPSE);
    else
        m_tree.Expand(hSel, TVE_EXPAND);
    return 0;
}

// ---------------------------------------------------------------------------
// Button actions
// ---------------------------------------------------------------------------
void BrowserWindow::OnAdd(UINT, int, HWND) {
    std::vector<std::shared_ptr<NavidromeNode>> selected;
    HTREEITEM hItem = m_tree.GetFirstVisibleItem();
    while (hItem) {
        if (m_tree.GetItemState(hItem, TVIS_SELECTED) & TVIS_SELECTED) {
            auto n = nodeForItem(hItem);
            if (n && n->type != NavidromeNode::Loading && n->type != NavidromeNode::Error)
                selected.push_back(n);
        }
        hItem = m_tree.GetNextVisibleItem(hItem);
    }
    if (selected.empty()) { setStatus("Select at least one item"); return; }

    setStatus("Loading tracks\u2026");
    std::thread([this, selected]() {
        std::vector<std::shared_ptr<NavidromeNode>> songs;
        for (auto& n : selected)
            collectSongsDeep(n, songs);
        fb2k::inMainThread([this, songs]() mutable {
            enqueueNodes(std::move(songs), false);
        });
    }).detach();
}

void BrowserWindow::OnPlay(UINT, int, HWND) {
    std::vector<std::shared_ptr<NavidromeNode>> selected;
    HTREEITEM hItem = m_tree.GetFirstVisibleItem();
    while (hItem) {
        if (m_tree.GetItemState(hItem, TVIS_SELECTED) & TVIS_SELECTED) {
            auto n = nodeForItem(hItem);
            if (n && n->type != NavidromeNode::Loading && n->type != NavidromeNode::Error)
                selected.push_back(n);
        }
        hItem = m_tree.GetNextVisibleItem(hItem);
    }
    if (selected.empty()) { setStatus("Select at least one item"); return; }

    setStatus("Loading tracks\u2026");
    std::thread([this, selected]() {
        std::vector<std::shared_ptr<NavidromeNode>> songs;
        for (auto& n : selected)
            collectSongsDeep(n, songs);
        fb2k::inMainThread([this, songs]() mutable {
            enqueueNodes(std::move(songs), true);
        });
    }).detach();
}

void BrowserWindow::OnRefresh(UINT, int, HWND) {
    m_search.SetWindowText(L"");
    loadArtists();
}

void BrowserWindow::OnSearchChanged(UINT, int, HWND) {
    wchar_t buf[256] = {};
    m_search.GetWindowText(buf, 256);
    std::string query = wToU8(buf);
    if (query.size() < 2) {
        if (m_rootNodes.empty()) loadArtists();
        return;
    }
    setStatus("Searching\u2026");
    std::thread([this, query]() {
        std::string err;
        auto results = navidrome::SubsonicClientWin::get().search(query, err);
        auto* payload = new LoadedPayload{};
        payload->error = err;
        for (auto& s : results.songs) {
            auto n = std::make_shared<NavidromeNode>();
            n->type           = NavidromeNode::Song;
            n->id             = s.id;
            n->displayName    = s.title + " — " + s.artist;
            n->coverArtId     = s.coverArtId;
            n->duration       = s.duration;
            n->childrenLoaded = true;
            payload->nodes.push_back(n);
        }
        PostMessage(WM_NAVIDROME_LOADED, reinterpret_cast<WPARAM>(payload), 0);
    }).detach();
}

// ---------------------------------------------------------------------------
// Deep song collection (synchronous, call from background thread)
// ---------------------------------------------------------------------------
void BrowserWindow::collectSongsDeep(std::shared_ptr<NavidromeNode> node,
                                     std::vector<std::shared_ptr<NavidromeNode>>& out) {
    if (node->type == NavidromeNode::Song) { out.push_back(node); return; }
    if (node->type == NavidromeNode::Loading || node->type == NavidromeNode::Error) return;

    if (node->type == NavidromeNode::Album) {
        if (node->childrenLoaded) {
            for (auto& c : node->children) collectSongsDeep(c, out);
        } else {
            std::string err;
            for (auto& s : navidrome::SubsonicClientWin::get()
                               .getSongsForAlbum(node->id, err)) {
                auto n = std::make_shared<NavidromeNode>();
                n->type = NavidromeNode::Song; n->id = s.id;
                n->displayName = s.title; n->subtitle = s.artist;
                n->coverArtId = s.coverArtId; n->track = s.track;
                n->duration = s.duration; n->childrenLoaded = true;
                out.push_back(n);
            }
        }
        return;
    }
    if (node->type == NavidromeNode::Artist) {
        if (node->childrenLoaded) {
            for (auto& c : node->children) collectSongsDeep(c, out);
        } else {
            std::string err;
            for (auto& a : navidrome::SubsonicClientWin::get()
                               .getAlbumsForArtist(node->id, err)) {
                auto albumNode = std::make_shared<NavidromeNode>();
                albumNode->type = NavidromeNode::Album; albumNode->id = a.id;
                albumNode->displayName = a.name;
                collectSongsDeep(albumNode, out);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Enqueue to foobar2000 playlist (call from main thread)
// ---------------------------------------------------------------------------
void BrowserWindow::enqueueNodes(std::vector<std::shared_ptr<NavidromeNode>> songs,
                                 bool play) {
    if (songs.empty()) { setStatus("No songs selected"); return; }

    metadb_handle_list tracks;
    auto hints = metadb_io_v2::get()->create_hint_list();

    for (auto& node : songs) {
        std::string url = navidrome::SubsonicClientWin::get().streamURL(node->id);

        metadb_handle_ptr handle;
        playable_location_impl loc;
        loc.set_path(url.c_str());
        loc.set_subsong(0);
        metadb::get()->handle_create(handle, loc);
        tracks += handle;

        file_info_impl info;
        if (!node->displayName.empty()) info.meta_set("title",  node->displayName.c_str());
        if (!node->subtitle.empty())    info.meta_set("artist", node->subtitle.c_str());
        if (node->track > 0)            info.meta_set("tracknumber", pfc::format_int(node->track));
        if (node->duration > 0)         info.set_length(node->duration);
        hints->add_hint(handle, info, filestats_invalid, true);
    }
    hints->on_done();

    auto pm = playlist_manager::get();
    t_size pl = pm->get_active_playlist();
    if (pl == pfc_infinite) {
        pm->create_playlist("Navidrome", ~0, pfc_infinite);
        pl = pm->get_active_playlist();
    }
    t_size insertPos = pm->playlist_get_item_count(pl);
    pm->playlist_add_items(pl, tracks, pfc::bit_array_false());

    if (play && tracks.get_count() > 0) {
        pm->set_active_playlist(pl);
        pm->playlist_execute_default_action(pl, insertPos);
    }

    std::string msg = "Added " + std::to_string(tracks.get_count()) + " tracks";
    setStatus(msg);
}

void BrowserWindow::setStatus(const std::string& msg) {
    m_status.SetWindowText(u8ToWide(msg).c_str());
}
