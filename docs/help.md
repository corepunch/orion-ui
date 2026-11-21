---
layout: default
title: Orion Help
---

# Orion Help

Choose **Help → Help Topics** or press **F1** in Ledger or Git Client. The
modeless viewer stays open while you work. Choose a topic from **Contents** or
search topic titles, keywords, article text, and screenshot captions. Use
**Back** and **Forward** to revisit topics. With the article focused, use
Page Up, Page Down, Home, and End to scroll it.

The **Tutorials** group on each app's Help tab opens guided tours on the live
screen. The tour highlights the relevant controls and advances with **Next**.

## The .ohlp book format

An Orion Help book is one ZIP file with the extension `.ohlp`. It contains a
UTF-8 `book.xml` manifest and any referenced JPEG or PNG screenshots. Version 1
uses this structure.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<help version="1" title="Example Help">
  <topic id="start" title="Get started" keywords="open workspace">
    <p>Choose File → Open to open your first workspace.</p>
    <image src="workspace.jpg" caption="The window after opening a workspace." />
    <heading>Return to your workspace</heading>
    <p>Choose your workspace from File → Recent.</p>
  </topic>
</help>
```

Topic order is the order in Contents. The first topic opens by default. Keep
each topic ID stable so an app can link directly to it. IDs and image names
use lowercase ASCII letters, digits and hyphens. Images are stored beside the
manifest. Blocks are plain text headings, paragraphs, or captioned screenshots.
Text uses Orion's string catalogs when translations are available.

Books may contain up to 128 topics and 64 blocks per topic. Keep each text
block under 512 UTF-8 bytes, each screenshot under 4 MiB, and the archive under
16 MiB. The viewer reads the archive in memory using native controls. It
requires no network connection.

## Add a book to an app

1. Put `book.xml` and the screenshots in `apps/<app>/help/`.
2. Run `make share`. It validates the source and writes a reproducible book to
   `build/share/<app>/<app>.ohlp`. The app's normal packaging copies that
   resource with its other shared files.
3. Declare the application's Help Topics command and F1 accelerator in its
   `.orion` file. In the command handler, call
   `help_open(owner, installed_book_path, NULL)`. Pass a stable topic ID in
   place of `NULL` to open context-specific help.

`help_open()` reuses a viewer already owned by the same window for the same
book. It closes the viewer automatically when its owner closes. Compile
live tutorials from the application's `<tutorials>` definitions and start the
appropriate one with `tutorial_start()` after showing its page.
