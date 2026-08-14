#include "stdafx.h"
#include "BrowserWindow.h"
#include "BrowserExtrasLogic.h"
#include "BrowserMutationHub.h"
#include "LibraryImportLogic.h"
#include "Localization.h"
#include "SubsonicClientWin.h"
#include "ServerIdentity.h"
#include "NavidromeInputWin.h"
#include "LibraryImporter.h"
#include "SongMetadataProjection.h"
#include <SDK/playlist.h>
#include <SDK/metadb.h>
#include <SDK/playable_location.h>
#include <SDK/playback_control.h>
#include <commctrl.h>
#include <algorithm>
#include <set>
#include <utility>
#pragma comment(lib, "comctl32.lib")

namespace {

template<typename Payload>
class DeferredPayloadOwner {
public:
    explicit DeferredPayloadOwner(Payload* payload) : m_payload(payload) {}
    ~DeferredPayloadOwner() { delete m_payload; }
    Payload* release() {
        auto* payload = m_payload;
        m_payload = nullptr;
        return payload;
    }

private:
    Payload* m_payload = nullptr;
};

template<typename Payload>
void dispatchBrowserPayload(const std::shared_ptr<BrowserDispatchState>& dispatch,
                            UINT message, Payload* payload) {
    auto owner = std::make_shared<DeferredPayloadOwner<Payload>>(payload);
    fb2k::inMainThread([dispatch, message, owner]() {
        if (!dispatch || !dispatch->alive || !::IsWindow(dispatch->hwnd)) {
            return;
        }
        ::SendMessage(dispatch->hwnd, message,
                      reinterpret_cast<WPARAM>(owner->release()), 0);
    });
}

bool cancellationRequested(const std::shared_ptr<std::atomic_bool>& cancel) {
    return cancel && cancel->load();
}

} // namespace

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

static std::string mutationKey(navidrome::BrowserMutationKind mutation,
                               navidrome::FavoriteKind kind,
                               const std::string& id) {
    char mutationPrefix = mutation == navidrome::BrowserMutationKind::RatingChanged
        ? 'g' : 'f';
    char prefix = 's';
    if (kind == navidrome::FavoriteKind::Album) prefix = 'a';
    if (kind == navidrome::FavoriteKind::Artist) prefix = 'r';
    return std::string(1, mutationPrefix) + ':' + prefix + ':' + id;
}

static std::optional<navidrome::FavoriteKind> favoriteKindForNode(
        const std::shared_ptr<NavidromeNode>& node) {
    if (!node) return std::nullopt;
    if (node->type == NavidromeNode::Song) return navidrome::FavoriteKind::Song;
    if (node->type == NavidromeNode::Album) return navidrome::FavoriteKind::Album;
    if (node->type == NavidromeNode::Artist) return navidrome::FavoriteKind::Artist;
    return std::nullopt;
}

static std::vector<std::string> playlistSongIds(
        const navidrome::ServerPlaylistDetails& details) {
    std::vector<std::string> ids;
    ids.reserve(details.songs.size());
    for (const auto& song : details.songs) ids.push_back(song.id);
    return ids;
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

    m_addAllBtn.Create(*this, CWindow::rcDefault, navidrome::l10n::addAllToPlaylist,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_ADD_ALL);
    m_addAllBtn.SetFont(hFont);

    m_reconcileBtn.Create(*this, CWindow::rcDefault, navidrome::l10n::reconcileLibrary,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, IDC_RECONCILE);
    m_reconcileBtn.SetFont(hFont);

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

    m_dispatchState = std::make_shared<BrowserDispatchState>();
    m_dispatchState->hwnd = *this;
    m_dispatchState->alive = true;
    m_mutationSubscription = navidrome::BrowserMutationHub::get().subscribe(
        [this](const navidrome::BrowserMutationEvent& event) {
            applyMutationEvent(event);
        });
    updateActionState();

    return 0;
}

