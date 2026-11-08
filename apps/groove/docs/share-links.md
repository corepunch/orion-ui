# Share links (design)

Status: the player page is built (`docs/groove/play/`) and plays a local
fixture. The upload service and the app command are not implemented.

File > Share Link uploads the current song and returns a short web link. The
link opens a page that plays the song, shows its arrangement, offers
**Download Groove** and **Open in Groove** (open the project itself to
remix it). It does not replace Export Audio and Share Audio. A loose audio
file plays well, but it has no link back to Groove or to the project.

## Pieces

| Piece | Where | Job |
|---|---|---|
| Share Link command | `groove.orion`, `share.c` | render, upload, show link in share sheet |
| Upload service | Cloudflare Worker + R2 bucket | issue IDs, accept files, delete on request |
| Player page | GitHub Pages, `docs/groove/play/` | static HTML/JS that plays a stored song |
| Deep link | `groove://` URL scheme | open a shared project in the app |

GitHub Pages serves only static files, so it hosts the player page but
cannot accept uploads. The app also cannot hold a GitHub token. Song files
therefore go into object storage behind a small upload endpoint. The page
loads them from there in the browser.

The page is published by the existing `docs.yml` workflow, which builds
`docs/` with Jekyll on version tags. It is served at
`https://corepunch.github.io/orion-ui/groove/play/`. The page changes rarely;
a song upload never triggers a Pages build.

## Link format

```
https://corepunch.github.io/orion-ui/groove/play/#k3J9xQm2Ab
```

The song ID is in the fragment. This means one static `index.html` serves
every song, and the ID is never sent to GitHub. IDs are 10 random base62
characters (about 59 bits), issued by the service, never sequential.

## What is uploaded

| File | Content | Typical size |
|---|---|---|
| `song.groove` | the project as `song_write_copy()` writes it, embedded recordings included | 2–5 KB stock-only, MB with recordings |
| `audio.m4a` | the Share Audio render (AAC) | ~4 MB for 3 minutes |
| `cover.png` | `song_draw_overview()` at 1200×630 | ~100 KB |
| `meta.json` | title, BPM, bars, duration, versions, cover geometry (below) | < 1 KB |

`meta.json`, as the page reads it:

```json
{
  "id": "k3J9xQm2Ab", "title": "Neon Skyline",
  "bpm": 140, "bars": 88, "duration": 150.857,
  "app_version": "1.0.4", "format_version": 4, "created": "2026-11-08T21:20:00Z",
  "cover": { "width": 1200, "height": 630, "timeline": { "x": 64, "y": 92, "w": 1104, "h": 506 } },
  "files": { "song": "song.groove", "audio": "audio.m4a", "cover": "cover.png" }
}
```

`cover.timeline` is the playhead travel rectangle that `song_export_canvas()`
returns (`ui_media_video_t` x/y/w/h), in cover pixels. The page draws its
playhead in that rectangle, so it never hard-codes the app's canvas layout.
The page rejects a `meta.json` without positive `bpm`, `bars`, `duration`
and cover sizes, and shows every text value with `textContent` only.

The app renders everything locally with the existing export code. The
service never runs Groove code, so it needs no audio stack. The page plays
`audio.m4a`, which sounds exactly like the app on every browser, whatever
the song's format version is.

## Upload service

Base URL `https://share.groove.<domain>/v1` (to be decided). The upload
service is the only part of the system that runs code on a server.

| Request | Effect |
|---|---|
| `POST /songs` with `meta.json` | returns `{id, delete_token, upload: {file: signed PUT URL}}` |
| `PUT <signed URL>` | stores one file in R2 (expires after 15 minutes) |
| `POST /songs/{id}/commit` | checks that all files exist and sizes match `meta.json`, makes the song public |
| `DELETE /songs/{id}` with `Authorization: Bearer <delete_token>` | removes the song's files |
| `POST /songs/{id}/report` | records an abuse report |

R2 layout: `songs/{id}/{song.groove,audio.m4a,cover.png,meta.json}`. The
bucket is public-read for committed songs, with CORS limited to the Pages
origin. Uncommitted uploads are removed after 24 hours.

Limits, enforced by the Worker: 50 MB per song, 10 minutes of audio,
20 uploads per IP per hour. The service stores only the delete token's
hash. No accounts are needed for the first version.

## App side

- `groove.orion`: add `file.share_link` ("Share Link...") to the File menu,
  next to Share Audio, and to the Share button's choices. Its labels are
  translated by the catalogs; regenerate them with `orionc --strings`.
- `share.c`: a worker thread, like the export dialog's, renders the M4A and
  cover. It uploads, commits, then reports progress and supports
  cancellation. The dialog joins the worker before freeing resources.
  Failure messages go through `ui_tr()`.
- HTTP goes through a small platform API in the kernel, not app code:
  `NSURLSession` on Apple platforms, libcurl on Linux and Windows. Groove has
  no networking today, so this is new framework support.
- The link is copied to the clipboard and offered in the native share
  sheet. Share Audio's sheet only takes files today, so it needs a variant
  for URLs.
- Every shared song is saved in `axSettingsDirectory()/Shares.tsv`: ID,
  title, date and delete token. A **Shared Songs** list lets the user copy a
  link again or delete it. A deleted song stops playing on the page.
