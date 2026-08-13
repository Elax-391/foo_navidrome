#include "stdafx.h"
#include "BrowserWindow.h"
#include "LibraryImportLogic.h"
#include "Localization.h"
#include "SubsonicClientWin.h"
#include "NavidromeInputWin.h"
#include "LibraryImporter.h"
#include "SongMetadataProjection.h"
#include <SDK/playlist.h>
#include <SDK/metadb.h>
#include <SDK/playable_location.h>
#include <SDK/playback_control.h>
#include <commctrl.h>
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
    updateActionState();

    return 0;
}

void BrowserWindow::OnDestroy() {
    if (m_queueCancel) m_queueCancel->store(true);
    ++m_queueOperationId;
    ++m_libraryRequestId;
    ++m_searchRequestId;
    ++m_displayGeneration;
    m_queueInProgress = false;
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
void BrowserWindow::loadArtists() {
    setStatus(navidrome::l10n::loadingArtists);
    m_libraryLoading = true;
    m_libraryRoots.clear();
    displayRootNodes({});
    updateActionState();

    const std::uint64_t requestId = ++m_libraryRequestId;
    auto dispatch = m_dispatchState;
    std::thread([dispatch, requestId]() {
        auto* payload = new LoadedPayload{};
        payload->source = LoadedPayload::Source::Library;
        payload->requestId = requestId;
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
        m_libraryRoots = payload->nodes;
        updateActionState();
        if (m_searchQuery.size() < 2) {
            displayRootNodes(m_libraryRoots);
            if (!m_queueInProgress)
                setStatus(navidrome::l10n::artistCount(m_libraryRoots.size()));
        }
        return;
    }

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
    // Show expand arrow for unloaded artists/albums or cached nodes with children.
    const bool container = node->type == NavidromeNode::Artist ||
                           node->type == NavidromeNode::Album;
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
    if (m_queueInProgress) return TRUE;
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

    const std::uint64_t displayGeneration = m_displayGeneration;
    auto dispatch = m_dispatchState;
    std::thread([dispatch, node, displayGeneration]() {
        auto* payload  = new LoadedPayload{};
        payload->parent = node;
        payload->displayGeneration = displayGeneration;
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
                n->metadata       = s;
                n->childrenLoaded = true;
                payload->nodes.push_back(n);
            }
        }
        payload->error = err;
        dispatchBrowserPayload(dispatch, WM_NAVIDROME_CHILDREN, payload);
    }).detach();

    return 0;
}

