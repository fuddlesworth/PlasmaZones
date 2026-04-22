// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// D-Bus signal slot handlers for AutotileHandler.
// Part of AutotileHandler — split from autotilehandler.cpp for SRP.

#include "../autotilehandler.h"
#include "../plasmazoneseffect.h"
#include "../navigationhandler.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorIdentity/WindowId.h>

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
#include <QPointer>
#include <QTimer>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

// Debounce window for minimize→float commits. KWin can transiently flip a
// tiled window's isMinimized() state to true and back within a few
// milliseconds when a plasmashell notification popup rearranges stacking.
// Deferring the D-Bus float for this long coalesces spurious cycles: if the
// matching unminimize arrives inside the window, no float is issued and the
// autotile layout never sees the transient 1-window state that causes the
// master to balloon to the full screen. Real user minimizes always last
// longer than this, so they commit normally.
static constexpr int kMinimizeFloatDebounceMs = 75;

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus signal slot handlers
// ═══════════════════════════════════════════════════════════════════════════════

void AutotileHandler::slotEnabledChanged(bool enabled)
{
    qCInfo(lcEffect) << "Autotile enabled state changed:" << enabled;
    if (!enabled) {
        restoreAllBorderless();
        restoreAllMonocleMaximized();
        m_savedSnapStackingOrder.clear();
        m_savedAutotileStackingOrder.clear();
        m_savedNotifiedForDesktopReturn.clear();
        // Drop any in-flight debounced minimize→float commits — they must not
        // fire against a disabled engine.
        clearAllPendingMinimizeFloats();
        m_effect->updateAllBorders();
    }
}

void AutotileHandler::cancelPendingMinimizeFloat(const QString& windowId)
{
    auto it = m_pendingMinimizeFloat.find(windowId);
    if (it == m_pendingMinimizeFloat.end()) {
        return;
    }
    if (QTimer* pending = it.value()) {
        pending->stop();
        pending->deleteLater();
    }
    m_pendingMinimizeFloat.erase(it);
}

void AutotileHandler::clearAllPendingMinimizeFloats()
{
    for (auto it = m_pendingMinimizeFloat.begin(); it != m_pendingMinimizeFloat.end(); ++it) {
        if (QTimer* pending = it.value()) {
            pending->stop();
            pending->deleteLater();
        }
    }
    m_pendingMinimizeFloat.clear();
}

