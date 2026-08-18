#include "SongMetadata.h"
#include "SongJsonParser.h"
#include "SongMetadataProjection.h"
#include "TrackUriMetadata.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
int failures = 0;

void check(bool condition, const char* description) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << description << '\n';
}
}

int main() {
    std::cout << "SongMetadataTests starting\n";
    const auto parsed = navidrome::parseSongJson(R"json({
        "id":"song-1","unknown":{"nested":[1,{"value":"ignored"}]},
        "parent":"parent-1","albumId":"album-1",
        "artistId":"artist-1","title":"\u4e2d\u6587\u6b4c\u66f2",
        "artist":"Artist","album":"Album","albumArtist":"Album Artist",
        "displayArtist":"Artist feat. Guest","sortName":"Song, The",
        "composer":"Composer","displayComposer":"Composer Display",
        "path":"Artist/Album/01.flac","created":"2026-08-13T00:00:00Z",
        "starred":"2026-08-13T01:00:00Z","played":"2026-08-13T02:00:00Z",
        "playCount":4,"track":1,"discNumber":2,"year":2026,"bpm":123.5,
        "genre":"Fallback Genre","genres":[{"name":"Rock"},{"name":"\u534e\u8bed"}],
        "groupings":["Suite","Part I"],"moods":["Calm"],
        "comment":"A & B = C","isrc":"TEST12345678",
        "musicBrainzId":"track-mbid","musicBrainzArtistId":"artist-mbid",
        "musicBrainzAlbumId":"album-mbid",
        "musicBrainzReleaseArtistId":"release-artist-mbid",
        "explicitStatus":"clean","coverArt":"cover-1","suffix":"flac",
        "contentType":"audio/flac","transcodedSuffix":"opus",
        "transcodedContentType":"audio/ogg","size":123456789,"duration":321.5,
        "bitRate":1000,"bitDepth":24,"samplingRate":96000,"channelCount":2,
        "replayGain":{"trackGain":-7.25,"albumGain":-6.5,
            "trackPeak":0.98,"albumPeak":0.99,"baseGain":-18,"fallbackGain":-9}
    })json");
    check(parsed.id == "song-1" && parsed.title == u8"中文歌曲",
          "OpenSubsonic Child identity and escaped UTF-8 parse");
    check(parsed.parent == "parent-1" && parsed.albumId == "album-1" &&
          parsed.artistId == "artist-1" && parsed.created == "2026-08-13T00:00:00Z",
          "catalog fields parse");
    check(parsed.path == "Artist/Album/01.flac" && parsed.starred && parsed.played &&
          parsed.playCount == 4 && parsed.track == 1 && parsed.discNumber == 2 &&
          parsed.year == 2026 && parsed.bpm == 123.5,
          "optional catalog and index fields parse");
    check(parsed.albumArtist == "Album Artist" &&
          parsed.displayArtist == "Artist feat. Guest" &&
          parsed.sortName == "Song, The" && parsed.composer == "Composer" &&
          parsed.displayComposer == "Composer Display" &&
          parsed.comment == "A & B = C" && parsed.isrc == "TEST12345678",
          "extended text tags parse");
    check(parsed.musicBrainzId == "track-mbid" &&
          parsed.musicBrainzArtistId == "artist-mbid" &&
          parsed.musicBrainzAlbumId == "album-mbid" &&
          parsed.musicBrainzReleaseArtistId == "release-artist-mbid" &&
          parsed.explicitStatus == "clean",
          "external identifiers parse");
    check(parsed.genres.size() == 2 && parsed.genres[1] == u8"华语" &&
          parsed.groupings.size() == 2 && parsed.moods.size() == 1,
          "metadata lists parse");
    check(parsed.size == 123456789 && parsed.samplingRate == 96000 &&
          parsed.bitRate == 1000 && parsed.bitDepth == 24 && parsed.channelCount == 2 &&
          parsed.duration == 321.5 && parsed.suffix == "flac" &&
          parsed.contentType == "audio/flac" && parsed.transcodedSuffix == "opus" &&
          parsed.transcodedContentType == "audio/ogg" && parsed.coverArt == "cover-1",
          "media facts parse");
    check(parsed.replayGain.trackGain == -7.25 && parsed.replayGain.albumGain == -6.5 &&
          parsed.replayGain.trackPeak == 0.98 && parsed.replayGain.albumPeak == 0.99 &&
          parsed.replayGain.baseGain == -18.0 && parsed.replayGain.fallbackGain == -9.0,
          "all ReplayGain fields parse");

    navidrome::Song song;
    song.genres = {"Rock", "", "Rock", u8"华语"};
    song.groupings = {"Suite", "Suite", "Part I"};
    song.moods = {"Calm", ""};
    song.replayGain.trackGain = -7.25;
    song.replayGain.albumPeak = 0.98;
    navidrome::normalizeSongLists(song);

    check(song.genres.size() == 2 && song.genres[1] == u8"华语",
          "metadata lists preserve order and remove duplicates");
    check(song.groupings.size() == 2, "groupings are normalized");
    check(song.moods.size() == 1, "empty moods are removed");
    check(song.replayGain.trackGain && *song.replayGain.trackGain == -7.25,
          "valid track gain is retained");
    check(song.replayGain.albumPeak && *song.replayGain.albumPeak == 0.98,
          "positive peak is retained");

    navidrome::Song invalid;
    invalid.replayGain.trackGain = std::numeric_limits<double>::quiet_NaN();
    invalid.replayGain.trackPeak = 0.0;
    navidrome::normalizeSongLists(invalid);
    check(invalid.replayGain.empty(), "invalid ReplayGain remains absent");
    check(!navidrome::validPeak(0.0), "zero peak is rejected");
    check(navidrome::validPeak(1.0), "positive peak is accepted");
    navidrome::Song partiallyValid;
    partiallyValid.replayGain.trackGain = -7.25;
    partiallyValid.replayGain.trackPeak = 0.0;
    navidrome::normalizeSongLists(partiallyValid);
    check(partiallyValid.replayGain.trackGain == -7.25 &&
          !partiallyValid.replayGain.trackPeak,
          "invalid ReplayGain field does not erase valid siblings");

    navidrome::Song source;
    source.id = u8"曲目/id&=";
    source.title = u8"标题 & = ?";
    source.artist = u8"歌手";
    source.album = u8"专辑";
    source.artistId = "artist/id";
    source.albumId = "album&id";
    source.created = "2026-08-13T00:00:00Z";
    source.suffix = "flac";
    source.contentType = "audio/flac";
    source.genres = {u8"华语", "Rock & Roll"};
    source.grouping = "Single grouping";
    source.discNumber = 2;
    source.samplingRate = 96000;
    source.bitDepth = 24;
    source.channelCount = 2;
    source.userRating = 4;
    source.replayGain.trackGain = -6.5;
    source.replayGain.trackPeak = 0.95;
    const auto uri = navidrome::buildTrackURI(source);
    navidrome::Song roundTrip;
    check(navidrome::parseTrackURI(uri, roundTrip), "generated URI parses");
    check(roundTrip.id == source.id && roundTrip.title == source.title,
          "UTF-8 and URI separators round trip");
    check(roundTrip.genres == source.genres, "multi-value genres round trip");
    check(roundTrip.artistId == source.artistId && roundTrip.albumId == source.albumId &&
          roundTrip.created == source.created && roundTrip.groupings.size() == 1 &&
          roundTrip.groupings[0] == *source.grouping,
          "catalog and single grouping fields round trip");
    check(roundTrip.samplingRate == source.samplingRate &&
          roundTrip.bitDepth == source.bitDepth && roundTrip.channelCount == source.channelCount &&
          roundTrip.userRating == source.userRating,
          "technical values round trip");
    check(roundTrip.replayGain.trackGain == source.replayGain.trackGain &&
          roundTrip.replayGain.trackPeak == source.replayGain.trackPeak,
          "ReplayGain round trips");

    navidrome::Song legacy;
    check(navidrome::parseTrackURI("navidrome://track/old-id", legacy) &&
          legacy.id == "old-id", "old URI without query remains valid");
    navidrome::Song malformed;
    check(navidrome::parseTrackURI(
        "navidrome://track/id?samplerate=oops&rgTrackPeak=0&bitdepth=24", malformed),
        "URI with malformed optional values still parses");
    check(!malformed.samplingRate && !malformed.replayGain.trackPeak &&
          malformed.bitDepth == 24, "malformed optionals remain absent");
    navidrome::Song invalidNumbers;
    check(navidrome::parseTrackURI(
        "navidrome://track/id?bitrate=-1&channels=-2&discnumber=0&playCount=-1&userRating=6&"
        "samplerate=999999999999999999999&duration=0", invalidNumbers),
        "URI with invalid numeric ranges still parses");
    check(!invalidNumbers.bitRate && !invalidNumbers.channelCount &&
           !invalidNumbers.discNumber && !invalidNumbers.playCount &&
           !invalidNumbers.userRating && !invalidNumbers.samplingRate &&
           invalidNumbers.duration == 0,
          "invalid URI numeric fields remain absent");
    navidrome::Song transcoded;
    transcoded.suffix = "flac";
    transcoded.contentType = "audio/flac";
    transcoded.transcodedSuffix = "opus";
    transcoded.transcodedContentType = "audio/ogg";
    check(navidrome::isTranscoded(transcoded) &&
          navidrome::effectiveCodec(transcoded) == "opus" &&
          navidrome::effectiveContentType(transcoded) == "audio/ogg",
          "transcoded stream identity overrides source format");
    const auto malformedJson = navidrome::parseSongJson(
        R"json({"id":"playable","title":"Playable","samplingRate":"bad",
        "bitDepth":-1,"unknown":true,"replayGain":{"trackPeak":0}})json");
    check(malformedJson.id == "playable" && !malformedJson.samplingRate &&
          !malformedJson.bitDepth && !malformedJson.replayGain.trackPeak,
          "malformed JSON optionals do not reject a playable song");
    const auto escapedArray = navidrome::parseSongArrayJson(R"json({
        "subsonic-response":{"searchResult3":{"song":[
            {"id":"slash","title":"Slash","path":"folder\\"},
            {"id":"next","title":"Next"}
        ]}}})json");
    check(escapedArray.size() == 2 && escapedArray[0].id == "slash" &&
          escapedArray[0].path == "folder\\" && escapedArray[1].id == "next",
          "song arrays handle strings ending in an escaped backslash");
    const auto overflowingSize = navidrome::parseSongJson(
        R"json({"id":"large","title":"Large","size":999999999999999999999999})json");
    check(!overflowingSize.size, "overflowing JSON size remains absent");

    file_info_impl projected;
    navidrome::Song projectionSong;
    projectionSong.title = "Projected title";
    projectionSong.artist = "Projected artist";
    projectionSong.album = "Projected album";
    projectionSong.genres = {"Rock", "Live"};
    projectionSong.duration = 123.0;
    projectionSong.suffix = "flac";
    projectionSong.contentType = "audio/flac";
    projectionSong.bitRate = 1411;
    projectionSong.samplingRate = 44100;
    projectionSong.channelCount = 2;
    projectionSong.bitDepth = 16;
    projectionSong.replayGain.trackGain = -7.0;
    projectionSong.replayGain.albumPeak = 0.98;
    projectionSong.replayGain.baseGain = -18.0;
    projectionSong.replayGain.fallbackGain = -9.0;
    navidrome::applySongMetadata(projected, projectionSong);
    check(std::string(projected.meta_get("title", 0)) == "Projected title" &&
          projected.meta_get_count_by_name("genre") == 2 &&
          projected.info_get_bitrate() == 1411 &&
          projected.info_get_int("samplerate") == 44100 &&
          projected.info_get_int("channels") == 2 &&
          projected.info_get_int("bitspersample") == 16,
          "file_info projection preserves tags and technical metadata");
    const auto projectedGain = projected.get_replaygain();
    check(projectedGain.is_track_gain_present() &&
          projectedGain.m_track_gain == -7.0f &&
          projectedGain.is_album_peak_present() &&
          projectedGain.m_album_peak == 0.98f,
          "file_info projection maps supported ReplayGain fields");
    file_info_impl decoderGain;
    decoderGain.info_set_replaygain_track_gain(-2.0f);
    decoderGain.info_set_replaygain_album_gain(-3.0f);
    navidrome::overlayReplayGain(projected, decoderGain);
    check(projected.get_replaygain().m_track_gain == -2.0f &&
          projected.get_replaygain().m_album_gain == -3.0f,
          "decoder ReplayGain fields overlay independently");
    file_info_impl serverPreferred;
    navidrome::applyReplayGain(serverPreferred, projectionSong.replayGain);
    navidrome::fillReplayGain(serverPreferred, decoderGain);
    check(serverPreferred.get_replaygain().m_track_gain == -7.0f &&
          serverPreferred.get_replaygain().m_album_gain == -3.0f,
          "server ReplayGain fields remain preferred while decoder fills gaps");
    file_info_impl unsupportedOnly;
    navidrome::Song::ReplayGain unsupportedGain;
    unsupportedGain.baseGain = -18.0;
    unsupportedGain.fallbackGain = -9.0;
    navidrome::applyReplayGain(unsupportedOnly, unsupportedGain);
    navidrome::fillReplayGain(unsupportedOnly, decoderGain);
    check(unsupportedOnly.get_replaygain().m_track_gain == -2.0f,
          "unsupported server ReplayGain extensions do not block decoder values");
    navidrome::Song transcodeProjection = projectionSong;
    transcodeProjection.transcodedSuffix = "opus";
    transcodeProjection.transcodedContentType = "audio/ogg";
    file_info_impl transcodedInfo;
    navidrome::applySongMetadata(transcodedInfo, transcodeProjection);
    check(transcodedInfo.info_get("codec") != nullptr &&
          std::string(transcodedInfo.info_get("codec")) == "opus" &&
          transcodedInfo.info_get_bitrate() == 0 &&
          transcodedInfo.info_get_int("samplerate") == 0,
          "transcoded projection suppresses source technical estimates");

    if (failures != 0) return 1;
    std::cout << "All SongMetadata tests passed\n";
    return 0;
}
