# PhosphorProtocol -- API Design Document

## Overview

PhosphorProtocol is the wire-format library for the Phosphor daemon-to-
compositor protocol. It defines the D-Bus struct types, service
addressing constants, API versioning, and client-side call helpers that
both the daemon and every compositor plugin (KWin, Wayfire, future)
share. The library owns _what_ crosses the wire; it does not own _how_
(transport selection, connection management, session lifecycle).

Content-agnostic in the transport sense: the structs carry D-Bus
streaming operators today, but the POD definitions and validation
methods are transport-independent. A future `phosphor-ipc` library
providing a native Wayland or Unix-socket transport would depend on
PhosphorProtocol for its type definitions.

**License:** LGPL-2.1-or-later (pure wire-format PODs, no domain logic)
**Namespace:** `PhosphorProtocol`
**Depends on:** Qt6::Core, Qt6::DBus
**Build artefact:** `libPhosphorProtocol.so` (SHARED)

---

## Motivation

The daemon-to-compositor wire types (`dbus_types.h`, 583 LOC header +
592 LOC impl), service constants (`dbus_constants.h`, 53 LOC), and
client helpers (`dbus_helpers.h`, 116 LOC) live in
`src/compositor-common/` -- a STATIC library with a relationship name,
not a domain name. Every other reusable piece has already been extracted
into a `phosphor-*` LGPL shared library.

Problems with the status quo:

1. **GPL license.** The compositor-common files are GPL-3.0. A Wayfire
   plugin or third-party compositor integration cannot link a GPL
   library without inheriting GPL. The types are pure PODs -- no domain
   logic justifies GPL.

2. **PlasmaZones namespace.** The types use `PlasmaZones::*`. Reusable
   Phosphor libraries use `Phosphor*` namespaces. Third-party consumers
   should not carry the app name.

3. **No install target.** The static lib is internal; there is no
   `find_package(PhosphorProtocol)` path for out-of-tree consumers.

4. **Logging ownership.** `dbus_helpers.h` depends on
   `lcCompositorCommon` from `logging.{h,cpp}` -- a category named
   after the relationship, not the domain.

---

## Dependency Graph

```
PhosphorProtocol (wire types, constants, client helpers)
       |
       +------ daemon (D-Bus adaptors, services)
       +------ kwin-effect (PlasmaZonesEffect, handlers)
       +------ wayfire-plugin [future]
       +------ third-party plugins [future]
       +------ phosphor-ipc [future, native transport]
```

---

## Library Layout

```
libs/phosphor-protocol/
  include/PhosphorProtocol/
    WireTypes.h            -- 21 POD structs, enums, QDBusArgument operators, Q_DECLARE_METATYPE
    ServiceConstants.h     -- service name, object path, interface names, API version
    ClientHelpers.h        -- fireAndForget, asyncCall, loadSettingAsync
    PhosphorProtocol.h     -- umbrella include
  src/
    wiretypes.cpp          -- streaming operators, validation, registerDBusTypes()
    logging.cpp            -- Q_LOGGING_CATEGORY(lcPhosphorProtocol, "phosphor.protocol")
  tests/
    test_phosphorprotocol.cpp  -- round-trip marshal/unmarshal, validation
  CMakeLists.txt
  PhosphorProtocolConfig.cmake.in
```

---

## Namespace Migration

All types move from `PlasmaZones::` to `PhosphorProtocol::`.

Clean break -- no compat aliases. All ~50 consumer files are in-tree
and updated atomically. The `PlasmaZones` namespace continues to exist
for daemon-internal types (services, controllers, settings) that are not
part of the wire protocol.

### What moves

