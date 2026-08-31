// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snaphandler.h"

#include "tilinghandler/tilinghandler.h"
#include "dragtracker.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "snapassisthandler.h"
#include "compositor/effectlogging.h"

#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/NavigationMarshalling.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowMarshalling.h>

#include <effect/effect.h> // Effect::animationTime, the deferred-unfloat grace
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <chrono> // std::chrono::milliseconds for Effect::animationTime
#include <memory>

namespace PlasmaZones {

static constexpr int kSnapMinimizeFloatDebounceMs = kSpuriousMinimizePairMs;
static constexpr int kSnapUnfloatRetryDelayMs = 250;
static constexpr int kSnapMaxUnfloatRetries = 3;

SnapHandler::SnapHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

void SnapHandler::markWindowSnapped(const QString& windowId, const QString& screenId)
{
    // An empty screenId is never a valid snap owner: the per-screen buckets are
    // keyed by screenId, so recording under "" would pollute the set with an
    // entry that the per-screen cross-screen cleanup can never reclaim.
    // Callers route unresolved/float windows through clearWindowSnapped instead;
    // this guard is defensive depth for any path that slips an empty screen in.
    if (windowId.isEmpty() || screenId.isEmpty()) {
        return;
    }
    KWin::EffectWindow* w = m_effect->findWindowById(windowId);
    if (!w || m_effect->getWindowId(w) != windowId) {
        // Window gone (closed mid-snap — the close often races the async
        // snap reply, so slotWindowClosed's bookkeeping may have ALREADY
        // run). Recording tiled tracking now would re-create state nothing
        // will ever clean up; drop any remnants instead. The exact-id check
        // matters: findWindowById's appId fuzzy fallback can resolve a
        // same-app SIBLING for a dead id, and tracking the sibling under the
        // dead key would strand it.
        TilingStateHelpers::removeFromAllScreens(m_border, windowId);
        return;
    }
    // A window can only be snap-managed by one screen at a time. Strip stale
    // tiled tracking from any OTHER screen before recording the new owner
    // (mirrors the autotile cross-screen-transfer cleanup in tiling.cpp).
    TilingStateHelpers::removeFromOtherScreens(m_border, windowId, screenId);
    TilingStateHelpers::addTiledOnScreen(m_border, screenId, windowId);
    m_restartSnapCandidates.remove(windowId);
    // Placed by some route already — whether this restore or another — so the
    // desktop-arrival park has nothing left to do.
    cancelDesktopArrivalRestore(windowId);

    // Title-bar (borderless) state is driven entirely by rules through
    // the effect's reconcileRuleHiddenTitleBar → DecorationManager path; this
    // handler only records snap tiled-tracking for border RENDERING.

    // Border overlays are visual-only, so skip the off-desktop case (consistent
    // with updateAllDecorations): redirecting an invisible window through the border
    // shader is wasted work. When the user switches to that window's desktop, the
    // desktopChanged → updateAllDecorations connection rebuilds its border.
    if (w->isOnCurrentDesktop()) {
        m_effect->updateWindowDecoration(windowId, w);
    }
}

void SnapHandler::clearWindowSnapped(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    TilingStateHelpers::removeFromAllScreens(m_border, windowId);
    m_restartSnapCandidates.remove(windowId);
    // The desktop-arrival park is deliberately NOT dropped here, unlike in
    // markWindowSnapped. Unsnapping answers where the window sits in the CURRENT
    // desktop's layout; it says nothing about a window still travelling to
    // another desktop, and a float still wants its recorded position restored
    // when it arrives. The park's own exits (arrival, close, daemon loss) cover
    // the rest.
    // A window that is no longer snap-managed occupies no zone. The zone cache
    // is the source of the IsSnapped / Zone rule-match fields, and several
    // unsnap paths (drag-out unsnap in particular) get their answer in the
    // endDrag reply with NO windowStateChanged broadcast to follow — leaving
    // the entry stale means the coalesced rule re-resolve still sees
    // "snapped", so a placement-scoped decoration never rebuilds and a
    // hide-title-bar-when-snapped rule keeps the title bar hidden until some
    // unrelated re-resolve (e.g. a focus swap). Clearing here, in the one
    // place every unsnap funnels through, keeps the fact and the tracking in
    // lockstep; clearWindowZone re-resolves the rules only when an entry was
    // actually dropped, so this is free for callers whose broadcast already
    // landed. Cleared BEFORE the re-decorate below so the re-resolve sees the
    // window as unsnapped and picks the floating-state chain, not the snapped
    // one it is leaving.
    m_effect->clearWindowZone(windowId);
    // Re-resolve the decoration SYNCHRONOUSLY instead of dropping it: this
    // funnel runs at drag start (the daemon floats the grabbed window), and a
    // bare removeWindowDecoration here blanked EVERY pack — interior blur
    // included — until some later push happened to rebuild the entry
    // mid-drag. The shared funnel swaps update-or-remove in the same turn.
    m_effect->reconcileDecorationOnPlacementFlip(windowId);
}

void SnapHandler::clearSnapTracking()
{
    // Bookkeeping only. Physical title-bar restores are the
    // DecorationManager's job — teardown callers pair this with
    // DecorationManager::restoreAll(). Callers also pair it with
    // clearAllDecorations() to release the per-window border shader redirect.
    // Preserve unresolved candidates from an earlier daemon cycle. They are
    // retired only by an authoritative snap, float, close, or restore miss.
    // CONSCIOUS BREADTH: every window snap-tracked at teardown becomes a
    // candidate, and one that never gets an authoritative answer stays a
    // minimize-float candidate indefinitely. Accepted — the alternative
    // (expiring candidates) re-strands the daemon-restart-while-minimized
    // window this set exists for, and the cost of the breadth is only that a
    // once-snapped window minimizing later is floated like a snapped one.
    for (auto it = m_border.tiledWindowsByScreen.cbegin(); it != m_border.tiledWindowsByScreen.cend(); ++it) {
        m_restartSnapCandidates.unite(it.value());
    }
    m_pendingUnminimizeUnfloat.cancelAll();
    // BOTH deferred queues, matching TilingHandler's clearAllPendingMinimizeFloats:
    // this also runs on daemon LOSS (not just teardown), and a minimize→float
    // debounce armed just before the loss would otherwise still fire — its
    // isDaemonReady gate suppresses the D-Bus call but the callback would
    // fabricate m_minimizeFloatedWindows ownership of a float that was never
    // issued and destroy the restart-candidate provenance the reconnect path
    // depends on. The retry budget is per-session too.
    m_pendingMinimizeFloat.cancelAll();
    m_unfloatRetryAttempts.clear();
    for (auto it = m_unfloatInFlight.cbegin(); it != m_unfloatInFlight.cend(); ++it) {
        m_minimizeFloatedWindows.insert(it.key());
    }
    m_unfloatInFlight.clear();
    // Desktop-arrival parks do not outlive the daemon that armed them. Each
    // entry is a promise to re-drive a restore against a specific placement
    // record, and a reconnecting daemon reloads its store from disk — those
    // records may already be consumed or rewritten, so a surviving park would
    // drive a restore against state that no longer matches. The bringup
    // stacking sweep re-announces every window anyway, which is the correct
    // retry for a window still waiting.
    m_awaitingDesktopArrivalRestore.clear();
    m_border.tiledWindowsByScreen.clear();
}

void SnapHandler::onWindowClosed(const QString& windowId)
{
    // Pure bookkeeping — the window is being destroyed, so no setNoBorder /
    // removeWindowDecoration is needed (the effect's close path drops the border
    // entry / shader redirect and the title bar dies with the window).
    TilingStateHelpers::removeFromAllScreens(m_border, windowId);
    m_restartSnapCandidates.remove(windowId);
    // A dead window's park would otherwise sit in the set until some later
    // desktop switch happened to notice its id has no live window.
    cancelDesktopArrivalRestore(windowId);
}

void SnapHandler::setFocusFollowsMouse(bool enabled)
{
    m_focusFollowsMouse = enabled;
}

void SnapHandler::callResolveWindowRestore(KWin::EffectWindow* window, std::function<void(bool)> onComplete,
                                           bool releaseSuppressionOnMiss, PhosphorEngine::RestoreReason reason)
{
    // The shadow seed and the daemon's two gates all ask the same question of
    // the reason, and they must keep asking exactly it: widening any of them
    // beyond Open changes which windows get seeded or reclaimed.
    const bool isOpenPath = reason == PhosphorEngine::RestoreReason::Open;
    if (!window) {
        if (onComplete) {
            onComplete(false);
        }
        return;
    }

    if (!m_effect->isDaemonReady("resolve window restore")) {
        // No daemon means no snap-restore (and no autotile either — it
        // needs the daemon too). Release first-frame suppression so the
        // window is not held invisible waiting on a reposition that will
        // never come.
        m_effect->endRestoreSuppression(window);
        if (onComplete) {
            onComplete(false);
        }
        return;
    }

    QPointer<KWin::EffectWindow> safeWindow = window;
    QString windowId = m_effect->getWindowId(window);
    QString screenId = m_effect->getWindowScreenId(window);
    bool sticky = m_effect->isWindowSticky(window);

    // On a resolve miss (daemon found no zone) release first-frame
    // suppression — unless the caller says another path will still
    // reposition the window (autotile-screen path), in which case the
    // suppression must hold until that reposition's geometry settles.
    const auto releaseSuppression = [this, safeWindow, releaseSuppressionOnMiss]() {
        if (releaseSuppressionOnMiss) {
            if (safeWindow) {
                m_effect->endRestoreSuppression(safeWindow);
            }
        }
    };
    const auto onMiss = [this, windowId, releaseSuppression]() {
        m_restartSnapCandidates.remove(windowId);
        releaseSuppression();
    };

    // Single D-Bus call — daemon runs the full appRule → persisted → emptyZone → lastZone chain.
    //
    // skipAnimation=true: teleport the window straight into the resolved
    // zone. The animated morph path tweens the window from its spawn
    // position, which both reads as "KDE opened the window, then we moved
    // it" and collides with any in-flight surface-extent window.open
    // shader (bounce / fly-in) — the morph translates the output-spanning
    // shader quad. Placing the window directly lets the open shader play
    // cleanly into the zone.
    //
    // storePreSnap=false: the window is already at its snap/zone position (from before
    // daemon restart or from KWin session restore), so its current frameGeometry is the
    // zone geometry — NOT the free-floating geometry. Storing it as pre-tile would cause
    // float toggle to restore to the zone geometry instead of the original free-floating position.
    // Seed the daemon's frame-geometry shadow before the resolve below, but
    // only on the open path. The daemon translates a bare RouteToScreen onto
    // the target monitor from that shadow, and the shadow has exactly two
    // writers (the debounced motion flush and the bring-up bulk seed), so a
    // window that has never MOVED has no entry at all — which is precisely a
    // freshly opened one. Without this the daemon reads an invalid rect and
    // takes its "the rule owns this window, but there is nothing to
    // translate" branch, leaving the window on its spawn monitor and
    // suppressing the remembered-placement fallback too. Confirmed live in a
    // nested session: the same rule routed an already-open window (shadow
    // seeded by a daemon restart) and silently did nothing for a fresh one.
    //
    // Ordering is why this can be fire-and-forget: both calls ride the same
    // D-Bus connection, and per-connection message order is preserved, so the
    // daemon has applied this push before it handles the resolve.
    if (isOpenPath) {
        const QRect openGeo = window->frameGeometry().toRect();
        if (openGeo.isValid()) {
            PhosphorProtocol::ClientHelpers::fireAndForget(
                m_effect, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("setFrameGeometry"),
                {windowId, openGeo.x(), openGeo.y(), openGeo.width(), openGeo.height()},
                QStringLiteral("setFrameGeometry open-path seed"));
        }
    }
    const int kindInt = static_cast<int>(m_effect->classifyWindowKind(window));
    // Thread the applied outcome to onComplete: onSnapSuccess fires only on
    // the zone-applied branch of tryAsyncSnapCall, and every branch calls
    // onComplete afterwards, so the flag is always settled when it runs.
    auto snapApplied = std::make_shared<bool>(false);
    std::function<void(const QString&, const QString&)> markApplied;
    std::function<void()> completeWithOutcome;
    if (onComplete) {
        markApplied = [snapApplied](const QString&, const QString&) {
            *snapApplied = true;
        };
        completeWithOutcome = [onComplete, snapApplied]() {
            onComplete(*snapApplied);
        };
    }
    // Client-declared minimum, the same value the tiling channel sends: on a
    // cross-screen reclaim the adopting engine evaluates its oversized/float
    // verdict once from this, and 0,0 left an oversized window tiled.
    const QSize declaredMin = TilingHandler::declaredMinSize(window);
    m_effect->tryAsyncSnapCall(
        PhosphorProtocol::Service::Interface::Snap, QStringLiteral("resolveWindowRestore"),
        {windowId, screenId, sticky, kindInt, static_cast<int>(reason), declaredMin.width(), declaredMin.height()},
        safeWindow, windowId, false, onMiss, markApplied,
        /*skipAnimation=*/true, completeWithOutcome, releaseSuppression);
}

void SnapHandler::ensurePreSnapGeometryStored(KWin::EffectWindow* w, const QString& windowId,
                                              const QRectF& preCapturedGeometry)
{
    if (!w || windowId.isEmpty()) {
        return;
    }

    if (!m_effect->isDaemonReady("ensure pre-snap geometry")) {
        return;
    }

    // Use pre-captured geometry if provided, otherwise read from window. Correct for
    // maximize/fullscreen: the callers' pre-captured frame is the live frameGeometry(),
    // which is the full-monitor rect while maximized — capturing that as the float-back
    // size floats the window back full-screen. freeGeometryForCapture substitutes the
    // pre-maximize restore rect (shared with the autotile capture path).
    QRectF geom = preCapturedGeometry.isValid() ? preCapturedGeometry : QRectF(w->frameGeometry());
    geom = m_effect->freeGeometryForCapture(w, geom);
    if (geom.width() <= 0 || geom.height() <= 0) {
        return;
    }

    // Mirror of the autotile capture's snap-owned guard
    // (saveAndRecordPreAutotileGeometry): a window the AUTOTILE side owns —
    // actively tiled, or held as an autotile minimize-float — is sitting on
    // its TILE rect, never a free-floating position. Capturing it here would
    // poison the shared float-back with the tile rect (the reverse of the
    // per-mode leak that guard closes).
    // Unguarded deref, this file's convention: m_tilingHandler is declared
    // before m_snapHandler on the effect, so it outlives every SnapHandler
    // call (documented at the adoptMinimizeFloated site).
    if (TilingHandler* autotile = m_effect->tilingHandler();
        autotile->isTiledWindow(windowId) || autotile->isMinimizeFloated(windowId)) {
        qCDebug(lcEffect) << "Skipped pre-snap geometry for autotile-owned window (frame is tile rect)" << windowId;
        return;
    }

    // Use virtual-screen-aware ID — getWindowScreenId() falls back to the physical
    // ID when virtual screen defs haven't loaded yet, so it is safe to call
    // unconditionally. Using it here ensures the stored screen ID always matches
    // the ID used by later lookups.
    const QString screenId = m_effect->getWindowScreenId(w);

    // Post the store directly with overwrite=false. The daemon's storePreTileGeometry
    // enforces per-windowId idempotency — a second capture for the same runtime
    // instance is a no-op. We deliberately skip the prior async hasPreTileGeometry
    // pre-check: that path matched on appId too, so a stale cross-session entry from
    // a prior window instance (keyed by appId) would block the fresh per-instance
    // capture and freeze float-restore at ancient coordinates.
    // qRound, not truncation: fractional-scale outputs leave sub-pixel
    // residue in frameGeometry() (same convention as the toRect() geometry
    // paths — see window_lifecycle.cpp).
    PhosphorProtocol::ClientHelpers::fireAndForget(
        m_effect, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("storePreTileGeometry"),
        {windowId, qRound(geom.x()), qRound(geom.y()), qRound(geom.width()), qRound(geom.height()), screenId, false},
        QStringLiteral("storePreTileGeometry"));
    qCInfo(lcEffect) << "Stored pre-tile geometry for window" << windowId << "geom=" << geom;
}

void SnapHandler::handleCursorMoved(const QPointF& pos, const QString& screenId)
{
    if (!m_focusFollowsMouse) {
        return;
    }

    // Pause FFM entirely during show-desktop/peek. Peeked windows are hidden
    // from the scene but keep their frameGeometry, so the scan below would
    // find one under the cursor and activateWindow() would synchronously
    // cancel the peek (mirrors the autotile FFM guard).
    if (PlasmaZonesEffect::isShowingDesktop()) {
        return;
    }

    // Pause FFM whenever the active window is NOT snapped into a zone. Any
    // non-snapped active window — a popup/dialog/excluded app, a window the user
    // deliberately floated, or a free window they simply have not snapped — is
    // one the user is working in on top of the snap stack; wandering the cursor
    // over a snapped window beneath it must not steal its focus. FFM resumes
    // (follows the cursor between snapped windows) once a snapped window is
    // active. Scoped to the cursor's screen (mirrors TilingHandler::
    // handleCursorMoved, discussion #461 + follow-up): a window active on another
    // monitor must not freeze FFM on the monitor the cursor is on. The daemon's
    // own passthrough overlay surface never counts as the kind of active window
    // worth protecting; the interactive editor DOES (it is not a passthrough
    // overlay, so it falls through to the not-snapped pause below and keeps
    // focus). (Autotile pauses on the same principle — floated/popup/under-
    // min-size — but everything else there is tiled, so it has no never-managed
    // "free" case; snap does, hence the single not-snapped predicate.)
    if (KWin::EffectWindow* active = KWin::effects->activeWindow()) {
        // Cheap overlay-class check first, then the heavier screen resolution
        // (mirrors the autotile guard's predicate ordering).
        if (!PlasmaZonesEffect::isOwnPassthroughOverlayClass(active->windowClass())
            && m_effect->getWindowScreenId(active) == screenId && !isTiledWindow(m_effect->getWindowId(active))) {
            return;
        }
    }

    // Find the topmost snapped window under the cursor (stacking order top → bottom).
    const auto windows = KWin::effects->stackingOrder();
    for (int i = windows.size() - 1; i >= 0; --i) {
        KWin::EffectWindow* w = windows[i];
        // isDeleted: a close-grabbed dying window under the cursor must not
        // pause FFM via the occlusion bail (or pollute id caches below).
        // isHiddenByShowDesktop: belt-and-braces behind the showing-desktop
        // bail above, for the frame where peek engages mid-scan.
        if (!w || w->isDeleted() || w->isMinimized() || w->isHiddenByShowDesktop() || !w->isOnCurrentDesktop()
            || !w->isOnCurrentActivity()) {
            continue;
        }
        // Cheap geometry test before the windowClass()/windowId allocations below.
        if (!w->frameGeometry().contains(pos)) {
            continue;
        }
        // Look through the daemon's own passthrough overlay surface — it is
        // full-screen and always topmost, so a bail here would kill FFM whenever
        // an overlay is up (mirrors the autotile FFM guard). The interactive
        // editor is NOT looked through: it falls to the not-snapped bail below,
        // so FFM leaves focus on it instead of stealing to a snapped window.
        if (PlasmaZonesEffect::isOwnPassthroughOverlayClass(w->windowClass())) {
            continue;
        }
        // The window directly under the cursor is not snapped (a floating dialog, popup,
        // or excluded app occluding a snapped window beneath). Don't look through it to
        // focus the snapped window — that would steal focus from what the user is pointing
        // at. Mirrors TilingHandler::handleCursorMoved's occlusion guard.
        if (!isTiledWindow(m_effect->getWindowId(w))) {
            return;
        }
        if (w == KWin::effects->activeWindow()) {
            return; // Already focused — no-op.
        }
        KWin::effects->activateWindow(w);
        return;
    }
}

void SnapHandler::callCancelSnap()
{
    qCInfo(lcEffect) << "Calling cancelSnap (drag cancelled by Escape or external event)";
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::WindowDrag,
                                                QStringLiteral("cancelSnap"));
}

