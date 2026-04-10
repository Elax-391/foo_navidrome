#import "SubsonicClient.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#import <CommonCrypto/CommonDigest.h>
#pragma clang diagnostic pop

// Forward declaration of config vars (defined in NavidromePlugin.mm)
namespace navidrome {
    extern cfg_string cfg_server_url;
    extern cfg_string cfg_username;
    extern cfg_string cfg_password;
    extern cfg_string cfg_salt;  // Fixed salt generated once at component load
}

// ---------------------------------------------------------------------------
// Data model implementations
// ---------------------------------------------------------------------------

@implementation SubsonicArtist
- (NSString *)description {
    return [NSString stringWithFormat:@"<SubsonicArtist %@ %@>", _artistId, _name];
}
@end

@implementation SubsonicAlbum
- (NSString *)description {
    return [NSString stringWithFormat:@"<SubsonicAlbum %@ %@>", _albumId, _name];
}
@end

@implementation SubsonicSong
- (NSString *)description {
    return [NSString stringWithFormat:@"<SubsonicSong %@ %@>", _songId, _title];
}
@end

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static NSString *md5HexString(NSString *input) {
    const char *cStr = [input UTF8String];
    unsigned char digest[CC_MD5_DIGEST_LENGTH];
    CC_MD5(cStr, (CC_LONG)strlen(cStr), digest);
    NSMutableString *hex = [NSMutableString stringWithCapacity:CC_MD5_DIGEST_LENGTH * 2];
    for (int i = 0; i < CC_MD5_DIGEST_LENGTH; i++) {
        [hex appendFormat:@"%02x", digest[i]];
    }
    return hex;
}

static NSString *urlEncode(NSString *s) {
    return [s stringByAddingPercentEncodingWithAllowedCharacters:
            [NSCharacterSet URLQueryAllowedCharacterSet]];
}

// ---------------------------------------------------------------------------
// SubsonicClient
// ---------------------------------------------------------------------------

@interface SubsonicClient ()
@property (nonatomic, strong) NSURLSession *session;
@end

@implementation SubsonicClient

+ (instancetype)sharedClient {
    static SubsonicClient *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[SubsonicClient alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        NSURLSessionConfiguration *config = [NSURLSessionConfiguration defaultSessionConfiguration];
        config.timeoutIntervalForRequest = 15.0;
        config.timeoutIntervalForResource = 30.0;
        _session = [NSURLSession sessionWithConfiguration:config];
    }
    return self;
}

- (BOOL)isConfigured {
    pfc::string8 url  = navidrome::cfg_server_url.get();
    pfc::string8 user = navidrome::cfg_username.get();
    pfc::string8 pass = navidrome::cfg_password.get();
    return (url.length() > 0 && user.length() > 0 && pass.length() > 0);
}

// Build the common auth query string
- (NSString *)authParams {
    NSString *username = [NSString stringWithUTF8String:navidrome::cfg_username.get().c_str()];
    NSString *password = [NSString stringWithUTF8String:navidrome::cfg_password.get().c_str()];
    pfc::string8 saltPfc = navidrome::cfg_salt.get();
    NSString *salt = saltPfc.length() > 0
        ? [NSString stringWithUTF8String:saltPfc.c_str()]
        : @"navidrome";
    NSString *token    = md5HexString([password stringByAppendingString:salt]);
    return [NSString stringWithFormat:@"u=%@&t=%@&s=%@&v=1.16.1&c=foo_navidrome&f=json",
            urlEncode(username), token, salt];
}

// Build a full API URL for the given endpoint + extra params
- (NSURL *)urlForEndpoint:(NSString *)endpoint params:(NSString *)params {
    NSString *base = [NSString stringWithUTF8String:navidrome::cfg_server_url.get().c_str()];
    // Strip trailing slash
    while ([base hasSuffix:@"/"]) {
        base = [base substringToIndex:base.length - 1];
    }
    NSString *auth = [self authParams];
    NSString *full;
    if (params.length > 0) {
        full = [NSString stringWithFormat:@"%@/rest/%@?%@&%@", base, endpoint, auth, params];
    } else {
        full = [NSString stringWithFormat:@"%@/rest/%@?%@", base, endpoint, auth];
    }
    return [NSURL URLWithString:full];
}