| Old (PlasmaZones::) | New (PhosphorProtocol::) |
|---|---|
| `WindowGeometryEntry` | `WindowGeometryEntry` |
| `TileRequestEntry` | `TileRequestEntry` |
| `SnapAllResultEntry` | `SnapAllResultEntry` |
| `SnapConfirmationEntry` | `SnapConfirmationEntry` |
| `WindowOpenedEntry` | `WindowOpenedEntry` |
| `WindowStateEntry` | `WindowStateEntry` |
| `UnfloatRestoreResult` | `UnfloatRestoreResult` |
| `ZoneGeometryRect` | `ZoneGeometryRect` |
| `EmptyZoneEntry` | `EmptyZoneEntry` |
| `SnapAssistCandidate` | `SnapAssistCandidate` |
| `NamedZoneGeometry` | `NamedZoneGeometry` |
| `AlgorithmInfoEntry` | `AlgorithmInfoEntry` |
| `BridgeRegistrationResult` | `BridgeRegistrationResult` |
| `MoveTargetResult` | `MoveTargetResult` |
| `FocusTargetResult` | `FocusTargetResult` |
| `CycleTargetResult` | `CycleTargetResult` |
| `SwapTargetResult` | `SwapTargetResult` |
| `RestoreTargetResult` | `RestoreTargetResult` |
| `PreTileGeometryEntry` | `PreTileGeometryEntry` |
| `DragPolicy` | `DragPolicy` |
| `DragOutcome` | `DragOutcome` |
| `DragBypassReason` | `DragBypassReason` |
| `HasDBusStreaming<T>` | `HasDBusStreaming<T>` |
| `DBus::ServiceName` etc. | `Service::Name` etc. |
| `DBus::Interface::*` | `Service::Interface::*` |
| `DBus::ApiVersion` | `Service::ApiVersion` |
| `DBusHelpers::*` | `ClientHelpers::*` |
| All `*List` type aliases | Same names |
| `registerDBusTypes()` | `registerWireTypes()` |

### What stays in PlasmaZones::

- All daemon services (OverlayService, WindowTrackingService, etc.)
- All D-Bus adaptors (they USE the types but are daemon-specific)
- All controllers, settings, config
- SnapEngine, ZoneDetection, etc.

---

## Header Design

### WireTypes.h

Contains all 21 POD structs, the `DragBypassReason` enum, wire-string
converters, `HasDBusStreaming<T>` trait, `QDBusArgument` operator
declarations, `Q_DECLARE_METATYPE` macros, and the `registerWireTypes()`
entry point.

The `PHOSPHORPROTOCOL_EXPORT` macro is applied to:
- Free functions (`toWireString`, `bypassReasonFromWireString`, `registerWireTypes`)
- The struct types themselves (for MSVC dllexport, Linux ignores)
- QDebug streaming operator

Structs remain POD with inline convenience methods (`toRect()`,
`fromRect()`, `validationError()`). Validation methods that have
non-trivial logic are declared in the header and defined in
`wiretypes.cpp`.

### ServiceConstants.h

Header-only. `inline constexpr` values in `PhosphorProtocol::Service::`.

```cpp
namespace PhosphorProtocol::Service {

inline constexpr QLatin1String Name("org.plasmazones");
inline constexpr QLatin1String ObjectPath("/PlasmaZones");

namespace Interface {
inline constexpr QLatin1String Settings("org.plasmazones.Settings");
// ... all 9 interface names
}

inline constexpr int ApiVersion = 2;
inline constexpr int MinPeerApiVersion = 2;
inline constexpr int SyncCallTimeoutMs = 500;

}
```

The D-Bus service name and interface strings do NOT change -- they are
the on-wire identity. Only the C++ accessor path changes
(`DBus::ServiceName` -> `Service::Name`).

### ClientHelpers.h

Header-only (inline/template functions). Moves from `DBusHelpers::`
to `PhosphorProtocol::ClientHelpers::`. The logging category changes
from `lcCompositorCommon` to `lcPhosphorProtocol`.

Functions:
- `fireAndForget()` -- fire-and-forget async D-Bus call with error logging
- `asyncCall()` -- create async call, return pending result
- `loadSettingAsync()` -- async setting fetch with QDBusVariant unwrap

