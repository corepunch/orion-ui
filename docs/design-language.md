# Orion design language

How an Orion app should look and behave. These are the defaults the Ledger and
Gitclient redesigns settled on (see [docs/ledger/redesign](ledger/redesign/README.md)
and [docs/gitclient/redesign](gitclient/redesign/README.md)). Each rule names
the framework piece that implements it, so following the rule means using that
piece, not drawing something similar in the app.

The visuals come from the theme. Porcelain (light) and Porcelain Dark (Atom's
One Dark colours) are the reference themes. Classic keeps its bevels and
engraved text.

## Principles

1. **Say where the user is.** Every page has a title row with the page's name
   over its context (organization, repository, period). The state of the
   current object is shown as status badges next to the title.
2. **Commands are words.** A command bar uses buttons with a caption, with an
   icon only where it helps recognition. Exactly one filled (default) button
   per view: the action the view exists for.
3. **Never colour alone.** Status is a badge with an icon and a word. A row
   state is a row icon of a different shape. Colour adds speed, never meaning.
4. **Show the consequence before it happens.** Document forms preview what
   saving will write, and commit buttons say what they will commit. Previews
   run in the editing context, not in the stored data.
5. **The focused thing is the loud thing.** A table's selection is strong only
   while the table has the keyboard focus, and becomes a quiet tint
   otherwise. The sidebar selection is a tint as well.
6. **Dense but calm.** Rows of about 21–24 px with hairline dividers, amounts
   right-aligned in tabular figures, empty cells for zero values, and
   secondary text in `brTextSecondary`.
7. **The keyboard is first-class.** Every command has an accelerator from the
   `.orion` menu tree. Primary commands follow familiar conventions:
   Ctrl+Enter commits or posts, Ctrl+S saves, F9 posts.
8. **Real content.** Mockups and demos use the app's demo data, never filler.

## App shell

```
┌ menu bar ─────────────────────────────────────────────────────────────┐
│ ┌ sidebar ┐ ┌ title row: ‹ › Title / subtitle        status badges ┐ │
│ │ section │ ├ command bar: [Primary] [Cmd] [Cmd] | [Cmd ▾]   search ┤ │
│ │ section │ │                                                       │ │
│ │ …       │ │ content (tables, forms, inspector)                    │ │
│ └─────────┘ └───────────────────────────────────────────────────────┘ │
│ status bar: facts in words                                            │
└───────────────────────────────────────────────────────────────────────┘
```

| Part | Orion piece | Rule |
|---|---|---|
| Sidebar | `ActivityBar labels="right"`, `<Page>` icon + `checked-icon` | Sections the user thinks in (Sales, Purchases; Changes, History). Collapses to an icon rail with `view.sidebar`. |
| Title row | host toolbar with `<title />`, `set_window_subtitle` | Title over context. Back and forward when pages have history. |
| Command bar | page `<Toolbar>` or a `StackView` of `Button`s | Captions, grouped by separators, the primary action first. |
| Status bar | `evStatusBar` text | Facts in words ("31 files changed"), not symbols. |
| Document windows | `MDIClient`, `db_form_open` | Documents open as their own windows, or as tabs when maximized. |

## Components

### Status badge

```xml
<Badge name="state" text="Not posted" icon="ledger-doc-draft" color="warning" v-align="center" />
```

A pill with a leading icon. Tones are theme roles: `success`, `warning`,
`error`, `info`, `secondary`. Set them at run time with `bdSetColor`,
`btnSetIconName` and `set_window_title`. Painted by `draw_status_badge`; its
metrics are in `theme_t.badge`. Use badges for state ("Posted", "Paid",
"2 to push") and never as buttons.

### Buttons

```xml
<Button name="post_close" text="Post and Close" flags="BUTTON_DEFAULT" tooltip="Save, post and close (Ctrl+Enter)" />
<Button name="post" text="Post" icon="ledger-post" tooltip="Save and post without closing" />
```

