// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotilehandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/windowanimator.h"
#include "handlers/navigationhandler.h"
#include "handlers/snaphandler.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorProtocol/WindowMarshalling.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QTimer>
#include <QtMath>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

AutotileHandler::AutotileHandler(PlasmaZonesEffect* effect, QObject* parent)
    : QObject(parent)
    , m_effect(effect)
{
}

QSize AutotileHandler::declaredMinSize(KWin::EffectWindow* w)
{
    int minWidth = 0;
    int minHeight = 0;
    KWin::Window* kw = w ? w->window() : nullptr;
    // Internal windows (our own overlays) crash on minSize(); see discussion #511.
    if (kw && !kw->isInternal()) {
        const QSizeF minSize = kw->minSize();
        if (minSize.isValid()) {
            minWidth = qCeil(minSize.width());
            minHeight = qCeil(minSize.height());
        }
    }
    return QSize(minWidth, minHeight);
}

void AutotileHandler::handleCursorMoved(const QPointF& pos, const QString& screenId)
{
    if (!m_focusFollowsMouse || m_autotileScreens.isEmpty()) {
        return;
    }

    // Pause FFM entirely during show-desktop/peek. Peeked windows are hidden
    // from the scene but keep their frameGeometry, so the scan below would
    // find one under the cursor and activateWindow() would synchronously
    // cancel the peek (Workspace::activateWindow unhides on activation).
    // Peek is hover-driven, so without this bail it collapses on the very
    // first cursor move.
    if (PlasmaZonesEffect::isShowingDesktop()) {
        return;
    }

    // Only act on autotile screens (screenId already resolved by caller)
    if (screenId.isEmpty() || !m_autotileScreens.contains(screenId)) {
        return;
    }

    // Pause focus-follows-mouse while the currently active window is one we
    // would refuse to focus via FFM anyway: excluded app, dialog, popup,
    // keep-above overlay, or a window below the min-size threshold. Without
    // this, the user opens an emoji picker or notification inside a zone,
    // moves the cursor across the underlying tiled window's visible area, and
    // FFM activates that tiled window first, sending the just-opened popup
    // straight to the background (discussion #461 item 3 follow-up).
    // Resumes naturally on the next cursor move once a tileable window is
    // active. Scoped to the same screen as the cursor so an unrelated focused
    // window on another monitor never freezes FFM here.
    if (KWin::EffectWindow* active = KWin::effects->activeWindow()) {
        if (!PlasmaZonesEffect::isOwnPassthroughOverlayClass(active->windowClass())
            && m_effect->getWindowScreenId(active) == screenId) {
            // Filter first, then size-check. This mirrors the under-cursor
            // guard below so the two predicates stay structurally aligned.
            // The cheap-to-skip min-size check is only paid when the active
            // window is otherwise tileable.
            if (!m_effect->isTileableWindow(active) || !m_effect->shouldHandleWindow(active)) {
                return;
            }
            // Also pause for floating active windows. FloatingCache covers
            // both manually-floated windows and overflow windows that the
            // daemon auto-floated past the maxWindows cap (applyFloatCleanup
            // path). Either kind is perched on top of the tiled stack while
            // the user works in it, so activating an underlying tiled window
            // on cursor wander sends the floating one straight to the
            // background — the same regression the excluded-active guard
            // above fixes (discussion #461 follow-up).
            if (m_effect->isWindowFloating(m_effect->getWindowId(active))) {
                return;
            }
            const QRectF aframe = active->frameGeometry();
            if ((m_effect->m_cachedMinWindowWidth > 0 && aframe.width() < m_effect->m_cachedMinWindowWidth)
                || (m_effect->m_cachedMinWindowHeight > 0 && aframe.height() < m_effect->m_cachedMinWindowHeight)) {
                return;
            }
        }
    }

    // Find the topmost autotile-managed window under the cursor.
    // Iterate stacking order in reverse (top → bottom).
    const auto windows = KWin::effects->stackingOrder();
    for (int i = windows.size() - 1; i >= 0; --i) {
        KWin::EffectWindow* w = windows[i];
        // isDeleted: a close-grabbed dying window under the cursor must not
        // pause FFM via the occlusion bail (mirrors the snap FFM guard).
        // isHiddenByShowDesktop: belt-and-braces behind the showing-desktop
        // bail above, for the frame where peek engages mid-scan.
        if (!w || w->isDeleted() || w->isMinimized() || w->isHiddenByShowDesktop() || !w->isOnCurrentDesktop()
            || !w->isOnCurrentActivity()) {
            continue;
        }
        // Geometry check first (cheap QRectF::contains) before shouldHandleWindow (allocates via windowClass())
        if (!w->frameGeometry().contains(pos)) {
            continue;
        }
        // Look through the daemon's own passthrough overlay surface — it is
        // full-screen and always topmost on the autotile monitor, so the bail
        // below would otherwise kill FFM forever (discussion #461 #3). The
        // editor is deliberately NOT looked through here: it is an interactive
        // fullscreen window, so it falls to the occluder bail below and FFM
        // leaves focus on it rather than stealing to the tiled window beneath.
        if (PlasmaZonesEffect::isOwnPassthroughOverlayClass(w->windowClass())) {
            continue;
        }
        // A non-autotile window (excluded app, keep-above overlay, popup, dialog,
        // Spectacle, etc.) occludes the cursor — don't look through it to focus a
        // tiled window beneath. This prevents focus-stealing from emoji pickers,
        // screenshot tools, and other excluded/overlay windows.
        if (!m_effect->isTileableWindow(w) || !m_effect->shouldHandleWindow(w)) {
            return;
        }
        // Also block focus for windows below the minimum size threshold.
        // These are normal windows (pass isTileableWindow) but too small
        // for autotile — e.g., emoji picker, small utilities. Without this,
        // hovering over them triggers auto-focus even though they're not tiled.
        {
            const QRectF frame = w->frameGeometry();
            if ((m_effect->m_cachedMinWindowWidth > 0 && frame.width() < m_effect->m_cachedMinWindowWidth)
                || (m_effect->m_cachedMinWindowHeight > 0 && frame.height() < m_effect->m_cachedMinWindowHeight)) {
                return;
            }
        }
        // Skip the activateWindow call when the window under the cursor
        // already holds compositor focus. The live activeWindow() read is
        // load-bearing: a local "last auto-focused window" cache would go
        // stale every time focus moved through another path (keyboard
        // shortcut, click, daemon-driven activate, focus-stealing window),
        // and the next cursor pass over the originally-cached window would
        // short-circuit without re-focusing it. See discussion #461 item 13.
        if (w == KWin::effects->activeWindow()) {
            return; // Already focused — no-op
        }
        // Only focus windows on autotile screens
        if (!m_autotileScreens.contains(m_effect->getWindowScreenId(w))) {
            return;
        }
        KWin::effects->activateWindow(w);
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Integration points
// ═══════════════════════════════════════════════════════════════════════════════

bool AutotileHandler::notifyWindowAdded(KWin::EffectWindow* w, bool knownFreeFloating)
{
    // Deleted windows bail before getWindowId (cache-pollution hazard).
    if (!w || w->isDeleted()) {
        return false;
    }

    const QString windowId = m_effect->getWindowId(w);

    bool minimizedOnly = false;
    if (!isEligibleForAutotileNotify(w, &minimizedOnly)) {
        if (knownFreeFloating && minimizedOnly && m_initialScreenQueryPending) {
            m_pendingFreshWindows.insert(windowId);
        }
        return false;
    }

    if (m_notifiedWindows.contains(windowId)) {
        return false;
    }

    const QString screenId = m_effect->getWindowScreenId(w);
    if (knownFreeFloating && m_initialScreenQueryPending && !m_autotileScreens.contains(screenId)) {
        // Retain spawn provenance until the initial screen query reveals
        // whether this window belongs in its follow-up batch.
        m_pendingFreshWindows.insert(windowId);
    }

    // Only notify autotile daemon for windows on autotile screens
    if (m_autotileScreens.contains(screenId)) {
        // Consume the spawn-provenance marker UNCONDITIONALLY — a short-circuit
        // (|| with remove second) would leave the entry behind whenever the
        // caller already passed true, and a later RE-ADD that deliberately
        // passes false would then flip to true off the stale entry.
        const bool wasFresh = m_pendingFreshWindows.remove(windowId) > 0;
        knownFreeFloating = knownFreeFloating || wasFresh;
        m_notifiedWindows.insert(windowId);
        m_notifiedWindowScreens[windowId] = screenId;
        // Save pre-autotile geometry BEFORE the daemon tiles the window.
        // Without this, a window launched directly into autotile has no saved
        // geometry — floating it would leave it at its tiled position instead
        // of restoring to its original free-floating size.
        //
        // knownFreeFloating defaults true for the genuine window-opened path
        // (the frame is KWin's spawn geometry — the authoritative pre-autotile
        // position — and the FloatingCache is not yet populated, so the
        // isWindowFloating() guard would otherwise drop the one-shot save).
        // RE-ADD callers pass false so the floating guard runs and rejects a
        // tiled zone rect instead of persisting it as free geometry.
        saveAndRecordPreAutotileGeometry(windowId, screenId, w, w->frameGeometry(), knownFreeFloating);

        const QSize minSize = declaredMinSize(w);

        auto* watcher = new QDBusPendingCallWatcher(
            PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::Autotile,
                                                       QStringLiteral("windowOpened"),
                                                       {windowId, screenId, minSize.width(), minSize.height()}),
            this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, windowId](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            if (w->isError()) {
                qCWarning(lcEffect) << "windowOpened D-Bus call failed for" << windowId << ":" << w->error().message();
                m_notifiedWindows.remove(windowId);
                m_notifiedWindowScreens.remove(windowId);
                // notifyWindowAdded() returned true on the synchronous
                // path, so the caller (PlasmaZonesEffect::slotWindowAdded)
                // left first-frame open suppression engaged expecting a
                // moveResize from the daemon's tile decision. The D-Bus
                // call failed — no moveResize is coming — so release
                // suppression here rather than letting the window sit
                // invisible until the 250 ms deadline. Exact-id re-check:
                // the fuzzy appId fallback could resolve a same-app sibling
                // for a just-closed window, ending the sibling's suppression
                // early.
                if (KWin::EffectWindow* effectWindow = m_effect->findWindowById(windowId);
                    effectWindow && m_effect->getWindowId(effectWindow) == windowId) {
                    m_effect->endRestoreSuppression(effectWindow);
                }
            }
        });
        qCDebug(lcEffect) << "Notified autotile: windowOpened" << windowId << "on screen" << screenId
                          << "minSize:" << minSize.width() << "x" << minSize.height();
        return true;
    }
    return false;
}

