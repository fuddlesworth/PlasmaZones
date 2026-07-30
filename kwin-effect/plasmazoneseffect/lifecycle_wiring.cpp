// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include <PhosphorAnimation/AnimationShaderEffect.h> // shaderEffectAppliesToEventPath (peek suppression gate)
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

#include "tilinghandler/tilinghandler.h"
#include "compositor/compositorclock.h"
#include "handlers/dragtracker.h"
#include "compositor/compositorbridge.h"
#include "handlers/navigationhandler.h"
#include "handlers/screenchangehandler.h"
#include "handlers/snapassisthandler.h"
#include "handlers/snaphandler.h"
#include "compositor/windowanimator.h"

namespace PlasmaZones {

// `lcEffect` is defined in plasmazoneseffect.cpp via Q_LOGGING_CATEGORY. Re-declare
// here so this TU can log under the same category without re-defining storage.
Q_DECLARE_LOGGING_CATEGORY(lcEffect)

// Constructor wiring, decomposed from the PlasmaZonesEffect ctor along its
// original comment seams. Called from the ctor shell (lifecycle.cpp) in this
// exact order; the "connect signals FIRST, then iterate screens" ordering the
// clocks block documents is preserved within initRenderingAndRegistries().
void PlasmaZonesEffect::initRenderingAndRegistries()
{
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
                // Make the GL context current FIRST. This fires from the registry's file
                // watcher, between frames, where the compositor's context is not current
                // — and everything below is GL: endShaderTransition hands the redirect
                // back (KWin destroys the offscreen framebuffer), and the two cache
                // clears destroy GLShaders and GLTextures, i.e. glDeleteProgram and
                // glDeleteTextures. Its SIBLING handler (the surface registry's, below)
                // has done this from the start and explains why.
                //
                // It is NOT enough that endShaderTransition makes the context current
                // itself: it only runs from the drain loop, once per LIVE transition, and
                // the ordinary hot-reload (a user saves a .glsl, no window mid-animation)
                // drains nothing at all. The cache clears below then run against no
                // context. An earlier pass removed this call on exactly that reasoning and
                // left this paragraph standing over the hole.
                ensureGlContextCurrent();

                // Drain UNCONDITIONALLY. Gating the drain on the context bought nothing —
                // and worse, the residual self-heal below would notice the undrained
                // transitions, log a CRITICAL, and then drain them anyway, which made the
                // gate a false-alarm generator rather than a safety measure. Bailing
                // outright, which this first did, was worse still: it left the caches
                // populated, so a hot-reloaded pack kept rendering from its OLD compiled
                // shader for the rest of the session.
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
                // The registry commit is also the FIRST moment (bringup) and
                // the ONLY moment (pack install/uninstall) a suppression-owning
                // pack's validity (peek / minimize / maximize) can change
                // without a tree edit: the profile
                // tree arrives on an EARLIER D-Bus reply than the registry
                // scan, so the tree-load sync at loadShaderProfileFromDbus
                // resolves against an empty registry on session start, and
                // deleting the assigned pack from disk never touches the tree
                // at all. Re-run the suppression sync here so KWin's stock
                // effects are unloaded exactly when our pack
                // becomes runnable and restored the moment it stops being.
                syncStockEffectSuppression();
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
        // ensureGlContextCurrent() is that one shared make-current; its only
        // false case is compositor teardown (!KWin::effects), where GL is being
        // torn down and the driver reclaims the objects regardless, so the
        // clears are safe either way. The sibling animation-registry handler
        // above uses the same helper.
        ensureGlContextCurrent();
        m_compiledPacks.clear();
        m_anyCompiledPackReadsCursor = false; // re-derived as packs recompile
        m_opacityTintFallbackWarned = false; // re-arm the capture-fallback warning with the fresh compiles
        m_surfaceMultipass.clear();
        // Repaint whenever there is a compositor, NOT only when the context went current: a
        // repaint is not GL work. Gating it on the make-current result meant a transient
        // failure dropped the caches but never asked the screen to redraw, so the reloaded
        // packs would not appear until something incidental damaged the scene.
        if (KWin::effects) {
            KWin::effects->addRepaintFull();
        }
        // A pack reload can flip a decoration pack's `audio` metadata flag,
        // which feeds the cava run gate via hasAudioReactiveDecoration() —
        // mirror the animation registry's effectsChanged handler above.
        scheduleEffectAudioSync();
    });
}

void PlasmaZonesEffect::initTimers()
{
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
}

