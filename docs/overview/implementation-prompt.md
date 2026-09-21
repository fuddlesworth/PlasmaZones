<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# Implementation prompt — Workspace Overview (branch `feat/overview`)

Revision 2, 2026-09-04, after a three-lens audit (tree facts, KWin 6.7.4 API, design
completeness). Every correction from that audit is folded in below; the audit reports themselves
are not shipped.

You are implementing the PlasmaZones **Overview**: a zoomed-out, per-monitor view of every
workspace, modelled on niri's overview and improved with the best ideas from KWin's Overview,
Hyprland's overview plugins, GNOME, COSMIC and Scroll. This prompt is self-contained together with
the five research files beside it in `docs/overview/`. Read all of them in full before writing
code. Read `CLAUDE.md` at the repo root and follow it without exception (SPDX headers, the
1000/1150-line file ceiling, Qt6 string literal rules, `PhosphorI18n::tr()` in C++, plain-prose
user-facing text, `ConfigDefaults` key accessors, no ad-hoc migrations, run tests after every
change, never `cmake --install`, never `sudo`).

Working directory: `/home/nlavender/Projects/PlasmaZones/.claude/worktrees/dynamic-workspaces`,
branch `feat/overview`, forked from `feat/dynamic-workspaces` (PR #990, v3.5 dynamic workspaces,
shipped and live-verified). Never leave this worktree. Never touch tags or releases. Commit each
phase with a conventional-commit message; do not push unless told to.

Research files (read in this order):

1. `docs/overview/research-niri-overview.md` — the reference behaviour, with niri source line refs.
2. `docs/overview/research-hyprland-overviews.md` — hyprexpo, Hyprspace, hyprtasking, KWin QML,
   GNOME, COSMIC, PaperWM, Scroll, and the "ideas worth borrowing" list.
