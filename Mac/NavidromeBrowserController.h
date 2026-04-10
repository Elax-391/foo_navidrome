#pragma once
#import <Cocoa/Cocoa.h>
#include "../SubsonicClient.h"

// ---------------------------------------------------------------------------
// Tree node types for the NSOutlineView
// ---------------------------------------------------------------------------
typedef NS_ENUM(NSInteger, NavidromeNodeType) {
    NavidromeNodeTypeArtist,
    NavidromeNodeTypeAlbum,
    NavidromeNodeTypeSong,
    NavidromeNodeTypeLoading,   // Placeholder while loading children
    NavidromeNodeTypeError,     // Placeholder when load fails
};

@interface NavidromeNode : NSObject

@property (nonatomic, assign) NavidromeNodeType type;
@property (nonatomic, copy)   NSString *nodeId;
@property (nonatomic, copy)   NSString *displayName;
@property (nonatomic, copy)   NSString *subtitle;       // artist (for albums/songs)
@property (nonatomic, copy)   NSString *albumName;      // album name (for song nodes)
@property (nonatomic, assign) NSInteger trackNumber;
@property (nonatomic, assign) NSInteger year;
@property (nonatomic, assign) NSTimeInterval duration;
@property (nonatomic, copy)   NSString *coverArtId;

// True if children have been loaded (may still be empty)
@property (nonatomic, assign) BOOL childrenLoaded;
// True while async load is in progress
@property (nonatomic, assign) BOOL isLoading;
// Child nodes (albums for artist nodes, songs for album nodes)
@property (nonatomic, strong) NSMutableArray<NavidromeNode *> *children;

// Convenience constructors
+ (instancetype)artistNode:(SubsonicArtist *)artist;
+ (instancetype)albumNode:(SubsonicAlbum *)album;
+ (instancetype)songNode:(SubsonicSong *)song;
+ (instancetype)loadingNode;
+ (instancetype)errorNodeWithMessage:(NSString *)msg;

- (BOOL)isLeaf;  // Songs are leaves; artists & albums can expand

@end

// ---------------------------------------------------------------------------
// Browser window controller
// ---------------------------------------------------------------------------

@interface NavidromeBrowserController : NSWindowController
                                      <NSOutlineViewDataSource,
                                       NSOutlineViewDelegate>

+ (instancetype)sharedBrowser;

@end
