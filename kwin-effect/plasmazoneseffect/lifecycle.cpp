// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorAudio/IAudioSpectrumProvider.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/Registration.h>

#include <effect/effecthandler.h>
#include <core/output.h>
#include <virtualdesktops.h>
#include <workspace.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QEvent>
#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>
#include <QVarLengthArray>

#include "../autotilehandler.h"
#include "../compositorclock.h"
#include "../dragtracker.h"
#include "../kwin_compositor_bridge.h"
#include "../navigationhandler.h"
#include "../screenchangehandler.h"
#include "../snapassisthandler.h"
#include "../snaphandler.h"
#include "../windowanimator.h"

namespace PlasmaZones {

// `lcEffect` is defined in plasmazoneseffect.cpp via Q_LOGGING_CATEGORY. Re-declare
// here so this TU can log under the same category without re-defining storage.
Q_DECLARE_LOGGING_CATEGORY(lcEffect)

PlasmaZonesEffect::PlasmaZonesEffect()
    : OffscreenEffect()
    , m_autotileHandler(std::make_unique<AutotileHandler>(this))
    , m_snapHandler(std::make_unique<SnapHandler>(this))
    , m_navigationHandler(std::make_unique<NavigationHandler>(this))
    , m_screenChangeHandler(std::make_unique<ScreenChangeHandler>(this))
    , m_snapAssistHandler(std::make_unique<SnapAssistHandler>(this))
    // Phase 3: per-output motion clocks drive every AnimatedValue in the
    // controller. One `CompositorClock` per `LogicalOutput` so mixed
    // refresh-rate displays (60 Hz + 144 Hz being the common case)
    // phase-lock independently instead of beating against a shared
    // process-wide clock. Populated below via effects->screens() and
    // maintained via the screenAdded/screenRemoved signals. The
    // fallback unbound clock covers: (a) the bootstrap window before
    // screens() populates, (b) windows whose `screen()` is null
    // mid-migration, (c) test paths that don't drive KWin::effects.
    , m_motionClockFallback(std::make_unique<CompositorClock>(nullptr))
    , m_windowAnimator(std::make_unique<WindowAnimator>())
    , m_shaderManager(this)
    , m_desktopTransition(this)
    , m_dragTracker(std::make_unique<DragTracker>(this))
    , m_compositorBridge(std::make_unique<KWinCompositorBridge>(*this))
    , m_decorationManager(std::make_unique<DecorationManager>(*m_compositorBridge))
{
    PhosphorProtocol::registerWireTypes();

    // Decoration-manager wiring (veto + signal connections) lives with the
    // rest of the border/decoration code in decorations.cpp.
    setupDecorationManager();

    // Seed the decoration profile tree with the empty/neutral default so the
    // pre-fetch state is well-defined; the async `decorationProfileTreeJson`
    // fetch overwrites the whole tree on arrival. Borders are rule-owned and
    // render correctly before the fetch, so no placeholder tree is needed.
    seedDecorationTreeBaseline();

    // Sub-pixel vertex precision. KWin's default snapping rounds quad
    // vertex positions to integer pixels before rasterising, which is
    // fine for static / pixel-aligned windows but quantises smooth
    // animations into 1px steps — visible judder at low translate
    // velocities (the end of a bounce as it eases to rest, slow drag
    // snaps). MagicLamp uses the same setting for its quad deformation.
    setVertexSnappingMode(KWin::RenderGeometry::VertexSnappingMode::None);

    // Single-worker pool for off-loading user-texture loads. See the
    // header docstring for `m_shaderManager.m_textureLoaderPool` for the rationale —
    // serialised loads keep the dedupe cheap and avoid duplicate GPU
    // uploads if multiple shader transitions reference the same file
    // in quick succession.
    m_shaderManager.m_textureLoaderPool.setMaxThreadCount(1);

    // Populate per-output clocks from the currently-known output set.
    // Subsequent hotplug events land in onScreenAdded / onScreenRemoved.
    //
    // Order: connect signals FIRST, then iterate the current screens()
    // snapshot. A screen plugged in between those two steps would
    // otherwise be missed — the signal wouldn't have an attached slot
    // yet, and the loop would already have run. With the signals
    // connected first, the worst case is a duplicate `onScreenAdded`
    // call (once via signal, once via loop). `onScreenAdded` is
    // idempotent (re-insertion check against m_motionClocksByOutput)
    // so the duplicate is a no-op.
    if (KWin::effects) {
        connect(KWin::effects, &KWin::EffectsHandler::screenAdded, this, &PlasmaZonesEffect::onScreenAdded);
        connect(KWin::effects, &KWin::EffectsHandler::screenRemoved, this, &PlasmaZonesEffect::onScreenRemoved);
        for (KWin::LogicalOutput* output : KWin::effects->screens()) {
            onScreenAdded(output);
        }
        // Seed the cursor cache with the live position so the first frame
        // after a fresh shader install with iMouse declared sees the real
        // cursor. The default-constructed QPointF(0, 0) would otherwise be
        // misinterpreted as INSIDE any window whose frame contains the
        // origin (i.e. all windows on the primary monitor with origin at
        // (0, 0)) for one frame, producing a false-positive hover spike
        // before prePaintScreen overwrites the cache on the next tick.
        m_shaderManager.m_cachedCursorGlobal = KWin::effects->cursorPos();
    }

    // Wire the fallback clock as the animator's default. The animator's
    // clockForHandle override resolves the per-output clock at
    // startAnimation time; the default kicks in only when a window has
    // no resolvable output (which is rare but real — XWayland
    // bootstrap, mid-migration with a null screen()).
    m_windowAnimator->setClock(m_motionClockFallback.get());
    m_windowAnimator->setOutputClockResolver([this](KWin::LogicalOutput* output) -> PhosphorAnimation::IMotionClock* {
        return clockForOutput(output);
    });
    m_windowAnimator->setOnAnimationCompleteCallback([this](KWin::EffectWindow* w) {
        // Only tear down ANIMATOR-DRIVEN shader transitions
        // (durationMs == 0; the leg rides m_windowAnimator's timeline).
        // Time-based transitions (durationMs > 0; window.open / close /
        // focus / etc.) have their own QTimer teardown scheduled by
        // tryBeginShaderForEvent — without this guard, a window.snapIn
        // transition that's been superseded by another window.* event leaves
        // the original animator running its geometry tween, and that
        // animator's eventual completion would prematurely kill the
        // successor (whose own QTimer hasn't fired yet).
        const auto* st = m_shaderManager.findTransition(w);
        if (!st || st->durationMs > 0) {
            return;
        }
        endShaderTransition(w);
    });
    connect(&m_shaderManager.m_animationShaderRegistry,
            &PhosphorAnimationShaders::AnimationShaderRegistry::effectsChanged, this, [this]() {
                QVarLengthArray<KWin::EffectWindow*, 8> windows;
                for (auto& [w, _] : m_shaderManager.shaderTransitions())
                    windows.push_back(w);
                for (auto* w : windows)
                    endShaderTransition(w);
                // Release-build pair for the contract: every transition entry
                // MUST drain through endShaderTransition before we clear the
                // shader cache. A residual entry holds a cached shader
                // pointer; clearing the cache while it survives would let
                // the next paintWindow on that window deref a freed shader.
                // Self-heal in production by re-running endShaderTransition
                // for the residual entries — same handler the loop above
                // uses — so a future refactor that adds an early-return to
                // endShaderTransition can't crash the compositor.
                if (!m_shaderManager.empty()) {
                    qCCritical(lcEffect) << "shader manager not drained before cache clear; re-draining"
                                         << m_shaderManager.shaderTransitions().size() << "residual transitions";
                    QVarLengthArray<KWin::EffectWindow*, 8> residual;
                    for (auto& [w, _] : m_shaderManager.shaderTransitions())
                        residual.push_back(w);
                    for (auto* w : residual)
                        endShaderTransition(w);
                }
                Q_ASSERT(m_shaderManager.empty());
                m_shaderManager.m_shaderCache.clear();
                // Drop the texture cache too — a hot-reload that swaps a
                // texture file behind the same metadata.json path needs
                // a fresh upload to pick up the new contents. The cache
                // is keyed by absolute path; without this clear a
                // file-content change with no path change would never
                // refresh.
                //
                // Bump the cache generation rather than draining the
                // loader pool synchronously. `waitForDone()` on the GL
                // thread would block the compositor for tens of ms when
                // a worker is mid-rasterise of a 1024x1024 SVG (the
                // worst case for `loadUserTextureImage`). Workers
                // already in flight will complete their CPU rasterise,
                // but their queued GL upload checks the generation
                // captured at submission time against
                // `m_shaderManager.m_textureCacheGeneration` and discards if mismatched
                // — so no stale (pre-reload) bytes can re-populate the
                // cleared cache. Clear immediately so the next
                // `beginShaderTransition` hits the synchronous fallback
                // path and uploads fresh content.
                ++m_shaderManager.m_textureCacheGeneration;
                m_shaderManager.m_textureLoadsInFlight.clear();
                m_shaderManager.m_textureCache.clear();
                // Desktop-switch packs are served by the SAME AnimationShaderRegistry
                // as the per-window effects, so a reloaded `desktop.switch` pack must
                // invalidate the DesktopTransitionManager's parallel compiled-shader
                // cache too — otherwise the next switch renders with the stale shader.
                m_desktopTransition.invalidateShaderCache();
                // A pack reload can flip a pack's `audio` metadata flag, which
                // feeds the cava run gate via hasAudioReactiveAnimation().
                scheduleEffectAudioSync();
            });

    // Surface shader pack hot-reload: when a data/surface pack changes on disk,
    // drop EVERY compiled surface pack so the next paint recompiles each
    // referenced pack against the new source, and repaint so decorated windows
    // pick it up. Also drop the per-window multipass FBO state: a recompiled pack
    // whose buffer-pass COUNT changed would otherwise under-render, because the
    // composite path's chainBufferTex realloc keys on the chain pack-id list (and
    // size), not on each pack's buffer-pass count — only clearing it here forces
    // the next paint to reallocate against the new pass count. The next
    // compiledPack() call recompiles lazily per pack id.
    connect(&m_surfaceShaderRegistry, &PhosphorSurfaceShaders::SurfaceShaderRegistry::effectsChanged, this, [this]() {
        // This fires from the registry's file watcher between frames, where the
        // compositor's GL context is NOT current. m_compiledPacks owns GLShaders
        // and m_surfaceMultipass owns GLTextures, so their destruction issues
        // glDelete* calls that want a current context (the same discipline
        // compiledPack()/surfacePresentShader() apply for off-paint callers).
        // Best-effort make-current on the normal path; the only false case is
        // compositor teardown (!KWin::effects), where GL is being torn down and
        // the driver reclaims the objects regardless, so the clears are safe
        // either way.
        const bool haveContext = KWin::effects && KWin::effects->makeOpenGLContextCurrent();
        m_compiledPacks.clear();
        m_surfaceMultipass.clear();
        if (haveContext) {
            KWin::effects->addRepaintFull();
        }
        // A pack reload can flip a decoration pack's `audio` metadata flag,
        // which feeds the cava run gate via hasAudioReactiveDecoration() —
        // mirror the animation registry's effectsChanged handler above.
        scheduleEffectAudioSync();
    });

    // Frame-geometry shadow flush timer. Debounces per-window
    // windowFrameGeometryChanged signals and pushes the latest geometry to
    // the daemon at ~20Hz so daemon-local shortcut handlers (float toggle,
    // etc.) have fresh geometry without a round-trip. Single-shot timer
    // re-armed on each incoming change — the flush fires at most one D-Bus
    // call per window per 50ms window regardless of how many pixels moved.
    m_frameGeometryFlushTimer = new QTimer(this);
    m_frameGeometryFlushTimer->setSingleShot(true);
    m_frameGeometryFlushTimer->setInterval(50);
    connect(m_frameGeometryFlushTimer, &QTimer::timeout, this, &PlasmaZonesEffect::flushPendingFrameGeometry);

    // Rules.rulesChanged debounce. See slotRulesChanged: the
    // daemon emits one signal per per-rule mutation, so without coalescing a
    // 50-rule batch edit fires 50 full-ruleset fetches + parses. 50ms matches
    // the frame-geometry flush above — single edits feel instant, bursts
    // collapse to a single fetch at the trailing edge.
    m_animationRulesRefreshDebounce.setSingleShot(true);
    m_animationRulesRefreshDebounce.setInterval(50);
    connect(&m_animationRulesRefreshDebounce, &QTimer::timeout, this, &PlasmaZonesEffect::loadRuleAnimationsFromDbus);

    // Connect DragTracker signals
    //
    // Performance optimization: keyboard grab and D-Bus dragMoved calls are deferred
    // until an activation trigger is detected. This eliminates 60Hz D-Bus traffic and
    // keyboard grab/ungrab overhead for non-zone window drags (discussion #167).
    connect(
        m_dragTracker.get(), &DragTracker::dragStarted, this,
        [this](KWin::EffectWindow* w, const QString& windowId, const QRectF& geometry) {
            qCDebug(lcEffect) << "Window move started -" << w->windowClass()
                              << "current modifiers:" << static_cast<int>(m_currentModifiers);

            // Capture the floating state at drag start, before any float
            // transition (the autotile-bypass fast path below floats tiled
            // windows). The drag-stop ApplyFloat path uses this to decide
            // whether to restore the pre-autotile size: a window that was
            // already floating is just being moved and must keep its current
            // user-chosen size, not snap back to the stale pre-autotile rect.
            m_dragStartedFloating = isWindowFloating(windowId);

            // Note: `cursor.drag` is intentionally NOT wired here. The
            // OffscreenEffect pipeline operates on window content; firing
            // a shader at drag start through it is indistinguishable from
            // `window.move`, and synchronously colliding with the
            // `windowStartUserMovedResized` lambda's `window.move` install
            // means whichever fires second wins (it would be `window.move`
            // here). The `cursor` class (`ProfilePaths::Cursor`, with its
            // `CursorHover` / `CursorClick` leaves) is reserved for a future
            // cursor-decoration / drag-shadow surface and carries no drag leaf.

            // Fire beginDrag async to get a daemon-authoritative policy.
            // While the reply is pending, we
            // default m_currentDragPolicy to a conservative snap-path so
            // the worst case (stale effect cache would have said autotile
            // but daemon knows better, or vice-versa) is a brief overlay
            // flash rather than a dead drag. The reply handler flips the
            // bypass flag retroactively a few ms later if the daemon says
            // this is an autotile drag.
            //
            // This replaces the previous stale-cache read of
            // m_autotileHandler->isAutotileScreen() as the single source
            // of truth for drag-start routing — root cause of the
            // post-settings-reload dead-drag window found in #310 log
            // forensics.
            m_currentDragPolicy = PhosphorProtocol::DragPolicy{};
            m_currentDragPolicy.streamDragMoved = true;
            m_currentDragPolicy.showOverlay = true;
            m_currentDragPolicy.grabKeyboard = true;
            m_currentDragPolicy.captureGeometry = true;

            // Bump the per-drag generation and capture the value so the
            // async reply below can detect a stale reply (drag ended
            // before reply arrived, or a new drag started in the gap).
            ++m_dragGeneration;
            const quint64 capturedDragGeneration = m_dragGeneration;
            const QString startScreenId = getWindowScreenId(w);
            const QRect frame = geometry.toRect();
            auto* beginWatcher = new QDBusPendingCallWatcher(
                PhosphorProtocol::ClientHelpers::asyncCall(
                    PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("beginDrag"),
                    {windowId, frame.x(), frame.y(), frame.width(), frame.height(), startScreenId,
                     static_cast<int>(m_currentMouseButtons)}),
                this);
            QPointer<KWin::EffectWindow> safeW = w;
            const QString capturedWindowId = windowId;
            const QString capturedScreenId = startScreenId;
            connect(
                beginWatcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeW, capturedWindowId, capturedScreenId, capturedDragGeneration](QDBusPendingCallWatcher* bw) {
                    bw->deleteLater();
                    QDBusPendingReply<PhosphorProtocol::DragPolicy> reply = *bw;
                    if (!reply.isValid()) {
                        qCWarning(lcEffect) << "beginDrag reply invalid:" << reply.error().message();
                        return;
                    }
                    const PhosphorProtocol::DragPolicy policy = reply.value();
                    if (const QString err = policy.validationError(); !err.isEmpty()) {
                        qCWarning(lcEffect) << "beginDrag reply rejected:" << err
                                            << "— keeping conservative snap-path policy for" << capturedWindowId;
                        return;
                    }
                    // Discard stale replies: the drag this call dispatched
                    // for has already ended (or a new drag started in the
                    // interim) — writing the captured policy now would
                    // bleed it into the active drag's state.
                    if (m_dragGeneration != capturedDragGeneration) {
                        qCInfo(lcEffect) << "beginDrag reply discarded: drag generation" << capturedDragGeneration
                                         << "is stale (current=" << m_dragGeneration << ") for" << capturedWindowId;
                        return;
                    }
                    m_currentDragPolicy = policy;
                    qCInfo(lcEffect) << "beginDrag reply:" << capturedWindowId
                                     << "bypass=" << m_currentDragPolicy.bypassReason
                                     << "stream=" << m_currentDragPolicy.streamDragMoved
                                     << "immediateFloat=" << m_currentDragPolicy.immediateFloatOnStart;
                    // If the daemon confirms autotile, flip the effect
                    // state to bypass mode. Usually the effect-side
                    // fast path below already did this synchronously;
                    // this catches the stale-cache case where the fast
                    // path missed.
                    if (m_currentDragPolicy.bypassReason == PhosphorProtocol::DragBypassReason::AutotileScreen) {
                        if (!m_dragBypassedForAutotile) {
                            m_dragBypassedForAutotile = true;
                            m_dragBypassScreenId = capturedScreenId;
                            qCInfo(lcEffect) << "beginDrag: retroactive autotile bypass for" << capturedWindowId;
                        }
                        // Apply immediate float transition if the policy
                        // says so and the window wasn't already floated
                        // by the fast path. Using QPointer so we skip
                        // if the window was destroyed between drag-start
                        // and reply.
                        if (safeW && !safeW->isDeleted() && m_currentDragPolicy.immediateFloatOnStart
                            && !isWindowFloating(capturedWindowId)
                            && !m_dragFloatedWindowIds.contains(capturedWindowId)) {
                            m_autotileHandler->handleDragToFloat(safeW, capturedWindowId, /*immediate=*/true);
                            m_dragFloatedWindowIds.insert(capturedWindowId);
                        }
                    }
                });

            // Fast path: the effect-side autotile cache is USUALLY correct.
            // We still consult it synchronously so the common case runs at
            // zero latency. The async beginDrag reply above runs as a
            // correction layer for the cases where the cache is stale
            // (post-settings-reload — the #310 scenario).
            if (m_autotileHandler->isAutotileScreen(startScreenId)) {
                m_dragBypassedForAutotile = true;
                m_dragBypassScreenId = startScreenId;
                // Reorder mode: the daemon owns drag-insert preview for tile
                // swapping. Skip the synchronous float transition — we want
                // the tile to stay visually in place while the daemon runs
                // moveToTiledPosition on each cursor tick. The effect still
                // flips into bypass state so snap-path logic is suppressed.
                const bool reorderMode = m_cachedAutotileDragBehavior == EffectAutotileDragBehavior::Reorder;
                // If the window is currently autotile-tiled, restore its
                // title bar and pre-autotile size NOW (synchronously, during
                // the interactive move). This mirrors snap mode, where
                // dragging a snapped window out of its zone visibly restores
                // the free-floating size before release — without this, the
                // user drags a borderless tile-sized window and only sees it
                // become a floating window after they drop.
                //
                // Guarded on isTrackedWindow so we don't touch windows that
                // are already floating (not in the autotile tree).
                if (!reorderMode && m_autotileHandler->isTrackedWindow(windowId) && !isWindowFloating(windowId)) {
                    m_autotileHandler->handleDragToFloat(w, windowId, /*immediate=*/true);
                    // Mark as drag-floated so the daemon's pre-tile geometry
                    // restore (applyGeometryForFloat, triggered by the
                    // setWindowFloatingForScreen call at drop) is skipped in
                    // slotApplyGeometryRequested — the window should stay
                    // where the user drops it, not snap back to a stored rect.
                    m_dragFloatedWindowIds.insert(windowId);
                }
                return;
            }
            m_dragBypassedForAutotile = false;
            m_dragActivationDetected = false;

            // beginDrag already initialized daemon-side snap-drag state
            // (called internally from the adaptor). The effect only needs
            // to decide whether to grab the keyboard for local Escape
            // handling.
            detectActivationAndGrab();
            // Grab keyboard to intercept Escape before KWin's MoveResizeFilter.
            // Without this, Escape cancels the interactive move AND the overlay.
            // With the grab, Escape only dismisses the overlay while the drag continues.
            if (!m_keyboardGrabbed) {
                KWin::effects->grabKeyboard(this);
                m_keyboardGrabbed = true;
            }
        });
    connect(
        m_dragTracker.get(), &DragTracker::dragMoved, this, [this](const QString& windowId, const QPointF& cursorPos) {
            // Cross-VS flip detection is daemon-owned. The
            // daemon's updateDragCursor handler computes policy at the
            // cursor position and emits dragPolicyChanged when it flips.
            // The effect reacts via slotDragPolicyChanged (see below).
            //
            // Here we only forward the cursor to the daemon as a
            // fire-and-forget call. The daemon-side dispatch handles
            // both the snap-path overlay updates and the cross-VS
            // detection in a single round trip.

            // In autotile bypass — skip snap zone processing locally;
            // the daemon's updateDragCursor still watches for a flip
            // BACK to snap mode.
            const bool bypassed = m_currentDragPolicy.bypassReason == PhosphorProtocol::DragBypassReason::AutotileScreen
                || m_dragBypassedForAutotile;
            if (!bypassed) {
                // Gate D-Bus calls on activation trigger state so a drag
                // without any intent to use zones doesn't flood the bus
                // at 30Hz. This is a local input-event optimization; it
                // isn't policy and doesn't come from the daemon.
                if (!detectActivationAndGrab() && !m_cachedZoneSelectorEnabled && m_triggersLoaded) {
                    return;
                }
            }

            // Forward the cursor to the daemon. For snap drags, this
            // drives overlay/zone detection. For bypass drags, the
            // daemon watches the cursor for a cross-VS flip and emits
            // dragPolicyChanged when the policy changes.
            PhosphorProtocol::ClientHelpers::fireAndForget(
                this, PhosphorProtocol::Service::Interface::WindowDrag, QStringLiteral("updateDragCursor"),
                {windowId, qRound(cursorPos.x()), qRound(cursorPos.y()), static_cast<int>(m_currentModifiers),
                 static_cast<int>(m_currentMouseButtons)},
                QStringLiteral("updateDragCursor"));
        });
    connect(m_dragTracker.get(), &DragTracker::dragStopped, this,
            [this](KWin::EffectWindow* w, const QString& windowId, bool cancelled) {
                // Release keyboard grab before handling drag end
                if (m_keyboardGrabbed) {
                    KWin::effects->ungrabKeyboard();
                    m_keyboardGrabbed = false;
                }

                // Clear the drag-floated marker on every drag end. Historically
                // this marker was used to suppress a post-drag pre-tile geometry
                // restore (applyGeometryForFloat), but the current daemon-side
                // drag-end path goes through AutotileEngine::setWindowFloat →
                // windowFloatingStateSynced → syncAutotileFloatStatePassive,
                // which never emits applyGeometryForFloat. Leaving the marker
                // set after a drag leaks it into subsequent Meta+F toggles:
                // the next user float is silently skipped, the window's visual
                // position diverges from the daemon's shadow, and then a
                // float→tile toggle overwrites the stored pre-tile rect with
                // the stale tile zone — permanently corrupting the restore
                // target (#bug: zed/firefox/plasmazones-settings resize issues).
                m_dragFloatedWindowIds.remove(windowId);

                // Single entry point for drag-end dispatch. The
                // daemon owns the decision; callEndDrag sends endDrag and
                // the reply handler applies whatever PhosphorProtocol::DragOutcome comes back
                // (ApplySnap / ApplyFloat / RestoreSize / NoOp / etc.).
                //
                // The autotile branch special-casing that used to live here
                // is gone — cross-VS transitions were applied mid-drag by
                // slotDragPolicyChanged, and final drop-time actions are
                // encoded in the PhosphorProtocol::DragOutcome.
                callEndDrag(w, windowId, cancelled);

                // Bump the per-drag generation so any in-flight beginDrag
                // reply for the drag we just ended is discarded by the
                // reply lambda's generation check. Without this bump, the
                // mismatch check only fires when a NEW drag starts before
                // the reply arrives — a drag that ends WITHOUT a successor
                // would leave the captured generation equal to the current
                // value, the reply would pass the guard, and write its
                // policy + retroactive autotile float into stale state.
                ++m_dragGeneration;

                // Clear drag state for the next session.
                m_currentDragPolicy = PhosphorProtocol::DragPolicy{};
                m_dragBypassedForAutotile = false;
                m_dragBypassScreenId.clear();
                m_dragActivationDetected = false;
            });