// Synchronous HTTP GET, returns parsed JSON or nil
- (NSDictionary *)fetchJSON:(NSURL *)url error:(NSError **)outError {
    __block NSData *responseData = nil;
    __block NSError *taskError = nil;
    __block NSHTTPURLResponse *httpResponse = nil;

    dispatch_semaphore_t sema = dispatch_semaphore_create(0);
    [[_session dataTaskWithURL:url completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
        responseData = data;
        taskError = error;
        httpResponse = (NSHTTPURLResponse *)response;
        dispatch_semaphore_signal(sema);
    }] resume];
    dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

    if (taskError) {
        if (outError) *outError = taskError;
        return nil;
    }
    if (httpResponse.statusCode != 200) {
        if (outError) {
            *outError = [NSError errorWithDomain:@"SubsonicClient"
                                           code:httpResponse.statusCode
                                       userInfo:@{NSLocalizedDescriptionKey:
                                                  [NSString stringWithFormat:@"HTTP %ld", (long)httpResponse.statusCode]}];
        }
        return nil;
    }
    if (!responseData) {
        if (outError) {
            *outError = [NSError errorWithDomain:@"SubsonicClient" code:-1
                                       userInfo:@{NSLocalizedDescriptionKey: @"Empty response"}];
        }
        return nil;
    }

    NSError *jsonError = nil;
    NSDictionary *json = [NSJSONSerialization JSONObjectWithData:responseData options:0 error:&jsonError];
    if (!json || jsonError) {
        if (outError) *outError = jsonError;
        return nil;
    }

    // Subsonic wraps everything in "subsonic-response"
    NSDictionary *root = json[@"subsonic-response"];
    if (!root) {
        if (outError) {
            *outError = [NSError errorWithDomain:@"SubsonicClient" code:-2
                                       userInfo:@{NSLocalizedDescriptionKey: @"Invalid response format"}];
        }
        return nil;
    }

    NSString *status = root[@"status"];
    if (![status isEqualToString:@"ok"]) {
        NSDictionary *err = root[@"error"];
        NSString *msg = err[@"message"] ?: @"Unknown Subsonic error";
        if (outError) {
            *outError = [NSError errorWithDomain:@"SubsonicClient"
                                           code:[err[@"code"] integerValue]
                                       userInfo:@{NSLocalizedDescriptionKey: msg}];
        }
        return nil;
    }

    return root;
}

// ---------------------------------------------------------------------------
// API Methods
// ---------------------------------------------------------------------------

- (BOOL)pingWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"ping.view" params:@""];
    NSDictionary *root = [self fetchJSON:url error:error];
    return root != nil;
}

- (NSArray<SubsonicArtist *> *)getArtistsWithError:(NSError **)error {
    NSURL *url = [self urlForEndpoint:@"getArtists.view" params:@""];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicArtist *> *result = [NSMutableArray array];
    NSDictionary *artistsObj = root[@"artists"];
    NSArray *indexArray = artistsObj[@"index"];

    for (NSDictionary *index in indexArray) {
        NSArray *artists = index[@"artist"];
        if (![artists isKindOfClass:[NSArray class]]) {
            // Single artist returned as dict
            if ([artists isKindOfClass:[NSDictionary class]]) {
                artists = @[(NSDictionary *)artists];
            } else {
                continue;
            }
        }
        for (NSDictionary *a in artists) {
            SubsonicArtist *artist = [[SubsonicArtist alloc] init];
            artist.artistId  = a[@"id"] ?: @"";
            artist.name      = a[@"name"] ?: @"Unknown Artist";
            artist.albumCount = [a[@"albumCount"] integerValue];
            artist.coverArtId = a[@"coverArt"] ?: @"";
            [result addObject:artist];
        }
    }

    return result;
}

- (NSArray<SubsonicAlbum *> *)getAlbumsForArtist:(NSString *)artistId error:(NSError **)error {
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(artistId)];
    NSURL *url = [self urlForEndpoint:@"getArtist.view" params:params];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicAlbum *> *result = [NSMutableArray array];
    NSDictionary *artistObj = root[@"artist"];
    NSArray *albums = artistObj[@"album"];
    if (![albums isKindOfClass:[NSArray class]]) {
        if ([albums isKindOfClass:[NSDictionary class]]) {
            albums = @[(NSDictionary *)albums];
        } else {
            return result;
        }
    }

    for (NSDictionary *a in albums) {
        SubsonicAlbum *album = [[SubsonicAlbum alloc] init];
        album.albumId    = a[@"id"] ?: @"";
        album.name       = a[@"name"] ?: @"Unknown Album";
        album.artist     = a[@"artist"] ?: @"";
        album.artistId   = a[@"artistId"] ?: artistId;
        album.songCount  = [a[@"songCount"] integerValue];
        album.year       = [a[@"year"] integerValue];
        album.coverArtId = a[@"coverArt"] ?: @"";
        [result addObject:album];
    }

    return result;
}

- (NSArray<SubsonicSong *> *)getSongsForAlbum:(NSString *)albumId error:(NSError **)error {
    NSString *params = [NSString stringWithFormat:@"id=%@", urlEncode(albumId)];
    NSURL *url = [self urlForEndpoint:@"getAlbum.view" params:params];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSMutableArray<SubsonicSong *> *result = [NSMutableArray array];
    NSDictionary *albumObj = root[@"album"];
    NSArray *songs = albumObj[@"song"];
    if (![songs isKindOfClass:[NSArray class]]) {
        if ([songs isKindOfClass:[NSDictionary class]]) {
            songs = @[(NSDictionary *)songs];
        } else {
            return result;
        }
    }

    for (NSDictionary *s in songs) {
        SubsonicSong *song = [[SubsonicSong alloc] init];
        song.songId     = s[@"id"] ?: @"";
        song.title      = s[@"title"] ?: @"Unknown Title";
        song.artist     = s[@"artist"] ?: @"";
        song.artistId   = s[@"artistId"] ?: @"";
        song.album      = s[@"album"] ?: @"";
        song.albumId    = s[@"albumId"] ?: albumId;
        song.track      = [s[@"track"] integerValue];
        song.year       = [s[@"year"] integerValue];
        song.duration   = [s[@"duration"] doubleValue];
        song.coverArtId = s[@"coverArt"] ?: @"";
        song.suffix     = s[@"suffix"] ?: @"";
        [result addObject:song];
    }

    return result;
}

