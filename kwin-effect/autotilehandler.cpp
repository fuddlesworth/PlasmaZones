// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autotilehandler.h"
#include "plasmazoneseffect.h"
#include "windowanimator.h"
#include "navigationhandler.h"

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
        if (!w || w->isDeleted() || w->isMinimized() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
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
    // Deleted windows bail before getWindowId (cache-pollution hazard);
    // every other rejection comes after the pending-close consume below.
    if (!w || w->isDeleted()) {
        return false;
    }

    const QString windowId = m_effect->getWindowId(w);

    // Window was already closed before we could notify open — skip (D-Bus
    // ordering race). Consumed BEFORE the eligibility check so an entry
    // whose racing add arrives ineligible doesn't strand in the set until
    // the next daemon restart.
    if (m_pendingCloses.remove(windowId)) {
        return false;
    }

    if (!isEligibleForAutotileNotify(w)) {
        return false;
    }

    if (m_notifiedWindows.contains(windowId)) {
        return false;
    }
    m_notifiedWindows.insert(windowId);

    QString screenId = m_effect->getWindowScreenId(w);
    m_notifiedWindowScreens[windowId] = screenId;

    // Only notify autotile daemon for windows on autotile screens
    if (m_autotileScreens.contains(screenId)) {
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
        saveAndRecordPreAutotileGeometry(windowId, screenId, w->frameGeometry(), knownFreeFloating);

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
                // The autotile→autotile transfer path in
                // handleWindowOutputChanged skips its decoration release in
                // anticipation of this call's tile request moving the claim.
                // The call failed — no tile request is coming — so release
                // the stale ownership here (no-op for fresh windows, which
                // hold no claim yet).
                m_effect->decorationManager()->releaseKind(windowId, DecorationManager::OwnerKind::Autotile);
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
                                              const QSet<QString>& screenFilter, bool resetNotified)
{
    // Collect eligible windows using the same filtering as notifyWindowAdded,
    // then send one batch D-Bus call instead of per-window round-trips.
    PhosphorProtocol::WindowOpenedList batchEntries;
    QStringList batchWindowIds; // for error rollback

    for (KWin::EffectWindow* w : windows) {
        // Deleted windows bail before any id/screen lookup (cache-pollution
        // hazard); the pending-close consume runs BEFORE the eligibility
        // check — same ordering rationale as notifyWindowAdded.
        if (!w || w->isDeleted()) {
            continue;
        }

        const QString windowId = m_effect->getWindowId(w);
        const bool suppressed = m_pendingCloses.remove(windowId);

        if (!isEligibleForAutotileNotify(w)) {
            continue;
        }
        if (suppressed) {
            continue;
        }

        const QString screenId = m_effect->getWindowScreenId(w);
        if (!screenFilter.isEmpty() && !screenFilter.contains(screenId)) {
            continue;
        }
        if (!m_autotileScreens.contains(screenId)) {
            continue;
        }

        if (resetNotified) {
            m_notifiedWindows.remove(windowId);
        }
        if (m_notifiedWindows.contains(windowId)) {
            continue;
        }
        m_notifiedWindows.insert(windowId);
        m_notifiedWindowScreens[windowId] = screenId;

        // knownFreeFloating only for windows we do NOT track as tiled. The
        // batch is also the RE-announce path (daemon restart, toggle-on,
        // screen-add) where a window's current frame is its tiled zone rect —
        // border tracking survives daemon restarts, so isTiledWindow is
        // authoritative here. Passing true for a tiled window would push
        // storePreTileGeometry with overwrite=true and destroy the daemon's
        // persisted free geometry (a snap→autotile window has no local
        // bucket entry, so the exact-match early-return cannot save it).
        // Genuinely fresh windows (not yet tiled) keep the spawn-geometry
        // overwrite semantics — see notifyWindowAdded() for that rationale.
        const bool freshFrame = !AutotileStateHelpers::isTiledWindow(m_border, windowId);
        saveAndRecordPreAutotileGeometry(windowId, screenId, w->frameGeometry(),
                                         /*knownFreeFloating=*/freshFrame);

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
            // Tracking rollback only — deliberately NO releaseKind or
            // endRestoreSuppression here, unlike notifyWindowAdded's error
            // handler: the batch paths (daemon-restart re-announce,
            // toggle-on) serve windows that are still physically tiled and
            // may be re-announced when the daemon returns, and the batch
            // never serves the cross-screen transfer that parks a kept
            // decoration claim on the single-window path.
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
            m_expectedOutputMove.remove(windowId);
            notifyWindowAdded(w);
            m_effect->updateAllBorders();
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
    if (const auto expIt = m_expectedOutputMove.constFind(windowId); expIt != m_expectedOutputMove.constEnd()) {
        const QString expectedScreen = expIt.value();
        m_expectedOutputMove.erase(expIt);
        if (expectedScreen == newScreenId) {
            if (newIsAutotile) {
                m_notifiedWindowScreens[windowId] = newScreenId;
                // The daemon's destination tile request moves the decoration claim
                // (releaseOthersOfKind + acquire in slotWindowsTileRequested); keep
                // our claim coherent here without a physical flap.
                m_effect->decorationManager()->releaseOthersOfKind(windowId, DecorationManager::OwnerKind::Autotile,
                                                                   newScreenId);
            } else {
                // Cross-MODE move: window left autotile for a SNAP screen. Release
                // the decoration claim and drop effect-side autotile tracking (daemon
                // already relinquished via handoffRelease) — else it lingers phantom.
                m_effect->decorationManager()->releaseKind(windowId, DecorationManager::OwnerKind::Autotile);
                cleanupAutotileTracking(windowId, oldScreenId);
            }
            m_effect->updateAllBorders();
            return;
        }
    }

    qCInfo(lcEffect) << "Window moved between monitors:" << windowId << oldScreenId << "->" << newScreenId;

    // Snapshot the pre-autotile geometry BEFORE onWindowClosed clears it
    // (the close-cleanup sweeps the geometry out of EVERY screen bucket).
    QRectF savedPreAutotileGeo;
    if (oldIsAutotile) {
        savedPreAutotileGeo = findPreAutotileGeometry(windowId);
    }

    // Release autotile's decoration ownership only when the window is
    // actually leaving autotile management. On an autotile→autotile
    // transfer the imminent retile moves the claim atomically without a
    // physical flap (releaseOthersOfKind + acquire in
    // slotWindowsTileRequested), so releasing here would only produce a
    // transient title-bar flash. Every no-retile fallthrough has a release:
    // a daemon overflow-float lands in applyFloatCleanup (releaseKind), a
    // locally-filtered re-add releases right below, and a failed
    // windowOpened call releases in notifyWindowAdded's error handler. The
    // predicate mirrors the re-add condition below.
    const bool willReAdd = newIsAutotile && !w->isMinimized() && w->isOnCurrentDesktop() && w->isOnCurrentActivity();
    if (!willReAdd) {
        m_effect->decorationManager()->releaseKind(windowId, DecorationManager::OwnerKind::Autotile);
    }

    // Remove from old screen's autotile state
    onWindowClosed(windowId, oldScreenId);

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
        if (!notifyWindowAdded(w, /*knownFreeFloating=*/false)) {
            // The re-add was filtered locally — eligibility drift (e.g. the
            // tiled frame now sits below the user's min-size threshold) or a
            // pending close. No windowOpened call was issued, so neither the
            // D-Bus error handler nor a tile request will ever touch this
            // window again: the claim kept above for the flap-free transfer
            // would strand the title bar hidden. Release it now.
            m_effect->decorationManager()->releaseKind(windowId, DecorationManager::OwnerKind::Autotile);
        }
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
        connect(watcher, &QDBusPendingCallWatcher::finished, m_effect,
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
                        m_effect->applySnapGeometry(safeW, geo);
                        return;
                    }

                    // Still dragging — wait for drop, then restore size once.
                    // Use a shared connection so the lambda can disconnect itself
                    // after firing once — preventing subsequent user resizes from
                    // snapping the window back to the pre-autotile size.
                    auto sharedConn = std::make_shared<QMetaObject::Connection>();
                    *sharedConn = connect(safeW.data(), &KWin::EffectWindow::windowFinishUserMovedResized, m_effect,
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
                                              m_effect->applySnapGeometry(safeW, geo);
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

    m_effect->updateAllBorders();
}

void AutotileHandler::cleanupAutotileTracking(const QString& windowId, const QString& screenId)
{
    // Compositor-agnostic state cleanup (shared helper).
    AutotileStateHelpers::AutotileWindowState windowState{
        m_notifiedWindows,      m_notifiedWindowScreens,   m_minimizeFloatedWindows, m_autotileTargetZones,
        m_centeredWaylandZones, m_monocleMaximizedWindows, m_preAutotileGeometries};
    AutotileStateHelpers::cleanupClosedWindowState(windowId, m_border, windowState);
    cancelPendingMinimizeFloat(windowId);
    // KWin-specific cleanup. NOTE: m_savedPreAutotileForDesktopMove is NOT cleared
    // here — the desktop-move path stashes it immediately before close (consume
    // site / clearDesktopMoveStash cover it). Also drop the unconsumed output-move
    // marker and the pending cross-screen-restore connection (a stale one could
    // fire a spurious applySnapGeometry).
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

void AutotileHandler::onWindowClosed(const QString& windowId, const QString& screenId, bool windowDestroyed)
{
    // If we haven't notified the daemon about this window yet, record the
    // close so we can suppress the open if it arrives late (D-Bus ordering
    // race). Genuine destruction only: a LIVE window routed through here
    // (transfer / desktop move / drag-bypass) that is untracked because it
    // was eligibility-filtered would otherwise have its NEXT genuine add
    // silently swallowed when it later becomes eligible.
    if (windowDestroyed && !m_notifiedWindows.contains(windowId) && m_autotileScreens.contains(screenId)) {
        m_pendingCloses.insert(windowId);
    }

    cleanupAutotileTracking(windowId, screenId);

    // Notify autotile daemon
    if (m_autotileScreens.contains(screenId)) {
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::Autotile,
                                                       QStringLiteral("windowClosed"), {windowId},
                                                       QStringLiteral("windowClosed"));
        qCDebug(lcEffect) << "Notified autotile: windowClosed" << windowId << "on screen" << screenId;
    }
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
                m_effect->applySnapGeometry(w, sizeRestored, /*allowDuringDrag=*/true);
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
                    effect->applySnapGeometry(wp, sizeRestored);
                    qCInfo(lcEffect) << "Drag-to-float: restored pre-autotile size for" << windowId << savedW << "x"
                                     << savedH;
                });
            }
        }
    }

    m_effect->updateAllBorders();
}

