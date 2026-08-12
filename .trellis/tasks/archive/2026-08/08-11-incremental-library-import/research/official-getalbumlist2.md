# getAlbumList2

Returns a list of random, newest, highest rated etc. albums.

##### Categories:

* [Lists](/categories/lists/)

`http://your-server/rest/getAlbumList2` Since [1.8.0](../../subsonic-versions)

Similar to [`getAlbumList`](../getalbumlist), but organizes music according to ID3 tags.

### Parameters

| Parameter | Req. | OpenS. | Default | Comment |
| --- | --- | --- | --- | --- |
| `type` | **Yes** | The list type. Must be one of the following: `random`, `newest`, `highest`, `frequent`, `recent`. Since [1.8.0](../../subsonic-versions) you can also use `alphabeticalByName` or `alphabeticalByArtist` to page through all albums alphabetically, and `starred` to retrieve starred albums. Since [1.10.1](../../subsonic-versions) you can use `byYear` and `byGenre` to list albums in a given year range or genre. |
| `size` | No | 10 | The number of albums to return. Max 500. |
| `offset` | No | 0 | The list offset. Useful if you for example want to page through the list of newest albums. |
| `fromYear` | **Yes** (if `type` is `byYear`) | The first year in the range. If `fromYear > toYear` a reverse chronological list is returned. |
| `toYear` | **Yes** (if `type` is `byYear`) | The last year in the range. |
| `genre` | **Yes** (if `type` is `byGenre`) | The name of the genre, e.g., “Rock”. |
| `musicFolderId` | No | (Since [1.11.0](../../subsonic-versions)) Only return albums in the music folder with the given ID. See `getMusicFolders`. |

### Example

`http://your-server/rest/getAlbumList2.view?type=random&u=demo&p=demo&v=1.13.0&c=AwesomeClientName&f=json`

### Result

A [`subsonic-response`](../../responses/subsonic-response) element with a nested [`albumList2`](../../responses/albumlist2) element on success.



```
{ {  "subsonic-response": {  "subsonic-response": { "subsonic-response" "status": "ok",  "status": "ok", "status" "ok" "version": "1.16.1",  "version": "1.16.1", "version""1.16.1" "type": "AwesomeServerName",  "type": "AwesomeServerName", "type" "AwesomeServerName" "serverVersion": "0.1.3 (tag)",  "serverVersion": "0.1.3 (tag)", "serverVersion""0.1.3 (tag)" "openSubsonic": true,  "openSubsonic": true, "openSubsonic" true "albumList2": {  "albumList2": { "albumList2" "album": [  "album": [ "album" {  {  "id": "200000021",  "id": "200000021", "id" "200000021" "album": "Forget and Remember",  "album": "Forget and Remember", "album" "Forget and Remember" "title": "Forget and Remember",  "title": "Forget and Remember", "title" "Forget and Remember" "name": "Forget and Remember",  "name": "Forget and Remember", "name" "Forget and Remember" "coverArt": "al-200000021",  "coverArt": "al-200000021", "coverArt""al-200000021" "songCount": 20,  "songCount": 20, "songCount" 20 "created": "2021-07-22T02:09:31+00:00",  "created": "2021-07-22T02:09:31+00:00", "created""2021-07-22T02:09:31+00:00" "duration": 4248,  "duration": 4248, "duration" 4248 "playCount": 0,  "playCount": 0, "playCount" 0 "artistId": "100000036",  "artistId": "100000036", "artistId" "100000036" "artist": "Comfort Fit",  "artist": "Comfort Fit", "artist" "Comfort Fit" "year": 2005,  "year": 2005, "year" 2005 "genre": "Hip-Hop"  "genre": "Hip-Hop" "genre""Hip-Hop" },  },  {  {  "id": "200000012",  "id": "200000012", "id" "200000012" "album": "Buried in Nausea",  "album": "Buried in Nausea", "album" "Buried in Nausea" "title": "Buried in Nausea",  "title": "Buried in Nausea", "title" "Buried in Nausea" "name": "Buried in Nausea",  "name": "Buried in Nausea", "name" "Buried in Nausea" "coverArt": "al-200000012",  "coverArt": "al-200000012", "coverArt""al-200000012" "songCount": 9,  "songCount": 9, "songCount" 9 "created": "2021-02-24T01:44:21+00:00",  "created": "2021-02-24T01:44:21+00:00", "created""2021-02-24T01:44:21+00:00" "duration": 1879,  "duration": 1879, "duration" 1879 "playCount": 0,  "playCount": 0, "playCount" 0 "artistId": "100000019",  "artistId": "100000019", "artistId" "100000019" "artist": "Various Artists",  "artist": "Various Artists", "artist" "Various Artists" "year": 2012,  "year": 2012, "year" 2012 "genre": "Punk"  "genre": "Punk" "genre" "Punk" }  }  ]  ]  }  }  }  } }}
```

