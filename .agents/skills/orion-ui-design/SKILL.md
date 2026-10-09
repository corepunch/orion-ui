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
- Icons: Lucide or project SVG icons following the stroke-only, even-coordinate, 2 px stroke rules in AGENTS.md.

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

## Related skills and docs

- Existing skill: [`orion-app-composition`](../orion-app-composition/SKILL.md)
- [AGENTS.md](../../../AGENTS.md)
- [ARCHITECTURE.md](../../../ARCHITECTURE.md)
- Theme and drawing docs in `docs/`

When a measurement or interaction pattern is ambiguous, prefer the classic Win32 / desktop convention that Orion emulates, then apply the usability principles above.
