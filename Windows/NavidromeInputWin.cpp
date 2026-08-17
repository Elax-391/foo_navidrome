#include "stdafx.h"
#include "NavidromeInputWin.h"
#include "SubsonicClientWin.h"
#include "MediaEnrichmentLogic.h"
#include "SongMetadataProjection.h"
#include "SongMetadata.h"
#include "TrackUriMetadata.h"
#include <SDK/input_impl.h>
#include <SDK/file.h>
#include <SDK/file_info_impl.h>
#include <SDK/http_client.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace navidrome {
extern cfg_string cfg_stream_format;
}

// ---------------------------------------------------------------------------
// Windows input_singletrack handler for the navidrome://track/<id>?... scheme,
// mirroring the macOS NavidromeInput.mm. On decode_initialize() it resolves the
// current HTTP stream URL from SubsonicClientWin and — when custom headers are
// configured (e.g. Cloudflare Access service tokens) — opens the stream itself
// via http_client with those headers, handing the resulting file::ptr to a
// nested decoder. Without custom headers it falls back to letting foobar open
// the URL directly (Content-Type sniffing), as before.
// ---------------------------------------------------------------------------

namespace {

class navidrome_input_win : public input_stubs {
public:
    void open(service_ptr_t<file> /*hint*/, const char* p_path,
              t_input_open_reason reason, abort_callback&) {
        if (reason == input_open_info_write) throw exception_tagging_unsupported();
        m_path = p_path;
        parse_uri(p_path);
        if (m_song_id.empty()) throw exception_io_data();
    }

    void get_info(file_info& info, abort_callback&) {
        navidrome::applySongMetadata(info, m_song);
    }

    t_filestats2 get_stats2(uint32_t, abort_callback&) {
        auto stats = filestats2_invalid;
        if (!navidrome::isTranscoded(m_song) && m_song.size && *m_song.size >= 0)
            stats.m_size = static_cast<t_filesize>(*m_song.size);
        return stats;
    }

    void decode_initialize(unsigned p_flags, abort_callback& p_abort) {
        std::string url = navidrome::SubsonicClientWin::get().streamURL(m_song_id);
        if (url.empty()) throw exception_io_data();
        m_resolved_url = url.c_str();

        // When custom headers are configured, open the stream ourselves so the
        // headers ride along; otherwise hand a null file and let foobar open the
        // URL (preserving the original Content-Type-based decoder selection).
        file::ptr httpFile;
        auto headers = navidrome::SubsonicClientWin::customHeaderLines();
        if (!headers.empty()) {
            http_request::ptr req = http_client::get()->create_request("GET");
            for (const auto& h : headers) req->add_header(h.c_str());
            httpFile = req->run(url.c_str(), p_abort);
        }

        // Our own file has no audio extension in the URL, so give the decoder a
        // suffix-based hint (track.<suffix>) to pick the codec; it still reads
        // bytes from httpFile, not from the hint path.
        const char* hint = m_resolved_url.c_str();
        pfc::string8 hintBuf;
        const std::string decoderSuffix = navidrome::effectiveStreamSuffix(
            navidrome::cfg_stream_format.get().c_str(),
            navidrome::effectiveCodec(m_song));
        if (httpFile.is_valid() && !decoderSuffix.empty()) {
            hintBuf << "track." << decoderSuffix.c_str();
            hint = hintBuf.c_str();
        }

        input_entry::g_open_for_decoding(m_decoder, httpFile, hint, p_abort, true);
        if (m_decoder.is_empty()) throw exception_io_data();
        m_decoder->initialize(0, p_flags, p_abort);
        m_decoderInfo.reset();
        try { m_decoder->get_info(0, m_decoderInfo, p_abort); }
        catch (...) { m_decoderInfo.reset(); }
        m_streamSpec.clear();
        m_streamInfoDirty = false;
    }

    bool decode_run(audio_chunk& chunk, abort_callback& abort) {
        if (!m_decoder.is_valid() || !m_decoder->run(chunk, abort)) return false;
        const auto spec = chunk.get_spec();
        if (spec.is_valid() && spec != m_streamSpec) {
            m_streamSpec = spec;
            m_decoderInfo.set_audio_chunk_spec(spec);
            m_streamInfoDirty = true;
        }
        return true;
    }
    void decode_seek(double s, abort_callback& abort) {
        if (m_decoder.is_valid()) m_decoder->seek(s, abort);
    }
    bool decode_can_seek() { return m_decoder.is_valid() && m_decoder->can_seek(); }
    bool decode_get_dynamic_info(file_info& out, double& delta) {
        if (!m_decoder.is_valid()) return false;
        const bool changed = m_decoder->get_dynamic_info(out, delta);
        if (!changed && !m_streamInfoDirty) return false;
        mergeDynamicInfo(out);
        m_streamInfoDirty = false;
        return true;
    }
    bool decode_get_dynamic_info_track(file_info& out, double& delta) {
        if (!m_decoder.is_valid() || !m_decoder->get_dynamic_info_track(out, delta)) return false;
        mergeDynamicInfo(out);
        return true;
    }
    void decode_on_idle(abort_callback& abort) {
        if (m_decoder.is_valid()) m_decoder->on_idle(abort);
    }

    void retag(const file_info&, abort_callback&) { throw exception_tagging_unsupported(); }
    void remove_tags(abort_callback&)             { throw exception_tagging_unsupported(); }

    static bool g_is_our_content_type(const char*) { return false; }
    static bool g_is_our_path(const char* p_path, const char*) {
        return p_path != nullptr &&
            strncmp(p_path, navidrome::kTrackUriPrefix,
                    navidrome::kTrackUriPrefixLength) == 0;
    }
    static GUID g_get_guid() {
        static constexpr GUID guid = { 0xa1b2c3d4, 0x1111, 0x2222,
            { 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x02, 0x01 } };
        return guid;
    }
    static const char* g_get_name() { return "Navidrome"; }

private:
    void parse_uri(const char* uri) {
        if (navidrome::parseTrackURI(uri, m_song)) m_song_id = m_song.id;
    }

    void mergeDynamicInfo(file_info& out) {
        file_info_impl merged;
        navidrome::applySongMetadata(merged, m_song);
        navidrome::overlayTechnicalInfo(merged, m_decoderInfo);
        navidrome::overlayTechnicalInfo(merged, out);
        navidrome::fillReplayGain(merged, m_decoderInfo);
        navidrome::fillReplayGain(merged, out);
        out.copy(merged);
    }

    std::string m_path, m_song_id;
    navidrome::Song m_song;
    pfc::string8 m_resolved_url;
    file_info_impl m_decoderInfo;
    audio_chunk::spec_t m_streamSpec;
    bool m_streamInfoDirty = false;
    service_ptr_t<input_decoder> m_decoder;
};

static input_singletrack_factory_t<navidrome_input_win, input_entry::flag_redirect>
    g_navidrome_input_win_factory;

} // namespace

// ---------------------------------------------------------------------------
// URI builder (public)
// ---------------------------------------------------------------------------
std::string navidrome::makeTrackURI(const std::string& id,
                                    const std::string& title,
                                    const std::string& artist,
                                    const std::string& album,
                                    int track,
                                    int year,
                                    double duration,
                                    const std::string& coverArtId,
                                    const std::string& suffix) {
    Song song;
    song.id = id; song.title = title; song.artist = artist; song.album = album;
    song.track = track; song.year = year; song.duration = duration;
    song.coverArtId = coverArtId; song.suffix = suffix;
    return buildTrackURI(song);
}

std::string navidrome::makeTrackURI(const navidrome::Song& song) {
    return buildTrackURI(song);
}
