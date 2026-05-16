<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-dbus

> Generic, service-agnostic D-Bus client utilities for Qt applications.

## Responsibility

Every consumer that talks to a D-Bus service re-implements the same
boilerplate: craft a `QDBusMessage`, issue an async call, allocate a
`QDBusPendingCallWatcher`, route errors, cancel on destruction. And every
adaptor that marshals a custom struct risks a runtime "demarshalling
function failed" crash if it forgets a streaming operator.

`phosphor-dbus` owns that generic plumbing. It knows nothing about
Phosphor, PlasmaZones, or any particular service — project-specific
service names and wire types belong one layer up (see
[`phosphor-protocol`](../phosphor-protocol/README.md)). Any Qt app, shell
plugin, or tool can reuse this library directly with its own endpoints.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorDBus::Client`           | Value-type method-call client bound to a `(connection, service, objectPath)` triple. Provides `fireAndForget`, `sendOneWay`, `asyncCall`, `syncCall`, `createCall`. |
| `PhosphorDBus::HasDBusStreaming` | Compile-time check that a type has `QDBusArgument` `operator<<` / `operator>>`. Use in `static_assert` to catch a missing marshaller at build time. |
| `PhosphorDBus::lcPhosphorDBus`   | Default logging category for call-failure warnings. |

## Typical use

```cpp
#include <PhosphorDBus/Client.h>

using PhosphorDBus::Client;

// Bind once to a destination — Client is a cheap, copyable value.
Client client(QDBusConnection::sessionBus(),
              QStringLiteral("org.example.Service"),
              QStringLiteral("/example/Object"));

// Fire-and-forget; the watcher is parented so it cancels with `this`.
client.fireAndForget(this, QStringLiteral("org.example.Iface"),
                     QStringLiteral("doThing"), {42});

// Async call the caller consumes itself.
QDBusPendingCall pending =
    client.asyncCall(QStringLiteral("org.example.Iface"), QStringLiteral("query"));
```

```cpp
#include <PhosphorDBus/Streaming.h>

static_assert(PhosphorDBus::HasDBusStreaming<MyEntry>::value,
              "MyEntry needs QDBusArgument operator<< and operator>>");
```

## Design notes

- **`Client` is a value object, not a singleton.** It holds a connection
  handle and two strings — cheap to copy and to construct per call site.
  A project that always targets one daemon wraps construction in a small
  free-function factory that returns a `Client` by value; no shared
  global state, no `Q_GLOBAL_STATIC`.
- **No synchronous introspection.** Calls are built with
  `QDBusMessage::createMethodCall`, never `QDBusInterface`, so a call
  never blocks the calling thread on wire introspection — important for
  compositor plugins.
- **Errors route through a caller-supplied category.** `Client` takes an
  optional `QLoggingCategory*`; consumers see failures under their own
  category rather than a category buried in this library.

## Dependencies

- `QtCore`, `QtDBus`

## See also

- [`phosphor-protocol`](../phosphor-protocol/README.md) — the PlasmaZones
  contract layer: builds on `Client` and `HasDBusStreaming` with the
  `org.plasmazones` service constants and wire types.
