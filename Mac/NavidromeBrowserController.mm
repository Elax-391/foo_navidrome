#import "NavidromeBrowserController.h"
#import "../NavidromeInput.h"
#include <SDK/playlist.h>
#include <SDK/metadb.h>
#include <SDK/playable_location.h>

// ---------------------------------------------------------------------------
// Helper: format seconds as M:SS
// ---------------------------------------------------------------------------
static NSString *formatDuration(NSTimeInterval secs) {
    int s = (int)secs;
    return [NSString stringWithFormat:@"%d:%02d", s / 60, s % 60];
}

// ---------------------------------------------------------------------------
// NavidromeNode
// ---------------------------------------------------------------------------

@implementation NavidromeNode

+ (instancetype)artistNode:(SubsonicArtist *)a {
    NavidromeNode *n = [NavidromeNode new];
    n.type        = NavidromeNodeTypeArtist;
    n.nodeId      = a.artistId;
    n.displayName = a.name;
    n.coverArtId  = a.coverArtId;
    n.children    = [NSMutableArray array];
    return n;
}

+ (instancetype)albumNode:(SubsonicAlbum *)a {
    NavidromeNode *n = [NavidromeNode new];
    n.type        = NavidromeNodeTypeAlbum;
    n.nodeId      = a.albumId;
    n.displayName = a.name;
    n.subtitle    = a.artist;
    n.coverArtId  = a.coverArtId;
    n.children    = [NSMutableArray array];
    return n;
}

+ (instancetype)songNode:(SubsonicSong *)s {
    NavidromeNode *n = [NavidromeNode new];
    n.type         = NavidromeNodeTypeSong;
    n.nodeId       = s.songId;
    n.displayName  = s.title;
    n.subtitle     = s.artist;
    n.albumName    = s.album;
    n.trackNumber  = s.track;
    n.year         = s.year;
    n.duration     = s.duration;
    n.coverArtId   = s.coverArtId;
    n.children     = [NSMutableArray array];
    n.childrenLoaded = YES;  // Songs are always leaves
    return n;
}

+ (instancetype)loadingNode {
    NavidromeNode *n = [NavidromeNode new];
    n.type        = NavidromeNodeTypeLoading;
    n.displayName = @"Loading…";
    n.children    = [NSMutableArray array];
    n.childrenLoaded = YES;
    return n;
}

+ (instancetype)errorNodeWithMessage:(NSString *)msg {
    NavidromeNode *n = [NavidromeNode new];
    n.type        = NavidromeNodeTypeError;
    n.displayName = msg;
    n.children    = [NSMutableArray array];
    n.childrenLoaded = YES;
    return n;
}

- (BOOL)isLeaf { return self.type == NavidromeNodeTypeSong ||
                        self.type == NavidromeNodeTypeLoading ||
                        self.type == NavidromeNodeTypeError; }

@end

// ---------------------------------------------------------------------------
// NavidromeBrowserController
// ---------------------------------------------------------------------------

// Outline view subclass that turns Return / Enter into a "commit" action.
// Key equivalents (default buttons) intercept Return before -keyDown:, so the
// Add button no longer claims @"\r" — this is the only Return handler now.
@interface NavidromeCommitOutlineView : NSOutlineView
@property (nonatomic, copy) void (^onCommit)(void);
@end

@implementation NavidromeCommitOutlineView
- (void)keyDown:(NSEvent *)event {
    NSString *chars = event.charactersIgnoringModifiers;
    unichar c = chars.length ? [chars characterAtIndex:0] : 0;
    if ((c == NSCarriageReturnCharacter || c == NSEnterCharacter) && self.onCommit) {
        self.onCommit();
        return;
    }
    [super keyDown:event];
}
@end