bool SnapHandler::offerMinimizeEdge(KWin::EffectWindow* window, const QString& windowId, const QString& screenId)
{
    // Mirror of handleMinimizeChanged's unminimize entry gate. See the
    // header doc: a transfer the gate would refuse must be reported to the
    // sender, not silently dropped.
    if (m_effect->tilingHandler()->isManagedScreen(screenId)) {
        return false;
    }
    handleMinimizeChanged(window, windowId, screenId, /*minimized=*/false);
    return true;
}

void SnapHandler::handleMinimizeChanged(KWin::EffectWindow* window, const QString& windowId, const QString& screenId,
                                        bool minimized)
{
    // Snap-mode-only: the autotile handler runs its own snap-state / float-state
    // machine for autotile screens.
    const bool autotileScreen = m_effect->tilingHandler()->isManagedScreen(screenId);
    if (!minimized && autotileScreen) {
        return;
    }

    if (minimized) {
        // A re-minimize during the deferred unfloat grace cancels the pending
        // commit: the window never unfloated, so it is still minimize-floated
        // and the isWindowFloating skip below covers the rest.
        cancelPendingUnminimizeUnfloat(windowId);
        if (m_unfloatInFlight.remove(windowId)) {
            qCInfo(lcEffect) << "Snap: re-minimize countermanding in-flight unfloat:" << windowId;
            if (m_effect->isDaemonReady("snap re-minimize countermand")) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                    QStringLiteral("setWindowFloatingForScreen"), {windowId, screenId, true},
                    QStringLiteral("setWindowFloatingForScreen"));
            }
            if (autotileScreen) {
                // The screen's mode owns the window now: hand the countermanded
                // float to autotile (single owner) instead of re-claiming a
                // record snap has no release path for on this screen. The
                // untiled marker routes the eventual unminimize through the
                // immediate-commit path — the rect belongs to the other mode.
                // Unguarded like this function's other tilingHandler()
                // derefs: m_tilingHandler is declared before m_snapHandler
                // on the effect, so it outlives every SnapHandler call.
                m_effect->tilingHandler()->adoptMinimizeFloated(windowId, /*untiled=*/true);
            } else {
                m_minimizeFloatedWindows.insert(windowId);
                // Refund the retry budget on the countermand's snap-side
                // re-claim too, matching the ordinary fall-through below:
                // after three failed retries plus a re-minimize landing
                // mid-flight, the NEXT unminimize's commit would otherwise
                // start with zero retries. Kept inside the non-autotile
                // branch so the refund's documented scoping (never reset by
                // a screen snap refuses to handle) holds.
                m_unfloatRetryAttempts.remove(windowId);
            }
            return;
        }
        if (autotileScreen) {
            return;
        }
        // Refund below the autotile bail: a minimize on a screen snap refuses
        // to handle must not reset snap's retry budget for that window.
        m_unfloatRetryAttempts.remove(windowId);
        if (m_effect->isWindowFloating(windowId)) {
            qCDebug(lcEffect) << "Snap: minimized already-floating window, skipping float:" << windowId;
            return;
        }
        // Only a snap-managed window owns a zone that minimizing should free.
        // A free window can reach this path with a cold floating cache after
        // daemon/effect bring-up and must remain unmanaged.
        if (!isTiledWindow(windowId) && !m_restartSnapCandidates.contains(windowId)) {
            qCDebug(lcEffect) << "Snap: minimized unmanaged window, skipping float:" << windowId;
            return;
        }
        if (m_pendingMinimizeFloat.contains(windowId)) {
            return;
        }

        QPointer<KWin::EffectWindow> wPtr(window);
        m_pendingMinimizeFloat.schedule(windowId, kSnapMinimizeFloatDebounceMs, [this, windowId, screenId, wPtr]() {
            if (!wPtr || wPtr->isDeleted()) {
                return;
            }
            KWin::EffectWindow* live = wPtr.data();
            if (!live->isMinimized() || !m_effect->shouldHandleWindow(live) || !m_effect->isTileableWindow(live)) {
                return;
            }
            const QString currentScreenId = m_effect->getWindowScreenId(live);
            if (currentScreenId != screenId || m_effect->tilingHandler()->isManagedScreen(currentScreenId)
                || m_effect->isWindowFloating(windowId)) {
                return;
            }

            m_minimizeFloatedWindows.insert(windowId);
            m_restartSnapCandidates.remove(windowId);
            qCInfo(lcEffect) << "Snap: window minimized (after debounce), floating:" << windowId << "on" << screenId;
            if (m_effect->isDaemonReady("snap minimize float")) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                    QStringLiteral("setWindowFloatingForScreen"), {windowId, screenId, true},
                    QStringLiteral("setWindowFloatingForScreen"));
            }
        });
        return;
    } else {
        if (m_pendingMinimizeFloat.contains(windowId)) {
            cancelPendingMinimizeFloat(windowId);
            qCDebug(lcEffect) << "Snap: coalesced spurious minimize/unminimize cycle for" << windowId;
            return;
        }
        if (!m_minimizeFloatedWindows.contains(windowId)) {
            // Adopt a minimize-float created by the AUTOTILE handler before
            // this screen swapped away from autotile — the mirror of the
            // adoption in TilingHandler::slotWindowMinimizedChanged, and
            // for the same reason: ownership must follow the screen's
            // current mode or the unminimize leaves the window floating
            // until the next mode toggle. removeMinimizeFloated also cancels
            // that handler's pending deferred commit for the window.
            // Unguarded deref per this file's convention (declaration order
            // on the effect guarantees the handler outlives us).
            TilingHandler* autotile = m_effect->tilingHandler();
            const int autotileBudgetUsed = autotile->unfloatRetryBudgetUsed(windowId);
            if (autotile->removeMinimizeFloated(windowId)) {
                m_minimizeFloatedWindows.insert(windowId);
                // Budget survives the hop (see seedUnfloatRetryBudget).
                seedUnfloatRetryBudget(windowId, autotileBudgetUsed);
                // The window still carries its autotile rect, not its snap-zone
                // rect. Committing at the edge dispatches the unfloat
                // immediately, but the daemon's resnap geometry only lands
                // after a D-Bus round trip — KWin would play the whole
                // restore against the stale autotile frame and then hop.
                // Withhold the first frames instead: the suppression releases
                // the moment the resnap moves the frame, or at its own 250 ms
                // deadline if no reposition arrives, so the window appears
                // once, at its snap placement.
                qCInfo(lcEffect) << "Snap: adopted autotile-mode minimize-float, unfloating immediately:" << windowId;
                // Daemon-ready gated: commitUnminimizeUnfloat's own early
                // return on a closed gate would otherwise leave the
                // suppression armed with nothing dispatched and no
                // reposition ever coming — the window withheld from
                // compositing until the hard 250 ms deadline for no reason.
                if (m_effect->isDaemonReady("snap adopt suppression")) {
                    m_effect->beginRestoreSuppression(window);
                }
                commitUnminimizeUnfloat(window, windowId, screenId);
                return;
            } else {
                qCDebug(lcEffect) << "Snap: unminimized window was not minimize-floated, skipping unfloat:" << windowId;
                return;
            }
        }
        // Supersede any surviving pending entry rather than skipping the
        // edge: the shared queue also carries RETRY timers
        // (scheduleUnminimizeUnfloatRetry), and while the minimize edge
        // cancels entries on its own path, a genuine unminimize must never be
        // swallowed by whichever stale timer slipped through — the fresh edge
        // is the authoritative signal, so the grace re-arms from it.
        cancelPendingUnminimizeUnfloat(windowId);
        // Defer the whole unfloat commit (restore-net queries included) past
        // KWin's unminimize animation, mirroring TilingHandler's deferred
        // unfloat and for the same reason: the unfloat re-snaps the window,
        // the daemon applies its zone geometry, and a moveResize landing
        // mid-flight cancels the stock animation (discussion #816). There is
        // no cross-effect API to observe the animation, so the grace is
        // animationTime(400ms) — stock minimize animations are 250ms base
        // scaled by the user's global animation-speed factor, which
        // animationTime applies too. Ownership moves to the in-flight set at
        // commit so a re-minimize can countermand the asynchronous unfloat.
        const int graceMs = int(KWin::Effect::animationTime(std::chrono::milliseconds(400)).count());
        QPointer<KWin::EffectWindow> wPtr(window);
        m_pendingUnminimizeUnfloat.schedule(windowId, graceMs, [this, windowId, wPtr]() {
            // Re-validate at commit time. Every bail leaves the window
            // minimize-floated, which the next unminimize edge picks up.
            if (!wPtr) {
                qCDebug(lcEffect) << "Snap: deferred unfloat window destroyed, skipping:" << windowId;
                return;
            }
            KWin::EffectWindow* fw = wPtr.data();
            if (fw->isMinimized()) {
                qCDebug(lcEffect) << "Snap: deferred unfloat window re-minimized, skipping:" << windowId;
                return;
            }
            // Mirror the caller-side gate (slotWindowMinimizedChanged only
            // forwards handleable tileable windows) and the autotile twin's
            // commit revalidation: the window may have become excluded or
            // non-tileable during the grace.
            if (!m_effect->shouldHandleWindow(fw) || !m_effect->isTileableWindow(fw)) {
                qCDebug(lcEffect) << "Snap: deferred unfloat no longer handleable, skipping:" << windowId;
                return;
            }
            const QString currentScreenId = m_effect->getWindowScreenId(fw);
            if (m_effect->tilingHandler()->isManagedScreen(currentScreenId)) {
                // The unminimize edge already happened, so waiting for another
                // edge would strand the suspension permanently. Transfer the
                // commit to the handler that owns the screen now; its adoption
                // path removes our marker and tiles immediately.
                qCInfo(lcEffect) << "Snap: deferred unfloat screen became autotile, transferring:" << windowId;
                if (!m_effect->tilingHandler()->offerMinimizeEdge(fw)) {
                    // Receiver's entry gates refused (window became
                    // unhandleable or the screen set moved again). The edge
                    // is spent, so re-arm from our side — ownership stayed
                    // here.
                    qCInfo(lcEffect) << "Snap: autotile refused transferred edge, re-arming retry:" << windowId;
                    scheduleUnminimizeUnfloatRetry(windowId);
                }
                return;
            }
            if (!m_minimizeFloatedWindows.contains(windowId)) {
                return; // State moved under us (e.g. bulk cleanup); nothing to commit.
            }
            commitUnminimizeUnfloat(fw, windowId, currentScreenId);
        });
        return;
    }
}

