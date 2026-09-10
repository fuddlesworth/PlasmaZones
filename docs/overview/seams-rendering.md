<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# Overview rendering/input seams — reference report

Worktree: /home/nlavender/Projects/PlasmaZones/.claude/worktrees/dynamic-workspaces

## 1. Effect paint pipeline

- `PlasmaZonesEffect : public KWin::OffscreenEffect` — `kwin-effect/plasmazoneseffect/plasmazoneseffect.h:160`, ctor `lifecycle.cpp:55`. Not QuickSceneEffect; no QML scene inside the effect.
- Overrides declared `plasmazoneseffect.h:195-240`: `prePaintScreen`, `paintScreen`, `prePaintWindow`, `paintWindow`, `drawWindow`, `apply` (quad deformation), `blocksDirectScanout`, `isActive`. `apply()` implemented `paint_capture.cpp:557` — replaces the window quad with an output-spanning quad for `fboExtent: "surface"` packs.
- Per-window transform today is **translate only, no scale**: `WindowAnimator::applyTransform`, `kwin-effect/compositor/windowanimator.cpp:468-499`, does `data += (desiredPos - actualPos)`. Comment at :481-495 records that `setXScale/setYScale` was deliberately REMOVED (discussion #868) because `WindowPaintData` offers no crop, so scaled content stretched then settled. Declaration `compositor/windowanimator.h:204`.
- Strip view offset applied per-window in paint: `paint_pipeline.cpp:1798-1800` (`animatedFrame.translate(m_stripViewAnimator->offsetFor(scrollOut))`); `scroll_clip.cpp:257,264` translate the visual rect by the view offset before the boundary clip.
- Offscreen render of ONE window into an FBO exists at three sites, all the identical shape: `GLTexture::allocate` -> `GLFramebuffer` -> `RenderTarget`/`RenderViewport` -> `KWin::ItemEffect keepRenderable(w->windowItem())` -> `effects->drawWindow(..., PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_TRANSLUCENT, Region::infinite(), data)`:
  - `compositor/snapassistthumbnailcapture.cpp:390-440` (readback path) and `:545-600` (dma-buf export, scratch->export blit-flip)
  - `plasmazoneseffect/paint_capture.cpp:350-383` (old-content snapshot for cross-fades)
  - `plasmazoneseffect/surface_capture.cpp:224-250`
- Full-screen SCENE capture into an FBO: `transitions/desktoptransitioncapture.cpp:41` `DesktopTransitionManager::captureDesktop`; re-enters `KWin::effects->paintScreen(renderTarget, viewport, mask, walkRegion, screen)` with a pushed framebuffer at `:291` and `:390`. This is the existing mechanism for getting a non-current desktop's scene into a texture.
- `redirect()` / `setShader()` per-window offscreen path: `decoration_render.cpp:220-221`; teardown `decoration_teardown.cpp:205-206`.
- **No `OffscreenQuickView`, no `WindowThumbnailItem`, no `EffectFrame` anywhere in the tree.** `snapassistthumbnailcapture.h:38-42` states KWin 6.7 removed `OffscreenQuickView::bufferAsImage()` and `update()` now needs an `OutputFrame`, so the earlier `OffscreenQuickScene + WindowThumbnail` QML approach is no longer available; the direct GLFramebuffer render replaced it.
- Arbitrary-texture quad drawing inside the effect already exists for the tab pills: `compositor/scrolltabindicatorpainter.cpp:333` (`GLTexture::upload(QImage)` from a QPainter raster), `:452-486` (`MapTexture | TransformColorspace` shader, `texture->render(clipRegion, quadSize, hardwareClipping=true)`).
- Non-current-desktop windows are uniformly excluded from paint: `isOnCurrentDesktop()` gates at `paint_pipeline.cpp:233, 441, 679, 859`, `surface_gating.cpp:75,150`. The ONLY two-desktop paint is the desktop-switch blend, and it blends two captured TEXTURES rather than walking off-desktop windows: `transitions/desktoptransitionmanager.h:36-60`.

## 2. Input

- Mouse interception: `KWin::effects->startMouseInterception(m_effect, Qt::PointingHandCursor)` / `stopMouseInterception` at `kwin-effect/tilinghandler/scrolltabs.cpp:1154-1156`, held only while the pointer is over a tab pill. Ordering note at `scrolltabs.cpp:860`.
- Effect pointer hooks: `pointerMotion`, `pointerButton`, `pointerAxis` overrides at `plasmazoneseffect.h:213-241`, with a long contract comment: KWin consumes EVERY pointer event for the effect while interception is held, so right/middle presses over a pill reach nothing (accepted gap); a wheel tick is routed through `pointerAxis` explicitly because the Effects filter sits below KWin's global-shortcut filter.
- `windowInputMouseEvent` is NOT overridden anywhere.
- Keyboard: `effects->grabKeyboard(this)` at `lifecycle_wiring_drag.cpp:198, 320, 343` and `drag_snap.cpp:1133`; `ungrabKeyboard()` at `lifecycle_wiring_drag.cpp:235, 395`, `lifecycle.cpp:468`, `window_lifecycle.cpp:320`. Handler `grabbedKeyboardEvent` (`plasmazoneseffect.h:211`). Grab policy travels over D-Bus as `DragPolicy::grabKeyboard`, `libs/phosphor-protocol/include/PhosphorProtocol/DragTypes.h:79`, set at `src/dbus/windowdragadaptor/drag_protocol.cpp:84,113,152`.
- Private-API input filter exists: `ScrollOverhangInputFilter : KWin::InputEventFilter`, `plasmazoneseffect/input_filter.h:77`, installed `input_filter.cpp:22-25` at `KWin::InputFilterOrder::Popup`. Overrides pointerMotion/Button/Axis + touchDown/Motion/Up/Cancel. Docstring `input_filter.h:20-77` documents weight relative to Decoration / WindowAction / Effects filters, and names two gaps: pointer FOCUS (enter/leave, focus-follows-mouse) cannot be retargeted by any filter, and tablet events are deliberately unhandled.
- Fullscreen claim: `effects->setActiveFullScreenEffect(m_effect)` `transitions/desktoptransitionmanager.cpp:218`; released `desktoptransitionteardown.cpp:53`. Peek path deliberately never claims (`desktoptransitionmanager.cpp:466-467`) because the setter itself cancels show-desktop on every null<->non-null transition.
- **No touchpad/touchscreen gesture registration at all.** Zero hits for `registerTouchpadSwipeShortcut`, `registerRealtimeTouchpadSwipeShortcut`, `registerTouchscreenSwipeShortcut`. Only `Gesture` symbol is `TilingHandler::reconcileMaximizeAfterGesture` (`tilinghandler/windowedfullscreen.cpp:270`, decl `tilinghandler.h:468`), unrelated.
- Interactive move/resize tracking with the daemon: `handlers/dragtracker.cpp:30,62` (`isUserMove()/isUserResize()`); `windowStartUserMovedResized` wiring `lifecycle_wiring_drag.cpp:59`; `windowFinishUserMovedResized` connect `tilinghandler/outputchange.cpp:670`; drag-end generation guard via `Window::interactiveMoveResizeCount()` at `drag_end.cpp:139, 208`.

## 3. phosphor-compositor / phosphor-rendering

- `libs/phosphor-compositor` is NOT a rendering library. Public headers: `ICompositorBridge.h`, `IDragHandler.h`, `IGeometryHandler.h`, `ILifecycleHandler.h`, `DaemonClient.h`, `DecorationManager.h`, `DecorationDefaults.h`, `FloatingCache.h`, `TilingState.h`, `ZoneCache.h`, `SnapAssistFilter.h`, `TriggerParser.h`, `GeometryHelpers.h`, `DebouncedAction.h`. It abstracts the compositor as a window/geometry service. `KWinCompositorBridge::isOnCurrentDesktop` `compositor/compositorbridge.cpp:120`; window info populated `:182` (`info.isOnCurrentDesktop`).
- `libs/phosphor-rendering` is the Qt-RHI / QQuick side, not KWin GL. `ShaderNodeRhi.h` builds on `QSGRenderNode`, `QQuickItem`, `QSGTextureProvider`, `rhi/qrhi.h`. Shader ABI binding map `ShaderNodeRhi.h:41-60`: binding 0 UBO, 2-5 multipass buffers, 6 audio, 7-10 user textures, 11 wallpaper, 12 depth; binding 1 free (Phosphor uses it for zone labels); 13..31 free (`kMaxConsumerBinding` = 31, Qt RHI minimum guarantee). Also `ZoneShaderNodeRhi.h`, `ZoneLabelTexture.h`, `ShaderCompiler.h`, `ShaderEffect.h`, `ShaderNodeLiveness.h`.
- Consequence: the RHI grid lives in QML surfaces; the KWin GL path lives in the effect. There is NO existing helper that draws a KWin window texture into a scaled quad at an arbitrary position. Nearest primitives are the FBO capture sites in section 1 plus `GLTexture::render(region, quadSize, hardwareClipping)` as used by the tab painter (`scrolltabindicatorpainter.cpp:486`).

## 4. Overlays / full-screen QML

- All passive overlays are layer-shell surfaces owned by the DAEMON, not in-effect. Roles in `src/daemon/overlayservice/phosphor_roles.h`: `ZoneOverlay` (:33), `ZoneSelector` (:42), `Osd` (:59), and the unified `PassiveShell` (:66+) — one wlr-layer-shell surface per screen, Overlay layer, AnchorAll, click-through, keyboard `None` at attach.
- Content are QML `Item` slots inside that shell: `src/ui/PassiveOverlayShell.qml`, `PassiveOverlayModalSlots.qml`, plus `ZoneOverlayContent.qml`, `ZoneSelectorContent.qml`, `LayoutPickerContent.qml`, `SnapAssistContent.qml`, `ScrollDropIndicatorContent.qml`, `CheatsheetContent.qml`, `RenderNodeOverlayContent.qml`, `ZoneItem.qml`, `ZoneSelectorStripCard.qml`.
- Keyboard CAN be raised on a live surface: `ShellHost::syncSurfaceState` promotes to `KeyboardInteractivity::Exclusive` while a slot that has to type is up, dropping back to `None` on the first edge of dismissal — `libs/phosphor-overlay/src/shellhost.cpp:284`, described `phosphor_roles.h:70-74`. Today only the cheatsheet search field uses it. Enum `libs/phosphor-layer/include/PhosphorLayer/Role.h:51-54` (None / Exclusive / OnDemand).
- Overlay-vs-Top layer choice is load-bearing: `phosphor_roles.h:88-97` — on Top, KWin stacks the shell in AboveLayer alongside keep-above toplevels, which put a floated window in front of the OSD.
- Anchors/keyboard/exclusive-zone plumbing: `PhosphorLayer/Role.h` (`Anchor`, `Anchors`, `withExclusiveZone`, `withKeyboard`), `ILayerShellTransport.h:79-80` (`setExclusiveZone`, `setKeyboardInteractivity`), `SurfaceConfig.h:90,148,152` (`keyboardOverride`, `effectiveExclusiveZone`, `effectiveKeyboard`). Other surface infra: `SurfaceFactory.h`, `ScreenSurfaceRegistry.h`, `TopologyCoordinator.h`, `ISurfaceAnimator.h`, `ISurfaceStore.h`, `IQmlEngineProvider.h`, `IScreenProvider.h`.
- No `setInputRegion` / `inputRegion` accessor is exposed in phosphor-layer's public headers.
- A separate dedicated full-screen `Window` still exists for the editor's shader preview: `src/ui/RenderNodeOverlay.qml:27`, rationale :10-24 (its own surface to avoid multipass clear interference with the live overlay's render pass; the editor writes per-frame properties to a single QQuickWindow).
- `src/shell/` is the Phosphor shell process (bar, control center, launcher, popout transports). Full-screen overlay routing: `src/shell/layerpopouttransport.cpp:362-380` — a full-screen overlay MUST set `exclusiveZone = -1` (`Role::isValid` rejects otherwise, see `LayerPopoutTransport.h:54`), and popout focus maps onto None / Exclusive / OnDemand.
- Daemon virtual-screen anchoring case: `src/daemon/overlayservice/internal.h:169,183` (AnchorTop|AnchorLeft + offset).

