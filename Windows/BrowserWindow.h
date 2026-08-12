#pragma once
#include "stdafx.h"
#include "../SubsonicTypes.h"
#include "LibraryImporter.h"
#include <map>
#include <memory>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

#define WM_NAVIDROME_LOADED  (WM_USER + 101)
#define WM_NAVIDROME_CHILDREN (WM_USER + 102)
#define WM_NAVIDROME_QUEUE_PROGRESS (WM_USER + 103)
#define WM_NAVIDROME_QUEUE_COMPLETE (WM_USER + 104)
#define WM_NAVIDROME_LIBRARY_PROGRESS (WM_USER + 105)
#define WM_NAVIDROME_LIBRARY_COMPLETE (WM_USER + 106)

// ---------------------------------------------------------------------------
// Tree node
// ---------------------------------------------------------------------------
struct NavidromeNode {
    enum Type { Artist, Album, Song, Loading, Error };
    Type        type         = Loading;
    std::string id;
    std::string displayName;
    std::string subtitle;    // artist name for albums/songs
    std::string album;       // album name for songs
    std::string coverArtId;
    std::string suffix;      // codec suffix (mp3/flac/…) for songs
    int         track        = 0;
    int         year         = 0;
    double      duration     = 0.0;
    bool        childrenLoaded = false;
    bool        isLoading    = false;
    HTREEITEM   hItem        = nullptr;
    std::vector<std::shared_ptr<NavidromeNode>> children;
};

// Payload sent from background thread to main thread
struct LoadedPayload {
    enum class Source { Library, Search };

    std::shared_ptr<NavidromeNode>              parent;   // nullptr = root load
    std::vector<std::shared_ptr<NavidromeNode>> nodes;
    std::string                                 error;
    Source                                      source = Source::Library;
    std::uint64_t                               requestId = 0;
    std::uint64_t                               displayGeneration = 0;
};

// Shared lifetime state captured by background workers instead of BrowserWindow*.
// The alive flag and HWND are read/written only on foobar2000's main thread.
struct BrowserDispatchState {
    HWND hwnd = nullptr;
    bool alive = false;
};

struct QueueProgressPayload {
    std::uint64_t operationId = 0;
    std::size_t completedRoots = 0;
    std::size_t totalRoots = 0;
    std::size_t songCount = 0;
    std::size_t failedItems = 0;
};

struct QueueCompletePayload {
    std::uint64_t operationId = 0;
    std::vector<std::shared_ptr<NavidromeNode>> songs;
    std::size_t failedItems = 0;
    bool cancelled = false;
    bool play = false;
    bool closeAfter = false;
};

struct LibraryProgressPayload {
    std::uint64_t operationId = 0;
    navidrome::LibraryImportProgress progress;
};

struct LibraryCompletePayload {
    std::uint64_t operationId = 0;
    navidrome::LibraryImportResult result;
};

struct PlaylistAppendReceipt {
    t_size playlist = pfc_infinite;
    t_size insertPos = pfc_infinite;
    t_size count = 0;
    metadb_handle_list tracks;
    bool success = false;
};

// ---------------------------------------------------------------------------
// BrowserWindow
// ---------------------------------------------------------------------------
class BrowserWindow : public CWindowImpl<BrowserWindow> {
public:
    static BrowserWindow& get();
    void show();
    // Create as a WS_CHILD panel filling `parent` (used by the Media Library
    // prefs page for an inline browser, mirroring the macOS embedded mount).
    void createEmbedded(HWND parent);

    DECLARE_WND_CLASS(L"foo_navidrome_BrowserWnd")

    BEGIN_MSG_MAP(BrowserWindow)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_SIZE(OnSize)
        MSG_WM_GETMINMAXINFO(OnGetMinMaxInfo)
        MESSAGE_HANDLER(WM_NAVIDROME_LOADED,   OnNavidromeLoaded)
        MESSAGE_HANDLER(WM_NAVIDROME_CHILDREN, OnNavidromeChildren)
        MESSAGE_HANDLER(WM_NAVIDROME_QUEUE_PROGRESS, OnQueueProgress)
        MESSAGE_HANDLER(WM_NAVIDROME_QUEUE_COMPLETE, OnQueueComplete)
        MESSAGE_HANDLER(WM_NAVIDROME_LIBRARY_PROGRESS, OnLibraryProgress)
        MESSAGE_HANDLER(WM_NAVIDROME_LIBRARY_COMPLETE, OnLibraryComplete)
        NOTIFY_CODE_HANDLER_EX(TVN_ITEMEXPANDING, OnTreeExpanding)
        NOTIFY_CODE_HANDLER_EX(NM_DBLCLK,        OnTreeDblClick)
        NOTIFY_CODE_HANDLER_EX(NM_RETURN,        OnTreeReturn)
        MSG_WM_CONTEXTMENU(OnContextMenu)
        COMMAND_ID_HANDLER_EX(IDC_ADD,     OnAdd)
        COMMAND_ID_HANDLER_EX(IDC_ADD_ALL, OnAddAll)
        COMMAND_ID_HANDLER_EX(IDC_RECONCILE, OnReconcile)
        COMMAND_ID_HANDLER_EX(IDC_PLAY,    OnPlay)
        COMMAND_ID_HANDLER_EX(IDC_REFRESH, OnRefresh)
        COMMAND_HANDLER_EX(IDC_SEARCH, EN_CHANGE, OnSearchChanged)
    END_MSG_MAP()

private:
    enum {
        IDC_TREE   = 1001,
        IDC_SEARCH = 1002,
        IDC_ADD    = 1003,
        IDC_PLAY   = 1004,
        IDC_REFRESH= 1005,
        IDC_STATUS = 1006,
        IDC_ADD_ALL= 1007,
        IDC_RECONCILE=1008,
    };

