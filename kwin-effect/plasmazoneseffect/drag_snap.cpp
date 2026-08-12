// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include "tilinghandler/tilinghandler.h"
#include "handlers/dragtracker.h"
#include "handlers/navigationhandler.h"
#include "handlers/snaphandler.h"
#include "compositor/windowanimator.h"
#include "shader_resolve.h"
#include "window_query.h"

#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/DragMarshalling.h>

#include <effect/effecthandler.h>
#include <window.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QPointer>

#include <memory>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

bool PlasmaZonesEffect::borderActivated(KWin::ElectricBorder border)
{
    Q_UNUSED(border)
    // We no longer reserve edges, so this callback won't be triggered by our effect.
    // The daemon handles disabling Quick Tile via KWin config.
    return false;
}

// The kwin-effect no longer calls the legacy dragStarted D-Bus method;
// beginDrag sets up snap-path state internally on the daemon side, so
// there's only one code path into the drag state machine. The dragMoved
// lambda sends updateDragCursor directly via ClientHelpers::fireAndForget.
// callEndDrag (the drag-end outcome dispatch) lives in drag_end.cpp.

void PlasmaZonesEffect::tryAsyncSnapCall(const QString& interface, const QString& method, const QList<QVariant>& args,
                                         QPointer<KWin::EffectWindow> window, const QString& windowId,
                                         bool storePreSnap, std::function<void()> fallback,
                                         std::function<void(const QString&, const QString&)> onSnapSuccess,
                                         bool skipAnimation, std::function<void()> onComplete,
                                         std::function<void()> onError)
{
    QDBusPendingCall call = PhosphorProtocol::ClientHelpers::asyncCall(interface, method, args);
    auto* watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, window, windowId, storePreSnap, method, fallback, onSnapSuccess, args, skipAnimation, onComplete,
             onError](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<int, int, int, int, bool> reply = *w;
                if (reply.isError()) {
                    qCDebug(lcEffect) << method << "error:" << reply.error().message();
                    if (onError)
                        onError();
                    else if (fallback)
                        fallback();
                    if (onComplete)
                        onComplete();
                    return;
                }
                if (reply.argumentAt<4>() && (!window || window->isDeleted())) {
                    // The daemon DID resolve/commit — the window just died in
                    // flight. This is not a restore miss: onMiss/fallback
                    // would drop restart-candidate state a same-app reopen
                    // may still need. Nothing to apply; just complete.
                    if (onComplete)
                        onComplete();
                    return;
                }
                if (reply.argumentAt<4>() && window && !window->isDeleted()) {
                    QRect geo(reply.argumentAt<0>(), reply.argumentAt<1>(), reply.argumentAt<2>(),
                              reply.argumentAt<3>());
                    qCInfo(lcEffect) << method << "snapping" << windowId << "to:" << geo;
                    if (storePreSnap)
                        // `window` is non-null inside this branch (guarded by
                        // the `reply.argumentAt<4>() && window` check above),
                        // so frameGeometry() needs no null-guard here.
                        m_snapHandler->ensurePreSnapGeometryStored(window, windowId, QRectF(window->frameGeometry()));
                    applyWindowGeometry(window, geo, false, skipAnimation);
                    // Async snap (keyboard / empty-zone / last-zone / auto-fill)
                    // committed — record in snapping's border set, but only for
                    // a resolved snap-mode screen (autotile windows are tracked
                    // by TilingHandler; an empty screen is left untracked,
                    // mirroring the batch path's discriminator).
                    if (const QString asyncScr = getWindowScreenId(window);
                        !asyncScr.isEmpty() && !m_tilingHandler->isManagedScreen(asyncScr)) {
                        // Defensive stale-float clear — see the drag-drop
                        // commit path; idempotent vs the daemon broadcast.
                        m_navigationHandler->setWindowFloating(windowId, false);
                        m_snapHandler->markWindowSnapped(windowId, asyncScr);
                        // Floating → snapped changes the Mode / IsSnapped rule
                        // match fields. Invalidate the per-window match cache so a
                        // placement-scoped border / opacity rule re-resolves now,
                        // rather than waiting for the daemon's windowStateChanged
                        // broadcast (self-contained, mirrors the autotile path).
                        invalidateRuleCacheForStateChange(windowId);
                    } else {
                        // Same discriminator epilogue as the other commit
                        // paths: drop stale snap tracking instead of skipping.
                        m_snapHandler->clearWindowSnapped(windowId);
                        // Symmetric with the snap-tracked branch: re-resolve rules.
                        invalidateRuleCacheForStateChange(windowId);
                    }
                    // args[1] is screenId (e.g. for snapToEmptyZone, snapToLastZone)
                    if (onSnapSuccess && args.size() >= 2) {
                        onSnapSuccess(windowId, args[1].toString());
                    }
                    if (onComplete)
                        onComplete();
                    return;
                }
                if (fallback)
                    fallback();
                if (onComplete)
                    onComplete();
                return;
            });
}

void PlasmaZonesEffect::repaintSnapRegions(KWin::EffectWindow* window, const QRectF& oldFrame, const QRect& newGeo)
{
    // Null-guarded beside the KWin::effects guard below: every current call
    // site passes a checked pointer, but the bare deref two lines above a
    // teardown guard read as an oversight and costs nothing to close.
    if (!window) {
        return;
    }
    window->addRepaintFull();
    // Guard the global compositor repaint requests: this method can run
    // from late D-Bus reply callbacks (callEndDrag → applySnap → here)
    // that may dispatch during compositor teardown, when KWin::effects
    // has been torn down. The window-local addRepaintFull above is
    // safe because the EffectWindow itself is alive (we hold a
    // QPointer-checked reference at the call site).
    if (KWin::effects) {
        if (oldFrame.isValid()) {
            KWin::effects->addRepaint(KWin::Rect(oldFrame.toAlignedRect()));
        }
        KWin::effects->addRepaint(KWin::Rect(newGeo));
    }
}