@interface NavidromeBrowserController ()
// Root artist nodes
@property (nonatomic, strong) NSMutableArray<NavidromeNode *> *rootNodes;
// YES when hosted in the standalone NSWindow (vs. embedded in the prefs page);
// only then does the Enter shortcut close the window after queueing.
@property (nonatomic, assign) BOOL standalone;
// Controls
@property (nonatomic, strong) NSOutlineView  *outlineView;
@property (nonatomic, strong) NSSearchField  *searchField;
@property (nonatomic, strong) NSProgressIndicator *spinner;
@property (nonatomic, strong) NSTextField    *statusLabel;
// Filtered nodes when searching
@property (nonatomic, strong) NSMutableArray<NavidromeNode *> *filteredNodes;
@property (nonatomic, assign) BOOL isSearching;
@end

@implementation NavidromeBrowserController

- (instancetype)init {
    self = [super initWithNibName:nil bundle:nil];
    if (self) {
        _rootNodes     = [NSMutableArray array];
        _filteredNodes = [NSMutableArray array];
    }
    return self;
}

- (void)loadView {
    NSView *content = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 520, 600)];
    content.wantsLayer = YES;
    self.view = content;
    [self buildUI];
    [self loadArtists];
}

- (void)buildUI {
    NSView *content = self.view;

    // ── Search field (top) ──────────────────────────────────────────────
    _searchField = [NSSearchField new];
    _searchField.translatesAutoresizingMaskIntoConstraints = NO;
    _searchField.placeholderString = @"Search artists, albums, songs…";
    _searchField.target = self;
    _searchField.action = @selector(searchChanged:);
    [content addSubview:_searchField];

    // ── Spinner (top-right corner) ───────────────────────────────────────
    _spinner = [[NSProgressIndicator alloc] init];
    _spinner.translatesAutoresizingMaskIntoConstraints = NO;
    _spinner.style = NSProgressIndicatorStyleSpinning;
    _spinner.controlSize = NSControlSizeSmall;
    [_spinner setDisplayedWhenStopped:NO];
    [content addSubview:_spinner];

    // ── Outline view (center) ────────────────────────────────────────────
    NavidromeCommitOutlineView *outline = [[NavidromeCommitOutlineView alloc] init];
    __weak typeof(self) weakSelf = self;
    outline.onCommit = ^{ [weakSelf commitSelectionFromKeyboard]; };
    _outlineView = outline;
    _outlineView.dataSource = self;
    _outlineView.delegate   = self;
    _outlineView.usesAlternatingRowBackgroundColors = YES;
    _outlineView.rowHeight = 20.0;
    _outlineView.allowsMultipleSelection = YES;
    _outlineView.autoresizesOutlineColumn = NO;
    _outlineView.target = self;
    _outlineView.doubleAction = @selector(doubleClicked:);

    // Columns
    NSTableColumn *nameCol = [[NSTableColumn alloc] initWithIdentifier:@"name"];
    nameCol.title = @"Name";
    nameCol.minWidth = 160;
    nameCol.width = 280;
    [_outlineView addTableColumn:nameCol];
    _outlineView.outlineTableColumn = nameCol;

    NSTableColumn *subCol = [[NSTableColumn alloc] initWithIdentifier:@"sub"];
    subCol.title = @"Artist / Album";
    subCol.minWidth = 80;
    subCol.width = 160;
    [_outlineView addTableColumn:subCol];

    NSTableColumn *durCol = [[NSTableColumn alloc] initWithIdentifier:@"dur"];
    durCol.title = @"Duration";
    durCol.minWidth = 50;
    durCol.width = 60;
    [_outlineView addTableColumn:durCol];

    NSScrollView *scrollView = [[NSScrollView alloc] init];
    scrollView.translatesAutoresizingMaskIntoConstraints = NO;
    scrollView.documentView = _outlineView;
    scrollView.hasVerticalScroller = YES;
    scrollView.hasHorizontalScroller = NO;
    scrollView.borderType = NSBezelBorder;
    [content addSubview:scrollView];

    // ── Status label (bottom-left) ───────────────────────────────────────
    _statusLabel = [NSTextField labelWithString:@""];
    _statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    _statusLabel.textColor = [NSColor secondaryLabelColor];
    _statusLabel.font = [NSFont systemFontOfSize:11];
    [content addSubview:_statusLabel];

    // ── Buttons (bottom-right) ───────────────────────────────────────────
    NSButton *addBtn = [NSButton buttonWithTitle:@"Add to Playlist"
                                          target:self
                                          action:@selector(addToPlaylist:)];
    addBtn.translatesAutoresizingMaskIntoConstraints = NO;
    // Return is handled by the outline view (commit + play + close); don't let
    // the default-button key equivalent steal it.

    NSButton *playBtn = [NSButton buttonWithTitle:@"Play Now"
                                           target:self
                                           action:@selector(playNow:)];
    playBtn.translatesAutoresizingMaskIntoConstraints = NO;

    NSButton *refreshBtn = [NSButton buttonWithTitle:@"Refresh"
                                              target:self
                                              action:@selector(refresh:)];
    refreshBtn.translatesAutoresizingMaskIntoConstraints = NO;

    [content addSubview:addBtn];
    [content addSubview:playBtn];
    [content addSubview:refreshBtn];

    // ── Auto-layout ──────────────────────────────────────────────────────
    CGFloat pad = 10;
    [NSLayoutConstraint activateConstraints:@[
        // Search field
        [_searchField.topAnchor constraintEqualToAnchor:content.topAnchor constant:pad],
        [_searchField.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:pad],
        [_searchField.trailingAnchor constraintEqualToAnchor:_spinner.leadingAnchor constant:-pad],

        // Spinner
        [_spinner.centerYAnchor constraintEqualToAnchor:_searchField.centerYAnchor],
        [_spinner.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-pad],

        // Scroll view
        [scrollView.topAnchor constraintEqualToAnchor:_searchField.bottomAnchor constant:pad],
        [scrollView.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:pad],
        [scrollView.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-pad],
        [scrollView.bottomAnchor constraintEqualToAnchor:addBtn.topAnchor constant:-pad],

        // Bottom row buttons
        [addBtn.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-pad],
        [addBtn.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-pad],

        [playBtn.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-pad],
        [playBtn.trailingAnchor constraintEqualToAnchor:addBtn.leadingAnchor constant:-pad],

        [refreshBtn.bottomAnchor constraintEqualToAnchor:content.bottomAnchor constant:-pad],
        [refreshBtn.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:pad],

        // Status label
        [_statusLabel.centerYAnchor constraintEqualToAnchor:addBtn.centerYAnchor],
        [_statusLabel.leadingAnchor constraintEqualToAnchor:refreshBtn.trailingAnchor constant:pad],
        [_statusLabel.trailingAnchor constraintEqualToAnchor:playBtn.leadingAnchor constant:-pad],
    ]];
}