void PlasmaZonesEffect::connectDragTracker()
{
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
            m_dragActivation.startedFloating = isWindowFloating(windowId);

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
            // m_tilingHandler->isManagedScreen() as the single source
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
            ++m_dragActivation.generation;
            const quint64 capturedDragGeneration = m_dragActivation.generation;
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
                    if (m_dragActivation.generation != capturedDragGeneration) {
                        qCInfo(lcEffect) << "beginDrag reply discarded: drag generation" << capturedDragGeneration
                                         << "is stale (current=" << m_dragActivation.generation << ") for"
                                         << capturedWindowId;
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
                        if (!m_dragBypassedForEngine) {
                            m_dragBypassedForEngine = true;
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
                            && !m_dragActivation.floatedWindowIds.contains(capturedWindowId)) {
                            m_tilingHandler->handleDragToFloat(safeW, capturedWindowId, /*immediate=*/true);
                            m_dragActivation.floatedWindowIds.insert(capturedWindowId);
                        }
                    } else if (m_dragBypassedForEngine
                               && m_currentDragPolicy.bypassReason == PhosphorProtocol::DragBypassReason::None) {
                        // The correction layer must correct BOTH ways: the
                        // fast path latched the engine bypass from the
                        // effect's cached union set, but the daemon (the
                        // authority) answered the CANONICAL SNAP policy.
                        // Without this clear, effect and daemon stay
                        // divergent for the whole drag — the effect
                        // suppresses its snap path while the daemon runs
                        // zone detection, and the drop can apply an
                        // untracked snap. Restricted to None: a
                        // ContextDisabled/SnappingDisabled answer is a DEAD
                        // drag, and un-bypassing would re-enter snap-path
                        // cursor streaming on a screen the user disabled.
                        // Run the same full transition slotDragPolicyChanged
                        // uses for the autotile→snap flip (tracking drop,
                        // activation reset, keyboard grab), not just a flag
                        // clear — a half transition leaves Escape uncaught
                        // and the snap state uninitialised.
                        // Guarded on the ID, not the dragged-window pointer:
                        // the call is id-keyed bookkeeping that never derefs
                        // the window, and a window that died between drag
                        // start and this reply must not skip the tracking
                        // cleanup for a still-valid id. slotDragPolicyChanged's
                        // equivalent transition guards the same way, and this
                        // branch claims to run the same full transition.
                        if (!capturedWindowId.isEmpty()) {
                            m_tilingHandler->onWindowClosed(capturedWindowId, m_dragBypassScreenId);
                        }
                        m_dragBypassedForEngine = false;
                        m_dragBypassScreenId.clear();
                        m_dragActivation.detected = false;
                        if (!m_keyboardGrabbed) {
                            KWin::effects->grabKeyboard(this);
                            m_keyboardGrabbed = true;
                        }
                        qCInfo(lcEffect) << "beginDrag: daemon rejected engine bypass for" << capturedWindowId
                                         << "- reverting to the snap path";
                    }
                });

            // Fast path: the effect-side autotile cache is USUALLY correct.
            // We still consult it synchronously so the common case runs at
            // zero latency. The async beginDrag reply above runs as a
            // correction layer for the cases where the cache is stale
            // (post-settings-reload — the #310 scenario).
            if (m_tilingHandler->isManagedScreen(startScreenId)) {
                m_dragBypassedForEngine = true;
                m_dragBypassScreenId = startScreenId;
                // Reorder mode: the daemon owns drag-insert preview for tile
                // swapping. Skip the synchronous float transition — we want
                // the tile to stay visually in place while the daemon runs
                // moveToTiledPosition on each cursor tick. The effect still
                // flips into bypass state so snap-path logic is suppressed.
                //
                // Scrolling screens are excluded: the setting is the AUTOTILE
                // drag behaviour, and there is no drag-insert preview for the
                // strip — the daemon's scroll branch unconditionally answers
                // immediateFloatOnStart for a tracked window. Letting a global
                // Reorder suppress the synchronous float on a scrolling screen
                // only deferred it to the async beginDrag reply, so the user
                // dragged a borderless strip-sized tile for the round trip,
                // which is the exact deferred-visual defect this fast path
                // exists to prevent.
                const bool reorderMode = !m_tilingHandler->isScrollingScreen(startScreenId)
                    && m_cachedAutotileDragBehavior == EffectAutotileDragBehavior::Reorder;
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
                if (!reorderMode && m_tilingHandler->isTrackedWindow(windowId) && !isWindowFloating(windowId)) {
                    m_tilingHandler->handleDragToFloat(w, windowId, /*immediate=*/true);
                    // Mark as drag-floated so the daemon's pre-tile geometry
                    // restore (applyGeometryForFloat, triggered by the
                    // setWindowFloatingForScreen call at drop) is skipped in
                    // slotApplyGeometryRequested — the window should stay
                    // where the user drops it, not snap back to a stored rect.
                    m_dragActivation.floatedWindowIds.insert(windowId);
                }
                return;
            }
            m_dragBypassedForEngine = false;
            m_dragActivation.detected = false;

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
                || m_dragBypassedForEngine;
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
                m_dragActivation.floatedWindowIds.remove(windowId);

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
                ++m_dragActivation.generation;

                // Clear drag state for the next session.
                m_currentDragPolicy = PhosphorProtocol::DragPolicy{};
                m_dragBypassedForEngine = false;
                m_dragBypassScreenId.clear();
                m_dragActivation.detected = false;
            });
}

