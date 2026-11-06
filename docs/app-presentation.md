---
layout: default
title: Presenting An Application
nav_order: 10
---

# Presenting An Application

Every substantial Orion app should explain what it is, show a real workflow,
and make reproduction straightforward. Treat the app page as product
documentation and as an implementation reference for other Orion developers.

## Required Content

1. **Name and one-sentence description**: identify the job the app performs.
2. **Hero screenshot**: show the main application with realistic content loaded.
3. **Capabilities**: list concrete user tasks, not internal implementation work.
4. **Run example**: include the command and any useful content argument.
5. **Framework surface**: name the Orion controls, services, and architecture
   patterns demonstrated by the app.
6. **More screenshots when useful**: add workflow, dialog, or detail views only
   when they reveal behavior not visible in the hero image.
7. **Package name**: state the package-manager command for released apps.

## Capture A Screenshot

Press **F12** in any running Orion application to save a timestamped JPEG in the
user settings directory.

Applications using `GEM_STANDALONE_MAIN` also accept a framework-owned command:

```bash
build/bin/myapp [content-arguments...] \
  --screenshot docs/screenshots/myapp_main.png
```

The launcher removes `--screenshot PATH` before calling the app's `gem_init`,
queues capture after the first fully painted frame, selects PNG or JPEG from
the path extension, and exits. Content arguments still reach the app, which
makes deterministic captures possible:

```bash
build/bin/imageeditor images/logo.png \
  --screenshot docs/screenshots/imageeditor_orion.jpg

build/bin/gitclient . \
  --screenshot docs/screenshots/gitclient_orion.jpg
```

For a custom workflow or dialog, queue capture from app code after reaching the
state to document:

```c
ui_request_screenshot("docs/screenshots/myapp_dialog.png", 90, true);
```

Use `ui_save_screenshot()` only when the current frame is already complete.
Use `ui_request_screenshot()` during initialization or message handling so
capture occurs at the event-loop paint boundary. PNG and JPEG encoding are
provided by Orion's bundled libpng and libjpeg libraries.

### How Capture Works

Screenshot capture is part of Orion's platform-backed rendering path; it does
not capture the macOS, Windows, or Linux desktop. `ui_save_screenshot()`:

1. Gets the platform surface size and display scale from `axGetSize()` and
  `axGetScaling()`.
2. Reads the current OpenGL back buffer at physical pixel dimensions through
  `capture_framebuffer_rgba()`.
3. Flips OpenGL's bottom-up rows into top-down image order and writes PNG for
  `.png`, or JPEG for `.jpg` and `.jpeg`.

`ui_request_screenshot()` is the preferred API. It invalidates visible
windows and queues the capture until `repost_messages()` has processed at least
one `evPaint`; the event loop reads the completed back buffer before presenting
the frame. Use `quit_after=true` for deterministic command-line captures.

The public declarations are in `orion/kernel/kernel.h`; request scheduling is
implemented in `orion/user/init.c` and consumed at the frame boundary in
`orion/user/message.c`. Raw framebuffer readback belongs to
`orion/kernel/renderer.c`, which uses the active platform OpenGL context.

### Check Text At Native Scale

Screenshots are captured at physical resolution. Use lossless PNG when checking
pixel coverage, and inspect it at 100% image zoom,
without browser or editor resampling. Preserve the grayscale coverage generated
by the font rasterizer; thresholding it into a binary alpha mask destroys edge
antialiasing. Also check repeated glyphs such as `H`, `I`, `l`, `m`, and `1`,
plus button and table labels. If stems are consistently blurred or uneven, fix
glyph raster positioning or select a font designed for the target pixel size;
sharpening the final screenshot is not an acceptable substitute.

## Screenshot Standards

- Store website images under `docs/screenshots/` as lowercase descriptive names.
- Prefer lossless `app_workflow.png`, such as `gitclient_orion.png`, when text
  clarity matters. Use JPEG for photographic content where compression is
  appropriate.
- Load representative content. Avoid blank documents, empty tables, splash
  screens, transient loading states, and open menus unless they are the subject.
- Show the complete app chrome and enough desktop margin to make window
  boundaries clear.
- Keep text readable at the rendered documentation width.
- Use repository-owned images; do not depend on external attachment URLs.
- Review the generated image visually and verify its dimensions and file size.
- Do not include credentials, private paths, tokens, or unrelated user data.

## App Page Template

```markdown
# App Name

One sentence describing the app and its intended user.

![App Name showing representative workflow](screenshots/app-workflow.jpg)

## Capabilities

- First user-visible capability
- Second user-visible capability
- Supported files, services, or workflows

## Run

  orion install package-name
  app-name [representative-content]

## Orion Features Demonstrated

- Controls: report view, toolbar, tabs, dialogs
- Services: database bindings, HTTP, filesystem
- Architecture: MVC, MDI, plugins, or GEM hosting
```

Keep the hero description factual and concise. Put implementation details in
"Orion Features Demonstrated" so users can scan capabilities independently of
the framework internals.

## Publishing Checklist

- The app starts successfully from the documented command.
- The screenshot was generated from the current build and visually reviewed.
- Image links are relative and work in GitHub Pages.
- Alt text names the app and visible workflow.
- Capabilities match implemented behavior.
- Package and executable names are exact.
- The app is linked from the [Applications gallery](examples).