QRect PlasmaZonesEffect::constrainTileGeometry(KWin::EffectWindow* window, const QRect& geometry) const
{
    // For X11/XWayland windows, KWin constrains the frame size to align with
    // WM_SIZE_HINTS (size increments for terminals like Ghostty, Kitty, etc.;
    // fixed-size hints for game launchers). Pre-compute the constrained size
    // and center the window in its zone so the gap is distributed evenly
    // instead of all at the bottom-right.
    // This applies to all snap operations (zone snap, autotile, resnap, etc.).
    // Wayland-native clients negotiate size async (constrainFrameSize only
    // checks min/max, not char-cell grid), so they're handled by the deferred
    // check in slotWindowFrameGeometryChanged().
    //
    // Split out of applyWindowGeometry so the scrolling batch path can predict
    // the rect a tile request will REQUEST of KWin: its animation origins and
    // the degenerate-leg comparisons must be built against that, not the raw
    // column rect — the mismatch drew a fixed-size X11 game at its column's
    // top-left (the top of the screen) for the length of every park and
    // arrival.
    //
    // "Predict" is the honest word, not "commit". Two known divergences, both
    // harmless for the callers as written: a Wayland client passes through
    // here untouched and negotiates its own size asynchronously (a min size
    // larger than the column lands elsewhere), and on a scaled XWayland output
    // KWin round-trips the request through device pixels, so the committed
    // frame is this rect rounded. Every consumer is an intersects() test or an
    // animation endpoint, none an exact equality, so neither divergence bites
    // — but do not add an equality comparand without revisiting this.
    //
    // Idempotent: the shift below is gated on the size actually changing, and
    // KWin's own constrainFrameSize is a fixed point (clamp to [min,max], then
    // floor to base + n*increment), so re-constraining an already constrained
    // rect takes no branch and returns it unchanged. The deferred user-move
    // replay depends on that — it re-enters applyWindowGeometry with a rect
    // this function already produced.
    if (!window || !window->isX11Client()) {
        return geometry;
    }
    KWin::Window* kw = window->window();
    if (!kw) {
        // Fails open to the unconstrained rect, which for the scroll path is
        // the pre-fix prediction (the column rect) — worth a trace, because a
        // null KWin::Window behind a live X11 EffectWindow is anomalous rather
        // than routine, and the symptom it produces is the very jump this
        // function exists to prevent.
        qCDebug(lcEffect) << "constrainTileGeometry: no KWin window behind X11 client, predicting the raw rect"
                          << geometry;
        return geometry;
    }
    QRect geo = geometry;
    const QSizeF constrained = kw->constrainFrameSize(QSizeF(geo.size()));
    const int cw = qRound(constrained.width());
    const int ch = qRound(constrained.height());
    if (cw != geo.width() || ch != geo.height()) {
        // BOTH directions, not shrink-only: a min size larger than
        // the zone previously skipped this branch entirely, so KWin
        // committed the constrained-larger frame unanchored while
        // every downstream comparand held the requested rect. Apply
        // the constrained size here so the pre-computed geo matches
        // what KWin will commit. Clamp the centring shift to
        // non-negative: when min-size exceeds the zone in a
        // dimension the window stays anchored at the zone's origin
        // rather than shifting past its edge.
        const int dx = qMax(0, geo.width() - cw) / 2;
        const int dy = qMax(0, geo.height() - ch) / 2;
        geo = QRect(geo.x() + dx, geo.y() + dy, cw, ch);
        qCDebug(lcEffect) << "Pre-centered X11 window with size constraints:"
                          << "zone=" << geometry.size() << "constrained=" << constrained << "adjusted=" << geo;
    }
    return geo;
}

