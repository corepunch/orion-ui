---
layout: default
title: Git Client site drafts
nav_exclude: true
---

# Git Client website — four directions

Open [index.html](index.html) to compare them. All four use the same
screenshots in `shots/` and the app's own Phosphor icons in `icons/`.

| Variant | Direction |
|---|---|
| [a/](a/) Midnight | Dark product launch: tilted hero window, auto-playing agent steps, bento grid of crops |
| [b/](b/) Paper | Light editorial: serif headlines, numbered chapters, crops on colour panels |
| [c/](c/) Terminal | Monospace, brutalist: tabbed full window, agent pipeline, comparison table |
| [d/](d/) Story | Scrollytelling: one pinned window; the view zooms to each feature as you read |

The pages are static HTML. Jekyll copies them unchanged; GitHub Pages serves them
under `/orion-ui/gitclient/site/`.

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