void AutotileHandler::notifyWindowsAddedBatch(const QList<KWin::EffectWindow*>& windows,
                                              const QSet<QString>& screenFilter, bool resetNotified,
                                              bool enteringAutotile)
{
    // Collect eligible windows using the same filtering as notifyWindowAdded,
    // then send one batch D-Bus call instead of per-window round-trips.
    PhosphorProtocol::WindowOpenedList batchEntries;
    QStringList batchWindowIds; // for error rollback

    for (KWin::EffectWindow* w : windows) {
        // Deleted windows bail before any id/screen lookup (cache-pollution hazard).
        if (!w || w->isDeleted()) {
            continue;
        }

        const QString windowId = m_effect->getWindowId(w);
        const QString screenId = m_effect->getWindowScreenId(w);
        if (!screenFilter.isEmpty() && !screenFilter.contains(screenId)) {
            continue;
        }

        // Reset BEFORE the eligibility check: a window that is currently
        // ineligible (minimized, fullscreen) must still shed its stale
        // m_notifiedWindows entry on a re-announce cycle, or its later
        // notifyWindowAdded (unminimize, exit-fullscreen) hits the
        // already-notified bail and silently never announces it.
        if (resetNotified) {
            m_notifiedWindows.remove(windowId);
        }

        bool minimizedOnly = false;
        if (!isEligibleForAutotileNotify(w, &minimizedOnly)) {
            if (minimizedOnly) {
                claimAlreadyMinimizedAsFloated(w, windowId, screenFilter, enteringAutotile);
            }
            continue;
        }

        if (!m_autotileScreens.contains(screenId)) {
            continue;
        }

        if (m_notifiedWindows.contains(windowId)) {
            continue;
        }
        m_notifiedWindows.insert(windowId);
        m_notifiedWindowScreens[windowId] = screenId;

        // Existing windows use the guarded path. A window first observed while
        // the initial screen query was pending retains explicit spawn provenance.
        const bool knownFreeFloating = m_pendingFreshWindows.remove(windowId)
            || (enteringAutotile && !AutotileStateHelpers::isTiledWindow(m_border, windowId));
        saveAndRecordPreAutotileGeometry(windowId, screenId, w, w->frameGeometry(), knownFreeFloating);

        const QSize minSize = declaredMinSize(w);

        PhosphorProtocol::WindowOpenedEntry entry;
        entry.windowId = windowId;
        entry.screenId = screenId;
        entry.minWidth = minSize.width();
        entry.minHeight = minSize.height();
        batchEntries.append(entry);
        batchWindowIds.append(windowId);
    }

    if (batchEntries.isEmpty()) {
        return;
    }

    auto* watcher =
        new QDBusPendingCallWatcher(PhosphorProtocol::ClientHelpers::asyncCall(
                                        PhosphorProtocol::Service::Interface::Autotile,
                                        QStringLiteral("windowsOpenedBatch"), {QVariant::fromValue(batchEntries)}),
                                    this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, batchWindowIds](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (w->isError()) {
            qCWarning(lcEffect) << "windowsOpenedBatch D-Bus call failed:" << w->error().message();
            // Tracking rollback only — deliberately NO endRestoreSuppression
            // here, unlike notifyWindowAdded's error handler: the batch paths
            // (daemon-restart re-announce, toggle-on) serve windows that are
            // still physically tiled and may be re-announced when the daemon
            // returns.
            for (const QString& wid : batchWindowIds) {
                m_notifiedWindows.remove(wid);
                m_notifiedWindowScreens.remove(wid);
            }
        }
    });
    qCInfo(lcEffect) << "Notified autotile: windowsOpenedBatch with" << batchEntries.size() << "windows";
}

