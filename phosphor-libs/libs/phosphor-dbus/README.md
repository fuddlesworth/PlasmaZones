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
Phosphor or any particular service. Project-specific
service names and wire types belong one layer up (see
[`phosphor-protocol`](../phosphor-protocol/README.md)). Any Qt app, shell
plugin, or tool can reuse this library directly with its own endpoints.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorDBus::Client`           | Value-type method-call client bound to a `(connection, service, objectPath)` triple. Provides `fireAndForget`, `sendOneWay`, `asyncCall`, `syncCall`, `createCall`. |
| `PhosphorDBus::ObjectManager`    | Service-agnostic observer for `org.freedesktop.DBus.ObjectManager`. Issues `GetManagedObjects`, tracks `InterfacesAdded` / `InterfacesRemoved`, and emits raw `(path, interfaces)` payloads for consumers to materialise their own typed objects. |
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
  handle and two strings, so it is cheap to copy and to construct per call site.
  A project that always targets one daemon wraps construction in a small
  free-function factory that returns a `Client` by value, with no shared
  global state and no `Q_GLOBAL_STATIC`.
- **No synchronous introspection.** Calls are built with
  `QDBusMessage::createMethodCall`, never `QDBusInterface`, so a call
  never blocks the calling thread on wire introspection, which matters for
  compositor plugins.
- **Errors route through a caller-supplied category.** `Client` takes an
  optional `QLoggingCategory*`, so consumers see failures under their own
  category rather than a category buried in this library.
- **`ObjectManager` stays un-typed.** It hand-demarshals the
  `a{oa{sa{sv}}}` / `a{sa{sv}}` payloads (no nested-container metatype to
  register) and emits the raw `(path, interfaces)` maps, leaving each
  service to build its own domain objects. Its `ready()` signal fires once,
  after the initial `GetManagedObjects` round-trip completes (success or
  error), giving consumers a deterministic "initial snapshot delivered"
  edge before they rely on incremental signals.

## Dependencies

- `QtCore`, `QtDBus`

## See also

- [`phosphor-protocol`](../phosphor-protocol/README.md) — the Phosphor
  contract layer. Builds on `Client` and `HasDBusStreaming` with the
  `org.plasmazones` service constants and wire types.