void BrowserWindow::OnDestroy() {
    m_mutationSubscription.reset();
    if (m_queueCancel) m_queueCancel->store(true);
    ++m_queueOperationId;
    ++m_mutationOperationId;
    ++m_playlistOperationId;
    ++m_libraryRequestId;
    ++m_searchRequestId;
    ++m_displayGeneration;
    m_queueInProgress = false;
    m_mutationInProgress = false;
    m_playlistInProgress = false;
    m_libraryLoading = false;
    if (m_dispatchState) {
        m_dispatchState->alive = false;
        m_dispatchState->hwnd = nullptr;
    }
    m_queueCancel.reset();
    m_dispatchState.reset();
    m_nodeMap.clear();
    m_rootNodes.clear();
    m_libraryRoots.clear();
    m_deferredChildren.clear();
    m_deferredMutationEvents.clear();
    m_confirmedMutations.clear();
    m_appliedMutationRevisions.clear();
    m_mutationIdentity.clear();
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
    const int desiredRefreshW = 56, desiredAddAllW = 88, desiredReconcileW = 88;
    const int desiredAddW = 168, desiredPlayW = 96;
    const int desiredButtonsW = desiredRefreshW + desiredAddAllW + desiredReconcileW +
        desiredAddW + desiredPlayW;
    const int availableRowW = w > 7 * pad ? w - 7 * pad : 0;
    const bool compact = availableRowW < desiredButtonsW;
    const int refreshW = compact ? availableRowW * desiredRefreshW / desiredButtonsW
                                 : desiredRefreshW;
    const int addAllW = compact ? availableRowW * desiredAddAllW / desiredButtonsW
                                : desiredAddAllW;
    const int reconcileW = compact ? availableRowW * desiredReconcileW / desiredButtonsW
                                   : desiredReconcileW;
    const int addW = compact ? availableRowW * desiredAddW / desiredButtonsW
                             : desiredAddW;
    const int allocatedW = refreshW + addAllW + reconcileW + addW;
    const int playW = compact ? (availableRowW > allocatedW
                                    ? availableRowW - allocatedW : 0)
                               : desiredPlayW;
    m_refreshBtn.SetWindowPos(nullptr, pad, btnY, refreshW, btnH, SWP_NOZORDER);
    const int statusX = pad + refreshW + pad;
    const int statusW = compact ? 0 : (availableRowW > desiredButtonsW
                                        ? availableRowW - desiredButtonsW : 0);
    m_status.SetWindowPos(nullptr,
        statusX, btnY + 4, statusW, btnH, SWP_NOZORDER);
    const int addAllX = statusX + statusW + pad;
    m_addAllBtn.SetWindowPos(nullptr, addAllX, btnY, addAllW, btnH, SWP_NOZORDER);
    const int reconcileX = addAllX + addAllW + pad;
    m_reconcileBtn.SetWindowPos(nullptr, reconcileX, btnY, reconcileW, btnH, SWP_NOZORDER);
    const int addX = reconcileX + reconcileW + pad;
    m_addBtn.SetWindowPos(nullptr, addX, btnY, addW, btnH, SWP_NOZORDER);
    m_playBtn.SetWindowPos(nullptr, addX + addW + pad, btnY, playW, btnH, SWP_NOZORDER);
    return 0;
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------
std::shared_ptr<NavidromeNode> BrowserWindow::makeArtistNode(
        const navidrome::Artist& artist) {
    auto node = std::make_shared<NavidromeNode>();
    node->type = NavidromeNode::Artist;
    node->id = artist.id;
    node->displayName = artist.name;
    node->coverArtId = artist.coverArtId;
    node->starred = artist.starred;
    return node;
}

std::shared_ptr<NavidromeNode> BrowserWindow::makeAlbumNode(
        const navidrome::Album& album) {
    auto node = std::make_shared<NavidromeNode>();
    node->type = NavidromeNode::Album;
    node->id = album.id;
    node->displayName = album.name;
    node->subtitle = album.artist;
    node->coverArtId = album.coverArtId;
    node->year = album.year;
    node->starred = album.starred;
    return node;
}

std::shared_ptr<NavidromeNode> BrowserWindow::makeSongNode(
        const navidrome::Song& song) {
    auto node = std::make_shared<NavidromeNode>();
    node->type = NavidromeNode::Song;
    node->id = song.id;
    node->displayName = song.title;
    node->subtitle = song.artist;
    node->album = song.album;
    node->coverArtId = song.coverArtId;
    node->suffix = song.suffix;
    node->track = song.track;
    node->year = song.year;
    node->duration = song.duration;
    node->metadata = song;
    node->childrenLoaded = true;
    return node;
}

std::shared_ptr<NavidromeNode> BrowserWindow::makePlaylistNode(
        const navidrome::ServerPlaylist& playlist) {
    auto node = std::make_shared<NavidromeNode>();
    node->type = NavidromeNode::ServerPlaylist;
    node->id = playlist.id;
    node->displayName = playlist.name;
    node->coverArtId = playlist.coverArtId;
    node->playlist = playlist;
    return node;
}

std::vector<std::shared_ptr<NavidromeNode>> BrowserWindow::fetchChildren(
        const std::shared_ptr<NavidromeNode>& node,
        const navidrome::SubsonicRequestContext& context,
        std::string& outError) {
    std::vector<std::shared_ptr<NavidromeNode>> result;
    auto& client = navidrome::SubsonicClientWin::get();
    if (!node) return result;

    if (node->type == NavidromeNode::Artist) {
        for (const auto& album : client.getAlbumsForArtist(context, node->id, outError))
            result.push_back(makeAlbumNode(album));
    } else if (node->type == NavidromeNode::Album) {
        for (const auto& song : client.getSongsForAlbum(context, node->id, outError))
            result.push_back(makeSongNode(song));
    } else if (node->type == NavidromeNode::ServerPlaylist) {
        const auto details = client.getPlaylist(context, node->id, outError);
        for (const auto& song : details.songs) result.push_back(makeSongNode(song));
    } else if (node->type == NavidromeNode::NavigationGroup &&
               node->navigationGroup == navidrome::NavigationGroupKind::ServerPlaylists) {
        for (const auto& playlist : client.getPlaylists(context, outError))
            result.push_back(makePlaylistNode(playlist));
    } else if (node->type == NavidromeNode::SmartList && node->smartList) {
        switch (*node->smartList) {
        case navidrome::SmartListKind::StarredSongs: {
            const auto starred = client.getStarred(context, outError);
            for (const auto& song : starred.songs) result.push_back(makeSongNode(song));
            break;
        }
        case navidrome::SmartListKind::StarredAlbums: {
            const auto starred = client.getStarred(context, outError);
            for (const auto& album : starred.albums) result.push_back(makeAlbumNode(album));
            break;
        }
        case navidrome::SmartListKind::StarredArtists: {
            const auto starred = client.getStarred(context, outError);
            for (const auto& artist : starred.artists) result.push_back(makeArtistNode(artist));
            break;
        }
        default: {
            auto kind = navidrome::AlbumListKind::Newest;
            if (*node->smartList == navidrome::SmartListKind::FrequentAlbums)
                kind = navidrome::AlbumListKind::Frequent;
            else if (*node->smartList == navidrome::SmartListKind::RecentAlbums)
                kind = navidrome::AlbumListKind::Recent;
            else if (*node->smartList == navidrome::SmartListKind::RandomAlbums)
                kind = navidrome::AlbumListKind::Random;
            for (const auto& album : client.getAlbumList(context, kind, 100, outError))
                result.push_back(makeAlbumNode(album));
            break;
        }
        }
    }
    return result;
}

std::vector<std::shared_ptr<NavidromeNode>> BrowserWindow::buildGroupedRoots() const {
    const auto defaults = navidrome::groupedNavigationDefaults();
    auto smartGroup = std::make_shared<NavidromeNode>();
    smartGroup->type = NavidromeNode::NavigationGroup;
    smartGroup->navigationGroup = navidrome::NavigationGroupKind::SmartLists;
    smartGroup->displayName = navidrome::l10n::smartLists;
    smartGroup->childrenLoaded = true;

    const auto addSmart = [&](navidrome::SmartListKind kind, const char* label) {
        auto node = std::make_shared<NavidromeNode>();
        node->type = NavidromeNode::SmartList;
        node->smartList = kind;
        node->displayName = label;
        smartGroup->children.push_back(std::move(node));
    };
    for (const auto kind : defaults.smartLists) {
        const char* label = navidrome::l10n::starredSongs;
        switch (kind) {
        case navidrome::SmartListKind::StarredSongs:
            label = navidrome::l10n::starredSongs;
            break;
        case navidrome::SmartListKind::StarredAlbums:
            label = navidrome::l10n::starredAlbums;
            break;
        case navidrome::SmartListKind::StarredArtists:
            label = navidrome::l10n::starredArtists;
            break;
        case navidrome::SmartListKind::NewestAlbums:
            label = navidrome::l10n::newestAlbums;
            break;
        case navidrome::SmartListKind::FrequentAlbums:
            label = navidrome::l10n::frequentAlbums;
            break;
        case navidrome::SmartListKind::RecentAlbums:
            label = navidrome::l10n::recentAlbums;
            break;
        case navidrome::SmartListKind::RandomAlbums:
            label = navidrome::l10n::randomAlbums;
            break;
        }
        addSmart(kind, label);
    }

    auto playlistGroup = std::make_shared<NavidromeNode>();
    playlistGroup->type = NavidromeNode::NavigationGroup;
    playlistGroup->navigationGroup = navidrome::NavigationGroupKind::ServerPlaylists;
    playlistGroup->displayName = navidrome::l10n::serverPlaylists;

    auto artistGroup = std::make_shared<NavidromeNode>();
    artistGroup->type = NavidromeNode::NavigationGroup;
    artistGroup->navigationGroup = navidrome::NavigationGroupKind::Artists;
    artistGroup->displayName = navidrome::l10n::artists;
    artistGroup->childrenLoaded = true;
    artistGroup->children = m_libraryRoots;

    std::vector<std::shared_ptr<NavidromeNode>> roots;
    roots.reserve(defaults.groups.size());
    for (const auto group : defaults.groups) {
        if (group == navidrome::NavigationGroupKind::SmartLists)
            roots.push_back(smartGroup);
        else if (group == navidrome::NavigationGroupKind::ServerPlaylists)
            roots.push_back(playlistGroup);
        else
            roots.push_back(artistGroup);
    }
    return roots;
}

void BrowserWindow::displayGroupedNavigation() {
    displayRootNodes(buildGroupedRoots());
    for (const auto& root : m_rootNodes) {
        if (root->navigationGroup == navidrome::NavigationGroupKind::Artists &&
            root->hItem) {
            m_tree.Expand(root->hItem, TVE_EXPAND);
            break;
        }
    }
}

void BrowserWindow::loadArtists() {
    setStatus(navidrome::l10n::loadingArtists);
    m_libraryLoading = true;
    m_libraryRoots.clear();
    displayGroupedNavigation();
    updateActionState();

    auto context = navidrome::SubsonicClientWin::get().snapshot();
    bindMutationIdentity(context);
    // A manual/account refresh makes the server response authoritative again.
    // Mutations confirmed after this point are cached while the request is in flight
    // and are re-applied when its nodes arrive.
    m_confirmedMutations.clear();
    m_appliedMutationRevisions.clear();
    const std::uint64_t requestId = ++m_libraryRequestId;
    auto dispatch = m_dispatchState;
    std::thread([dispatch, requestId, context = std::move(context)]() {
        auto* payload = new LoadedPayload{};
        payload->source = LoadedPayload::Source::Library;
        payload->requestId = requestId;
        std::string err;
        auto artists = navidrome::SubsonicClientWin::get().getArtists(context, err);
        payload->error = err;
        for (const auto& artist : artists)
            payload->nodes.push_back(BrowserWindow::makeArtistNode(artist));
        dispatchBrowserPayload(dispatch, WM_NAVIDROME_LOADED, payload);
    }).detach();
}

LRESULT BrowserWindow::OnNavidromeLoaded(UINT, WPARAM wParam, LPARAM, BOOL&) {
    std::unique_ptr<LoadedPayload> payload(
        reinterpret_cast<LoadedPayload*>(wParam));
    const bool current = payload->source == LoadedPayload::Source::Library
        ? payload->requestId == m_libraryRequestId
        : payload->requestId == m_searchRequestId;
    if (current) populateRoot(payload.get());
    return 0;
}

LRESULT BrowserWindow::OnNavidromeChildren(UINT, WPARAM wParam, LPARAM, BOOL&) {
    std::unique_ptr<LoadedPayload> payload(
        reinterpret_cast<LoadedPayload*>(wParam));
    if (!payload->parent ||
        payload->requestId != payload->parent->childRequestId)
        return 0;
    if (payload->displayGeneration != m_displayGeneration) {
        if (payload->parent) payload->parent->isLoading = false;
        return 0;
    }
    if (m_queueInProgress) {
        m_deferredChildren.push_back(std::move(payload));
        return 0;
    }
    populateChildren(payload.get());
    return 0;
}

void BrowserWindow::populateRoot(LoadedPayload* payload) {
    const bool isLibrary = payload->source == LoadedPayload::Source::Library;
    if (isLibrary) m_libraryLoading = false;

    if (!payload->error.empty()) {
        if (isLibrary) m_libraryRoots.clear();
        updateActionState();
        if (!m_queueInProgress)
            setStatus(navidrome::l10n::error(payload->error));
        return;
    }

    if (isLibrary) {
        applyKnownMutations(payload->nodes);
        m_libraryRoots = payload->nodes;
        updateActionState();
        if (m_searchQuery.size() < 2) {
            displayGroupedNavigation();
            if (!m_queueInProgress)
                setStatus(navidrome::l10n::artistCount(m_libraryRoots.size()));
        }
        return;
    }

    applyKnownMutations(payload->nodes);
    displayRootNodes(payload->nodes);
    if (!m_queueInProgress)
        setStatus(navidrome::l10n::searchResultCount(payload->nodes.size()));
}

void BrowserWindow::displayRootNodes(
        const std::vector<std::shared_ptr<NavidromeNode>>& nodes) {
    ++m_displayGeneration;
    m_tree.DeleteAllItems();
    m_nodeMap.clear();
    m_rootNodes = nodes;
    for (auto& node : m_rootNodes)
        insertNodeTree(TVI_ROOT, node);
}

void BrowserWindow::populateChildren(LoadedPayload* payload) {
    auto parent = payload->parent;
    if (!parent) return;

    // Remove placeholder "Loading..." item
    removeLoadingChildren(parent);

    parent->isLoading      = false;
    parent->childrenLoaded = true;
    applyKnownMutations(payload->nodes);
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

void BrowserWindow::applyDeferredChildren() {
    for (auto& payload : m_deferredChildren) {
        if (payload->displayGeneration == m_displayGeneration)
            populateChildren(payload.get());
        else if (payload->parent)
            payload->parent->isLoading = false;
    }
    m_deferredChildren.clear();
}

void BrowserWindow::applyDeferredMutations() {
    auto events = std::move(m_deferredMutationEvents);
    m_deferredMutationEvents.clear();
    for (const auto& event : events) applyMutationProjection(event);
}

void BrowserWindow::forgetTreeBranch(HTREEITEM item) {
    if (!item) return;
    HTREEITEM child = m_tree.GetChildItem(item);
    while (child) {
        const HTREEITEM next = m_tree.GetNextSiblingItem(child);
        forgetTreeBranch(child);
        child = next;
    }
    const auto visible = m_nodeMap.find(item);
    if (visible != m_nodeMap.end()) {
        ++visible->second->childRequestId;
        visible->second->isLoading = false;
        visible->second->hItem = nullptr;
        m_nodeMap.erase(visible);
    }
}

void BrowserWindow::clearNodeChildren(
        const std::shared_ptr<NavidromeNode>& parent) {
    if (!parent) return;
    const auto visible = parent->hItem ? m_nodeMap.find(parent->hItem)
                                       : m_nodeMap.end();
    if (visible != m_nodeMap.end() && visible->second.get() == parent.get()) {
        HTREEITEM child = m_tree.GetChildItem(parent->hItem);
        while (child) {
            const HTREEITEM next = m_tree.GetNextSiblingItem(child);
            forgetTreeBranch(child);
            m_tree.DeleteItem(child);
            child = next;
        }
    }
    parent->children.clear();
}

void BrowserWindow::startChildLoad(
        const std::shared_ptr<NavidromeNode>& node,
        navidrome::SubsonicRequestContext context, bool replaceExisting) {
    if (!node) return;
    if (replaceExisting) clearNodeChildren(node);
    node->childrenLoaded = false;
    node->isLoading = true;
    const std::uint64_t requestId = ++node->childRequestId;

    const auto visible = node->hItem ? m_nodeMap.find(node->hItem) : m_nodeMap.end();
    if (visible != m_nodeMap.end() && visible->second.get() == node.get()) {
        auto loadNode = std::make_shared<NavidromeNode>();
        loadNode->type = NavidromeNode::Loading;
        loadNode->displayName = navidrome::l10n::loading;
        insertNode(node->hItem, std::move(loadNode));
    }

    const std::uint64_t displayGeneration = m_displayGeneration;
    auto dispatch = m_dispatchState;
    std::thread([dispatch, node, requestId, displayGeneration,
                 context = std::move(context)]() {
        auto* payload = new LoadedPayload{};
        payload->parent = node;
        payload->requestId = requestId;
        payload->displayGeneration = displayGeneration;
        std::string error;
        payload->nodes = BrowserWindow::fetchChildren(node, context, error);
        payload->error = std::move(error);
        dispatchBrowserPayload(dispatch, WM_NAVIDROME_CHILDREN, payload);
    }).detach();
}

void BrowserWindow::refreshFavoriteSmartList(navidrome::FavoriteKind kind) {
    const auto target = navidrome::favoriteSmartListKind(kind);
    for (const auto& root : m_rootNodes) {
        for (const auto& node : root->children) {
            if (node->type != NavidromeNode::SmartList ||
                node->smartList != target ||
                (!node->childrenLoaded && !node->isLoading))
                continue;
            auto context = navidrome::SubsonicClientWin::get().snapshot();
            if (navidrome::serverAccountIdentity(
                    context.serverUrl, context.username) != m_mutationIdentity)
                return;
            startChildLoad(node, std::move(context), true);
            return;
        }
    }
}

void BrowserWindow::refreshServerPlaylistCatalog() {
    for (const auto& node : m_rootNodes) {
        if (node->type != NavidromeNode::NavigationGroup ||
            node->navigationGroup != navidrome::NavigationGroupKind::ServerPlaylists ||
            (!node->childrenLoaded && !node->isLoading))
            continue;
        auto context = navidrome::SubsonicClientWin::get().snapshot();
        if (navidrome::serverAccountIdentity(
                context.serverUrl, context.username) != m_mutationIdentity)
            return;
        startChildLoad(node, std::move(context), true);
        return;
    }
}

void BrowserWindow::removeLoadingChildren(
        const std::shared_ptr<NavidromeNode>& parent) {
    if (!parent || !parent->hItem) return;
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
}

HTREEITEM BrowserWindow::insertNode(HTREEITEM hParent,
                                    std::shared_ptr<NavidromeNode> node) {
    const std::string label = nodeLabel(node);

    TVINSERTSTRUCT tvi    = {};
    tvi.hParent           = hParent;
    tvi.hInsertAfter      = TVI_LAST;
    tvi.item.mask         = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    auto wlabel           = u8ToWide(label);
    tvi.item.pszText      = const_cast<LPWSTR>(wlabel.c_str());
    tvi.item.lParam       = reinterpret_cast<LPARAM>(node.get());
    const bool container = navidrome::isBrowserContainer(node->type);
    tvi.item.cChildren = container &&
        (!node->childrenLoaded || !node->children.empty()) ? 1 : 0;

    HTREEITEM hItem = m_tree.InsertItem(&tvi);
    node->hItem = hItem;
    m_nodeMap[hItem] = node;
    return hItem;
}

HTREEITEM BrowserWindow::insertNodeTree(
        HTREEITEM hParent, std::shared_ptr<NavidromeNode> node) {
    HTREEITEM hItem = insertNode(hParent, node);
    if (node->childrenLoaded) {
        for (auto& child : node->children)
            insertNodeTree(hItem, child);
    }
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
    if (isBusy()) return TRUE;
    auto* pnm = reinterpret_cast<LPNMTREEVIEW>(pnmh);
    if (pnm->action != TVE_EXPAND) return 0;

    auto node = nodeForItem(pnm->itemNew.hItem);
    if (!node || node->childrenLoaded || node->isLoading) return 0;
    auto context = navidrome::SubsonicClientWin::get().snapshot();
    if (!ensureCurrentAccount(context)) return TRUE;
    startChildLoad(node, std::move(context), false);

    return 0;
}

LRESULT BrowserWindow::OnTreeDblClick(LPNMHDR) {
    if (isBusy()) return 0;
    HTREEITEM hSel = m_tree.GetSelectedItem();
    if (!hSel) return 0;
    auto node = nodeForItem(hSel);
    if (!node) return 0;
    if (navidrome::isBrowserPlayable(node->type) &&
        !navidrome::isBrowserContainer(node->type))
        queueNodes({ node }, true, false, false);
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
            if (n && navidrome::isBrowserPlayable(n->type))
                selected.push_back(n);
        }
        hItem = m_tree.GetNextVisibleItem(hItem);
    }
    return selected;
}

