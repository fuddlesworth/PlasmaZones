// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"
#include "shader_internal.h"
#include "compositor/effectlogging.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/RetargetPolicy.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <window.h>

#include <QLoggingCategory>
#include <QPointer>
#include <QScopeGuard>
#include <QTimer>

#include "tilinghandler/tilinghandler.h"
#include "handlers/dragtracker.h"
#include "handlers/screenchangehandler.h"

namespace PlasmaZones {

namespace {
// A pending maximize morph whose size-landing commit arrives later than this
// is considered stale (state flipped but the commit never came, e.g. an
// occluded client under the lock screen) — the morph is skipped so a much
// later unrelated resize cannot fire a bogus maximize animation.
constexpr qint64 kPendingMaximizeMorphDeadlineMs = 1000;

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

    // Idempotency guard. Every connect below uses a lambda slot, which rules out
    // Qt::UniqueConnection, so a second call for the same window would silently
    // double each per-window handler — every geometry change handled twice, every
    // push marshalled twice. The two callers' window sets are disjoint by
    // construction (see the member's declaration), so this never fires today; it
    // is here so that stays true when a third caller appears.
    if (m_wiredWindows.contains(w)) {
        return;
    }
    m_wiredWindows.insert(w);

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
            if (m_tilingHandler->isManagedScreen(screenId)) {
                // Save pre-autotile geometry before onWindowClosed clears it.
                // When the window is re-added on the target desktop, this preserved
                // geometry is used instead of the current (tiled) frame position.
                m_tilingHandler->savePreTileForDesktopMove(windowId);

                // Title-bar state is rule-driven (no autotile decoration claim
                // to release): KWin's off-desktop noBorder reset is corrected on
                // desktop return by updateAllDecorations → resyncWindow for any
                // rule-owned window. releaseWindowTracking, NOT onWindowClosed:
                // the window is alive and merely moving desktops, so the close
                // relay's capture and its ledger append must not fire (the
                // preserved pre-tile geometry above is the state that matters).
                m_tilingHandler->releaseWindowTracking(windowId, screenId);
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
            // Daemon-driven geometry applies must not be mistaken for user
            // moves (symmetric with the frameGeometryChanged VS-crossing
            // handler below). This matters for the scrolling engine: parked
            // columns sit ENTIRELY outside the screen rect, so on a
            // multi-head layout the parked frame's centre can land on the
            // neighbouring output — KWin fires outputChanged and, without
            // this guard, the parked window would be handed to the other
            // screen's engine mid-apply.
            if (m_daemonGate.inGeometryApply) {
                return;
            }
            const QString newScreenId = getWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            m_trackedScreenPerWindow[safeW] = newScreenId;
            // A cross-screen move changes the Mode/screenId inputs of the
            // window's cached rule verdict (tiling vs scrolling screens
            // especially); nothing else invalidates it when the window stays
            // tiled through the move. The invalidation itself is issued
            // below, once the involuntary-move and mid-drag gates have been
            // applied — an unconditional one here bypassed both (and ran the
            // per-window decoration rebuild mid-drag, which the drag deferral
            // exists to avoid).

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
                m_tilingHandler->handleWindowOutputChanged(safeW);
            }

