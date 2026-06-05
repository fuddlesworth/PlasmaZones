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
  `GetServerInformation`) and two signals (`NotificationClosed`,
  `ActionInvoked`).
- Own notification lifecycle: id allocation, `replaces_id` reuse, expiry, and
  close-reason bookkeeping.
- Decode the hint set (urgency, category, desktop-entry, image-data, ...) and
  surface notifications as typed models. *(Decode + model land in milestones
  3-5.)*

It deliberately does **not** render anything: toasts, the notification center,
per-app rules, and markup rendering are Phase 3.4 / 4.3 consumers of this
library.

## Key types

| Type                 | Role                                                                              |
|----------------------|-----------------------------------------------------------------------------------|
| `NotificationServer` | Owns the bus name and the notification lifecycle; forwarding target of the generated `org.freedesktop.Notifications` adaptor. |
| `Notification`       | One decoded live notification (summary / body / actions / urgency / image / hints), mutated in place on `replaces_id`. |
| `NotificationModel`  | `QAbstractListModel` over a server's live notifications (bind its `server` property); rows track add / replace / close. |

## Design notes

- **Generated adaptor, not direct export.** The interface is generated from
  `src/dbus/org.freedesktop.Notifications.xml` via `qt6_add_dbus_adaptor`. All
  four methods reply synchronously, so there is no delayed-reply reason to
  hand-roll dispatch the way `phosphor-service-bluetooth`'s `Agent1` does.
- **Name conflict is inert, not fatal.** Exactly one process may own the name;
  when another daemon (dunst / mako / Plasma) holds it, the server surfaces
  `nameAcquired() == false` and stays inert. Taking over is an explicit opt-in
  exposed by the CLI demo, never a forced `ReplaceExisting`.
- **Dependency injection for tests.** `NotificationServer` takes an injectable
  `(QDBusConnection, serviceName)`, so the ingest path runs over a private peer
  bus with no real session daemon and no name conflict.

## Dependencies

- Qt6 ≥ 6.6: Core, Qml, DBus, Gui (`QImage` decode, scoped to image only).
- `PhosphorServiceIconTheme` (private): `image-path`-as-icon-name resolution.

## Status

Phase 2.5: in progress. Milestones 1+2 (skeleton + CMake + adaptor + name
acquisition + static methods; milestone 2's facade/name-acquisition shipped
inside the milestone-1 commit), 3 (Notify ingestion + the `Notification` object +
hint / image decode), 4 (expiry timers + close lifecycle + action invoke), 5
(`NotificationModel`), and 6 (QML facade, verified by a real QQmlEngine load
test) landed; milestones 7-9 (CLI daemon demo, full wire tests, README
finalisation) follow per the plan.
