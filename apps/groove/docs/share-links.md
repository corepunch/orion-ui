# Sharing Groove songs

On iPad and Mac, open **Share Song…** in the toolbar or File menu.

- **Share MP3…** renders the arrangement and opens the system share sheet.
  On iPad, **Save to Files** can save the MP3 to iCloud Drive. **Export Song…**
  exports the editable `.groove` file.
- **Create Link** uploads an MP3 and opens the share sheet with an unlisted
  URL. Anyone with the link can play the song in a browser. **Include editable
  song** also uploads a copy of the project with any embedded recordings.
- **Manage Links…** browses links made on this installation, starting with
  the newest. Use it to share the link again or stop sharing.

The browser player also accepts MP3 uploads at its home page. The removal
key for a browser upload is saved in that browser. Keep the URL and use the
same browser to remove the song. Shared audio and projects remain available
until removed. Copies already downloaded by listeners remain with them.

## Hosting

Deploy the Worker from `apps/groove/share-service/` with Wrangler 4.36 or
later on a Cloudflare account with R2 enabled.

```sh
npx wrangler login
npx wrangler r2 bucket create groove-share-private
npx wrangler deploy
```

Keep the bucket private. Wrangler binds it as `SONGS`, serves the player
assets from `docs/groove/play/`, and configures rate limiting for uploads
and reports. Use the HTTPS URL returned by deployment or a Worker custom
domain. Verify that `GET /api/health` returns `{"uploads":true}`.

Set the Apple build setting `GROOVE_SHARE_BASE_URL` to that HTTPS origin
in both targets, including the archive environment. Xcode expands it into
the bundle's `GrooveShareBaseURL` Info.plist key.

```sh
xcodebuild -project apps/groove/groove.xcodeproj -scheme groove-macos \
  GROOVE_SHARE_BASE_URL=https://share.example.com build
```

Use the `groove-ipad` scheme for iPad. With the setting empty, the app offers
MP3 file sharing and disables **Create Link**. The static GitHub Pages
player continues to serve the Neon Skyline demo at `#neon-skyline`.

## API and storage

| Request | Result |
|---|---|
| `POST /api/songs` | Multipart `title`, `audio` (MP3), optional `project` (.groove). Returns `{id,url,deleteToken}`. |
| `GET /s/{id}` | Browser player |
| `GET /api/songs/{id}` | Title, MP3 URL and optional project URL |
| `GET /api/songs/{id}/audio` | MP3 with byte-range support for seeking |
| `GET /api/songs/{id}/project` | Download of the original uploaded project |
| `DELETE /api/songs/{id}` | Revokes the link with the uploader's bearer token |
| `POST /api/songs/{id}/report` | Records a JSON `{reason}` for review |

Limits are 12 MiB for an MP3, 20 MiB for a project, and 33 MiB for the
entire multipart request. The Worker uses random link IDs and stores a hash
of the removal token. The native app keeps its removal tokens in its
local settings. The tokens are excluded from the URLs people share.

Review reports under `reports/{id}/` in the R2 dashboard. For a takedown,
delete `songs/{id}/metadata` first to revoke access, then remove the
`audio` and optional `project` objects under the same prefix. Reports
remain available for review after a song is removed.

## Verification

Run `node --test apps/groove/share-service/test.mjs` from the repository
root. With Wrangler installed, run `npx wrangler dev` from the service
directory to try the browser uploader with local storage. On the deployed
host, verify upload, seeking, project download, reporting, and revocation.
With the origin configured in a signed Mac or iPad build, verify that
both the URL and MP3 open in the native share sheet.
