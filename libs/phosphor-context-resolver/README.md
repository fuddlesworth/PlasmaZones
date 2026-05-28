# PhosphorContextResolver

A small LGPL library that wraps the four interlocking PlasmaZones state
sources (`ScreenModeRouter`, `VirtualDesktopManager`, `ActivityManager`,
`ISettings` disable/lock surface) into one **frozen-snapshot** resolver
façade. Every consumer that previously stitched

```cpp
const auto mode = m_screenModeRouter->modeFor(screenId);
const int desktop = m_layoutManager->currentVirtualDesktop();
const QString activity = m_layoutManager->currentActivity();
if (isContextDisabled(m_settings.get(), mode, screenId, desktop, activity))
    return;
if (m_settings->isContextLocked(
        Utils::contextLockKey(static_cast<int>(mode), screenId),
        desktop, activity))
    return;
```

becomes

```cpp
const auto ctx = m_contextResolver->handleFor(screenId);
if (m_contextResolver->isGated(ctx))
    return;
```

## Architecture

- **`ContextHandle`** — POD snapshot of `(screenId, virtualDesktop,
  activity, mode)`. Captured once per consumer call site so handlers
  cannot read desktop N at line 5 and act on desktop N+1 at line 12
  because the user happened to virtual-desktop-switch mid-call.

- **`IContextResolver`** — the façade. Owns no state; routes every public
  query into the bound adapter trio.

- **`IWorkspaceState`** — adapter for "where is the user right now?"
  (desktop + activity). The daemon's implementation forwards to
  `PhosphorWorkspaces::VirtualDesktopManager` and `ActivityManager`.

- **`IModeProvider`** — adapter for per-screen mode dispatch. The daemon's
  implementation is a one-line wrapper around `ScreenModeRouter::modeFor`.

- **`IContextGateSource`** — adapter for the disable + lock cascade. The
  daemon's implementation routes the disable triple to
  `IZoneVisualizationSettings` and composes the lock key for
  `ISettings::isContextLocked` internally.

- **`ContextResolver`** — the concrete `IContextResolver` everyone ships,
  takes three non-owning pointers (one per adapter) and routes the
  public API through them.

## What the resolver does NOT do

- Rule evaluation. That stays in `PhosphorWindowRule`. The resolver
  consumes `LayoutRegistry` (which already wraps the rule evaluator) when
  it needs effective values; it does not re-implement priority math.
- Engine routing. `ScreenModeRouter::engineFor(screenId)` /
  `navigatorForShortcut(...)` continue to dispatch placement engines.
  The resolver only consumes `modeFor()` because the gate primitives are
  Mode-scoped.
- Mutation. Toggling a monitor-disable list or a lock still goes through
  the original settings writer surface. The resolver is read-only.
- Settings-app QML / D-Bus. Those layers live in GPL trees and stay
  there; the resolver is consumed by the GPL daemon / D-Bus adaptors /
  KWin effect via standard GPL↔LGPL linking.

## License

LGPL-2.1-or-later. The library is consumed by GPL-3 daemon code; the
LGPL boundary keeps the resolver linkable from future third-party
consumers (plugins, headless tooling) without forcing GPL on them.