void PlasmaZonesEffect::connectWindowAndScreenSignals()
{
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

    // Full-screen show-desktop PEEK transition, the sibling of the desktop
    // switch above. Resolve the `desktop.peek` shader; when one is assigned,
    // run the windows-scene / bare-desktop blend (hide leg on true, show-back
    // leg on false). One node drives both legs over the same endpoints; the
    // show leg reverses the blend rather than swapping the captures, so an
    // asymmetric pack retraces its own motion (see paintOutput). An empty
    // resolve is a no-op, so KWin's default show-desktop behaviour proceeds
    // untouched.
    // While a pack IS assigned, KWin's windowaperture / eyeonscreen script
    // effects are unloaded (syncStockEffectSuppression) — they ignore
    // the fullscreen claim, and left loaded they would leak their transforms
    // into the peek captures.
    connect(KWin::effects, &KWin::EffectsHandler::showingDesktopChanged, this, [this](bool showing) {
        // Honour the global animations master toggle, exactly as the
        // desktop-switch handler above does — folded into the resolve so the
        // empty-id call below still happens.
        QString effectId;
        PhosphorAnimationShaders::ShaderProfile profile;
        if (m_windowAnimator->isEnabled()) {
            profile = PhosphorAnimationShaders::resolveShaderWithDefault(m_shaderManager.profileTree(),
                                                                         PhosphorAnimation::ProfilePaths::DesktopPeek);
            effectId = profile.effectiveEffectId();
        }
        if (effectId.isEmpty()) {
            // No pack runnable for THIS toggle — but beginPeek must still see
            // the signal: its empty-id contract reaps any live peek leg, so a
            // hide leg begun while a pack WAS assigned (since unassigned, or
            // animations since disabled) cannot keep blending against the
            // reversed toggle or poison the bare-desktop cache.
            m_desktopTransition.beginPeek(showing, QString(), QVariantMap(), 0, nullptr);
            return;
        }
        // Same one-walk motion resolve as desktop.switch: global animator
        // profile → `desktop` → `desktop.peek` motion-tree overrides. Peek is
        // a windowless event, so the empty WindowQuery skips the rule layer.
        const PhosphorAnimation::Profile eventMotion = resolveEventMotionProfile(
            PhosphorAnimation::ProfilePaths::DesktopPeek, PhosphorRules::WindowQuery{}, QString());
        m_desktopTransition.beginPeek(showing, effectId, profile.effectiveParameters(),
                                      qRound(eventMotion.effectiveDuration()), eventMotion.curve);
    });

    // Reap any live desktop transition whose OUTGOING desktop is removed from the
    // pager mid-switch: it captured a raw VirtualDesktop* in begin() that the
    // deferred captureDesktop() still COMPARES (isOnDesktop) up to the
    // transition's duration later. It never derefs it, so this is not a crash
    // guard — a freed pointer must simply not linger and be matched against a
    // live desktop.
    connect(KWin::effects, &KWin::EffectsHandler::desktopRemoved, this, [this](KWin::VirtualDesktop* desktop) {
        m_desktopTransition.desktopRemoved(desktop);
    });

    // Belt-and-suspenders: windowClosed removes animations, but if a deferred
    // timer re-adds one between windowClosed and windowDeleted, the Item tree
    // will be torn down while an animation entry still references the window.
    // Purge here to prevent SIGSEGV in the per-frame damage sweep
    // (scheduleRepaints → expandedPadding → expandedGeometry).
    // Also clean up caches that slotWindowClosed may have already cleared —
    // QHash::take/remove on missing keys is a no-op, so this is safe.
    connect(KWin::effects, &KWin::EffectsHandler::windowDeleted, this, [this](KWin::EffectWindow* w) {
        endShaderTransition(w);
        m_windowAnimator->removeAnimation(w);
        if (m_idCaches.windowIdCache.contains(w)) {
            const QString cachedId = m_idCaches.windowIdCache.take(w);
            m_idCaches.windowIdReverse.remove(cachedId);
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
            //
            // Its own make-current, not one borrowed from the endShaderTransition above:
            // this destroys GLTextures and GLFramebuffers, we are off the paint cycle, and
            // the day that neighbour grows an early return this would silently become
            // glDelete* against no context. The window is gone, so the transition guard
            // releaseSurfaceState applies has nothing left to protect — erase directly.
            if (m_surfaceMultipass.contains(cachedId)) {
                ensureGlContextCurrent();
                m_surfaceMultipass.erase(cachedId);
            }
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
        // Spurious-minimize-pair stamp — raw-pointer-keyed like its
        // siblings below, so erase here both to stay bounded and so a
        // reused address can't inherit a stale stamp that would swallow
        // the new window's first genuine un-minimize animation.
        m_minimizeShaderStamp.remove(w);
        // Drop per-window shader-event bookkeeping. m_lastFocusShaderWindow is
        // a QPointer that auto-nulls on destroy, so it's already cleaned up;
        // m_shaderManager.m_lastFullyMaximized is a raw-pointer-keyed QHash so we explicitly
        // erase here to keep it bounded across long sessions.
        m_shaderManager.m_lastFullyMaximized.remove(w);
        m_lastPushedCaption.remove(w);
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
            if (workspace && m_daemonGate.serviceRegistered) {
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
        m_idCaches.screenIdCache.clear();
        m_idCaches.connectedPhysicalIdsValid = false;
        m_lastEffectiveScreenId.clear();
    });
}

} // namespace PlasmaZones
