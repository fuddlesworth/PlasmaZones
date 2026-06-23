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
#include <QScopeGuard>
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
        // Internal-state cleanup only — fast, no compositor round-trips.
        // Title-bar restores are NOT done here: slotScreensChanged (which
        // always fires alongside this signal, both originating from the same
        // setActiveScreens(QSet()) call in the engine) handles them via a
        // delayed defer so the resnap dispatch can land first. Doing it here
        // would race with that defer and block applyGeometriesBatch.
        restoreAllMonocleMaximized();
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
    // Delegate per-entry so the cancel semantics (stop + deleteLater + erase)
    // live in exactly one place. keys() snapshot: the delegate erases.
    const QStringList ids = m_pendingMinimizeFloat.keys();
    for (const QString& windowId : ids) {
        cancelPendingMinimizeFloat(windowId);
    }
}

void AutotileHandler::slotScreensChanged(const QStringList& screenIds, bool isDesktopSwitch)
{
    // Invalidate in-flight stagger timers from prior autotile operations.
    // Without this, a desktop switch can race with a pending stagger from the
    // previous desktop, applying geometry from the old context.
    ++m_autotileStaggerGeneration;
    // Invalidate any in-flight loadSettings property reply — this signal
    // carries a newer screen set (see m_screensSignalGeneration doc).
    ++m_screensSignalGeneration;

    const QSet<QString> newScreens(screenIds.begin(), screenIds.end());
    const QSet<QString> removed = m_autotileScreens - newScreens;
    const QSet<QString> added = newScreens - m_autotileScreens;

    const auto windows = KWin::effects->stackingOrder();

    if (!removed.isEmpty()) {
        if (isDesktopSwitch) {
            qCInfo(lcEffect) << "slotScreensChanged: desktop switch, removed screens:" << removed;
            // Pass 1: windows on the desktop just LEFT. They are no longer
            // autotiled on this desktop; demote their tracking and remember
            // them for the desktop-return `added` branch. Do NOT restore —
            // their borderless / geometry state belongs to the other
            // desktop's still-live autotile session.
            for (KWin::EffectWindow* w : windows) {
                // isDeleted: close-grabbed dying windows linger in the
                // stacking order — getWindowScreenId/getWindowId on them
                // would re-pollute the scrubbed id caches.
                if (w && !w->isDeleted() && removed.contains(m_effect->getWindowScreenId(w))) {
                    if (!w->isOnCurrentDesktop()) {
                        const QString wid = m_effect->getWindowId(w);
                        if (m_notifiedWindows.remove(wid)) {
                            m_notifiedWindowScreens.remove(wid);
                            m_savedNotifiedForDesktopReturn.insert(wid);
                        }
                    }
                }
            }
            // Pass 2 (discussion #461): windows on the desktop just ARRIVED
            // AT, on a screen no longer autotiled here — the user switched
            // onto an autotile-disabled desktop. The daemon emits no
            // windowsReleased / resnap for a desktop switch (it deliberately
            // suppresses both), so these windows are stuck at their tiled
            // frame, borderless. Run the per-window restore — borders,
            // monocle, pre-autotile geometry, tracking — WITHOUT the
            // per-screen state clearing the genuine-toggle branch does: that
            // state belongs to the desktop we left. Every step below is a
            // no-op for a window that was never autotiled, so this is inert
            // on a healthy desktop switch.
            for (KWin::EffectWindow* w : windows) {
                // isDeleted: a window mid-close must not be churned through
                // the per-window restore steps (same cache hygiene as pass 1).
                if (!w || w->isDeleted() || !w->isOnCurrentDesktop()) {
                    continue;
                }
                const QString screenId = m_effect->getWindowScreenId(w);
                if (!removed.contains(screenId)) {
                    continue;
                }
                // setNoBorder() and geometry are global KWin properties — skip
                // sticky and multi-desktop windows UNCONDITIONALLY on a desktop
                // switch, even when this desktop's autotile set is empty: the
                // engine preserves the other desktop's TilingState, so such a
                // window may still be tiled in that desktop's live session and
                // restoring it here would leak the title bar / geometry into
                // that session. The only sanctioned restore for these windows
                // is the genuine-toggle path below (full disable arrives with
                // isDesktopSwitch=false and an empty set).
                if (w->isOnAllDesktops() || w->desktops().size() > 1) {
                    continue;
                }
                const QString windowId = m_effect->getWindowId(w);
                // A window the user floated in autotile keeps its
                // pre-autotile geometry entry and notify tracking — but its
                // CURRENT position is the user's chosen float spot.
                // Restoring the saved rect would teleport it, and demoting
                // its tracking would re-announce (and possibly re-tile) it
                // on desktop return. Floating windows need none of the
                // cleanup below: they hold no decoration ownership, no
                // border, no zone tracking.
                if (m_effect->isWindowFloating(windowId)) {
                    continue;
                }
                // Fullscreen windows: KWin owns their geometry and re-asserts
                // the fullscreen frame against any moveResize, so the
                // geometry-restore steps below would fight it and park the
                // window wherever KWin's exit-restore lands. The
                // enter-fullscreen slot already released the decoration and
                // border tracking. DO demote — this screen is leaving
                // autotile, and stale tracking would make the exit-fullscreen
                // slot re-claim the window on a no-longer-autotile screen.
                if (w->isFullScreen()) {
                    if (m_notifiedWindows.remove(windowId)) {
                        m_notifiedWindowScreens.remove(windowId);
                    }
                    continue;
                }
                // Capture tracked-ness BEFORE demoting: it is the only
                // evidence the window was actually autotile-managed on this
                // desktop. The daemon-fallback restore below must never fire
                // without it — the placement store is mode-shared,
                // appId-fuzzy and session-persisted, so a never-autotiled
                // free window would match a stale entry and teleport on a
                // mere desktop switch.
                const bool wasTracked = m_notifiedWindows.remove(windowId);
                if (wasTracked) {
                    m_notifiedWindowScreens.remove(windowId);
                }
                // Release autotile's decoration ownership on every screen.
                // The manager restores the title bar only when NO owner
                // remains — a snap takeover or a rule hide simply leaves
                // their owner in place. Deferred: each physical restore is a
                // 30-120 ms synchronous Wayland round-trip, and this loop can
                // visit many windows — restoring synchronously would stall
                // the compositor through the desktop-switch animation. The
                // drain after the loop runs them one per event-loop tick
                // (same policy as the genuine-toggle branch below and
                // updateHideTitleBarsSetting).
                m_effect->decorationManager()->releaseKind(windowId, DecorationManager::OwnerKind::Autotile,
                                                           DecorationManager::Restore::Deferred);
                AutotileStateHelpers::removeFromAllScreens(m_border, windowId);
                unmaximizeMonocleWindow(windowId);
                // Drop stale zone-centering tracking so a later
                // frameGeometryChanged does not re-snap the window into an
                // old autotile zone.
                m_autotileTargetZones.remove(windowId);
                m_centeredWaylandZones.remove(windowId);
                // Apply the pre-autotile geometry — the ONLY thing that
                // un-tiles the window, since the daemon does not resnap on a
                // desktop switch. m_preAutotileGeometries is read non-
                // destructively (the entry stays for the next genuine toggle);
                // the all-bucket lookup covers windows whose rect is keyed
                // under a screen other than their current one. BOTH restore
                // branches are gated on wasTracked: the geometry bucket also
                // survives non-destructively, so a window already restored by
                // a previous switch (now untracked, user may have moved it)
                // would otherwise be re-teleported to the stale rect on every
                // later switch onto this desktop.
                const QRectF savedGeo = findPreAutotileGeometry(windowId);
                if (savedGeo.isValid() && wasTracked) {
                    // applyWindowGeometry's moveResize, and the maximize-state
                    // clear below, emit windowFrameGeometryChanged
                    // synchronously; suppress the VS-crossing detectors
                    // (autotile slotWindowFrameGeometryChanged and the
                    // snapping windowFrameGeometryChanged handler) so this
                    // same-screen restore is not mistaken for a virtual-
                    // screen crossing — the genuine retile path guards the
                    // same way (tiling.cpp).
                    m_effect->m_inDaemonGeometryApply = true;
                    const auto geomGuard = qScopeGuard([this] {
                        m_effect->m_inDaemonGeometryApply = false;
                    });
                    // Clear any lingering KWin maximize flag before restoring
                    // the pre-autotile geometry: a still-maximized window
                    // makes KWin re-assert the maximize-area rect and defeat
                    // the restore — the tile-request path clears it for the
                    // same reason (discussion #461).
                    if (KWin::Window* kw = w->window(); kw && kw->maximizeMode() != KWin::MaximizeRestore) {
                        ++m_suppressMaximizeChanged;
                        kw->maximize(KWin::MaximizeRestore);
                        --m_suppressMaximizeChanged;
                    }
                    m_effect->applyWindowGeometry(w, savedGeo.toRect());
                } else if (wasTracked) {
                    // No local bucket entry but the window WAS tile-managed
                    // here: it was snap-managed when it entered autotile, so
                    // saveAndRecordPreAutotileGeometry deliberately stored
                    // nothing (its frame was the zone rect). The daemon's
                    // placement store holds the true pre-snap geometry —
                    // fetch it async and restore once the reply lands.
                    // Without this the window stays parked at its tiled
                    // frame. Gated on wasTracked: see the capture above.
                    requestDaemonPreTileRestore(w, windowId);
                }
            }
            // Drain the deferred releases queued above: the daemon emits no
            // resnap batch on a desktop switch, so without an explicit drain
            // the queued title-bar restores would sit until the 500 ms
            // fallback timer. One restore per event-loop tick, same as the
            // hide-title-bars toggle path.
            m_effect->decorationManager()->drainPendingRestores();
            m_effect->updateAllBorders();
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
                    // Skip sticky (all-desktops) and multi-desktop windows when
                    // some screens still use autotile. setNoBorder() is a global
                    // KWin property — restoring the border here would remove it
                    // on OTHER desktops where the window is still autotiled.
                    // Only restore when autotile is fully disabled.
                    if ((w->isOnAllDesktops() || w->desktops().size() > 1) && !newScreens.isEmpty()) {
                        continue;
                    }
                    windowsOnRemovedScreens.insert(m_effect->getWindowId(w));
                }
            }
            m_notifiedWindows -= windowsOnRemovedScreens;
            for (const QString& wid : std::as_const(windowsOnRemovedScreens)) {
                m_notifiedWindowScreens.remove(wid);
            }

            // Defer title-bar restores until slotApplyGeometriesBatch calls
            // DecorationManager::drainPendingRestores() after the daemon's
            // resnap signal has been dispatched. Each restore is a per-window
            // Wayland decoration round-trip; restoring synchronously here
            // would block kwin's event loop and serialize ahead of the queued
            // applyGeometriesBatch, stalling the snap animation by 250+ ms.
            // The manager's deferred queue also covers the races the old
            // hand-rolled drain handled: a window re-acquired mid-drain (snap
            // takeover or rapid re-toggle back into autotile) keeps its title
            // bar hidden, and a fallback timer drains if no resnap arrives.
            // Tiled tracking is cleared at stash time (not drain time as the
            // old code did), so autotile border OVERLAYS drop at the toggle
            // instant while the title bars restore during the resnap
            // animation — intentional: windows leaving autotile should not
            // keep autotile borders through the transition.
            for (const QString& wid : std::as_const(windowsOnRemovedScreens)) {
                m_effect->decorationManager()->releaseKind(wid, DecorationManager::OwnerKind::Autotile,
                                                           DecorationManager::Restore::Deferred);
                AutotileStateHelpers::removeFromAllScreens(m_border, wid);
            }

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

            // Clear autotile zone state for entries on REMOVED screens only.
            // A partial toggle (one screen disabled, sibling autotile screens
            // untouched) must not wipe sibling entries mid-animation — that
            // would strand their windows without a centering target (see the
            // NOTE in slotWindowsTileRequested). Entries are classified by
            // their TARGET ZONE's screen, not the window's current frame:
            // mid-retile the frame may still resolve to the removed screen
            // while the pending centering belongs to a surviving sibling
            // zone (Wayland clients commit the new rect asynchronously).
            // Entries whose window is gone are pruned unconditionally.
            // (Stagger timers were already invalidated by the bump at
            // function entry; no stagger can have been scheduled since.)
            const auto pruneRemovedScreenEntries = [this, &removed](QHash<QString, QRect>& map) {
                for (auto it = map.begin(); it != map.end();) {
                    KWin::EffectWindow* mw = m_effect->findWindowById(it.key());
                    if (!mw) {
                        it = map.erase(it);
                        continue;
                    }
                    const QPoint zoneCenter = it.value().center();
                    const auto* output = KWin::effects->screenAt(zoneCenter);
                    const QString entryScreen = output ? m_effect->resolveEffectiveScreenId(zoneCenter, output)
                                                       : m_effect->getWindowScreenId(mw);
                    if (removed.contains(entryScreen)) {
                        it = map.erase(it);
                    } else {
                        ++it;
                    }
                }
            };
            pruneRemovedScreenEntries(m_autotileTargetZones);
            pruneRemovedScreenEntries(m_centeredWaylandZones);

            // Clear pre-autotile geometries for removed screens — they're
            // no longer needed.
            for (const QString& screenId : removed) {
                m_preAutotileGeometries.remove(screenId);
            }
        }
    }

    m_autotileScreens = newScreens;

    // A desktop switch enters even with empty `added`: when both desktops'
    // autotile sets are IDENTICAL the engine re-emits the unchanged set with
    // isDesktopSwitch=true (discussion #219) solely so the catch-scan below
    // can re-add windows moved here while the user was away. The added-keyed
    // re-tracking loops are vacuous no-ops in that case (Pass 1 demoted
    // nothing).
    if (!added.isEmpty() || isDesktopSwitch) {
        if (isDesktopSwitch) {
            // Desktop/activity return: windows are already tiled on this desktop.
            // Re-add current-desktop windows to m_notifiedWindows so they're not
            // re-notified by later notifyWindowAdded calls (e.g., window moves).
            qCInfo(lcEffect) << "slotScreensChanged: desktop return, added screens:" << added
                             << "autotile screens:" << m_autotileScreens;
            for (const QString& screenId : added) {
                for (KWin::EffectWindow* w : windows) {
                    if (w && m_effect->shouldHandleWindow(w) && w->isOnCurrentDesktop() && w->isOnCurrentActivity()
                        && m_effect->getWindowScreenId(w) == screenId) {
                        const QString windowId = m_effect->getWindowId(w);
                        if (m_savedNotifiedForDesktopReturn.contains(windowId)
                            || m_notifiedWindows.contains(windowId)) {
                            // Previously tracked — re-add without re-notifying the
                            // daemon. Restore the SCREEN record too: the demotion
                            // dropped both, and a window tracked with an empty
                            // screen record never detects cross-monitor / cross-VS
                            // transfers again (handleWindowOutputChanged
                            // early-returns on an unknown old screen).
                            m_notifiedWindows.insert(windowId);
                            m_notifiedWindowScreens[windowId] = screenId;
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

            // Catch windows moved to this desktop while the user was on
            // another — they were removed from tiling by windowDesktopsChanged
            // on the source desktop and need re-adding here. Scans ALL
            // autotile screens, not just `added`, so it covers both
            // PARTIAL-overlap desktop switches (the moved window sits on a
            // shared screen that isn't in `added`) and IDENTICAL-set switches
            // (discussion #219), where the engine re-emits the unchanged set
            // with isDesktopSwitch=true precisely to reach this scan.
            // notifyWindowAdded is idempotent (checks m_notifiedWindows).
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
                        // RE-ADD (desktop return): this window's current frame is
                        // the tiled rect from the source desktop, not a free
                        // position. knownFreeFloating=false runs the floating
                        // guard so a tiled rect is not persisted as free geometry
                        // (a stash restore above already populated the local
                        // bucket for windows that had one; this protects the rest).
                        notifyWindowAdded(w, /*knownFreeFloating=*/false);
                    }
                }
            }

            // Refresh active border for the focused window on the returned-to
            // desktop. This also re-asserts borderless state: KWin silently
            // resets noBorder for windows on non-current desktops and the
            // daemon skips the retile on desktop return, but updateAllBorders
            // runs DecorationManager::resyncWindow for every window — a
            // self-guarding, owner-kind-agnostic re-hide of exactly the
            // windows the manager owns whose decoration came back.
            m_effect->updateAllBorders();
        } else {
            // Genuine user toggle — process all added screens as new.

            // Save pre-autotile geometry for ALL eligible windows (including minimized).
            // The window's current position IS the pre-autotile geometry we want to save.
            // Floating windows additionally back-fill the daemon's pre-tile
            // entry — non-destructively (overwrite=false; the inner comment
            // explains why an overflow float must not clobber a correct
            // existing entry).
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
                    // qRound, not truncation: fractional-scale sub-pixel
                    // residue (matches the toRect() geometry-capture convention).
                    PhosphorProtocol::ClientHelpers::fireAndForget(
                        m_effect, PhosphorProtocol::Service::Interface::WindowTracking,
                        QStringLiteral("storePreTileGeometry"),
                        {windowId, qRound(frame.x()), qRound(frame.y()), qRound(frame.width()), qRound(frame.height()),
                         screenId, false},
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
            auto* watcher = new QDBusPendingCallWatcher(
                PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                           QStringLiteral("getPreTileGeometries")),
                this);
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
                                // isDeleted: a dying same-app window must not
                                // consume the geometry override or trip the
                                // ambiguous-skip, robbing the live window.
                                if (!ew || ew->isDeleted() || !m_effect->shouldHandleWindow(ew))
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
    // No pre-autotile geometry capture here: a minimize-floated window cannot
    // move while minimized, so its frame is always the tiled rect — recording
    // it would poison the local float-back cache for windows with no prior
    // entry (snap→autotile transitions), and a genuine entry already exists
    // from the original capture.

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
    // isDeleted: a dying window's fullscreen flip must not create a stale
    // decoration claim / border-tracking entry that only close-cleanup sweeps.
    if (!w || w->isDeleted()) {
        return;
    }
    const QString windowId = m_effect->getWindowId(w);
    if (!w->isFullScreen()) {
        // EXIT fullscreen: a window the daemon still tiles (it stayed in
        // m_notifiedWindows the whole time — the daemon never untiles on
        // fullscreen) returns to its tiled rect, so re-establish the
        // decoration claim and border tracking the enter-path released.
        const QString screenId = m_notifiedWindowScreens.value(windowId);
        if (!m_notifiedWindows.contains(windowId)) {
            // Never-tracked window: a window that OPENED fullscreen was
            // rejected by isEligibleForAutotileNotify (fullscreen guard) and
            // never announced. Now that it has a normal frame, announce it so
            // the daemon tiles it (notifyWindowAdded re-checks eligibility,
            // including the current desktop/activity).
            const QString currentScreen = m_effect->getWindowScreenId(w);
            if (m_autotileScreens.contains(currentScreen)) {
                notifyWindowAdded(w);
            }
            return;
        }
        if (screenId.isEmpty()) {
            return;
        }
        // Autotile was disabled on this window's tracked screen while it was
        // fullscreen on a non-current desktop (the genuine-toggle path skips
        // non-current-desktop windows, so the tracking survived). Re-claiming
        // onto a no-longer-autotiled screen would hide the title bar with no
        // retile coming — demote the stale tracking and release instead.
        if (!m_autotileScreens.contains(screenId)) {
            m_notifiedWindows.remove(windowId);
            m_notifiedWindowScreens.remove(windowId);
            m_effect->decorationManager()->releaseKind(windowId, DecorationManager::OwnerKind::Autotile);
            return;
        }
        // Floating windows stay released: a window floated while fullscreen
        // (manual toggle, minimize-float, overflow batch-float — all keep
        // m_notifiedWindows intact) is free-floating on exit; re-claiming it
        // would hide its title bar with no retile ever restoring it.
        if (m_effect->isWindowFloating(windowId)) {
            return;
        }
        AutotileStateHelpers::addTiledOnScreen(m_border, screenId, windowId);
        if (m_border.hideTitleBars) {
            // AlreadyPlaced: the window is back at its tiled frame — KWin
            // restores the pre-fullscreen geometry itself; the manager only
            // needs to re-hide the title bar and re-assert that frame.
            m_effect->decorationManager()->acquire(windowId, DecorationManager::autotile(screenId),
                                                   DecorationManager::Placement::AlreadyPlaced);
        }
        m_effect->updateAllBorders();
        return;
    }
    // Clear border tracking and release decoration ownership so borders are
    // not drawn over fullscreen content (the manager restores the title bar
    // unless another owner still claims the window).
    AutotileStateHelpers::removeFromAllScreens(m_border, windowId);
    m_effect->decorationManager()->releaseKind(windowId, DecorationManager::OwnerKind::Autotile);
    if (m_monocleMaximizedWindows.remove(windowId)) {
        qCInfo(lcEffect) << "Monocle window went fullscreen:" << windowId << "- removed from tracking";
    }
    // Drop any unconsumed zone-centering entries: a window that fullscreens
    // right after being tiled must not have the fullscreen frame change
    // consume a stale centering target and moveResize against the fullscreen
    // geometry (same cleanup applyFloatCleanup performs).
    m_autotileTargetZones.remove(windowId);
    m_centeredWaylandZones.remove(windowId);
    m_effect->removeWindowBorder(windowId);
}

} // namespace PlasmaZones