---

## What Changes for Consumers

### Include paths

```
#include <dbus_types.h>       ->  #include <PhosphorProtocol/WireTypes.h>
#include <dbus_constants.h>   ->  #include <PhosphorProtocol/ServiceConstants.h>
#include <dbus_helpers.h>     ->  #include <PhosphorProtocol/ClientHelpers.h>
```

### Namespace

```cpp
// Before:
PlasmaZones::WindowGeometryEntry e;
PlasmaZones::DBus::ServiceName;
PlasmaZones::DBusHelpers::fireAndForget(...);

// After:
PhosphorProtocol::WindowGeometryEntry e;
PhosphorProtocol::Service::Name;
PhosphorProtocol::ClientHelpers::fireAndForget(...);
```

Consumers that use many types can add `using namespace PhosphorProtocol;`
in their .cpp files. Header files should use fully-qualified names.

### CMake linkage

```cmake
# Before:
target_link_libraries(my_target PRIVATE plasmazones_compositor_common)

# After:
target_link_libraries(my_target PRIVATE PhosphorProtocol::PhosphorProtocol)
```

`plasmazones_compositor_common` remains as a static library for the
non-protocol files (`snap_assist_filter`, `trigger_parser`,
`compositor_bridge`, `autotile_state`, `floating_cache`,
`geometry_helpers`). It drops its D-Bus sources and gains a link to
`PhosphorProtocol::PhosphorProtocol`.

---

## What Stays in compositor-common/

| File | Status |
|---|---|
| `compositor_bridge.h` | Stays (ICompositorBridge -- compositor SDK candidate) |
| `autotile_state.h` | Stays (compositor-plugin POD) |
| `floating_cache.h` | Stays (compositor-plugin cache) |
| `geometry_helpers.h` | Stays (inline helper) |
| `snap_assist_filter.{h,cpp}` | Stays (targeted at phosphor-tiles later) |
| `trigger_parser.{h,cpp}` | Stays (targeted at phosphor-shortcuts later) |
| `debounced_action.h` | Stays (unused, can drop separately) |
| `dbus_types.{h,cpp}` | **Removed** (moved to PhosphorProtocol) |
| `dbus_constants.h` | **Removed** (moved to PhosphorProtocol) |
| `dbus_helpers.h` | **Removed** (moved to PhosphorProtocol) |
| `logging.{h,cpp}` | **Removed** (only consumer was dbus_helpers) |

---

## D-Bus Metatype Registration

`registerWireTypes()` (renamed from `registerDBusTypes()`) registers
each type under both qualified and unqualified names via
`qRegisterMetaType<T>()` + `qDBusRegisterMetaType<T>()`. The
unqualified registration is a defensive measure documented in the
original code.

Both daemon (`main.cpp`) and compositor plugin (`PlasmaZonesEffect`
constructor) call `registerWireTypes()` at startup.

---

## Testing

Unit tests (`libs/phosphor-protocol/tests/`):

- **Round-trip marshal/unmarshal** -- each struct type serialized to
  QDBusArgument and deserialized back, fields compared
- **Validation methods** -- TileRequestEntry, BridgeRegistrationResult,
  DragPolicy, DragOutcome edge cases
- **DragBypassReason wire strings** -- toWireString/fromWireString
  round-trip, unknown-value fallback
- **HasDBusStreaming trait** -- static_assert coverage

---

## Migration Sequence

1. Create `libs/phosphor-protocol/` with all headers and sources
2. Add to root `CMakeLists.txt`
3. Update `compositor-common/CMakeLists.txt` -- remove D-Bus sources,
   link PhosphorProtocol
4. Update all consumer includes and namespaces (~50 files)
5. Update `kwin-effect/CMakeLists.txt` link
6. Update `src/CMakeLists.txt` link
7. Build and test (Docker)
8. Update `library-extraction-survey.md`
