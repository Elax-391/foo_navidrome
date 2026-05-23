#pragma once
#import <Foundation/Foundation.h>

@class SubsonicSong;

// Custom URI scheme used by foo_navidrome to represent a single playable track.
//
// Format: navidrome://track/<songId>?title=...&artist=...&album=...&tracknumber=N
//                                   &date=YYYY&duration=SEC&coverArt=...&suffix=mp3
//
// Metadata is embedded in the URI so playlists render correctly without a network
// round-trip. The actual HTTP stream URL is built at decode time from the current
// Subsonic credentials, so playlists survive credential rotation / server URL
// changes.

extern NSString *const NavidromeURIScheme;     // @"navidrome"
extern NSString *const NavidromeURIPrefix;     // @"navidrome://track/"

// Builds a navidrome://track/<id>?... URI for a SubsonicSong.
NSString *NavidromeMakeTrackURI(SubsonicSong *song);

// Builds the same URI from discrete fields (used by NavidromeBrowserController,
// whose node objects don't carry a `suffix` field).
NSString *NavidromeMakeTrackURIWithFields(NSString *songId,
                                          NSString *title,
                                          NSString *artist,
                                          NSString *album,
                                          NSInteger track,
                                          NSInteger year,
                                          NSTimeInterval duration,
                                          NSString *coverArtId,
                                          NSString *suffix);