// ---------------------------------------------------------------------------
// Data loading
// ---------------------------------------------------------------------------

- (void)loadArtists {
    if (![SubsonicClient.sharedClient isConfigured]) {
        _statusLabel.stringValue = @"Not configured — set server in Preferences > Navidrome";
        return;
    }
    [_spinner startAnimation:nil];
    _statusLabel.stringValue = @"Loading artists…";
    [_rootNodes removeAllObjects];
    [_outlineView reloadData];

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSArray<SubsonicArtist *> *artists = [SubsonicClient.sharedClient getArtistsWithError:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            [_spinner stopAnimation:nil];
            if (err || !artists) {
                _statusLabel.stringValue = [NSString stringWithFormat:@"Error: %@", err.localizedDescription ?: @"Unknown"];
                return;
            }
            for (SubsonicArtist *a in artists) {
                [_rootNodes addObject:[NavidromeNode artistNode:a]];
            }
            _statusLabel.stringValue = [NSString stringWithFormat:@"%lu artists", (unsigned long)artists.count];
            [_outlineView reloadData];
        });
    });
}

- (void)loadChildrenOfNode:(NavidromeNode *)node inOutlineView:(NSOutlineView *)ov {
    if (node.childrenLoaded || node.isLoading) return;
    node.isLoading = YES;

    // Insert temporary "Loading…" placeholder
    [node.children removeAllObjects];
    [node.children addObject:[NavidromeNode loadingNode]];
    [ov reloadItem:node reloadChildren:YES];

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSMutableArray<NavidromeNode *> *childNodes = [NSMutableArray array];

        if (node.type == NavidromeNodeTypeArtist) {
            NSArray<SubsonicAlbum *> *albums =
                [SubsonicClient.sharedClient getAlbumsForArtist:node.nodeId error:&err];
            if (!err) {
                for (SubsonicAlbum *a in albums)
                    [childNodes addObject:[NavidromeNode albumNode:a]];
            }
        } else if (node.type == NavidromeNodeTypeAlbum) {
            NSArray<SubsonicSong *> *songs =
                [SubsonicClient.sharedClient getSongsForAlbum:node.nodeId error:&err];
            if (!err) {
                for (SubsonicSong *s in songs)
                    [childNodes addObject:[NavidromeNode songNode:s]];
            }
        }

        dispatch_async(dispatch_get_main_queue(), ^{
            node.isLoading = NO;
            node.childrenLoaded = YES;
            [node.children removeAllObjects];
            if (err) {
                [node.children addObject:[NavidromeNode errorNodeWithMessage:
                    [NSString stringWithFormat:@"Error: %@", err.localizedDescription]]];
            } else {
                [node.children addObjectsFromArray:childNodes];
            }
            [ov reloadItem:node reloadChildren:YES];
        });
    });
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