void PlasmaZonesEffect::applyWindowGeometry(KWin::EffectWindow* window, const QRect& geometry, bool allowDuringDrag,
                                            bool skipAnimation, const QString& profilePath,
                                            const QRectF& originOverride, const QRectF& visualTargetOverride)
{
    if (!window) {
        qCWarning(lcEffect) << "applyGeometry: window is null";
        return;
    }

    // Normalize so width/height are non-negative; reject invalid rects
    QRect geo = geometry.normalized();
    if (!geo.isValid() || geo.width() <= 0 || geo.height() <= 0) {
        qCWarning(lcEffect) << "applyGeometry: invalid or empty geometry:" << geometry;
        // Release the open-restore suppression on the way out, like the
        // fullscreen bail and the already-at-target skip below. Nothing is
        // going to reposition this window, so a suppressed one would be
        // withheld from compositing until the hard deadline for nothing.
        endRestoreSuppression(window);
        return;
    }

    // Don't call moveResize() on fullscreen windows, it can crash KWin.
    // See KDE bugs #429752, #301529, #489546 (X11-era; moveResize on a
    // fullscreen window is spike-verified safe on KWin >= 6.7).
    //
    // Two narrow exemptions, both keyed on KWin's REQUESTED state (the
    // committed isFullScreen() lags a client round-trip):
    //   - a window in scrolling WINDOWED FULLSCREEN holds fullscreen state
    //     at its column rect on purpose — geometry applies ARE the feature,
    //     and every re-apply path (screen change, daemon retile) must keep
    //     working or KWin's ensureSpecialStateGeometry clobber wins;
    //   - a window whose fullscreen was just requested OFF (the windowed-
    //     fullscreen exit) would otherwise have its restoring batch rect
    //     swallowed while the committed state drains.
    if (window->isFullScreen()) {
        KWin::Window* kwFs = window->window();
        const bool requestedFullScreen = !kwFs || kwFs->isRequestedFullScreen();
        // isEmpty() fast path for sessions that never use the feature, and
        // the isDeleted() term, both the structural predicate's gate style:
        // getWindowId on a corpse would re-insert the reverse-map entry
        // buildWindowMap deliberately skips.
        const bool windowedFsMember = !m_windowedFullscreenWindows.isEmpty() && !window->isDeleted()
            && m_windowedFullscreenWindows.contains(getWindowId(window));
        if (requestedFullScreen && !windowedFsMember) {
            qCDebug(lcEffect) << "applyGeometry: window is fullscreen, skipping";
            // Release the hold-suppression on this bail like the no-op skip
            // below does: no reposition is coming at all, so a suppressed
            // window would be withheld from compositing until the hard
            // 250 ms deadline for nothing.
            endRestoreSuppression(window);
            return;
        }
    }

    // For X11/XWayland windows, pre-compute the size KWin will actually commit
    // and center it in the zone — see constrainTileGeometry.
    geo = constrainTileGeometry(window, geo);

    // If this window is held invisible until it is repositioned on open
    // (first-frame suppression — see RestoreSuppression), stamp the
    // resolved rect as its settle target. The windowFrameGeometryChanged
    // handler treats the next geometry change as the real reposition (not
    // the client's own initial sizing) only once this target is set.
    if (auto supIt = m_restoreSuppress.find(window); supIt != m_restoreSuppress.end()) {
        supIt->targetGeometry = geo;
    }

    // Skip no-op: if window is already at the target geometry AND there is
    // no in-flight animation, calling moveResize() is redundant and can have
    // subtle stacking side effects on some KWin versions (e.g. during daemon
    // restart double-processing).
    //
    // When an animation IS in flight, frameGeometry() already reflects the
    // committed target from the previous applyWindowGeometry's moveResize —
    // but the visual position is still mid-transition. A rapid reversal
    // (float → unfloat, rotate → rotate back) legitimately targets the same
    // committed geometry and must NOT be skipped, because the animation needs
    // to play from the current visual position to that target.
    // Compare integer-aligned rects: `frameGeometry()` carries qreal
    // precision and on fractional-scale outputs may keep sub-pixel residue
    // from prior moveResize commits, so a float-bit-exact equality against
    // an integer `geo` would silently miss and run a redundant moveResize.
    if (geo == window->frameGeometry().toRect() && !m_windowAnimator->hasAnimation(window)) {
        qCDebug(lcEffect) << "moveResize: window already at target geometry, skipping:" << geo;
        // Release first-frame open suppression here. The settle-detection
        // hook on windowFrameGeometryChanged would otherwise wait forever
        // for a configure that never fires (the resolved zone equals the
        // spawn position — happens on KWin session restore where the
        // saved geometry already matches a snap zone). Hold-suppression
        // exists only to mask the placement→reposition flash; with no
        // reposition coming, the window must paint immediately.
        endRestoreSuppression(window);
        return;
    }

    // INFO level: a standing record of every resolved window placement.
    // Generally useful operationally, and the resolved pixel rect is the one
    // number a support report needs to diagnose zone-geometry bugs (the zone
    // id is logged elsewhere; the resolved rect previously was not). Mirrors
    // the autotile path, which already logs "Autotile tile request: QRect=".
    qCInfo(lcEffect) << "Setting window geometry from" << window->frameGeometry() << "to" << geo;

    // Capture old frame before moveResize for repaint region.
    const QRectF trueOldFrame = window->frameGeometry();
    // The animation's departure rect. Identical to the true frame except for a
    // scrolling strip tile, whose parked position is chosen for safety and so
    // says nothing about which edge it should appear to come from — see the
    // originOverride contract on the declaration. Only the ANIMATION uses
    // this; the repaint region below must keep the true frame, or the pixels
    // the window actually vacated never get repainted.
    const QRectF oldFrame = originOverride.isValid() ? originOverride : trueOldFrame;

    // In KWin 6, we use the window's moveResize methods
    // When allowDuringDrag is false: defer if window is in user move/resize (snap on release)
    // When allowDuringDrag is true: apply immediately (snap-on-hover during drag)
    if (!allowDuringDrag && (window->isUserMove() || window->isUserResize())) {
        qCDebug(lcEffect) << "Window in user move/resize, deferring geometry via windowFinishUserMovedResized";
        QPointer<KWin::EffectWindow> safeWindow = window;
        // Snapshot the batch-supersession context at defer time: the fire can
        // land arbitrarily later (the user keeps dragging), and replaying the
        // old rect after a NEWER per-screen batch repositioned things would
        // clobber it — or, after the drag crossed screens, teleport the
        // window back to the old screen's rect.
        const QString deferScreen = getWindowScreenId(window);
        const uint64_t deferGen = m_daemonGate.batchGenByScreen.value(deferScreen);
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn =
            connect(window, &KWin::EffectWindow::windowFinishUserMovedResized, this,
                    [this, safeWindow, geo, skipAnimation, profilePath, conn, deferScreen, deferGen, originOverride,
                     visualTargetOverride](KWin::EffectWindow*) {
                        disconnect(*conn);
                        if (!safeWindow || safeWindow->isDeleted()) {
                            return;
                        }
                        // Same predicate as the top-of-function fullscreen
                        // bail, exemptions included: a deferred apply for a
                        // windowed-fullscreen member must not be silently
                        // dropped by a plain isFullScreen() test the entry
                        // bail was deliberately opened for.
                        if (safeWindow->isFullScreen()) {
                            KWin::Window* kwFs = safeWindow->window();
                            const bool requestedFullScreen = !kwFs || kwFs->isRequestedFullScreen();
                            const bool windowedFsMember = !m_windowedFullscreenWindows.isEmpty()
                                && m_windowedFullscreenWindows.contains(getWindowId(safeWindow.data()));
                            if (requestedFullScreen && !windowedFsMember) {
                                // Same release the synchronous fullscreen bail
                                // does: this replay is the reposition, and it
                                // is not happening.
                                endRestoreSuppression(safeWindow.data());
                                return;
                            }
                        }
                        const QString nowScreen = getWindowScreenId(safeWindow.data());
                        if (nowScreen != deferScreen || m_daemonGate.batchGenByScreen.value(deferScreen) != deferGen) {
                            qCDebug(lcEffect) << "Deferred geometry superseded (screen or batch changed), dropping:"
                                              << getWindowId(safeWindow.data());
                            endRestoreSuppression(safeWindow.data());
                            return;
                        }
                        // Re-assert the self-caused-frame-change guard the
                        // original (batch) apply held — without it the
                        // synchronous frame change from this moveResize
                        // reads as an external move and can report a
                        // phantom cross-VS unsnap.
                        // Save/restore, not set/clear (nesting-safe).
                        const bool prevInApply = m_daemonGate.inGeometryApply;
                        m_daemonGate.inGeometryApply = true;
                        const auto guard = qScopeGuard([this, prevInApply] {
                            m_daemonGate.inGeometryApply = prevInApply;
                        });
                        // Forward BOTH scroll overrides: dropping them replayed a
                        // leaving column as a direct animate-to-park, sweeping it
                        // backwards across the screen — the exact artifact the
                        // override split exists to prevent. They are frame-relative
                        // snapshots from defer time; the batch-generation guard above
                        // already dropped the replay if anything moved since.
                        applyWindowGeometry(safeWindow, geo, false, skipAnimation, profilePath, originOverride,
                                            visualTargetOverride);
                    });
        return;
    }

    // Animation: moveResize to the final geometry immediately, then morph
    // the window visually from its old position/size to the new one using
    // translate + scale in paintWindow(). This follows the standard KDE
    // effect pattern — effects are visual overlays, never per-frame moveResize.
    //
    // `shouldAnimateWindow` adds the user's per-animation Window
    // Filtering gate (transient / min-size / app / class) and lets a
    // Rule carrying any effect-consumed (Tag::Effect) action override
    // the filter when the rule's match expression resolves. Falling through to
    // the non-animated path just runs the moveResize without the snap
    // motion / shader.
    //
    // BUT never let the geometry morph supersede an in-flight
    // window.open animation. A window that is snapped / placed AS IT OPENS
    // (snap-restore, autotile, daemon placement) should show its OPEN animation
    // at the snapped position, not a snap morph — otherwise the geometry morph
    // (the snap default) installs over the just-started open transition
    // and the open animation never plays. The open transition holds the
    // WindowAddedGrabRole (addedGrabHeld), so detect it and fall through to the
    // instant-moveResize path below: the window jumps to its snapped geometry and
    // the open animation plays over it. A snap that is NOT on a freshly-opened
    // window (drag-snap, retile, focus move) has no such transition and morphs
    // normally.
    const ShaderTransition* const inFlight = m_shaderManager.findTransition(window);
    const bool openAnimationInFlight = inFlight && inFlight->addedGrabHeld;
    // Caller-owned memoisation slot: when the gate builds the WindowQuery for
    // its rule probes, the resolver pass below reuses it instead of walking
    // the ~30 accessors a second time per animated apply.
    std::optional<PhosphorRules::WindowQuery> sharedQuery;
    if (!skipAnimation && !allowDuringDrag && !openAnimationInFlight && m_windowAnimator->isEnabled()
        && shouldAnimateWindow(window, &sharedQuery)) {
        const QRectF targetFrame(geo);
        // Where the window is COMMITTED (targetFrame) versus where the motion
        // is seen to END (animTarget). Identical unless the caller split them
        // — see visualTargetOverride on the declaration. Everything the
        // animator and the shader morph touch below uses animTarget; only the
        // moveResize uses targetFrame.
        const QRectF animTarget = visualTargetOverride.isValid() ? visualTargetOverride : targetFrame;

        // Bail before any work when the in-flight animation already
        // targets this frame — saves both the moveResize signal
        // emission AND the rule resolve on rapid retargets to the same
        // zone. Pre-Pass-2 the moveResize ran first and was redundant
        // here (kwin's moveResize is internally a no-op when geometry
        // already matches, but still pays signal-dispatch cost on the
        // hot path of rapid drag retargets).
        //
        // When the caller SPLIT the visual target from the committed frame
        // (leaving scroll columns), the bail must ALSO require the committed
        // frame to already match: two successive leaving-column batches can
        // share an animTarget (derived from the screen edge and the window's
        // size) while carrying DIFFERENT park rects — bailing on animTarget
        // alone would skip the moveResize and strand the committed geometry
        // at the previous park. Without a split, animTarget IS geo and the
        // extra term is deliberately not evaluated: frameGeometry() can lag
        // a size-changing moveResize until the client acks, and a defeated
        // bail would fall through to a retarget that re-anchors the running
        // animation on every rapid identical retarget.
        if (m_windowAnimator->hasAnimation(window) && m_windowAnimator->isAnimatingToTarget(window, animTarget)
            && (!visualTargetOverride.isValid() || window->frameGeometry().toRect() == geo)) {
            return; // Already animating to this target (with the frame committed, when split)
        }

        // Apply final geometry immediately — client starts re-rendering at new size.
        // Do this before touching the animator so the controller's
        // downstream bounds / padding queries see the updated
        // expandedGeometry for this frame.
        KWin::Window* kw = window->window();
        if (kw) {
            kw->moveResize(targetFrame);
        }

        // Per-window animation motion-cascade: rule → per-event motion node
        // (incl. the `window.movement` "All") → global animator profile. A
        // Timing Rule for this (windowClass, eventPath) wins; below it, the
        // motion ProfileTree's per-event / "All" duration override applies;
        // the global animator profile is the floor. Retarget intentionally does
        // not re-apply the cascade — once an animation is in flight, it
        // stays on the curve that started it for visual continuity.
        //
        // Reuse the gate's query when it built one (rules present); build
        // only when the gate's fast paths never needed it — matches the shape
        // `shouldAnimateWindow` uses for its rule-override gate, so a rule
        // that gates the animation also resolves its curve / timing / shader
        // slots.
        const PhosphorRules::WindowQuery query = sharedQuery ? *sharedQuery : ruleQuery(window);
        const QString windowId = getWindowId(window);
        const auto& baseProfile = m_windowAnimator->profile();
        // Resolve the fully-cascaded motion profile for this event (curve +
        // duration): global animator profile → category "All" → per-node
        // motion-tree override → per-window Rule. Shared SSOT with the
        // time-driven shader path (tryBeginShaderForEvent), so an autotile
        // rotate / mode-change / snap reposition animates on the SAME per-event
        // curve + duration the user configured — including a `window.movement`
        // "All" override. The WindowAnimator consumes the whole profile, so the
        // per-event curve rides along; without this the morph always used the
        // global animator profile.
        //
        // Gated on a non-empty tree OR rule set so the default-state user keeps
        // the historical fast-path — no resolve, no deep `Profile::operator!=`
        // (which walks `curve->equals` virtual + 5 std::optional comparisons).
        // Compared against the animator's own `baseProfile` so the override is
        // passed whenever the effective profile differs from what the animator
        // would use unaided.
        const bool hasMotionOverrides = m_shaderManager.motionProfileTree().hasAnyOverride();
        const bool hasAnimationRules = !m_shaderManager.animationRuleSet().isEmpty();
        const PhosphorAnimation::Profile* motionOverridePtr = nullptr;
        PhosphorAnimation::Profile motionProfile;
        if (hasMotionOverrides || hasAnimationRules) {
            motionProfile = resolveEventMotionProfile(profilePath, query, windowId);
            if (motionProfile != baseProfile)
                motionOverridePtr = &motionProfile;
        }

        // Where the animator's replacement animation departs FROM. The
        // shader geometry-morph below must anchor its iFromRect at the
        // same point (see the re-anchor branch there): the animator's
        // retarget resets progress to 0 and re-anchors at this rect, so a
        // morph that keeps its old fromGeometry would draw
        // lerp(originalFrom, newTo, 0) on the next frame — a visible jump
        // back to the ORIGINAL departure rect on every rapid successive
        // move (discussion #795).
        QRectF morphAnchor(oldFrame);
        if (m_windowAnimator->hasAnimation(window)) {
            // Capture the displaced animation's endpoints before retarget
            // modifies or deletes the entry. On a rapid reversal where
            // advance() hasn't ticked, m_current still equals m_from
            // (the animation's start point), so retarget(newTarget) sees
            // current ≈ newTarget when the reversal goes back to the
            // original zone — degenerate. Use the displaced animation's
            // TARGET as the visual origin for the replacement: that's
            // where the window was visually heading (and where moveResize
            // just committed to), so animating from there to the new
            // target matches the user's expectation.
            const QRectF displacedTarget = m_windowAnimator->animationFor(window)->to();
            const QRectF visualPos = m_windowAnimator->currentValue(window, QRectF(oldFrame));
            const auto result = m_windowAnimator->retargetWithResult(
                window, animTarget, PhosphorAnimation::RetargetPolicy::PreserveVelocity);
            morphAnchor = visualPos;
            if (result == PhosphorAnimation::RetargetResult::DegenerateReap) {
                // Retarget collapsed (current visual ≈ new target). The reap
                // already dropped the displaced animation; start a fresh one
                // from the displaced target (where the window was heading) to
                // the new target. If that's also degenerate (same point),
                // startAnimation returns false and no animation plays — correct,
                // since there's no visual distance to cover. In that no-replay
                // sub-case the `hasAnimation` block below is skipped, so no new
                // shader morph is anchored; any morph from the reaped animation
                // is left to the shader manager's own reconciliation, the same
                // as the reap/replace paths in WindowAnimator. morphAnchor is
                // still set so that, when a replacement DOES play, its iFromRect
                // matches the animator's re-anchored departure point.
                const QRectF animFrom = (displacedTarget != animTarget) ? displacedTarget : visualPos;
                m_windowAnimator->startAnimation(window, animFrom, animTarget, motionOverridePtr);
                morphAnchor = animFrom;
            }
        } else {
            m_windowAnimator->startAnimation(window, QRectF(oldFrame), animTarget, motionOverridePtr);
        }

        if (m_windowAnimator->hasAnimation(window)) {
            // Same cascade as tryBeginShaderForEvent: rule layer wins
            // for matching windows; engaged-empty rule effectId blocks
            // the tree fallthrough. Reuse the `query` local from the
            // motion-cascade above instead of rebuilding the WindowQuery.
            //
            // Route through `resolveAnimationShaderProfile` (which
            // uses `evaluator.resolveCached(windowId, query)`). When a rule
            // set is configured, the sister `resolveEventMotionProfile`
            // call above already warmed the per-window cache slot for this
            // query, so this cached read is a hit. (An empty rule set still
            // goes through resolveCached, but its walk over zero rules is
            // trivially cheap and the cache slot dedups it.) The earlier shape
            // called a standalone uncached shader-profile resolver here, which
            // paid an extra priority-order walk per snap on every
            // non-empty rule set — same regression the shim was
            // introduced to fix for `tryBeginShaderForEvent` (see the
            // historical-pair note in shader_resolve.cpp).
            //
            // The duration field is intentionally discarded: the snap
            // shader path leaves durationMs at zero on purpose —
            // paintWindow rides the WindowAnimator's timeline. The
            // Timing-rule duration override is honoured transitively
            // via `motionProfile` above (driving the animator's
            // duration), so the shader still terminates with the
            // rule-overridden snap motion.
            const auto resolved = PlasmaZones::resolveAnimationShaderProfile(
                m_shaderManager.animationRuleEvaluator(), m_shaderManager.profileTree(), windowId, query, profilePath);
            auto shaderProfile = resolved.profile;
            if (!resolved.shaderSlotFromRule && shaderProfile.effectiveEffectId().isEmpty()) {
                // No rule matched and no tree override resolved a shader for
                // this snap event — apply the built-in per-event default
                // (window-morph for snap / layoutSwitch) via the shared SSOT,
                // which respects an explicit tree "None". Keeps the default
                // consistent with what the settings UI shows
                // (resolvedShaderProfile uses the same helper) without
                // persisting it into config. Gated on `!shaderSlotFromRule`: a
                // per-app rule that set "None" (engaged-empty effectId)
                // is a deliberate opt-out and must NOT be overridden here.
                shaderProfile =
                    PhosphorAnimationShaders::resolveShaderWithDefault(m_shaderManager.profileTree(), profilePath);
            }
            // Runtime applicability gate — same canonical-predicate check
            // as tryBeginShaderForEvent (resolvedShaderAppliesToEvent): the
            // rule layer or a stale config can deliver a pack that provably
            // cannot drive this snap leg (a move-physics or desktop pack).
            // Refusing here keeps the C++ WindowAnimator geometry animation
            // as the fallback instead of paying capture + paint cost for an
            // identity no-op transition.
            const QString snapShaderId = shaderProfile.effectiveEffectId();
            const bool snapShaderApplies =
                !snapShaderId.isEmpty() && resolvedShaderAppliesToEvent(snapShaderId, profilePath);
            // Tear down a live transition this snap leg is NOT going to replace.
            // Both no-install outcomes leave a stale-morph hazard, so both are
            // handled here (only reachable when the rule set or tree is edited
            // mid-drag — every applyWindowGeometry path shares the geometry class,
            // so the gate cannot flip between retargets otherwise; an open leg can
            // never reach here either, it holds addedGrabHeld and the enclosing
            // block is skipped via openAnimationInFlight):
            //
            //  1. A REFUSED pack (non-empty id that provably cannot drive this
            //     leg): clear ANY live transition — a morph from an earlier leg of
            //     this drag, or a settling wobble / in-flight focus leg — for a
            //     clean slate.
            //  2. An EMPTY id (the tree or a rule was edited to "None" mid-drag):
            //     clear only a transition that OWNS GEOMETRY (declares iFromRect).
            //     Its from/to rects are frozen at the PREVIOUS leg's endpoints and
            //     nothing retargets them, so leaving it would keep painting toward
            //     the OLD target while the WindowAnimator (retargeted above) heads
            //     to the new one — the identical stale-morph failure case 1 exists
            //     to prevent. A NON-geometry transition is deliberately left alone
            //     here: a settling wobble rings out over the WindowAnimator
            //     translate, exactly as on a long drag that snaps mid-settle. The
            //     bundled move pack declares no iFromRect, so shaderOwnsGeometry
            //     stays false, the animator keeps the geometry, and the two
            //     compose. A hybrid move+geometry pack that DOES declare iFromRect
            //     would own geometry — and is therefore torn down, correctly.
            if (!snapShaderApplies) {
                const ShaderTransition* live = m_shaderManager.findTransition(window);
                const bool liveOwnsGeometry = live && live->cached && live->cached->iFromRectLoc >= 0;
                if (live && (!snapShaderId.isEmpty() || liveOwnsGeometry)) {
                    endShaderTransition(window);
                }
            }
            if (snapShaderApplies) {
                const bool installed = beginShaderTransition(window, shaderProfile);
                // Identity gate before mutating the live leg — the same rule
                // as the heldMove stamp and the maximize morph endpoints. The
                // applicability gate above filtered the empty/refused shapes,
                // but beginShaderTransition can still return false for a
                // compile failure, the sticky null-shader sentinel, a
                // registry miss, or a collapsed surface — and in those cases
                // findTransition hands back an UNRELATED leg (a maximize
                // morph mid-flight is the reachable one). Retargeting that
                // leg's endpoints toward this snap would mutate a foreign
                // event's animation; but leaving a foreign GEOMETRY leg alive
                // is the frozen-stale-morph hazard the declined branch below
                // documents, because the window has already moved. So: owned
                // leg → retarget; foreign geometry-owning leg → tear down,
                // exactly as the animator-declined branch does.
                bool ownsSnapLeg = installed;
                auto* mt = m_shaderManager.findTransition(window);
                if (!ownsSnapLeg && mt) {
                    const auto cacheIt = m_shaderManager.m_shaderCache.find(snapShaderId);
                    ownsSnapLeg = cacheIt != m_shaderManager.m_shaderCache.end() && cacheIt->second.shader
                        && mt->cached == &cacheIt->second;
                }
                if (!ownsSnapLeg) {
                    if (mt && mt->cached && mt->cached->iFromRectLoc >= 0) {
                        endShaderTransition(window);
                    }
                    repaintSnapRegions(window, trueOldFrame, geo);
                    return;
                }
                // If the installed shader is a geometry morph (declares
                // iFromRect), hand it the old/new frames and request the
                // old-content snapshot. The morph then owns the visual
                // geometry animation — it interpolates the drawn rect from
                // oldFrame to targetFrame and cross-fades the old snapshot
                // into the live new content — so paintWindow gates off the
                // C++ WindowAnimator translate+scale for this window. The
                // WindowAnimator still runs (durationMs == 0) purely to drive
                // the morph's progress timeline.
                if (mt && mt->cached && mt->cached->iFromRectLoc >= 0) {
                    // Always retarget the morph to the new destination.
                    mt->toGeometry = animTarget;
                    // On a RETARGET mid-morph, beginShaderTransition short-
                    // circuits (same shader) and keeps the existing transition,
                    // so its captured snapshot already holds the ORIGINAL old
                    // content. Preserve the snapshot — re-capturing here would
                    // grab the mid-morph/new content and collapse the
                    // cross-fade — but RE-ANCHOR fromGeometry at the animator's
                    // new departure rect: the retarget above reset the animator
                    // progress to 0, so the morph's very next frame draws
                    // lerp(fromGeometry, toGeometry, 0). Keeping the stale
                    // origin made rapid successive moves visibly jump back to
                    // the original departure rect and replay (#795). Only a
                    // fresh morph (no snapshot yet) requests the capture.
                    // morphAnchor == oldFrame on a fresh start, so the
                    // unconditional assignment also covers the pre-fix
                    // fresh-morph anchoring — and fixes the sibling mismatch
                    // where a retarget landing before the first paint anchored
                    // at the already-committed previous target instead of the
                    // animator's departure rect.
                    mt->fromGeometry = morphAnchor;
                    // Gate on the compiled shader actually LINKING uOldWindow,
                    // matching the two sibling request sites (the move-start
                    // hookup and beginMaximizeShaderMorph) and the bind/unbind
                    // pair in decoration_render.cpp, which already test this
                    // same predicate. The bundled window-morph is vertex-only
                    // and samples no old frame, so an ungated request paid a
                    // full-window drawWindow re-entry plus an RGBA8 allocation
                    // on every snap, tile and reflow to fill a texture nothing
                    // would ever read. A cross-fade pack keeps its snapshot by
                    // declaring the uniform.
                    if (mt->cached->iOldWindowLoc >= 0 && !mt->oldSnapshot) {
                        mt->needsSnapshot = true;
                    }
                }
            }
        } else {
            // The animator DECLINED this leg — SnapPolicy refused the spec, or the
            // move fell under Profile::minDistance with no size change (a
            // user-settable 0-200px threshold, so this is reachable in a default-ish
            // config, not just a corner case). The whole install block above is
            // therefore skipped, including its stale-morph teardown — but the
            // moveResize higher up has ALREADY committed the new geometry.
            //
            // A live transition that OWNS GEOMETRY (declares iFromRect) froze its
            // from/to rects at the previous leg's endpoints and nothing retargets
            // them, so it would go on painting toward the OLD target across a window
            // that already sits at the new one. Same hazard the in-branch teardown
            // exists to prevent, on the path where the animator never ran.
            if (const ShaderTransition* live = m_shaderManager.findTransition(window);
                live && live->cached && live->cached->iFromRectLoc >= 0) {
                endShaderTransition(window);
            }
        }

        repaintSnapRegions(window, trueOldFrame, geo);
        return;
    }

    // No animation path (disabled, during drag, etc.): apply moveResize directly.
    // The null check runs BEFORE the removeAnimation so the drop and the
    // geometry commit share a branch: removeAnimation's contract requires the
    // caller to commit geometry immediately after (it schedules no damage),
    // and dropping the animation on the null-window path would strand the
    // last animated frame. window() is never null in modern KWin, so the
    // else-branch is defensive.
    KWin::Window* kwinWindow = window->window();
    if (kwinWindow) {
        if (m_windowAnimator->hasAnimation(window)) {
            m_windowAnimator->removeAnimation(window);
        }
        // DEBUG: the resolved rect is already logged at INFO above ("Setting
        // window geometry from ... to ..."), which covers both the animated
        // and non-animated paths — keep this one at debug to avoid a
        // duplicate INFO line for the same apply.
        qCDebug(lcEffect) << "moveResize: QRect=" << geo << "-> QRectF=" << QRectF(geo);
        kwinWindow->moveResize(QRectF(geo));

        repaintSnapRegions(window, trueOldFrame, geo);
    } else {
        qCWarning(lcEffect) << "Cannot get underlying Window from EffectWindow";
    }
}