void AutotileHandler::handleWindowOutputChanged(KWin::EffectWindow* w)
{
    if (!w || w->isDeleted() || m_inOutputChanged) {
        return;
    }
    // Re-entrancy guard: geometry changes from onWindowClosed/notifyWindowAdded
    // could trigger outputChanged again if tiling moves the window across screens.
    m_inOutputChanged = true;
    const auto guard = qScopeGuard([this] {
        m_inOutputChanged = false;
    });

    const QString windowId = m_effect->getWindowId(w);
    const QString newScreenId = m_effect->getWindowScreenId(w);

    // Consume the one-shot cross-mode marker HERE, at the top, into a local:
    // every branch below used to be responsible for erasing it and three
    // early-return paths (untracked-guard miss, same-screen, neither-screen-
    // autotile) leaked it — a stale marker then swallowed the NEXT genuine
    // cross-monitor move by taking the bookkeeping-only path. Destination
    // matching stays each consumer's job.
    const QString expectedScreen = m_expectedOutputMove.take(windowId);

    if (!m_notifiedWindows.contains(windowId)) {
        // Window not tracked — but if it moved TO an autotile screen, add it
        if (m_autotileScreens.contains(newScreenId) && m_effect->shouldHandleWindow(w) && !w->isMinimized()
            && w->isOnCurrentDesktop() && w->isOnCurrentActivity()) {
            // A cross-mode SWAP arms windowOutputMoveExpected for the snap partner
            // migrating onto this (autotile) source screen. That partner is untracked
            // effect-side, so its outputChanged lands here rather than the tracked
            // branch below — its arrival IS the marker's expected echo, so consume
            // the one-shot (mirroring the tracked-branch erase) instead of stranding
            // it. The daemon already placed the window via handoffReceive; the
            // notifyWindowAdded below establishes the effect-side tracking the daemon
            // does not touch, and is a no-op daemon-side (insertWindow rejects an
            // already-tracked window).
            // Destination match, mirroring the tracked branch: a stale marker
            // armed for a DIFFERENT screen means a later genuine move
            // superseded it, and treating that move as expected would flip
            // knownFreeFloating and silently drop a fresh window's geometry
            // capture.
            const bool expectedMove = !expectedScreen.isEmpty() && expectedScreen == newScreenId;
            notifyWindowAdded(w, /*knownFreeFloating=*/!expectedMove);
            m_effect->updateAllDecorations();
        }
        return;
    }

    const QString oldScreenId = m_notifiedWindowScreens.value(windowId);

    if (oldScreenId.isEmpty() || oldScreenId == newScreenId) {
        return; // Same screen or unknown — no transfer needed
    }

    const bool oldIsAutotile = m_autotileScreens.contains(oldScreenId);
    const bool newIsAutotile = m_autotileScreens.contains(newScreenId);

    if (!oldIsAutotile && !newIsAutotile) {
        // Neither screen is autotiled — snapping unsnap is handled by the
        // effect's outputChanged. Still refresh the notified-screen record:
        // a window that left autotile via the desktop-switch float skip
        // stays tracked, and a stale screen here would make every later
        // frameGeometryChanged re-detect a phantom VS crossing.
        if (m_notifiedWindowScreens.contains(windowId)) {
            m_notifiedWindowScreens[windowId] = newScreenId;
        }
        return;
    }

    // Daemon-owned cross-output move: the daemon already migrated its tiling state
    // onto newScreenId and reflowed both outputs; KWin's outputChanged is the
    // expected echo. Re-issuing windowClosed/windowOpened now would re-resolve the
    // close to the destination (the daemon's map already points there) and tear
    // down the placement, so the source reflow never lands — update our bookkeeping
    // and move the decoration claim, then stop. Consume the marker one-shot; honour
    // it only when the destination matches (a mismatch means a later genuine move
    // superseded it → fall through to the normal transfer).
    if (!expectedScreen.isEmpty() && expectedScreen == newScreenId) {
        if (newIsAutotile) {
            m_notifiedWindowScreens[windowId] = newScreenId;
        } else {
            // Cross-MODE move: window left autotile for a SNAP screen. Drop
            // effect-side autotile tracking (daemon already relinquished via
            // handoffRelease) — else it lingers phantom.
            cleanupAutotileTracking(windowId, oldScreenId);
        }
        m_effect->updateAllDecorations();
        return;
    }

    qCInfo(lcEffect) << "Window moved between monitors:" << windowId << oldScreenId << "->" << newScreenId;

    // Snapshot the pre-autotile geometry BEFORE onWindowClosed clears it
    // (the close-cleanup sweeps the geometry out of EVERY screen bucket).
    QRectF savedPreAutotileGeo;
    if (oldIsAutotile) {
        savedPreAutotileGeo = findPreAutotileGeometry(windowId);
    }

    // The predicate mirrors the re-add condition below.
    const bool willReAdd = newIsAutotile && !w->isMinimized() && w->isOnCurrentDesktop() && w->isOnCurrentActivity();

    // Minimize-float ownership must SURVIVE the transfer: onWindowClosed's
    // cleanup wipes it wholesale, the window is alive and still floated
    // daemon-side, and willReAdd is false for a minimized window — without
    // re-establishing the claim (or handing it to the snap handler for a
    // snap destination) nobody ever unfloats the window and it stays
    // floating until the next mode toggle. Reachable in bulk on monitor
    // hotplug and VS reconfigure.
    const bool ownedMinimizeFloat = m_minimizeFloatedWindows.contains(windowId);
    const bool wasUntiledMinimizeFloat = m_untiledMinimizeFloats.contains(windowId);

    // Remove from old screen's autotile state
    onWindowClosed(windowId, oldScreenId);

    if (ownedMinimizeFloat && w->isMinimized()) {
        if (newIsAutotile) {
            m_minimizeFloatedWindows.insert(windowId);
            if (wasUntiledMinimizeFloat) {
                m_untiledMinimizeFloats.insert(windowId);
            }
            qCInfo(lcEffect) << "Autotile: minimize-float ownership carried across screens:" << windowId << "->"
                             << newScreenId;
        } else if (SnapHandler* snap = m_effect->snapHandler()) {
            // Snap destination: hand the record over so the unminimize edge
            // on that screen finds an owner (mirrors the deferred-commit
            // transfer in the unfloat grace path).
            snap->adoptMinimizeFloated(windowId);
            qCInfo(lcEffect) << "Autotile: minimize-float ownership handed to snap on screen change:" << windowId
                             << "->" << newScreenId;
        }
    }

    if (willReAdd) {
        // Re-add on new autotile screen, carrying over pre-autotile geometry.
        // Cancel any pending cross-screen restore — the window is back in autotile.
        auto pendingIt = m_pendingCrossScreenRestore.find(windowId);
        if (pendingIt != m_pendingCrossScreenRestore.end()) {
            QObject::disconnect(pendingIt.value());
            m_pendingCrossScreenRestore.erase(pendingIt);
        }
        if (savedPreAutotileGeo.isValid()) {
            m_preAutotileGeometries[newScreenId][windowId] = savedPreAutotileGeo;
        }
        // RE-ADD: a tiled window's current frame is its zone rect on the old
        // screen — knownFreeFloating=false lets the floating guard reject it
        // (a genuinely floating window passes the guard and keeps its float
        // geometry captured). Without this the tiled rect would be pushed with
        // overwrite=true and destroy the daemon's persisted free-back.
        // Re-add on the new autotile screen. Eligibility drift (e.g. the tiled
        // frame now sits below the user's min-size threshold) or a pending
        // close can filter it locally — a no-op then, the daemon's tile path
        // owns any follow-up.
        notifyWindowAdded(w, /*knownFreeFloating=*/false);
    } else if (oldIsAutotile && !newIsAutotile) {
        // Autotile → snapping: restore the window's original (pre-snap/pre-tile)
        // SIZE after the drag ends.  The effect-side m_preAutotileGeometries may
        // hold the snap zone geometry (if the window was snapped before entering
        // autotile), so we ask the daemon for the true pre-tile geometry instead.
        // If unavailable, fall back to the effect-side cache.
        QPointer<KWin::EffectWindow> safeW = w;
        const QString wid = windowId;

        // Cancel any prior pending restore for this window (rapid back-and-forth)
        auto oldIt = m_pendingCrossScreenRestore.find(windowId);
        if (oldIt != m_pendingCrossScreenRestore.end()) {
            QObject::disconnect(oldIt.value());
            m_pendingCrossScreenRestore.erase(oldIt);
        }

        // Fetch the daemon's pre-tile geometry (original size before any snapping).
        // The effect-side savedPreAutotileGeo is kept as fallback.
        const int fallbackW = savedPreAutotileGeo.isValid() ? std::max(0, qRound(savedPreAutotileGeo.width())) : 0;
        const int fallbackH = savedPreAutotileGeo.isValid() ? std::max(0, qRound(savedPreAutotileGeo.height())) : 0;

        auto* watcher = new QDBusPendingCallWatcher(
            PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("getValidatedPreTileGeometry"), {windowId}),
            m_effect);
        // Context is `this` (the handler), not m_effect: the lambda captures and
        // dereferences AutotileHandler members (m_autotileScreens,
        // m_pendingCrossScreenRestore), and the handler is a unique_ptr member of
        // the effect destroyed before its sibling watcher. A `this` context auto-
        // disconnects the callback when the handler dies, matching the sibling
        // watchers above.
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, safeW, wid, fallbackW, fallbackH](QDBusPendingCallWatcher* pw) {
                    pw->deleteLater();

                    int restoreW = fallbackW;
                    int restoreH = fallbackH;
                    QDBusPendingReply<bool, int, int, int, int> reply = *pw;
                    if (reply.isValid() && reply.count() >= 5 && reply.argumentAt<0>()) {
                        int dw = reply.argumentAt<3>();
                        int dh = reply.argumentAt<4>();
                        if (dw > 0 && dh > 0) {
                            restoreW = dw;
                            restoreH = dh;
                        }
                    }

                    if (restoreW <= 0 || restoreH <= 0 || !safeW || safeW->isDeleted()) {
                        return;
                    }

                    // If the window bounced back to an autotile screen during the
                    // D-Bus round-trip, skip the restore — it's being tiled again.
                    const QString currentScreen = m_effect->getWindowScreenId(safeW);
                    if (m_autotileScreens.contains(currentScreen)) {
                        return;
                    }

                    // If the drag already ended, apply immediately — unless the
                    // window was snapped to a zone by dragStopped during this D-Bus
                    // round-trip. In that case, zone geometry is already correct.
                    if (!safeW->isUserMove() && !safeW->isUserResize()) {
                        if (!m_effect->isWindowFloating(wid)) {
                            return; // Snapped to zone — don't clobber zone geometry
                        }
                        const QRectF frame = safeW->frameGeometry();
                        const QRect geo(qRound(frame.x()), qRound(frame.y()), restoreW, restoreH);
                        // Snap-out: the window is leaving tile-managed sizing.
                        m_effect->applyWindowGeometry(safeW, geo, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                                      PhosphorAnimation::ProfilePaths::WindowSnapOut);
                        return;
                    }

                    // Still dragging — wait for drop, then restore size once.
                    // Use a shared connection so the lambda can disconnect itself
                    // after firing once — preventing subsequent user resizes from
                    // snapping the window back to the pre-autotile size.
                    auto sharedConn = std::make_shared<QMetaObject::Connection>();
                    // Context is `this` (the handler), not m_effect: the lambda
                    // mutates m_pendingCrossScreenRestore, so the connection must
                    // die with the handler.
                    *sharedConn = connect(safeW.data(), &KWin::EffectWindow::windowFinishUserMovedResized, this,
                                          [this, safeW, wid, restoreW, restoreH, sharedConn](KWin::EffectWindow*) {
                                              // Disconnect immediately so this only fires once (the drop),
                                              // not on every subsequent user resize.
                                              QObject::disconnect(*sharedConn);
                                              m_pendingCrossScreenRestore.remove(wid);
                                              if (!safeW || safeW->isDeleted()) {
                                                  return;
                                              }
                                              // Guard: window may have bounced back to autotile during drag.
                                              const QString dropScreen = m_effect->getWindowScreenId(safeW);
                                              if (m_autotileScreens.contains(dropScreen)) {
                                                  return;
                                              }
                                              // Guard: window may have been snapped to a zone by dragStopped.
                                              if (!m_effect->isWindowFloating(wid)) {
                                                  return;
                                              }
                                              const QRectF frame = safeW->frameGeometry();
                                              const QRect geo(qRound(frame.x()), qRound(frame.y()), restoreW, restoreH);
                                              // Snap-out: leaving tile-managed sizing.
                                              m_effect->applyWindowGeometry(
                                                  safeW, geo, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                                  PhosphorAnimation::ProfilePaths::WindowSnapOut);
                                          });
                    m_pendingCrossScreenRestore[wid] = *sharedConn;
                });

        // NOTE: Do NOT call windowUnsnapped here. The drag-drop handler
        // (WindowDragAdaptor::dragStopped) manages the zone assignment transition.
        // Firing windowUnsnapped now would race with the snap-to-zone D-Bus call
        // from the drop handler, destroying the zone assignment that was just created.
        // The drop handler's cross-screen path already clears stale state.

        // Raise above existing windows so it doesn't end up buried behind
        // snapped windows on the target screen.
        KWin::Window* kw = w->window();
        if (kw) {
            auto* ws = KWin::Workspace::self();
            if (ws) {
                ws->raiseWindow(kw);
            }
        }
    }

    m_effect->updateAllDecorations();
}