void AutotileHandler::slotScreensChanged(const QStringList& screenIds, bool isDesktopSwitch)
{
    // Invalidate in-flight stagger timers from prior autotile/restore operations.
    // Without this, a desktop switch can race with a pending stagger from the
    // previous desktop, applying geometry from the old context.
    ++m_autotileStaggerGeneration;
    ++m_restoreStaggerGeneration;

    const QSet<QString> newScreens(screenIds.begin(), screenIds.end());
    const QSet<QString> removed = m_autotileScreens - newScreens;
    const QSet<QString> added = newScreens - m_autotileScreens;

    const auto windows = KWin::effects->stackingOrder();

    if (!removed.isEmpty()) {
        if (isDesktopSwitch) {
            qCInfo(lcEffect) << "slotScreensChanged: desktop switch detected, skipping"
                             << "restore for removed screens:" << removed;
            // Only update m_notifiedWindows — windows on removed screens are no
            // longer autotiled on this desktop. Don't touch pre-autotile geometries,
            // stacking orders, or borderless state — they belong to the other desktop.
            // Save which windows we're removing from tracking — needed by the
            // added path to distinguish "daemon already has this window" from
            // "genuinely new window opened while desktop was not active".
            for (KWin::EffectWindow* w : windows) {
                if (w && removed.contains(m_effect->getWindowScreenId(w))) {
                    // Only remove from notified if the window is on the OLD desktop
                    // (not current). Desktop 2's windows were never notified.
                    if (!w->isOnCurrentDesktop()) {
                        const QString wid = m_effect->getWindowId(w);
                        if (m_notifiedWindows.remove(wid)) {
                            m_notifiedWindowScreens.remove(wid);
                            m_savedNotifiedForDesktopReturn.insert(wid);
                        }
                    }
                }
            }
        } else {
            QSet<QString> windowsOnRemovedScreens;
            for (KWin::EffectWindow* w : windows) {
                if (w && removed.contains(m_effect->getWindowScreenId(w))) {
                    // Only restore borders for windows on the CURRENT desktop.
                    // Windows on other desktops may still be autotiled and must keep
                    // their borderless state — restoring them here would leak title bars
                    // into other desktops' autotile sessions.
                    if (!w->isOnCurrentDesktop()) {
                        continue;
                    }
                    // Skip sticky (all-desktops) windows when some screens still
                    // use autotile. setNoBorder() is a global KWin property — restoring
                    // the border here would remove it on OTHER desktops where the window
                    // is still autotiled. Only restore when autotile is fully disabled.
                    if (w->isOnAllDesktops() && !newScreens.isEmpty()) {
                        continue;
                    }
                    windowsOnRemovedScreens.insert(m_effect->getWindowId(w));
                }
            }
            m_notifiedWindows -= windowsOnRemovedScreens;
            for (const QString& wid : std::as_const(windowsOnRemovedScreens)) {
                m_notifiedWindowScreens.remove(wid);
            }

            // Restore title bars and clear tiled tracking for windows on removed screens
            for (const QString& windowId : std::as_const(windowsOnRemovedScreens)) {
                // Find every screen that currently tracks this window and
                // remove it from each. We don't know which specific screen
                // the window "belongs to" in our buckets at this point —
                // there may be stale entries across multiple screens if the
                // window ever transferred. Clean them all.
                QStringList screensHoldingBorderless;
                for (auto it = m_border.borderlessWindowsByScreen.constBegin();
                     it != m_border.borderlessWindowsByScreen.constEnd(); ++it) {
                    if (it.value().contains(windowId)) {
                        screensHoldingBorderless.append(it.key());
                    }
                }
                KWin::EffectWindow* w = m_effect->findWindowById(windowId);
                for (const QString& sid : std::as_const(screensHoldingBorderless)) {
                    if (w) {
                        setWindowBorderless(w, windowId, false, sid);
                    } else {
                        AutotileStateHelpers::removeBorderlessOnScreen(m_border, sid, windowId);
                    }
                }
                AutotileStateHelpers::removeFromAllScreens(m_border, windowId);
            }
            m_effect->updateAllBorders();

            // Save autotile stacking order before restoring snap-mode order.
            // This allows restoring the user's autotile z-order (e.g. floated
            // windows raised to front) when re-entering autotile mode.
            // Only save windows on the current desktop — other desktops' windows
            // are not being toggled and their stacking order is irrelevant here.
            for (const QString& screenId : removed) {
                QStringList autotileOrder;
                for (KWin::EffectWindow* w : windows) {
                    if (w && m_effect->shouldHandleWindow(w) && w->isOnCurrentDesktop()
                        && m_effect->getWindowScreenId(w) == screenId) {
                        autotileOrder.append(m_effect->getWindowId(w));
                    }
                }
                if (!autotileOrder.isEmpty()) {
                    m_savedAutotileStackingOrder[screenId] = autotileOrder;
                }
            }

            // Unmaximize monocle windows on removed screens so they return to
            // normal geometry when resnapped or restored.
            for (KWin::EffectWindow* w : windows) {
                if (!w || !m_effect->shouldHandleWindow(w) || !w->isOnCurrentDesktop()) {
                    continue;
                }
                const QString screenId = m_effect->getWindowScreenId(w);
                if (!removed.contains(screenId)) {
                    continue;
                }
                unmaximizeMonocleWindow(m_effect->getWindowId(w));
            }

            // Snapshot global stacking order for z-order restore after resnap.
            // The daemon emits ONE batched resnapToNewLayoutRequested signal;
            // handleResnapToNewLayout uses this snapshot to re-raise windows
            // in their original order after applying zone geometries.
            m_savedGlobalStackForResnap.clear();
            for (KWin::EffectWindow* w : windows) {
                m_savedGlobalStackForResnap.append(QPointer<KWin::EffectWindow>(w));
            }

            // Invalidate pending stagger timers and clear autotile zone state.
            ++m_autotileStaggerGeneration;
            m_autotileTargetZones.clear();
            m_centeredWaylandZones.clear();
            ++m_restoreStaggerGeneration;

            // Clear pre-autotile geometries and saved snap stacking order
            // for removed screens — they're no longer needed.
            for (const QString& screenId : removed) {
                m_preAutotileGeometries.remove(screenId);
                m_savedSnapStackingOrder.remove(screenId);
            }
        }
    }

    m_autotileScreens = newScreens;
    m_lastFocusFollowsMouseWindowId.clear();

    if (!added.isEmpty()) {
        if (isDesktopSwitch) {
            // Desktop/activity return: windows are already tiled on this desktop.
            // Re-add current-desktop windows to m_notifiedWindows so they're not
            // re-notified by later notifyWindowAdded calls (e.g., window moves).
            qCInfo(lcEffect) << "slotScreensChanged: desktop return for screens:" << added;
            for (const QString& screenId : added) {
                for (KWin::EffectWindow* w : windows) {
                    if (w && m_effect->shouldHandleWindow(w) && w->isOnCurrentDesktop() && w->isOnCurrentActivity()
                        && m_effect->getWindowScreenId(w) == screenId) {
                        const QString windowId = m_effect->getWindowId(w);
                        if (m_savedNotifiedForDesktopReturn.contains(windowId)
                            || m_notifiedWindows.contains(windowId)) {
                            // Previously tracked — re-add without re-notifying daemon
                            m_notifiedWindows.insert(windowId);
                        } else {
                            // Genuinely new window opened while this desktop was
                            // not active — notify daemon so it's added to PhosphorTiles::TilingState
                            notifyWindowAdded(w);
                        }
                    }
                }
            }
            // Only remove entries for windows on screens we just processed.
            // In multi-screen setups, windows on OTHER screens (not in `added`)
            // must remain in the set for when their screen returns.
            for (const QString& screenId : added) {
                for (KWin::EffectWindow* w : windows) {
                    if (w && m_effect->shouldHandleWindow(w) && w->isOnCurrentDesktop() && w->isOnCurrentActivity()
                        && m_effect->getWindowScreenId(w) == screenId) {
                        m_savedNotifiedForDesktopReturn.remove(m_effect->getWindowId(w));
                    }
                }
            }

            // Catch windows moved to this desktop while the user was on another.
            // When both desktops share the same autotile screens, `added` is empty
            // and the loop above doesn't run. Scan ALL autotile screens for windows
            // on the current desktop that aren't tracked — they were removed from
            // tiling by windowDesktopsChanged on the source desktop and need to be
            // re-added here. notifyWindowAdded is idempotent (checks m_notifiedWindows).
            for (const QString& screenId : m_autotileScreens) {
                for (KWin::EffectWindow* w : windows) {
                    if (!w || !m_effect->shouldHandleWindow(w) || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()
                        || w->isMinimized()) {
                        continue;
                    }
                    if (m_effect->getWindowScreenId(w) != screenId) {
                        continue;
                    }
                    const QString windowId = m_effect->getWindowId(w);
                    if (!m_notifiedWindows.contains(windowId)) {
                        // Restore preserved pre-autotile geometry so float-restore
                        // returns to the original position, not the tiled frame from
                        // the source desktop. Only apply when the source screen
                        // matches the destination — saved rects are in absolute
                        // coordinates of the source monitor and would land off-
                        // target on a different screen after a cross-desktop +
                        // cross-screen move.
                        auto savedIt = m_savedPreAutotileForDesktopMove.find(windowId);
                        if (savedIt != m_savedPreAutotileForDesktopMove.end()) {
                            if (savedIt.value().first == screenId) {
                                m_preAutotileGeometries[screenId][windowId] = savedIt.value().second;
                            } else {
                                qCDebug(lcEffect)
                                    << "Desktop switch: dropping cross-screen pre-autotile rect for" << windowId
                                    << "source=" << savedIt.value().first << "dest=" << screenId;
                            }
                            m_savedPreAutotileForDesktopMove.erase(savedIt);
                        }
                        qCInfo(lcEffect) << "Desktop switch: re-adding moved window to autotile:" << windowId << "on"
                                         << screenId;
                        notifyWindowAdded(w);
                    }
                }
            }

            // Re-apply borderless state for windows returning to autotile desktop.
            // The daemon skips retile for desktop return (tiledWindowCount > 0), so
            // slotWindowsTileRequested — which normally applies setWindowBorderless —
            // does NOT fire. KWin may also reset setNoBorder() for windows that were
            // on a non-current desktop. Re-apply here to ensure correct state.
            if (m_border.hideTitleBars) {
                for (const QString& screenId : added) {
                    for (KWin::EffectWindow* w : windows) {
                        if (!w || !m_effect->shouldHandleWindow(w) || !w->isOnCurrentDesktop()
                            || !w->isOnCurrentActivity() || w->isMinimized()) {
                            continue;
                        }
                        if (m_effect->getWindowScreenId(w) != screenId) {
                            continue;
                        }
                        const QString windowId = m_effect->getWindowId(w);
                        if (m_effect->isWindowFloating(windowId)) {
                            continue; // Floating windows don't get borderless
                        }
                        if (AutotileStateHelpers::borderlessOnScreen(m_border, screenId).contains(windowId)) {
                            // Already tracked — force KWin property in case it was reset
                            KWin::Window* kw = w->window();
                            if (kw && !kw->noBorder()) {
                                kw->setNoBorder(true);
                                qCDebug(lcEffect) << "Desktop return: re-applied setNoBorder for" << windowId;
                            }
                        }
                        // Don't apply borderless to untracked windows here — they may
                        // be genuinely new (opened while this desktop was inactive).
                        // Borderless is applied when the daemon retiles via
                        // slotWindowsTileRequested, not during desktop return.
                    }
                }
            }

            // Refresh active border for the focused window on the returned-to desktop
            m_effect->updateAllBorders();
        } else {
            // Genuine user toggle — process all added screens as new.

            // Save stacking order for windows on screens entering autotile.
            // Restored when leaving autotile so windows return to their
            // pre-autotile z-order (autotile focus changes reorder windows).
            for (KWin::EffectWindow* w : windows) {
                if (!w || !m_effect->shouldHandleWindow(w)) {
                    continue;
                }
                if (!w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
                    continue;
                }
                const QString screenId = m_effect->getWindowScreenId(w);
                if (added.contains(screenId)) {
                    m_savedSnapStackingOrder[screenId].append(m_effect->getWindowId(w));
                }
            }

            // Save pre-autotile geometry for ALL eligible windows (including minimized).
            // The window's current position IS the pre-autotile geometry we want to save.
            // For floating windows, also update daemon's pre-tile geometry to current
            // position with overwrite=true (user may have moved the window while floating).
            for (KWin::EffectWindow* w : windows) {
                if (!w || !m_effect->shouldHandleWindow(w)) {
                    continue;
                }
                if (!w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
                    continue;
                }
                const QString screenId = m_effect->getWindowScreenId(w);
                if (!added.contains(screenId)) {
                    continue;
                }
                const QString windowId = m_effect->getWindowId(w);
                saveAndRecordPreAutotileGeometry(windowId, screenId, w->frameGeometry());
                if (m_effect->isWindowFloating(windowId) && m_effect->m_daemonServiceRegistered) {
                    QRectF frame = w->frameGeometry();
                    // Use overwrite=false: an overflow-floated window may still have its
                    // frame at the tiled position. If a correct pre-tile entry already
                    // exists, preserve it. If no entry exists, the floating window's
                    // current geometry is the best available fallback.
                    PhosphorProtocol::ClientHelpers::fireAndForget(
                        m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                        QStringLiteral("storePreTileGeometry"),
                        {windowId, static_cast<int>(frame.x()), static_cast<int>(frame.y()),
                         static_cast<int>(frame.width()), static_cast<int>(frame.height()), screenId, false},
                        QStringLiteral("storePreTileGeometry"));
                }
            }

            // Batch-notify all windows on newly-added autotile screens in one D-Bus
            // call (windowsOpenedBatch) instead of per-window windowOpened round-trips.
            // saveAndRecordPreAutotileGeometry is called inside notifyWindowsAddedBatch.
            notifyWindowsAddedBatch(windows, added, /*resetNotified=*/true);
            qCInfo(lcEffect) << "Saved pre-autotile geometries for screens:" << added;

            // Async fetch of daemon's persisted pre-autotile geometries from previous session.
            // These may be more accurate than the current frame for windows that were resnapped
            // to zones in manual mode (current frame = zone position, daemon value = original).
            // Non-blocking: the old synchronous QDBus::Block call (500ms timeout) froze the
            // compositor thread, causing jerky first-retile animations since QElapsedTimer
            // kept advancing while no frames were rendered.
            QDBusMessage fetchMsg = QDBusMessage::createMethodCall(
                PhosphorProtocol::Service::Name, PhosphorProtocol::Service::ObjectPath,
                PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("getPreTileGeometries"));
            auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(fetchMsg), this);
            // Capture expected screen set for staleness detection — if the user
            // rapidly toggles autotile, a stale reply must not overwrite fresh data.
            const QSet<QString> expectedScreens = newScreens;
            connect(watcher, &QDBusPendingCallWatcher::finished, this,
                    [this, added, expectedScreens](QDBusPendingCallWatcher* w) {
                        w->deleteLater();
                        QDBusPendingReply<PhosphorProtocol::PreTileGeometryList> reply = *w;
                        if (!reply.isValid()) {
                            return;
                        }
                        // Bail if the autotile screen set changed while we were waiting
                        if (m_autotileScreens != expectedScreens) {
                            qCDebug(lcEffect) << "Stale async pre-autotile geometry reply, screen set changed";
                            return;
                        }
                        const PhosphorProtocol::PreTileGeometryList entries = reply.value();
                        const auto allWindows = KWin::effects->stackingOrder();
                        for (const auto& entry : entries) {
                            const QString stableId = entry.appId;
                            QRectF geom = QRectF(entry.toRect());
                            if (geom.width() <= 0 || geom.height() <= 0)
                                continue;
                            // Find all windows on added screens matching this stableId.
                            // If multiple windows share the same stableId (e.g., 3 Dolphin instances),
                            // the daemon's single geometry is ambiguous — skip the override entirely.
                            KWin::EffectWindow* matchedWindow = nullptr;
                            bool ambiguous = false;
                            for (KWin::EffectWindow* ew : allWindows) {
                                if (!ew || !m_effect->shouldHandleWindow(ew))
                                    continue;
                                if (::PhosphorIdentity::WindowId::extractAppId(m_effect->getWindowId(ew)) != stableId)
                                    continue;
                                if (!added.contains(m_effect->getWindowScreenId(ew)))
                                    continue;
                                if (matchedWindow) {
                                    ambiguous = true;
                                    break;
                                }
                                matchedWindow = ew;
                            }
                            if (ambiguous || !matchedWindow) {
                                if (ambiguous) {
                                    qCDebug(lcEffect) << "Skipping daemon geometry override for ambiguous stableId"
                                                      << stableId << "(multiple live windows match)";
                                }
                                continue;
                            }
                            {
                                const QString scr = m_effect->getWindowScreenId(matchedWindow);
                                auto& screenGeometries = m_preAutotileGeometries[scr];
                                const QString wId = m_effect->getWindowId(matchedWindow);
                                // Only pre-populate if no entry yet (saveAndRecordPreAutotileGeometry
                                // already ran for windows on these screens). If the entry matches the
                                // window's current frame (i.e., it was zone-snapped), prefer the daemon's
                                // stored value which is the original pre-autotile position.
                                auto existingIt = screenGeometries.find(wId);
                                if (existingIt == screenGeometries.end()) {
                                    screenGeometries[wId] = geom;
                                    qCDebug(lcEffect) << "Pre-populated pre-autotile geometry from daemon for"
                                                      << stableId << "on" << scr << ":" << geom;
                                } else if (existingIt.value().toRect() != geom.toRect()) {
                                    // Daemon stored a different geometry (likely from before the window
                                    // was resnapped to a zone). Prefer the daemon's version as it's the
                                    // true pre-autotile position.
                                    qCDebug(lcEffect) << "Updated pre-autotile geometry from daemon for" << stableId
                                                      << "on" << scr << ":" << existingIt.value() << "->" << geom;
                                    existingIt.value() = geom;
                                }
                            }
                        }
                    });
        } // else (genuine user toggle)
    }

    qCInfo(lcEffect) << "Autotile screens changed:" << m_autotileScreens;
}