void PlasmaZonesEffect::slotRestoreSizeDuringDrag(const QString& windowId, int width, int height)
{
    // Restore pre-snap size when cursor leaves zone during drag. The window may have been
    // snapped when the drag started (at zone size); when the user drags out of all zones,
    // we restore to floated state immediately so they see the window return to original size.
    // This complements the release path (dragStopped) which also handles restore.
    if (!m_dragTracker->isDragging() || m_dragTracker->draggedWindowId() != windowId) {
        return;
    }

    KWin::EffectWindow* window = m_dragTracker->draggedWindow();
    if (!window || !shouldHandleWindow(window)) {
        return;
    }

    if (width <= 0 || height <= 0) {
        return;
    }

    // Restore-size-only: keep current position, apply pre-snap width/height
    QRectF frame = window->frameGeometry();
    // qRound, not truncation — fractional-scale sub-pixel residue (see above).
    QRect geometry(qRound(frame.x()), qRound(frame.y()), width, height);

    qCDebug(lcEffect) << "Restoring size during drag:" << windowId << geometry;
    // Live drag-out unsnap: restoring pre-snap dimensions while the user is still dragging.
    // Logically a snap-out (the window is leaving zone-managed sizing).
    applyWindowGeometry(window, geometry, /*allowDuringDrag=*/true, /*skipAnimation=*/false,
                        PhosphorAnimation::ProfilePaths::WindowSnapOut);
}