```
{ {  "subsonic-response": {  "subsonic-response": { "subsonic-response" "status": "ok",  "status": "ok", "status" "ok" "version": "1.16.1",  "version": "1.16.1", "version""1.16.1" "albumList2": {  "albumList2": { "albumList2" "album": [  "album": [ "album" {  {  "id": "200000021",  "id": "200000021", "id" "200000021" "album": "Forget and Remember",  "album": "Forget and Remember", "album" "Forget and Remember" "title": "Forget and Remember",  "title": "Forget and Remember", "title" "Forget and Remember" "name": "Forget and Remember",  "name": "Forget and Remember", "name" "Forget and Remember" "coverArt": "al-200000021",  "coverArt": "al-200000021", "coverArt""al-200000021" "songCount": 20,  "songCount": 20, "songCount" 20 "created": "2021-07-22T02:09:31+00:00",  "created": "2021-07-22T02:09:31+00:00", "created""2021-07-22T02:09:31+00:00" "duration": 4248,  "duration": 4248, "duration" 4248 "playCount": 0,  "playCount": 0, "playCount" 0 "artistId": "100000036",  "artistId": "100000036", "artistId" "100000036" "artist": "Comfort Fit",  "artist": "Comfort Fit", "artist" "Comfort Fit" "year": 2005,  "year": 2005, "year" 2005 "genre": "Hip-Hop"  "genre": "Hip-Hop" "genre""Hip-Hop" },  },  {  {  "id": "200000012",  "id": "200000012", "id" "200000012" "album": "Buried in Nausea",  "album": "Buried in Nausea", "album" "Buried in Nausea" "title": "Buried in Nausea",  "title": "Buried in Nausea", "title" "Buried in Nausea" "name": "Buried in Nausea",  "name": "Buried in Nausea", "name" "Buried in Nausea" "coverArt": "al-200000012",  "coverArt": "al-200000012", "coverArt""al-200000012" "songCount": 9,  "songCount": 9, "songCount" 9 "created": "2021-02-24T01:44:21+00:00",  "created": "2021-02-24T01:44:21+00:00", "created""2021-02-24T01:44:21+00:00" "duration": 1879,  "duration": 1879, "duration" 1879 "playCount": 0,  "playCount": 0, "playCount" 0 "artistId": "100000019",  "artistId": "100000019", "artistId" "100000019" "artist": "Various Artists",  "artist": "Various Artists", "artist" "Various Artists" "year": 2012,  "year": 2012, "year" 2012 "genre": "Punk"  "genre": "Punk" "genre" "Punk" }  }  ]  ]  }  }  }  } }}
```

| Field | Type | Req. | OpenS. | Details |
| --- | --- | --- | --- | --- |
| `albumList2` | [`albumList2`](../../responses/albumlist2) | **Yes** | The album list |

Last modified May 20, 2025: [Update to latest Docsy version (#147) (0a50bcd)](https://github.com/opensubsonic/open-subsonic-api/commit/0a50bcd18f34a35079d9253fb7ae6b9a9f79efad)
