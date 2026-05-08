# Library API Consistency Audit

Audit of public headers across phosphor-compositor, phosphor-placement,
phosphor-workspaces, phosphor-engine, phosphor-geometry.

## Overall Grade: B+

Strong patterns in place. Key issues to address before third-party release.

## Issues by Priority

### HIGH — Must fix before plugins ship

| # | Issue | Location | Fix |
|---|-------|----------|-----|
| 1 | Missing PHOSPHORENGINE_EXPORT on `IPlacementEngine` | `phosphor-engine/include/PhosphorEngine/IPlacementEngine.h:46` | Add export macro |
| 2 | Missing PHOSPHORENGINE_EXPORT on `IPlacementState` | `phosphor-engine/include/PhosphorEngine/IPlacementState.h:24` | Add export macro |
| 3 | `IPlacementEngine` has 30+ default implementations — unclear which methods are required | `IPlacementEngine.h:134-536` | Document required vs optional, or split into focused facets |

### MEDIUM — Should fix for API clarity

| # | Issue | Location | Fix |
|---|-------|----------|-----|
| 4 | Undocumented ownership on DaemonClient setters | `DaemonClient.h:34-45` | Add `@param handler Not owned; caller retains lifetime` |
| 5 | Undocumented ownership on `setWindowRegistry(QObject*)` | `IPlacementEngine.h:504-506` | Add ownership doc |
| 6 | `windowFocused()` vs `windowActivated()` — same concept, different names | IPlacementEngine vs DaemonClient | Standardize to `windowActivated` (Qt-aligned) |

### LOW — Cosmetic / ergonomic

| # | Issue | Location | Fix |
|---|-------|----------|-----|
| 7 | Manager vs Service naming inconsistency | `VirtualDesktopManager` vs `WindowTrackingService` | Accept as-is (Service describes the pattern, Manager describes the domain) |
| 8 | Undocumented callback lifetime on `setIsWindowFloatingFn` | `IPlacementEngine.h:508-511` | Add `@param fn Must outlive this engine` |

## What's Already Consistent (keep doing this)

- `#pragma once` everywhere
- PascalCase namespaces and classes
- camelCase methods
- I-prefix for pure abstract interfaces
- Angle-bracket includes for library headers (`<PhosphorX/Header.h>`)
- Forward declarations to minimize header deps
- `QPointer` for optional QObject refs
- No raw `new` in public APIs
- Section separators (═══) in large headers
- Signals use past tense (`windowZoneChanged`, `stateChanged`)
- Slots use `on*` prefix (`onDaemonReadySignal`)

## IPlacementEngine Split Proposal

The 500-line interface should become focused facets:

```
IPlacementEngine (core — ~15 truly required methods)
  ├── windowOpened / windowClosed / windowFocused
  ├── isActiveOnScreen / activeScreens / setActiveScreens
  ├── navigate / moveWindow / swapWindow
  └── seedWindowOrder / managedWindowOrder

IPlacementDragPreview (optional — drag insert visualization)
  └── computeDragInsertIndexAtPoint / updateDragInsertPreview / clearDragInsertPreview

IMasterOperations (autotile-only — master/stack methods)
  └── focusMaster / swapWithMaster / increase/decreaseMasterRatio / increase/decreaseMasterCount

IPerScreenConfig (optional — per-screen overrides)
  └── applyPerScreenConfig / getPerScreenConfigJson / replacePerScreenConfig

IPlacementSerializer (optional — save/load state)
  └── serialize / deserialize / buildPerScreenResnapBuffer
```

Engines inherit only the facets they implement. The daemon dispatches
via `qobject_cast` or stores typed pointers for optional facets.
