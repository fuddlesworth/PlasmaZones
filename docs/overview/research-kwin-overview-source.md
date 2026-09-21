<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# KWin 6.7.4 Overview effect: source-level findings

Source: sparse clone of `https://invent.kde.org/plasma/kwin.git` tag `v6.7.4` at
`scratchpad/kwin/` (paths `src/plugins/overview`, `src/plugins/private`, `src/effect`,
`src/scripting`). Installed headers checked at `/usr/include/kwin/effect/` and installed QML
at `/usr/lib/qt6/qml/org/kde/kwin/`.

## Headline: the QuickSceneEffect route is OPEN, not closed

The codebase seam report (`seams-rendering.md` §1) says the "OffscreenQuickScene + WindowThumbnail
QML approach is no longer available" citing `snapassistthumbnailcapture.h:38-42`. That comment is
about one specific API: `OffscreenQuickView::bufferAsImage()` (CPU readback of a QML scene to a
QImage) was removed in 6.7. It says nothing about rendering QML *into the compositor scene*, which is
exactly what KWin's own Overview does and which is fully public in 6.7.4:

| Header (installed) | What it gives |
|---|---|
| `effect/quickeffect.h` | `KWin::QuickSceneEffect : Effect` (public, exported). One `QuickSceneView` (an `OffscreenQuickView` in `ExportMode::Texture`) per output, delegate component instantiated per screen, `viewForScreen`, `viewAt`, `activateView`, view caching, `itemDraggedOutOfScreen` / `itemDroppedOutOfScreen` cross-screen DnD signals, `checkItemDraggedOutOfScreen` / `checkItemDroppedOutOfScreen`. |
| `effect/offscreenquickview.h` | `OffscreenQuickView` / `OffscreenQuickScene` (public). Usable directly from an ordinary `OffscreenEffect` too. |
| `effect/effecttogglablestate.h` | `EffectTogglableState` (Inactive/Activating/Deactivating/Active/Stopped, `partialActivationFactor`, activate/deactivate/toggle QActions), `EffectTogglableGesture` (`addTouchpadSwipeGesture(dir, fingers)`, `addTouchpadPinchGesture`, `addTouchscreenSwipeGesture`), `EffectTogglableTouchBorder`. |
| `effect/effecthandler.h` | `registerTouchpadPinchShortcut`, `registerTouchscreenSwipeShortcut`, `registerRealtimeTouchBorder`, `grabKeyboard`, `startMouseInterception`, `setActiveFullScreenEffect`, `windowToDesktops`, `windowToScreen`, `currentDesktop(output)`, `setCurrentDesktop(desktop, output)`, `desktops()`, `desktopChanging(desktop, offset, with, output)`, `qmlEngine()`. |

QML types available to any QML loaded through `effects->qmlEngine()` (registered in-process by
`src/scripting/scripting.cpp:693-719` under `import org.kde.kwin`):
`WindowThumbnail` (`wId`/`client`), `DesktopBackground`, `WindowModel`, `WindowFilterModel`
(`activity`, `desktop`, `screenName`, `windowType` mask, `minimizedWindows`, `filter`),
`VirtualDesktopModel` (`create(position)`, `remove(index)`, `maximum`), `SceneView` (uncreatable
attached: `.effect`, `.screen`, `.currentDesktop`), `Workspace` singleton (`desktops`,
`currentDesktop`, `createDesktop(position, name)`, `removeDesktop(desktop)`,
**`moveDesktop(desktop, position)`** — a desktop REORDER verb the dynamic-workspaces plan believed
KWin lacked; it is scripting-only, not on D-Bus), `SwipeGestureHandler`, `PinchGestureHandler`,
`ShortcutHandler`, `ScreenEdgeHandler`, `DBusCall`.

Also installed and importable: `import org.kde.kwin.private.effects` at
`/usr/lib/qt6/qml/org/kde/kwin/private/effects/` providing `WindowHeap`, `WindowHeapDelegate`,
`ExpoLayout`, `ExpoArea` (libeffectsplugin.so). "private" is a naming convention, not an access
gate; Overview imports it from a separate module (`org.kde.kwin.overview`).