void AutotileHandler::onDaemonReady()
{
    // Connect BEFORE querying: a screensChanged emitted after the daemon
    // serves Properties.Get but before our AddMatch lands would be both lost
    // and unable to bump the generation guard. Connect-then-query is
    // strictly sound — a signal lost pre-AddMatch implies the Get (served
    // after the change) already returns the new set.
    connectSignals();
    loadSettings();
    m_notifiedWindows.clear();
    m_notifiedWindowScreens.clear();
    m_savedNotifiedForDesktopReturn.clear();
    m_savedPreAutotileForDesktopMove.clear();
    m_pendingCloses.clear();
    // Centering state is per-retile transient: the restarted daemon has no
    // memory of the zones these entries point at, and a stale
    // m_centeredWaylandZones entry that happens to equal the first
    // post-restart tile request would trip the skipMoveResize short-circuit
    // in slotWindowsTileRequested against a freshly-restored decoration,
    // leaving a title-bar-height gap (the CallerWillPlace acquire there
    // expects the caller to re-assert geometry).
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
    clearAllPendingMinimizeFloats();
    m_minimizeFloatedWindows.clear();
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
    if (m_effect->m_daemonServiceRegistered) {
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
// WindowTrackingAdaptor::toggleWindowFloat (which emits applyGeometryRequested).

// connectSignals() / loadSettings() live in autotilehandler/wiring.cpp
// (split for the 800-line limit).

} // namespace PlasmaZones