void PlasmaZonesEffect::slotDragPolicyChanged(const QString& windowId, const PhosphorProtocol::DragPolicy& newPolicy)
{
    // Daemon-owned cross-VS flip. The daemon's updateDragCursor
    // handler computed policy at the current cursor position and found it
    // different from the policy in force — tell us so we can apply the
    // compositor-level transition. Replaces the effect-side cross-VS flip
    // loop in the dragMoved lambda that walked KWin::effects->screens()
    // with a stale m_managedScreens cache.
    //
    // Guards: this slot only acts if we're actively tracking the drag for
    // this windowId. Stray signals (daemon restart, out-of-order delivery)
    // are ignored.
    if (!m_dragTracker->isDragging() || m_dragTracker->draggedWindowId() != windowId) {
        qCDebug(lcEffect) << "slotDragPolicyChanged: drag no longer active for" << windowId;
        return;
    }

    if (const QString err = newPolicy.validationError(); !err.isEmpty()) {
        // Garbled policy change — keep current state rather than transitioning
        // to a corrupted one. The daemon will re-emit on the next cursor tick
        // if this was transient.
        qCWarning(lcEffect) << "slotDragPolicyChanged rejected:" << err << "for" << windowId;
        return;
    }

    const PhosphorProtocol::DragBypassReason oldReason = m_currentDragPolicy.bypassReason;
    const PhosphorProtocol::DragBypassReason newReason = newPolicy.bypassReason;
    // The latch has to agree with the reason for this to be a genuine no-op,
    // not just the two reasons matching. The un-bypass transition below gates
    // on the effect's OWN latch precisely because the drag-start fast path can
    // set it while m_currentDragPolicy still holds the conservative default
    // (reason None) — and when the beginDrag reply errors, its correction arm
    // never runs, so that mismatch persists. A later None→None emission from
    // the daemon (which compares against its own record, not ours) then landed
    // here and returned early, so the un-bypass never ran: tracking stayed
    // held, the keyboard was never grabbed, and Escape went uncaught for the
    // rest of the drag. Requiring the latch to agree lets that case fall
    // through to the transition it was always meant to reach.
    const bool latchAgreesWithReason =
        m_dragBypassedForEngine == (newReason == PhosphorProtocol::DragBypassReason::EngineOwnedScreen);
    if (oldReason == newReason && latchAgreesWithReason) {
        // Same reason but different screenId (autotile→autotile cross-VS):
        // update the captured screen so endDrag's ApplyFloat uses the right one.
        m_currentDragPolicy = newPolicy;
        if (newReason == PhosphorProtocol::DragBypassReason::EngineOwnedScreen) {
            m_dragBypassScreenId = newPolicy.screenId;
        }
        return;
    }

    qCInfo(lcEffect) << "slotDragPolicyChanged:" << windowId << oldReason << "->" << newReason
                     << "screen=" << newPolicy.screenId;

    m_currentDragPolicy = newPolicy;

    if (newReason == PhosphorProtocol::DragBypassReason::EngineOwnedScreen) {
        // Snap → autotile (or context-disabled → autotile). Cancel any
        // active snap overlay, enter bypass mode. Mirrors the old
        // effect-side flip block's "snap→autotile" branch, but driven by
        // daemon truth rather than an effect-cached screen set.
        if (!m_dragBypassedForEngine) {
            m_snapHandler->callCancelSnap();
            m_dragBypassedForEngine = true;
            m_dragBypassScreenId = newPolicy.screenId;
        } else {
            // Already in bypass but on a different autotile screen — just
            // update the captured screen id.
            m_dragBypassScreenId = newPolicy.screenId;
        }
        return;
    }

    // Gate on the effect's OWN latch, not merely on the daemon's previous
    // reason. The drag-start fast path latches the bypass from the union
    // isManagedScreen without consulting the context-disable lists, while the
    // daemon checks ContextDisabled FIRST and so answers ContextDisabled (not
    // EngineOwnedScreen) for an engine-managed screen whose context is disabled.
    // The beginDrag correction layer only clears the latch on a reply of None,
    // so the drag can be underway latched-bypassed with a policy that was never
    // EngineOwnedScreen. Keying this transition on oldReason alone then let
    // ContextDisabled -> None (and SnappingDisabled -> None) fall through to the
    // no-op tail with the latch still set for the rest of the drag: the engine
    // tracking drop never ran (the window kept its tile tracking and hidden
    // title bar while it snapped), the keyboard was never grabbed, and the
    // activation state was never reset. Scrolling widens the reachable surface
    // because every scrolling screen is in the union the fast path latches on.
    if (oldReason == PhosphorProtocol::DragBypassReason::EngineOwnedScreen || m_dragBypassedForEngine) {
        // Autotile → snap (or autotile → context-disabled). Drop the
        // bypass flag and initialize snap-drag state as if the drag just
        // started on this snap screen. Remove the window from autotile
        // tracking so slotWindowFrameGeometryChanged doesn't fight the
        // snap geometry on subsequent geometry changes.
        //
        // Do NOT call handleDragToFloat here: the mid-drag schedule would
        // race against the zone snap at drop, making the window jump after
        // the user lets go. onWindowClosed alone clears the tracking state.
        // Guarded on the ID, not the dragged-window pointer: the call is
        // id-keyed bookkeeping that never derefs the window, and a
        // died-mid-drag pointer must not skip the tracking cleanup for a
        // still-valid id.
        if (!windowId.isEmpty()) {
            // releaseWindowTracking, NOT onWindowClosed: the window is live
            // and mid-drag, and the close relay's capture would record the
            // drag frame as its float-back.
            m_tilingHandler->releaseWindowTracking(windowId, m_dragBypassScreenId);
        }
        m_dragBypassedForEngine = false;
        // Cleared with the flag, as the equivalent transition in
        // lifecycle_wiring.cpp does: leaving a stale engine screen id behind
        // meant it survived into any later re-bypass until the EngineOwnedScreen
        // branch above happened to overwrite it.
        m_dragBypassScreenId.clear();
        m_dragActivation.detected = false;
        // KWin::effects guarded: this slot runs from a D-Bus signal
        // dispatch (slotDragPolicyChanged), which can land during compositor
        // teardown when the global is already gone — same rule and reason
        // repaintSnapRegions documents above.
        if (!m_keyboardGrabbed && KWin::effects) {
            KWin::effects->grabKeyboard(this);
            m_keyboardGrabbed = true;
        }
        return;
    }

    // Other transitions (snap ↔ context_disabled / snapping_disabled) with no
    // bypass latch held: no compositor-level work needed. The daemon will
    // return a NoOp at endDrag for disabled paths.
}

} // namespace PlasmaZones