- (void)searchChanged:(id)sender {
    NSString *query = [_searchField stringValue];
    if (query.length < 2) {
        _isSearching = NO;
        [_filteredNodes removeAllObjects];
        [_outlineView reloadData];
        return;
    }

    _isSearching = YES;
    [_spinner startAnimation:nil];
    _statusLabel.stringValue = @"Searching…";

    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSError *err = nil;
        NSDictionary *results = [SubsonicClient.sharedClient search:query error:&err];
        dispatch_async(dispatch_get_main_queue(), ^{
            [_spinner stopAnimation:nil];
            [_filteredNodes removeAllObjects];

            if (err || !results) {
                _statusLabel.stringValue = [NSString stringWithFormat:@"Search error: %@", err.localizedDescription];
                [_outlineView reloadData];
                return;
            }

            // Build flat list of song nodes matching the search
            NSArray<SubsonicSong *> *songs = results[@"songs"];
            for (SubsonicSong *s in songs)
                [_filteredNodes addObject:[NavidromeNode songNode:s]];

            NSUInteger total = [results[@"artists"] count] + [results[@"albums"] count] + songs.count;
            _statusLabel.stringValue = [NSString stringWithFormat:@"%lu songs found", (unsigned long)songs.count];
            (void)total;

            [_outlineView reloadData];
        });
    });
}

// ---------------------------------------------------------------------------
// Adding to playlist
// ---------------------------------------------------------------------------

// Returns all selected nodes (artists, albums, or songs).
- (NSArray<NavidromeNode *> *)selectedNodes {
    NSMutableArray<NavidromeNode *> *nodes = [NSMutableArray array];
    NSIndexSet *selected = [_outlineView selectedRowIndexes];
    [selected enumerateIndexesUsingBlock:^(NSUInteger idx, BOOL *stop) {
        NavidromeNode *node = [_outlineView itemAtRow:idx];
        if (node.type == NavidromeNodeTypeSong ||
            node.type == NavidromeNodeTypeArtist ||
            node.type == NavidromeNodeTypeAlbum) {
            [nodes addObject:node];
        }
    }];
    return nodes;
}

// Entry point for Add/Play actions — handles async deep loading for artists/albums.
- (void)addNodesToPlaylist:(NSArray<NavidromeNode *> *)nodes play:(BOOL)play {
    [self addNodesToPlaylist:nodes play:play closeWhenDone:NO];
}

