// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "shader_internal.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <window.h>

#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

#include "autotilehandler/autotilehandler.h"
#include "handlers/dragtracker.h"
#include "handlers/screenchangehandler.h"

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace {
// Maximize-morph landing discriminator: has the frame SIZE actually moved
// away from the departure rect? A maximize/restore always changes the frame
// size, while a position-only change can apply early server-side, so the
// size delta (with a 1px tolerance for fractional-scale residue) is what
// distinguishes "the jump landed" from "still waiting on the client's
// commit". Shared by the state-changed arming site and the deferred
// windowFrameGeometryChanged completion so the threshold has one home.
bool maximizeSizeLanded(const QRectF& frame, const QRectF& departureFrame)
{
    return qAbs(frame.width() - departureFrame.width()) > 1.0 || qAbs(frame.height() - departureFrame.height()) > 1.0;
}
} // namespace

void PlasmaZonesEffect::setupWindowConnections(KWin::EffectWindow* w)
{
    if (!w)
        return;

    connect(w, &KWin::EffectWindow::windowDesktopsChanged, this, [this](KWin::EffectWindow* window) {
        updateWindowStickyState(window);
        // No metadata push here: the daemon's float resolver reads the
        // window's own desktop/activity from the registry, but that is kept
        // fresh by the KWin::Window::desktopsChanged → pushLatest connection
        // below (this signal is KWin's EffectWindow relay of the same event,
        // so a push here would build and marshal the extended snapshot twice
        // per desktop move).

        // When a window is moved to a different desktop (e.g., "Move to Desktop 2"),
        // treat it as removed from the current desktop's tiling. The normal desktop-
        // switch flow will pick it up when the user switches to the target desktop.
        if (window && !window->isOnCurrentDesktop() && !window->isOnAllDesktops()) {
            const QString windowId = getWindowId(window);
            const QString screenId = getWindowScreenId(window);
            if (m_autotileHandler->isAutotileScreen(screenId)) {
                // Save pre-autotile geometry before onWindowClosed clears it.
                // When the window is re-added on the target desktop, this preserved
                // geometry is used instead of the current (tiled) frame position.
                m_autotileHandler->savePreAutotileForDesktopMove(windowId);

                // Title-bar state is rule-driven (no autotile decoration claim
                // to release): KWin's off-desktop noBorder reset is corrected on
                // desktop return by updateAllDecorations → resyncWindow for any
                // rule-owned window. onWindowClosed below only clears effect-side
                // tracking (shared with the genuine-close path).
                m_autotileHandler->onWindowClosed(windowId, screenId);
                removeWindowDecoration(windowId);
                qCInfo(lcEffect) << "Window moved off current desktop, removed from autotile:" << windowId;
            }
        }
    });

    // Detect when a window moves between monitors (e.g., "Move to Screen Right").
    // KWin::Window::outputChanged fires once when the window's output property changes.
    // Transfer the window from the old screen's autotile state to the new screen's state,
    // and unsnap any snapped window that crosses screens.
    KWin::Window* kw = w->window();
    if (kw) {
        QPointer<KWin::EffectWindow> safeW = w;
        // Track the window's screen ID so we can detect cross-screen moves for snapping windows
        // (not tracked by the autotile handler's m_notifiedWindowScreens).
        m_trackedScreenPerWindow[w] = getWindowScreenId(w);
        connect(kw, &KWin::Window::outputChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            const QString newScreenId = getWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            m_trackedScreenPerWindow[safeW] = newScreenId;

            // Detect involuntary moves up front: when a monitor drops out
            // (DPMS standby on Wayland, hotplug-unplug) KWin reassigns the
            // windows that were on it to a remaining output and fires
            // outputChanged for each — even though the user did nothing. Both
            // the autotile and snapping paths below must skip these, because
            // routing them through the normal cross-screen logic would either
            // tile a window from the disabled monitor into the active
            // autotile zone (discussion #527) or fire a spurious unsnap.
            // Recovery is owned by the daemon's virtualScreensReconfigured /
            // ScreenChangeHandler debounce, which resettles assignments once
            // the screen change has stopped chattering.
            bool oldScreenStillConnected = false;
            for (const auto* output : KWin::effects->screens()) {
                if (outputScreenId(output) == oldScreenId) {
                    oldScreenStillConnected = true;
                    break;
                }
            }
            const bool involuntaryMove = !oldScreenId.isEmpty()
                && (!oldScreenStillConnected || m_screenChangeHandler->isScreenChangeInProgress());

            // Delegate autotile handling (autotile→autotile, autotile→snapping, etc.)
            // This must run even during drag so the autotile engine removes the
            // window from the old screen's tiling state immediately. The
            // involuntary-move guard is the symmetric partner of the snapping
            // guard further down — before #527, only the snapping path was
            // protected and KWin's orphan-reassignment got mistaken for the
            // window genuinely entering autotile.
            if (!involuntaryMove) {
                m_autotileHandler->handleWindowOutputChanged(safeW);
            }

            // For snapping→snapping cross-screen moves: notify the daemon which
            // decides whether to unsnap based on its own state. If the daemon just
            // assigned this window to the new screen (restore/resnap/snap assist),
            // the stored screen matches and no unsnap occurs. If the user moved
            // the window via "Move to Screen" shortcut, the stored screen differs
            // and the daemon unsnaps.
            // Skip during drag: the drag system owns snap state transitions
            // (float, unsnap, size restore, pre-tile cleanup) and handles them
            // in dragStopped() with richer context.
            // Skip involuntary moves: see the involuntaryMove computation above.
            if (!oldScreenId.isEmpty() && oldScreenId != newScreenId
                && !m_autotileHandler->isAutotileScreen(oldScreenId)
                && !m_autotileHandler->isAutotileScreen(newScreenId) && !m_dragTracker->isDragging()
                && !involuntaryMove) {
                const QString windowId = getWindowId(safeW);
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("windowScreenChanged"),
                    {windowId, newScreenId}, QStringLiteral("cross-screen move"));
            }
        });
        // Virtual screen boundary detection: KWin's outputChanged only fires when
        // the physical monitor changes. Moving a window between virtual screens on the
        // same physical monitor (e.g., A/vs:0 → A/vs:1) is invisible to outputChanged.
        // Detect these crossings via frameGeometryChanged, using the same trackedScreen
        // state as the outputChanged handler above.
        // (The autotile handler has its own detection in slotWindowFrameGeometryChanged;
        // this covers snapping-mode windows which autotile doesn't track.)
        //
        // VS crossing detection uses PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing()
        // (<PhosphorIdentity/VirtualScreenId.h>) — the same predicate used by
        // autotilehandler/tiling.cpp.
        connect(safeW, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted() || m_virtualScreenDefs.isEmpty() || !m_virtualScreensReady) {
                return;
            }
            // Suppress crossing detection while the daemon is moving this window in response
            // to a VS swap/rotate or resnap. The cached m_virtualScreenDefs may still hold
            // pre-rotation regions when the geometry change fires synchronously from
            // applyWindowGeometry, so getWindowScreenId would resolve the new position against
            // stale boundaries and report a phantom crossing.
            if (m_inDaemonGeometryApply) {
                return;
            }
            const QString newScreenId = getWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            if (!PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing(oldScreenId, newScreenId)) {
                return;
            }
            m_trackedScreenPerWindow[safeW] = newScreenId;

            // Skip during drag — the drag system owns state transitions.
            // Autotile drag handles VS transfers via the drag-policy-changed path.
            // Snapping drag handles cross-screen unsnap on drag-stop via the daemon.
            if (m_dragTracker->isDragging()) {
                return;
            }

            // Skip VS detection for autotile-tracked windows — the autotile
            // handler's slotWindowFrameGeometryChanged owns VS crossing for
            // windows it already tracks (m_notifiedWindows). Only untracked
            // windows (snapping-mode entering an autotile VS) need delegation.
            const QString windowId = getWindowId(safeW);
            if (m_autotileHandler->isTrackedWindow(windowId)) {
                return;
            }

            // Delegate autotile handling for untracked cross-VS transitions
            // (snapping→autotile). The autotile handler's own detection only
            // covers windows it already tracks.
            m_autotileHandler->handleWindowOutputChanged(safeW);

            // For snapping→snapping cross-VS moves: notify the daemon
            if (!m_autotileHandler->isAutotileScreen(oldScreenId) && !m_autotileHandler->isAutotileScreen(newScreenId)
                && !m_screenChangeHandler->isScreenChangeInProgress()) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("windowScreenChanged"),
                    {windowId, newScreenId}, QStringLiteral("virtual screen crossing"));
            }
        });

        // Clean up the tracked screen entry when the window is destroyed. Capture the RAW
        // pointer value, not the QPointer: inside a destroyed() slot the QPointer has already
        // been nulled, so removing `safeW` would remove the null key and leave the real entry
        // to leak. The pointer is only ever a lookup key here, never dereferenced, so its
        // value is exactly what remove() needs.
        KWin::EffectWindow* const rawW = safeW;
        connect(safeW, &QObject::destroyed, this, [this, rawW]() {
            m_trackedScreenPerWindow.remove(rawW);
        });

        // Metadata mutations: KWin fires these when an app swaps its class or
        // desktop file after the surface is already mapped. Electron/CEF apps
        // (Emby, some Discord forks) do this mid-session and silently break any
        // daemon state keyed to the first-seen class. Push the latest metadata
        // to the WindowRegistry so consumers query the current value.
        //
        // Per feedback_class_change_exclusion.md: the registry only updates its
        // record. It does NOT retroactively unsnap, re-snap, or re-evaluate
        // rules — that would surprise users. Committed state stays committed.
        auto pushLatest = [this, safeW]() {
            if (safeW && !safeW->isDeleted()) {
                pushWindowMetadata(safeW);
            }
        };
        // Caption changes fire every frame for terminals / browsers; the extended
        // property snapshot (geometry / state flags) doesn't change on a title tick,
        // so refresh the registry's core metadata (title) WITHOUT rebuilding and
        // marshalling the ~20-entry a{sv} each frame. The daemon preserves the
        // existing extended fields when none are sent.
        auto pushCaptionOnly = [this, safeW]() {
            if (safeW && !safeW->isDeleted()) {
                pushWindowMetadata(safeW, /*includeExtended=*/false);
            }
        };
        // Class / desktop-file mutations invalidate the animation rule
        // evaluator's per-window match cache. The cache is keyed on the
        // window's frozen composite id but the cascade resolves against
        // the LIVE windowClass — so without invalidation, a SetOpacity /
        // OverrideAnimation* rule for the post-rename class silently
        // never applies (Electron/CEF/Steam family). pushLatest already
        // refreshes the daemon's WindowRegistry record; mirror that
        // refresh on the effect's local resolver cache. Caption /
        // desktops / activities / role changes don't feed the
        // WindowClass matcher so they don't need the cache drop.
        auto invalidateRuleCache = [this, safeW]() {
            m_shaderManager.animationRuleEvaluator().clearCache();
            // The cache drop alone revives nothing: appearance slots (opacity,
            // tint, border colour) bake into the decoration at
            // updateWindowDecoration time, and the stacking layer is
            // EVENT-driven. Both applied eagerly at window-added against the
            // pre-swap placeholder class — re-drive them here so a rule keyed
            // to the real class applies (and one keyed to the placeholder
            // releases) without waiting for an incidental focus / placement
            // sweep. Decoration re-folds only for an on-desktop window
            // (matching updateAllDecorations' gate); an off-desktop swap is
            // picked up by the desktop-switch rebuild.
            if (safeW && !safeW->isDeleted()) {
                const QString wid = getWindowId(safeW);
                if (safeW->isOnCurrentDesktop()) {
                    updateWindowDecoration(wid, safeW);
                }
                reconcileRuleWindowLayer(wid, safeW);
            }
        };
        connect(kw, &KWin::Window::windowClassChanged, this, pushLatest);
        connect(kw, &KWin::Window::windowClassChanged, this, invalidateRuleCache);
        connect(kw, &KWin::Window::desktopFileNameChanged, this, pushLatest);
        connect(kw, &KWin::Window::desktopFileNameChanged, this, invalidateRuleCache);
        connect(kw, &KWin::Window::captionChanged, this, pushCaptionOnly);
        // Per-window virtual-desktop / activity / role changes also refresh the
        // registry so context-aware rule resolution sees current values. Same
        // record-only contract: no retroactive re-evaluation of committed state.
        connect(kw, &KWin::Window::desktopsChanged, this, pushLatest);
        connect(kw, &KWin::Window::activitiesChanged, this, pushLatest);
        connect(kw, &KWin::Window::windowRoleChanged, this, pushLatest);

        // Diagnostic dump on identity change — but ONLY for class / desktop-file,
        // never caption. CEF/Electron apps (Steam included) map with a
        // placeholder class and swap in the real one here, so re-dumping on
        // those catches the final classification the filters act on. Caption
        // is deliberately excluded: it feeds no filter, and terminals /
        // browsers (and this very tool's progress spinner) rewrite their title
        // every frame — dumping on captionChanged floods the journal with
        // identical blocks. See logWindowDiagnostics().
        auto logIdentityChange = [this, safeW]() {
            if (safeW && !safeW->isDeleted()) {
                logWindowDiagnostics(safeW, "identityChanged");
            }
        };
        connect(kw, &KWin::Window::windowClassChanged, this, logIdentityChange);
        connect(kw, &KWin::Window::desktopFileNameChanged, this, logIdentityChange);
    }

    // Detect drag start/end via KWin's per-window signals instead of polling.
    // windowStartUserMovedResized fires once when an interactive move (or resize) begins;
    // windowFinishUserMovedResized fires once when it ends (button release, Escape, etc.).
    // This eliminates the poll timer that previously scanned the full stacking order at
    // 32ms intervals during drag — a significant source of compositor-thread overhead.
    //
    // NOTE: windowFrameGeometryChanged / windowStepUserMovedResized are intentionally NOT
    // connected for drag tracking. They fire on every pixel of movement, which would flood
    // D-Bus. Cursor position updates are handled event-driven via slotMouseChanged →
    // DragTracker::updateCursorPosition(), throttled to ~30Hz.
    connect(w, &KWin::EffectWindow::windowStartUserMovedResized, this, [this](KWin::EffectWindow* window) {
        m_dragTracker->handleWindowStartMoveResize(window);
        // Latch interactive-resize identity AND the pre-resize frame for the finish
        // handler (see below): KWin clears isUserResize() before
        // windowFinishUserMovedResized fires, so both the move-vs-resize
        // discriminator and the baseline geometry must be captured here, at the
        // start. The geometry feeds the neighbour-reflow report (GitHub #652);
        // m_resizeStartGeometry is only read at finish when this latch identifies a
        // resize, so a plain move leaves it cleared.
        m_resizingWindow = (window && window->isUserResize()) ? window : nullptr;
        m_resizeStartGeometry = QRect();
        if (m_resizingWindow) {
            m_resizeStartGeometry = window->frameGeometry().toRect();
        }
        // window.movement.move shader transition: KWin's interactive move is
        // its own animation system (Window::moveResize via pointer drag), but
        // we layer an effect-side shader for visual feedback.
        // windowStartUserMovedResized doesn't disambiguate move from resize;
        // w->isUserResize() does — interactive resize sets it, plain move
        // leaves it false. Interactive RESIZE deliberately starts NO shader
        // event: it is a held gesture with no discrete before/after until
        // release (the compositor repaints the re-laid content live the whole
        // time), so a crossfade pack has nothing meaningful to play, and the
        // soft-body sim omits KWin's resize edge-lock logic (mesh_sim.cpp) so
        // the move-physics packs have no real story there either. Discrete
        // resizes are covered by the snapIn / layoutSwitch / maximize events.
        // tryBeginShaderForEvent silently no-ops if the user didn't assign a
        // shader to the path.
        if (window && !window->isUserResize()) {
            tryBeginShaderForEvent(window, PhosphorAnimation::ProfilePaths::WindowMove, animationDurationMs());
            // Genuine old-content capture for cross-fade legs: the drag
            // begins with the window ALIVE and its pre-drag content still
            // current, so a move pack that declares uOldWindow gets a real
            // decorated snapshot to fade FROM — matching the drag-snap morph
            // path — instead of leaning on the iHasOldWindow fallback (which
            // collapses the old side to the live content). The !oldSnapshot
            // guard preserves an existing capture on a retargeted transition,
            // mirroring drag_snap; a failed capture clears needsSnapshot and
            // the shader-side fallback covers it.
            // `heldMove`, NOT liveness. window.movement.move is opt-in with no
            // default shader, so the stock config installs nothing here and
            // findTransition would hand back an unrelated leg — most reachably the
            // window.focus leg the click that began this drag installed moments ago.
            // Pinning THAT at progress 1, bumping its generation (killing its
            // teardown timer) and ramping it 1→0 on release plays the focus
            // animation backward after the drop; a maximize pack that declares
            // iFromRect would freeze the window at its pre-drag rect for the whole
            // drag. See ShaderTransition::heldMove.
            if (auto* st = m_shaderManager.findTransition(window); st && st->cached && st->heldMove) {
                if (st->cached->iOldWindowLoc >= 0 && !st->oldSnapshot) {
                    st->needsSnapshot = true;
                }
                // Anchor iFromRect at the grab frame for rect-driven packs.
                // Under the opt-in `move` class, only a pack declaring BOTH
                // move and geometry can reach this leg with iFromRect
                // declared (pure crossfade packs are refused by the
                // resolvedShaderAppliesToEvent gate, and wobble reads no
                // rects), but the anchor keeps such a hybrid correct:
                // unseeded, rect-driven packs derive their drawn rect from
                // iFromRect unconditionally, so the first `durationMs` of the
                // drag would play mix(0-rect, live, t) — the window sweeping
                // in from the screen origin. Seeded at the grab, the ramp is
                // a short catch-up ease toward the live frame and the pinned
                // tail (progress held at 1) draws the live rect exactly as
                // before. The !isValid guard preserves a retargeted
                // transition's original anchor.
                if (st->cached->iFromRectLoc >= 0 && !st->fromGeometry.isValid()) {
                    st->fromGeometry = window->frameGeometry();
                }
                // Re-grab during a release leg: resume from the current
                // (descending) progress rather than snapping back to pinned-1.
                // Freeze the accrued down-ramp and hand it to the decaying
                // re-grab offset, which paintWindow subtracts from the painted
                // progress and ramps to 0 over durationMs. startTimeMs is left
                // ALONE on purpose — rewinding it cannot reconstruct the
                // resumed value once iTime is curve-eased, and does nothing at
                // all for a stateful spring. See ShaderTransition::regrabStartMs.
                // A fresh grab (releaseStartMs still -1) skips this and keeps
                // its normal ramp.
                if (st->releaseStartMs >= 0 && st->durationMs > 0) {
                    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
                    const qreal downP = qMax<qreal>(0.0, qreal(nowMs - st->releaseStartMs) / qreal(st->durationMs));
                    st->regrabDownOffset = qMin<qreal>(1.0, downP);
                    st->regrabStartMs = nowMs;
                    st->releaseStartMs = -1;
                }
                // HELD transition: the drag is open-ended, so the shader
                // stays active (progress clamped at 1) until the release
                // handler below schedules the settle-tail teardown; the
                // duration timer stands down for held transitions. The
                // grab origin anchors iMoveOffset, and the velocity spring
                // integrates from here (see the paint pipeline).
                st->holdUntilRelease = true;
                // Fresh epoch for the (re-)hold: a re-grab inside the prior
                // drag's settle window rides the same-effect short-circuit
                // (beginShaderTransition installs nothing new), so the prior
                // release's tail / safety-cap timer still carries this
                // transition's generation and would fire mid-drag, killing
                // the shader for the rest of the new drag. Bumping here
                // invalidates it. For a fresh install the bump is harmless:
                // the just-scheduled duration timer stands down on the hold
                // flag anyway, and every later consumer captures the live
                // generation at its own schedule time.
                st->generation = ++m_shaderManager.m_shaderTransitionGenerationCounter;
                st->grabOrigin = window->frameGeometry().topLeft();
                st->lastMovePos = st->grabOrigin;
                st->lastMoveSampleMs = -1;
                // Seed the generic soft-body lattice (iMoveMesh) so a
                // mesh-consuming pack (wobble, ...) gets neighbour-coupled
                // physics from the first frame. The grip is the node
                // nearest the cursor at grab; physics constants use KWin's
                // middle preset (per-pack tuning can layer on later).
                if (st->cached->iMoveMeshLoc >= 0 && KWin::effects) {
                    ShaderInternal::initMeshSim(st->meshSim, window->frameGeometry(), KWin::effects->cursorPos(),
                                                st->meshParams);
                }
            }
        }
    });
    connect(w, &KWin::EffectWindow::windowFinishUserMovedResized, this, [this](KWin::EffectWindow* window) {
        // Release a HELD move transition with a settle tail (interactive
        // resize starts no shader transition, see the start handler): the
        // velocity spring decays through zero over the next fraction of a
        // second, letting wobble/tilt shaders relax to rest before the
        // teardown lands. Generation-guarded exactly like the duration
        // timer so an interrupting transition owns its own lifetime.
        if (window) {
            // `heldMove &&` guards the mirror of the drag-start defect: releasing
            // must only ever act on the leg the drag itself installed, never on
            // whatever is live. holdUntilRelease alone is not that test — it is the
            // flag the old drag-start bug wrongly set on an unrelated leg.
            if (auto* st = m_shaderManager.findTransition(window); st && st->heldMove && st->holdUntilRelease) {
                QPointer<KWin::EffectWindow> safeWindow(window);
                if (st->meshSim.initialized) {
                    // Soft-body lattice: hand teardown to the settle gate.
                    // Clearing holdUntilRelease drops the transition into
                    // "active while the lattice still has energy" mode (see
                    // the paint pipeline), so the wobble rings out for as
                    // long as it physically takes rather than a fixed tail.
                    // The timer is only a generous SAFETY cap in case the
                    // sim never reaches its settle threshold.
                    //
                    // Fresh epoch for the handoff: the start-scheduled
                    // duration timer in tryBeginShaderForEvent captured the
                    // install generation and only stands down while
                    // holdUntilRelease is set. On a drag SHORTER than the
                    // nominal duration that timer fires after this clear,
                    // sees a matching generation, and would cut the ring-out
                    // off mid-settle — so bump the generation to invalidate
                    // it. The paint pipeline's expiry teardown captures the
                    // live generation at queue time, so the settle gate and
                    // the safety cap below both own the new epoch.
                    st->holdUntilRelease = false;
                    st->generation = ++m_shaderManager.m_shaderTransitionGenerationCounter;
                    const quint64 myGeneration = st->generation;
                    constexpr int kMeshSettleSafetyCapMs = 4000;
                    QTimer::singleShot(kMeshSettleSafetyCapMs, this, [this, safeWindow, myGeneration]() {
                        if (!safeWindow) {
                            return;
                        }
                        if (const auto* live = m_shaderManager.findTransition(safeWindow);
                            live && live->generation == myGeneration) {
                            endShaderTransition(safeWindow);
                        }
                    });
                } else if (st->releaseStartMs < 0) {
                    // Velocity / trail packs: the springLag decays over the
                    // next fraction of a second, so keep the fixed tail.
                    // holdUntilRelease stays SET here, so the start-scheduled
                    // duration timer keeps standing down and this tail timer
                    // (guarded on the install generation) owns the teardown.
                    // Stamp the release leg: paintWindow ramps the pinned
                    // progress back toward 0 from this moment. The ramp is
                    // scaled by the transition's OWN durationMs (a per-event
                    // duration or an OverrideAnimationTiming rule can differ
                    // from the global default), so the tail timer must grant
                    // exactly that many ms — a shorter tail would tear down
                    // mid-ramp and snap, the artifact the release leg exists
                    // to prevent. The releaseStartMs < 0 guard on this branch
                    // makes a duplicate finish signal a no-op instead of
                    // restarting the ramp and double-scheduling teardown.
                    //
                    // Fold any in-flight RE-GRAB offset into this release rather
                    // than leaving both live. A release during a still-decaying
                    // re-grab leaves paintWindow subtracting two offsets whose
                    // slopes are equal and opposite (+1/durationMs and
                    // -1/durationMs), so they cancel and the progress FREEZES on a
                    // plateau until the re-grab offset expires — the dissolve
                    // visibly stalls before it starts. Rebasing releaseStartMs by
                    // the residual makes `down` start at exactly that residual, so
                    // the ramp is continuous at this frame and descends at the
                    // normal rate; the tail is shortened to match so the teardown
                    // timer still lands when the ramp reaches 0 rather than cutting
                    // it mid-flight.
                    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
                    qreal residual = 0.0;
                    if (st->regrabStartMs >= 0 && st->durationMs > 0) {
                        residual = qBound<qreal>(
                            0.0, st->regrabDownOffset - qreal(nowMs - st->regrabStartMs) / qreal(st->durationMs), 1.0);
                        st->regrabStartMs = -1;
                        st->regrabDownOffset = 0.0;
                    }
                    st->releaseStartMs = nowMs - qint64(residual * st->durationMs);
                    const quint64 myGeneration = st->generation;
                    const int rampMs = qMax(1, qRound((1.0 - residual) * st->durationMs));
                    QTimer::singleShot(rampMs, this, [this, safeWindow, myGeneration]() {
                        if (!safeWindow) {
                            return;
                        }
                        if (const auto* live = m_shaderManager.findTransition(safeWindow);
                            live && live->generation == myGeneration) {
                            endShaderTransition(safeWindow);
                        }
                    });
                }
            }
        }
        const bool wasResize = (window && m_resizingWindow == window);
        m_resizingWindow = nullptr;
        // A floating window the user just RESIZED has a new free size. Persist it
        // immediately into the unified record's shared free geometry (overwrite=true)
        // so the float-back is durable right away — recordFreeGeometry marks the
        // placement store dirty, arming the debounced save. The save-time sweep only
        // folds the live frame shadow into the record on the next dirtying event /
        // shutdown, and a bare resize never marks anything dirty, so without this the
        // new size could be lost on an unclean exit. Resizes never snap, so this can
        // never race the drag→snap pipeline (which owns the move case); guarding on
        // isWindowFloating keeps it to genuinely floated windows.
        if (wasResize && shouldHandleWindow(window)) {
            const QString windowId = getWindowId(window);
            if (!windowId.isEmpty() && isWindowFloating(windowId)) {
                // toRect() (rounding) rather than truncation: fractional-scale
                // outputs leave sub-pixel residue in frameGeometry(), and the
                // other geometry-capture paths round too. Correct for
                // maximize/fullscreen (freeGeometryForCapture) so maximizing a
                // floating window does not clobber its free-float size with the
                // full-monitor rect (this store uses overwrite=true).
                const QRect geom = freeGeometryForCapture(window, QRectF(window->frameGeometry())).toRect();
                if (geom.width() > 0 && geom.height() > 0) {
                    PhosphorProtocol::ClientHelpers::fireAndForget(
                        this, PhosphorProtocol::Service::Interface::WindowTracking,
                        QStringLiteral("storePreTileGeometry"),
                        {windowId, geom.x(), geom.y(), geom.width(), geom.height(), getWindowScreenId(window),
                         /*overwrite=*/true},
                        QStringLiteral("storePreTileGeometry - float resize"));
                }
            }
            // Report the committed resize to the daemon so it can reflow tiled
            // neighbours (GitHub #652). The daemon ignores floating / untracked
            // windows, so this is harmless for the float case handled just above.
            // The enclosing shouldHandleWindow(window) is the effect-side gate
            // (excluded windows never reach here); the daemon then additionally
            // re-validates membership before reflowing.
            notifyWindowResized(window, m_resizeStartGeometry);
        }
        m_dragTracker->handleWindowFinishMoveResize(window);
    });

    // Track when user manually unmaximizes a monocle-maximized window
    connect(w, &KWin::EffectWindow::windowMaximizedStateChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowMaximizedStateChanged);

    // Departure-rect capture for the maximize morph wiring below. KWin
    // guarantees windowMaximizedStateAboutToChange fires before the
    // maximize/restore geometry change (effectwindow.h documents the
    // ordering, and the stock maximize script relies on it the same way),
    // so frameGeometry() here is the rect the window is leaving — the only
    // point the old rect can be read. The state-changed edge below may fire
    // with the destination geometry already applied OR still pending the
    // client's commit (see PendingMaximizeMorph); either way this capture
    // is what anchors the morph's departure. Latest-wins per window: an
    // axis-only intermediate flip overwrites the entry, which is correct —
    // the morph should depart from wherever the window actually was just
    // before the edge we act on. Erased on windowDeleted alongside
    // m_lastFullyMaximized.
    connect(w, &KWin::EffectWindow::windowMaximizedStateAboutToChange, this,
            [this](KWin::EffectWindow* window, bool, bool) {
                if (window) {
                    m_shaderManager.m_preMaximizeFrame.insert(window, window->frameGeometry());
                }
            });

    // window.maximize / window.unmaximize shader transition. Sibling lambda
    // to the AutotileHandler hookup above (autotile drives the snap-back
    // logic; we drive the shader leg).
    //
    // KWin emits windowMaximizedStateChanged once per axis flip — a
    // user-driven left-half-snap → fully-maximize sequence fires twice
    // (vertical-only first, then fully-maximized). Without an edge filter
    // we'd start the WindowMaximize shader for the intermediate state,
    // then immediately install WindowMaximize on the next emission, with
    // the timer-driven teardown of the first racing the install of the
    // second. Track the last fully-maximized state per window and only
    // fire on actual edge transitions.
    connect(w, &KWin::EffectWindow::windowMaximizedStateChanged, this,
            [this](KWin::EffectWindow* window, bool horizontal, bool vertical) {
                if (!window) {
                    return;
                }
                const bool fullyMaximized = horizontal && vertical;
                const bool wasFullyMaximized = m_shaderManager.m_lastFullyMaximized.value(window, false);
                if (fullyMaximized == wasFullyMaximized) {
                    return; // intermediate axis-only flip, no shader
                }
                m_shaderManager.m_lastFullyMaximized.insert(window, fullyMaximized);
                // Drag-restore guard: KWin unmaximizes a window mid interactive
                // move when the user grabs the maximized title bar and pulls
                // ("restore on drag"). The drag already owns the visuals — the
                // windowStartUserMovedResized hookup above installed the
                // window.move shader as a HELD transition — and installing
                // WindowMaximize here would supersede it: the move pack dies
                // mid-drag and a full-screen→cursor morph replays over the
                // pointer. The isUserResize branch is skipped for a different
                // reason: an interactive resize starts NO shader (the start
                // handler gates on !isUserResize), but it is still a held
                // gesture with continuous geometry feedback, so a discrete
                // maximize morph replaying under the pointer would be just as
                // wrong. Skip the shader; the edge tracking above still ran,
                // so the next non-interactive flip fires normally.
                if (window->isUserMove() || window->isUserResize()) {
                    m_shaderManager.m_pendingMaximizeMorph.remove(window);
                    return;
                }
                const QRectF newFrame = window->frameGeometry();
                QRectF preFrame = m_shaderManager.m_preMaximizeFrame.value(window);
                if (preFrame.isEmpty()) {
                    // No capture (window managed after the about-to-change
                    // fired, or a degenerate rect). Fall back to the live
                    // frame: the size test below then defers to the geometry
                    // change, which still carries the real jump.
                    preFrame = newFrame;
                }
                // KWin does NOT guarantee the maximize/restore geometry has
                // been applied when this state signal fires — see the
                // PendingMaximizeMorph docstring for the observed decoupling.
                // Only install here when the size has actually changed
                // (maximizeSizeLanded above); otherwise arm the pending entry
                // and let the size-delivering windowFrameGeometryChanged below
                // complete the install at the visible jump.
                if (maximizeSizeLanded(newFrame, preFrame)) {
                    m_shaderManager.m_pendingMaximizeMorph.remove(window);
                    beginMaximizeShaderMorph(window, preFrame);
                } else {
                    m_shaderManager.m_pendingMaximizeMorph.insert(window,
                                                                  {preFrame, ShaderInternal::shaderClockNowMs()});
                }
            });

    // Track when a monocle-maximized window goes fullscreen
    connect(w, &KWin::EffectWindow::windowFullScreenChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowFullScreenChanged);

    // Autotile: center undersized Wayland windows as soon as they commit constrained size
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowFrameGeometryChanged);

    // Single windowFrameGeometryChanged lambda combining the effect-side
    // per-tick work — first-frame open suppression release AND debounced
    // daemon push — into one connection. Keeping these as two separate
    // connections (which they were originally) doubled the per-geometry-
    // tick lambda dispatch cost without functional benefit; the bodies
    // are independent so collapsing them just runs one capture+vtable
    // hop per tick instead of two. The autotile-handler connection
    // immediately above is kept separate because it dispatches to a slot
    // on a different receiver (`m_autotileHandler.get()`).
    //
    // Body 1 — first-frame open suppression release: a window withheld
    // from compositing on open (see RestoreSuppression) is released the
    // moment its reposition configure lands — detected as the live
    // geometry leaving the spawn point once applyWindowGeometry has
    // stamped the resolved target. Before the target is known a
    // geometry change is just the client's own initial size negotiation
    // and is ignored. Full-rect compare (not just topLeft) catches
    // size-only configures whose origin coincidentally matches the
    // spawn point.
    //
    // Body 2 — frame-geometry shadow: push the latest geometry to the
    // daemon so daemon-local shortcut handlers (float toggle, etc.) can
    // read fresh geometry without round-tripping. Debounced at ~50 ms
    // per window via m_frameGeometryFlushTimer so rapid move/resize
    // sequences collapse into at most one D-Bus push.
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, this,
            [this, safeW = QPointer<KWin::EffectWindow>(w)]() {
                if (!safeW) {
                    return;
                }
                // Body 0 — deferred maximize-morph completion. The maximize
                // state edge above arms this entry when it fires before the
                // client has committed the new size (see PendingMaximizeMorph);
                // the geometry change that actually delivers the size lands
                // here and starts the morph at the visible jump. A
                // position-only step keeps waiting. The deadline discards a
                // stale entry (state flipped but the commit never came — e.g.
                // an occluded client under the lock screen) so a much later
                // unrelated resize cannot fire a bogus maximize animation.
                if (const auto pendingIt = m_shaderManager.m_pendingMaximizeMorph.constFind(safeW.data());
                    pendingIt != m_shaderManager.m_pendingMaximizeMorph.constEnd()) {
                    const auto pending = pendingIt.value();
                    if (maximizeSizeLanded(safeW->frameGeometry(), pending.departureFrame)) {
                        m_shaderManager.m_pendingMaximizeMorph.remove(safeW.data());
                        constexpr qint64 kPendingMaximizeMorphDeadlineMs = 1000;
                        const bool stale =
                            ShaderInternal::shaderClockNowMs() - pending.armedAtMs > kPendingMaximizeMorphDeadlineMs;
                        // Same interactive guard as the arming site: a drag
                        // that started while the entry was pending owns the
                        // visuals through the window.move shader.
                        if (!stale && !safeW->isUserMove() && !safeW->isUserResize()) {
                            beginMaximizeShaderMorph(safeW.data(), pending.departureFrame);
                        }
                    }
                }
                // Body 1 — suppression release
                if (auto it = m_restoreSuppress.find(safeW.data()); it != m_restoreSuppress.end()
                    && it->targetGeometry.isValid() && safeW->frameGeometry() != it->spawnGeometry) {
                    endRestoreSuppression(safeW.data());
                }
                // Body 2 — debounced daemon shadow. Per tick this stashes the
                // latest geometry and runs ONLY the cheap decoration resync:
                // the shouldHandleWindow exclusion gate (an uncached rule
                // resolve over a freshly built ruleQuery) moved into
                // flushPendingFrameGeometry, so it runs once per 50ms flush
                // per window instead of on every geometry tick — animated
                // geometry (retiles, morphs, interactive resize) fired it
                // hundreds of times per second (discussion #816).
                const QString windowId = getWindowId(safeW);
                if (windowId.isEmpty()) {
                    return;
                }
                // Self-heal a noBorder reset KWin issues asynchronously after
                // a cross-OUTPUT move. For a rule-owned (title-bar-hidden)
                // window the manager already believes it hidden, so the
                // synchronous resync in updateAllDecorations bails ("still
                // suppressed") when it runs before KWin re-evaluates the
                // decoration. KWin grows the frame by the title-bar height
                // when it re-decorates, firing this very signal: resyncWindow
                // re-hides exactly the windows the manager owns and believes
                // hidden whose decoration drifted back, and is a self-guarding
                // no-op otherwise. Kept PER TICK, not behind the flush: it is
                // a hash lookup plus two flag checks for the untracked common
                // case, and deferring it to the flush let the re-decorated
                // title bar flash for up to the 50ms throttle window. No
                // shouldHandleWindow gate needed — the manager only ever owns
                // windows that passed it.
                m_decorationManager->resyncWindow(windowId);
                const QRect geo = safeW->frameGeometry().toRect();
                if (geo.width() <= 0 || geo.height() <= 0) {
                    return;
                }
                m_pendingFrameGeometry[windowId] = {geo, safeW};
                if (!m_frameGeometryFlushTimer->isActive()) {
                    m_frameGeometryFlushTimer->start();
                }
            });

    // Autotile: track minimize/unminimize to remove/re-add windows from tiling
    connect(w, &KWin::EffectWindow::minimizedChanged, m_autotileHandler.get(),
            &AutotileHandler::slotWindowMinimizedChanged);

    // Snap mode: track minimize/unminimize to float/unfloat snapped windows
    connect(w, &KWin::EffectWindow::minimizedChanged, this, &PlasmaZonesEffect::slotWindowMinimizedChanged);
}