    LRESULT OnCreate(LPCREATESTRUCT);
    void    OnDestroy();
    LRESULT OnSize(UINT, CSize);
    void    OnGetMinMaxInfo(LPMINMAXINFO);
    LRESULT OnNavidromeLoaded(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNavidromeChildren(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnQueueProgress(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnQueueComplete(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnLibraryProgress(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnLibraryComplete(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnTreeExpanding(LPNMHDR);
    LRESULT OnTreeDblClick(LPNMHDR);
    LRESULT OnTreeReturn(LPNMHDR);
    void    OnContextMenu(CWindow wnd, CPoint point);
    void    OnAdd(UINT, int, HWND);
    void    OnAddAll(UINT, int, HWND);
    void    OnReconcile(UINT, int, HWND);
    void    OnPlay(UINT, int, HWND);
    void    OnRefresh(UINT, int, HWND);
    void    OnSearchChanged(UINT, int, HWND);

    void    loadArtists();
    void    populateRoot(LoadedPayload* payload);
    void    populateChildren(LoadedPayload* payload);
    void    applyDeferredChildren();
    void    removeLoadingChildren(const std::shared_ptr<NavidromeNode>& parent);
    void    displayRootNodes(const std::vector<std::shared_ptr<NavidromeNode>>& nodes);
    HTREEITEM insertNode(HTREEITEM parent, std::shared_ptr<NavidromeNode> node);
    HTREEITEM insertNodeTree(HTREEITEM parent, std::shared_ptr<NavidromeNode> node);
    std::shared_ptr<NavidromeNode> nodeForItem(HTREEITEM hItem);
    static void collectSongsDeep(
        const std::shared_ptr<NavidromeNode>& node,
        std::vector<std::shared_ptr<NavidromeNode>>& out,
        std::size_t& failedItems,
        const std::shared_ptr<std::atomic_bool>& cancel);
    PlaylistAppendReceipt enqueueNodes(
        std::vector<std::shared_ptr<NavidromeNode>> songs, bool play);
    bool rollbackAppend(const PlaylistAppendReceipt& receipt);
    std::vector<std::shared_ptr<NavidromeNode>> selectedNodes();
    void    queueSelected(bool play, bool closeAfter);
    void    queueNodes(std::vector<std::shared_ptr<NavidromeNode>> roots,
                       bool play, bool closeAfter, bool reportRootProgress);
    void    importLibrary(bool forceFull);
    void    updateActionState();
    void    setStatus(const std::string& msg);

    CTreeViewCtrl m_tree;
    CEdit         m_search;
    CButton       m_addBtn, m_addAllBtn, m_reconcileBtn, m_playBtn, m_refreshBtn;
    CStatic       m_status;

    // True when hosted inline in the prefs page (vs. the standalone window);
    // only the standalone window hides itself after an Enter "queue + play".
    bool          m_embedded = false;

    // Keeps nodes alive; HTREEITEM lParam points into these shared_ptrs
    std::map<HTREEITEM, std::shared_ptr<NavidromeNode>> m_nodeMap;
    std::vector<std::shared_ptr<NavidromeNode>>          m_rootNodes;
    std::vector<std::shared_ptr<NavidromeNode>>          m_libraryRoots;
    std::vector<std::unique_ptr<LoadedPayload>>           m_deferredChildren;

    std::shared_ptr<BrowserDispatchState> m_dispatchState;
    std::shared_ptr<std::atomic_bool>      m_queueCancel;
    std::uint64_t m_libraryRequestId = 0;
    std::uint64_t m_searchRequestId = 0;
    std::uint64_t m_displayGeneration = 0;
    std::uint64_t m_queueOperationId = 0;
    std::string   m_searchQuery;
    bool          m_libraryLoading = false;
    bool          m_queueInProgress = false;
};
