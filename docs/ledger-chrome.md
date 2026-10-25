---
layout: default
title: Ledger Chrome
nav_exclude: true
---

# Ledger chrome

Status: the palette and title row are done. Document tabs and the 1C gaps
below are next.

![Porcelain](screenshots/ledger-chrome/porcelain-light.jpg)
![Porcelain Dark](screenshots/ledger-chrome/porcelain-dark.jpg)

## Why Amber read as Ubuntu

| Cue | Amber | Ubuntu (Yaru / GNOME) |
|---|---|---|
| Window and chrome greys | warm `#ECECE9`, `#F7F7F5`, `#DEDEDA` | warm `#FAFAFA` / `#F6F5F4` family |
| Dialog caption | graphite `#3A3E44` bar, white text | dark header bars |
| Desktop | indigo-violet `#29356B` | aubergine `#2C001E` → `#77216F` |
| Accent | amber-orange selection gradient | orange `#E95420` |

The yellow was the smallest of these cues. Four palettes changed all of
them (Citron, Porcelain, Sage and Midnight), and **Porcelain** was chosen:
cool white surfaces, white wells, and a solid indigo selection with white
text, after Finder and banking dashboards. **Porcelain Dark** is its dark
counterpart. The desktop colour is a palette role (`brDesktopBg`) instead of a
literal in `win_desktop`.

## Light and dark across Orion

Themes come in pairs: Light ↔ Modern and Porcelain ↔ Porcelain Dark. With the
default `APPEARANCE_SYSTEM`, every Orion app follows the OS appearance at
start-up and when it changes. macOS and Windows post `kEventAppearanceChanged`
for this. See [drawing](drawing.md#light-and-dark-appearance) for the API and
`--appearance light|dark|system`.

## The title row

Before, the merged title row spanned the full width over the ActivityBar,
leaving empty band above the section panel and pushing the title far in. The
sidebar and the band shared one colour. Now, as in Finder and Mail:

- **The sidebar runs full height in its own colour.** The ActivityBar paints
  as `THEME_PART_SIDEBAR` (`TOOLBAR_STYLE_SIDEBAR`). A `<sidebar-separator />`
  in the title row (≈ `NSTrackingSeparatorToolbarItem`) gives the segment
  above the sidebar the same paint, and the toggle button sits there. Layout,
  paint and hit-testing all use the left pane's edge.
- **Collapse to a rail.** The toggle and Ctrl+B call
  `page_host_set_sidebar_collapsed()`, which hides the bar's labels; the title
  row follows the new edge.
- **Back and forward.** PageHost records selections (`page_host_go`,
  `page_host_can_go`); Ledger binds `view.back` and `view.forward` with
  Alt+Left/Right and enables the buttons on `evPageChanged`.
- **Title and subtitle.** The window title names the page; the organisation
  is the subtitle (`set_window_subtitle`, ≈ `NSWindow.subtitle`).
- **Window controls** stay at the trailing end, as on Windows. A Finder-style
  leading placement would be a theme metric, not an app option.

## Next: documents as tabs (MDI)

1C 8.3 opens every form in a tab of the main window: journals, documents and
reports. Ledger opens documents as modal dialogs, so only one is open at a
time.

- The host gets a **document tab strip** under the title row. The section page
  is the pinned first tab, and documents and reports open beside it, captioned
  like the document ("Реализация товаров РТ-0001 от 10.03.2003"). This reuses
  the TabView-owns-windows model from [page docking](page-docking.md).
- **Hosted editing contexts:** `db_form_open(host, form, record_id, opts)`
  creates the same form as `show_db_dialog_opts`, docked as a tab, with the
  same editing context. OK closes the tab; a modified tab asks before closing.
  Modal dialogs stay for small row editors.
- Tab activation joins the navigation history, so Back returns to the
  previous document.

## Next: 1C screens

| 1C screen | Ledger today | Missing |
|---|---|---|
| **Остатки товаров** (Бухгалтерия 3.0 report) | No warehouses. Quantities exist on document lines and entries, but no stock report. | `Склады` catalog; warehouse on goods documents; a stock register (quantity and amount by warehouse and item); a report with a date, grouping by warehouse, group totals and a Сформировать/Настройки bar; a balance-entry assistant |
| **Начисление зарплаты** | Payroll document: employee, position, amount, withheld, contributions, net; Fill | "Зарплата за" period field; department; days and hours worked; line numbers; separate Записать / Провести / Провести и закрыть; Дт/Кт entries view; Начислить and Удержать kinds; Выплатить (a payment statement based on the document); amounts that drill down |
| **Заказ-наряд** | — | This screen is from **1C:Альфа-Авто**, an auto-service configuration, not Бухгалтерия. It covers customer car, VIN, mileage, works in norm-hours, parts, customer materials and piecework split. It fits a separate Ledger configuration. |
| Shared form chrome | Modal dialogs with OK and Post / Cancel | Tabs; favourites; a command bar with the default Провести и закрыть; Создать на основании; per-document Отчеты; attachments |