void AutotileHandler::cleanupAutotileTracking(const QString& windowId, const QString& screenId)
{
    // Compositor-agnostic state cleanup (shared helper).
    AutotileStateHelpers::AutotileWindowState windowState{
        m_notifiedWindows,      m_notifiedWindowScreens,   m_minimizeFloatedWindows, m_autotileTargetZones,
        m_centeredWaylandZones, m_monocleMaximizedWindows, m_preAutotileGeometries};
    AutotileStateHelpers::cleanupClosedWindowState(windowId, m_border, windowState);
    m_untiledMinimizeFloats.remove(windowId);
    m_unfloatInFlight.remove(windowId);
    // Retry budget and route/provenance markers die with the tracking: a
    // reused windowId must not inherit an exhausted budget, and every direct
    // caller of this cleanup (not just onWindowClosed) must drop the
    // spawn-provenance entries or they leak past cross-mode moves.
    m_unfloatRetryAttempts.remove(windowId);
    m_pendingFreshWindows.remove(windowId);
    m_deferredWindowRoutes.remove(windowId);
    cancelPendingMinimizeFloat(windowId);
    cancelPendingUnminimizeUnfloat(windowId);
    // KWin-specific cleanup. NOTE: m_savedPreAutotileForDesktopMove is NOT cleared
    // here — the desktop-move path stashes it immediately before close (consume
    // site / clearDesktopMoveStash cover it). Also drop the unconsumed output-move
    // marker and the pending cross-screen-restore connection (a stale one could
    // fire a spurious applyWindowGeometry).
    m_savedNotifiedForDesktopReturn.remove(windowId);
    m_expectedOutputMove.remove(windowId);
    if (auto pendingConn = m_pendingCrossScreenRestore.find(windowId);
        pendingConn != m_pendingCrossScreenRestore.end()) {
        QObject::disconnect(pendingConn.value());
        m_pendingCrossScreenRestore.erase(pendingConn);
    }
    if (m_savedAutotileStackingOrder.contains(screenId)) {
        m_savedAutotileStackingOrder[screenId].removeAll(windowId);
    }
}

