#import "stdafx.h"
#import "SubsonicClient.h"
#include <SDK/album_art.h>

// ---------------------------------------------------------------------------
// Helper: extract a query-param value from a URL string
// ---------------------------------------------------------------------------
static pfc::string8 urlParamValue(const char* url, const char* key) {
    pfc::string8 token = key;
    token += "=";
    const char* pos = strstr(url, token.c_str());
    if (!pos) return "";
    pos += token.length();
    const char* end = strchr(pos, '&');
    if (!end) end = pos + strlen(pos);
    return pfc::string8(pos, (t_size)(end - pos));
}

// ---------------------------------------------------------------------------
// album_art_extractor_instance — fetches cover art for one track from Navidrome
// ---------------------------------------------------------------------------
class navidrome_art_instance : public album_art_extractor_instance {
public:
    navidrome_art_instance(const char* artId) : m_artId(artId) {}

    album_art_data_ptr query(const GUID& p_what, abort_callback& /*p_abort*/) override {
        if (p_what != album_art_ids::cover_front)
            throw exception_album_art_not_found();

        NSString *idStr = [NSString stringWithUTF8String:m_artId.c_str()];
        NSURL *url = [SubsonicClient.sharedClient coverArtURLForId:idStr size:0];
        if (!url) throw exception_album_art_not_found();

        NSError *err = nil;
        NSData *data = [NSData dataWithContentsOfURL:url options:0 error:&err];
        if (!data || data.length == 0)
            throw exception_album_art_not_found();

        return album_art_data_impl::g_create(data.bytes, (t_size)data.length);
    }

private:
    pfc::string8 m_artId;
};

// ---------------------------------------------------------------------------
// album_art_extractor — foobar2000 calls is_our_path() for every track it
// needs art for. Returning true from is_our_path() guarantees open() is
// called, which is more reliable than album_art_fallback for HTTP streams.
// ---------------------------------------------------------------------------
class navidrome_art_extractor : public album_art_extractor {
public:
    bool is_our_path(const char* p_path, const char* /*p_ext*/) override {
        return strstr(p_path, "/rest/stream.view") != nullptr;
    }

    album_art_extractor_instance_ptr open(file_ptr /*p_file*/,
                                          const char* p_path,
                                          abort_callback& /*p_abort*/) override {
        // Prefer the coverArt param (album / Folder.jpg ID embedded at enqueue time).
        // Fall back to the song id if coverArt is absent.
        pfc::string8 artId = urlParamValue(p_path, "coverArt");
        if (artId.length() == 0)
            artId = urlParamValue(p_path, "id");
        if (artId.length() == 0)
            throw exception_album_art_not_found();
        return new service_impl_t<navidrome_art_instance>(artId.c_str());
    }
};

FB2K_SERVICE_FACTORY(navidrome_art_extractor);
