---
layout: default
title: Ledger documents and assistant windows
nav_exclude: true
---

# Ledger documents and assistant windows

Ledger follows the Office 97 / Excel 2003 MDI interaction model: one application
frame, several modeless document editors inside its document workspace, and an
owned assistant palette. This implements the public WinAPI window contract in
Orion; it does not reproduce the proprietary Office or Microsoft Agent internals.

```text
Ledger frame
  MDIClient (document workspace)
    Document A — independent editing context
    Document B — independent editing context
  owned tool window: Clippy + speech bubble
  owned modal dialogs, when needed
```

## Documents

`<MDIClient name="documents_client" />` is a reusable framework control. Open a
database form with `db_form_open(..., client, record_id, ...)`. It returns
immediately. Reopening the same form and committed record activates its existing
editor. New drafts receive distinct internal keys and remain absent from journals
and assistant queries until saved. Ledger assigns visible document numbers when
the draft reaches the committed store.

Each document has `mdi_client` pointing to the client. Orion renders roots into
separate surfaces, so the document remains a render and command root with
screen-space geometry; its ordinary `parent` field is not repurposed. The client
provides the logical MDI parent: lifetime, effective visibility, workspace clipping
in the compositor and hit testing, activation, maximization and movement with the
frame. This differs from creating native `WS_CHILD` HWNDs, while preserving the
MDI containment contract in Orion's window system.

The Window menu cycles forward/backward (Ctrl+F6 / Ctrl+Shift+F6), closes the active
editor (Ctrl+F4), and arranges documents in a cascade or side by side. Ctrl+S saves
the active document and keeps it open; with no document it saves the infobase.
Closing a modified editor or quitting offers Save / Discard / Cancel; cancellation
keeps the application open. Small record/detail editors remain modal.

`db_form_open` owns a private `db_schema_xml` editing context. Message dispatch
binds database lookup to the receiving editor; nested detail dialogs and controls
therefore use its working data. The assistant and journals read the registered
committed store. Save validates changed rows against the opening snapshot and
applies their delta through the database class in one transaction. A conflicting
save stays open with an error. Cancel destroys only that editor's context.
See [editing contexts](editing-contexts.md) for the cost and API limits.

## Assistant and modality

Clippy uses `WINDOW_TOOLWINDOW | WINDOW_ALWAYSONTOP`, with `owner` set to the
Ledger frame through `window_set_owner`. Ownership and topmost placement are
independent properties. Orion currently scopes application palettes to their
owner family: the palette stays above documents, while another application's
foreground window can cover the entire group. This is not an OS-wide topmost
character window as provided by Microsoft Agent.
Owned modal dialogs appear above the palette and block that application's family,
including Clippy, while leaving other Orion applications usable.

Ownership is independent of the layout `parent`: a popup keeps its own coordinate
space and render root. Destroying an owner destroys its owned windows; destroying
an MDI client destroys its documents. Orion suppresses effective visibility when
the frame is hidden, preserving each window's requested visible/hidden state for
restoration. This suppression also covers Orion's hide-as-minimize behavior; raw
WinAPI `ShowWindow(owner, SW_HIDE)` has different owned-popup visibility semantics.

The assistant opens automatically without taking keyboard focus. The existing
Lua assistant supplies commands and animation; the SpeechBubble control supplies
the conversation surface. Collapsing the bubble preserves conversation and input.
File → Assistant / Ctrl+Shift+A restores a hidden companion. `--no-assistant`
disables automatic startup; `--assistant` opens it and focuses its input.

## References and checks

- Microsoft: [About the Multiple Document Interface](https://learn.microsoft.com/en-us/windows/win32/winmsg/about-the-multiple-document-interface).
- Microsoft: [Window Features — owned windows](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features).
- Microsoft Agent: [The Character Window](https://learn.microsoft.com/en-us/windows/win32/lwef/the-character-window).
  Agent's documented character window is topmost; this alone does not establish
  Office's private assistant implementation. Orion currently scopes its
  companion to its application; OS-wide Agent window parity is not implemented.
- `owned_window_test`: ownership, lifetime, modal scope, grouping, MDI activation,
  containment, maximizing and frame movement.
- `db_edit_context_test`: independent Save/Cancel, conflicts, reserved keys and
  atomic rollback after a database rule rejects a write.
- `ledger_assistant_test`: real Ledger documents, repeated opening, save without
  closing, cancellation, invisible drafts and the assistant's ownership.