- The caption is a verb phrase that says what happens ("Commit 2 files to
  main", not "OK").
- `icon=` puts a 16 px Phosphor icon before the caption. An icon-only button
  needs a `tooltip=`.
- One `BUTTON_DEFAULT` per form; Enter in a field does not trigger it in
  document forms.
- Porcelain rounds buttons at 6 px and fields at 5 px (`theme_t.button`,
  `theme_t.field`).

### Headings and read-only values

```xml
<Label name="caption" text="New Document" font="title" />
<Label name="total" field="ib.documents.total" font="title" h-align="end" />
```

`font="title"` is the heading face. A `Label` with `field=` shows the field
formatted (numbers through `ui_format_number`, dates through `ui_format_date`)
and never writes it back. Totals are labels, not disabled text boxes.

### Forms

- Related fields share one `GridView`; see `orion-app-composition`. Fields
  that belong together on one line (number and date) share a horizontal
  `StackView` inside a grid cell.
- Rarely changed settings stay out of the way: below the main fields, or
  behind a button.
- A document form reads top to bottom: heading with badges, commands, header
  fields, lines (commands above the table), what saving will write, then
  comment and totals.

### Tables

- `TableView` with `zeros="blank"`, `total="sum"` totals rows and `RowIcon`
  status icons.
- Selection: strong while the table has the focus, quiet tint otherwise
  (`CTRL_INACTIVE`, handled by ReportView).
- A table placed inside a form gets a frame: `frame="true"` draws a rounded
  border with radius `theme_t.table.corner_radius` (6 px in Porcelain), and the
  header and rows are clipped to it by `set_clip_rounded_rect`. Tables that
  fill a page or pane edge to edge stay unframed.
- Inspectors and preview panes sit beside or below a list in a `SplitView`.
  They show the selected record without opening it.

### Assistant

The Office Assistant (`share/addons/assistant`) is a command palette in an
Office 97 balloon. It opens with Ctrl+Shift+K (Help, Find a Command) and has four
parts:

- the question;
- a field that drives the result list;
- the answer;
- grouped rows: the answer's records, related commands, tips, examples.

The arrows move through the rows and Enter opens one. Commands come from the
`.orion` menu tree (`ledger.commands()`), so the palette offers exactly what
the menus do. Tips come from the books and appear when the balloon is opened;
when there is a new one while the balloon is closed, Clippy only asks for
attention. The empty field suggests one real command (`placeholders` in the
add-on's page data).

## Tokens

| Token | Porcelain | Porcelain Dark (One Dark) |
|---|---|---|
| Face `brControlBg` | `#F8F9FB` | `#21252B` |
| Wells `brColumnViewBg` | `#FFFFFF` | `#282C34` |
| Sidebar `brPanelDark` | `#EEF0F4` | `#1E2227` |
| Text `brTextNormal` | `#13151A` | `#ABB2BF` |
| Secondary `brTextSecondary` | `#646A75` | `#7F848E` |
| Accent `brAccent` | `#4C5BD4` | `#568AF2` |
| Success / Warning / Error / Info | `#17855A` `#B26B00` `#D23B30` `#2F6FD6` | `#98C379` `#E5C07B` `#E06C75` `#61AFEF` |
| Edges `brLightEdge` / `brDarkEdge` | `#D8DCE3` / `#E3E6EB` | `#3A3F4B` / `#181A1F` |
| Assistant balloon `brInfoBackground` / `brInfoText` | `#FFFFE1` / `#303025` | `#2C313A` / `#C8CCD4` |

| Metric | Value |
|---|---|
| Button / field / card / table radius (Porcelain) | 6 / 5 / 7 / 6 px |
| Badge | 18 px pill, 7 px padding, 12 px icon, face tint `0x26`, ink mix `0x50` |
| Text shadow | off in flat themes (`theme_t.text_shadow`), on in Classic |
| Quiet selection | accent at `0x26` alpha, body text colour |

Colours in controls are always theme roles. Derive variants with
`color_with_alpha` and `color_mix`, never with literal RGB.

## Writing

- Sentence case, short labels, verbs on buttons.
- State results the user can check: "Paid", "1 behind origin",
  "31 changed".
- Hints name keys in the user's language: "Ins — добавить · F2 — изменить".
- Every user-visible string goes through `.orion` or `ui_tr()`; see
  [localization.md](localization.md).

## Review checklist

- [ ] Title row names the page and its context; state shown as badges.
- [ ] One default button; captions are verbs; icon-only buttons have tooltips.
- [ ] No status shown by colour alone.
- [ ] Unfocused tables show a quiet selection.
- [ ] Amounts right-aligned, zeros blank, totals as labels.
- [ ] Looks right in Porcelain and Porcelain Dark (`--appearance light|dark`).
- [ ] Every new visual is a framework piece documented here or in
      [drawing.md](drawing.md); nothing is approximated in the app.
