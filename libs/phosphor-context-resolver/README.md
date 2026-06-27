<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: LGPL-2.1-or-later -->

# phosphor-context-resolver

> Frozen-snapshot façade over the per-screen mode + desktop + activity +
> disable/lock cascade. Replaces hand-stitched cascade chains across the
> daemon's navigation/start/osd paths and the three D-Bus adaptors
> (SnapAdaptor, WindowDragAdaptor, WindowTrackingAdaptor) with one
> resolver call. The KWin effect does not consume this library
> (effect-side state has no disable/lock cascade). OverlayService is the
> remaining unmigrated consumer in the daemon, still on the legacy
> inline `isContextDisabled(...)` cascade. See the `src/daemon/daemon.h`
> `contextResolver()` docstring.

## Responsibility

Every gating site in the Phosphor stack used to inline the same
five-call cascade: read the screen's mode, read the current virtual
desktop, read the current activity, ask the disable list whether that
tuple is off, ask the lock store whether that tuple is locked. Each
call crossed a different state source, and the inline shape let a user's
virtual-desktop switch mid-handler decouple the mode from the gate.

This library names that cascade as a single primitive. `ContextHandle`
freezes `(screenId, virtualDesktop, activity, mode)` at construction.
The resolver answers gate questions against that frozen tuple instead
of re-crossing the workspace on every read. The cascade order
(monitor > desktop > activity) and the
`desktop = 0 means "pinned across all desktops"` sentinel match the
historical `contextDisabledReason` policy verbatim so the migration is
behaviour-preserving.

The lib owns:

- **The handle value type.** `ContextHandle` carries the
  frozen tuple between call sites without re-resolution.
- **The gate primitive.** `IContextResolver` exposes
  `disabledReason()`, `isLocked()`, `isDisabled()`, `isGated()`. Every
  gate question is one method call.
- **The adapter contracts.** `IWorkspaceState`, `IModeProvider`, and
  `IContextGateSource` are three small interfaces that consumers wire
  into their own state sources. The daemon ships concrete adapters
  forwarding to `VirtualDesktopManager` / `ActivityManager` /
  `ScreenModeRouter` / `ISettings`.
- **The concrete resolver.** `ContextResolver` borrows three adapter
  pointers, composes the cascade, and owns no state of its own.

## Key types

| Type | Purpose |
|------|---------|
| `PhosphorContext::ContextHandle`         | Frozen `(screenId, virtualDesktop, activity, mode)` snapshot passed to gate calls. |
| `PhosphorContext::DisabledReason`        | Why a context is disabled: `MonitorDisabled`, `DesktopDisabled`, `ActivityDisabled`, or `NotDisabled`. |
| `PhosphorContext::IContextResolver`      | The façade: `handleFor()`, `globalHandle()`, `handleForMode()`, `handleForPersisted()`, `disabledReason()`, `isLocked()`, `isGated()`. |
| `PhosphorContext::ContextResolver`       | Concrete `IContextResolver` over the three adapter interfaces. |
| `PhosphorContext::IWorkspaceState`       | Adapter answering "what desktop / activity is the user on?" |
| `PhosphorContext::IModeProvider`         | Adapter answering "what mode is this screen?" |
| `PhosphorContext::IContextGateSource`    | Adapter answering "is this `(mode, screen, desktop, activity)` disabled / locked?" |

## Typical use

```cpp
#include <PhosphorContext/ContextResolver.h>

using namespace PhosphorContext;

IContextResolver *resolver = /* injected — see ContextResolverWiring */;

// Snapshot once; every downstream gate sees the same tuple.
const ContextHandle ctx = resolver->handleFor(screenId);
if (resolver->isGated(ctx)) {
    return; // disabled or locked — silently no-op
}

// Override the mode axis when a caller already knows which mode to query
// (e.g. autotile-only shortcut handlers). The desktop/activity axes
// stay frozen at the original snapshot.
const ContextHandle autotileCtx =
    resolver->handleForMode(screenId, AssignmentEntry::Autotile);
const auto reason = resolver->disabledReason(autotileCtx);
```

## Design notes

- **Snapshot, not live reads.** `handleFor()` captures
  `(desktop, activity, mode)` once and threads the handle through every
  downstream gate. The raw workspace readers (`currentVirtualDesktop()`,
  `currentActivity()`) re-cross the workspace state every call and are
  documented as such. Use the handle's field when consistency with a
  prior snapshot matters.
- **Empty-screen contract.** `globalHandle()` returns a handle whose
  `screenId` is empty, and the mode is delegated to
  `IModeProvider::modeFor("")` per the documented adapter contract.
  Empty `desktop` (`<= 0`) and empty `activity` short-circuit inside
  the daemon's `IContextGateSource` adapter so a sentinel-axis handle
  never matches a per-axis disable entry. Empty `screenId` is handled
  upstream of the adapter by `ScreenIdentity::variantsFor`, which
  returns an empty list for the empty input, so `Settings::isMonitorDisabled`
  then finds no candidate to match against.
- **Adapters are non-owning.** Every interface implementation is wired
  with a raw pointer the resolver does not own, and lifetime is the
  composition root's job. The concrete `ContextResolver` `qFatal`s on a
  null adapter at construction so wiring mistakes are loud.
- **No mutation surface.** Toggling a disable list, setting a lock, and
  changing the active mode all still go through the
  original settings writer. The resolver is read-only.
- **No engine routing.** `ScreenModeRouter::engineFor(screenId)`
  continues to dispatch placement engines. The resolver only consumes
  `IModeProvider::modeFor()` because the gate primitives are
  Mode-scoped.
- **LGPL boundary for GPL consumers.** The daemon, D-Bus adaptors, and
  KWin effect are all GPL-3 and link this library through standard
  LGPL→GPL linking. Keeping the resolver LGPL preserves the option of a
  third-party plugin or headless tool linking it without inheriting GPL.

## Dependencies

- `Qt6::Core`
- [`phosphor-zones`](../phosphor-zones/README.md) — `AssignmentEntry::Mode` is the mode wire type carried in `ContextHandle` (public link).

No `Qt6::Gui`, no QObjects, no QML, no D-Bus.

## See also

- [`phosphor-rule`](../phosphor-rule/README.md) — rule evaluation. The resolver intentionally does not duplicate priority math.
- [`phosphor-zones`](../phosphor-zones/README.md) — owns `AssignmentEntry::Mode` and the wire vocabulary the handle carries.
