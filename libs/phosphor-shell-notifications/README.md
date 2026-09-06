<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# PhosphorShellNotifications

The `Phosphor.Notifications` toast framework: the transient notification
popups a shell flashes when an app posts a notification. A toast anchors
under the band of the window it concerns when the placement map knows
that window, and falls back to a centred stack at the screen's top edge.
Pure QML, themed through [`phosphor-theme`](../phosphor-theme/README.md)
and built on the [`phosphor-shell-widgets`](../phosphor-shell-widgets/README.md)
atoms.

Phase 3.4 deliverable per
[`docs/phosphor-shell-design/04-implementation-plan.md`](../../docs/phosphor-shell-design/04-implementation-plan.md).

## Responsibility

Own the lifecycle and layout of toasts: stack them at the screen's top
edge (newest on top) or anchor them to the window they concern, show up
to `maxVisible` at once in the screen stack and queue the rest,
auto-dismiss each after a timeout (restarted when the pointer leaves),
and animate in/out/reflow.
Provide a seam where a rules service can suppress or retime toasts.

The framework owns presentation, queueing, and timing only. Notification
*ingest* (the `org.freedesktop.Notifications` server) is a separate
concern. The host feeds toasts in by calling `show()`. The toast demo
wires `phosphor-service-notifications` to that call.

## Key types

| Component   | Role                                                                                       |
|-------------|--------------------------------------------------------------------------------------------|
| `ToastHost` | The toast stack: screen-edge and window-anchored placement, queue beyond `maxVisible`, dismiss + promote, reveal/reflow transitions, per-app-rules seam. Takes `screenName`, `placementMap`, `decoration` and reports `inputRects` / `anchoredCount`. |
| `Toast`     | A single toast card: optional image, app name, summary, rich-text body, close, urgency accent, hover-to-pause auto-dismiss. |

## Typical use

```qml
import Phosphor.Notifications

ToastHost {
    id: toasts
    anchors.fill: parent
    maxVisible: 4
    rules: dndRules   // optional, see below
}

// when a notification arrives:
toasts.show({
    appName: "Mail",
    summary: "New message",
    body: "From <b>Ada</b>",          // StyledText markup
    imageSource: "file:///.../avatar.png",
    urgency: 1,                        // 0 low, 1 normal, 2 critical
    timeout: 5000                      // 0 = sticky
})
```

`show()` returns the toast id, or `-1` if a rule suppressed it. Dismiss
with `dismiss(id)`, and `clear()` removes everything (e.g. before a
session lock). `toastDismissed(id)` fires whenever a toast leaves, whether
it was visible or still queued.

## Design notes

- **Queue, don't flood.** At most `maxVisible` toasts are shown; the rest
  queue and promote as slots free up. Newest shows on top.
- **Hover-to-pause.** Each `Toast` owns its auto-dismiss timer, which
  stops while the pointer is over it (so a toast you're reading stays).
- **Motion via the view.** `ToastHost` uses a `ListView` whose `add` /
  `remove` / `displaced` transitions draw the band out and back and
  reflow the stack, all on `phosphor-theme` Motion tokens. Nothing
  slides in from the side.
- **Rich text + image.** `Toast.body` is `StyledText` (the freedesktop
  notification body markup subset); `imageSource` renders an image/avatar
  when set.
- **Per-app-rules seam.** Assign `ToastHost.rules`, an object exposing
  `evaluate(toast) -> { suppress: bool, timeout: int } | null`. ToastHost
  consults it before showing each toast. `suppress` drops it, and
  `timeout` overrides the auto-dismiss. The rules editor and its persistence are
  Phase 4.3 (Notification center) and wire into this seam without
  touching ToastHost.

## Dependencies

- Qt6 ≥ 6.6 Core / Gui / Qml / Quick; `QtQuick.Shapes` (`Qt6::QuickShapes`)
  for the close glyph.
- `phosphor-theme` (`Phosphor.Theme`) for tokens, Motion, and `StateLayer`;
  `phosphor-shell-widgets` (`Phosphor.Widgets`) for `SpectrumStroke`,
  `SettleAnimation` and `DecorationSlot`.
  In-tree builds link their QML plugins automatically. This
  module is static and in-tree-only today.

## Status

Shipped. `ToastHost` and `Toast` are in the tree with a QtQuickTest suite
and an acceptance demo (`examples/phosphor-toast-demo/`, fed by
`phosphor-service-notifications` so `notify-send` raises a toast). The
window-anchored placement landed with the spectrum identity work.
