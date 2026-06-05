<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# PhosphorServiceNotifications

The session-bus server for `org.freedesktop.Notifications` (Desktop
Notifications Specification 1.2). This library IS the notification daemon: it
owns the well-known name and answers it, rather than being a client of one. It
has no UI; it stores and lifecycles notifications and surfaces them as Qt/QML
types for a shell to render.

Phase 2.5 of the service-library plan in
[`docs/phosphor-shell-design/04-implementation-plan.md`](../../docs/phosphor-shell-design/04-implementation-plan.md).

## Responsibility

- Acquire `org.freedesktop.Notifications` on the session bus and answer its four
  methods (`Notify`, `CloseNotification`, `GetCapabilities`,
  `GetServerInformation`) and its signals (`NotificationClosed`, `ActionInvoked`,
  `ActivationToken`).
- Own the notification lifecycle: id allocation, `replaces_id` reuse, per-urgency
  expiry, user dismissal, action invocation, and close-reason bookkeeping.
- Decode the hint set (urgency, category, desktop-entry, image-data / image-path,
  resident, transient, suppress-sound, value) into a typed `Notification`, and
  expose the live set through a `NotificationModel`.

It deliberately does **not** render anything: toasts, the notification center,
per-app rules, and markup rendering are Phase 3.4 / 4.3 consumers of this
library.

## Key types

| Type                 | Role                                                                              |
|----------------------|-----------------------------------------------------------------------------------|
| `NotificationServer` | Owns the bus name and the notification lifecycle; forwarding target of the generated `org.freedesktop.Notifications` adaptor. `dismissNotification` / `invokeAction` are `Q_INVOKABLE` for a UI, never exported on the bus. |
| `Notification`       | One decoded live notification (summary / body / actions / urgency / image / hints), mutated in place on `replaces_id`. |
| `NotificationModel`  | `QAbstractListModel` over a server's live notifications (bind its `server` property); rows track add / replace / close. |

## Typical use

Composition root (the shell binary) registers the QML types once, before any
engine loads QML:

```cpp
#include <PhosphorServiceNotifications/QmlRegistration.h>
PhosphorServiceNotifications::registerQmlTypes();
```

QML binds a model to a server and renders the rows however it likes:

```qml
import Phosphor.Service.Notifications 1.0

NotificationServer { id: notifications }
NotificationModel { id: model; server: notifications }

ListView {
    model: model
    delegate: ToastDelegate {
        summary: model.summary
        body: model.body
        critical: model.urgency === Notification.Critical
        onActioned: (key) => notifications.invokeAction(model.id, key, activationToken)
        onDismissed: notifications.dismissNotification(model.id)
    }
}
```

The CLI doubles as the worked example and the acceptance harness. It is both the
daemon and a client:

```sh
# terminal 1: become the notification daemon and log events
phosphor-service-notifications-cli watch --replace

# terminal 2
phosphor-service-notifications-cli send "Build done" "tests passed" --urgency critical --action default:Open
phosphor-service-notifications-cli info
notify-send "hi" "there"   # any notifying app works too
```

## Design notes

- **Generated adaptor, not direct export.** The interface is generated from
  `src/dbus/org.freedesktop.Notifications.xml` via `qt6_add_dbus_adaptor`. All
  four methods reply synchronously, so there is no delayed-reply reason to
  hand-roll dispatch the way `phosphor-service-bluetooth`'s `Agent1` does.
- **Name conflict is inert, with opt-in takeover.** Exactly one process may own
  the name; when another daemon (dunst / mako / Plasma) holds it, the server
  surfaces `nameAcquired() == false` and stays inert. `acquireName(true)` (the
  CLI's `--replace`) takes over an owner that allows replacement; it is never a
  forced default.
- **Image decode, once.** The `image-data` `(iiibiiay)` hint is demarshalled
  inline (`QDBusArgument`) into a deep-copied `QImage`, with `image-path`
  falling back to a file load or an icon-theme name. This is the lib's only
  `Qt::Gui` use, scoped to `QImage`; the raw hint map stays on `Notification`
  for advanced bindings.
- **Markup stays raw.** `body` markup is stored, never rendered; `GetCapabilities`
  advertises `body` / `actions` / `icon-static` / `persistence` but not
  `body-markup` until a renderer exists (Phase 4.3).
- **Dependency injection for tests.** `NotificationServer` takes an injectable
  `(QDBusConnection, serviceName)`, so the ingest path runs over a private peer
  bus with no real session daemon and no name conflict. The `image-data` decode,
  which needs real marshalling, is covered by a peer-to-peer wire test.

## Dependencies

- Qt6 ≥ 6.6: Core, Qml, DBus, Gui (`QImage` decode, scoped to image only).
- `PhosphorServiceIconTheme` (private): `image-path`-as-icon-name resolution.

## Status

Phase 2.5: shipped. `NotificationServer` + `Notification` + `NotificationModel`
cover the full headless server (name acquisition, ingest, hint / image decode,
expiry, dismissal, action invocation, the live model). The
`examples/phosphor-service-notifications-cli` server/client demo drives the
contract end to end, and three test binaries (direct-call smoke, QML-engine
facade, peer-to-peer wire) pass. UI consumers (toast, notification center) are
Phase 3.4 / 4.3.