void BrowserWindow::queueSelected(bool play, bool closeAfter) {
    auto selected = selectedNodes();
    if (selected.empty()) { setStatus(navidrome::l10n::selectAtLeastOne); return; }
    queueNodes(std::move(selected), play, closeAfter, false);
}

// Resolve roots on a background thread, then enqueue once on the main thread.
// The worker captures shared dispatch state rather than a BrowserWindow pointer.
void BrowserWindow::queueNodes(
        std::vector<std::shared_ptr<NavidromeNode>> roots,
        bool play, bool closeAfter, bool reportRootProgress) {
    if (isBusy()) {
        setStatus(navidrome::l10n::queueBusy);
        return;
    }
    if (roots.empty()) {
        setStatus(navidrome::l10n::noSongsSelected);
        return;
    }

    auto context = navidrome::SubsonicClientWin::get().snapshot();
    if (!ensureCurrentAccount(context)) return;
    m_queueInProgress = true;
    const std::uint64_t operationId = ++m_queueOperationId;
    m_queueCancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_queueCancel;
    auto dispatch = m_dispatchState;
    const std::size_t totalRoots = roots.size();

    updateActionState();
    setStatus(reportRootProgress
        ? navidrome::l10n::importProgress(0, totalRoots, 0, 0)
        : navidrome::l10n::loadingTracks);

    std::thread([dispatch, cancel, operationId, roots = std::move(roots),
                 context = std::move(context),
                 totalRoots, play, closeAfter, reportRootProgress]() mutable {
        std::vector<std::shared_ptr<NavidromeNode>> songs;
        std::size_t failedItems = 0;
        std::size_t completedRoots = 0;

        for (auto& root : roots) {
            if (cancellationRequested(cancel)) break;
            BrowserWindow::collectSongsDeep(
                root, songs, failedItems, cancel, context);
            if (cancellationRequested(cancel)) break;
            ++completedRoots;

            if (reportRootProgress) {
                auto* progress = new QueueProgressPayload{};
                progress->operationId = operationId;
                progress->completedRoots = completedRoots;
                progress->totalRoots = totalRoots;
                progress->songCount = songs.size();
                progress->failedItems = failedItems;
                dispatchBrowserPayload(dispatch, WM_NAVIDROME_QUEUE_PROGRESS,
                                       progress);
            }
        }

        auto* complete = new QueueCompletePayload{};
        complete->operationId = operationId;
        complete->songs = std::move(songs);
        complete->failedItems = failedItems;
        complete->cancelled = cancellationRequested(cancel);
        complete->play = play;
        complete->closeAfter = closeAfter;
        dispatchBrowserPayload(dispatch, WM_NAVIDROME_QUEUE_COMPLETE, complete);
    }).detach();
}