void PlasmaZonesEffect::beginMaximizeShaderMorph(KWin::EffectWindow* window, const QRectF& departureFrame)
{
    if (!window) {
        return;
    }
    // ALWAYS a forward leg. Geometry packs encode direction in the rects,
    // not the timeline: the zone-snap path (drag_snap.cpp) never reverses
    // either — a shrink into a small zone is a forward morph with a small
    // iToRect. Reversing here split the two geometry-shader families:
    // fragment morphs read raw iTime (flipped → played maximized→restored),
    // but the vertex-grid packs (fold / stretch / flow / ripple-snap) run
    // their motion through legProgress(), which un-flips iTime back to a
    // forward 0→1 — with swapped rects they animated restored→MAXIMIZED
    // while the real window sat restored, then popped at teardown ("gets
    // sized down, then plays an animation"). Forward + natural rects
    // satisfies both families with the same values, and keeps the grid
    // anchoring contract intact (apply() builds the deform grid on
    // iToRect == the live frame).
    tryBeginShaderForEvent(window, PhosphorAnimation::ProfilePaths::WindowMaximize, animationDurationMs(),
                           /*reverse=*/false);
    // Geometry-morph endpoints — sibling of the drag-snap wiring in
    // drag_snap.cpp. window.maximize is a geometry-contract event, so every
    // assignable pack derives its drawn rect from iFromRect/iToRect; leaving
    // them default-invalid pushes zero vec4s and a morph pack masks every
    // fragment outside a 0×0 rect at the origin — the window paints fully
    // transparent for the whole leg and pops in on teardown.
    auto* st = m_shaderManager.findTransition(window);
    if (!st || !st->cached || st->cached->iFromRectLoc < 0) {
        return;
    }
    const QRectF newFrame = window->frameGeometry();
    QRectF preFrame = departureFrame;
    if (preFrame.isEmpty()) {
        // Degenerate departure rect: degrade to a static morph at the live
        // frame — visible, just motionless — rather than the transparent
        // zero-rect.
        preFrame = newFrame;
    }
    // Always retarget the destination; anchor the departure + snapshot only
    // on a fresh morph. A rapid maximize→unmaximize toggle with the same
    // shader lands here while the first leg is still live (same effect,
    // same direction, same timing mode → beginShaderTransition's
    // same-effect short-circuit keeps the prior transition), and the
    // captured snapshot already holds the ORIGINAL content — re-anchoring
    // fromGeometry or re-capturing mid-flight would jump the drawn rect and
    // collapse the cross-fade. Mirrors the drag-snap retarget rule.
    st->toGeometry = newFrame;
    if (!st->oldSnapshot) {
        st->fromGeometry = preFrame;
        // Old-content cross-fade: same guard as the move-start hookup. The
        // raw capture happens on the first paint (post-jump, so it degrades
        // to the live content for undecorated windows), but decorated
        // windows seed from the frozen pre-jump composite — see
        // captureOldWindowSnapshot.
        if (st->cached->iOldWindowLoc >= 0) {
            st->needsSnapshot = true;
        }
    }
}

} // namespace PlasmaZones
