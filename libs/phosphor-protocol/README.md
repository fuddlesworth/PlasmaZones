<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-protocol

> The PlasmaZones D-Bus contract: service names, wire types, and the
> daemon client.

## Responsibility

A daemon, a compositor-side plugin (such as a KWin effect), and a settings
UI all talk to each other over D-Bus. This library is the single place
that owns the shared `org.plasmazones` wire surface, so magic strings and
ad-hoc marshallers don't proliferate across every consumer.

It is layered so the value vocabulary does not drag QtDBus into
pure-compute consumers, and so the generic D-Bus plumbing stays reusable:

```
phosphor-dbus              generic D-Bus client + HasDBusStreaming
   ▲
phosphor-protocol          org.plasmazones contract  ── this library
   ▲
PhosphorProtocol::Types    QtCore-only value vocabulary
```

## Targets

| CMake target | Links | Linked by |
|--------------|-------|-----------|
| `PhosphorProtocol::Types`            | `Qt6::Core` only | Pure-compute domain libraries (snap engine, placement) that need to *name* protocol results but never touch the bus. |
| `PhosphorProtocol::PhosphorProtocol` | `::Types` + `Qt6::DBus` + `PhosphorDBus` | D-Bus adaptors, the daemon/effect entry points, transport round-trip tests. |

## Key headers

| Header | Purpose |
|--------|---------|
| `PhosphorProtocol/Types.h`            | Value structs + enums that cross D-Bus (drag policy, window IDs, zone rects, navigation result types) plus `Q_DECLARE_METATYPE`. **QtCore only** — no QtDBus. |
| `PhosphorProtocol/Marshalling.h`      | `QDBusArgument` `operator<<` / `operator>>` for the `Types.h` structs, the `HasDBusStreaming` `static_assert` guard, and `registerWireTypes()`. |
| `PhosphorProtocol/ServiceConstants.h` | Canonical service name, object path, interface names (`org.plasmazones.Autotile`, `.Screen`, `.Overlay`, …), API version, timeouts. |
| `PhosphorProtocol/ClientHelpers.h`    | `daemonClient()` — a `PhosphorDBus::Client` value bound to the daemon — plus thin `ClientHelpers::` wrappers (`fireAndForget`, `asyncCall`, `syncCall`, `loadSettingAsync`). |

## Design notes

- **Value types are transport-free.** `Types.h` defines the structs with
  no QtDBus dependency, so a domain library can name `MoveTargetResult`
  without linking the bus. The `QDBusArgument` marshallers are free
  functions in `Marshalling.h`, layered on top — the struct never knows
  it travels over D-Bus.
- **One source of truth for interface names.** New compositor
  integrations depend on `ServiceConstants` instead of hand-rolling the
  string.
- **Wire compatibility is explicit.** Some enums serialize as their
  legacy strings (e.g. drag policy) so that rolling upgrades don't
  require an ApiVersion bump. Unknown wire values parse to a safe default
  rather than failing loudly.
- **The daemon client is a thin binding.** `daemonClient()` returns a
  `PhosphorDBus::Client` by value, pre-bound to `org.plasmazones` on the
  session bus. The generic call mechanics live in `phosphor-dbus`; this
  library only supplies the PlasmaZones endpoint. No singleton.
- **Build-time marshaller check.** Add a struct to `Types.h` and forget
  its `operator<<` / `>>` in `Marshalling.h`, and the `HasDBusStreaming`
  `static_assert` fails the build instead of crashing D-Bus dispatch at
  runtime.

## Dependencies

- `QtCore`, `QtDBus`
- [`phosphor-dbus`](../phosphor-dbus/README.md) — generic D-Bus client and
  the `HasDBusStreaming` concept.

## See also

- [`phosphor-screens`](../phosphor-screens/README.md) — `DBusScreenAdaptor` implements the `.Screen` interface from these constants.
- [`phosphor-snap-engine`](../phosphor-snap-engine/README.md) — navigation result types (`MoveTargetResult`, `FocusTargetResult`, etc.) come from `Types.h` and are linked QtDBus-free via `PhosphorProtocol::Types`.
