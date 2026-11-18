---
layout: default
title: Git Client website
nav_exclude: true
---

# Git Client website

[index.html](index.html) is the Git Client landing page: a scrollytelling page
that follows a platform engineer's morning. One app window stays pinned while
the text scrolls, and the view zooms to the part of the window each step
describes. Below 960 px the pinned window gives way to an inline crop per step.

The page is static HTML; it uses the screenshots in `shots/`.
Jekyll copies it unchanged; GitHub Pages serves it
at `/orion-ui/gitclient/site/`.

## Reproducing the screenshots

1. `make build/bin/gitclient`
2. `sh docs/gitclient/site/tools/make_site_demo.sh DIR` — the standard demo
   workspace, with api-gateway rebuilt as a small Go service with real history.
3. Start `build/bin/gitclient DIR/workspace.gitworkspace --appearance dark -rc 17777`
   and drive it over the remote-control port (`key`, `click`, `type`,
   `screenshot PATH`); see `tools/orion_rc_mcp.py`.

The Agents screenshots ran real Git Client agent batches, but `claude` and
`codex` on `PATH` were stand-in scripts that print sample progress and edit the
worktree, so the tasks finish in seconds without accounts. The terminal output
says "demo" for that reason. The snapshot commits, diffs and worktrees are
Git Client's own. The repository path in the Start Three Agents dialog was
blanked out in `window-dialog.webp`.

Screens are captured at 2048×1536 (1024×768 at 2×) and saved as WebP.
