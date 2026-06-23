<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-service-notifications

The `org.freedesktop.Notifications` (Desktop Notifications Specification 1.3)
server for Phosphor-based desktop shells.

## Responsibility

This library IS the notification daemon. It owns the well-known name and answers
it, rather than being a client of one. There is no UI. It stores and lifecycles
notifications and surfaces them as Qt + QML types for a shell to render.

- Acquire `org.freedesktop.Notifications` on the session bus and answer its four
  methods (`Notify`, `CloseNotification`, `GetCapabilities`,
  `GetServerInformation`) and its signals (`NotificationClosed`, `ActionInvoked`,
  `ActivationToken`).
- Own the notification lifecycle: id allocation, `replaces_id` reuse, per-urgency
  expiry, user dismissal, action invocation, and close-reason bookkeeping.
- Decode the hint set (urgency, category, desktop-entry, image-data / image-path,
  resident, transient, suppress-sound, value) into a typed `Notification`, and
  expose the live set through a `NotificationModel`.

Toasts, the notification center, per-app rules, and markup rendering are
future shell consumers of this library, not part of it.

## Key types

| Type                 | Role                                                                              |
|----------------------|-----------------------------------------------------------------------------------|
| `NotificationServer` | Owns the bus name and the notification lifecycle, and is the forwarding target of the generated `org.freedesktop.Notifications` adaptor. `dismissNotification` / `invokeAction` are `Q_INVOKABLE` for a UI, never exported on the bus. |
| `Notification`       | One decoded live notification (summary / body / actions / urgency / image / hints), mutated in place on `replaces_id`. |
| `NotificationModel`  | `QAbstractListModel` over a server's live notifications (bind its `server` property). Rows track add / replace / close. |

## Typical use

C++ shell composition root:

```cpp
#include <PhosphorServiceNotifications/QmlRegistration.h>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    PhosphorServiceNotifications::registerQmlTypes();
    // ... load shell.qml
}
```

QML consumer (a toast per live notification):

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

## Design notes

- **Generated adaptor, not direct export.** The interface is generated from
  `src/dbus/org.freedesktop.Notifications.xml` via `qt6_add_dbus_adaptor`. All
  four methods reply synchronously, so there is no delayed-reply reason to
  hand-roll dispatch the way `phosphor-service-bluetooth`'s `Agent1` does.
- **Name conflict is inert, with opt-in takeover.** Exactly one process may own
  the name. When another daemon (dunst / mako / Plasma) holds it, the server
  surfaces `nameAcquired() == false` and stays inert. `acquireName(true)` (the
  CLI's `--replace`) takes over an owner that allows replacement, and is never a
  forced default.
- **Image decode, once.** The `image-data` `(iiibiiay)` hint is demarshalled
  inline (`QDBusArgument`) into a deep-copied `QImage`, with `image-path`
  falling back to a file load or an icon-theme name. This is the lib's only
  `Qt::Gui` use, scoped to `QImage`. The raw hint map stays on `Notification`
  for advanced bindings. The `image` property / model role is a `QImage`, so a
  QML delegate that wants to paint it needs a `QQuickImageProvider` keyed off the
  notification id (a shell toast supplies one). It is not directly an
  `Image.source`.
- **Markup stays raw.** `body` markup is stored, never rendered. `GetCapabilities`
  advertises `body` / `actions` / `icon-static` / `persistence` but not
  `body-markup` until a renderer exists.
- **Dependency injection for tests.** `NotificationServer` takes an injectable
  `(QDBusConnection, serviceName)`, so the ingest path runs over a private peer
  bus with no real session daemon and no name conflict. The `image-data` decode,
  which needs real marshalling, is covered by a peer-to-peer wire test.

## Dependencies

- A free `org.freedesktop.Notifications` name on the session bus (another running
  notification daemon makes this server inert unless taken over via `--replace`).
- `PhosphorServiceIconTheme` (private link): `image-path`-as-icon-name resolution.
- Qt6 ≥ 6.6 (Core, Qml, DBus, Gui). Gui is scoped to `QImage` decode only.

## Status

Shipped. The name-owning server with the generated
`org.freedesktop.Notifications` adaptor and opt-in `--replace` takeover
(`NotificationServer`), `Notify` ingestion with the full hint decode including
the `image-data` `(iiibiiay)` → `QImage` path (`Notification`), per-urgency
expiry timers, the unified close lifecycle (expired / dismissed / closed-by-call
reason codes), `invokeAction` + `ActivationToken` and `dismissNotification`, the
`QAbstractListModel` over the live set (`NotificationModel`), and the
`phosphorctl`-style CLI demo (`examples/phosphor-service-notifications-cli`:
watch, send, close, info, running as the daemon and logging received
notifications) all landed. Three test binaries pin the contract
deterministically over a private peer bus with no session daemon: the direct-call
smoke harness (id allocation + `replaces_id`, hint decode, expiry / dismiss /
action lifecycle, model roles + add / replace / close under
`QAbstractItemModelTester`), the QML-engine facade load test, and the
peer-to-peer wire test (real marshalling, including the `image-data` struct
decode and its oversized-input rejection). UI consumers (toast, notification
center) are future shell consumers.