## 5. Snap-assist thumbnails

- Produced IN the KWin process, effect-side, by `SnapAssistThumbnailCapture` (`kwin-effect/compositor/snapassistthumbnailcapture.h:69`). No PipeWire, no ScreenShot2, no portal, no second scene-graph pass.
- Two transports, documented `snapassistthumbnailcapture.h:44-67`:
  - DEFAULT zero-copy GPU: the FBO texture is exported as a single-plane dma-buf and the fd shipped via `org.plasmazones.Overlay.setWindowThumbnailDmabuf` (`snapassistthumbnailcapture.cpp:1029`, error handling :1066-1070). Batched one batch per show.  `PLASMAZONES_DMABUF_THUMBNAILS=0` pins the session off.
  - FALLBACK raw pixels: `GLTexture::toImage().flipped(Qt::Vertical)` (`:437`), posted as raw ARGB32 non-premultiplied via `setSnapAssistThumbnail`, one render+readback at a time.
- Default box `DefaultThumbnailSize = QSize(256, 256)` (`snapassistthumbnailcapture.h:85`). Recovery hooks: `onDmabufRejected`, `resetRecentlyPosted` (`:100-112`), plus a daemon-ready re-arm.
- Capture is guarded by `ShaderInternal::ScopedGlState` and an explicit `glDisable(GL_SCISSOR_TEST)` (`:380-389`) so state cannot leak into the next real frame; `m_capturingSnapshot` is deliberately NOT set so decorations stay in the thumbnail.
- This is the closest existing analogue to an overview: live compositor textures of arbitrary unfocused/obscured/minimized windows rendered offscreen and handed to a QML surface.