// closeWhenDone closes the standalone window once tracks are queued — used by
// the Enter shortcut ("queue, play, and dismiss"). No-op when embedded.
- (void)addNodesToPlaylist:(NSArray<NavidromeNode *> *)nodes
                      play:(BOOL)play
             closeWhenDone:(BOOL)closeWhenDone {
    if (nodes.count == 0) {
        _statusLabel.stringValue = @"Select at least one item first";
        return;
    }

    // Fast path: everything is already a song node
    BOOL allSongs = YES;
    for (NavidromeNode *n in nodes)
        if (n.type != NavidromeNodeTypeSong) { allSongs = NO; break; }
    if (allSongs) {
        [self enqueueNodes:nodes play:play];
        if (closeWhenDone) [self closeStandaloneWindow];
        return;
    }

    [_spinner startAnimation:nil];
    _statusLabel.stringValue = @"Loading tracks…";

    // Copy nodes list for use on background thread
    NSArray *nodesCopy = [nodes copy];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        NSMutableArray<NavidromeNode *> *songs = [NSMutableArray array];
        NSError *err = nil;
        for (NavidromeNode *node in nodesCopy) {
            [self collectSongsDeep:node into:songs error:&err];
            if (err) break;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            [_spinner stopAnimation:nil];
            if (err) {
                _statusLabel.stringValue = [NSString stringWithFormat:@"Error: %@",
                                            err.localizedDescription];
            } else {
                [self enqueueNodes:songs play:play];
                if (closeWhenDone) [self closeStandaloneWindow];
            }
        });
    });
}

// Return / Enter in the tree: queue the selection, start playing the first
// track, and close the window (standalone only).
- (void)commitSelectionFromKeyboard {
    [self addNodesToPlaylist:[self selectedNodes] play:YES closeWhenDone:YES];
}

- (void)closeStandaloneWindow {
    if (self.standalone) [self.view.window close];
}

// Synchronous deep song collector — must be called from a background thread.
- (void)collectSongsDeep:(NavidromeNode *)node
                    into:(NSMutableArray<NavidromeNode *> *)songs
                   error:(NSError **)outError {
    if (node.type == NavidromeNodeTypeSong) {
        [songs addObject:node];
        return;
    }
    if (node.type == NavidromeNodeTypeLoading || node.type == NavidromeNodeTypeError)
        return;

    if (node.type == NavidromeNodeTypeAlbum) {
        if (node.childrenLoaded && node.children.count > 0) {
            for (NavidromeNode *child in node.children)
                [self collectSongsDeep:child into:songs error:outError];
        } else {
            NSArray<SubsonicSong *> *raw =
                [SubsonicClient.sharedClient getSongsForAlbum:node.nodeId error:outError];
            if (outError && *outError) return;
            for (SubsonicSong *s in raw)
                [songs addObject:[NavidromeNode songNode:s]];
        }
        return;
    }

    if (node.type == NavidromeNodeTypeArtist) {
        NSArray<NavidromeNode *> *albumNodes;
        if (node.childrenLoaded && node.children.count > 0) {
            albumNodes = node.children;
        } else {
            NSArray<SubsonicAlbum *> *albums =
                [SubsonicClient.sharedClient getAlbumsForArtist:node.nodeId error:outError];
            if (outError && *outError) return;
            NSMutableArray *tmp = [NSMutableArray array];
            for (SubsonicAlbum *a in albums)
                [tmp addObject:[NavidromeNode albumNode:a]];
            albumNodes = tmp;
        }
        for (NavidromeNode *albumNode in albumNodes) {
            [self collectSongsDeep:albumNode into:songs error:outError];
            if (outError && *outError) return;
        }
    }
}