void AutotileHandler::slotWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId)
{
    Q_UNUSED(screenId)
    qCInfo(lcEffect) << "Autotile floating changed:" << windowId << "isFloating:" << isFloating
                     << "screen:" << screenId;

    if (!isFloating) {
        m_effect->m_navigationHandler->setWindowFloating(windowId, false);
        KWin::EffectWindow* unfloatWin = m_effect->findWindowById(windowId);
        if (unfloatWin && unfloatWin == KWin::effects->activeWindow()) {
            m_pendingAutotileFocusWindowId = windowId;
            KWin::effects->activateWindow(unfloatWin);
        }
    } else {
        applyFloatCleanup(windowId);

        // Raise the floated window if it's the active window (user-initiated float)
        KWin::EffectWindow* floatWin = m_effect->findWindowById(windowId);
        if (!floatWin) {
            qCDebug(lcEffect) << "Autotile: window not found for float raise:" << windowId;
        } else if (floatWin == KWin::effects->activeWindow()) {
            m_pendingAutotileFocusWindowId = windowId;
            auto* ws = KWin::Workspace::self();
            if (ws) {
                KWin::Window* kw = floatWin->window();
                if (kw) {
                    ws->raiseWindow(kw);
                }
            }
            KWin::effects->activateWindow(floatWin);
        }
    }
}

