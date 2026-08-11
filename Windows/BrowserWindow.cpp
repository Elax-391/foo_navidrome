#include "stdafx.h"
#include "BrowserWindow.h"
#include "Localization.h"
#include "SubsonicClientWin.h"
#include "NavidromeInputWin.h"
#include <SDK/playlist.h>
#include <SDK/metadb.h>
#include <SDK/playable_location.h>
#include <SDK/playback_control.h>
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
        Create(nullptr, CWindow::rcDefault, navidrome::l10n::browserTitle,
               WS_OVERLAPPEDWINDOW, 0);
        SetWindowPos(nullptr, 0, 0, 580, 660,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);
        loadArtists();
    } else {
        ShowWindow(SW_SHOW);
        SetForegroundWindow(*this);
    }
}

// Inline mount for the Media Library prefs page. A fresh (non-singleton)
// instance owned by the host; the host sizes it to fill its client area.
void BrowserWindow::createEmbedded(HWND parent) {
    m_embedded = true;
    if (IsWindow()) return;
    RECT rc{}; ::GetClientRect(parent, &rc);
    Create(parent, rc, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0);
    loadArtists();
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
    m_search.SetCueBannerText(navidrome::l10n::searchCue);

    // Tree view
    m_tree.Create(*this, CWindow::rcDefault, nullptr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES |
        TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
        0, IDC_TREE);
    m_tree.SetFont(hFont);

    // Buttons
    m_addBtn.Create(*this, CWindow::rcDefault, navidrome::l10n::addToPlaylist,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_ADD);
    m_addBtn.SetFont(hFont);

    m_playBtn.Create(*this, CWindow::rcDefault, navidrome::l10n::playNow,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_PLAY);
    m_playBtn.SetFont(hFont);

    m_refreshBtn.Create(*this, CWindow::rcDefault, navidrome::l10n::refresh,
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

void BrowserWindow::OnGetMinMaxInfo(LPMINMAXINFO info) {
    if (m_embedded) return;
    if (info->ptMinTrackSize.x < 400) info->ptMinTrackSize.x = 400;
    if (info->ptMinTrackSize.y < 260) info->ptMinTrackSize.y = 260;
}

LRESULT BrowserWindow::OnSize(UINT, CSize sz) {
    const int pad = 6, btnH = 26, searchH = 22;
    const int w = sz.cx > 0 ? sz.cx : 0;
    const int h = sz.cy > 0 ? sz.cy : 0;
    const int contentW = w > 2 * pad ? w - 2 * pad : 0;
    const int treeH = h > searchH + btnH + 4 * pad
        ? h - searchH - btnH - 4 * pad
        : 0;

    m_search.SetWindowPos(nullptr,
        pad, pad, contentW, searchH,
        SWP_NOZORDER);
    m_tree.SetWindowPos(nullptr,
        pad, pad + searchH + pad,
        contentW, treeH,
        SWP_NOZORDER);

    const int btnY = h > pad + btnH ? h - pad - btnH : 0;
    const int desiredRefreshW = 56, desiredAddW = 168, desiredPlayW = 96;
    const int desiredButtonsW = desiredRefreshW + desiredAddW + desiredPlayW;
    const int availableRowW = w > 5 * pad ? w - 5 * pad : 0;
    const bool compact = availableRowW < desiredButtonsW;
    const int refreshW = compact ? availableRowW * desiredRefreshW / desiredButtonsW
                                 : desiredRefreshW;
    const int addW = compact ? availableRowW * desiredAddW / desiredButtonsW
                             : desiredAddW;
    const int playW = compact ? availableRowW - refreshW - addW
                              : desiredPlayW;
    m_refreshBtn.SetWindowPos(nullptr, pad, btnY, refreshW, btnH, SWP_NOZORDER);
    const int statusX = pad + refreshW + pad;
    const int statusW = compact ? 0 : availableRowW - desiredButtonsW;
    m_status.SetWindowPos(nullptr,
        statusX, btnY + 4, statusW, btnH, SWP_NOZORDER);
    const int addX = statusX + statusW + pad;
    m_addBtn.SetWindowPos(nullptr, addX, btnY, addW, btnH, SWP_NOZORDER);
    m_playBtn.SetWindowPos(nullptr, addX + addW + pad, btnY, playW, btnH, SWP_NOZORDER);
    return 0;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
void BrowserWindow::loadArtists() {
    setStatus(navidrome::l10n::loadingArtists);
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
        setStatus(navidrome::l10n::error(payload->error)); return;
    }
    m_rootNodes = payload->nodes;
    for (auto& n : m_rootNodes)
        insertNode(TVI_ROOT, n);
    setStatus(navidrome::l10n::artistCount(m_rootNodes.size()));
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
        errNode->displayName = navidrome::l10n::error(payload->error);
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
    loadNode->displayName = navidrome::l10n::loading;
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
                n->album          = s.album;
                n->coverArtId     = s.coverArtId;
                n->suffix         = s.suffix;
                n->track          = s.track;
                n->year           = s.year;
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
// Gather the tree's selected, playable nodes (standard treeview is single-
// select, but iterating TVIS_SELECTED keeps this correct if that ever changes).
std::vector<std::shared_ptr<NavidromeNode>> BrowserWindow::selectedNodes() {
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
    return selected;
}

// Resolve the selected nodes to songs on a background thread, then enqueue on
// the main thread. closeAfter hides the window once the tracks are queued \u2014
// used by the Enter shortcut so "select artist + Enter" queues and dismisses.
void BrowserWindow::queueSelected(bool play, bool closeAfter) {
    auto selected = selectedNodes();
    if (selected.empty()) { setStatus(navidrome::l10n::selectAtLeastOne); return; }

    setStatus(navidrome::l10n::loadingTracks);
    std::thread([this, selected, play, closeAfter]() {
        std::vector<std::shared_ptr<NavidromeNode>> songs;
        for (auto& n : selected)
            collectSongsDeep(n, songs);
        fb2k::inMainThread([this, songs, play, closeAfter]() mutable {
            enqueueNodes(std::move(songs), play);
            if (closeAfter && !m_embedded && IsWindow()) ShowWindow(SW_HIDE);
        });
    }).detach();
}

void BrowserWindow::OnAdd(UINT, int, HWND)  { queueSelected(false, false); }
void BrowserWindow::OnPlay(UINT, int, HWND) { queueSelected(true,  false); }

// Enter in the tree = add the selected item(s) to the playlist, start playing
// the first track, and close the window. A quick "queue this artist, play it,
// and get out of my way" shortcut.
LRESULT BrowserWindow::OnTreeReturn(LPNMHDR) {
    queueSelected(true, true);
    return 0;
}

// Right-click context menu on the tree — mirrors the Add/Play buttons for a
// native feel. The menu item IDs are IDC_PLAY / IDC_ADD, so TrackPopupMenu
// posts WM_COMMAND straight into the existing OnPlay / OnAdd handlers.
void BrowserWindow::OnContextMenu(CWindow wnd, CPoint point) {
    if (wnd.m_hWnd != m_tree.m_hWnd) { SetMsgHandled(FALSE); return; }

    if (point.x == -1 && point.y == -1) {
        // Keyboard-invoked (Shift+F10 / menu key): anchor on the selected item.
        HTREEITEM sel = m_tree.GetSelectedItem();
        CRect rc;
        if (sel && m_tree.GetItemRect(sel, &rc, TRUE)) point = rc.CenterPoint();
        else { m_tree.GetClientRect(&rc); point = rc.TopLeft(); }
        m_tree.ClientToScreen(&point);
    } else {
        // Mouse: select the row under the cursor so the action targets it.
        CPoint client(point);
        m_tree.ScreenToClient(&client);
        UINT flags = 0;
        HTREEITEM hit = m_tree.HitTest(client, &flags);
        if (hit) m_tree.SelectItem(hit);
    }

    if (selectedNodes().empty()) return;

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, IDC_PLAY, navidrome::l10n::playNow);
    menu.AppendMenu(MF_STRING, IDC_ADD, navidrome::l10n::addToPlaylist);
    menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, *this);
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
    setStatus(navidrome::l10n::searching);
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
            n->subtitle       = s.artist;
            n->album          = s.album;
            n->coverArtId     = s.coverArtId;
            n->track          = s.track;
            n->year           = s.year;
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
                n->album = s.album;
                n->coverArtId = s.coverArtId; n->track = s.track;
                n->suffix = s.suffix;
                n->year = s.year;
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
    if (songs.empty()) { setStatus(navidrome::l10n::noSongsSelected); return; }

    metadb_handle_list tracks;
    auto hints = metadb_io_v2::get()->create_hint_list();

    for (auto& node : songs) {
        // Enqueue a navidrome://track/<id>?... URI (not the raw HTTP URL) so the
        // input handler resolves the stream — with custom headers — at decode
        // time, and metadata renders without a network round-trip.
        std::string uri = navidrome::makeTrackURI(node->id, node->displayName,
            node->subtitle, node->album, node->track, node->year,
            node->duration, node->coverArtId, node->suffix);
        if (uri.empty()) continue;

        metadb_handle_ptr handle;
        playable_location_impl loc;
        loc.set_path(uri.c_str());
        loc.set_subsong(0);
        metadb::get()->handle_create(handle, loc);
        tracks += handle;

        file_info_impl info;
        if (!node->displayName.empty()) info.meta_set("title",  node->displayName.c_str());
        if (!node->subtitle.empty())    info.meta_set("artist", node->subtitle.c_str());
        if (!node->album.empty())       info.meta_set("album",  node->album.c_str());
        if (node->track > 0)            info.meta_set("tracknumber", pfc::format_int(node->track));
        if (node->year > 0)             info.meta_set("date",   pfc::format_int(node->year));
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
        // Start playback honoring the user's Playback > Order setting (Shuffle,
        // Random, Default, …). track_command_play asks the active playback order
        // for the starting track; the focus biases in-order modes to the first
        // newly-added track. (playlist_execute_default_action would instead pin
        // that exact track and ignore the order.)
        pm->set_active_playlist(pl);
        pm->set_playing_playlist(pl);
        pm->playlist_set_focus_item(pl, insertPos);
        playback_control::get()->start(playback_control::track_command_play);
    }

    setStatus(navidrome::l10n::addedTracks(tracks.get_count()));
}

void BrowserWindow::setStatus(const std::string& msg) {
    m_status.SetWindowText(u8ToWide(msg).c_str());
}
