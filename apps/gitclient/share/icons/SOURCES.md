GitClient's navigation icons use authored outline and filled pairs. The toolbar
displays the outline variant normally and the filled variant while selected,
through `checked-icon`. Path geometry is unmodified; width and height are set
to 24 px, preserving the upstream viewBox.

Source: [Phosphor core](https://github.com/phosphor-icons/core), revision
`2b75f3ad12b420c9504ef05df8d2564a28f8500e` (the same revision as Jamsheet).

| Asset pair | Upstream icon |
|---|---|
| gc-nav-overview.svg / gc-nav-overview-fill.svg | squares-four |
| gc-nav-history.svg / gc-nav-history-fill.svg | clock-counter-clockwise |
| gc-nav-github.svg / gc-nav-github-fill.svg | github-logo |

Upstream paths: `assets/regular/<name>.svg` and `assets/fill/<name>-fill.svg`.
The MIT license is included as `PHOSPHOR-LICENSE`.

The Overview table's row icons are single regular-weight Phosphor icons from the
same revision (`assets/regular/<name>.svg`), saved with a `gc-` prefix:

| Asset | Upstream icon | Row |
|---|---|---|
| gc-check-circle.svg | check-circle | checkout in sync |
| gc-warning-circle.svg | warning-circle | uncommitted changes |
| gc-x-circle.svg | x-circle | merge conflicts |
| gc-arrows-down-up.svg | arrows-down-up | commits to push or pull |
| gc-question.svg | question | folder missing |
| gc-play.svg | play | run target |
| gc-hammer.svg | hammer | build target |
| gc-flask.svg | flask | test target |
| gc-floppy-disk.svg | floppy-disk | save guessed targets as launch.xml |
| gc-stop-circle.svg | stop-circle | running target (click to stop) |
| gc-terminal-window.svg | terminal-window | show the target terminal |

The Changes pair uses Bootstrap Icons v1.13.1's `file-diff` and `file-diff-fill`,
which show additions and deletions on a document. Blind review found Phosphor's
`git-diff` ambiguous with branch comparison or synchronization.

Source: [Bootstrap Icons](https://github.com/twbs/icons/tree/v1.13.1/icons),
`icons/file-diff.svg` and `icons/file-diff-fill.svg`. The upstream 16-unit viewBox
is preserved. The MIT license is included as `BOOTSTRAP-LICENSE`.