void AutotileHandler::slotWindowMinimizedChanged(KWin::EffectWindow* w)
{
    if (!w || !m_effect->shouldHandleWindow(w) || !m_effect->isTileableWindow(w)) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    const QString screenId = m_effect->getWindowScreenId(w);

    if (!m_autotileScreens.contains(screenId)) {
        return;
    }

    const bool minimized = w->isMinimized();

    if (minimized) {
        if (m_effect->isWindowFloating(windowId)) {
            qCDebug(lcEffect) << "Autotile: minimized already-floating window, skipping floatWindow:" << windowId;
            return;
        }
        // A commit is already pending for this window — nothing new to do.
        if (m_pendingMinimizeFloat.contains(windowId)) {
            return;
        }

        // Debounce the float. KWin emits spurious minimizedChanged(true) events
        // on tiled windows when plasmashell notification popups change stacking;
        // the matching unminimize arrives ~1-2ms later. If we commit the float
        // immediately, the autotile engine recalculates with N-1 windows, the
        // surviving master gets tiled to the full screen, and the user sees a
        // "100%" flash on every notification. By deferring we give the
        // unminimize path a chance to cancel the float entirely.
        QPointer<KWin::EffectWindow> wPtr(w);
        auto* timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(kMinimizeFloatDebounceMs);
        connect(timer, &QTimer::timeout, this, [this, windowId, screenId, wPtr]() {
            // Consume the hash entry first. We take the timer out by value
            // so we own its deleteLater regardless of which early-return
            // branch we take below.
            auto it = m_pendingMinimizeFloat.find(windowId);
            if (it == m_pendingMinimizeFloat.end()) {
                return; // Already cancelled by cancelPendingMinimizeFloat.
            }
            QPointer<QTimer> owned = it.value();
            m_pendingMinimizeFloat.erase(it);
            if (owned) {
                owned->deleteLater();
            }

            // Re-validate the full entry predicate: the window may have been
            // destroyed, unminimized, moved, or had autotile disabled on its
            // screen during the debounce window. Mirrors the checks at entry
            // to slotWindowMinimizedChanged so the deferred path can't commit
            // a float the entry path would have rejected.
            if (!wPtr) {
                qCDebug(lcEffect) << "Autotile: debounced minimize window destroyed, skipping float:" << windowId;
                return;
            }
            KWin::EffectWindow* fw = wPtr.data();
            if (!m_effect->shouldHandleWindow(fw) || !m_effect->isTileableWindow(fw)) {
                qCDebug(lcEffect) << "Autotile: debounced minimize no longer handleable, skipping float:" << windowId;
                return;
            }
            if (!fw->isMinimized()) {
                qCDebug(lcEffect) << "Autotile: debounced minimize reverted, skipping float:" << windowId;
                return;
            }
            if (m_effect->isWindowFloating(windowId)) {
                return;
            }
            // Screen membership must still hold, and the window must still
            // be on the same screen we captured — a mid-debounce output
            // change invalidates the deferred commit.
            const QString currentScreenId = m_effect->getWindowScreenId(fw);
            if (currentScreenId != screenId || !m_autotileScreens.contains(currentScreenId)) {
                qCDebug(lcEffect) << "Autotile: debounced minimize screen changed or no longer autotiled, skipping:"
                                  << windowId << "was=" << screenId << "now=" << currentScreenId;
                return;
            }

            m_minimizeFloatedWindows.insert(windowId);

            qCInfo(lcEffect) << "Autotile: window minimized (after debounce), floating:" << windowId << "on"
                             << screenId;

            if (m_effect->m_daemonServiceRegistered) {
                PhosphorProtocol::ClientHelpers::fireAndForget(
                    m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                    QStringLiteral("setWindowFloatingForScreen"), {windowId, screenId, true},
                    QStringLiteral("setWindowFloatingForScreen"));
            }
        });
        m_pendingMinimizeFloat.insert(windowId, timer);
        timer->start();
        return;
    }

    // Unminimized path.
    //
    // If a debounced float is still pending, cancel it: the minimize was
    // spurious (a transient plasmashell-induced flicker). No D-Bus traffic,
    // no retile, no balloon-to-100% flash.
    if (m_pendingMinimizeFloat.contains(windowId)) {
        cancelPendingMinimizeFloat(windowId);
        qCDebug(lcEffect) << "Autotile: coalesced spurious minimize/unminimize cycle for" << windowId;
        notifyWindowAdded(w);
        return;
    }

    if (!m_minimizeFloatedWindows.remove(windowId)) {
        qCDebug(lcEffect) << "Autotile: unminimized window was not minimize-floated, skipping unfloatWindow:"
                          << windowId;
        notifyWindowAdded(w);
        return;
    }
    saveAndRecordPreAutotileGeometry(windowId, screenId, w->frameGeometry());

    qCInfo(lcEffect) << "Autotile: window unminimized, unfloating:" << windowId << "on" << screenId;

    if (m_effect->m_daemonServiceRegistered) {
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("setWindowFloatingForScreen"),
                                                       {windowId, screenId, false},
                                                       QStringLiteral("setWindowFloatingForScreen"));
    }

    notifyWindowAdded(w);
}