void SnapHandler::commitUnminimizeUnfloat(KWin::EffectWindow* window, const QString& windowId, const QString& screenId)
{
    const bool daemonReady = m_effect->isDaemonReady("snap unminimize");
    if (!daemonReady || !m_minimizeFloatedWindows.contains(windowId)) {
        return;
    }
    m_minimizeFloatedWindows.remove(windowId);
    const quint64 requestGeneration = ++m_unfloatRequestGeneration;
    m_unfloatInFlight.insert(windowId, requestGeneration);
    // Restore net for a snap-tracked window minimized across a daemon
    // restart: every restore pass (slotDaemonReady's untracked sweep,
    // slotPendingRestoresAvailable) deliberately skips minimized windows,
    // and the unfloat sent below only flips a floating flag the daemon
    // applies to windows it already tracks — the new daemon session tracks
    // this window as neither snapped nor floating, so the unfloat no-ops
    // and the window never rejoins its zone.
    //
    // Discriminating "orphaned by a restart" from a NORMAL minimize cycle
    // needs both daemon states: minimize-float UNSNAPS the window
    // (SnapEngine::setWindowFloat(true) → unsnapForFloat), so in a normal
    // cycle it is absent from getSnappedWindows too — but it IS in the
    // daemon's floating set, and the unfloat below re-snaps it. Only a
    // window in NEITHER set is orphaned. Both queries are dispatched HERE,
    // before the unfloat call (asyncCall + watcher) below enters the same D-Bus send
    // queue, so the daemon answers them against the pre-unfloat state.
    //
    // The restore-on-orphan is deliberately scoped to an owned or adopted
    // minimize transition: an
    // unconditional net would fire resolveWindowRestore on every
    // unminimize of any never-tracked window, re-running its open-time
    // placement routing (RouteToDesktop) and churning the placement
    // store's per-app FIFO on a path that is not an open.
    //
    // Tracked-ness MUST be checked, not resolved blindly:
    // resolveWindowRestore consumes the single-shot FIFO pending-restore
    // entry for the window's appId, and burning it on a window the daemon
    // still owns robs a sibling window's restore. A failed query counts
    // as tracked for the same reason.
    if (window) {
        struct QueryJoin
        {
            int pending = 2;
            bool trackedOrFailed = false;
        };
        auto join = std::make_shared<QueryJoin>();
        QPointer<KWin::EffectWindow> safeWindow = window;
        const auto onReplyDone = [this, join, safeWindow, windowId, requestGeneration]() {
            if (--join->pending > 0 || join->trackedOrFailed) {
                return;
            }
            if (m_unfloatInFlight.value(windowId) != requestGeneration) {
                return;
            }
            // Same eligibility guards as slotPendingRestoresAvailable's
            // sweep, re-checked post-await: the window may have closed,
            // re-minimized, or left the current desktop/activity while
            // the queries were in flight (restoring an off-desktop window
            // would snap it through the wrong desktop's snap state).
            if (!safeWindow || safeWindow->isDeleted() || safeWindow->isMinimized() || !safeWindow->isOnCurrentDesktop()
                || !safeWindow->isOnCurrentActivity()) {
                return;
            }
            qCInfo(lcEffect) << "Snap: unminimized window is untracked by daemon — retrying restore:" << windowId;
            // Unminimize, not an open. Without the distinction the daemon's
            // cross-screen tile reclaim could TELEPORT the just-unminimized
            // window to its recorded home monitor.
            callResolveWindowRestore(safeWindow.data(), nullptr, /*releaseSuppressionOnMiss=*/true,
                                     PhosphorEngine::RestoreReason::Unminimize);
        };
        auto* snappedWatcher = new QDBusPendingCallWatcher(
            PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("getSnappedWindows")),
            this);
        connect(snappedWatcher, &QDBusPendingCallWatcher::finished, this,
                [join, windowId, onReplyDone](QDBusPendingCallWatcher* w) {
                    w->deleteLater();
                    QDBusPendingReply<QStringList> reply = *w;
                    if (!reply.isValid() || reply.value().contains(windowId)) {
                        join->trackedOrFailed = true;
                    }
                    onReplyDone();
                });
        auto* floatingWatcher = new QDBusPendingCallWatcher(
            PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("queryWindowFloating"), {windowId}),
            this);
        connect(floatingWatcher, &QDBusPendingCallWatcher::finished, this,
                [join, onReplyDone](QDBusPendingCallWatcher* w) {
                    w->deleteLater();
                    QDBusPendingReply<bool> reply = *w;
                    if (!reply.isValid() || reply.value()) {
                        join->trackedOrFailed = true;
                    }
                    onReplyDone();
                });
    }

    qCInfo(lcEffect) << "Snap: window unminimized, unfloating:" << windowId << "on" << screenId;

    auto* watcher =
        new QDBusPendingCallWatcher(PhosphorProtocol::ClientHelpers::asyncCall(
                                        PhosphorProtocol::Service::Interface::WindowTracking,
                                        QStringLiteral("setWindowFloatingForScreen"), {windowId, screenId, false}),
                                    this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, windowId, requestGeneration](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                const auto inFlight = m_unfloatInFlight.constFind(windowId);
                if (inFlight == m_unfloatInFlight.constEnd() || inFlight.value() != requestGeneration) {
                    return;
                }
                if (w->isError()) {
                    m_unfloatInFlight.remove(windowId);
                    m_minimizeFloatedWindows.insert(windowId);
                    qCWarning(lcEffect) << "Snap: unfloat request failed for" << windowId << ':'
                                        << w->error().message();
                    scheduleUnminimizeUnfloatRetry(windowId);
                    return;
                }

                auto* stateWatcher = new QDBusPendingCallWatcher(
                    PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                               QStringLiteral("queryWindowFloating"), {windowId}),
                    this);
                connect(stateWatcher, &QDBusPendingCallWatcher::finished, this,
                        [this, windowId, requestGeneration](QDBusPendingCallWatcher* stateCall) {
                            stateCall->deleteLater();
                            const auto current = m_unfloatInFlight.constFind(windowId);
                            if (current == m_unfloatInFlight.constEnd() || current.value() != requestGeneration) {
                                return;
                            }
                            QDBusPendingReply<bool> reply = *stateCall;
                            if (!reply.isValid()) {
                                m_unfloatInFlight.remove(windowId);
                                m_minimizeFloatedWindows.insert(windowId);
                                scheduleUnminimizeUnfloatRetry(windowId);
                                return;
                            }
                            if (reply.value()) {
                                m_unfloatInFlight.remove(windowId);
                                m_minimizeFloatedWindows.insert(windowId);
                                scheduleUnminimizeUnfloatRetry(windowId);
                                return;
                            }
                            m_unfloatInFlight.remove(windowId);
                            m_minimizeFloatedWindows.remove(windowId);
                            m_unfloatRetryAttempts.remove(windowId);
                            m_effect->slotWindowFloatingChanged(windowId, false, QString());
                        });
            });
}