LRESULT BrowserWindow::OnTreeDblClick(LPNMHDR) {
    if (m_queueInProgress) return 0;
    HTREEITEM hSel = m_tree.GetSelectedItem();
    if (!hSel) return 0;
    auto node = nodeForItem(hSel);
    if (!node) return 0;
    if (node->type == NavidromeNode::Song)
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
            if (n && n->type != NavidromeNode::Loading && n->type != NavidromeNode::Error)
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
    if (m_queueInProgress) {
        setStatus(navidrome::l10n::queueBusy);
        return;
    }
    if (roots.empty()) {
        setStatus(navidrome::l10n::noSongsSelected);
        return;
    }

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
                 totalRoots, play, closeAfter, reportRootProgress]() mutable {
        std::vector<std::shared_ptr<NavidromeNode>> songs;
        std::size_t failedItems = 0;
        std::size_t completedRoots = 0;

        for (auto& root : roots) {
            if (cancellationRequested(cancel)) break;
            BrowserWindow::collectSongsDeep(root, songs, failedItems, cancel);
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
    if (m_queueInProgress) {
        setStatus(navidrome::l10n::queueBusy);
        return;
    }
    auto context = navidrome::SubsonicClientWin::get().snapshot();
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
        nodes.push_back(std::move(node));
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

    applyDeferredChildren();
    m_queueInProgress = false;
    m_queueCancel.reset();
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
    if (m_queueInProgress) return 0;
    queueSelected(true, true);
    return 0;
}

// Right-click context menu on the tree — mirrors the Add/Play buttons for a
// native feel. The menu item IDs are IDC_PLAY / IDC_ADD, so TrackPopupMenu
// posts WM_COMMAND straight into the existing OnPlay / OnAdd handlers.
void BrowserWindow::OnContextMenu(CWindow wnd, CPoint point) {
    if (wnd.m_hWnd != m_tree.m_hWnd) { SetMsgHandled(FALSE); return; }
    if (m_queueInProgress) return;

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
    if (m_queueInProgress) return;
    m_searchQuery.clear();
    ++m_searchRequestId;
    loadArtists();
    m_search.SetWindowText(L"");
}

void BrowserWindow::OnSearchChanged(UINT, int, HWND) {
    if (m_queueInProgress) return;
    wchar_t buf[256] = {};
    m_search.GetWindowText(buf, 256);
    std::string query = wToU8(buf);
    m_searchQuery = query;
    const std::uint64_t requestId = ++m_searchRequestId;
    if (query.size() < 2) {
        if (!m_libraryRoots.empty()) {
            displayRootNodes(m_libraryRoots);
            setStatus(navidrome::l10n::artistCount(m_libraryRoots.size()));
        } else if (!m_libraryLoading) {
            loadArtists();
        }
        return;
    }
    setStatus(navidrome::l10n::searching);
    auto dispatch = m_dispatchState;
    std::thread([dispatch, query, requestId]() {
        std::string err;
        auto results = navidrome::SubsonicClientWin::get().search(query, err);
        auto* payload = new LoadedPayload{};
        payload->source = LoadedPayload::Source::Search;
        payload->requestId = requestId;
        payload->error = err;
        for (auto& s : results.songs) {
            auto n = std::make_shared<NavidromeNode>();
            n->type           = NavidromeNode::Song;
            n->id             = s.id;
            n->displayName    = s.title;
            n->subtitle       = s.artist;
            n->album          = s.album;
            n->coverArtId     = s.coverArtId;
            n->track          = s.track;
            n->year           = s.year;
            n->duration       = s.duration;
            n->suffix        = s.suffix;
            n->metadata       = s;
            n->childrenLoaded = true;
            payload->nodes.push_back(n);
        }
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
        const std::shared_ptr<std::atomic_bool>& cancel) {
    if (cancellationRequested(cancel)) return;
    if (node->type == NavidromeNode::Song) { out.push_back(node); return; }
    if (node->type == NavidromeNode::Error) { ++failedItems; return; }
    if (node->type == NavidromeNode::Loading) return;

    if (node->type == NavidromeNode::Album) {
        if (node->childrenLoaded) {
            for (auto& child : node->children) {
                if (cancellationRequested(cancel)) return;
                collectSongsDeep(child, out, failedItems, cancel);
            }
        } else {
            std::string err;
            auto fetched = navidrome::SubsonicClientWin::get()
                .getSongsForAlbum(node->id, err);
            if (cancellationRequested(cancel)) return;
            if (!err.empty()) ++failedItems;
            for (auto& s : fetched) {
                if (cancellationRequested(cancel)) return;
                auto n = std::make_shared<NavidromeNode>();
                n->type = NavidromeNode::Song; n->id = s.id;
                n->displayName = s.title; n->subtitle = s.artist;
                n->album = s.album;
                n->coverArtId = s.coverArtId; n->track = s.track;
                n->suffix = s.suffix;
                n->year = s.year;
                n->duration = s.duration; n->childrenLoaded = true;
                n->metadata = s;
                out.push_back(n);
            }
        }
        return;
    }
    if (node->type == NavidromeNode::Artist) {
        if (node->childrenLoaded) {
            for (auto& child : node->children) {
                if (cancellationRequested(cancel)) return;
                collectSongsDeep(child, out, failedItems, cancel);
            }
        } else {
            std::string err;
            auto fetched = navidrome::SubsonicClientWin::get()
                .getAlbumsForArtist(node->id, err);
            if (cancellationRequested(cancel)) return;
            if (!err.empty()) ++failedItems;
            for (auto& a : fetched) {
                if (cancellationRequested(cancel)) return;
                auto albumNode = std::make_shared<NavidromeNode>();
                albumNode->type = NavidromeNode::Album; albumNode->id = a.id;
                albumNode->displayName = a.name;
                albumNode->subtitle = a.artist;
                albumNode->coverArtId = a.coverArtId;
                collectSongsDeep(albumNode, out, failedItems, cancel);
            }
        }
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
        pm->create_playlist("Navidrome", ~0, pfc_infinite);
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
    const BOOL idle = m_queueInProgress ? FALSE : TRUE;
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

void BrowserWindow::setStatus(const std::string& msg) {
    m_status.SetWindowText(u8ToWide(msg).c_str());
}