    // Connect to window lifecycle signals
    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &PlasmaZonesEffect::slotWindowAdded);
    connect(KWin::effects, &KWin::EffectsHandler::windowClosed, this, &PlasmaZonesEffect::slotWindowClosed);

    // Panel lifecycle drives KWin's work area: a panel added, removed, or
    // resized changes the strut-excluded clientArea. Route panel windows to
    // the screen-change handler so it re-pushes the authoritative work area
    // to the daemon. Covers docks AND unmovable layer-shell surfaces (a
    // third-party shell's exclusive-zone panel is not isDock() to KWin);
    // trackDockWindow / onWindowClosed no-op for every other window.
    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, m_screenChangeHandler.get(),
            &ScreenChangeHandler::trackDockWindow);
    connect(KWin::effects, &KWin::EffectsHandler::windowClosed, m_screenChangeHandler.get(),
            &ScreenChangeHandler::onWindowClosed);
    // Panels mapped before the effect loaded never fire windowAdded — hook the
    // already-present panels now so a later resize of one still re-reports.
    // Skip close-grabbed dying windows: other effects' close animations can
    // hold deleted windows in the stacking order across an effect (re)load.
    for (KWin::EffectWindow* existing : KWin::effects->stackingOrder()) {
        if (!existing || existing->isDeleted()) {
            continue;
        }
        m_screenChangeHandler->trackDockWindow(existing);
    }
    // clientArea(MaximizeArea) is queried for the current virtual desktop, so a
    // panel that reserves space on only one desktop changes the work area when
    // the user switches desktops — re-push so the daemon tracks per-desktop
    // struts too.
    connect(KWin::effects, &KWin::EffectsHandler::desktopChanged, m_screenChangeHandler.get(),
            &ScreenChangeHandler::scheduleClientAreaReport);

    // Border overlays are built only for current-desktop windows (markWindowSnapped
    // and updateAllDecorations both gate on isOnCurrentDesktop), so the overlay for a
    // window snapped while on another desktop isn't created until that desktop
    // becomes current. Rebuild on every desktop switch so those borders appear
    // without waiting for the window to be re-activated.
    connect(KWin::effects, &KWin::EffectsHandler::desktopChanged, this, [this]() {
        updateAllDecorations();
    });

    // Per-output virtual desktops (Plasma 6.7 "switch desktops independently for
    // each screen"): report each output's current desktop so the daemon keys its
    // per-screen desktop map off real per-output switches instead of KWin's global
    // current — which flips merely on cursor movement between monitors on
    // different desktops (#648). This signal does NOT fire on cursor movement,
    // only on an actual desktop change for an output, so it is the deterministic
    // source. `output == nullptr` is a global all-output switch (per-output mode
    // off); fan out to every screen so the daemon has one code path.
    connect(KWin::effects, &KWin::EffectsHandler::desktopChanged, this,
            [this](KWin::VirtualDesktop*, KWin::VirtualDesktop* newDesktop, KWin::EffectWindow*,
                   KWin::LogicalOutput* output) {
                if (!newDesktop) {
                    return;
                }
                if (output) {
                    reportScreenDesktop(outputScreenId(output), static_cast<int>(newDesktop->x11DesktopNumber()));
                    return;
                }
                for (auto* out : KWin::effects->screens()) {
                    if (auto* vd = KWin::effects->currentDesktop(out)) {
                        reportScreenDesktop(outputScreenId(out), static_cast<int>(vd->x11DesktopNumber()));
                    }
                }
            });

    // Full-screen desktop-switch TRANSITION (separate from the daemon-reporting
    // connection above). Resolve the `desktop.switch` shader from the profile
    // tree; when one is assigned, run the two-desktop blend. An empty resolve
    // (default state / user picked None) is a no-op, so KWin's normal switch —
    // or its built-in Slide — proceeds untouched.
    connect(KWin::effects, &KWin::EffectsHandler::desktopChanged, this,
            [this](KWin::VirtualDesktop* oldDesktop, KWin::VirtualDesktop* newDesktop, KWin::EffectWindow*,
                   KWin::LogicalOutput* output) {
                if (!oldDesktop || !newDesktop) {
                    return;
                }
                // Honour the global animations master toggle, exactly as the two
                // per-window shader paths do (beginShaderTransition /
                // tryBeginShaderForEvent). `animationsEnabled` drives
                // m_windowAnimator->setEnabled(); a user who turns all animations
                // off must not still get a full-screen desktop-switch blend.
                if (!m_windowAnimator->isEnabled()) {
                    return;
                }
                const PhosphorAnimationShaders::ShaderProfile profile =
                    PhosphorAnimationShaders::resolveShaderWithDefault(m_shaderManager.profileTree(),
                                                                       PhosphorAnimation::ProfilePaths::DesktopSwitch);
                const QString effectId = profile.effectiveEffectId();
                if (effectId.isEmpty()) {
                    return;
                }
                // Per-event motion profile (curve + duration) for the desktop
                // switch in ONE walk via the shared SSOT: global animator profile
                // → `desktop` → `desktop.switch` motion-tree overrides. The base
                // is the global animator profile, so with no override the switch
                // inherits the master animation duration + curve (the global
                // slider retimes it), and BOTH duration and curve come from the
                // same base. desktop.switch is a windowless event (no per-window
                // rule scope), so pass an empty WindowQuery — the rule layer is
                // then skipped. paintOutput eases iTime through `.curve` so the
                // node's curve shapes the switch.
                const PhosphorAnimation::Profile eventMotion = resolveEventMotionProfile(
                    PhosphorAnimation::ProfilePaths::DesktopSwitch, PhosphorRules::WindowQuery{}, QString());
                const int durationMs = qRound(eventMotion.effectiveDuration());
                m_desktopTransition.begin(oldDesktop, newDesktop, output, effectId, profile.effectiveParameters(),
                                          durationMs, eventMotion.curve);
            });

    // Reap any live desktop transition whose OUTGOING desktop is removed from the
    // pager mid-switch: it captured a raw VirtualDesktop* in begin() that the
    // deferred captureDesktop() dereferences up to the transition's duration later.
    connect(KWin::effects, &KWin::EffectsHandler::desktopRemoved, this, [this](KWin::VirtualDesktop* desktop) {
        m_desktopTransition.desktopRemoved(desktop);
    });

    // Belt-and-suspenders: windowClosed removes animations, but if a deferred
    // timer re-adds one between windowClosed and windowDeleted, the Item tree
    // will be torn down while an animation entry still references the window.
    // Purge here to prevent SIGSEGV in animationBounds → expandedGeometry.
    // Also clean up caches that slotWindowClosed may have already cleared —
    // QHash::take/remove on missing keys is a no-op, so this is safe.
    connect(KWin::effects, &KWin::EffectsHandler::windowDeleted, this, [this](KWin::EffectWindow* w) {
        endShaderTransition(w);
        m_windowAnimator->removeAnimation(w);
        if (m_windowIdCache.contains(w)) {
            const QString cachedId = m_windowIdCache.take(w);
            m_windowIdReverse.remove(cachedId);
            // Free the border entry AND its multipass FBO targets keyed by this
            // window id. Normally removeWindowDecoration (run from slotWindowClosed)
            // already cleared both; the explicit call here is defence-in-depth
            // for a window deleted without a preceding close. Pass the window
            // pointer so the GL release (setShader(nullptr) + unredirect) can
            // still run — findWindowById returns null post-delete. Critically
            // this also drops the m_windowDecorations entry, so a delete-without-
            // close can't strand it and keep isActive() pinned true.
            removeWindowDecoration(cachedId, w);
            // Belt-and-suspenders for the not-expected case of a multipass entry
            // without a border entry (removeWindowDecoration's no-border early-return
            // would otherwise skip the FBO cleanup).
            m_surfaceMultipass.erase(cachedId);
            // Mirror the m_pendingFrameGeometry cleanup that
            // slotWindowClosed runs (window_lifecycle.cpp). A
            // windowFrameGeometryChanged emission between
            // slotWindowClosed and windowDeleted (possible for
            // windows held alive via WindowClosedGrabRole) would
            // re-insert into the pending map; without this belt-
            // and-suspenders cleanup the entry would leak for the
            // rest of the session. Keyed by `cachedId` (composite
            // appId|uuid) which is the same key the pending map
            // uses on the push side.
            m_pendingFrameGeometry.remove(cachedId);
            // Same belt-and-suspenders as m_frameOpacityCache below: a closing
            // decorated window keeps painting under its close animation, and
            // pushBorderUniforms re-creates the m_focusFade entry via operator[]
            // on every such frame AFTER slotWindowClosed already scrubbed it. So
            // the slotWindowClosed removal alone is not enough; drop it here too
            // (keyed by the frozen cachedId) or the entry leaks for the session.
            m_focusFade.remove(cachedId);
            // Same delete-without-close defence for the layer snapshot: the
            // normal removal lives in slotWindowClosed, which a window deleted
            // without a preceding close never reaches. No restore is possible
            // (the window is gone); this only keeps the map bounded.
            m_ruleWindowLayerSnapshots.remove(cachedId);
        }
        m_trackedScreenPerWindow.remove(w);
        m_restoreSuppress.remove(w);
        // Drop per-window shader-event bookkeeping. m_lastFocusShaderWindow is
        // a QPointer that auto-nulls on destroy, so it's already cleaned up;
        // m_shaderManager.m_lastFullyMaximized is a raw-pointer-keyed QHash so we explicitly
        // erase here to keep it bounded across long sessions.
        m_shaderManager.m_lastFullyMaximized.remove(w);
        // Sibling raw-pointer-keyed hashes — the maximize morph's departure
        // rect and the deferred-install entry. Same bounded-across-long-
        // sessions rationale as above, plus address-reuse safety for the
        // pending entry (a stale entry at a reused address would fire a
        // bogus morph on the new window's first resize).
        m_shaderManager.m_preMaximizeFrame.remove(w);
        m_shaderManager.m_pendingMaximizeMorph.remove(w);
        // Drop the queued-expiry guard for this raw pointer. KWin reuses
        // EffectWindow heap addresses freely, so a stale entry surviving
        // past windowDeleted would cause the next window allocated at the
        // same address to skip its first expiry queue (paint_pipeline.cpp's
        // `m_pendingShaderExpiryEnd.contains(w)` check would see the stale
        // entry and decline to insert a fresh one), leaking that window's
        // first lifecycle-event teardown. endShaderTransition above also
        // removes this entry as defence-in-depth, but if a teardown ran
        // earlier in the session and the queued lambda was still pending
        // when windowDeleted fires, the lambda's safeWindow QPointer
        // catches deletion — the bare set entry then needs explicit
        // cleanup here.
        m_shaderManager.m_pendingShaderExpiryEnd.remove(w);
        // Drop the per-frame SetOpacity cache entry for this window. The cache
        // is normally cleared at postPaintScreen, but a window deleted
        // mid-frame leaves a stale raw-pointer key; KWin reuses EffectWindow
        // heap addresses, so a stale entry surviving until postPaintScreen
        // could be read by a paintWindow call that landed at the same
        // address.
        m_shaderManager.m_frameOpacityCache.remove(w);
    });

    connect(KWin::effects, &KWin::EffectsHandler::windowActivated, this, &PlasmaZonesEffect::slotWindowActivated);

    // Update the daemon's primary screen override when KDE Display Settings change
    if (auto* ws = KWin::Workspace::self()) {
        connect(ws, &KWin::Workspace::outputOrderChanged, this, [this]() {
            auto* workspace = KWin::Workspace::self();
            if (workspace && m_daemonServiceRegistered) {
                const auto outputs = workspace->outputOrder();
                if (!outputs.isEmpty()) {
                    PhosphorProtocol::ClientHelpers::fireAndForget(
                        this, PhosphorProtocol::Service::Interface::Screen, QStringLiteral("setPrimaryScreenFromKWin"),
                        {outputs.first()->name()}, QStringLiteral("setPrimaryScreenFromKWin"));
                }
            }
        });
    }

    // mouseChanged is the only reliable way to get modifier state in a KWin effect on Wayland;
    // QGuiApplication::queryKeyboardModifiers() doesn't work since effects run in the compositor.
    connect(KWin::effects, &KWin::EffectsHandler::mouseChanged, this, &PlasmaZonesEffect::slotMouseChanged);

    // Connect to screen geometry changes for keepWindowsInZonesOnResolutionChange feature
    // In KWin 6, use virtualScreenGeometryChanged (not per-screen signal)
    connect(KWin::effects, &KWin::EffectsHandler::virtualScreenGeometryChanged, m_screenChangeHandler.get(),
            &ScreenChangeHandler::slotScreenGeometryChanged);

    // Discussion #527 follow-up: latch the screen-change flag the instant KWin
    // tells us an output appeared or disappeared. KWin fires screenAdded /
    // screenRemoved BEFORE the per-window outputChanged signals it emits for
    // windows it reassigns as part of the layout change, so this beats the
    // race where outputChanged would reach the autotile-delegation guard in
    // window_lifecycle.cpp without isScreenChangeInProgress() set — and
    // when KWin shifts a remaining monitor's x-offset on the second add
    // (DPMS wake of a dual-monitor setup), oldScreenStillConnected returns
    // true and is no help on its own. slotScreenLayoutChanged sets the same
    // pending flag + debounce that virtualScreenGeometryChanged eventually
    // would, so the existing settle path is unchanged once it catches up.
    connect(KWin::effects, &KWin::EffectsHandler::screenAdded, m_screenChangeHandler.get(),
            &ScreenChangeHandler::slotScreenLayoutChanged);
    connect(KWin::effects, &KWin::EffectsHandler::screenRemoved, m_screenChangeHandler.get(),
            &ScreenChangeHandler::slotScreenLayoutChanged);
    // Invalidate screen ID cache and refresh virtual screen definitions on screen changes
    // (connector names may be reassigned, physical screen geometry changes invalidate
    // virtual screen absolute geometry)
    connect(KWin::effects, &KWin::EffectsHandler::virtualScreenGeometryChanged, this, [this]() {
        m_screenIdCache.clear();
        m_lastEffectiveScreenId.clear();
    });

    // Connect to daemon's settingsChanged D-Bus signal. A failed connect is
    // silent otherwise — check the return so a broken subscription is
    // debuggable instead of looking like a daemon that never emits.
    const bool settingsConnected =
        QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                              PhosphorProtocol::Service::Interface::Settings,
                                              QStringLiteral("settingsChanged"), this, SLOT(slotSettingsChanged()));
    if (settingsConnected) {
        qCInfo(lcEffect) << "Connected to daemon settingsChanged D-Bus signal";
    } else {
        qCWarning(lcEffect) << "Failed to connect to daemon settingsChanged D-Bus signal";
    }

    // Connect to virtual screen changes — daemon emits this when a physical screen's
    // virtual subdivisions are added, removed, or modified.
    const bool vsChangedConnected = QDBusConnection::sessionBus().connect(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Screen, QStringLiteral("virtualScreensChanged"), this,
        SLOT(onVirtualScreensChanged(QString)));
    if (vsChangedConnected) {
        qCInfo(lcEffect) << "Connected to daemon virtualScreensChanged D-Bus signal";
    } else {
        qCWarning(lcEffect) << "Failed to connect to daemon virtualScreensChanged D-Bus signal";
    }

    // Connect to per-event motion-profile-tree changes. The daemon emits
    // this (separate from settingsChanged) when a per-event animation
    // duration is edited, so per-event durations apply live instead of
    // only after a logout/login.
    const bool motionTreeConnected = QDBusConnection::sessionBus().connect(
        PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
        PhosphorProtocol::Service::Interface::Settings, QStringLiteral("motionProfileTreeChanged"), this,
        SLOT(slotMotionProfileTreeChanged()));
    if (motionTreeConnected) {
        qCInfo(lcEffect) << "Connected to daemon motionProfileTreeChanged D-Bus signal";
    } else {
        qCWarning(lcEffect) << "Failed to connect to daemon motionProfileTreeChanged D-Bus signal";
    }

    // Connect to keyboard navigation D-Bus signals
    connectNavigationSignals();

    // Connect to autotile D-Bus signals
    m_autotileHandler->connectSignals();
    m_autotileHandler->loadSettings();

    // Verify daemon availability asynchronously to avoid blocking the compositor.
    // CRITICAL: Do NOT use synchronous isServiceRegistered() here. The daemon
    // registers its D-Bus service name in init() BEFORE start() runs heavy
    // initialization and BEFORE the event loop begins (main.cpp:88→94→102).
    // During that window, isServiceRegistered() returns true but the daemon
    // can't process messages. Any synchronous QDBusInterface creation would
    // trigger Introspect, blocking KWin for up to the D-Bus timeout (~25s).
    //
    // Instead, send an async Introspect — if the daemon responds, it's fully
    // operational and we trigger slotDaemonReady(). If it can't respond (still
    // initializing), the call times out harmlessly and we wait for the
    // daemonReady D-Bus signal instead.
    {
        QDBusMessage introspect = QDBusMessage::createMethodCall(
            PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
            QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
        auto* watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(introspect, PhosphorProtocol::Service::DaemonReadyProbeTimeoutMs),
            this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            QDBusPendingReply<QString> reply = *w;
            if (reply.isValid() && !m_daemonServiceRegistered) {
                // Daemon responded — it's fully operational.
                // Trigger the same ready flow as the daemonReady signal.
                slotDaemonReady();
            }
        });
    }

    // Connect to daemon's daemonReady signal — emitted at the end of Daemon::start()
    // after all initialization is complete and the daemon can process D-Bus messages.
    // This is the safe point to set m_daemonServiceRegistered and create QDBusInterfaces.
    QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                          PhosphorProtocol::Service::Interface::LayoutRegistry,
                                          QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));

    // Watch for daemon D-Bus service registration and unregistration.
    // After a daemon restart, m_lastCursorOutput is still valid in the effect
    // but the daemon's lastCursorScreenName/lastActiveScreenName are empty.
    // Without this, keyboard shortcuts (rotate, etc.) operate on all screens
    // because resolveShortcutScreen returns nullptr.
    //
    // On Wayland, this watcher uses D-Bus monitoring (not X11 selection),
    // which works reliably across both sessions.
    auto* serviceWatcher = new QDBusServiceWatcher(
        PhosphorProtocol::Service::Name, QDBusConnection::sessionBus(),
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(serviceWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        qCInfo(lcEffect) << "Daemon service unregistered";
        m_daemonServiceRegistered = false;
        // Drop the virtual-screen readiness immediately. The defs from the
        // previous daemon cycle are now stale; without clearing the flag here,
        // the windowFrameGeometryChanged VS-crossing detector would keep
        // resolving against stale virtual-screen boundaries during the gap
        // between unregistration and the next daemon's fetch. continueDaemonReady
        // setup re-clears and refetches on bringup; this closes the gap before it.
        m_virtualScreensReady = false;
        // The stale floating-window set is dropped further down in this same
        // handler (clearAllFloatingState beside clearAllZoneState, paired with
        // the rule-cache invalidation) — no separate clear here.
        // Also clear the bridge-registration in-flight gate. Without
        // this, a daemon-restart racing the in-flight registerBridge
        // reply leaves the gate set: the new daemon's `daemonReady`
        // signal arrives, slotDaemonReady sees the gate true and
        // bails, and the gate only clears later when the stale call's
        // error reply arrives — by which time no further signal will
        // re-trigger slotDaemonReady. The effect would sit idle
        // indefinitely. Resetting here keeps the gate authoritative
        // across daemon restarts.
        m_bridgeRegistrationInFlight = false;
        m_daemonReadyRestoresDone = false;
        m_daemonReadyWindowStateProcessed = false;
        m_snapHandler->clearRestoreCache();
        // Reset the rules-subscription gate so the next daemon's
        // `rulesChanged` broadcasts can be re-subscribed. Without this,
        // the daemonReady disconnect+reconnect dance below would re-wire
        // daemonReady against the new bus name but the rulesChanged
        // subscription guard would still latch and skip the re-subscribe
        // — silently dropping rule edits across daemon restarts.
        //
        // Disconnect the previous rulesChanged match rule BEFORE flipping
        // the gate. Qt does not deduplicate match rules (same pitfall the
        // daemonReady serviceRegistered handler addresses); without this
        // disconnect, every daemon restart accumulates one extra match
        // rule, and each rulesChanged emission then dispatches N times
        // to slotRulesChanged across N restarts. The debounce
        // collapses the work to a single fetch, but each dispatch still
        // pays D-Bus delivery + Qt slot invocation.
        QDBusConnection::sessionBus().disconnect(QString(PhosphorProtocol::Service::Name),
                                                 QString(PhosphorProtocol::Service::ObjectPath),
                                                 QString(PhosphorProtocol::Service::Interface::Rules),
                                                 QStringLiteral("rulesChanged"), this, SLOT(slotRulesChanged()));
        m_rulesSubscribed = false;
        // Release any pending first-frame open suppression. Without the
        // daemon there is no `resolveWindowRestore` reply coming and no
        // autotile reposition either, so the suppression entry would just
        // hold the window invisible until its 250ms deadline. Releasing
        // each entry through endRestoreSuppression also schedules the
        // per-window repaint so the windows become visible immediately
        // rather than at the next natural compositor cycle.
        const auto suppressedWindows = m_restoreSuppress.keys();
        for (KWin::EffectWindow* sw : suppressedWindows) {
            endRestoreSuppression(sw);
        }

        // Restore borderless and monocle-maximized windows — daemon state is
        // gone. Clear the handlers' tiled tracking FIRST: restoreAll() emits
        // windowDecorationRestored per window, and the rebuild-on-restore
        // handler would otherwise recreate a border item for every still-
        // tracked window only for clearAllDecorations() to destroy it moments
        // later. With tracking cleared, resolveSurfacePathFor resolves
        // mode-tracked windows to window.floating during the restore burst and
        // the handler drops their items. Windows matched by a still-live SetBorder rule
        // (the rule sets deliberately survive daemon loss, see below) can
        // still get an item recreated and immediately torn down by
        // clearAllDecorations() — bounded, invisible churn that is cheaper than
        // suppressing the handler across the burst.
        m_autotileHandler->clearTiledTracking();
        m_snapHandler->clearSnapTracking();
        // Drop the zone / floating caches that feed the IsSnapped / Zone /
        // IsFloating rule-match fields. Unlike the exclusion / animation rule
        // sets (deliberately preserved below), these caches mirror per-window
        // PLACEMENT state owned by the now-dead daemon session. Keeping them
        // would let a `WHEN IsSnapped` / `Zone(...)` / `IsFloating` rule match
        // against stale state during the bringup race until the async
        // syncZonesFromDaemon / getFloatingWindows re-seed lands. Both are
        // authoritatively repopulated on daemon-ready.
        m_navigationHandler->clearAllZoneState();
        m_navigationHandler->clearAllFloatingState();
        // The placement caches above feed placement-scoped rule match inputs. A
        // SetOpacity rule keyed on IsSnapped/IsFloating/Zone caches its verdict
        // per (windowId, ruleSet revision) — neither moves here — so without this
        // the window keeps its stale opacity (borders revert via restoreAll /
        // clearAllDecorations below, but opacity would not). Re-resolve every opacity
        // window against the now-cleared placement, matching the border teardown.
        // Also carries the window-layer sweep (see invalidateAllRuleCaches): a
        // `WHEN IsFloating` layer rule releases its keep-above here (snapshot
        // restore) instead of stranding it for the daemon-down interval.
        invalidateAllRuleCaches();
        m_decorationManager->restoreAll();
        m_autotileHandler->restoreAllMonocleMaximized();
        clearAllDecorations();
        // Deliberately do NOT clear `m_snappingExclusionRuleSet`,
        // `m_animationExclusionRuleSet`, or the shader manager's animation
        // rule set. Across a daemon restart the user's last-known rule set
        // remains authoritative — clearing here would briefly drop every
        // exclusion / animation override during the bringup race, flashing
        // un-filtered animations and unstyled snaps until the new daemon
        // replays its rulesChanged broadcast. The sets get refreshed once
        // the new daemon's `loadRuleAnimationsFromDbus` reply lands.
    });
    connect(serviceWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        qCInfo(lcEffect) << "Daemon registered: waiting for daemonReady signal";

        // DO NOT set m_daemonServiceRegistered = true here.
        // The daemon registers its D-Bus service name in init(), BEFORE start()
        // runs heavy initialization and BEFORE the event loop begins. Keep the
        // flag false until the daemon's own daemonReady signal fires (end of
        // Daemon::start()), confirming it can handle D-Bus requests.

        // Reconnect daemonReady signal — Qt may cache the old daemon's unique bus
        // name in match rules, so refresh for the new daemon instance.
        // Disconnect first to prevent duplicate match rules (Qt doesn't deduplicate),
        // which would cause slotDaemonReady to fire twice on the same signal.
        QDBusConnection::sessionBus().disconnect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                                 PhosphorProtocol::Service::Interface::LayoutRegistry,
                                                 QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));
        QDBusConnection::sessionBus().connect(PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                                              PhosphorProtocol::Service::Interface::LayoutRegistry,
                                              QStringLiteral("daemonReady"), this, SLOT(slotDaemonReady()));
    });

    // NOTE: daemon state sync (floating windows, cached settings) is NOT done
    // here. m_daemonServiceRegistered is false at this point (set only by
    // slotDaemonReady), so any ensureInterface() call would bail out immediately.
    // All daemon state sync is deferred to slotDaemonReady().

    // Connect to existing windows. Skip close-grabbed dying windows — wiring
    // per-window connections and seeding screen tracking for a window whose
    // close already happened would resurrect state nothing cleans up.
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        if (!w || w->isDeleted()) {
            continue;
        }
        setupWindowConnections(w);
    }

    // The daemon disables KWin's Quick Tile via kwriteconfig6. We don't reserve electric borders
    // here because that would turn on the edge effect visually; the daemon's config approach
    // is the right way to prevent Quick Tile from activating.

    // Seed m_lastCursorOutput with the compositor's active screen. This ensures
    // the daemon has a valid cursor screen even if no mouse movement occurs after login.
    // slotMouseChanged will overwrite this as soon as the cursor moves.
    //
    // The actual D-Bus push to the daemon happens in slotDaemonReady(), which fires
    // either from the async Introspect callback above (daemon already running) or
    // from the daemonReady D-Bus signal (daemon starts later). We do NOT push here
    // to avoid synchronous QDBusInterface creation on the compositor thread.
    auto* initialScreen = KWin::effects->activeScreen();
    if (initialScreen) {
        m_lastCursorOutput = initialScreen->name();
    }

    qCInfo(lcEffect) << "initialized: C++ effect with D-Bus support and mouseChanged connection";
}