void SnapHandler::scheduleUnminimizeUnfloatRetry(const QString& windowId)
{
    if (!m_effect->m_daemonGate.serviceRegistered || m_pendingUnminimizeUnfloat.contains(windowId)
        || m_unfloatRetryAttempts.value(windowId) >= kSnapMaxUnfloatRetries) {
        return;
    }
    KWin::EffectWindow* window = m_effect->findWindowById(windowId);
    if (!window || window->isDeleted() || window->isMinimized()) {
        return;
    }
    QPointer<KWin::EffectWindow> safeWindow = window;
    ++m_unfloatRetryAttempts[windowId];
    m_pendingUnminimizeUnfloat.schedule(windowId, kSnapUnfloatRetryDelayMs, [this, windowId, safeWindow]() {
        if (!safeWindow || safeWindow->isDeleted() || safeWindow->isMinimized()) {
            return;
        }
        const QString screenId = m_effect->getWindowScreenId(safeWindow.data());
        if (!m_minimizeFloatedWindows.contains(windowId)) {
            return;
        }
        if (m_effect->tilingHandler()->isManagedScreen(screenId)) {
            if (!m_effect->tilingHandler()->offerMinimizeEdge(safeWindow.data())) {
                // Same re-arm contract as the deferred-commit transfer above.
                qCInfo(lcEffect) << "Snap: autotile refused retry transfer, re-arming:" << windowId;
                scheduleUnminimizeUnfloatRetry(windowId);
            }
            return;
        }
        commitUnminimizeUnfloat(safeWindow.data(), windowId, screenId);
    });
}

