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

    DECLARE_WND_CLASS(L"foo_navidrome_BrowserWnd")

    BEGIN_MSG_MAP(BrowserWindow)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_SIZE(OnSize)
        MESSAGE_HANDLER(WM_NAVIDROME_LOADED,   OnNavidromeLoaded)
        MESSAGE_HANDLER(WM_NAVIDROME_CHILDREN, OnNavidromeChildren)
        NOTIFY_CODE_HANDLER_EX(TVN_ITEMEXPANDING, OnTreeExpanding)
        NOTIFY_CODE_HANDLER_EX(NM_DBLCLK,        OnTreeDblClick)
        COMMAND_ID_HANDLER_EX(IDC_ADD,     OnAdd)
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
    };

    LRESULT OnCreate(LPCREATESTRUCT);
    void    OnDestroy();
    LRESULT OnSize(UINT, CSize);
    LRESULT OnNavidromeLoaded(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnNavidromeChildren(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnTreeExpanding(LPNMHDR);
    LRESULT OnTreeDblClick(LPNMHDR);
    void    OnAdd(UINT, int, HWND);
    void    OnPlay(UINT, int, HWND);
    void    OnRefresh(UINT, int, HWND);
    void    OnSearchChanged(UINT, int, HWND);

    void    loadArtists();
    void    populateRoot(LoadedPayload* payload);
    void    populateChildren(LoadedPayload* payload);
    HTREEITEM insertNode(HTREEITEM parent, std::shared_ptr<NavidromeNode> node);
    std::shared_ptr<NavidromeNode> nodeForItem(HTREEITEM hItem);
    void    collectSongsDeep(std::shared_ptr<NavidromeNode> node,
                             std::vector<std::shared_ptr<NavidromeNode>>& out);
    void    enqueueNodes(std::vector<std::shared_ptr<NavidromeNode>> songs, bool play);
    void    setStatus(const std::string& msg);

    CTreeViewCtrl m_tree;
    CEdit         m_search;
    CButton       m_addBtn, m_playBtn, m_refreshBtn;
    CStatic       m_status;

    // Keeps nodes alive; HTREEITEM lParam points into these shared_ptrs
    std::map<HTREEITEM, std::shared_ptr<NavidromeNode>> m_nodeMap;
    std::vector<std::shared_ptr<NavidromeNode>>          m_rootNodes;
};