## How WindowThumbnail renders (src/scripting/windowthumbnailitem.cpp)

`WindowThumbnailSource` connects `Window::damaged` and `WorkspaceScene::preFrameRender` → `update()`
(`:49-54`), which renders `m_handle->windowItem()` into an offscreen render target via
`renderer->renderItem(offscreenRenderTarget, offscreenViewport, windowItem, mask, Region::infinite(),
WindowPaintData{}, {}, {})` (`:143`). Consequences:
- Live: re-rendered every frame the window is damaged. Not a snapshot.
- Desktop-agnostic: renders the window item regardless of `isOnCurrentDesktop()` or minimized state
  (Overview shows other desktops and minimized windows with it).
- Scaled by the QQuickItem's own scale/size, so arbitrary zoom with correct cropping — the exact
  capability `windowanimator.cpp:481-495` records `WindowPaintData` cannot provide.
- Cost: one offscreen render per damaged window per frame while the effect runs; this is what KWin
  Overview pays today.

## How the Overview effect is wired (overvieweffect.cpp, 347 lines)

- Three `EffectTogglableState`s: `m_overviewState` (inactive→overview), `m_transitionState`
  (overview→grid), `m_gridState` (inactive→grid). Each has an `EffectTogglableGesture`:
  4-finger touchpad swipe up / 3-finger touchscreen swipe up for overview and transition, swipe down
  for grid. Status-change lambdas keep the three mutually consistent (`:49-103`).
- Partial activation factors are exposed as Q_PROPERTYs and the QML derives everything from
  `overviewVal`/`gridVal` (Main.qml `:74-111`), states `initial|overview|grid|partialOverview|
  partialGrid|transition`.
- Per-output desktop switch offsets: `effects->desktopChanging(old, offset, with, output)` and
  `desktopChanged(..., output)` fill `m_screenDesktopOffsets[output]`; QML reads
  `effect.desktopOffsetForScreen(targetScreen)` so a live per-output slide shows inside the
  overview (`:105-131`, Main.qml `:36, 489-496`).
- Shortcuts: `KGlobalAccel::self()->setGlobalShortcut(m_overviewState->toggleAction(), Meta+W)`,
  `Meta+G` grid, plus Cycle / Cycle Opposite. Electric borders via `reserveElectricBorder`
  (`BorderActivate` default top-left), touch borders via `EffectTogglableTouchBorder`.
- `requestedEffectChainPosition() == 70`. Delegate loaded from module
  `org.kde.kwin.overview` `Main` asynchronously. `screenAboutToLock → deactivateNow`.
- `QuickSceneEffect::startInternal` (`quickeffect.cpp:560-566`) does `grabKeyboard`,
  `startMouseInterception(Qt::ArrowCursor)`, `setActiveFullScreenEffect(this)`, installs a
  qApp event filter for cursor shape, creates one view per screen (or revives cached views), and
  `activateView(activeView())`. `stopInternal` reverses all of it and `addRepaintFull()`.

## Main.qml (861 lines) mechanics worth copying

- Per-screen root: `effect = SceneView.effect`, `targetScreen = SceneView.screen`,
  `currentDesktop = SceneView.currentDesktop` (`:24-36`).
- Desktop grid: `Repeater` over a `VirtualDesktopModel`; each cell has `DesktopBackground`
  (`outputName: targetScreen.name`), a `WindowHeap` whose model is
  `WindowFilterModel { activity, desktop, screenName: targetScreen.name, windowType: ~Dock &
  ~Desktop & ~Notification & ~CriticalNotification }` (`:683-693`), and a `DropArea` (`:594-616`):
  `drop.keys.includes("kwin-desktop")` → `Workspace.moveDesktop(drag.source.desktop, desktop.x11DesktopNumber - 1)`
  (reorder), else `drag.source.desktops = [mainBackground.desktop]` (move window).