void SnapHandler::retryVisibleMinimizeFloats()
{
    const auto windowIds = m_minimizeFloatedWindows.values();
    for (const QString& windowId : windowIds) {
        // A window mid-grace keeps its scheduled commit: firing here too
        // would land the moveResize mid-animation — the exact stutter the
        // #816 grace exists to prevent — and leave the armed timer to fire
        // against a consumed entry.
        if (m_pendingUnminimizeUnfloat.contains(windowId)) {
            continue;
        }
        KWin::EffectWindow* window = m_effect->findWindowById(windowId);
        if (!window || window->isDeleted() || window->isMinimized()) {
            // Budget stays: a still-minimized window's exhausted retries must
            // not be silently refunded by an unrelated screen-set change.
            continue;
        }
        const QString screenId = m_effect->getWindowScreenId(window);
        TilingHandler* autotile = m_effect->tilingHandler();
        if (autotile->isManagedScreen(screenId)) {
            // offerMinimizeEdge, not the void slot: the slot silently
            // returns on its entry gates (unhandleable, non-tileable), and a
            // refused transfer would otherwise leave the window floating
            // with no armed timer — recoverable only by another screen-set
            // change. Both sibling transfer sites use this refusal shape.
            // The budget refund happens per-arm, AFTER the hop offer: the
            // autotile adopt path seeds its own budget from
            // unfloatRetryBudgetUsed(), so refunding before the offer would
            // hand every adopted window a fresh budget (same rule as the
            // deferred-commit transfer's refund placement).
            if (!autotile->offerMinimizeEdge(window)) {
                qCInfo(lcEffect) << "Snap: autotile refused visible-float transfer, re-arming retry:" << windowId;
                m_unfloatRetryAttempts.remove(windowId);
                scheduleUnminimizeUnfloatRetry(windowId);
            }
            continue;
        }
        m_unfloatRetryAttempts.remove(windowId);
        commitUnminimizeUnfloat(window, windowId, screenId);
    }
}