- Sharing the same unchanged song again returns the existing link (the
  project's hash is stored next to the ID).

## Player page

`docs/groove/play/index.html`: one file of HTML, CSS and JS, without a
build step or third-party requests. Jekyll copies it unchanged because it
has no front matter. Configuration is at the top of its script:

| Constant | Meaning |
|---|---|
| `SONG_BASE` | storage URL; a song is read from `SONG_BASE/<id>/`. `null` until the service exists |
| `FIXTURES` | IDs served from `fixtures/` next to the page; `#neon-skyline` is the demo |
| `ID_PATTERN` | accepted upload IDs (10 base62 characters); anything else is "not a song" |
| `APP_STORE`, `RELEASES` | download links |
| `REPORT_EMAIL` | report address until `POST /songs/{id}/report` exists |

Behaviour:

1. Read the ID from `location.hash`, fetch `meta.json`, set the title.
   The page reloads when the hash changes.
2. Show `cover.png` with a playhead and a tint over the played part, from
   `audio.currentTime`. The facts line shows BPM, current bar and length.
3. Play `audio.m4a` with an `<audio>` element and custom controls. Click
   or drag on the arrangement to seek; the arrangement is also a keyboard
   slider (arrow keys move one bar, Page Up/Down eight bars, Home/End).
   Space plays and pauses. Lock-screen controls use the Media Session API.
4. **Download Groove**: goes to the store or release download for the
   visitor's platform, and to the full list on other platforms (Android has
   no build). The visitor's platform is outlined in the list.
5. **Open in Groove**: `groove://open?id=<id>`. If the page is still
   visible after 1.5 s, it says Groove didn't open and scrolls to the
   downloads.
6. **Report this song** (mail with the song link) and a notice that a
   Groove user shared it.
7. States: no ID (what the page is for), a malformed ID, a missing or
   deleted song, damaged or unreadable `meta.json`, and audio that fails to
   load.

Seeking needs HTTP Range responses. GitHub Pages and R2 send them;
`python3 -m http.server` does not, so test the page locally with a server
that does.

The fixture in `docs/groove/play/fixtures/neon-skyline/` is the Neon
Skyline demo rendered with `song_export_wav()` + `ui_media_encode()` (M4A),
`song_export_canvas()` at 1200×630 and `song_write_copy()`. The Share Link
command will produce the same four files. Page images in `assets/` come
from `apps/groove/share/branding/groove.png` and `apps/groove/share/icon.png`;
icons are Groove's Phosphor play-fill, pause-fill and arrow-square-out.

## Link previews

iMessage, Discord, X, Telegram and Slack create link previews from
`og:` tags without running the page's JavaScript, and they do not send the
fragment. A static page on Pages therefore gives every song the same
preview card ("A song made with Groove"). Per-song previews (title, cover,
inline audio) need HTML generated for each ID. The Worker could serve
`share.groove.<domain>/s/{id}` with those tags and a redirect to the Pages
player. This is optional and can come after the first version.

## Deep link

Register the `groove` URL scheme in the macOS and iPad Info.plist
(generated from the Xcode project generator, not edited by hand), and as a
protocol handler in the Linux `.desktop` file and Windows registry from
the installers. Universal links are not possible on
`corepunch.github.io/orion-ui`. They require
`/.well-known/apple-app-site-association` at the root of the host, and the
host root belongs to the `corepunch.github.io` repository. A custom domain
for Pages would make them possible later.

## Abuse and copyright

Recording means users can upload any audio. Before launch:

- A report button on every page, sending reports to the Worker, with an
  email notification to the maintainer.
- A takedown procedure: delete by ID with an admin token. Document it on a
  `docs/groove/share-policy.md` page linked from the player.
- A first-share confirmation in the app: the song becomes public, anyone
  with the link can hear and download it.
- Update `docs/groove/privacy.md`. It currently says the app does not send
  projects or recordings to the developer; Share Link does, on request.

## Later

- **Play the project in the browser.** Compile the synth and mixer to
  WebAssembly and play `song.groove` live with an AudioWorklet. Stock-only
  songs (a few KB) could then be put inside the link itself, compressed in
  the fragment, without any upload. This requires exact parity with the
  app for every format version; `audio.m4a` stays as the fallback.
- **Remix in the browser:** a read-only arrangement view from the same
  WebAssembly build.
- **Optional accounts** for a public profile and the list of a user's songs
  on other devices.

## Tests

- Unit: `meta.json` contents, share-again detection, `Shares.tsv` read and
  write, cancellation removes staging files and uncommitted uploads.
- Worker: size and rate limits, commit with missing files, delete with a
  wrong token, CORS headers.
- Page: a fixture song in `docs/groove/play/fixtures/`, loaded locally;
  missing song; deleted song; playhead position at the end of the song.
- Manual: Open in Groove on signed macOS and iPad builds, and preview
  cards in iMessage and Discord.

## Open questions

1. The App Store link (`apps.apple.com/app/id6821380428`) works only once
   Groove is approved and released.
2. Domain for the upload service (and possibly for Pages, which would also
   enable universal links).
3. Who receives abuse reports, and the response time we promise.
4. Whether shared songs expire (for example after a year without plays) or
   stay until deleted.