- (NSDictionary *)search:(NSString *)query error:(NSError **)error {
    NSString *params = [NSString stringWithFormat:@"query=%@&artistCount=20&albumCount=20&songCount=50",
                        urlEncode(query)];
    NSURL *url = [self urlForEndpoint:@"search3.view" params:params];
    NSDictionary *root = [self fetchJSON:url error:error];
    if (!root) return nil;

    NSDictionary *searchResult = root[@"searchResult3"] ?: @{};

    // Parse artists
    NSMutableArray<SubsonicArtist *> *artists = [NSMutableArray array];
    NSArray *rawArtists = searchResult[@"artist"];
    if ([rawArtists isKindOfClass:[NSDictionary class]]) rawArtists = @[rawArtists];
    for (NSDictionary *a in rawArtists) {
        SubsonicArtist *artist = [[SubsonicArtist alloc] init];
        artist.artistId  = a[@"id"] ?: @"";
        artist.name      = a[@"name"] ?: @"";
        artist.coverArtId = a[@"coverArt"] ?: @"";
        [artists addObject:artist];
    }

    // Parse albums
    NSMutableArray<SubsonicAlbum *> *albums = [NSMutableArray array];
    NSArray *rawAlbums = searchResult[@"album"];
    if ([rawAlbums isKindOfClass:[NSDictionary class]]) rawAlbums = @[rawAlbums];
    for (NSDictionary *a in rawAlbums) {
        SubsonicAlbum *album = [[SubsonicAlbum alloc] init];
        album.albumId    = a[@"id"] ?: @"";
        album.name       = a[@"name"] ?: @"";
        album.artist     = a[@"artist"] ?: @"";
        album.artistId   = a[@"artistId"] ?: @"";
        album.coverArtId = a[@"coverArt"] ?: @"";
        [albums addObject:album];
    }

    // Parse songs
    NSMutableArray<SubsonicSong *> *songs = [NSMutableArray array];
    NSArray *rawSongs = searchResult[@"song"];
    if ([rawSongs isKindOfClass:[NSDictionary class]]) rawSongs = @[rawSongs];
    for (NSDictionary *s in rawSongs) {
        SubsonicSong *song = [[SubsonicSong alloc] init];
        song.songId   = s[@"id"] ?: @"";
        song.title    = s[@"title"] ?: @"";
        song.artist   = s[@"artist"] ?: @"";
        song.album    = s[@"album"] ?: @"";
        song.albumId  = s[@"albumId"] ?: @"";
        song.track    = [s[@"track"] integerValue];
        song.year     = [s[@"year"] integerValue];
        song.duration = [s[@"duration"] doubleValue];
        song.coverArtId = s[@"coverArt"] ?: @"";
        song.suffix   = s[@"suffix"] ?: @"";
        [songs addObject:song];
    }

    return @{ @"artists": artists, @"albums": albums, @"songs": songs };
}

// ---------------------------------------------------------------------------
// URL builders
// ---------------------------------------------------------------------------

- (NSString *)streamURLForSongId:(NSString *)songId coverArtId:(NSString *)coverArtId {
    NSString *base = [NSString stringWithUTF8String:navidrome::cfg_server_url.get().c_str()];
    while ([base hasSuffix:@"/"]) base = [base substringToIndex:base.length - 1];
    NSString *auth = [self authParams];
    NSString *artParam = (coverArtId.length > 0)
        ? [NSString stringWithFormat:@"&coverArt=%@", urlEncode(coverArtId)]
        : @"";
    return [NSString stringWithFormat:@"%@/rest/stream.view?id=%@%@&%@",
            base, urlEncode(songId), artParam, auth];
}

- (NSURL *)coverArtURLForId:(NSString *)coverArtId size:(NSInteger)size {
    NSString *base = [NSString stringWithUTF8String:navidrome::cfg_server_url.get().c_str()];
    while ([base hasSuffix:@"/"]) base = [base substringToIndex:base.length - 1];
    NSString *auth = [self authParams];
    NSString *sizeParam = size > 0 ? [NSString stringWithFormat:@"&size=%ld", (long)size] : @"";
    NSString *full = [NSString stringWithFormat:@"%@/rest/getCoverArt.view?id=%@&%@%@",
                      base, urlEncode(coverArtId), auth, sizeParam];
    return [NSURL URLWithString:full];
}

@end