void AutotileHandler::onWindowClosed(const QString& windowId, const QString& screenId)
{
    m_pendingFreshWindows.remove(windowId);
    m_deferredWindowRoutes.remove(windowId);
    cleanupAutotileTracking(windowId, screenId);

    // Notify autotile daemon
    if (m_autotileScreens.contains(screenId)) {
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Autotile,
                                                       QStringLiteral("windowClosed"), {windowId},
                                                       QStringLiteral("windowClosed"));
        qCDebug(lcEffect) << "Notified autotile: windowClosed" << windowId << "on screen" << screenId;
    }
}

void AutotileHandler::deferWindowRouting(KWin::EffectWindow* window, bool canSnapRestore)
{
    if (!window || window->isDeleted()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(window);
    m_pendingFreshWindows.insert(windowId);
    m_deferredWindowRoutes.insert(windowId, DeferredWindowRoute{QPointer<KWin::EffectWindow>(window), canSnapRestore});
}

QSet<QString> AutotileHandler::completeDeferredWindowRoutes()
{
    const auto routes = m_deferredWindowRoutes;
    m_deferredWindowRoutes.clear();
    QSet<QString> routedWindowIds;
    routedWindowIds.reserve(routes.size());
    for (auto it = routes.constBegin(); it != routes.constEnd(); ++it) {
        routedWindowIds.insert(it.key());
        KWin::EffectWindow* window = it->window.data();
        if (!window || window->isDeleted()) {
            m_pendingFreshWindows.remove(it.key());
            continue;
        }
        const QString windowId = m_effect->getWindowId(window);
        routedWindowIds.insert(windowId);
        // The pending-fresh entry was keyed by the id at defer time; if the
        // live id diverged, the old key would leak forever (the tail prune
        // below only drops dead/off-screen windows, and this window is
        // neither).
        if (windowId != it.key()) {
            m_pendingFreshWindows.remove(it.key());
        }
        // The defer-time first-frame suppression was armed with the standard
        // deadline, but the screen query this dispatch waited on can outlast
        // it — re-arm (deadline only, no-op for unsuppressed windows) so the
        // window doesn't return to compositing at its centred spawn placement
        // between deadline expiry and the reposition below.
        m_effect->refreshRestoreSuppressionDeadline(window);
        // Consume (and maybe apply) the instant snap-restore cache entry,
        // exactly as the non-deferred open path does — a deferred window must
        // not leave its entry alive for a later same-app sibling to claim.
        // A teleport can move the window to another screen; re-resolve after.
        QString screenId = m_effect->getWindowScreenId(window);
        if (it->canSnapRestore && !window->isMinimized()
            && m_effect->tryInstantSnapRestore(window, windowId, /*canSnapRestore=*/true)) {
            screenId = m_effect->getWindowScreenId(window);
        }
        if (m_autotileScreens.contains(screenId)) {
            if (window->isMinimized()) {
                // A window that minimized while the screen query was pending
                // is excluded from the follow-up batch (it is in
                // routedWindowIds), so nothing else will claim it — claim it
                // here, release the first-frame suppression (a minimized
                // window paints nothing, and leaving the suppression armed
                // stalls its eventual restore for the 250 ms deadline), and
                // drop the spawn-provenance marker so a later re-add cannot
                // inherit knownFreeFloating=true from a stale entry.
                claimAlreadyMinimizedAsFloated(window, windowId, m_autotileScreens, /*enteringAutotile=*/true);
                m_pendingFreshWindows.remove(windowId);
                m_effect->endRestoreSuppression(window);
                continue;
            }
            if (it->canSnapRestore && m_effect->snapHandler()) {
                QPointer<KWin::EffectWindow> safeWindow = window;
                m_effect->snapHandler()->callResolveWindowRestore(
                    window,
                    [this, safeWindow, windowId](bool snapApplied) {
                        if (!safeWindow || safeWindow->isDeleted()) {
                            return;
                        }
                        if (!m_autotileScreens.contains(m_effect->getWindowScreenId(safeWindow.data()))) {
                            m_pendingFreshWindows.remove(windowId);
                            m_effect->endRestoreSuppression(safeWindow.data());
                            return;
                        }
                        // knownFreeFloating only when the restore did NOT
                        // apply — a zone-placed window's live frame is the
                        // zone rect, not a genuine free frame.
                        if (!notifyWindowAdded(safeWindow.data(), /*knownFreeFloating=*/!snapApplied)
                            && !m_notifiedWindows.contains(windowId)) {
                            m_effect->endRestoreSuppression(safeWindow.data());
                        }
                    },
                    /*releaseSuppressionOnMiss=*/false);
            } else if (!notifyWindowAdded(window, /*knownFreeFloating=*/true)
                       && !m_notifiedWindows.contains(windowId)) {
                m_effect->endRestoreSuppression(window);
            }
            continue;
        }

        m_pendingFreshWindows.remove(it.key());
        m_pendingFreshWindows.remove(windowId);
        if (it->canSnapRestore && !window->isMinimized() && m_effect->snapHandler()) {
            m_effect->snapHandler()->callResolveWindowRestore(window);
        } else {
            m_effect->endRestoreSuppression(window);
        }
    }

    const auto pendingIds = m_pendingFreshWindows.values();
    for (const QString& windowId : pendingIds) {
        // EXACT resolve: the entry is keyed to a specific instance's id, so a
        // fuzzy hit on a same-app sibling must not keep a dead entry alive —
        // a retained stale entry later flips knownFreeFloating to true and
        // poisons the free-geometry capture.
        KWin::EffectWindow* window = m_effect->findWindowByIdExact(windowId);
        if (!window || window->isDeleted() || !m_autotileScreens.contains(m_effect->getWindowScreenId(window))) {
            m_pendingFreshWindows.remove(windowId);
        }
    }
    return routedWindowIds;
}

void AutotileHandler::handleDragToFloat(KWin::EffectWindow* w, const QString& windowId, bool immediate)
{
    // Restore border and clear tiling state synchronously — don't wait for
    // the daemon's async windowFloatingChanged signal, which may never arrive
    // (e.g., cross-screen drag where onWindowClosed removes daemon tracking
    // before setWindowFloatingForScreen processes).
    applyFloatCleanup(windowId);

    // Restore pre-autotile SIZE at the window's current position. The
    // all-bucket lookup matters here: size is coordinate-space-independent,
    // so any bucket's rect is safe.
    if (w) {
        const QRectF savedGeo = findPreAutotileGeometry(windowId);
        if (savedGeo.isValid()) {
            const int savedW = qRound(savedGeo.width());
            const int savedH = qRound(savedGeo.height());

            if (immediate) {
                // Drag-start path: apply synchronously during the
                // interactive move (allowDuringDrag=true) so the user
                // sees the window return to its free-floating size the
                // moment they start dragging — matches snap-mode behavior.
                // Re-center horizontally under the cursor so the window
                // doesn't "jump away" from the grab point when it shrinks.
                QRectF currentFrame = w->frameGeometry();
                const QPointF cursor = KWin::effects->cursorPos();
                int newX = qRound(currentFrame.x());
                int newY = qRound(currentFrame.y());
                if (currentFrame.width() > 0 && savedW < currentFrame.width()) {
                    const qreal cursorOffsetRatio = (cursor.x() - currentFrame.x()) / currentFrame.width();
                    newX = qRound(cursor.x() - cursorOffsetRatio * savedW);
                }
                QRect sizeRestored(newX, newY, savedW, savedH);
                // Snap-out: leaving tile-managed sizing.
                m_effect->applyWindowGeometry(w, sizeRestored, /*allowDuringDrag=*/true, /*skipAnimation=*/false,
                                              PhosphorAnimation::ProfilePaths::WindowSnapOut);
                qCInfo(lcEffect) << "Drag-start float: restored pre-autotile size for" << windowId << savedW << "x"
                                 << savedH;
            } else {
                // Drag-stop path: defer to next event loop tick so
                // KWin has finished the interactive move and the window's
                // frame geometry reflects the actual drop position.
                QPointer<KWin::EffectWindow> wp = w;
                PlasmaZonesEffect* effect = m_effect;
                QTimer::singleShot(0, effect, [effect, wp, windowId, savedW, savedH]() {
                    if (!wp || wp->isDeleted()) {
                        return;
                    }
                    // Skip if the window was re-snapped during the deferred tick
                    // (e.g., dropped on a zone on a snap screen during cross-VS drag).
                    if (!effect->isWindowFloating(effect->getWindowId(wp))) {
                        qCDebug(lcEffect) << "Drag-to-float: skipping size restore for re-snapped window" << windowId;
                        return;
                    }
                    QRectF currentFrame = wp->frameGeometry();
                    QRect sizeRestored(qRound(currentFrame.x()), qRound(currentFrame.y()), savedW, savedH);
                    // Snap-out: leaving tile-managed sizing.
                    effect->applyWindowGeometry(wp, sizeRestored, /*allowDuringDrag=*/false, /*skipAnimation=*/false,
                                                PhosphorAnimation::ProfilePaths::WindowSnapOut);
                    qCInfo(lcEffect) << "Drag-to-float: restored pre-autotile size for" << windowId << savedW << "x"
                                     << savedH;
                });
            }
        }
    }

    m_effect->updateAllDecorations();
}

void AutotileHandler::onDaemonReady()
{
    // Connect BEFORE querying: a screensChanged emitted after the daemon
    // serves Properties.Get but before our AddMatch lands would be both lost
    // and unable to bump the generation guard. Connect-then-query is
    // strictly sound — a signal lost pre-AddMatch implies the Get (served
    // after the change) already returns the new set.
    connectSignals();
    m_initialScreenQueryPending = false;
    loadSettings();
    m_notifiedWindows.clear();
    m_notifiedWindowScreens.clear();
    m_savedNotifiedForDesktopReturn.clear();
    m_savedPreAutotileForDesktopMove.clear();
    // Centering state is per-retile transient: the restarted daemon has no
    // memory of the zones these entries point at, and a stale
    // m_centeredWaylandZones entry that happens to equal the first
    // post-restart tile request would trip the skipMoveResize short-circuit
    // in slotWindowsTileRequested against a freshly-restored decoration,
    // leaving a title-bar-height gap because the geometry is never re-asserted.
    m_autotileTargetZones.clear();
    m_centeredWaylandZones.clear();
    // Per-screen stagger generations describe the dead session's in-flight
    // batches. They are otherwise only ever inserted (one entry per distinct
    // screenId ever seen, never pruned), so resetting here both restarts the
    // staggered-apply epochs cleanly and keeps the map bounded across reconnects.
    m_autotileStaggerGenByScreen.clear();
    // Daemon-owned cross-output move markers belong to the dead session. A
    // stale one-shot armed before the restart (windowOutputMoveExpected fired,
    // matching outputChanged not yet seen) would swallow the next genuine
    // outputChanged for that window — taking the bookkeeping-only path and
    // skipping the real transfer. Clear it like every other per-session map.
    m_expectedOutputMove.clear();
    // In-flight debounced minimize→float commits and minimize-float records
    // belong to the dead daemon session — a timer firing now would issue a
    // setWindowFloatingForScreen against state the new daemon never had, and
    // a stale record would mis-route the next unminimize. Pending
    // cross-screen size-restore connections are likewise per-session.
    // clearAllPendingMinimizeFloats() also cancels the pending deferred
    // unminimize→unfloat timers; an escapee's timeout would bail anyway when
    // ownership lookup misses after the clear below.
    clearAllPendingMinimizeFloats();
    m_minimizeFloatedWindows.clear();
    m_unfloatInFlight.clear();
    m_unfloatRetryAttempts.clear();
    m_untiledMinimizeFloats.clear();
    // Routes deferred against the dead daemon session and their provenance
    // markers must not survive into the new one: a stale m_pendingFreshWindows
    // entry silently upgrades a later re-add to knownFreeFloating=true, which
    // is the free-geometry overwrite this contract exists to prevent.
    m_pendingFreshWindows.clear();
    m_deferredWindowRoutes.clear();
    for (auto connIt = m_pendingCrossScreenRestore.begin(); connIt != m_pendingCrossScreenRestore.end(); ++connIt) {
        QObject::disconnect(connIt.value());
    }
    m_pendingCrossScreenRestore.clear();
    // A stacking order saved before the restart describes a dead session's
    // z-order — the first post-restart retile's onComplete would replay it
    // and re-raise windows in stale order; same for a stale pending focus id.
    m_savedAutotileStackingOrder.clear();
    m_pendingAutotileFocusWindowId.clear();

    // Re-send the effect's pre-autotile geometry cache to the freshly
    // (re)connected daemon as a backstop. storePreTileGeometry lands in the
    // unified WindowPlacementStore record (which IS persisted), but a record
    // the store had not flushed before the daemon died — or a daemon started
    // with wiped state — would leave already-tiled windows with no
    // pre-autotile position to return to on autotile→snap or drag-to-float.
    // The effect survives daemon restarts and still holds each window's true
    // pre-autotile frame here. overwrite=false so anything the daemon
    // restored from its own persisted records wins.
    if (m_effect->m_daemonGate.serviceRegistered) {
        int resent = 0;
        for (auto scrIt = m_preAutotileGeometries.constBegin(); scrIt != m_preAutotileGeometries.constEnd(); ++scrIt) {
            const QString& screenId = scrIt.key();
            for (auto winIt = scrIt.value().constBegin(); winIt != scrIt.value().constEnd(); ++winIt) {
                const QRectF& geo = winIt.value();
                if (geo.width() <= 0 || geo.height() <= 0) {
                    continue;
                }
                // qRound, not truncation: fractional-scale sub-pixel residue
                // (matches the toRect() geometry-capture convention).
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                    QStringLiteral("storePreTileGeometry"),
                    {winIt.key(), qRound(geo.x()), qRound(geo.y()), qRound(geo.width()), qRound(geo.height()), screenId,
                     false},
                    QStringLiteral("storePreTileGeometry"));
                ++resent;
            }
        }
        if (resent > 0) {
            qCInfo(lcEffect) << "Re-sent" << resent << "pre-autotile geometries to daemon after reconnect";
        }
    }
}

// handleAutotileFloatToggle removed: float toggle is now daemon-local via
// SnapAdaptor::toggleFloatForWindow (which emits applyGeometryRequested).

// connectSignals() / loadSettings() live in autotilehandler/wiring.cpp.

} // namespace PlasmaZones