- (void)enqueueNodes:(NSArray<NavidromeNode *> *)songNodes play:(BOOL)play {
    if (songNodes.count == 0) {
        _statusLabel.stringValue = @"No songs selected";
        return;
    }
    // Build metadb handle list. Each item is identified by a navidrome://
    // URI — our input handler resolves it to the current HTTP stream at
    // decode time, so playlists survive credential / server URL changes.
    metadb_handle_list tracks;
    auto hintList = metadb_io_v2::get()->create_hint_list();

    for (NavidromeNode *node in songNodes) {
        NSString *uri = NavidromeMakeTrackURIWithFields(node.nodeId,
                                                        node.displayName,
                                                        node.subtitle,
                                                        node.albumName,
                                                        node.trackNumber,
                                                        node.year,
                                                        node.duration,
                                                        node.coverArtId ?: @"",
                                                        @"");
        if (!uri) continue;

        metadb_handle_ptr handle;
        playable_location_impl loc;
        loc.set_path([uri UTF8String]);
        loc.set_subsong(0);
        metadb::get()->handle_create(handle, loc);
        tracks += handle;

        // Provide metadata hints so foobar displays correct info immediately
        file_info_impl info;
        if (node.displayName.length)
            info.meta_set("title", [node.displayName UTF8String]);
        if (node.subtitle.length)
            info.meta_set("artist", [node.subtitle UTF8String]);
        if (node.albumName.length)
            info.meta_set("album", [node.albumName UTF8String]);
        if (node.trackNumber > 0)
            info.meta_set("tracknumber", pfc::format_int(node.trackNumber));
        if (node.year > 0)
            info.meta_set("date", pfc::format_int(node.year));
        if (node.duration > 0)
            info.set_length(node.duration);

        hintList->add_hint(handle, info, filestats_invalid, true);
    }

    hintList->on_done();

    auto tracksCopy = std::make_shared<metadb_handle_list>(tracks);
    bool doPlay = play;

    fb2k::inMainThread([tracksCopy, doPlay] {
        auto pm = playlist_manager::get();
        t_size activePlaylist = pm->get_active_playlist();
        if (activePlaylist == pfc_infinite) {
            pm->create_playlist("Navidrome", ~0, pfc_infinite);
            activePlaylist = pm->get_active_playlist();
        }
        t_size insertPos = pm->playlist_get_item_count(activePlaylist);
        pm->playlist_add_items(activePlaylist, *tracksCopy, pfc::bit_array_false());

        if (doPlay && tracksCopy->get_count() > 0) {
            pm->set_active_playlist(activePlaylist);
            pm->playlist_execute_default_action(activePlaylist, insertPos);
        }
    });

    _statusLabel.stringValue = [NSString stringWithFormat:@"Added %lu tracks", (unsigned long)songNodes.count];
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

- (IBAction)addToPlaylist:(id)sender {
    [self addNodesToPlaylist:[self selectedNodes] play:NO];
}

- (IBAction)playNow:(id)sender {
    [self addNodesToPlaylist:[self selectedNodes] play:YES];
}

- (IBAction)refresh:(id)sender {
    _isSearching = NO;
    _searchField.stringValue = @"";
    [self loadArtists];
}

- (void)doubleClicked:(id)sender {
    NSInteger row = [_outlineView clickedRow];
    if (row < 0) return;
    NavidromeNode *node = [_outlineView itemAtRow:row];
    if (!node) return;

    if (node.type == NavidromeNodeTypeSong) {
        [self addNodesToPlaylist:@[node] play:YES];
    } else {
        // Toggle expand/collapse
        if ([_outlineView isItemExpanded:node])
            [_outlineView collapseItem:node];
        else
            [_outlineView expandItem:node];
    }
}

// ---------------------------------------------------------------------------
// NSOutlineViewDataSource
// ---------------------------------------------------------------------------

- (NSInteger)outlineView:(NSOutlineView *)ov numberOfChildrenOfItem:(id)item {
    if (item == nil) {
        return (NSInteger)(_isSearching ? _filteredNodes.count : _rootNodes.count);
    }
    NavidromeNode *node = (NavidromeNode *)item;
    if (node.isLeaf) return 0;
    // If not yet loaded, show 1 (will trigger loading when expanded)
    if (!node.childrenLoaded && !node.isLoading) return 1;
    return (NSInteger)node.children.count;
}

- (id)outlineView:(NSOutlineView *)ov child:(NSInteger)index ofItem:(id)item {
    if (item == nil) {
        NSArray *roots = _isSearching ? _filteredNodes : _rootNodes;
        return roots[(NSUInteger)index];
    }
    NavidromeNode *node = (NavidromeNode *)item;
    if (!node.childrenLoaded && !node.isLoading && index == 0) {
        // Return a temporary node while we trigger loading
        return [NavidromeNode loadingNode];
    }
    return node.children[(NSUInteger)index];
}

- (BOOL)outlineView:(NSOutlineView *)ov isItemExpandable:(id)item {
    NavidromeNode *node = (NavidromeNode *)item;
    return !node.isLeaf;
}

// ---------------------------------------------------------------------------
// NSOutlineViewDelegate
// ---------------------------------------------------------------------------

- (NSView *)outlineView:(NSOutlineView *)ov
     viewForTableColumn:(NSTableColumn *)tableColumn
                   item:(id)item {
    NavidromeNode *node = (NavidromeNode *)item;

    NSTextField *cell = [ov makeViewWithIdentifier:tableColumn.identifier owner:self];
    if (!cell) {
        cell = [NSTextField labelWithString:@""];
        cell.identifier = tableColumn.identifier;
    }

    // Style placeholders differently
    if (node.type == NavidromeNodeTypeLoading || node.type == NavidromeNodeTypeError) {
        cell.textColor = [NSColor secondaryLabelColor];
        cell.stringValue = [tableColumn.identifier isEqualToString:@"name"] ? node.displayName : @"";
        return cell;
    }

    cell.textColor = [NSColor labelColor];

    if ([tableColumn.identifier isEqualToString:@"name"]) {
        NSString *name = node.displayName ?: @"";
        if (node.type == NavidromeNodeTypeSong && node.trackNumber > 0)
            name = [NSString stringWithFormat:@"%ld. %@", (long)node.trackNumber, name];
        cell.stringValue = name;
    } else if ([tableColumn.identifier isEqualToString:@"sub"]) {
        cell.stringValue = node.subtitle ?: @"";
        cell.textColor = [NSColor secondaryLabelColor];
    } else if ([tableColumn.identifier isEqualToString:@"dur"]) {
        cell.stringValue = node.duration > 0 ? formatDuration(node.duration) : @"";
        cell.textColor = [NSColor secondaryLabelColor];
        cell.alignment = NSTextAlignmentRight;
    }

    return cell;
}

- (void)outlineViewItemWillExpand:(NSNotification *)notification {
    NavidromeNode *node = notification.userInfo[@"NSObject"];
    if (node && !node.childrenLoaded && !node.isLoading) {
        [self loadChildrenOfNode:node inOutlineView:_outlineView];
    }
}

@end

// ---------------------------------------------------------------------------
// Standalone window wrapper for the File menu and library_viewer.activate().
// Each call creates a fresh browser controller and wraps it in an NSWindow.
// The window+controller pair is retained in a static set until the window
// closes, at which point it's released. Multiple windows can coexist.
// ---------------------------------------------------------------------------

@interface NavidromeBrowserWindowOwner : NSObject <NSWindowDelegate>
@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) NavidromeBrowserController *vc;
@end