void SnapHandler::slotSnapAssistReady(const QString& windowId, const QString& releaseScreenId,
                                      const PhosphorProtocol::EmptyZoneList& emptyZones)
{
    // Discard if a new drag has already started — this signal was from a
    // prior drop. The daemon defers the compute to after endDrag returns,
    // so by the time this slot fires the user may already be dragging again.
    if (m_effect->m_dragTracker->isDragging()) {
        qCDebug(lcEffect) << "Discarding snapAssistReady: new drag in progress";
        return;
    }
    if (emptyZones.isEmpty() || releaseScreenId.isEmpty()) {
        return;
    }
    m_effect->m_snapAssistHandler->asyncShow(windowId, releaseScreenId, emptyZones);
}

void SnapHandler::slotMoveSpecificWindowToZoneRequested(const QString& windowId, const QString& zoneId, int x, int y,
                                                        int width, int height)
{
    QRect geometry(x, y, width, height);
    if (!geometry.isValid()) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: invalid geometry" << geometry;
        return;
    }

    // Match by exact full window ID (appId|uuid) to distinguish
    // multiple windows of the same application. Fall back to appId only if
    // the exact match fails (e.g. window was recreated between candidate build
    // and selection).
    KWin::EffectWindow* targetWindow = nullptr;
    const auto windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow* w : windows) {
        // !isDeleted: a close-grabbed dying instance can still carry the
        // exact requested id (the recreated-window scenario the appId
        // fallback below exists for) — snapping it would track a dead id
        // with no future close event to clean it, and block the fallback
        // from finding the live sibling.
        if (w && !w->isDeleted() && m_effect->shouldHandleWindow(w) && m_effect->getWindowId(w) == windowId) {
            targetWindow = w;
            break;
        }
    }
    if (!targetWindow) {
        // appId fallback (window recreated between candidate build and
        // selection) — only when UNAMBIGUOUS: with two same-app windows,
        // taking the first stacking-order match would snap (and track) the
        // wrong sibling. Mirrors findWindowById's matchCount guard.
        const QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);
        KWin::EffectWindow* appMatch = nullptr;
        int matchCount = 0;
        for (KWin::EffectWindow* w : windows) {
            if (w && !w->isDeleted() && m_effect->shouldHandleWindow(w)
                && ::PhosphorIdentity::WindowId::extractAppId(m_effect->getWindowId(w)) == appId) {
                appMatch = w;
                ++matchCount;
            }
        }
        if (matchCount == 1) {
            targetWindow = appMatch;
        }
    }

    if (!targetWindow) {
        qCWarning(lcEffect) << "slotMoveSpecificWindowToZoneRequested: window not found" << windowId;
        m_effect->emitNavigationFeedback(false, QStringLiteral("snap_assist"), QStringLiteral("window_not_found"));
        return;
    }

    // Capture geometry BEFORE applyWindowGeometry resizes the window. The async D-Bus
    // callback in ensurePreSnapGeometryStored would read frameGeometry() after the
    // resize, corrupting the pre-tile entry with zone dimensions.
    ensurePreSnapGeometryStored(targetWindow, m_effect->getWindowId(targetWindow), targetWindow->frameGeometry());
    m_effect->applyWindowGeometry(targetWindow, geometry);

    // Derive screen from the applied geometry center. Use resolveEffectiveScreenId
    // to get the virtual screen ID (not just the physical output).
    QPoint geoCenter = geometry.center();
    const auto* output = KWin::effects->screenAt(geoCenter);
    QString screenId =
        output ? m_effect->resolveEffectiveScreenId(geoCenter, output) : m_effect->getWindowScreenId(targetWindow);

    if (m_effect->isDaemonReady("snap assist windowSnapped")) {
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Snap,
                                                       QStringLiteral("windowSnapped"),
                                                       {m_effect->getWindowId(targetWindow), zoneId, screenId});
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Snap,
                                                       QStringLiteral("recordSnapIntent"),
                                                       {m_effect->getWindowId(targetWindow), true});

        const bool isAutotile = m_effect->tilingHandler()->isManagedScreen(screenId);

        // Snap-assist placed the window in a zone — record it in snapping's
        // border set, but only for a resolved snap-mode screen. An empty
        // (unresolved) or autotile-managed screen is owned by TilingHandler,
        // so recording it here would double-track the window — same
        // discriminator as slotApplyGeometryRequested / the async snap path.
        if (!screenId.isEmpty() && !isAutotile) {
            const QString wid = m_effect->getWindowId(targetWindow);
            markWindowSnapped(wid, screenId);
            // Snapped — re-resolve Mode / IsSnapped rules now instead of waiting
            // for the daemon broadcast, consistent with the drag-commit paths.
            m_effect->invalidateRuleCacheForStateChange(wid);
        }

        // Snap Assist continuation: only for manual-mode screens.
        // Autotile screens manage their own window placement; showing snap assist
        // after an autotile resnap is incorrect (the daemon silently ignores the
        // selection anyway via the isManagedScreen guard in signals.cpp).
        if (!isAutotile) {
            m_effect->m_snapAssistHandler->showContinuationIfNeeded(screenId);
        }
    }
}

