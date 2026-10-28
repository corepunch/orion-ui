---
name: orion-ui-design
description: >
  Design guidelines for Orion UI, adapted from Apple's Human Interface Guidelines
  and classic Win32 conventions. Provides rules for visual hierarchy, controls,
  layout, accessibility, and consistency so agents produce polished, usable
  desktop interfaces in the Orion retro style.
  Use when designing or reviewing Orion apps, forms, or controls.
---

# Orion UI Design Skill

Orion is a retro-styled, cross-platform desktop UI framework that faithfully
reproduces the Windows API (Win32) message-based model with hardware-accelerated
rendering. Good taste means respecting both classic desktop conventions and
modern accessibility/usability principles distilled from Apple's HIG.

Source inspiration: https://github.com/justinwetch/HIGAgentSkills. The principles
below are adapted to Orion's component model (windows, controls, themes, datasources)
and its WinAPI-inspired architecture.

## When to use

- Designing a new form, dialog, toolbar, or main window in an `apps/` project.
- Reviewing `.orion` declarative UI or C window procedures for hierarchy and usability.
- Choosing theme roles, control types, and layout containers.
- Ensuring accessibility and keyboard navigation.

## Core principles (HIG-inspired + Orion conventions)

- **Clarity and hierarchy**: One primary task per window or form. Use visual weight, spacing, and grouping so the most important content is obvious.
- **Consistency**: Use the same control for the same purpose. Prefer framework controls (`Button`, `Label`, `Edit`, `List`, `ComboBox`, `TabView`, etc.) over custom drawing.
- **Native-feeling within the retro theme**: Shapes, borders, and metrics come from the active theme (`card_corner_radius`, colors as `brText*` roles). Do not hard-code RGB or approximate rounded corners with stacked rectangles.
- **Keyboard first**: Every action should be reachable via keyboard. Use accelerator tables, not raw key handling. Tab order and focus must be logical.
- **Feedback and state**: Controls show disabled, hover, pressed, and focused states via the theme. Invalidate windows on state changes.
- **Accessibility**: Provide meaningful labels and tooltips. Support high-contrast themes. Do not rely on color alone for status.
- **Density appropriate for desktop**: Dense enough for power users, but with clear grouping and breathing room. Avoid cramped forms.
- **No wasteful animation or polling**: Idle UI does no extra work. Repaint only via `invalidate_window`.

## Layout and composition

- Prefer declarative `.orion` files for static structure (forms, menus, toolbars, datasources, templates).
- Use `GridView` for labeled form fields so labels and inputs share edges.
- Use `StackView` / `FlowView` / `Card` for grouping. Composite views are sub-windows laid out by the layout system, not single custom-painted controls.
- Accent edges and cards use framework drawing functions that respect the theme silhouette.
- Icons: use only Phosphor upstream SVG assets, following AGENTS.md. Use matching regular and fill weights for unselected and selected states.

## Controls mapping (Win32 / Orion)

| Purpose | Orion control / approach |
|---------|--------------------------|
| Action | `Button` (default, push, check) |
| Text input | `Edit` |
| Choice | `ComboBox`, `ListBox` |
| Data display | `TableView` / list with datasource |
| Navigation | `TabView`, menus, toolbars |
| Grouping | `Card`, `GroupBox` equivalent via theme |
| Status | theme-colored labels or badges (`draw_badge`) |
| Dialog | `show_dialog` / modal window proc |

Extend framework controls in `orion/commctl/` rather than reimplementing behavior in apps.

## Visual language

- Colors are theme roles only (`brTextError`, `brTextSuccess`, etc.). Soften with `color_with_alpha` when needed.
- Typography follows the theme. Prefer system or theme fonts.
- Icons are 24×24 SVG, stroke-based, with proper margins and even coordinates.
- Dark/light or high-contrast themes are handled by switching the theme; do not hard-code appearance.

## Orion design language (the defaults)

Read [docs/design-language.md](../../../docs/design-language.md) before designing or reviewing a screen. In short:

- **App shell**: section sidebar (`ActivityBar labels="right"`), title row with the page name over its context, a command bar of captioned buttons with one default action, facts-in-words status bar.
- **State as badges**: `<Badge text=… icon=… color="success|warning|error|info|secondary">` next to the title; never colour alone.
- **Buttons**: verb captions ("Post and Close", "Commit 2 files to main"), `icon=` for a leading 16 px icon, `tooltip=` on icon-only buttons, exactly one `BUTTON_DEFAULT`.
- **Read-only values**: `<Label field=…>` (formatted, never written back); headings use `font="title"`.
- **Tables**: blank zeros, `total="sum"`, row icons for state; strong selection only while focused (quiet tint otherwise is automatic).
- **Consequences first**: preview what saving or committing will do, computed in the editing context.
- **Themes**: verify in Porcelain and Porcelain Dark (One Dark colours) with `--appearance light|dark`; flat themes draw no text shadow.
- **Mockups**: render real demo data with the theme's tokens before building; keep the mockup and the built screen side by side and close the gaps.

If a screen needs something these pieces cannot express, add the piece to Orion (`orion/commctl`, `orion/user`, `tools/orionc.c` and `orion/commctl/form_loader.c` together), document it in `docs/drawing.md` or `docs/design-language.md`, and only then use it.

## Related skills and docs

- Existing skill: [`orion-app-composition`](../orion-app-composition/SKILL.md)
- [docs/design-language.md](../../../docs/design-language.md)
- [AGENTS.md](../../../AGENTS.md)
- [ARCHITECTURE.md](../../../ARCHITECTURE.md)
- Theme and drawing docs in `docs/`

When a measurement or interaction pattern is ambiguous, prefer the classic Win32 / desktop convention that Orion emulates, then apply the usability principles above.