void BrowserWindow::OnAdd(UINT, int, HWND)  { queueSelected(false, false); }
void BrowserWindow::OnAddAll(UINT, int, HWND) {
    importLibrary(false);
}
void BrowserWindow::OnReconcile(UINT, int, HWND) { importLibrary(true); }
void BrowserWindow::OnPlay(UINT, int, HWND) { queueSelected(true,  false); }

void BrowserWindow::importLibrary(bool forceFull) {
    if (isBusy()) {
        setStatus(navidrome::l10n::queueBusy);
        return;
    }
    auto context = navidrome::SubsonicClientWin::get().snapshot();
    if (!ensureCurrentAccount(context)) return;
    if (context.serverUrl.empty() || context.username.empty() || context.password.empty()) {
        setStatus(navidrome::l10n::libraryNotLoaded);
        return;
    }
    m_queueInProgress = true;
    const std::uint64_t operationId = ++m_queueOperationId;
    m_queueCancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_queueCancel;
    auto dispatch = m_dispatchState;
    updateActionState();
    setStatus(forceFull ? navidrome::l10n::reconcilingLibrary
                        : navidrome::l10n::checkingNewTracks);

    std::thread([dispatch, cancel, context = std::move(context),
                 operationId, forceFull]() mutable {
        auto progress = [dispatch, operationId](
                const navidrome::LibraryImportProgress& info) {
            auto* payload = new LibraryProgressPayload{};
            payload->operationId = operationId;
            payload->progress = info;
            dispatchBrowserPayload(dispatch, WM_NAVIDROME_LIBRARY_PROGRESS, payload);
        };
        auto* complete = new LibraryCompletePayload{};
        complete->operationId = operationId;
        complete->result = navidrome::runLibraryImport(
            context, forceFull, operationId, cancel, progress);
        dispatchBrowserPayload(dispatch, WM_NAVIDROME_LIBRARY_COMPLETE, complete);
    }).detach();
}

LRESULT BrowserWindow::OnLibraryProgress(UINT, WPARAM wParam, LPARAM, BOOL&) {
    std::unique_ptr<LibraryProgressPayload> payload(
        reinterpret_cast<LibraryProgressPayload*>(wParam));
    if (!m_queueInProgress || payload->operationId != m_queueOperationId) return 0;
    const auto& info = payload->progress;
    setStatus(info.recursive
        ? navidrome::l10n::importProgress(
            info.completed, info.total, info.scanned, info.failed)
        : navidrome::l10n::pageProgress(info.scanned, info.added));
    return 0;
}

LRESULT BrowserWindow::OnLibraryComplete(UINT, WPARAM wParam, LPARAM, BOOL&) {
    std::unique_ptr<LibraryCompletePayload> payload(
        reinterpret_cast<LibraryCompletePayload*>(wParam));
    if (!m_queueInProgress || payload->operationId != m_queueOperationId) return 0;
    m_queueInProgress = false;
    m_queueCancel.reset();
    applyDeferredChildren();
    applyDeferredMutations();
    updateActionState();
    auto& result = payload->result;
    if (result.cancelled) return 0;
    if (!result.error.empty() || !result.preparedState) {
        setStatus(navidrome::l10n::error(result.error.empty()
            ? navidrome::l10n::stateNotPrepared : result.error));
        return 0;
    }

    std::vector<std::shared_ptr<NavidromeNode>> nodes;
    nodes.reserve(result.candidates.size());
    for (const auto& song : result.candidates) {
        nodes.push_back(makeSongNode(song));
    }
    PlaylistAppendReceipt receipt;
    if (!nodes.empty()) {
        receipt = enqueueNodes(std::move(nodes), false);
        if (!receipt.success || receipt.count != result.candidates.size()) {
            if (receipt.success) rollbackAppend(receipt);
            setStatus(navidrome::l10n::noSongsFound);
            return 0;
        }
    }

    auto commitResult = navidrome::commitWithPlaylistCompensation(
        receipt.count != 0,
        [&result](std::string& error) {
            return result.preparedState->commit(error);
        },
        [this, &receipt]() { return rollbackAppend(receipt); });
    if (!commitResult.committed) {
        setStatus(commitResult.rolledBack
            ? navidrome::l10n::stateCommitRolledBack(commitResult.error)
            : navidrome::l10n::stateRollbackFailed);
        return 0;
    }
    setStatus(receipt.count == 0 ? navidrome::l10n::libraryUpToDate
                                 : navidrome::l10n::addedTracks(receipt.count));
    return 0;
}

LRESULT BrowserWindow::OnQueueProgress(UINT, WPARAM wParam, LPARAM, BOOL&) {
    std::unique_ptr<QueueProgressPayload> payload(
        reinterpret_cast<QueueProgressPayload*>(wParam));
    if (!m_queueInProgress || payload->operationId != m_queueOperationId)
        return 0;
    setStatus(navidrome::l10n::importProgress(
        payload->completedRoots, payload->totalRoots,
        payload->songCount, payload->failedItems));
    return 0;
}

LRESULT BrowserWindow::OnQueueComplete(UINT, WPARAM wParam, LPARAM, BOOL&) {
    std::unique_ptr<QueueCompletePayload> payload(
        reinterpret_cast<QueueCompletePayload*>(wParam));
    if (!m_queueInProgress || payload->operationId != m_queueOperationId)
        return 0;

    m_queueInProgress = false;
    m_queueCancel.reset();
    applyDeferredChildren();
    applyDeferredMutations();
    updateActionState();
    if (payload->cancelled) return 0;

    if (payload->songs.empty()) {
        setStatus(payload->failedItems > 0
            ? navidrome::l10n::allItemsFailed(payload->failedItems)
            : navidrome::l10n::noSongsFound);
        return 0;
    }

    const auto receipt = enqueueNodes(std::move(payload->songs), payload->play);
    const std::size_t added = receipt.success ? receipt.count : 0;
    if (!receipt.success || added == 0) {
        setStatus(payload->failedItems > 0
            ? navidrome::l10n::allItemsFailed(payload->failedItems)
            : navidrome::l10n::noSongsFound);
        return 0;
    }
    if (payload->failedItems > 0)
        setStatus(navidrome::l10n::addedTracksWithFailures(
            added, payload->failedItems));
    if (added > 0 && payload->closeAfter && !m_embedded && IsWindow())
        ShowWindow(SW_HIDE);
    return 0;
}

