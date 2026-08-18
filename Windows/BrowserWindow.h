#pragma once
#include "stdafx.h"
#include "../SubsonicTypes.h"
#include "BrowserExtrasLogic.h"
#include "BrowserMutationHub.h"
#include "ServerConnectionHub.h"
#include "LibraryImporter.h"
#include "SubsonicClientWin.h"
#include <map>
#include <memory>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <string>

#define WM_NAVIDROME_LOADED  (WM_USER + 101)
#define WM_NAVIDROME_CHILDREN (WM_USER + 102)
#define WM_NAVIDROME_QUEUE_PROGRESS (WM_USER + 103)
#define WM_NAVIDROME_QUEUE_COMPLETE (WM_USER + 104)
#define WM_NAVIDROME_LIBRARY_PROGRESS (WM_USER + 105)
#define WM_NAVIDROME_LIBRARY_COMPLETE (WM_USER + 106)
#define WM_NAVIDROME_MUTATION_COMPLETE (WM_USER + 107)
#define WM_NAVIDROME_PLAYLIST_CATALOG (WM_USER + 108)
#define WM_NAVIDROME_PLAYLIST_COMPLETE (WM_USER + 109)
#define WM_NAVIDROME_DOWNLOAD_COMPLETE (WM_USER + 110)
#define WM_NAVIDROME_SERVER_PLAYLIST_COMPLETE (WM_USER + 111)
#define WM_NAVIDROME_CONNECTION_CHANGED (WM_USER + 112)

// ---------------------------------------------------------------------------
// Tree node
// ---------------------------------------------------------------------------
struct NavidromeNode {
    using Type = navidrome::BrowserNodeKind;
    static constexpr Type NavigationGroup = Type::NavigationGroup;
    static constexpr Type Artist = Type::Artist;
    static constexpr Type Album = Type::Album;
    static constexpr Type Song = Type::Song;
    static constexpr Type Genre = Type::Genre;
    static constexpr Type SmartList = Type::SmartList;
    static constexpr Type ServerPlaylist = Type::ServerPlaylist;
    static constexpr Type Loading = Type::Loading;
    static constexpr Type Error = Type::Error;

    Type        type         = Loading;
    std::optional<navidrome::NavigationGroupKind> navigationGroup;
    std::optional<navidrome::SmartListKind> smartList;
    std::string id;
    std::string displayName;
    std::string subtitle;    // artist name for albums/songs
    std::string album;       // album name for songs
    std::string coverArtId;
    std::string suffix;      // codec suffix (mp3/flac/…) for songs
    int         track        = 0;
    int         year         = 0;
    double      duration     = 0.0;
    navidrome::Song metadata;
    navidrome::ServerPlaylist playlist;
    std::optional<std::string> starred;
    bool        childrenLoaded = false;
    bool        isLoading    = false;
    std::uint64_t childRequestId = 0;
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
    navidrome::EnqueueDisposition disposition =
        navidrome::EnqueueDisposition::Append;
};

struct LibraryProgressPayload {
    std::uint64_t operationId = 0;
    navidrome::LibraryImportProgress progress;
};

struct LibraryCompletePayload {
    std::uint64_t operationId = 0;
    navidrome::LibraryImportResult result;
};

struct MutationCompletePayload {
    std::uint64_t operationId = 0;
    std::string identity;
    navidrome::BrowserMutationKind kind =
        navidrome::BrowserMutationKind::FavoriteChanged;
    navidrome::FavoriteKind entityKind = navidrome::FavoriteKind::Song;
    std::string entityId;
    bool favorite = false;
    int rating = 0;
    bool success = false;
    std::string error;
};

struct PlaylistCatalogPayload {
    std::uint64_t operationId = 0;
    navidrome::SubsonicRequestContext context;
    std::string identity;
    std::string activePlaylistName;
    navidrome::PlaylistUriMapping mapping;
    std::size_t sourceItemCount = 0;
    std::vector<navidrome::ServerPlaylist> catalog;
    navidrome::OpenSubsonicCapabilities capabilities;
    std::string error;
};

struct PlaylistCompletePayload {
    std::uint64_t operationId = 0;
    std::string identity;
    navidrome::BrowserUploadPlan plan;
    navidrome::BrowserWriteOutcome outcome =
        navidrome::BrowserWriteOutcome::Unknown;
    std::string error;
};

struct PlaylistAppendReceipt {
    t_size playlist = static_cast<t_size>(pfc_infinite);
    t_size insertPos = static_cast<t_size>(pfc_infinite);
    t_size count = 0;
    metadb_handle_list tracks;
    bool success = false;
};

struct DownloadCompletePayload {
    std::uint64_t operationId = 0;
    std::size_t succeeded = 0;
    std::size_t failed = 0;
};