3. `docs/overview/research-kwin-overview-source.md` — KWin 6.7.4 `QuickSceneEffect`,
   `WindowThumbnail`, `EffectTogglableState`, `WindowHeap`, `Workspace.moveDesktop`. Where it
   disagrees with the next file, this one wins. Where §6 of THIS prompt disagrees with it, §6 wins
   (it records what the audit found by reading KWin's `.cpp` files).
4. `docs/overview/seams-rendering.md` — how the existing effect paints, grabs input, animates.
5. `docs/overview/seams-workspaces.md` — workspace map, reconciler, controller, verbs, D-Bus
   channels, scroll strip API, drag-insert seams, shortcut catalog, settings pages, tests.

Every file:line in those reports was verified on 2026-09-04 against this tree, then re-audited.
Known imprecisions in the seam reports are corrected in §7. Re-read each seam before you edit it
anyway; drift is your problem to detect.

---

## 0. What is being built (user requirements, in priority order)

1. **Viewing workspaces.** Toggle a zoomed-out view where every monitor simultaneously shows its
   own ordered column of workspaces (the daemon's per-screen slice from `WorkspaceMap`), each
   workspace showing its live windows at their real positions, scaled. The trailing empty
   workspace is shown as an ordinary workspace. Named and pinned workspaces are labelled.
2. **Moving windows between workspaces, across placement modes.** Drag a window from one
   workspace to another (same monitor or another monitor), into the gap between two workspaces to
   create a new one, or onto the trailing empty one. The target may run a different placement mode
   (snapping, tiling, scrolling); the move hands the window to the target engine through the
   existing cross-mode handoff, extended to carry the drop intent. In a scrolling target the drop
   point decides new column vs join-column (stacked or tabbed), as in niri. In a snapping target
   the drop point decides the zone. In a tiling target the drop point decides the insert slot.
3. **Strip management.** From the overview: reorder workspaces by dragging a workspace label
   (niri cannot; KWin can), move a workspace to another monitor the same way, rename in place,
   pin/unpin (declare or undeclare a named workspace), close windows, and for scrolling
   workspaces pan the strip with right-drag or horizontal wheel, including on non-current
   workspaces where the pan persists until that workspace is next focused.

Non-goals (do not build): search/filter field; mipmapped downscaling; touchscreen long-press;
tablet input; interacting with a window's content inside a thumbnail; Plasma Pager integration;
any change to the reconciler's lifecycle policy (create-on-occupy, destroy-on-empty, debounce
values); any migration code; reordering columns inside a zoomed scrolling workspace (use the
existing move-column verbs, which keep working while the overview is open); same-cell drops that
change position (spring back, no verb); hot corners; an in-overview affordance for the bar.

---

## 1. Decisions already made (do not reopen)

| Decision | Value | Why |
|---|---|---|
| Rendering host | A **second KWin effect plugin**, `kwin_effect_plasmazones_overview`, a `KWin::QuickSceneEffect` subclass under `kwin-effect/overview/`, QML module `org.plasmazones.overview` embedded in the `.so` | Ratified by the user. A plugin factory creates exactly one `Effect` (`effectloader.cpp:342-377`); `PlasmaZonesEffect` is an `OffscreenEffect` (3765-line header). `QuickSceneEffect` supplies per-output QML views, keyboard grab, mouse interception, the fullscreen-effect claim, `viewAt` pointer routing with implicit grab, cross-screen DnD signals, and gesture partial activation. Requires OpenGL compositing (`QuickSceneEffect::supported()`). |
| Window imagery | `org.kde.kwin` `WindowThumbnail` items | Live (re-rendered on damage, one-frame latency), render minimized, other-desktop and other-output windows, bake in the window's opacity, aspect-FIT (never crop) into the item. Since each tile is sized `rect × zoom` with the window's own aspect, fit equals crop. KWin's shadow extends beyond `frameGeometry`, so tiles clip or inset. Cost: one full-resolution offscreen render per damaged window per frame while open. The dma-buf `SnapAssistThumbnailCapture` path is NOT used here. |
| Layout authority | The **daemon**, via a new `org.plasmazones.Overview` interface publishing per-(screen, desktop) window rects and strip structure | KWin geometry is stale for non-current scrolling workspaces (parked columns are committed off-screen; `visualX` exists only in the engine). Engines are the only source of where a workspace's windows belong. |
| Stacking order | The **effect** sorts each cell's tiles by `effects->stackingOrder()` index; a window missing from that list sorts last | No engine tracks compositor z-order. The daemon's window lists are unordered. |
| Activity | The overview shows and mutates the CURRENT activity only; the model carries `activity`; an activity change while open closes the overview | `WorkspaceMap` has no activity dimension; `crossModeMoveImpl` resolves against `currentActivity()`. |
| Rect units | LOGICAL pixels, workspace-local (relative to the output's logical geometry origin), integers | What engines and the effects API speak. The QML multiplies by zoom then rounds to physical pixels with the view's device pixel ratio (niri #1467). Never send relative floats. |
| Zoom model | One global open state; a QML-animated progress `p ∈ [0,1]`; zoom = `1 - p(1 - zoomSetting)`; workspace gap = 10% of screen height × zoom; workspaces centred horizontally; the current workspace of each screen sits exactly over the real screen at `p = 0` | niri (`compute_overview_zoom`, `workspace_gap`). `partialActivationFactor` is only the raw gesture value during a swipe and a 0/1 step on toggle; the animated QML value is the real progress (see §6.1). |
| Feature gate | Part of the **Workspaces** feature; requires it enabled AND `WorkspaceController::isAdopted()`; toggle is a no-op with a debug log otherwise | Data source is the workspace map; before adoption the map is empty. |
| Click semantics | Left press on a window starts a drag grab; release without movement activates the window, switches that screen to its workspace, closes. Left click on empty cell area: switch to it, close, focus unchanged. Left drag: move window. Right drag on a scrolling cell: pan its strip. Middle click on a window: close it. Escape or the toggle shortcut: close without changing focus. | niri press-to-grab, release-to-activate; KWin middle-close. |
| Wheel | Over a cell: vertical wheel = focus workspace up/down on THAT screen (verb carries the screen under the pointer; never changes active screen or keyboard focus); horizontal wheel or Shift+vertical = pan that cell's strip (scrolling mode only). Wheel over the backdrop or a dock thumbnail behaves as over the nearest cell of that screen. | niri. Pass-through to the bar is impossible under mouse interception (§6.3); docks are drawn as fading thumbnails like KWin. |
| Gestures | 4-finger touchpad swipe up opens (tracked), swipe down closes; 3-finger touchscreen swipe likewise (one line each, not verifiable in the virtual harness) | `EffectTogglableGesture`. |
| Default shortcut | `Meta+W`, taken over from KWin's stock Overview via the existing foreign-rebind steal when "rebind KWin desktop shortcuts" is on; otherwise unbound. Owned by the daemon's `ShortcutManager`, never by the plugin. | Consistent with the Meta+Ctrl+Up/Down takeover. Fires while the effect holds the keyboard grab because `GlobalShortcutFilter` runs before `EffectsFilter` (§6.2). |
| Backdrop | Solid colour behind the workspaces, default `#262626`, ONE `DesktopBackground` per SCREEN (not per cell) drawn blurred/dimmed behind the column like KWin's Overview, plus a workspace shadow always on | `DesktopBackground` returns the same wallpaper window for every desktop of an output on Plasma 6 (§6.4); per-cell wallpaper would be N identical full-res renders. |
| Keyboard while open | Arrows navigate windows, then workspaces at the edge; Return activates the focused window (or, with none focused, switches to the focused workspace) and closes; Escape closes; `F2` renames the focused workspace; `Delete` closes the focused window; digits `1`..`9` jump to that slice index on the active screen. Every other key is DROPPED (it cannot be forwarded; `EffectsFilter` already consumed it), but global shortcuts still fire because they are resolved one filter earlier. | niri table + KWin keys + `input.h:348-374` filter order. |
| Animation | `animationDuration` Q_PROPERTY on `OverviewEffect`, resolved from the `DesktopSwitch` motion profile (`ProfilePaths.h:85`, tree via `org.plasmazones.Settings` `motionProfileTree`), default 300 ms. All open/close motion is QML `Behavior`/`NumberAnimation` on the progress value; reversal mid-flight is Qt Quick retargeting. The effect keeps running one duration after `deactivate()` (KWin's `m_shutdownTimer`) so the close animation plays. | `EffectTogglableState` has no timeline (§6.1). No separate duration setting. |

Improvements over niri that ARE in scope (from research-hyprland-overviews §Ideas): drag a
workspace to reorder or move it to another monitor (niri #1468); rename in place, pin toggle, never
delete the last workspace; hold the column reflow animation for one duration after a workspace is
created OR destroyed by a drop; the trailing placeholder workspace as the create affordance; jump
digits.

---

## 2. Architecture

```
 daemon (plasmazonesd)                          KWin process
 ┌──────────────────────────────┐   D-Bus       ┌──────────────────────────────────────┐
 │ OverviewAdaptor (always up)  │ ───────────▶  │ kwin_effect_plasmazones_overview     │
 │ OverviewController (gated)   │ overviewModel │  OverviewEffect : QuickSceneEffect   │
 │  - IOverviewModelSource per  │ Changed +     │   - one QML view per output          │
 │    engine → model JSON       │ replay        │   - EffectTogglableState + gestures  │
 │  - verb sink → Workspace-    │               │   - caches model + workspace map     │
 │    Controller by-id verbs,   │ ◀───────────  │   - forwards verbs (fireAndForget)   │
 │    HandoffContext intent     │ verbs         │  QML: Main.qml per screen            │
 │  - open/close ownership via  │               │   WorkspaceColumn → WorkspaceCell →  │
 │    QDBusServiceWatcher       │               │   WindowTile(WindowThumbnail)        │
 └──────────────────────────────┘               │                                      │
                                                │ kwin_effect_plasmazones (existing)   │
                                                │   - gates strip/decoration work on   │
                                                │     effects->activeFullScreenEffect()│
                                                └──────────────────────────────────────┘
```

### 2.1 Daemon

#### Adaptor and controller lifetime

`OverviewAdaptor` (`src/dbus/overviewadaptor.{h,cpp}`) is an UNCONDITIONAL `QDBusAbstractAdaptor`
child of `Daemon` with `Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Overview")`, created in
`initCoreAdaptors` (`src/daemon/daemon/init_adaptors.cpp:91-140`) beside the other 28, exported by
the single `registerObject` (`init_dbus_service.cpp:69`). The daemon does NOT use
`qt6_add_dbus_adaptor`; `dbus/org.plasmazones.Overview.xml` is a checked-in descriptor added to
the root `install(FILES ...)` list (`CMakeLists.txt:548-566`), and the interface name joins
`libs/phosphor-protocol/include/PhosphorProtocol/ServiceConstants.h:34-57`. The adaptor
authenticates inbound verbs the way `OverlayAdaptor::authenticateKwinSender` does
(`src/dbus/overlayadaptor.h:130-190`); that check is daemon-side, the effect copies nothing.

`OverviewController` (`src/daemon/controllers/overviewcontroller.{h,cpp}`, pre-split into
`overviewcontroller_model.cpp` and `overviewcontroller_verbs.cpp`) is constructed only while the
workspaces feature is on and attached to the adaptor with `setController(ptr)`; teardown detaches
(`setController(nullptr)`), which also emits `closeOverviewRequested`. Construction and connects
live in a NEW `src/daemon/daemon/overview.cpp` TU (`workspaces.cpp` is at 995 lines; do not grow
it). Constructor takes `WorkspaceController*`, `VirtualDesktopManager*`, `ScreenManager*`,
`LayoutRegistry*`, `ISettings*`, and a `QList<IOverviewModelSource*>`.

#### Model

Define in `libs/phosphor-engine` (LGPL):

```cpp
class IOverviewModelSource {
public:
    virtual ~IOverviewModelSource() = default;
    /// Windows the engine tracks under key, with workspace-local logical rects.
    /// Returns std::nullopt when the engine has NO state for key (never visited,
    /// stashed by a mode reassignment, or mode disabled) so the builder can fall
    /// back to tracked window geometry. Must not create state.
    virtual std::optional<QList<OverviewWindowEntry>> overviewWindowsFor(const PlacementStateKey&) const = 0;
    /// Scrolling only: strip structure for key (viewX, columns, tiles). Others return nullopt.
    virtual std::optional<OverviewStripEntry> overviewStripFor(const PlacementStateKey&) const;
};
```

Each engine implements it (the engine base can hold the default `overviewStripFor`):

- **Scrolling**: add a KEY-taking overload of the EXISTING `ScrollEngine::stripSnapshot`
  (`ScrollEngine.h:483`, `engine_snapshot.cpp`, `ScrollStripSnapshot{Column,Tile}` in
  `ScrollEngineTypes.h:139-190`, tested by `tests/test_scrollengine_snapshot.cpp`). Do not invent a
  second column/tile type: extend `ScrollStripSnapshot` with `viewX` and absolute
  workspace-local rects (the existing fields are column-relative 0..1 and stay as they are for the
  drag-insert resolver). The overload reads `m_states` via `stateForKey(key, /*create*/false)`,
  never mutates focus or anchor, and reports parked columns at `visualX/visualY`.
- **Snapping**: new public per-key read: zone assignment per window for the key
  (`SnapState`, `zoneForWindow`) joined with that context's layout zone geometry (the shape
  `resolveCrossDesktopZone` at `SnapEngine.h:122` already computes for a target desktop).
- **Tiling (autotile)**: new public per-key read over `TilingState::tileTargetZones`
  (`libs/phosphor-tiles/include/PhosphorTiles/TilingState.h:98`) plus floats from the key's window
  order.
- **Fallback** for `nullopt`: the daemon's tracked window geometry from `WindowRegistry`, flagged
  `floating: false, mode: "none"` only when the mode for the key is disabled; otherwise the mode
  string is still reported and the rects come from tracking.

Mode per key comes from `LayoutRegistry::modeForScreen(screenId, desktop, activity)`
(`libs/phosphor-zones/include/PhosphorZones/LayoutRegistry.h:809`), the resolver
`crossModeMoveImpl` uses (`crossmode.cpp:170-171`). NOT `Daemon::currentModeFor` /
`ScreenModeRouter::modeFor`, which are current-context only and would mislabel every non-current
workspace.

Virtual screens: the map keys PHYSICAL outputs (`canonicalScreenId`); engine keys may carry the
`/vs:` virtual-screen suffix (`WindowPlacement.h:143-157`, `crossmode.cpp:293-296`). For each map
screen enumerate its virtual screens through `PhosphorScreens`, query each engine per virtual key,
and offset rects into the physical output's logical space. Compare ids with `screensMatch`, never
raw string equality.

Window entries: every window the daemon tracks on that `(screen, desktop)` is listed exactly once
across the model. Sticky (all-desktops) and multi-desktop windows appear once, on their screen's
CURRENT workspace, with `sticky: true`, and are not draggable. Windows the daemon does not track
(Dock, Desktop, Notification types) are omitted; the effect never draws windows the daemon did not
list, EXCEPT docks, which the effect draws itself from a `WindowFilterModel { windowType: Dock }`
fading with progress (KWin parity).

Schema (`v: 1`), published as the `overviewModel` string:

```
{ v: 1, generation: uint64, workspaceMapGeneration: uint64, activity: string,
  screens: { <screenId>: { logicalSize: {w, h},
    workspaces: { <desktopId>: {
      mode: "snapping" | "tiling" | "scrolling" | "none",
      windows: [ { id: string, rect: {x, y, w, h}, floating: bool, minimized: bool,
                   sticky: bool, column: int | -1, tile: int | -1 } ],   // unordered
      strip: { viewX: int, columns: [ { x: int, width: int, tabbed: bool, activeTab: int,
               tiles: [ { id: string, y: int, height: int } ] } ] }      // scrolling only
    } } } } }
```

`mode: "none"` cells render wallpaper only and refuse drops. `column`/`tile` are the scroll
engine's own vocabulary (indices are acceptable there; zone ids stay ids for snapping, resolved
daemon-side).

#### Publish and replay

Method `overviewModel()` (replay) and signal `overviewModelChanged(QString)` on the adaptor,
change-gated and stamped exactly like `workspaceMap` (`windowtrackingadaptor.h:1262-1269`). Two
counters: `generation` orders model payloads (the effect drops non-increasing);
`workspaceMapGeneration` must EQUAL the effect's cached map generation, else the effect re-requests
`workspaceMap()` then `overviewModel()`, bounded to 3 retries then closes. Both counters are scoped
by the daemon's service epoch (the client bumps its epoch on name-owner change), so a daemon
restart resets them.

Streaming is gated by `setOverviewOpen(bool)`: the effect calls `setOverviewOpen(true)` FIRST,
then `overviewModel()`; `overviewModel()` while closed returns an empty string. Rebuild triggers
while open: `WorkspaceController::workspaceMapPublished`, engine `placementChanged`,
`ScrollingAdaptor::stripChanged`, window add/remove/desktop change in `WindowRegistry`. Coalesce
with a 0-ms single-shot timer. Never emit while closed (test it).

Open state ownership: the daemon installs a `QDBusServiceWatcher` on the unique name that called
`setOverviewOpen(true)` and resets to closed (stop streaming, re-enable workspace OSD hints) when
that name vanishes. The daemon also emits `closeOverviewRequested` on feature teardown, on
activity change, and before it exits. `setOverviewOpen(true)` also suppresses the workspace OSD
hints (snap-back, displaced) until closed.

#### Verb sink (D-Bus signatures; all return void; every refusal is a `qCDebug` plus no change)

| Method | Semantics |
|---|---|
| `setOverviewOpen(b open)` | Streaming gate and OSD suppression, above. |
| `focusWorkspace(s screenId, s desktopId)` | New PUBLIC `WorkspaceController::focusWorkspaceById(screenId, desktopId)` wrapping the private `switchScreenToDesktop` (`workspacecontroller.h:229`), deferred behind the ledger like the delta verbs. |
| `moveWindowToWorkspace(s windowId, s screenId, s desktopId, i dropX, i dropY)` | dropX/Y are workspace-local logical px, UNZOOMED (the QML divides by zoom). New PUBLIC `WorkspaceController::moveWindowToWorkspaceById(windowId, screenId, desktopId, HandoffIntent)` that runs the private `watchWindowMove` and emits `windowWorkspaceMoveRequested` with the intent. There is exactly ONE placement, inside the target engine's `handoffReceive`; do not add a post-move insert. |
| `moveWindowToNewWorkspace(s windowId, s screenId, i sliceIndex, i dropX, i dropY)` | `sliceIndex` is the 0-based GAP index (0 = above the first workspace, `sliceSize` = below the trailing empty). Rule (niri): `sliceIndex >= sliceSize - 1` reuses the trailing empty and becomes `moveWindowToWorkspace` onto it; smaller indices insert. Insert needs a NEW reconciler verb `WorkspaceReconciler::requestInsertWorkspace(screenId, sliceIndex)` that ledgers a Create at `globalPositionForInsert` and tags the new desktop RESERVED so `DestroyDebounceMs` skips it until its first `onPopulationChanged`; the controller queues the window move with `runWhenQuiet` and resolves the desktop id via `desktopIdAtSliceIndex` once `mapChanged` fires. Never call `WorkspaceMap::insert` from the controller. |
| `reorderWorkspace(s screenId, s desktopId, i newSliceIndex)` | New reconciler verb `reorderWorkspace(desktopId, newSliceIndex)` beside `reorderCurrentWorkspace`, keeping `maintainScreen` + `bumpGeneration` (never call `map().reorderWithinSlice` directly, `WorkspaceReconciler.cpp:591-607`). A NAMED workspace also rewrites its declaration `position` through the `ISettings` named-entries setter so the next re-apply agrees. |
| `moveWorkspaceToScreen(s desktopId, s targetScreenId, i sliceIndex)` | New reconciler verb `transferWorkspace(desktopId, targetScreenId, sliceIndex)` beside `transferCurrentWorkspace` (refuses when the source slice would drop to zero, as `:620` does); the controller's window-relocation rider loop (`workspacecontroller_verbs.cpp:265-290`) becomes a shared helper both paths call. A PINNED workspace rewrites its `outputId` declaration. Target screens validated with `knowsScreen`, not `hasScreen`. |
| `renameWorkspace(s desktopId, s name)` | Capped at `WorkspaceNameMaxLength`. Named workspace: rewrite the declaration name. Dynamic workspace: `VirtualDesktopManager::setDesktopName` through the reconciler's name-push ledger. |
| `pinWorkspace(s desktopId, b pinned)` | Pin = append a `Workspaces.Named` declaration `{name: current KWin name or "Workspace N", output: owner screen, position: slice index}` through the daemon's `ISettings` setter for the named entries; unpin = remove it. NEVER call `applyNamedDeclarations` directly: it is fed from `Settings::workspacesNamedEntries` and re-applied on `workspacesNamedEntriesChanged` (`workspaces.cpp:704+`), so a direct call diverges from config and is undone on the next change. |
| `panStrip(s screenId, s desktopId, i deltaPx)` | Current context: existing `scrollView`. Non-current context: new `ScrollEngine::panStoredView(key, deltaPx)` applying `scrollViewBy` on the stored strip with the key's screen params, marking state dirty, emitting nothing to clients (that workspace is invisible); the next context switch's normal relayout applies it. |
| `closeWindow(s windowId)` | Relay to the effect (it owns `closeWindow`); or let the QML call `window.closeWindow()` directly. Pick: QML direct, no verb. |
| `activateWindow` | NOT a daemon verb. The QML activates directly (`SceneView.currentDesktop = desktop` for the switch is NOT used either; see §2.2 close sequence). |

The `HandoffIntent`: extend `IPlacementEngine::HandoffContext` (`IPlacementEngine.h:886`, LGPL)
with `int insertTileIndex = -1` (scrolling: join the column at `insertIndex` as a tile at this
position, clamped; a tabbed column joins its tabs; -1 = new column). `dropPos` already exists and
is used by snap. Thread the intent through `windowWorkspaceMoveRequested` →
`moveWindowToWorkspaceVerb` (`crossmode.cpp:61`) → `crossModeMoveImpl` as an optional parameter
defaulting to today's direction-derived behaviour (`crossmode.cpp:257-266`). Target arms:

- **Scrolling**: the controller computes `NewColumn(i)` / `InColumn(col, tile)` from `dropPos`
  and the key's `stripSnapshot` (mirror niri `scrolling_insert_position`: above/below/between
  columns → new column at that index; inside a tile → that column at that tile index) and sets
  `insertIndex` / `insertTileIndex`. `ScrollEngine::handoffReceive` honours `insertTileIndex`.
- **Snapping**: `handoffReceive` resolves the zone containing `dropPos` through the zone detector
  for `(screen, desktop)`, falling back to `entryZoneForCrossing`; no zone → the window is left
  free at its live frame, non-floating (what the tail at `float.cpp:655-680` does today). A window
  floating in the source arrives non-floating in the target (`WindowPlacement.h` float-is-per-mode;
  `ctx.wasFloating` stays false, `crossmode.cpp:253`). The overview never carries a float bit.
- **Tiling (autotile)**: the reactive branch (`crossmode.cpp:240, 301-330`) releases the window
  and lets the effect's catch-scan tile it when the desktop becomes current, so the dropped tile
  would VANISH from the overview. On the overview path autotile receives IMMEDIATELY with
  `ctx.toDesktop` set (the deferral existed because directional verbs had no target-desktop state;
  the overview does, via `PerScreenStates`). Add `AutotileEngine::insertIndexForPoint(key, pos)`
  mirroring `computeDragInsertTargetAtPoint` for an arbitrary key. The model reports the window at
  its post-receive tile rect until the geometry ack lands.

The overview never calls `beginDragInsertPreview` / `commitDragInsertPreview`; those belong to
the compositor interactive move, which is not running during a QML drag.

`canonicalScreenId` (`workspacecontroller.h:196`, currently private static) becomes public; every
screen id arriving on the adaptor passes through it.

### 2.2 KWin: `kwin-effect/overview/`

**CMake.** `kcoreaddons_add_plugin(kwin_effect_plasmazones_overview SOURCES ... INSTALL_NAMESPACE
"kwin/effects/plugins")` with its own `metadata.json`, `list(APPEND CMAKE_AUTOMOC_MACRO_NAMES
"KWIN_EFFECT_FACTORY_SUPPORTED")` as `kwin-effect/CMakeLists.txt:53` does, and
`qt6_add_qml_module(kwin_effect_plasmazones_overview NO_PLUGIN URI org.plasmazones.overview
QML_FILES ...)` on that SAME shared target. The QML and generated `qmldir` are embedded under
`:/qt/qml/org/plasmazones/overview/`; the rcc static initializer registers them when KWin dlopens
the plugin and `loadFromModule("org.plasmazones.overview", "Main")` resolves through the engine's
default `qrc:/qt/qml` import path. Nothing is installed under `KDE_INSTALL_QMLDIR`. Build-verify
this once; if `loadFromModule` reports "module not installed", fall back to
`setSource(QUrl("qrc:/qt/qml/org/plasmazones/overview/Main.qml"))`. `import org.kde.kwin 3.0` needs
no CMake: KWin registers `WindowThumbnail`, `DesktopBackground`, `SceneView`, `WindowFilterModel`,
`Workspace` imperatively into `effects->qmlEngine()` (`scripting.cpp:693-719`). Do not import
`org.kde.kwin.private.effects`.

**`overvieweffect.{h,cpp}`.** `class OverviewEffect : public KWin::QuickSceneEffect`, modelled on
`src/plugins/overview/overvieweffect.cpp` (research-kwin-overview-source): one
`EffectTogglableState m_state`, an `EffectTogglableGesture` with `addTouchpadSwipeGesture(Up, 4)`
and `addTouchscreenSwipeGesture(Up, 3)`, `requestedEffectChainPosition() == 70`, a one-duration
`m_shutdownTimer` between `deactivate()` and `setRunning(false)`, `screenAboutToLock →
deactivateNow`. Q_PROPERTYs for QML: `partialActivationFactor`, `gestureInProgress`,
`animationDuration`, `zoom`, `backdropColor`, `showWorkspaceNames`, `workspaceMap` (parsed
QVariantMap), `overviewModel` (parsed), `daemonAvailable`. Q_INVOKABLE forwarders for every §2.1
verb. `initialProperties(LogicalOutput*)` is overridden to pass the screen's canonical id
(computed with the same rule as `PlasmaZonesEffect::outputScreenId`, `screens.cpp:41`, including
the `/connector` suffix for duplicate models); the QML gets `effect`/`screen` from the
`SceneView.effect` / `SceneView.screen` attached properties.

Start/refusal: `setRunning(true)` silently does nothing when another fullscreen effect holds the
slot or `grabKeyboard` fails (`quickeffect.cpp:523-561`). `OverviewEffect` checks `isRunning()`
after the attempt and reports `overviewStateChanged(bool)` to the daemon; the daemon never
assumes `setOverviewOpen(true)` succeeded. KWin's own Overview, Present Windows, Cube and the
PlasmaZones desktop-switch blend (which claims the slot during its run,
`desktoptransitionmanager.cpp:219`) are therefore mutually exclusive with ours; a toggle during
one of them is a no-op.

**`daemonclient.{h,cpp}`** (namespace `PlasmaZones::Overview`, distinct from
`PhosphorCompositor::DaemonClient`): proxy to `org.plasmazones.Overview` and a second subscriber to
`WindowTracking.workspaceMapChanged` (copy the epoch/generation guards from
`daemon_bringup.cpp:975-1020`; do not include the main effect's headers). Watches
`org.plasmazones` service ownership: replay `workspaceMap()` on appearance, drop caches and close
the overview on disappearance (`lifecycle_wiring_daemon.cpp:206-207` pattern). Toggle is a no-op
while the daemon is absent. Verbs via `PhosphorProtocol::ClientHelpers::fireAndForget(parent,
interface, method, args, logContext)` (`ClientHelpers.h:52`). Handles `toggleOverviewRequested`
and `closeOverviewRequested` from the daemon.

**QML** (`qml/Main.qml`, `WorkspaceColumn.qml`, `WorkspaceCell.qml`, `WindowTile.qml`,
`WorkspaceGapDropArea.qml`, `WorkspaceLabel.qml`, `DockLayer.qml`; every file under 1000 lines):

- Root per screen: the view texture has NO alpha (`QuickSceneView` is
  `OffscreenQuickView(Texture, alpha=false)`), so the root paints the backdrop `Rectangle` over
  its whole area, then one `DesktopBackground { outputName: SceneView.screen.name }` (connector
  name, NOT the canonical id) dimmed/blurred, then the `WorkspaceColumn`, then `DockLayer`
  (`WindowThumbnail`s from `WindowFilterModel { windowType: Dock; screenName }` at
  `opacity: 1 - progress`).
- Progress: `property real progress` with `Behavior on progress { NumberAnimation { duration:
  effect.animationDuration; easing.type: Easing.OutCubic } }` bound to
  `effect.partialActivationFactor` when `!effect.gestureInProgress`, and tracking it directly
  (Behavior disabled) while a gesture is in progress. `zoom = 1 - progress * (1 - effect.zoom)`.
- Column: cells of `screen.size × zoom`, gap `0.1 × screen.height × zoom`, centred horizontally,
  scrolled so the cell for `effects->currentDesktop(output)` (resolved by `x11DesktopNumber`, read
  through a `currentDesktopIndex` property the effect exposes per screen) sits over the real
  screen at `progress = 0` and the column centres as `progress → 1` (niri `workspaces_render_geo`).
  Highlight uses the same source; the map's `current` flag is not read by the QML. Identity is by
  `desktopId`, so renumbers are invisible to the QML. Screens with `knowsScreen && !hasScreen`
  render one placeholder cell labelled with `i18nd("plasmazones", "No workspaces yet")`,
  non-droppable.
- Cell: shadow, then `WindowTile`s at `rect × zoom`, sorted by `effect.stackingIndex(id)`;
  `WindowTile` is a `WindowThumbnail { wId }` with `clip: true` (shadow bleed), dimmed when
  `minimized`, labelled when `showWorkspaceNames`. Cells are non-interactive while
  `progress < 1` and no drag is active. Round positions to physical pixels AFTER zoom.
- Cell scrolling strip: tiles are placed from `strip.columns[].x - strip.viewX`; the cell clips.
- Drag and drop: `WindowTile` is the `Drag` source (`Drag.keys: ["pz-window"]`,
  `Drag.source: tile`, `Drag.proposedAction: Qt.MoveAction`, `onX/YChanged:
  effect.checkItemDraggedOutOfScreen(tile)`). `WorkspaceLabel` is the source for `pz-workspace`.
  `WorkspaceCell` and `WorkspaceGapDropArea` are `DropArea`s accepting both keys. Release order
  follows KWin: `Drag.drop()` first; only when the result is not `Qt.MoveAction` call
  `effect.checkItemDroppedOutOfScreen(globalPos, tile)`. Qt Quick `DropArea`s only see drags from
  the SAME `QQuickWindow`, so the receiving screen resolves the cell or gap from `globalPos` itself
  in its `onItemDroppedOutOfScreen(pos, item, screen)` handler (`screen === SceneView.screen`);
  `itemDraggedOutOfScreen` may list several screens, pick the one containing the pointer. The QML
  NEVER moves a tile locally on drop: it springs the tile back to its model position, sends the
  verb, and the next `overviewModelChanged` repositions it. No optimistic placement. Refusals
  therefore look like a spring-back. Drop coordinates sent to the daemon are
  `(pos_within_cell / zoom)` as integers.
- Reflow hold: after a `moveWindowToNewWorkspace` or a drop that empties a workspace, disable the
  column's position `Behavior` for one `animationDuration` (KWin `desktopJustCreated`), so the
  destroy debounce's later renumber does not lurch the column.
- Close sequence on window click: (1) send `focusWorkspace(screen, desktopId)` (daemon →
  `setScreenDesktopRequested` → main effect `slotSetScreenDesktopRequested`, which already arms
  `m_programmaticDesktopSwitch` around the synchronous `setCurrentDesktop`, `daemon_apply.cpp:287-291`),
  (2) `effects->activateWindow(w)` directly in the plugin, (3) `deactivate()`. The column scroll to
  the chosen cell is animated in QML (bind the column anchor to the target cell before
  deactivating); a programmatic `setCurrentDesktop` emits NO `desktopChanging` offset (§6.5), so the
  `desktopOffsetForScreen` bookkeeping is kept ONLY to show a live touchpad desktop swipe inside an
  open overview.
- i18n: `i18nd("plasmazones", ...)` / `i18ndc(...)` in this QML, never bare `i18n()`: it runs in
  KWin's QML engine whose default domain is `kwin`. Verify the catalogue loads in the harness and
  note the result in `Main.qml`'s header.

### 2.3 Main effect changes (`kwin-effect/plasmazoneseffect/`)

Minimal and local: connect `effects->activeFullScreenEffectChanged` and, when
`activeFullScreenEffect() && activeFullScreenEffect() != this`, skip strip transition passes, tab
pill painting, decoration folds and the per-window burn work in `prePaintScreen`/`paintWindow`.
The whole window stack is still painted under the opaque view every frame with full-screen damage
(`workspacescene.cpp:644-649, 709-724`), so this gate is the primary GPU saving, not a belt.
`DesktopTransitionManager::begin` already refuses while a fullscreen effect is active
(`desktoptransitionmanager.cpp:198-219, 289`), and the overview's switch rides
`slotSetScreenDesktopRequested`, so no guard extension is needed. No QML, no thumbnails, no new
D-Bus subscription here.

### 2.4 Daemon shortcut and settings

**Shortcut** (`overview_toggle`), full recipe, all under `src/daemon/controllers/` and
`src/config/`: id in `shortcutmanager_ids.h`; added to the enumerated `kWorkspaceIds` set in
`shortcutmanager.cpp:62-68` (never prefix-match, comment `:55-59`; the set drives the grab gate at
`:618`); `ConfigDefaults::overviewToggleShortcut()` default in `configdefaults_workspaces.h`
(`Meta+W`); `Shortcuts.Global` key accessor; `Settings::overviewToggleShortcut()` store-backed
getter/setter (`P_STORE_GET`/`P_STORE_SET_STRING` in `src/config/settings/workspaces.cpp`),
`ISettings` signal, `snapshotWorkspaceKeyFamilies` (`settings.cpp:181,674,767,797`),
`appendWorkspacesShortcutKeys` in `settingsschema_workspaces.cpp`; `StaticEntry` row in
`shortcutmanager_table.cpp:39` (`{id, &ConfigDefaults::…, &Settings::…, label, fire}`); catalog
row in `shortcutmanager_catalog.cpp:271-279` (category "Workspaces", `"all"`); signal
`overviewToggleRequested` connected in the new `src/daemon/daemon/overview.cpp` and relayed as the
adaptor's `toggleOverviewRequested`. Stock steal: add KWin's `Overview` action to the
`KWinDesktopActions` list (`workspaces.cpp:827-830`; steal only when currently bound, `:859`).

**Settings group** `Workspaces.Overview` (`P_CONFIG_GROUP` in `configkeys_workspaces.h:37-40`,
mode-neutral, top level), five keys: `Zoom` (double, 0.5, clamp 0.1–0.75), `BackdropColor`
(`#262626`), `GestureEnabled` (bool, true), `WheelSwitchesWorkspaces` (bool, true),
`ShowWorkspaceNames` (bool, true). The workspaces block is STORE-BACKED (`settings.h:1380-1387`,
`settings/workspaces.cpp:11-17`): no members, no `loadsave.cpp` arms. Steps: `P_CONFIG_KEY`,
`ConfigDefaults` default, `ISettings` signal, `Q_PROPERTY` + getter/setter decl in `settings.h`,
`P_STORE_GET`/`P_STORE_SET_*` in `settings/workspaces.cpp`, `KeyDef`s in
`settingsschema_workspaces.cpp` (`appendWorkspacesSchema`), page-owned manifest in
`settingscontroller_pagekeys.cpp` (`pageOwnedConfigKeys()`), AND the group added to the sweep list
at `tests/unit/settings/test_page_owned_config_keys.cpp:208-214` (a group missing there is silently
unswept). The effect reads these over `org.plasmazones.Settings`.

**Settings page**: fourth leaf `workspaces-overview` under the `workspaces` drill-in parent
(`settingscontroller_pageregistration.cpp:118-133`), QML
`src/settings/qml/pages/workspaces/WorkspacesOverviewPage.qml`, house patterns (no page-level
InlineMessage banners). The page id `overview` is TAKEN by the monitor dashboard (`:87`).

---

## 3. Phases (each builds, `ctest --test-dir build --output-on-failure` green, usable before the next; a phase is not done with a red or skipped suite)

1. **Engine read surfaces + daemon model + interface.** `IOverviewModelSource`; scroll
   `stripSnapshot(key)` overload with `viewX`/absolute rects; snap and autotile per-key rect reads;
   `OverviewAdaptor` (unconditional) + XML + `ServiceConstants`; `OverviewController` model
   builder with fakes; publish/replay/generation; `setOverviewOpen` gate and service watcher.
   Tests: `tests/unit/daemon/test_overview_controller.cpp` (three screens × mixed modes via fake
   sources, virtual-screen offsetting, generation stamping, change gating, no emission while
   closed, watcher reset); `libs/phosphor-scroll-engine/tests/test_scrollengine_snapshot.cpp`
   extended for the key overload (non-current context equals the same context made current, parked
   columns at visual positions, no focus/anchor mutation, `nullopt` for stashed/never-created
   keys); NEW `libs/phosphor-scroll-engine/tests/test_scroll_engine_workspaces.cpp` (LGPL; closes
   the gap that autotile and snap already cover: reap/renumber of a key holding a panned
   non-current strip); snap and autotile per-key read tests in their own trees.
2. **Effect plugin, view only.** CMake target + embedded QML module (verify `loadFromModule`
   once), `OverviewEffect`, `daemonclient`, column of cells with `WindowThumbnail`s, backdrop,
   dock layer, shadow, labels, progress animation, shutdown timer, gesture, Escape. Main-effect
   `activeFullScreenEffect` gate. Shortcut recipe (all files in §2.4) + stock steal +
   `overview.cpp` TU. Live check in the nested harness: `dbus-run-session -- kwin_wayland --virtual
   --output-count 2 --socket pztest --no-lockscreen --no-global-shortcuts --no-kactivities` with
   isolated `XDG_CONFIG_HOME/DATA/CACHE/STATE`, `QT_QPA_PLATFORM=wayland`,
   `QT_PLUGIN_PATH=<repo>/build/bin` prepended so KWin loads BOTH locally built plugins; effect
   logs land in `journalctl --user`. A rebuilt `.so` is not reloaded by `unloadEffect/loadEffect`
   in a real session; only the harness or a logout picks it up.
3. **Navigation + window moves (same-mode, scrolling and snapping targets).** Click-to-activate
   close sequence, wheel, keyboard, jump digits; `HandoffContext::insertTileIndex`; controller
   by-id verbs (`focusWorkspaceById`, `moveWindowToWorkspaceById`, public `canonicalScreenId`);
   insert-position resolver; drag within a screen and across screens; `requestInsertWorkspace` +
   reserve tag; reflow hold. Tests: resolver cases mirroring niri (above first, inside cell, in gap,
   below last, new workspace always `NewColumn(0)`, floating stays floating, tile → in-column
   index, tabbed column joins); scroll `handoffReceive` with `insertTileIndex`
   (`libs/phosphor-scroll-engine/tests`); snap zone-from-`dropPos` and no-zone-free arm
   (`libs/phosphor-snap-engine/tests`); sticky refusal; trailing-empty reuse vs gap insert and the
   reserve-against-debounce (`workspacereconcilerharness.h`); mutation test that a no-position-change
   drop issues no verb.
   **3b. Tiling target** as its own commit: immediate `handoffReceive` with `toDesktop`,
   `insertIndexForPoint(key, pos)`, model reports the post-receive rect. Tests in
   `tests/unit/autotile/engine/`.
4. **Strip management.** `pz-workspace` drag → `reorderWorkspace` / `moveWorkspaceToScreen` (new
   reconciler by-id verbs + shared rider helper + declaration rewrite for named/pinned), rename
   (F2 + label click), pin/unpin through `ISettings`, middle-click/Delete close, right-drag and
   horizontal-wheel pan (`panStoredView` for non-current), last-workspace guard, removal reflow
   hold. Tests: reconciler harness reorder-by-id / transfer-by-id keep `maintainScreen` and bump
   generation; named-declaration rewrite round trip; `panStoredView` then make current → first
   relayout's view equals the panned value; refuse transfer that would empty a slice.
5. **Settings + polish.** `Workspaces.Overview` group (all steps in §2.4 including the sweep list),
   page, cheatsheet entry, `docs/overview.md` feature note in the style of
   `docs/dynamic-workspaces.md` (user-facing policy only, plain prose, no architecture),
   CHANGELOG entry, translations (`cmake --build build --target update-ts`). Final full ctest, no
   new warnings, live acceptance pass.

Build: `cmake -B build -DBUILD_TESTING=ON -DBUILD_PHOSPHOR_SHELL=ON && cmake --build build
--parallel 6 && ctest --test-dir build --output-on-failure` (parallelism cap 6; `glslang` on PATH).

---

## 4. Invariants and traps (each is a test or a header comment, not a hope)

- The daemon is the sole writer of the workspace map. The plugin never calls KWin's
  `createDesktop`/`removeDesktop`/`setDesktopName`/`Workspace.moveDesktop` and never writes
  `window.desktops` from QML; every mutation is a verb to the daemon driving the reconciler's
  ledger. `Workspace.moveDesktop` is available to the QML and MUST NOT be used.
- Screen ids sent to the daemon pass `canonicalScreenId`; the plugin computes ids with the
  `outputScreenId` rule including the `/connector` suffix (a connector-keyed map is the bug that
  segfaulted plasmashell twice). `DesktopBackground.outputName` is the exception: it wants
  `LogicalOutput::name()` (connector), so use `SceneView.screen.name` there.
- Desktop identity is the UUID string; the live 1-based int is only for `effects->desktops()`
  addressing, resolved by `x11DesktopNumber()` never by list position (`daemon_apply.cpp:73-93`).
- Generation rules per §2.1; bounded retry as `wiring.cpp:605-640`.
- No `isOnCurrentDesktop()` in the plugin. It is per the WINDOW's own output's current desktop
  (`window.cpp:812-815`), which is the wrong predicate for arbitrary (screen, desktop) pairs.
- `grabKeyboard` returns false when another effect holds the grab; `QuickSceneEffect` refuses to
  start then. Never call `ungrabKeyboard` unless your grab succeeded.
- Sticky and multi-desktop windows: shown once on their screen's current workspace, not
  draggable (verb refused daemon-side, tile springs back).
- Minimized windows render normally in `WindowThumbnail`; the dimming is the tile's own opacity.
  Snap frees a minimized window's zone (`daemon_apply.cpp:407-419`), so its engine record is a
  float with a stale frame; report it at that frame.
- Output added or removed while open: `deactivateNow()`; fostering happens in the daemon and the
  next open renders it. Daemon restart while open: client closes and re-sends nothing until the
  service reappears. Effect crash while open: the daemon's service watcher resets the open state.
- Never stream the model while closed (test).
- The `WorkspaceController` teardown path (`workspaces.cpp:952-993`) detaches the controller from
  the adaptor and emits `closeOverviewRequested`.
- User-facing strings: plain prose, `PhosphorI18n::tr()` in C++, `i18nd("plasmazones", …)` in this
  plugin's QML, `i18n()` in the settings page QML, no em-dashes, no clause-splicing semicolons.
- File ceiling: 1000 target, 1150 hard, for every NEW file; `workspaces.cpp` (995) and
  `plasmazoneseffect.h` (3765, grandfathered) must not grow.
- Licences: `kwin-effect/**`, `src/**` and `tests/**` are `GPL-3.0-or-later`; edits and tests
  under `libs/phosphor-engine/**`, `libs/phosphor-scroll-engine/**`, `libs/phosphor-snap-engine/**`,
  `libs/phosphor-tiles/**`, `libs/phosphor-workspaces/**` are `LGPL-2.1-or-later`.

---

## 5. Acceptance (all must hold in the nested harness before you call it done)

1. The configured chord opens the overview on every monitor at once; each shows only its own
   workspaces in slice order, trailing empty included, named ones labelled, current highlighted;
   backdrop plus one dimmed wallpaper per screen; docks fade out. Escape closes.
2. Four-finger swipe up tracks the zoom continuously and settles open or closed on release.
3. Left click on a window on another workspace of the same monitor focuses it and lands on that
   workspace with one animated zoom-in and column scroll; no `desktop.switch` shader blend fires.
4. Dragging a window from a scrolling workspace into the gap below the last workspace creates a
   new workspace with it as the only column; the trailing empty is reused where niri reuses it;
   the source evaporates after the debounce if it became empty and was not named, without a
   visible lurch.
5. Snapping → scrolling drop inserts a new column at the drop position; dropping onto an existing
   tile stacks into that column; onto a tabbed column joins its tabs. Scrolling → snapping drop
   snaps into the zone under the pointer, or leaves the window free at its live frame when no zone
   is there. Dropping onto a tiling workspace tiles it at the slot under the pointer and the tile
   stays visible in the overview.
6. Dragging a workspace label above another reorders the slice; onto another monitor's column
   transfers it with its windows; a pinned workspace's declaration follows.
7. F2 renames; pin makes it a named workspace that survives being emptied; unpin reverts; the
   settings page shows the same declaration.
8. Right-drag on a non-current scrolling workspace pans its strip; focusing it later shows the pan.
9. A refused drop (sticky window, unknown target) springs back with no state change.
10. Every ctest passes; no new build warnings; every new file has an SPDX header and is under the
    ceiling.

---

## 6. KWin 6.7.4 behaviour verified by reading the source (authoritative over the research files)

1. **`EffectTogglableState` has no timeline.** `activate()`/`deactivate()` set
   `partialActivationFactor` to 1/0 and the status immediately (`effecttogglablestate.cpp:44-64`);
   `partialActivate/Deactivate` set the raw gesture factor (`:94-110`); gesture release snaps at
   0.5 (`:19-40`). There is no `setAnimationDuration` on it or on `QuickSceneEffect`; KWin's
   `animationDuration` is `OverviewEffect`'s own property (`overvieweffect.h:20,43-44,94`) and the
   QML animates with `NumberAnimation` (`Main.qml:100-126`). Shutdown timer:
   `overvieweffect.cpp:129-134, 309-314`.
2. **Filter order.** `InputFilterOrder`: `… TabBox, GlobalShortcut, Effects, InteractiveMoveResize …`
   (`input.h:348-374`); `GlobalShortcutFilter::keyboardKey` consumes matched shortcuts
   (`input.cpp:1075-1086`) before `EffectsFilter::keyboardKey` hands everything else to
   `grabbedKeyboardEvent` and returns true (`input.cpp:560-579`). Unhandled keys cannot be
   forwarded anywhere. Keyboard-shortcut inhibition and the lock screen filter sit above both.
3. **Mouse interception consumes every pointer event** (`effecthandler.cpp:534-565`), and
   `QuickSceneEffect::startInternal` always calls `startMouseInterception`
   (`quickeffect.cpp:564`). Effects have no layer-shell layer accessor; docks are detectable via
   `isDock()` + `frameGeometry()` but cannot receive the event. KWin's Overview draws docks as
   fading thumbnails (`Main.qml:813-830`).
4. **`DesktopBackground`** picks the Desktop-type window on `outputName` (connector name via
   `workspace()->findOutput`) for the desktop/activity (`desktopbackgrounditem.cpp:38-41, 85-122`);
   Plasma's desktop window is on all desktops, so every cell of a screen would get the same
   wallpaper. It does not track `windowAdded`.
5. **`setCurrentDesktop(desktop, output)` emits only `desktopChanged`** (`virtualdesktops.cpp:577-601`);
   `desktopChanging` with an offset fires only from the touchpad swipe lambda (`:782-792`).
   `desktopChanged` is emitted synchronously inside `setCurrent`.
6. **`QuickSceneEffect` painting.** `prePaintScreen` only ORs `PAINT_SCREEN_TRANSFORMED` and
   updates dirty views (`quickeffect.cpp:435-449`); no `paintScreen`/`paintWindow` override. The
   view is an opaque `SurfaceItem` on `overlayItem()` (`offscreenquickview.cpp:120-148, 196-198`);
   `paintGenericScreen` paints every window bottom-to-top with no occlusion culling, then the
   overlay (`workspacescene.cpp:709-724`), with full-device damage every frame (`:644-649`).
   `blocksDirectScanout()` is false. Other effects' hooks still run (`effecthandler.cpp:371-408,
   423-438`).
7. **Pointer routing.** `pointerMotion/Button/Axis` resolve the view with `viewAt`, latch an
   implicit grab on the pressed view until all buttons are up, and forward
   (`quickeffect.cpp:652-734`); touch picks per event (`:771-807`).
   `checkItemDraggedOutOfScreen` emits every OTHER screen's view the item rect intersects
   (`:213-225`); `checkItemDroppedOutOfScreen` emits the first other screen containing the point
   (`:227-236`). `WindowHeapDelegate.qml:304-329` calls `Drag.drop()` first.
8. **`WindowThumbnail`.** Renders on `preFrameRender` when dirty (`windowthumbnailitem.cpp:45-54,
   111-152`) at `visibleGeometry().size() × dpr`, root `WindowItem` rendered regardless of its
   visibility (`itemrenderer_opengl.cpp:365-386`), opacity baked (`windowitem.cpp:59-60,288-290`),
   aspect-fit centred with the shadow offset outside bounds (`:390-412`), GL only (`:35-39`).
   Sources are shared per `(QQuickWindow, Window)`.
9. **`activeFullScreenEffect()`** is public with `activeFullScreenEffectChanged()`
   (`effecthandler.h:455-456, 1041`); `startInternal` refuses while another holds it
   (`quickeffect.cpp:525-527`). Two effects may share chain position 70 (`effect_order` is a
   `QMultiMap`).
10. **All API names used above exist in 6.7.4**: `screens()`, `LogicalOutput`,
    `x11DesktopNumber()`, `screenAboutToLock`, `stackingOrder()`, `findWindow(QUuid)`,
    `currentDesktop(output)`, `setCurrentDesktop(desktop, output)`, `qmlEngine()`,
    `EffectTogglableGesture::addTouchpadSwipeGesture/addTouchscreenSwipeGesture`.

---

## 7. Corrections to the seam reports (apply these over `seams-*.md`)

- `moveWindowToWorkspaceVerb` is a `WindowTrackingAdaptor` method (`crossmode.cpp:61`,
  `windowtrackingadaptor.h:1601`), reached from the controller's `windowWorkspaceMoveRequested`
  via `workspaces.cpp:583-591`; not a controller method.
- `watchWindowMove` (`workspacecontroller.h:282`), `canonicalScreenId` (`:196`) and
  `switchScreenToDesktop` (`:229`) are PRIVATE today; §2.1 adds the public by-id verbs.
- `reorderWithinSlice`/`transfer` are `WorkspaceMap` methods; the reconciler exposes only the
  `*CurrentWorkspace` pair; by-id twins are new.
- `ScrollEngine::stripSnapshot(screenId, excludeWindowId)` already exists; the overview adds a
  key overload, not a new `stripSnapshotFor`. `ScrollEngine.h:547` is a comment on
  `blueprintProgressForScreen`, not the `visibleStripJson` gate; that gate is
  `isActiveOnScreen` in the adaptor at `scrollingadaptor.cpp:432`, with current-context selection
  inside `currentKeyForScreen`.
- Non-current scrolling contexts may be STASHED (`stashStripStructure`/`restoreFromStripStash`,
  `ScrollEngine.h:1128-1160`) or never created; hence the `nullopt` arm in `IOverviewModelSource`.
- Snap and autotile have NO public per-key rect query today; Phase 1 adds them.
- The shortcut recipe spans the files listed in §2.4, not two; the table is
  `shortcutmanager_table.cpp`, files are under `src/daemon/controllers/`.
- `activateWindow` exists only as the adaptor signal `activateWindowRequested`
  (`windowtrackingadaptor.h:1420`); the plugin activates directly.
- `PhosphorCompositor::DaemonClient` already exists; the plugin's client lives in its own namespace.
