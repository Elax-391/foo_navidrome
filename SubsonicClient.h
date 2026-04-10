#pragma once
#import <Foundation/Foundation.h>
#include "stdafx.h"

// ---------------------------------------------------------------------------
// Data model objects
// ---------------------------------------------------------------------------

@interface SubsonicArtist : NSObject
@property (nonatomic, copy) NSString *artistId;
@property (nonatomic, copy) NSString *name;
@property (nonatomic, assign) NSInteger albumCount;
@property (nonatomic, copy) NSString *coverArtId;
@end

@interface SubsonicAlbum : NSObject
@property (nonatomic, copy) NSString *albumId;
@property (nonatomic, copy) NSString *name;
@property (nonatomic, copy) NSString *artist;
@property (nonatomic, copy) NSString *artistId;
@property (nonatomic, assign) NSInteger songCount;
@property (nonatomic, assign) NSInteger year;
@property (nonatomic, copy) NSString *coverArtId;
@end

@interface SubsonicSong : NSObject
@property (nonatomic, copy) NSString *songId;
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *artist;
@property (nonatomic, copy) NSString *artistId;
@property (nonatomic, copy) NSString *album;
@property (nonatomic, copy) NSString *albumId;
@property (nonatomic, assign) NSInteger track;
@property (nonatomic, assign) NSInteger year;
@property (nonatomic, assign) NSTimeInterval duration;  // seconds
@property (nonatomic, copy) NSString *coverArtId;
@property (nonatomic, copy) NSString *suffix;           // mp3, flac, etc.
@end

// ---------------------------------------------------------------------------
// Subsonic API client (Singleton)
// ---------------------------------------------------------------------------

@interface SubsonicClient : NSObject

+ (instancetype)sharedClient;

// Returns YES if server URL + credentials are configured
- (BOOL)isConfigured;

// Test connection — returns YES on success, sets *error on failure
- (BOOL)pingWithError:(NSError **)error;

// Browse hierarchy
- (NSArray<SubsonicArtist *> *)getArtistsWithError:(NSError **)error;
- (NSArray<SubsonicAlbum *> *)getAlbumsForArtist:(NSString *)artistId
                                            error:(NSError **)error;
- (NSArray<SubsonicSong *> *)getSongsForAlbum:(NSString *)albumId
                                         error:(NSError **)error;

// Search (returns dict with keys "artists", "albums", "songs")
- (NSDictionary *)search:(NSString *)query error:(NSError **)error;

// URL builders — no network required
// Returns the authenticated HTTP stream URL for foobar2000 to play directly.
// coverArtId is embedded as a query param so the art extractor can retrieve it.
- (NSString *)streamURLForSongId:(NSString *)songId coverArtId:(NSString *)coverArtId;
// Returns cover art URL (size 0 = original)
- (NSURL *)coverArtURLForId:(NSString *)coverArtId size:(NSInteger)size;

@end