// Enter in the tree = add the selected item(s) to the playlist, start playing
// the first track, and close the window. A quick "queue this artist, play it,
// and get out of my way" shortcut.
LRESULT BrowserWindow::OnTreeReturn(LPNMHDR) {
    if (isBusy()) return 0;
    queueSelected(true, true);
    return 0;
}

std::string BrowserWindow::nodeLabel(
        const std::shared_ptr<NavidromeNode>& node) const {
    if (!node) return {};
    if (node->type == NavidromeNode::Song) {
        auto metadata = node->metadata;
        if (metadata.title.empty()) metadata.title = node->displayName;
        if (metadata.track == 0) metadata.track = node->track;
        return navidrome::formatSongLabel(metadata, node->displayName);
    }
    if (node->type == NavidromeNode::ServerPlaylist)
        return navidrome::formatPlaylistLabel(node->playlist);
    if ((node->type == NavidromeNode::Artist ||
         node->type == NavidromeNode::Album) && node->starred)
        return u8"★ " + node->displayName;
    return node->displayName;
}

void BrowserWindow::refreshNodeLabel(
        const std::shared_ptr<NavidromeNode>& node) {
    if (!node || !node->hItem) return;
    const auto visible = m_nodeMap.find(node->hItem);
    if (visible == m_nodeMap.end() || visible->second.get() != node.get())
        return;
    const auto label = u8ToWide(nodeLabel(node));
    m_tree.SetItemText(node->hItem, label.c_str());
}

void BrowserWindow::bindMutationIdentity(
        const navidrome::SubsonicRequestContext& context) {
    const auto identity = navidrome::serverAccountIdentity(
        context.serverUrl, context.username);
    if (identity == m_mutationIdentity) return;
    m_mutationIdentity = identity;
    m_confirmedMutations.clear();
    m_appliedMutationRevisions.clear();
}

bool BrowserWindow::ensureCurrentAccount(
        const navidrome::SubsonicRequestContext& context) {
    const auto identity = navidrome::serverAccountIdentity(
        context.serverUrl, context.username);
    if (m_mutationIdentity.empty()) {
        bindMutationIdentity(context);
        return true;
    }
    if (identity == m_mutationIdentity) return true;
    PostMessage(WM_COMMAND, MAKEWPARAM(IDC_REFRESH, BN_CLICKED), 0);
    setStatus(navidrome::l10n::accountChangedRefreshing);
    return false;
}

void BrowserWindow::applyMutationToNode(
        const navidrome::BrowserMutationEvent& event,
        const std::shared_ptr<NavidromeNode>& node) {
    const auto kind = favoriteKindForNode(node);
    if (!kind || *kind != event.entityKind || node->id != event.entityId) return;
    if (event.kind == navidrome::BrowserMutationKind::FavoriteChanged) {
        const std::optional<std::string> value = event.favorite
            ? std::optional<std::string>("1") : std::nullopt;
        if (node->type == NavidromeNode::Song) node->metadata.starred = value;
        else node->starred = value;
    } else if (event.kind == navidrome::BrowserMutationKind::RatingChanged &&
               node->type == NavidromeNode::Song) {
        node->metadata.userRating = event.rating > 0
            ? std::optional<int>(event.rating) : std::nullopt;
    }
    refreshNodeLabel(node);
}

void BrowserWindow::applyKnownMutations(
        const std::vector<std::shared_ptr<NavidromeNode>>& nodes) {
    for (const auto& node : nodes) {
        if (const auto kind = favoriteKindForNode(node)) {
            const auto favoriteIt = m_confirmedMutations.find(mutationKey(
                navidrome::BrowserMutationKind::FavoriteChanged, *kind, node->id));
            if (favoriteIt != m_confirmedMutations.end())
                applyMutationToNode(favoriteIt->second, node);
            if (*kind == navidrome::FavoriteKind::Song) {
                const auto ratingIt = m_confirmedMutations.find(mutationKey(
                    navidrome::BrowserMutationKind::RatingChanged, *kind, node->id));
                if (ratingIt != m_confirmedMutations.end())
                    applyMutationToNode(ratingIt->second, node);
            }
        }
        applyKnownMutations(node->children);
    }
}

void BrowserWindow::applyMutationEvent(
        const navidrome::BrowserMutationEvent& event) {
    if (event.identity != m_mutationIdentity) return;
    if (event.kind == navidrome::BrowserMutationKind::PlaylistCatalogChanged) {
        const std::string key = "playlist-catalog";
        if (event.revision <= m_appliedMutationRevisions[key]) return;
        m_appliedMutationRevisions[key] = event.revision;
    } else {
        const auto key = mutationKey(event.kind, event.entityKind, event.entityId);
        if (!navidrome::shouldApplyBrowserMutation(
                event, m_mutationIdentity, m_appliedMutationRevisions[key])) return;
        m_appliedMutationRevisions[key] = event.revision;
        m_confirmedMutations[key] = event;
    }

    if (m_queueInProgress) {
        m_deferredMutationEvents.push_back(event);
        return;
    }
    applyMutationProjection(event);
}

void BrowserWindow::applyMutationProjection(
        const navidrome::BrowserMutationEvent& event) {
    if (event.identity != m_mutationIdentity) return;
    if (event.kind == navidrome::BrowserMutationKind::PlaylistCatalogChanged) {
        refreshServerPlaylistCatalog();
        return;
    }

    std::set<NavidromeNode*> visited;
    const auto applyTree = [&](const auto& self,
                               const std::shared_ptr<NavidromeNode>& node) -> void {
        if (!node || !visited.insert(node.get()).second) return;
        applyMutationToNode(event, node);
        for (const auto& child : node->children) self(self, child);
    };
    for (const auto& root : m_libraryRoots) applyTree(applyTree, root);
    for (const auto& root : m_rootNodes) applyTree(applyTree, root);
    if (event.kind == navidrome::BrowserMutationKind::FavoriteChanged)
        refreshFavoriteSmartList(event.entityKind);
}

void BrowserWindow::startMutation(
        const std::shared_ptr<NavidromeNode>& node,
        navidrome::BrowserMutationKind kind, bool favorite, int rating) {
    const auto entityKind = favoriteKindForNode(node);
    if (!entityKind || isBusy()) return;
    auto context = navidrome::SubsonicClientWin::get().snapshot();
    if (!ensureCurrentAccount(context)) return;
    const auto identity = m_mutationIdentity;
    const auto operationId = ++m_mutationOperationId;
    m_mutationInProgress = true;
    updateActionState();
    setStatus(kind == navidrome::BrowserMutationKind::FavoriteChanged
        ? navidrome::l10n::updatingFavorite : navidrome::l10n::updatingRating);
    auto dispatch = m_dispatchState;
    const auto entityId = node->id;
    std::thread([dispatch, context = std::move(context), identity,
                 operationId, kind, entityKind = *entityKind, entityId,
                 favorite, rating]() {
        auto* payload = new MutationCompletePayload{};
        payload->operationId = operationId;
        payload->identity = identity;
        payload->kind = kind;
        payload->entityKind = entityKind;
        payload->entityId = entityId;
        payload->favorite = favorite;
        payload->rating = rating;
        if (kind == navidrome::BrowserMutationKind::FavoriteChanged) {
            payload->success = navidrome::SubsonicClientWin::get().setFavorite(
                context, entityKind, entityId, favorite, payload->error);
        } else {
            payload->success = navidrome::SubsonicClientWin::get().setRating(
                context, entityId, rating, payload->error);
        }
        dispatchBrowserPayload(dispatch, WM_NAVIDROME_MUTATION_COMPLETE, payload);
    }).detach();
}

LRESULT BrowserWindow::OnMutationComplete(
        UINT, WPARAM wParam, LPARAM, BOOL&) {
    std::unique_ptr<MutationCompletePayload> payload(
        reinterpret_cast<MutationCompletePayload*>(wParam));
    if (!m_mutationInProgress || payload->operationId != m_mutationOperationId)
        return 0;
    m_mutationInProgress = false;
    updateActionState();
    if (payload->identity != m_mutationIdentity) return 0;
    if (!payload->success) {
        setStatus(navidrome::l10n::error(payload->error));
        return 0;
    }
    navidrome::BrowserMutationEvent event;
    event.identity = payload->identity;
    event.kind = payload->kind;
    event.entityKind = payload->entityKind;
    event.entityId = payload->entityId;
    event.favorite = payload->favorite;
    event.rating = payload->rating;
    navidrome::BrowserMutationHub::get().publish(std::move(event));
    setStatus(payload->kind == navidrome::BrowserMutationKind::FavoriteChanged
        ? navidrome::l10n::favoriteUpdated : navidrome::l10n::ratingUpdated);
    return 0;
}

