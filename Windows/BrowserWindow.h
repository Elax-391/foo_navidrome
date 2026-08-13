#pragma once
#include "stdafx.h"
#include "../SubsonicTypes.h"
#include <map>
#include <memory>
#include <vector>
#include <string>

#define WM_NAVIDROME_LOADED  (WM_USER + 101)
#define WM_NAVIDROME_CHILDREN (WM_USER + 102)

// ---------------------------------------------------------------------------
// Tree node
// ---------------------------------------------------------------------------
struct NavidromeNode {
    enum Type { Artist, Album, Song, Category, Playlist, Loading, Error };
    // Smart-list roots shown above the artist list; each maps to one Subsonic
    // endpoint (see BrowserWindow::fetchChildren).
    enum CategoryKind {
        CatStarred,          // getStarred2.view       → songs
        CatRecentlyAdded,    // getAlbumList2 newest   → albums
        CatMostPlayed,       // getAlbumList2 frequent → albums
        CatRecentlyPlayed,   // getAlbumList2 recent   → albums
        CatRandom,           // getAlbumList2 random   → albums
        CatPlaylists,        // getPlaylists.view      → playlists
    };

    Type        type         = Loading;
    CategoryKind category    = CatStarred;   // category nodes only
    std::string id;
    std::string displayName;
    std::string subtitle;    // artist name for albums/songs
    std::string album;       // album name for songs
    std::string coverArtId;
    std::string suffix;      // codec suffix (mp3/flac/…) for songs
    int         track        = 0;
    int         year         = 0;
    double      duration     = 0.0;
    bool        starred      = false;   // server-side favorite
    int         rating       = 0;       // 0 = unrated, else 1-5
    bool        childrenLoaded = false;
    bool        isLoading    = false;
    HTREEITEM   hItem        = nullptr;
    std::vector<std::shared_ptr<NavidromeNode>> children;
};

// Payload sent from background thread to main thread
struct LoadedPayload {
    std::shared_ptr<NavidromeNode>              parent;   // nullptr = root load
    std::vector<std::shared_ptr<NavidromeNode>> nodes;
    std::string                                 error;
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
        MESSAGE_HANDLER(WM_NAVIDROME_LOADED,   OnNavidromeLoaded)
        MESSAGE_HANDLER(WM_NAVIDROME_CHILDREN, OnNavidromeChildren)
        NOTIFY_CODE_HANDLER_EX(TVN_ITEMEXPANDING, OnTreeExpanding)
        NOTIFY_CODE_HANDLER_EX(NM_DBLCLK,        OnTreeDblClick)
        NOTIFY_CODE_HANDLER_EX(NM_RETURN,        OnTreeReturn)
        MSG_WM_CONTEXTMENU(OnContextMenu)
        COMMAND_ID_HANDLER_EX(IDC_ADD,     OnAdd)
        COMMAND_ID_HANDLER_EX(IDC_PLAY,    OnPlay)
        COMMAND_ID_HANDLER_EX(IDC_REFRESH, OnRefresh)
        COMMAND_ID_HANDLER_EX(IDC_STAR,    OnStar)
        COMMAND_ID_HANDLER_EX(IDC_UNSTAR,  OnUnstar)
        COMMAND_ID_HANDLER_EX(IDC_SEND_PLAYLIST, OnSendActivePlaylist)
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
        IDC_STAR   = 1007,
        IDC_UNSTAR = 1008,
        IDC_SEND_PLAYLIST = 1009,
        // Contiguous so a single COMMAND_RANGE_HANDLER_EX covers 0-5 stars.
        IDC_RATE_0 = 1010,
        IDC_RATE_5 = 1015,
    };

    LRESULT OnCreate(LPCREATESTRUCT);
    void    OnDestroy();
    LRESULT OnSize(UINT, CSize);
    LRESULT OnNavidromeLoaded(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNavidromeChildren(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnTreeExpanding(LPNMHDR);
    LRESULT OnTreeDblClick(LPNMHDR);
    LRESULT OnTreeReturn(LPNMHDR);
    void    OnContextMenu(CWindow wnd, CPoint point);
    void    OnAdd(UINT, int, HWND);
    void    OnPlay(UINT, int, HWND);
    void    OnRefresh(UINT, int, HWND);
    void    OnStar(UINT, int, HWND);
    void    OnUnstar(UINT, int, HWND);
    void    OnRate(UINT, int, HWND);
    void    OnSendActivePlaylist(UINT, int, HWND);
    void    OnSearchChanged(UINT, int, HWND);

    void    loadArtists();
    void    populateRoot(LoadedPayload* payload);
    void    populateChildren(LoadedPayload* payload);
    HTREEITEM insertNode(HTREEITEM parent, std::shared_ptr<NavidromeNode> node);
    std::shared_ptr<NavidromeNode> nodeForItem(HTREEITEM hItem);
    // Synchronous child fetch for any expandable node — background thread only.
    // Shared by lazy expansion and the deep song collector so both agree on
    // what a category / playlist / artist / album contains.
    std::vector<std::shared_ptr<NavidromeNode>>
            fetchChildren(const std::shared_ptr<NavidromeNode>& node, std::string& outError);
    void    collectSongsDeep(std::shared_ptr<NavidromeNode> node,
                             std::vector<std::shared_ptr<NavidromeNode>>& out);
    void    applyStarred(bool starred);
    // Tree label for a node: track number, favorite marker and rating stars.
    std::string labelFor(const std::shared_ptr<NavidromeNode>& node) const;
    void    refreshLabel(const std::shared_ptr<NavidromeNode>& node);
    void    enqueueNodes(std::vector<std::shared_ptr<NavidromeNode>> songs, bool play);
    std::vector<std::shared_ptr<NavidromeNode>> selectedNodes();
    void    queueSelected(bool play, bool closeAfter);
    void    setStatus(const std::string& msg);

    CTreeViewCtrl m_tree;
    CEdit         m_search;
    CButton       m_addBtn, m_playBtn, m_refreshBtn;
    CStatic       m_status;

    // True when hosted inline in the prefs page (vs. the standalone window);
    // only the standalone window hides itself after an Enter "queue + play".
    bool          m_embedded = false;

    // Keeps nodes alive; HTREEITEM lParam points into these shared_ptrs
    std::map<HTREEITEM, std::shared_ptr<NavidromeNode>> m_nodeMap;
    std::vector<std::shared_ptr<NavidromeNode>>          m_rootNodes;
};