## 6. phosphor-animation

- `AnimatedValue<T>` — `libs/phosphor-animation/include/PhosphorAnimation/AnimatedValue.h`; `retarget(T, RetargetPolicy)` at :104 (rejects non-finite, degrades PreserveVelocity to PreservePosition on stateless curves at :160 and for QTransform at :143), spec-default overload at :199.
- `RetargetPolicy.h:9-16`: `PreserveVelocity` (default), `ResetVelocity`, `PreservePosition`.
- Other headers: `Spring.h`, `PhosphorSpring.h`, `SnapPolicy.h`, `MotionSpec.h`, `Curve.h`, `Easing.h`, `Interpolate.h`, `IMotionClock.h`, `QtQuickClock.h`, `StaggerTimer.h`, `SurfaceAnimator.h`, profile-tree headers.
- **No driven/tracked mode exists.** No API accepts externally-supplied progress or follows a gesture. The idiom for gesture-like following is repeated `retarget(..., PreserveVelocity)`, which is exactly what `StripViewAnimator` does per wheel batch (`compositor/stripviewanimator.h:36-56`: it animates the ABSOLUTE view coordinate, not the offset, precisely so a mid-flight scroll is an ordinary retarget rather than a velocity-seeded start the library does not offer).
- Effect clock: `kwin-effect/compositor/compositorclock.cpp` (samples `std::chrono::steady_clock`, matching KWin's AnimationEffect clock; KWin 6.7 dropped the presentTime parameter, see `plasmazoneseffect.h:191-193`).

## 7. Strip pass / view offset

- `StripViewAnimator` (`compositor/stripviewanimator.h`): one spring per output over the absolute view coordinate; paint offset is `committed - animated`, starting at the batch delta and ringing to zero (:50-56). Damage hook is per-OUTPUT (`setRepaintRequest`, :70-73) because the whole strip moves. `OutputClockResolver` at :67.
- `StripTransitionManager` (`transitions/striptransitionmanager.h:34-80`) is a per-output POST-PROCESS over the live scene, not a from/to blend. Each frame while the spring is live it renders the strip layer (columns already translated by the spring, parked columns relocated, tab pills blitted at the anchor's slot, desktop background beneath, windows stacked ABOVE the strip excluded and composited sharp afterwards — see `PlasmaZonesEffect::m_stripCaptureAboveStrip` and the record-and-return in paintWindow) into a per-output capture, then draws one full-screen quad running the `scrolling.view` pack sampling `uStrip` via `strip_transition.glsl`, driven by `iStripMotion` offset/velocity uniforms.
- Liveness is the SPRING's, not a timer's; settled entries are reaped from `postPaintScreen` (`reapSettled`). A short settle fade (`StripMotionSampler`) outlives the spring.
- Forces composition while live via `PlasmaZonesEffect::blocksDirectScanout`; never runs under a live desktop transition (which freezes the strip visually for its duration, documented and accepted).
- `notifyLeg()` is called from the tiling batch path (`kwin-effect/tilinghandler/tiling.cpp`) BEFORE `applyBatchDelta` for the same output; that ordering is load-bearing for distinguishing a fresh leg from a retarget, a pack swap, and an axis flip (`striptransitionmanager.h:82-120`).
- Ordering inside paintScreen documented at `plasmazoneseffect.h:196-207`: desktop transition first (replaces the scene wholesale), strip pass second, else chain through.

## 8. Per-screen desktop knowledge in the effect

- `KWin::effects->currentDesktop(output)` used at: `handlers/snaphandler.cpp:51`, `plasmazoneseffect/screens.cpp:165`, `window_desktop_connections.cpp:92, 156`, `decoration_rules.cpp:207`.
- `snaphandler.cpp:42-52` records that the global `isOnCurrentDesktop()` both over- and under-fires under per-output desktops; the correct predicate is `w->isOnDesktop(effects->currentDesktop(out))` with a global fallback. Same pattern at `decoration_rules.cpp:207-208`.
- `window_desktop_connections.cpp:83-92`: `EffectWindow::isOnCurrentDesktop()` has NO output overload; KWin exposes only `QHash<LogicalOutput*, VirtualDesktop*>` with no global current desktop.
- Desktop list: `KWin::effects->desktops()` at `daemon_apply.cpp:88, 158`. Lookup by `x11DesktopNumber()` in `desktopByNumber` (`daemon_apply.cpp:83`); the comment at :75 says identity is by that number, NOT by position in the list.
- Per-window desktops: `w->desktops()` at `window_query.cpp:324` and `window_desktop_connections.cpp:32`.
- Desktop-move slots into the effect: `slotWindowDesktopMoveRequested` and `slotWindowDesktopMoveByIdRequested` (`plasmazoneseffect.h`, private slots block ~:300), shared tail `applyDesktopMove` (`daemon_apply.cpp:176`) handling the sticky carve-out and the membership write.
- Dynamic-workspaces ownership map ALREADY streams daemon to effect and is cached but unread: `slotWorkspaceMapChanged` at `daemon_bringup.cpp:974` (payload validation :1005, generation guard :1011-1015), signal wiring `:835`, bringup replay `:289-316` with epoch guards (`m_workspaceMapEpoch`, `m_workspaceMapGeneration`). Its docstring at `plasmazoneseffect.h:315-317` reads verbatim: "Consumer stub: the future overview renders from this; nothing else reads it yet."
- Daemon-side workspace model: `libs/phosphor-workspaces/include/PhosphorWorkspaces/` — `WorkspaceMap.h`, `WorkspaceReconciler.h`, `VirtualDesktopManager.h`, `ActivityManager.h`.

## Which rendering strategy the code is closer to

**(A) in-effect painting with transformed live windows — decisively.** The effect is an `OffscreenEffect` with a mature in-effect GL pipeline: per-window redirect plus shader, quad deformation via `apply()`, per-window translate via `WindowPaintData`, full-screen scene capture into FBOs, a two-texture desktop blend, a per-output strip post-process, and raster-to-`GLTexture` UI drawing for the tab pills.

Route (B) is not merely unused, it is CLOSED: `snapassistthumbnailcapture.h:38-42` documents that KWin 6.7 removed the offscreen-QML readback and that this codebase already migrated AWAY from `OffscreenQuickScene + WindowThumbnail` to direct `GLFramebuffer` renders. No `QuickSceneEffect`, no `WindowThumbnailItem` in the tree.

Two facts complicate a pure (A) overview:

1. Live-window SCALING via `WindowPaintData` was deliberately removed and there is no crop (`windowanimator.cpp:481-495`). The proven mechanism for a scaled window here is the FBO capture used by snap assist.
2. Painting a non-current desktop's windows has only ever been done as a captured texture (`captureDesktop` re-entering `effects->paintScreen`), never by walking off-desktop windows in the paint chain, which is uniformly gated on `isOnCurrentDesktop()`.

Input-side facts that bear on the choice: no gesture registration anywhere; no `windowInputMouseEvent`; the mouse-interception precedent is deliberately narrow (held only over a tab pill, and it swallows right and middle presses entirely). The alternative input surface, a layer-shell QML overlay that can raise itself to `KeyboardInteractivity::Exclusive`, already exists and is in use, and windows keep being painted by the effect below it.