void BrowserWindow::OnContextMenu(CWindow wnd, CPoint point) {
    if (wnd.m_hWnd != m_tree.m_hWnd) { SetMsgHandled(FALSE); return; }
    if (isBusy()) return;

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

    auto node = nodeForItem(m_tree.GetSelectedItem());
    if (!node) return;

    CMenu menu;
    menu.CreatePopupMenu();
    if (navidrome::isBrowserPlayable(node->type)) {
        menu.AppendMenu(MF_STRING, IDC_PLAY, navidrome::l10n::playNow);
        menu.AppendMenu(MF_STRING, IDC_ADD, navidrome::l10n::addToPlaylist);
    }
    if (favoriteKindForNode(node)) {
        menu.AppendMenu(MF_SEPARATOR);
        const bool starred = node->type == NavidromeNode::Song
            ? node->metadata.starred.has_value() : node->starred.has_value();
        menu.AppendMenu(MF_STRING | (starred ? MF_CHECKED : MF_UNCHECKED),
                        IDC_FAVORITE, navidrome::l10n::favorite);
    }
    if (node->type == NavidromeNode::Song) {
        CMenu rating;
        rating.CreatePopupMenu();
        rating.AppendMenu(MF_STRING, IDC_RATE_0, navidrome::l10n::clearRating);
        for (int value = 1; value <= 5; ++value) {
            const auto label = std::to_wstring(value) + L" / 5";
            rating.AppendMenu(MF_STRING, IDC_RATE_0 + value, label.c_str());
        }
        const int current = node->metadata.userRating.value_or(0);
        rating.CheckMenuRadioItem(IDC_RATE_0, IDC_RATE_5,
                                  IDC_RATE_0 + current, MF_BYCOMMAND);
        menu.AppendMenu(MF_POPUP, reinterpret_cast<UINT_PTR>(rating.m_hMenu),
                        navidrome::l10n::rating);
        rating.Detach();
    }
    if (node->type == NavidromeNode::NavigationGroup &&
        node->navigationGroup == navidrome::NavigationGroupKind::ServerPlaylists) {
        if (menu.GetMenuItemCount() > 0) menu.AppendMenu(MF_SEPARATOR);
        menu.AppendMenu(MF_STRING, IDC_UPLOAD_PLAYLIST,
                        navidrome::l10n::uploadActivePlaylist);
    }
    if (menu.GetMenuItemCount() == 0) return;
    menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, *this);
}

void BrowserWindow::OnFavorite(UINT, int, HWND) {
    auto node = nodeForItem(m_tree.GetSelectedItem());
    if (!favoriteKindForNode(node)) {
        setStatus(navidrome::l10n::selectFavoriteTarget);
        return;
    }
    const bool starred = node->type == NavidromeNode::Song
        ? node->metadata.starred.has_value() : node->starred.has_value();
    startMutation(node, navidrome::BrowserMutationKind::FavoriteChanged,
                  !starred, 0);
}

void BrowserWindow::OnRate(UINT, int commandId, HWND) {
    auto node = nodeForItem(m_tree.GetSelectedItem());
    if (!node || node->type != NavidromeNode::Song) {
        setStatus(navidrome::l10n::selectRatingTarget);
        return;
    }
    const int rating = commandId - IDC_RATE_0;
    if (rating < 0 || rating > 5) return;
    startMutation(node, navidrome::BrowserMutationKind::RatingChanged,
                  false, rating);
}

void BrowserWindow::OnUploadActivePlaylist(UINT, int, HWND) {
    if (isBusy()) return;
    auto manager = playlist_manager::get();
    const t_size active = manager->get_active_playlist();
    if (active == pfc_infinite) {
        setStatus(navidrome::l10n::noActivePlaylist);
        return;
    }

    pfc::string8 name;
    manager->playlist_get_name(active, name);
    metadb_handle_list items;
    manager->playlist_get_all_items(active, items);
    std::vector<std::string> paths;
    paths.reserve(static_cast<std::size_t>(items.get_count()));
    for (t_size index = 0; index < items.get_count(); ++index)
        paths.emplace_back(items[index]->get_path());
    const auto mapping = navidrome::mapActivePlaylistUris(paths);
    if (mapping.orderedSongIds.empty()) {
        setStatus(navidrome::l10n::noNavidromeTracks);
        return;
    }

    auto context = navidrome::SubsonicClientWin::get().snapshot();
    if (!ensureCurrentAccount(context)) return;
    const auto operationId = ++m_playlistOperationId;
    m_playlistInProgress = true;
    updateActionState();
    setStatus(navidrome::l10n::loadingServerPlaylists);
    auto dispatch = m_dispatchState;
    const std::string playlistName = name.is_empty() ? "foobar2000" : name.c_str();
    const auto sourceItemCount = static_cast<std::size_t>(items.get_count());
    const auto identity = m_mutationIdentity;
    std::thread([dispatch, operationId, context = std::move(context), identity,
                 playlistName, mapping, sourceItemCount]() mutable {
        auto* payload = new PlaylistCatalogPayload{};
        payload->operationId = operationId;
        payload->context = context;
        payload->identity = identity;
        payload->activePlaylistName = playlistName;
        payload->mapping = mapping;
        payload->sourceItemCount = sourceItemCount;
        payload->catalog = navidrome::SubsonicClientWin::get().getPlaylists(
            context, payload->error);
        if (payload->error.empty()) {
            std::string capabilityError;
            payload->capabilities = navidrome::SubsonicClientWin::get()
                .getOpenSubsonicCapabilities(context, capabilityError);
        }
        dispatchBrowserPayload(dispatch, WM_NAVIDROME_PLAYLIST_CATALOG, payload);
    }).detach();
}