static NSMutableSet<NavidromeBrowserWindowOwner *> *gStandaloneOwners = nil;

@implementation NavidromeBrowserWindowOwner
- (void)windowWillClose:(NSNotification *)note {
    [gStandaloneOwners removeObject:self];
}
@end

void NavidromeShowStandaloneBrowser(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!gStandaloneOwners) gStandaloneOwners = [NSMutableSet set];

        NavidromeBrowserWindowOwner *owner = [NavidromeBrowserWindowOwner new];
        owner.vc = [NavidromeBrowserController new];
        owner.vc.standalone = YES;   // enables the Enter = queue+play+close shortcut

        NSWindow *win = [[NSWindow alloc]
                         initWithContentRect:NSMakeRect(0, 0, 520, 600)
                         styleMask:(NSWindowStyleMaskTitled |
                                    NSWindowStyleMaskClosable |
                                    NSWindowStyleMaskMiniaturizable |
                                    NSWindowStyleMaskResizable)
                         backing:NSBackingStoreBuffered
                         defer:NO];
        win.title = @"Navidrome Browser";
        win.minSize = NSMakeSize(360, 300);
        win.releasedWhenClosed = NO;
        win.contentViewController = owner.vc;
        win.delegate = owner;
        [win center];

        owner.window = win;
        [gStandaloneOwners addObject:owner];
        [win makeKeyAndOrderFront:nil];
    });
}