            // A genuine screen change stales this window's cached rule verdict.
            // The verdict cache is keyed on (windowId, rule-set revision) and
            // neither moves here, while ScreenId, ScreenOrientation and (since the
            // ActiveLayout wire) the screen's active layout are all per-screen
            // match inputs — so without this the window keeps matching against the
            // monitor it came FROM, indefinitely, because two monitors sitting on
            // unchanged layouts produce no broadcast to correct it.
            //
            // Gated exactly like the daemon notify below: not for KWin's
            // orphan-reassignment when a monitor drops out, and not mid-drag,
            // where the drag system owns the transitions and the deferred flush's
            // decoration rebuild has not been established as safe.
            //
            // Mid-drag the invalidation is deferred, not dropped: the id goes
            // into m_dragSuppressedRuleInvalidations and callEndDrag drains it
            // once the daemon's outcome has been applied. Nothing at drag end
            // could rediscover the crossing on its own, because the stamp above
            // already made the tracked screen equal to the live one.
            if (!oldScreenId.isEmpty() && oldScreenId != newScreenId && !involuntaryMove) {
                if (m_dragTracker->isDragging()) {
                    m_dragSuppressedRuleInvalidations.insert(getWindowId(safeW));
                } else {
                    invalidateRuleCacheForStateChange(getWindowId(safeW));
                }
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
            if (!oldScreenId.isEmpty() && oldScreenId != newScreenId && !m_tilingHandler->isManagedScreen(oldScreenId)
                && !m_tilingHandler->isManagedScreen(newScreenId) && !m_dragTracker->isDragging() && !involuntaryMove) {
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
        // tilinghandler/tiling.cpp.
        connect(safeW, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this, safeW]() {
            if (!safeW || safeW->isDeleted() || m_virtualScreenDefs.isEmpty() || !m_daemonGate.virtualScreensReady) {
                return;
            }
            // Suppress crossing detection while the daemon is moving this window in response
            // to a VS swap/rotate or resnap. The cached m_virtualScreenDefs may still hold
            // pre-rotation regions when the geometry change fires synchronously from
            // applyWindowGeometry, so getWindowScreenId would resolve the new position against
            // stale boundaries and report a phantom crossing.
            if (m_daemonGate.inGeometryApply) {
                return;
            }
            const QString newScreenId = getWindowScreenId(safeW);
            const QString oldScreenId = m_trackedScreenPerWindow.value(safeW);
            if (!PhosphorIdentity::VirtualScreenId::isVirtualScreenCrossing(oldScreenId, newScreenId)) {
                return;
            }
            m_trackedScreenPerWindow[safeW] = newScreenId;

            // A virtual-screen crossing stales this window's cached rule verdict
            // exactly like the physical cross-screen move above: ScreenId,
            // ScreenOrientation and the screen's active layout are all per-screen
            // match inputs, and the verdict cache is keyed on (windowId, rule-set
            // revision), neither of which moves here. Same gating as the sibling —
            // not mid-drag, where the drag system owns the transitions — and it
            // runs ahead of the autotile / daemon delegation below, which return
            // early for tracked and autotile-screen windows whose verdicts are
            // stale all the same. The sibling's non-empty / differs terms are
            // already guaranteed here by isVirtualScreenCrossing above. Mid-drag
            // the id is parked in m_dragSuppressedRuleInvalidations for
            // callEndDrag to drain, exactly as the sibling does, because the
            // stamp above leaves nothing at drag end to detect the crossing from.
            if (m_dragTracker->isDragging()) {
                m_dragSuppressedRuleInvalidations.insert(getWindowId(safeW));
            } else {
                invalidateRuleCacheForStateChange(getWindowId(safeW));
            }

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
            if (m_tilingHandler->isTrackedWindow(windowId)) {
                return;
            }

            // Delegate autotile handling for untracked cross-VS transitions
            // (snapping→autotile). The autotile handler's own detection only
            // covers windows it already tracks.
            m_tilingHandler->handleWindowOutputChanged(safeW);

            // For snapping→snapping cross-VS moves: notify the daemon
            if (!m_tilingHandler->isManagedScreen(oldScreenId) && !m_tilingHandler->isManagedScreen(newScreenId)
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
            if (!safeW || safeW->isDeleted()) {
                return;
            }
            // Skip content-identical pushes: KWin can emit captionChanged
            // without a net caption change, and the marshal (plus the
            // daemon-side upsert) should not ride those. captionNormal
            // derives from the caption, so an unchanged caption implies an
            // unchanged push.
            const QString caption = safeW->caption();
            QString& last = m_lastPushedCaption[safeW.data()];
            if (last == caption) {
                return;
            }
            last = caption;
            pushWindowMetadata(safeW, /*includeExtended=*/false);
            // A compositor-drawn tab pill shows this caption in the CHIPS
            // style; rebuild the strips that name the window, skipping screens
            // whose bar style never draws it (a chatty terminal title must not
            // re-raster a bar that cannot change). One hash probe when it is
            // not a tab anywhere, which is the common case.
            m_tilingHandler->noteScrollTabTitleChanged(getWindowId(safeW.data()));
        };
        // Class / desktop-file mutations invalidate the animation rule
        // evaluator's per-window match cache. The cache is keyed on the
        // window's frozen composite id but the cascade resolves against
        // the LIVE windowClass — so without invalidation, a SetOpacity /
        // OverrideAnimation* rule for the post-rename class silently
        // never applies (Electron/CEF/Steam family). pushLatest already
        // refreshes the daemon's WindowRegistry record; mirror that
        // refresh on the effect's local resolver cache. Desktop / activity /
        // role changes get their own invalidation connects below.
        //
        // CAPTION is the deliberate exception: Title and CaptionNormal ARE
        // matchable fields stamped live into the query, so a Title-scoped
        // verdict IS knowingly left stale until the next natural invalidation
        // (focus change, placement change, rule edit). A per-caption clear is
        // strictly worse than the staleness: terminals and browsers rewrite
        // their title every frame, and each clear drops the GLOBAL per-window
        // cache — one noisy terminal would cold-start every other window's
        // verdict at title-tick rate.
        auto invalidateRuleCache = [this, safeW]() {
            // Gate each clear on its own rule set, mirroring the sibling
            // invalidation in slotWindowActivated: the no-rules case pays
            // nothing on a class swap.
            if (!m_shaderManager.animationRuleSet().isEmpty()) {
                m_shaderManager.animationRuleEvaluator().clearCache();
            }
            // The verdict cache keys on the same frozen id and matches on
            // WindowClass / AppId just as readily (an Electron/CEF class swap
            // is exactly how a per-app scroll multiplier starts or stops
            // applying), so it takes the same clear.
            if (!m_shaderManager.effectVerdictRuleSet().isEmpty()) {
                m_shaderManager.effectVerdictRuleEvaluator().clearCache();
            }
            // The exclusion verdict caches key on the same frozen id and the
            // WindowClass matcher — a class swap can flip an Exclude verdict.
            if (!m_snappingExclusionRuleSet.isEmpty()) {
                m_snappingExclusionEvaluator.clearCache();
            }
            if (!m_decorationExclusionRuleSet.isEmpty()) {
                m_decorationExclusionEvaluator.clearCache();
            }
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
                // Title-bar override rides the same appearance resolve as the
                // decoration re-fold, but updateWindowDecoration deliberately
                // does not resolve it (decorations.cpp documents the split) —
                // without this, a SetHideTitleBar rule keyed to the real
                // post-swap class waits for the next focus-driven sweep.
                // Outside the desktop gate, matching updateAllDecorations:
                // title-bar state is persistent and survives desktop switches.
                reconcileRuleHiddenTitleBar(wid, safeW);
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
        // VirtualDesktop and Activity are matchable rule fields stamped live
        // into the per-window query, but the verdict caches key on
        // (windowId, ruleSet revision) — neither moves on a desktop or
        // activity move, so a `WHEN VirtualDesktop Equals N` exclusion or
        // appearance verdict would pin stale across the move. Enqueue the
        // coalesced per-window invalidation, mirroring the outputChanged
        // handler; the flush clears the caches and re-drives decoration /
        // title bar / layer for exactly this window.
        auto invalidateForContextMove = [this, safeW]() {
            if (safeW && !safeW->isDeleted()) {
                invalidateRuleCacheForStateChange(getWindowId(safeW));
            }
        };
        connect(kw, &KWin::Window::desktopsChanged, this, invalidateForContextMove);
        connect(kw, &KWin::Window::activitiesChanged, this, invalidateForContextMove);
        // WindowRole is likewise matchable and stamped live; role changes are
        // rare (X11 clients setting WM_WINDOW_ROLE post-map), so the heavier
        // immediate class-swap invalidation is fine here and keeps the
        // identity-change family on one code path.
        connect(kw, &KWin::Window::windowRoleChanged, this, invalidateRuleCache);

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
    connect(w, &KWin::EffectWindow::windowMaximizedStateChanged, m_tilingHandler.get(),
            &TilingHandler::slotWindowMaximizedStateChanged);

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
    // to the TilingHandler hookup above (autotile drives the snap-back
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
    // Seed from the LIVE maximize mode: a window already fully maximized when
    // the effect (re)loads has no entry, so its first RESTORE compared
    // false==false, read as a no-edge, and played no morph.
    if (KWin::Window* kwSeed = w->window()) {
        m_shaderManager.m_lastFullyMaximized.insert(w, kwSeed->maximizeMode() == KWin::MaximizeFull);
    }
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
                // IsMaximized is a matchable rule field with the same
                // cache-key staleness as IsMinimized (see the minimizedChanged
                // metadata lambda below) — invalidate on the genuine
                // full-maximize edge, after the tracking write and before the
                // interactive-gesture early return (the verdict must refresh
                // even when the shader is skipped).
                invalidateRuleCacheForStateChange(getWindowId(window));
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
    connect(w, &KWin::EffectWindow::windowFullScreenChanged, m_tilingHandler.get(),
            &TilingHandler::slotWindowFullScreenChanged);

    // Autotile: center undersized Wayland windows as soon as they commit constrained size
    connect(w, &KWin::EffectWindow::windowFrameGeometryChanged, m_tilingHandler.get(),
            &TilingHandler::slotWindowFrameGeometryChanged);

    // Single windowFrameGeometryChanged lambda combining the effect-side
    // per-tick work: deferred maximize completion, first-frame suppression
    // release, and debounced daemon push. Keeping the latter two as separate
    // connections (which they were originally) doubled the per-geometry-
    // tick lambda dispatch cost without functional benefit; the bodies
    // are independent so collapsing them just runs one capture+vtable
    // hop per tick instead of two. The autotile-handler connection
    // immediately above is kept separate because it dispatches to a slot
    // on a different receiver (`m_tilingHandler.get()`).
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
                // Body -1 — retarget a strip animation onto the rect the
                // client actually committed.
                //
                // Only fires for a client that did not take the geometry it
                // was handed. A programmatic moveResize commits synchronously,
                // so for an ordinary window frameGeometry() already equals the
                // animation's target by the time any leg is running and the
                // isAnimatingToTarget test short-circuits. A client with a
                // size constraint it enforces itself — an aspect ratio, a
                // minimum bigger than its column — negotiates asynchronously
                // and lands somewhere else, usually centred within the rect it
                // was offered. The leg then drove toward the COLUMN rect while
                // the commit sat centred inside it, so the window slid to its
                // column's edge and snapped back on every step scroll.
                //
                // Measured, not predicted: constrainTileGeometry can pre-empt
                // this for X11 (it applies the same constraint KWin will) but
                // is a pass-through for Wayland, where the negotiated size is
                // not knowable up front. Retargeting when the commit arrives
                // needs no prediction and covers both.
                //
                // Scoped to strip members: this is the only path that
                // relocates a window away from its committed rect, so it is
                // the only one where a divergent commit desynchronises the
                // leg from where the window will actually be.
                if (m_windowAnimator->hasAnimation(safeW.data()) && scrollManagedOutputFor(safeW.data())) {
                    const QRectF committed = safeW->frameGeometry();
                    if (!committed.isEmpty() && !m_windowAnimator->isAnimatingToTarget(safeW.data(), committed)) {
                        // PreservePosition: the leg keeps the pixels it is
                        // already showing and bends toward the true rect.
                        // Velocity carried across would re-scale to a
                        // correction that is a few hundred pixels at most and
                        // overshoot it.
                        m_windowAnimator->retargetWithResult(safeW.data(), committed,
                                                             PhosphorAnimation::RetargetPolicy::PreservePosition);
                    }
                }
                // Body -0.5 — centre a client that answered its column with
                // a different size.
                //
                // The strip offers the full column rect the first time it
                // sees a column SIZE, because until the client has answered
                // there is nothing else to offer. A client that will not take
                // that size commits its own at the column's top-left, so a
                // freshly inserted window — or one that just went full width —
                // hugs the top (or the left) until the next batch, which is
                // the first placement able to offer the settled size centred.
                //
                // Correct it here instead of waiting: the commit that just
                // arrived IS the answer, so the centred position is known now.
                // Measured rather than predicted, like every other placement
                // decision on this path — the alternative would be modelling
                // the client's own size rule, which is not knowable up front.
                //
                // A pure move(), so it cannot renegotiate the size it just
                // settled on and cannot be re-anchored by a queued configure.
                // Converges in one step: the guard compares against the
                // position it is about to install, so the synchronous
                // frameGeometryChanged this emits re-enters and does nothing.
                if (!m_daemonGate.inGeometryApply && !m_scrollOfferedColumn.isEmpty()
                    && scrollManagedOutputFor(safeW.data())) {
                    const QString scrollId = getWindowId(safeW.data());
                    const auto colIt = m_scrollOfferedColumn.constFind(scrollId);
                    if (colIt != m_scrollOfferedColumn.constEnd()) {
                        const QRect live = safeW->frameGeometry().toRect();
                        if (live.size() != colIt->size() && !live.size().isEmpty()) {
                            // Same centring as the strip apply and the paint
                            // resolver: the same toRect() rounding, and the
                            // same clamp at zero, so a frame whose minimum
                            // exceeds its column stays anchored at the column's
                            // origin rather than shifting past its edge.
                            //
                            // isEmpty rather than isValid: QSize::isValid()
                            // admits 0x0, which would centre a degenerate
                            // mid-unmap commit by the whole column.
                            const QPoint centred(colIt->x() + qMax(0, colIt->width() - live.width()) / 2,
                                                 colIt->y() + qMax(0, colIt->height() - live.height()) / 2);
                            if (live.topLeft() != centred && safeW->window()) {
                                // Bracketed like every other geometry commit
                                // in the tree. The move emits a synchronous
                                // frameGeometryChanged, which re-enters this
                                // signal's whole connection list from the top
                                // — including the virtual-screen crossing
                                // detector and the autotile reactive centring
                                // pass, both connected ahead of this lambda.
                                // Without the gate they treat a move the effect
                                // itself made as a user-driven one.
                                // Save/restore, not set/clear (nesting-safe).
                                const bool prevInApply = m_daemonGate.inGeometryApply;
                                m_daemonGate.inGeometryApply = true;
                                const auto restoreGate = qScopeGuard([this, prevInApply] {
                                    m_daemonGate.inGeometryApply = prevInApply;
                                });
                                safeW->window()->move(QPointF(centred));
                            }
                        }
                    }
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
                        // The deadline SKIPS the morph for a stale entry; the
                        // entry itself is consumed either way by the remove
                        // above (only a size-landing geometry change reaches
                        // this branch, so a never-landing entry lives until
                        // the windowDeleted cleanup — bounded, and cheaper
                        // than a timer per entry).
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
                // Body 1 — suppression release. Integer-aligned compare:
                // fractional-scale outputs leave sub-pixel residue in
                // frameGeometry(), and a bit-exact inequality released the
                // suppression on jitter that moved nothing.
                if (auto it = m_restoreSuppress.find(safeW.data()); it != m_restoreSuppress.end()
                    && it->targetGeometry.isValid() && safeW->frameGeometry().toRect() != it->spawnGeometry.toRect()) {
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

    // Refresh the daemon's registry metadata on every minimize edge, connected
    // BEFORE the handler connections below. For SNAP the ordering matters on
    // the bus: the handler's float commit rides the same edge, and the push
    // must land first so the daemon's suspension classification reads fresh
    // minimize state. The AUTOTILE handler's float commit is debounced
    // (kMinimizeFloatDebounceMs), so for it the ordering guarantee comes from
    // that delay, not from connection order. The daemon's mode-swap
    // seed/restore decisions consult WindowMetadata::isMinimized, which would
    // otherwise remain at its previous snapshot until an unrelated refresh —
    // a stale value lets a mode-swap seed tile a window that is minimized
    // right now (the per-slot floating check cannot cover this: it resolves
    // via the screen's CURRENT mode, which flips mid-toggle).
    // Liveness-guarded but deliberately NOT gated on shouldHandleWindow /
    // isTileableWindow: the open-time push in slotWindowAdded registers EVERY
    // window, and the daemon's rule predicates (IsMinimized) evaluate against
    // that registry metadata for every window too — a tileable-only gate here
    // would leave non-tileable windows' minimize state permanently stale.
    // Spurious minimize pairs cost only the marshal: the registry upsert
    // de-dupes content-identical pushes.
    connect(w, &KWin::EffectWindow::minimizedChanged, this, [this, safeW = QPointer<KWin::EffectWindow>(w)]() {
        // The minimize edge can race close teardown (the EffectWindow
        // outlives the client as a Deleted shell); pushing metadata for it
        // would resurrect a registry record the close path just removed.
        if (!safeW || safeW->isDeleted()) {
            return;
        }
        pushWindowMetadata(safeW.data());
        // IsMinimized is a matchable rule field stamped live into the
        // per-window query, but the verdict caches key on (windowId, ruleSet
        // revision) — neither moves on a minimize edge, so an
        // `IsMinimized`-scoped exclusion or appearance verdict would pin
        // stale (buildWindowMap consults the placement gate for minimized
        // windows, so the wrong verdict IS produced and cached). The managed
        // paths' float-flip invalidation only covers engine-managed windows
        // and only when the float bit actually flips; this covers every
        // window on every edge, coalesced by the flush.
        invalidateRuleCacheForStateChange(getWindowId(safeW.data()));
    });

    // Autotile: track minimize/unminimize to remove/re-add windows from tiling
    connect(w, &KWin::EffectWindow::minimizedChanged, m_tilingHandler.get(),
            &TilingHandler::slotWindowMinimizedChanged);

    // Snap mode: track minimize/unminimize to float/unfloat snapped windows
    connect(w, &KWin::EffectWindow::minimizedChanged, this, &PlasmaZonesEffect::slotWindowMinimizedChanged);

    // Refresh the registry on every urgency edge, for the same reason as the
    // minimize edge above: WindowMetadata::isDemandingAttention would
    // otherwise sit at whatever the last unrelated push snapshotted, and a
    // stale urgency is worse than none — the tab indicator would keep a tab
    // lit long after the window stopped asking for attention, or never light
    // it at all. The signal lives on KWin::Window, not EffectWindow, so this
    // connection needs the underlying window; a window without one (no
    // KWin::Window backing) simply never reports urgency, which the daemon
    // reads as "not urgent". The EffectWindow is captured weakly; the
    // KWin::Window is only the signal SENDER and is not captured at all, and
    // passing `this` as the context object means Qt drops the connection when
    // either the sender or the effect is destroyed.
    if (KWin::Window* underlying = w->window()) {
        connect(underlying, &KWin::Window::demandsAttentionChanged, this,
                [this, safeW = QPointer<KWin::EffectWindow>(w)]() {
                    if (!safeW || safeW->isDeleted()) {
                        return;
                    }
                    pushWindowMetadata(safeW.data());
                    // Urgency lights a compositor-drawn tab pill; same rebuild
                    // as the caption hook above.
                    m_tilingHandler->noteScrollTabWindowChanged(getWindowId(safeW.data()));
                });
    }
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
    bool ownsMaximizeLeg = false;
    tryBeginShaderForEvent(window, PhosphorAnimation::ProfilePaths::WindowMaximize, animationDurationMs(),
                           /*reverse=*/false, /*holdCloseGrab=*/false, /*holdAddedGrab=*/false,
                           /*animateMinimized=*/false, &ownsMaximizeLeg);
    // Geometry-morph endpoints — sibling of the drag-snap wiring in
    // drag_snap.cpp. window.maximize is a geometry-contract event, so every
    // assignable pack derives its drawn rect from iFromRect/iToRect; leaving
    // them default-invalid pushes zero vec4s and a morph pack masks every
    // fragment outside a 0×0 rect at the origin — the window paints fully
    // transparent for the whole leg and pops in on teardown.
    //
    // Gate on the IDENTITY verdict, not on findTransition liveness: with no
    // window.maximize pack assigned, tryBeginShaderForEvent installs nothing
    // and findTransition hands back whatever unrelated leg is in flight
    // (window.open on a self-maximizing app is the reachable case). Writing
    // morph endpoints onto that leg re-anchors its drawn rect mid-flight and
    // — for a non-morph leg — switches it into morph mode. Same rule as the
    // heldMove stamp on the drag path.
    if (!ownsMaximizeLeg) {
        return;
    }
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