LRESULT BrowserWindow::OnPlaylistCatalog(
        UINT, WPARAM wParam, LPARAM, BOOL&) {
    std::unique_ptr<PlaylistCatalogPayload> payload(
        reinterpret_cast<PlaylistCatalogPayload*>(wParam));
    if (!m_playlistInProgress || payload->operationId != m_playlistOperationId)
        return 0;
    if (payload->identity != m_mutationIdentity) {
        m_playlistInProgress = false;
        updateActionState();
        return 0;
    }
    if (!payload->error.empty()) {
        m_playlistInProgress = false;
        updateActionState();
        setStatus(navidrome::l10n::error(payload->error));
        return 0;
    }

    const auto matches = navidrome::exactNameMatches(
        payload->catalog, payload->activePlaylistName);
    const auto content = u8ToWide(navidrome::l10n::uploadConfirmation(
        payload->activePlaylistName,
        navidrome::normalizeServerUrl(payload->context.serverUrl),
        payload->context.username, payload->mapping.orderedSongIds.size(),
        payload->mapping.skippedCount()));
    const auto title = navidrome::l10n::uploadDialogTitle;
    const std::wstring mainInstruction = matches.empty()
        ? navidrome::l10n::uploadCreateInstruction
        : navidrome::l10n::uploadCollisionInstruction;

    constexpr int kCreateButton = 2101;
    constexpr int kReplaceButton = 2102;
    constexpr int kCopyButton = 2103;
    constexpr int kFirstPlaylistRadio = 2200;
    std::vector<std::wstring> radioLabels;
    std::vector<TASKDIALOG_BUTTON> radioButtons;
    radioLabels.reserve(matches.size());
    radioButtons.reserve(matches.size());
    for (std::size_t index = 0; index < matches.size(); ++index) {
        std::wstring label = u8ToWide(matches[index].name);
        if (!matches[index].owner.empty())
            label += L" — " + u8ToWide(matches[index].owner);
        label += navidrome::l10n::playlistChoiceSongCount(matches[index].songCount);
        radioLabels.push_back(std::move(label));
    }
    for (std::size_t index = 0; index < radioLabels.size(); ++index)
        radioButtons.push_back({kFirstPlaylistRadio + static_cast<int>(index),
                                radioLabels[index].c_str()});

    TASKDIALOG_BUTTON actionButtons[2] = {};
    UINT actionCount = 0;
    if (matches.empty()) {
        actionButtons[0] = {kCreateButton, navidrome::l10n::uploadCreate};
        actionCount = 1;
    } else {
        actionButtons[0] = {kReplaceButton, navidrome::l10n::uploadReplace};
        actionButtons[1] = {kCopyButton, navidrome::l10n::uploadNumberedCopy};
        actionCount = 2;
    }

    TASKDIALOGCONFIG config = {};
    config.cbSize = sizeof(config);
    config.hwndParent = *this;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION |
                     TDF_POSITION_RELATIVE_TO_WINDOW;
    config.pszWindowTitle = title;
    config.pszMainInstruction = mainInstruction.c_str();
    config.pszContent = content.c_str();
    config.cButtons = actionCount;
    config.pButtons = actionButtons;
    config.nDefaultButton = matches.empty() ? kCreateButton : kCopyButton;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.cRadioButtons = static_cast<UINT>(radioButtons.size());
    config.pRadioButtons = radioButtons.empty() ? nullptr : radioButtons.data();
    config.nDefaultRadioButton = matches.empty() ? 0 : kFirstPlaylistRadio;

    int pressed = IDCANCEL;
    int selectedRadio = matches.empty() ? 0 : kFirstPlaylistRadio;
    const HRESULT dialogResult = TaskDialogIndirect(
        &config, &pressed, &selectedRadio, nullptr);

    navidrome::UploadChoice choice = navidrome::UploadChoice::Cancel;
    std::optional<std::string> selectedId;
    if (SUCCEEDED(dialogResult)) {
        if (pressed == kCreateButton) choice = navidrome::UploadChoice::Create;
        else if (pressed == kCopyButton) choice = navidrome::UploadChoice::NumberedCopy;
        else if (pressed == kReplaceButton) {
            choice = navidrome::UploadChoice::Replace;
            const int matchIndex = selectedRadio - kFirstPlaylistRadio;
            if (matchIndex >= 0 && static_cast<std::size_t>(matchIndex) < matches.size())
                selectedId = matches[static_cast<std::size_t>(matchIndex)].id;
        }
    }

    auto plan = navidrome::makeUploadPlan(
        payload->activePlaylistName, payload->mapping, choice, selectedId,
        payload->catalog, payload->sourceItemCount);
    if (plan.status != navidrome::UploadPlanStatus::Ready) {
        m_playlistInProgress = false;
        updateActionState();
        setStatus(plan.status == navidrome::UploadPlanStatus::Cancelled
            ? navidrome::l10n::playlistUploadCancelled
            : navidrome::l10n::error(navidrome::l10n::invalidPlaylistUploadChoice));
        return 0;
    }

    setStatus(navidrome::l10n::uploadingPlaylist);
    auto dispatch = m_dispatchState;
    const auto operationId = payload->operationId;
    const auto identity = payload->identity;
    const bool formPost = payload->capabilities.formPost;
    auto context = std::move(payload->context);
    std::thread([dispatch, operationId, identity, plan = std::move(plan),
                 formPost, context = std::move(context)]() mutable {
        auto* complete = new PlaylistCompletePayload{};
        complete->operationId = operationId;
        complete->identity = identity;
        complete->plan = plan;

        navidrome::WriteEvidence evidence;
        evidence.requestedIds = plan.orderedSongIds;
        navidrome::ServerPlaylistDetails original;
        if (plan.targetPlaylistId) {
            std::string originalError;
            original = navidrome::SubsonicClientWin::get().getPlaylist(
                context, *plan.targetPlaylistId, originalError);
            if (!originalError.empty() || original.playlist.id.empty()) {
                complete->outcome = navidrome::BrowserWriteOutcome::FailedNoChange;
                complete->error = originalError.empty()
                    ? navidrome::l10n::existingPlaylistSnapshotFailed
                    : originalError;
                dispatchBrowserPayload(dispatch, WM_NAVIDROME_PLAYLIST_COMPLETE,
                                       complete);
                return;
            }
            evidence.originalIds = playlistSongIds(original);
        }

        evidence.mutationAttempted = true;
        std::string writeError;
        const auto write = navidrome::SubsonicClientWin::get().createOrReplacePlaylist(
            context, plan.targetPlaylistId, plan.targetName,
            plan.orderedSongIds, formPost, writeError);
        evidence.transportState = write.state;
        complete->error = !writeError.empty() ? writeError : write.error;

        std::string targetId = write.playlist.id;
        if (targetId.empty() && plan.targetPlaylistId) targetId = *plan.targetPlaylistId;
        if (targetId.empty()) {
            std::string catalogError;
            const auto catalog = navidrome::SubsonicClientWin::get().getPlaylists(
                context, catalogError);
            const auto createdMatches = navidrome::exactNameMatches(
                catalog, plan.targetName);
            if (catalogError.empty() && createdMatches.size() == 1)
                targetId = createdMatches.front().id;
            else if (complete->error.empty())
                complete->error = catalogError.empty()
                    ? navidrome::l10n::ambiguousCreatedPlaylist
                    : catalogError;
        }

        if (!targetId.empty()) {
            std::string verifyError;
            const auto verified = navidrome::SubsonicClientWin::get().getPlaylist(
                context, targetId, verifyError);
            if (verifyError.empty() && !verified.playlist.id.empty())
                evidence.verifiedCurrentIds = playlistSongIds(verified);
            else if (complete->error.empty())
                complete->error = verifyError.empty()
                    ? navidrome::l10n::playlistVerificationFailed : verifyError;
        }

        const bool verifiedRequested = evidence.verifiedCurrentIds &&
            *evidence.verifiedCurrentIds == evidence.requestedIds;
        const bool verifiedOriginal = evidence.originalIds &&
            evidence.verifiedCurrentIds &&
            *evidence.verifiedCurrentIds == *evidence.originalIds;
        if (evidence.originalIds && !verifiedRequested && !verifiedOriginal) {
            evidence.restorationAttempted = true;
            std::string restoreError;
            navidrome::SubsonicClientWin::get().createOrReplacePlaylist(
                context, plan.targetPlaylistId, plan.targetName,
                *evidence.originalIds, formPost, restoreError);
            if (plan.targetPlaylistId) {
                std::string verifyRestoreError;
                const auto restored = navidrome::SubsonicClientWin::get().getPlaylist(
                    context, *plan.targetPlaylistId, verifyRestoreError);
                if (verifyRestoreError.empty() && !restored.playlist.id.empty())
                    evidence.verifiedRestoredIds = playlistSongIds(restored);
                else if (complete->error.empty())
                    complete->error = verifyRestoreError.empty()
                        ? navidrome::l10n::playlistVerificationFailed
                        : verifyRestoreError;
            }
            if (!restoreError.empty() && complete->error.empty()) {
                complete->error = restoreError;
            }
        }

        complete->outcome = navidrome::classifyWriteOutcome(evidence);
        dispatchBrowserPayload(dispatch, WM_NAVIDROME_PLAYLIST_COMPLETE, complete);
    }).detach();
    return 0;
}

LRESULT BrowserWindow::OnPlaylistComplete(
        UINT, WPARAM wParam, LPARAM, BOOL&) {
    std::unique_ptr<PlaylistCompletePayload> payload(
        reinterpret_cast<PlaylistCompletePayload*>(wParam));
    if (!m_playlistInProgress || payload->operationId != m_playlistOperationId)
        return 0;
    m_playlistInProgress = false;
    updateActionState();
    if (payload->identity != m_mutationIdentity) return 0;

    switch (payload->outcome) {
    case navidrome::BrowserWriteOutcome::Complete:
        setStatus(navidrome::l10n::playlistUploadComplete(
            payload->plan.targetName, payload->plan.orderedSongIds.size(),
            payload->plan.skippedCount));
        break;
    case navidrome::BrowserWriteOutcome::Partial:
        setStatus(navidrome::l10n::playlistUploadPartial(payload->error));
        break;
    case navidrome::BrowserWriteOutcome::Restored:
        setStatus(navidrome::l10n::playlistUploadRestored(payload->error));
        break;
    case navidrome::BrowserWriteOutcome::Unknown:
        setStatus(navidrome::l10n::playlistUploadUnknown(payload->error));
        break;
    case navidrome::BrowserWriteOutcome::FailedNoChange:
        setStatus(navidrome::l10n::playlistUploadFailed(payload->error));
        break;
    }

    if (navidrome::shouldRefreshPlaylistCatalog(payload->outcome)) {
        navidrome::BrowserMutationEvent event;
        event.identity = payload->identity;
        event.kind = navidrome::BrowserMutationKind::PlaylistCatalogChanged;
        navidrome::BrowserMutationHub::get().publish(std::move(event));
    }
    return 0;
}