void SnapHandler::slotSnapAllWindowsRequested(const QString& screenId)
{
    qCInfo(lcEffect) << "Snap all windows requested for screen:" << screenId;

    if (!m_effect->isDaemonReady("snap all windows")) {
        return;
    }

    // Async fetch all snapped windows to filter already-snapped ones locally
    QDBusPendingCall snapCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* snapWatcher = new QDBusPendingCallWatcher(snapCall, this);

    connect(snapWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* sw) {
        sw->deleteLater();

        QDBusPendingReply<QStringList> snapReply = *sw;
        QSet<QString> snappedFullIds;
        QSet<QString> snappedAppIds;
        if (snapReply.isValid()) {
            for (const QString& id : snapReply.value()) {
                snappedFullIds.insert(id);
                snappedAppIds.insert(::PhosphorIdentity::WindowId::extractAppId(id));
            }
        }

        // Collect unsnapped, non-floating windows on this screen in stacking order
        // (bottom-to-top) so lower windows get lower-numbered zones deterministically
        QStringList unsnappedWindowIds;
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* w : windows) {
            // !isDeleted: a close-grabbed dying window would get a zone
            // assigned under a dead id (slotWindowClosed already ran, so
            // nothing ever cleans the resulting snap record).
            if (!w || w->isDeleted() || !m_effect->shouldHandleWindow(w)) {
                continue;
            }

            QString windowId = m_effect->getWindowId(w);
            QString appId = ::PhosphorIdentity::WindowId::extractAppId(windowId);

            // User-initiated snap commands override floating state.
            // windowSnapped() on the daemon clears floating inside SnapEngine::commitSnap (clearFloatingForSnap).

            // Always use EDID-based screen ID for comparison
            QString winScreen = m_effect->getWindowScreenId(w);
            if (winScreen != screenId) {
                qCDebug(lcEffect) << "snap-all: skipping window on different screen" << appId;
                continue;
            }

            if (w->isMinimized() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
                qCDebug(lcEffect) << "snap-all: skipping minimized/other-desktop window" << appId;
                continue;
            }

            // Full ID match first (distinguishes multi-instance apps),
            // appId fallback for single-instance apps
            if (snappedFullIds.contains(windowId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window" << appId;
                continue;
            }
            if (!m_effect->hasOtherWindowOfClassWithDifferentPid(w) && snappedAppIds.contains(appId)) {
                qCDebug(lcEffect) << "snap-all: skipping already-snapped window (appId match)" << appId;
                continue;
            }

            unsnappedWindowIds.append(windowId);
        }

        qCDebug(lcEffect) << "snap-all: found" << unsnappedWindowIds.size() << "unsnapped windows to snap";

        if (unsnappedWindowIds.isEmpty()) {
            qCDebug(lcEffect) << "No unsnapped windows to snap on screen" << screenId;
            m_effect->emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("no_unsnapped_windows"),
                                             QString(), QString(), screenId);
            return;
        }

        if (!m_effect->isDaemonReady("snap all windows calculation")) {
            return;
        }

        // Ask daemon to calculate zone assignments
        QDBusPendingCall calcCall = PhosphorProtocol::ClientHelpers::asyncCall(
            PhosphorProtocol::Service::Interface::Snap, QStringLiteral("calculateSnapAllWindows"),
            {QVariant::fromValue(unsnappedWindowIds), screenId});
        auto* calcWatcher = new QDBusPendingCallWatcher(calcCall, this);

        connect(calcWatcher, &QDBusPendingCallWatcher::finished, this, [this, screenId](QDBusPendingCallWatcher* cw) {
            cw->deleteLater();

            QDBusPendingReply<PhosphorProtocol::SnapAllResultList> calcReply = *cw;
            if (calcReply.isError()) {
                qCWarning(lcEffect) << "calculateSnapAllWindows failed:" << calcReply.error().message();
                m_effect->emitNavigationFeedback(false, QStringLiteral("snap_all"), QStringLiteral("calculation_error"),
                                                 QString(), QString(), screenId);
                return;
            }

            PhosphorProtocol::SnapAllResultList snapResults = calcReply.value();

            // Build WindowGeometryList for the batch geometry path. Stamp the
            // screen the batch was computed for onto every entry:
            // toGeometryEntry() has no screen to give (SnapAllResultEntry
            // carries none), and an EMPTY screenId makes the batch consumer's
            // discriminator take the float/restore arm — clearWindowSnapped
            // instead of markWindowSnapped — so no snap-all window would ever
            // enter the border set. The collection loop above already skipped
            // any window whose screen differs, so this id IS each window's
            // real screen.
            PhosphorProtocol::WindowGeometryList snapGeometries;
            snapGeometries.reserve(snapResults.size());
            for (const auto& r : snapResults) {
                PhosphorProtocol::WindowGeometryEntry entry = r.toGeometryEntry();
                entry.screenId = screenId;
                snapGeometries.append(entry);
            }
            m_effect->slotApplyGeometriesBatch(snapGeometries, QStringLiteral("snap_all"));

            // Confirm snap assignments with daemon
            if (m_effect->isDaemonReady("snap-all confirmation")) {
                PhosphorProtocol::SnapConfirmationList confirmEntries;
                for (const auto& r : snapResults) {
                    PhosphorProtocol::SnapConfirmationEntry entry;
                    entry.windowId = r.windowId;
                    entry.zoneId = r.targetZoneId;
                    entry.screenId = screenId;
                    entry.isRestore = false;
                    confirmEntries.append(entry);
                }
                if (!confirmEntries.isEmpty()) {
                    PhosphorProtocol::ClientHelpers::fireAndForget(
                        m_effect, PhosphorProtocol::Service::Interface::Snap, QStringLiteral("windowsSnappedBatch"),
                        {QVariant::fromValue(confirmEntries)}, QStringLiteral("windowsSnappedBatch"));
                }
            }
        });
    });
}