- Drag payload: `WindowHeapDelegate` sets `Drag.keys: ["kwin-window"]`, `Drag.source: thumb.window`,
  `Drag.proposedAction: Qt.MoveAction`; `onX/YChanged → effect.checkItemDraggedOutOfScreen`,
  release → `Drag.drop()` then `effect.checkItemDroppedOutOfScreen(globalPos, thumbSource)`
  (`WindowHeapDelegate.qml:135-144, 290-324`). Cross-screen drop lands in
  `onItemDroppedOutOfScreen(pos, item, screen)` in the target screen's view (Main.qml `:620-635`).
- Right click on a thumbnail toggles on-all-desktops (`window.desktops = []` vs `[desktop]`,
  `:735-737`); middle click `closeWindow()`; touch flick-down closes with `downGestureProgress`.
- Wheel steps desktops by 120-unit notches, honouring `virtualDesktopNavigationWrapsAround`
  (`:239-290`). Keyboard: arrows via `WindowHeap.selectNextItem(Direction)`, `+`/`=` create,
  `-` remove last, `Delete` remove focused, F2 rename, Escape close.
- `DesktopBar.qml` (310 lines): `Flickable` of `DesktopView` thumbnails at `gridUnit*5` tall scaled
  `desktopHeight / targetScreen.geometry.height`, hover-revealed delete, in-place rename TextField,
  trailing add button gated on `desktopModel.maximum`, per-thumbnail `DropArea` writing
  `drag.source.desktops = [delegate.desktop]`.
- Dock windows are drawn separately from a `WindowFilterModel { windowType: Dock }` so panels keep
  their place (`:814-818`).

## Constraints for PlasmaZones

1. **One `Effect` per plugin.** `KWIN_EFFECT_FACTORY*` macros (`effect.h:1157-1201`) create exactly
   one `Effect` via `createEffect()`. `PlasmaZonesEffect` is an `OffscreenEffect`; it cannot also be
   a `QuickSceneEffect`. Two viable shapes: (a) a second plugin target beside
   `kwin_effect_plasmazones` (e.g. `kwin_effect_plasmazones_overview`) that is a `QuickSceneEffect`
   subclass, talking to the daemon over D-Bus like the main effect does; (b) the existing effect
   owns an `OffscreenQuickScene` per output directly and paints/injects input itself, reproducing
   the relevant parts of `QuickSceneEffectPrivate`. (a) is what KWin itself does and keeps the
   1000-line ceiling honest; (b) shares the effect's caches (workspace map, strip geometry,
   decorations) without IPC. Both need deciding in the plan.
2. **`isOnCurrentDesktop` is global, not per-output**, in `EffectWindow` (see
   `seams-rendering.md` §8). `WindowFilterModel.desktop` filters by membership, which is what the
   overview wants anyway.
3. **`Workspace.moveDesktop` exists only in the scripting/QML wrapper**, not on
   `org.kde.KWin.VirtualDesktopManager` D-Bus. A QML overview inside KWin can reorder desktops; the
   daemon cannot. If reorder-by-drag is in scope, the reorder must be issued from the effect side and
   the daemon's `WorkspaceReconciler` must treat the resulting `desktopListSettled` as an external
   reorder (its "insert-correct + opportunistic repair" contiguity policy already assumes KWin can
   reorder underneath it).
4. **Mipmapping / thin-border jank at small zoom** is not solved by WindowThumbnail either; niri
   issues #1467/#1470 apply equally. `WindowThumbnailItem` samples the offscreen texture with the
   scene graph's default filtering (linear, no mipmaps).
5. **Keyboard grab semantics**: while a `QuickSceneEffect` runs, ALL keyboard goes to the QML view
   (`grabbedKeyboardEvent → activeView()`), so niri's "all normal bindings keep working" needs
   the overview QML to forward unhandled keys to the daemon's verb dispatcher (or to not grab and
   rely on KGlobalAccel, which still fires under a grab since it sits above the Effects filter).