void BrowserWindow::OnRefresh(UINT, int, HWND) {
    if (isBusy()) return;
    m_searchQuery.clear();
    ++m_searchRequestId;
    loadArtists();
    m_search.SetWindowText(L"");
}

void BrowserWindow::OnSearchChanged(UINT, int, HWND) {
    if (isBusy()) return;
    wchar_t buf[256] = {};
    m_search.GetWindowText(buf, 256);
    std::string query = wToU8(buf);
    auto context = navidrome::SubsonicClientWin::get().snapshot();
    if (!ensureCurrentAccount(context)) return;
    m_searchQuery = query;
    const std::uint64_t requestId = ++m_searchRequestId;
    if (query.size() < 2) {
        if (!m_libraryRoots.empty()) {
            displayGroupedNavigation();
            setStatus(navidrome::l10n::artistCount(m_libraryRoots.size()));
        } else if (!m_libraryLoading) {
            loadArtists();
        }
        return;
    }
    setStatus(navidrome::l10n::searching);
    auto dispatch = m_dispatchState;
    std::thread([dispatch, query, requestId, context = std::move(context)]() {
        std::string err;
        auto results = navidrome::SubsonicClientWin::get().search(context, query, err);
        auto* payload = new LoadedPayload{};
        payload->source = LoadedPayload::Source::Search;
        payload->requestId = requestId;
        payload->error = err;
        for (const auto& song : results.songs)
            payload->nodes.push_back(BrowserWindow::makeSongNode(song));
        dispatchBrowserPayload(dispatch, WM_NAVIDROME_LOADED, payload);
    }).detach();
}

// ---------------------------------------------------------------------------
// Deep song collection (synchronous, call from background thread)
// ---------------------------------------------------------------------------
void BrowserWindow::collectSongsDeep(
        const std::shared_ptr<NavidromeNode>& node,
        std::vector<std::shared_ptr<NavidromeNode>>& out,
        std::size_t& failedItems,
        const std::shared_ptr<std::atomic_bool>& cancel,
        const navidrome::SubsonicRequestContext& context) {
    if (cancellationRequested(cancel)) return;
    if (node->type == NavidromeNode::Song) { out.push_back(node); return; }
    if (node->type == NavidromeNode::Error) { ++failedItems; return; }
    if (node->type == NavidromeNode::Loading) return;
    if (!navidrome::isBrowserContainer(node->type)) return;

    std::vector<std::shared_ptr<NavidromeNode>> fetched;
    const std::vector<std::shared_ptr<NavidromeNode>>* children = &node->children;
    if (!node->childrenLoaded) {
        std::string error;
        fetched = fetchChildren(node, context, error);
        if (cancellationRequested(cancel)) return;
        if (!error.empty()) ++failedItems;
        children = &fetched;
    }
    for (const auto& child : *children) {
        if (cancellationRequested(cancel)) return;
        collectSongsDeep(child, out, failedItems, cancel, context);
    }
}

// ---------------------------------------------------------------------------
// Enqueue to foobar2000 playlist (call from main thread)
// ---------------------------------------------------------------------------
PlaylistAppendReceipt BrowserWindow::enqueueNodes(
        std::vector<std::shared_ptr<NavidromeNode>> songs, bool play) {
    PlaylistAppendReceipt receipt;
    if (songs.empty()) {
        setStatus(navidrome::l10n::noSongsSelected);
        return receipt;
    }

    metadb_handle_list tracks;
    auto hints = metadb_io_v2::get()->create_hint_list();

    for (auto& node : songs) {
        // Enqueue a navidrome://track/<id>?... URI (not the raw HTTP URL) so the
        // input handler resolves the stream — with custom headers — at decode
        // time, and metadata renders without a network round-trip.
        navidrome::Song metadata = node->metadata;
        metadata.id = node->id;
        if (metadata.title.empty()) metadata.title = node->displayName;
        if (metadata.artist.empty()) metadata.artist = node->subtitle;
        if (metadata.album.empty()) metadata.album = node->album;
        if (metadata.coverArtId.empty()) metadata.coverArtId = node->coverArtId;
        if (metadata.suffix.empty()) metadata.suffix = node->suffix;
        if (metadata.track == 0) metadata.track = node->track;
        if (metadata.year == 0) metadata.year = node->year;
        if (metadata.duration == 0) metadata.duration = node->duration;
        std::string uri = navidrome::makeTrackURI(metadata);
        if (uri.empty()) continue;

        metadb_handle_ptr handle;
        playable_location_impl loc;
        loc.set_path(uri.c_str());
        loc.set_subsong(0);
        metadb::get()->handle_create(handle, loc);
        tracks += handle;

        file_info_impl info;
        navidrome::applySongMetadata(info, metadata);
        auto stats = filestats_invalid;
        if (!navidrome::isTranscoded(metadata) && metadata.size && *metadata.size >= 0)
            stats.m_size = static_cast<t_filesize>(*metadata.size);
        hints->add_hint(handle, info, stats, true);
    }
    hints->on_done();

    if (tracks.get_count() == 0) {
        setStatus(navidrome::l10n::noSongsFound);
        return receipt;
    }

    auto pm = playlist_manager::get();
    t_size pl = pm->get_active_playlist();
    if (pl == pfc_infinite) {
        pm->create_playlist("Navidrome", static_cast<t_size>(pfc_infinite),
                            static_cast<t_size>(pfc_infinite));
        pl = pm->get_active_playlist();
    }
    t_size insertPos = pm->playlist_get_item_count(pl);
    const t_size insertedAt = pm->playlist_insert_items(
        pl, insertPos, tracks, pfc::bit_array_false());
    if (insertedAt == pfc_infinite) return receipt;
    receipt.playlist = pl;
    receipt.insertPos = insertedAt;
    receipt.count = tracks.get_count();
    receipt.tracks = tracks;
    receipt.success = true;

    if (play && tracks.get_count() > 0) {
        // Start playback honoring the user's Playback > Order setting (Shuffle,
        // Random, Default, …). track_command_play asks the active playback order
        // for the starting track; the focus biases in-order modes to the first
        // newly-added track. (playlist_execute_default_action would instead pin
        // that exact track and ignore the order.)
        pm->set_active_playlist(pl);
        pm->set_playing_playlist(pl);
        pm->playlist_set_focus_item(pl, insertedAt);
        playback_control::get()->start(playback_control::track_command_play);
    }

    setStatus(navidrome::l10n::addedTracks(tracks.get_count()));
    return receipt;
}

bool BrowserWindow::rollbackAppend(const PlaylistAppendReceipt& receipt) {
    if (!receipt.success || receipt.count == 0) return true;
    auto pm = playlist_manager::get();
    const t_size before = pm->playlist_get_item_count(receipt.playlist);
    if (!navidrome::isValidAppendRange(
            receipt.insertPos, receipt.count, before))
        return false;
    metadb_handle_list current;
    pm->playlist_get_items(receipt.playlist, current,
        pfc::bit_array_range(receipt.insertPos, receipt.count));
    if (current.get_count() != receipt.tracks.get_count()) return false;
    for (t_size i = 0; i < current.get_count(); ++i) {
        if (current[i] != receipt.tracks[i]) return false;
    }
    if (!pm->playlist_remove_items(receipt.playlist,
        pfc::bit_array_range(receipt.insertPos, receipt.count))) return false;
    return navidrome::didRollbackRestoreCount(
        before, pm->playlist_get_item_count(receipt.playlist), receipt.count);
}

void BrowserWindow::updateActionState() {
    const BOOL idle = isBusy() ? FALSE : TRUE;
    if (m_tree.IsWindow()) m_tree.EnableWindow(idle);
    if (m_search.IsWindow()) m_search.EnableWindow(idle);
    if (m_refreshBtn.IsWindow()) m_refreshBtn.EnableWindow(idle);
    if (m_addBtn.IsWindow()) m_addBtn.EnableWindow(idle);
    if (m_playBtn.IsWindow()) m_playBtn.EnableWindow(idle);
    if (m_addAllBtn.IsWindow())
        m_addAllBtn.EnableWindow(idle && !m_libraryRoots.empty());
    if (m_reconcileBtn.IsWindow())
        m_reconcileBtn.EnableWindow(idle && !m_libraryRoots.empty());
}

bool BrowserWindow::isBusy() const noexcept {
    return m_queueInProgress || m_mutationInProgress || m_playlistInProgress;
}

void BrowserWindow::setStatus(const std::string& msg) {
    m_status.SetWindowText(u8ToWide(msg).c_str());
}