struct ServerPlaylistCompletePayload {
    enum class Action { Rename, Delete };
    std::uint64_t operationId = 0;
    std::string identity;
    Action action = Action::Rename;
    std::string playlistId;
    std::string name;
    bool success = false;
    std::string error;
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
        MESSAGE_HANDLER(WM_NAVIDROME_MUTATION_COMPLETE, OnMutationComplete)
        MESSAGE_HANDLER(WM_NAVIDROME_PLAYLIST_CATALOG, OnPlaylistCatalog)
        MESSAGE_HANDLER(WM_NAVIDROME_PLAYLIST_COMPLETE, OnPlaylistComplete)
        MESSAGE_HANDLER(WM_NAVIDROME_DOWNLOAD_COMPLETE, OnDownloadComplete)
        MESSAGE_HANDLER(WM_NAVIDROME_SERVER_PLAYLIST_COMPLETE, OnServerPlaylistComplete)
        MESSAGE_HANDLER(WM_NAVIDROME_CONNECTION_CHANGED, OnConnectionChanged)
        NOTIFY_CODE_HANDLER_EX(TVN_ITEMEXPANDING, OnTreeExpanding)
        NOTIFY_CODE_HANDLER_EX(NM_DBLCLK,        OnTreeDblClick)
        NOTIFY_CODE_HANDLER_EX(NM_RETURN,        OnTreeReturn)
        MSG_WM_CONTEXTMENU(OnContextMenu)
        COMMAND_ID_HANDLER_EX(IDC_ADD,     OnAdd)
        COMMAND_ID_HANDLER_EX(IDC_ADD_ALL, OnAddAll)
        COMMAND_ID_HANDLER_EX(IDC_RECONCILE, OnReconcile)
        COMMAND_ID_HANDLER_EX(IDC_PLAY,    OnPlay)
        COMMAND_ID_HANDLER_EX(IDC_REFRESH, OnRefresh)
        COMMAND_ID_HANDLER_EX(IDC_FAVORITE, OnFavorite)
        COMMAND_ID_HANDLER_EX(IDC_UPLOAD_PLAYLIST, OnUploadActivePlaylist)
        COMMAND_ID_HANDLER_EX(IDC_DOWNLOAD_ORIGINAL, OnDownloadOriginal)
        COMMAND_ID_HANDLER_EX(IDC_RENAME_SERVER_PLAYLIST, OnRenameServerPlaylist)
        COMMAND_ID_HANDLER_EX(IDC_DELETE_SERVER_PLAYLIST, OnDeleteServerPlaylist)
        COMMAND_RANGE_HANDLER_EX(IDC_RATE_0, IDC_RATE_5, OnRate)
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
        IDC_FAVORITE=1009,
        IDC_UPLOAD_PLAYLIST=1010,
        IDC_RATE_0=1011,
        IDC_RATE_5=1016,
        IDC_DOWNLOAD_ORIGINAL=1017,
        IDC_RENAME_SERVER_PLAYLIST=1018,
        IDC_DELETE_SERVER_PLAYLIST=1019,
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
    LRESULT OnMutationComplete(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPlaylistCatalog(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPlaylistComplete(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDownloadComplete(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnServerPlaylistComplete(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnConnectionChanged(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnTreeExpanding(LPNMHDR);
    LRESULT OnTreeDblClick(LPNMHDR);
    LRESULT OnTreeReturn(LPNMHDR);
    void    OnContextMenu(CWindow wnd, CPoint point);
    void    OnAdd(UINT, int, HWND);
    void    OnAddAll(UINT, int, HWND);
    void    OnReconcile(UINT, int, HWND);
    void    OnPlay(UINT, int, HWND);
    void    OnRefresh(UINT, int, HWND);
    void    OnFavorite(UINT, int, HWND);
    void    OnRate(UINT, int, HWND);
    void    OnUploadActivePlaylist(UINT, int, HWND);
    void    OnDownloadOriginal(UINT, int, HWND);
    void    OnRenameServerPlaylist(UINT, int, HWND);
    void    OnDeleteServerPlaylist(UINT, int, HWND);
    void    OnSearchChanged(UINT, int, HWND);

    void    loadArtists();
    void    populateRoot(LoadedPayload* payload);
    void    populateChildren(LoadedPayload* payload);
    void    applyDeferredChildren();
    void    applyDeferredMutations();
    void    removeLoadingChildren(const std::shared_ptr<NavidromeNode>& parent);
    void    forgetTreeBranch(HTREEITEM item);
    void    clearNodeChildren(const std::shared_ptr<NavidromeNode>& parent);
    void    startChildLoad(const std::shared_ptr<NavidromeNode>& node,
                           navidrome::SubsonicRequestContext context,
                           bool replaceExisting);
    void    refreshFavoriteSmartList(navidrome::FavoriteKind kind);
    void    refreshServerPlaylistCatalog();
    void    displayRootNodes(const std::vector<std::shared_ptr<NavidromeNode>>& nodes);
    void    displayGroupedNavigation();
    std::vector<std::shared_ptr<NavidromeNode>> buildGroupedRoots() const;
    HTREEITEM insertNode(HTREEITEM parent, std::shared_ptr<NavidromeNode> node);
    HTREEITEM insertNodeTree(HTREEITEM parent, std::shared_ptr<NavidromeNode> node);
    std::shared_ptr<NavidromeNode> nodeForItem(HTREEITEM hItem);
    static std::shared_ptr<NavidromeNode> makeArtistNode(
        const navidrome::Artist& artist);
    static std::shared_ptr<NavidromeNode> makeAlbumNode(
        const navidrome::Album& album);
    static std::shared_ptr<NavidromeNode> makeSongNode(
        const navidrome::Song& song);
    static std::shared_ptr<NavidromeNode> makeGenreNode(
        const navidrome::Genre& genre);
    static std::shared_ptr<NavidromeNode> makePlaylistNode(
        const navidrome::ServerPlaylist& playlist);
    static std::vector<std::shared_ptr<NavidromeNode>> fetchChildren(
        const std::shared_ptr<NavidromeNode>& node,
        const navidrome::SubsonicRequestContext& context,
        std::string& outError);
    static void collectSongsDeep(
        const std::shared_ptr<NavidromeNode>& node,
        std::vector<std::shared_ptr<NavidromeNode>>& out,
        std::size_t& failedItems,
        const std::shared_ptr<std::atomic_bool>& cancel,
        const navidrome::SubsonicRequestContext& context);
    PlaylistAppendReceipt enqueueNodes(
        std::vector<std::shared_ptr<NavidromeNode>> songs, bool play,
        navidrome::EnqueueDisposition disposition =
            navidrome::EnqueueDisposition::Append);
    bool rollbackAppend(const PlaylistAppendReceipt& receipt);
    std::vector<std::shared_ptr<NavidromeNode>> selectedNodes();
    void    queueSelected(bool play, bool closeAfter,
                          navidrome::EnqueueDisposition disposition =
                              navidrome::EnqueueDisposition::Append);
    void    queueNodes(std::vector<std::shared_ptr<NavidromeNode>> roots,
                       bool play, bool closeAfter, bool reportRootProgress,
                       navidrome::EnqueueDisposition disposition =
                           navidrome::EnqueueDisposition::Append);
    void    importLibrary(bool forceFull);
    void    startMutation(const std::shared_ptr<NavidromeNode>& node,
                          navidrome::BrowserMutationKind kind,
                          bool favorite, int rating);
    void    bindMutationIdentity(const navidrome::SubsonicRequestContext& context);
    bool    ensureCurrentAccount(
        const navidrome::SubsonicRequestContext& context);
    void    applyMutationEvent(const navidrome::BrowserMutationEvent& event);
    void    applyMutationProjection(const navidrome::BrowserMutationEvent& event);
    void    applyKnownMutations(
        const std::vector<std::shared_ptr<NavidromeNode>>& nodes);
    void    applyMutationToNode(const navidrome::BrowserMutationEvent& event,
                                const std::shared_ptr<NavidromeNode>& node);
    void    refreshNodeLabel(const std::shared_ptr<NavidromeNode>& node);
    std::string nodeLabel(const std::shared_ptr<NavidromeNode>& node) const;
    bool    isBusy() const noexcept;
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
    std::vector<navidrome::BrowserMutationEvent>          m_deferredMutationEvents;

    std::shared_ptr<BrowserDispatchState> m_dispatchState;
    std::shared_ptr<std::atomic_bool>      m_queueCancel;
    std::uint64_t m_libraryRequestId = 0;
    std::uint64_t m_searchRequestId = 0;
    std::uint64_t m_displayGeneration = 0;
    std::uint64_t m_queueOperationId = 0;
    std::uint64_t m_mutationOperationId = 0;
    std::uint64_t m_playlistOperationId = 0;
    std::uint64_t m_downloadOperationId = 0;
    std::uint64_t m_playlistMutationOperationId = 0;
    std::string   m_searchQuery;
    std::string   m_mutationIdentity;
    navidrome::BrowserMutationSubscription m_mutationSubscription;
    navidrome::ServerConnectionSubscription m_connectionSubscription;
    std::uint64_t m_connectionRevision = 0;
    std::map<std::string, navidrome::BrowserMutationEvent> m_confirmedMutations;
    std::map<std::string, std::uint64_t> m_appliedMutationRevisions;
    bool          m_libraryLoading = false;
    bool          m_queueInProgress = false;
    bool          m_mutationInProgress = false;
    bool          m_playlistInProgress = false;
    bool          m_downloadInProgress = false;
    bool          m_playlistMutationInProgress = false;
};