void AutotileHandler::slotWindowMaximizedStateChanged(KWin::EffectWindow* w, bool horizontal, bool vertical)
{
    if (m_suppressMaximizeChanged || !w) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    if (!m_monocleMaximizedWindows.contains(windowId)) {
        return;
    }
    if (horizontal || vertical) {
        return;
    }
    m_monocleMaximizedWindows.remove(windowId);
    // Use screen ID (EDID-based) for daemon tracking D-Bus calls
    const QString screenId = m_effect->getWindowScreenId(w);
    qCInfo(lcEffect) << "Monocle window manually unmaximized:" << windowId << "- floating";

    if (m_effect->m_daemonServiceRegistered) {
        PhosphorProtocol::ClientHelpers::fireAndForget(m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                                                       QStringLiteral("setWindowFloatingForScreen"),
                                                       {windowId, screenId, true},
                                                       QStringLiteral("setWindowFloatingForScreen"));
    }
}

void AutotileHandler::slotWindowFullScreenChanged(KWin::EffectWindow* w)
{
    if (!w || !w->isFullScreen()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    // Clear border and borderless tracking so borders are not drawn over fullscreen content
    m_border.zoneGeometries.remove(windowId);
    const bool wasBorderlessAnywhere = AutotileStateHelpers::isBorderlessWindow(m_border, windowId);
    AutotileStateHelpers::removeFromAllScreens(m_border, windowId);
    if (wasBorderlessAnywhere) {
        KWin::Window* kw = w->window();
        if (kw) {
            kw->setNoBorder(false);
        }
    }
    if (m_monocleMaximizedWindows.remove(windowId)) {
        qCInfo(lcEffect) << "Monocle window went fullscreen:" << windowId << "- removed from tracking";
    }
    m_effect->removeWindowBorder(windowId);
}

QVector<QPointer<KWin::EffectWindow>> AutotileHandler::takeSavedGlobalStack()
{
    return std::move(m_savedGlobalStackForResnap);
}

} // namespace PlasmaZones
