#pragma once

#include "../SubsonicTypes.h"
#include "SongMetadata.h"
#include <SDK/file_info.h>

namespace navidrome {

inline void setMeta(file_info& info, const char* name, const std::optional<std::string>& value) {
    if (value && !value->empty()) info.meta_set(name, value->c_str());
}

inline void setMetaList(file_info& info, const char* name,
                        const std::vector<std::string>& values) {
    if (values.empty()) return;
    const auto index = info.meta_set(name, values.front().c_str());
    for (size_t i = 1; i < values.size(); ++i) info.meta_add_value(index, values[i].c_str());
}

// file_info::copy_info() replaces the entire technical-info collection. Use a
// field overlay when combining server estimates, decoder facts and live values.
inline void overlayTechnicalInfo(file_info& target, const file_info& source) {
    const auto count = source.info_get_count();
    for (size_t i = 0; i < count; ++i)
        target.info_set(source.info_enum_name(i), source.info_enum_value(i));
}

inline void applyReplayGain(file_info& info, const Song::ReplayGain& source) {
    replaygain_info gain = info.get_replaygain();
    if (source.trackGain) gain.m_track_gain = static_cast<float>(*source.trackGain);
    if (source.albumGain) gain.m_album_gain = static_cast<float>(*source.albumGain);
    if (source.trackPeak) gain.m_track_peak = static_cast<float>(*source.trackPeak);
    if (source.albumPeak) gain.m_album_peak = static_cast<float>(*source.albumPeak);
    // foobar2000 v2 exposes only track/album gain and peak in replaygain_info.
    // OpenSubsonic baseGain/fallbackGain remain in Song and the URI so they are
    // not lost, but this SDK has no dedicated slots for them.
    if (source.trackGain || source.albumGain || source.trackPeak || source.albumPeak)
        info.set_replaygain(gain);
}

inline void overlayReplayGain(file_info& target, const file_info& source) {
    auto gain = target.get_replaygain();
    const auto sourceGain = source.get_replaygain();
    if (sourceGain.is_track_gain_present()) gain.m_track_gain = sourceGain.m_track_gain;
    if (sourceGain.is_album_gain_present()) gain.m_album_gain = sourceGain.m_album_gain;
    if (sourceGain.is_track_peak_present()) gain.m_track_peak = sourceGain.m_track_peak;
    if (sourceGain.is_album_peak_present()) gain.m_album_peak = sourceGain.m_album_peak;
    if (sourceGain.is_track_gain_present() || sourceGain.is_album_gain_present() ||
        sourceGain.is_track_peak_present() || sourceGain.is_album_peak_present())
        target.set_replaygain(gain);
}

inline void fillReplayGain(file_info& target, const file_info& source) {
    auto gain = target.get_replaygain();
    const auto sourceGain = source.get_replaygain();
    bool changed = false;
    if (!gain.is_track_gain_present() && sourceGain.is_track_gain_present()) {
        gain.m_track_gain = sourceGain.m_track_gain;
        changed = true;
    }
    if (!gain.is_album_gain_present() && sourceGain.is_album_gain_present()) {
        gain.m_album_gain = sourceGain.m_album_gain;
        changed = true;
    }
    if (!gain.is_track_peak_present() && sourceGain.is_track_peak_present()) {
        gain.m_track_peak = sourceGain.m_track_peak;
        changed = true;
    }
    if (!gain.is_album_peak_present() && sourceGain.is_album_peak_present()) {
        gain.m_album_peak = sourceGain.m_album_peak;
        changed = true;
    }
    if (changed) target.set_replaygain(gain);
}

inline void applySongMetadata(file_info& info, const Song& song, bool technical = true) {
    if (!song.title.empty()) info.meta_set("title", song.title.c_str());
    if (!song.artist.empty()) info.meta_set("artist", song.artist.c_str());
    if (!song.album.empty()) info.meta_set("album", song.album.c_str());
    if (song.track > 0) info.meta_set("tracknumber", pfc::format_int(song.track));
    if (song.year > 0) info.meta_set("date", pfc::format_int(song.year));
    setMeta(info, "album artist", song.albumArtist);
    setMeta(info, "display artist", song.displayArtist);
    setMeta(info, "titlesort", song.sortName);
    setMeta(info, "composer", song.composer);
    setMeta(info, "display composer", song.displayComposer);
    setMeta(info, "comment", song.comment);
    setMeta(info, "isrc", song.isrc);
    setMeta(info, "musicbrainz_trackid", song.musicBrainzId);
    setMeta(info, "musicbrainz_artistid", song.musicBrainzArtistId);
    setMeta(info, "musicbrainz_albumid", song.musicBrainzAlbumId);
    setMeta(info, "musicbrainz_release_artistid", song.musicBrainzReleaseArtistId);
    setMeta(info, "explicit", song.explicitStatus);
    if (song.discNumber && *song.discNumber > 0)
        info.meta_set("discnumber", pfc::format_int(*song.discNumber));
    if (song.bpm && *song.bpm >= 0.0) info.meta_set("bpm", std::to_string(*song.bpm).c_str());
    setMetaList(info, "genre", song.genres);
    setMetaList(info, "grouping", song.groupings.empty()
        ? (song.grouping ? std::vector<std::string>{*song.grouping} : std::vector<std::string>{})
        : song.groupings);
    setMetaList(info, "mood", song.moods);
    if (song.duration > 0) info.set_length(song.duration);
    applyReplayGain(info, song.replayGain);
    if (!technical) return;
    const std::string codec = effectiveCodec(song);
    const auto contentType = effectiveContentType(song);
    if (!codec.empty()) info.info_set("codec", codec.c_str());
    if (contentType && !contentType->empty())
        info.info_set("content type", contentType->c_str());
    if (isTranscoded(song)) return;
    if (song.bitRate && *song.bitRate > 0) info.info_set_bitrate(*song.bitRate);
    if (song.samplingRate && *song.samplingRate > 0)
        info.info_set_int("samplerate", *song.samplingRate);
    if (song.channelCount && *song.channelCount > 0) info.info_set_channels(*song.channelCount);
    if (song.bitDepth && *song.bitDepth > 0) info.info_set_bitspersample(*song.bitDepth);
}

} // namespace navidrome
