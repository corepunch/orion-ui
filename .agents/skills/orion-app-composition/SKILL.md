---
name: orion-app-composition
description: Use when implementing or restructuring Orion applications, especially declarative forms, library/document views, datasources, custom window classes, toolbars, or FormEditor support. Guides app work toward framework composition and keeping static UI in .orion.
---

# Orion app composition

Build applications by composing Orion framework components. Do not duplicate control behavior or build a second view system inside an app.

## Before editing

1. Read the applicable `AGENTS.md` and inspect the current app's `.orion` file.
2. Locate relevant framework controls, layout containers, datasource APIs, form code generation, and class/component registration.
3. Compare with working examples. Use `apps/socialfeed/` for database-backed forms and `apps/formeditor/` for declarative component authoring and registration; inspect other examples when they better match the feature.
4. Identify what the framework can already render and name any missing generic capability before designing an app-specific workaround.

## Choose the right layer

- **Form:** declares a screen or document view and its static hierarchy.
- **Layout control:** arranges children. `FlowView` is a container/layout, not the whole library or document model.
- **Item template:** declares the controls used to present one record in a repeated collection, such as a library block card.
- **Datasource:** supplies records and field metadata to engine-rendered views.
- **Controller:** owns app-level commands, active document/page, and state transitions; keep it out of reusable controls.
- **Window class:** gives a custom control a named, focused window procedure.

For a data-backed library, prefer a declared form containing a FlowView (or the appropriate collection control), bound to a library datasource, with an item template for each record. Do not manually paint or position repeated cards when the framework can render the datasource and template.

## Keep declarative UI authoritative

Put static forms, menus, toolbars, accelerators, datasource schemas, bindings, and item templates in `.orion` whenever supported. Runtime C code should supply data and implement behavior the declarative engine cannot express. Do not maintain a second C-built version of a form that is already declared in `.orion`.

Declare static toolbar commands in `.orion`. A toolbar datasource is appropriate when toolbar items are data-driven and the engine supports rendering that source; do not add an adapter merely to move static command declarations out of the resource file. If a binding or rendering capability is missing, prefer adding generic Orion support and then using it from `.orion`.

## Register and isolate custom components

For every custom window/control referenced from a form:

1. Define a named window class and keep its window procedure focused on that component's rendering and notifications.
2. Register the class during app startup so the runtime can instantiate it.
3. Register its component metadata/plugin with FormEditor so designers can access and configure it.
4. Keep datasource adaptation, app command routing, and view code in separate modules with narrow interfaces.

If the framework cannot yet instantiate forms or repeat item templates, add the generic engine support when practical. For a temporary bridge, keep the imperative code minimal, isolate it, and document the missing framework feature.

## Completion checklist

- Static UI lives in `.orion` wherever the engine supports it.
- Existing Orion controls and layouts are reused; missing reusable behavior is implemented in the framework rather than copied into the app.
- Data-driven collections use a datasource and declarative item template.
- Every custom form component has a named class registered at runtime and in FormEditor metadata.
- Datasource, controller, and view responsibilities remain separate.
- Any imperative bridge exists only for a specific framework gap and is kept small and isolated.