void SnapHandler::slotPendingRestoresAvailable()
{
    // If slotDaemonReady already dispatched snap restores for this daemon
    // session, skip — both signals fire during restart, and the second round
    // of moveResize() calls would disrupt the stacking order that the first
    // round carefully preserves via activateWindow(previouslyActive).
    if (m_effect->m_daemonGate.readyRestoresDone) {
        qCInfo(lcEffect) << "Pending restores: already handled by slotDaemonReady, skipping";
        return;
    }

    qCInfo(lcEffect) << "Pending restores: retrying restoration for all visible windows";

    if (!m_effect->isDaemonReady("pending restores")) {
        return;
    }

    // Use ASYNC batch call to get all tracked windows at once
    QDBusPendingCall pendingCall = PhosphorProtocol::ClientHelpers::asyncCall(
        PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getSnappedWindows"));
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QDBusPendingReply<QStringList> reply = *w;
        QSet<QString> trackedWindowIds;

        if (reply.isValid()) {
            // Track by FULL windowId (appId|uuid), NOT appId. A multi-window app
            // (e.g. several ghostty terminals, each snapped to its own zone) has one
            // tracked entry PER window; deduping by appId would treat the whole app
            // as "handled" the moment ONE of its windows restored, and skip every
            // sibling below — including a window that individually failed its early
            // restore (it raced startup and got a not-ready no-snap) and is the exact
            // window this retry net exists to recover. The daemon tracks restored
            // windows by their live id, which matches getWindowId() here.
            const QStringList trackedWindows = reply.value();
            for (const QString& windowId : trackedWindows) {
                if (!windowId.isEmpty()) {
                    trackedWindowIds.insert(windowId);
                }
            }
            qCDebug(lcEffect) << "Got" << trackedWindowIds.size() << "tracked windows from daemon";
        } else {
            qCWarning(lcEffect) << "Failed to get tracked windows:" << reply.error().message();
            // Continue anyway - will try to restore all windows (daemon will handle duplicates)
        }

        // Now iterate through all visible windows and restore untracked ones
        const auto windows = KWin::effects->stackingOrder();
        for (KWin::EffectWindow* window : windows) {
            // !isDeleted: a close-grabbed dying window would consume the
            // single-shot FIFO pending-restore entry for its appId, robbing
            // the app's next REAL window of its restore.
            if (!window || window->isDeleted() || !m_effect->shouldHandleWindow(window)) {
                continue;
            }

            // Skip minimized or invisible windows
            if (window->isMinimized() || !window->isOnCurrentDesktop() || !window->isOnCurrentActivity()) {
                continue;
            }

            // Check if THIS window is already tracked (exact id, O(1)). A snapped
            // sibling of the same app no longer masks an untracked window here.
            QString windowId = m_effect->getWindowId(window);
            if (trackedWindowIds.contains(windowId)) {
                continue; // Already tracked
            }

            // Window is not tracked - try to restore it.
            // PendingSweep: the pending-restores sweep re-resolves
            // already-open windows; it must not drive the cross-screen tile
            // reclaim and move windows the user is looking at.
            qCDebug(lcEffect) << "Retrying restoration for untracked window:" << windowId;
            callResolveWindowRestore(window, nullptr, /*releaseSuppressionOnMiss=*/true,
                                     PhosphorEngine::RestoreReason::PendingSweep);
        }
    });
}

void SnapHandler::armDesktopArrivalRestore(const QString& windowId)
{
    if (windowId.isEmpty()) {
        // The daemon has already moved this window off the visible desktop, so
        // an unresolvable id means it is parked nowhere and nothing will restore
        // it. Logged rather than dropped silently, because every other exit in
        // this pair is traceable and this one strands a window.
        qCDebug(lcEffect) << "Desktop-arrival park skipped: empty window id";
        return;
    }
    m_awaitingDesktopArrivalRestore.insert(windowId);
    qCDebug(lcEffect) << "Parked for snap restore on desktop arrival:" << windowId;
}

void SnapHandler::slotDesktopChangedRestoreArrivals()
{
    if (m_awaitingDesktopArrivalRestore.isEmpty()) {
        return;
    }
    if (!m_effect->isDaemonReady("desktop-arrival snap restore")) {
        // Nothing to drive the restore against. The park is left in place for a
        // transient stall, but a real daemon loss runs clearSnapTracking, which
        // drops the set — a reconnecting daemon reloads its store from disk, so
        // its bringup stacking sweep is the correct retry rather than a park
        // armed against the previous daemon's records.
        return;
    }

    // Resolve against the LIVE window set rather than walking the id set: an id
    // whose window is gone must be dropped, not carried forever.
    QHash<QString, KWin::EffectWindow*> live;
    for (KWin::EffectWindow* w : KWin::effects->stackingOrder()) {
        if (!w || w->isDeleted()) {
            continue;
        }
        const QString id = m_effect->getWindowId(w);
        if (m_awaitingDesktopArrivalRestore.contains(id)) {
            live.insert(id, w);
        }
    }

    for (const QString& windowId : QSet<QString>(m_awaitingDesktopArrivalRestore)) {
        KWin::EffectWindow* window = live.value(windowId);
        if (!window) {
            // Closed while parked, or its id changed under it. Either way no
            // restore is possible and the entry is spent.
            m_awaitingDesktopArrivalRestore.remove(windowId);
            continue;
        }
        drainDesktopArrivalFor(windowId, window);
    }
}

bool SnapHandler::drainDesktopArrivalFor(const QString& windowId, KWin::EffectWindow* window)
{
    if (!window || !m_awaitingDesktopArrivalRestore.contains(windowId)) {
        return false;
    }
    // Measured against the window's OWN output, matching the arm in
    // slotWindowDesktopMoveRequested. isOnCurrentDesktop() reads the global
    // current desktop, which under per-output virtual desktops both
    // over-fires (some other output switched to this window's desktop, so
    // it is still not visible where it lives) and under-fires (its own
    // output switched to it while the global current is elsewhere, so the
    // park would sit unspent until an unrelated switch). Falls back to the
    // global reading when the window has no output.
    KWin::LogicalOutput* const out = window->screen();
    KWin::VirtualDesktop* const shownHere = out ? KWin::effects->currentDesktop(out) : nullptr;
    const bool desktopInView = shownHere ? window->isOnDesktop(shownHere) : window->isOnCurrentDesktop();
    if (!desktopInView || !window->isOnCurrentActivity()) {
        return false; // Still waiting for its desktop.
    }
    if (window->isMinimized()) {
        // KWin can bring a session's windows back minimized, and nothing
        // re-drives the restore on unminimize (that path drives the unfloat
        // retries, not this one). So the park is KEPT rather than spent: a
        // later desktop switch is the only remaining chance to place this
        // window, and onWindowClosed drops the entry if it never comes.
        return false;
    }
    if (!m_effect->shouldHandleWindow(window)) {
        // Never going to be placed by this handler, so the park is spent
        // rather than carried for a restore that cannot happen.
        m_awaitingDesktopArrivalRestore.remove(windowId);
        return false;
    }
    // Snap-mode screens only. A window that landed on a tiling or scrolling
    // screen is re-announced by that handler's own desktop-return catch-scan
    // (TilingHandler::slotScreensChanged), and driving the snap resolve at it
    // as well risks the no-match float default landing on a window the tiling
    // engine is about to adopt. Spent, because that handler now owns it.
    const QString screenId = m_effect->getWindowScreenId(window);
    if (m_effect->tilingHandler()->isManagedScreen(screenId)) {
        m_awaitingDesktopArrivalRestore.remove(windowId);
        return false;
    }

    // Spend the park BEFORE dispatching: the restore is a one-shot, and an
    // entry left behind would re-drive on every later desktop switch — the
    // repeated-float-restore failure the member's comment describes, just
    // reached by a different route.
    m_awaitingDesktopArrivalRestore.remove(windowId);

    // DesktopArrival: not an open, so the daemon's cross-desktop restore arm
    // does not fire a second
    // time and bouncing the window straight back off the desktop it just
    // reached.
    qCInfo(lcEffect) << "Desktop arrival: re-driving snap restore for" << windowId << "on" << screenId;
    callResolveWindowRestore(window, nullptr, /*releaseSuppressionOnMiss=*/true,
                             PhosphorEngine::RestoreReason::DesktopArrival);
    return true;
}

} // namespace PlasmaZones