void PlasmaZonesEffect::clearDaemonCompositorState()
{
    qCInfo(lcEffect) << "Clearing daemon compositor drag/overlay state before effect shutdown";

    // Drop any in-flight desktop-switch transition and release its
    // active-fullscreen-effect claim. reset() is null-safe (it guards every
    // KWin::effects access), so it is fine to run here even though this can be
    // reached from the destructor before the KWin::effects teardown guards.
    m_desktopTransition.reset();

    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::WindowDrag,
                                                QStringLiteral("clearForCompositorReconnect"));
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::Overlay,
                                                QStringLiteral("hideOverlay"));
}

PlasmaZonesEffect::~PlasmaZonesEffect()
{
    // Sever the registry's `effectsChanged` connection BEFORE anything
    // else runs. The slot lambda touches `m_shaderManager.m_shaderTransitions`,
    // `m_shaderManager.m_shaderCache`, and `m_shaderManager.m_textureCache` — all
    // declared AFTER `m_animationShaderRegistry` in shadertransitionmanager.h,
    // so they destruct FIRST in C++ reverse-declaration order. The registry
    // destructs LAST, and any signal it (or its underlying file-watcher) emits
    // during its own member teardown would dispatch to the slot AFTER the
    // cache members are gone — UAF. Disconnect now while everything is still
    // alive.
    disconnect(&m_shaderManager.m_animationShaderRegistry, nullptr, this, nullptr);
    disconnect(&m_surfaceShaderRegistry, nullptr, this, nullptr);

    // Stop the audio-spectrum provider (terminates its cava child process) and
    // sever its signal before teardown, so a late spectrumUpdated can't dispatch
    // to onEffectAudioSpectrum against half-destroyed members.
    if (m_audioProvider) {
        disconnect(m_audioProvider.get(), nullptr, this, nullptr);
        m_audioProvider->stop();
    }
    // Force the audio run gate off BEFORE the posted-event drain below. Teardown
    // (clearAllDecorations → removeWindowDecoration) posts fresh
    // scheduleEffectAudioSync MetaCalls, and the QEvent::MetaCall drain would run
    // syncEffectAudioState while decorations still exist — respawning cava moments
    // before member destruction kills it again. With the toggle forced off,
    // wantRun is false so the drained sync takes the harmless not-wanted branch.
    // Clear the spectrum size too so that branch's `wasLive` is false and it
    // requests no spurious full repaint during shutdown.
    m_enableAudioVisualizer = false;
    m_audioSpectrumSize = 0;

    // Drain the texture loader pool before any other teardown. A
    // worker that's mid-rasterise would otherwise post a queued
    // upload via QMetaObject::invokeMethod against `this` AFTER our
    // members start tearing down — UAF on the texture cache.
    // QThreadPool::waitForDone is called here BEFORE we let the
    // member's destructor run (which itself blocks on waitForDone)
    // so the pending invokeMethod posts are flushed against a still-
    // intact `this`.
    //
    // Distinct from the hot-reload path in the `effectsChanged`
    // lambda above: hot-reload bumps `m_shaderManager.m_textureCacheGeneration` (no
    // wait) so workers in flight can discard their queued upload
    // without blocking the compositor. Shutdown REQUIRES the wait —
    // the queued uploads need to be flushed against a live `this`,
    // not raced against member destruction.
    m_shaderManager.m_textureLoaderPool.waitForDone();
    m_shaderManager.m_textureLoadsInFlight.clear();

    // Clear daemon-side drag/overlay state while the effect is still alive,
    // but only after the destructor's local UAF-prevention guards have run.
    clearDaemonCompositorState();

    // Restore borderless and monocle-maximized windows so they recover properly.
    // Guard against compositor teardown — effects may outlive the stacking order.
    if (KWin::effects) {
        // Tiled tracking cleared first so the rebuild-on-restore handler
        // doesn't churn border items during the restore burst (see the
        // daemon-loss site above); restoreAll() covers every owner kind
        // including rule overrides.
        m_autotileHandler->clearTiledTracking();
        m_snapHandler->clearSnapTracking();
        m_decorationManager->restoreAll();
        m_autotileHandler->restoreAllMonocleMaximized();
        restoreAllRuleWindowLayers();
        clearAllDecorations();
    }

    if (m_keyboardGrabbed && KWin::effects) {
        // Symmetric with the `if (KWin::effects)` guard above: during
        // compositor teardown KWin::effects can be null, and an
        // unguarded deref here would crash even though we reached the
        // destructor body cleanly. The compositor's own teardown
        // releases the grab when KWin::effects is gone.
        KWin::effects->ungrabKeyboard();
        m_keyboardGrabbed = false;
    }
    m_screenChangeHandler->stop();
    // We no longer reserve/unreserve edges; the daemon disables KWin snap via config.

    // Explicitly tear down every active shader transition before the
    // member-by-member destruction runs. `endShaderTransition` calls
    // `setShader(window, nullptr)` and `unredirect(window)` to release
    // KWin's offscreen state cleanly; relying on default destruction
    // would let the offscreen FBOs linger until KWin's effect-removed
    // bookkeeping ran. Iterates a snapshot because endShaderTransition
    // erases from `m_shaderManager.m_shaderTransitions` mid-loop.
    //
    // Guarded by `if (KWin::effects)` matching the clearAllDecorations /
    // ungrabKeyboard guards above: during compositor teardown the global
    // is null and `endShaderTransition` dereferences it (setShader,
    // unredirect, refWindow). The compositor's own teardown reclaims
    // the offscreen state when KWin::effects is gone.
    if (KWin::effects) {
        QVarLengthArray<KWin::EffectWindow*, 8> activeWindows;
        for (auto& [w, _] : m_shaderManager.shaderTransitions()) {
            activeWindows.push_back(w);
        }
        for (auto* w : activeWindows) {
            endShaderTransition(w);
        }
        // endShaderTransition queues each window's `unrefWindow` via
        // QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection) to
        // avoid use-after-free in paintWindow's expiry fall-through. In
        // the destructor path that defer is unsafe in the opposite
        // direction: ~QObject runs after this body returns and discards
        // pending posted MetaCalls targeted at `this`, so the queued
        // unrefs would never fire and KWin's EffectWindow refcount
        // stays incremented for every close-grab transition active at
        // teardown — leaking the close grab. Drain the queue here, while
        // `this` is still fully constructed and the lambdas can safely
        // run. The lambdas only call `KWin::effects->unrefWindow(...)`
        // and don't touch our member state after the unref, so
        // synchronous dispatch from the dtor body is sound.
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
    }
}

} // namespace PlasmaZones
