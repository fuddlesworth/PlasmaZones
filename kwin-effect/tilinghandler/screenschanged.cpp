// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The managed-screen-set change handler for TilingHandler.
// Split from signals.cpp, which was past the file-size ceiling: this is one
// coherent concern (slotScreensChanged and the two halves of its
// removed-screens pass) and it accounted for most of that file.

#include "tilinghandler.h"
#include "plasmazoneseffect/plasmazoneseffect.h"
#include "compositor/effectlogging.h"
#include "handlers/snaphandler.h" // cross-mode minimize-float adoption

#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/ClientHelpers.h>

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QTimer>

namespace PlasmaZones {

// Pass over the windows on screens that left the managed set because the
// DESKTOP (or activity) changed, not because autotile was turned off for
// them. Extracted from slotScreensChanged, which carried five distinct
// phases in one body; this is the demote half and the caller keeps the
// epoch bookkeeping and the managed-set write around it.
//
// Demote only. The windows' borderless and geometry state belongs to the
// desktop's still-live autotile session, so nothing is restored here — see
// the per-pass comments below.
void TilingHandler::demoteWindowsForDesktopSwitch(const QSet<QString>& removed,
                                                  const QList<KWin::EffectWindow*>& windows,
                                                  QStringList& windowedFsToRelease,
                                                  QHash<QString, QRectF>& windowedFsPreTileRestore)
{
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
            // Activity axis too: an ACTIVITY switch arrives here with
            // isDesktopSwitch=true as well (the engine ORs both into
            // the switch flag), and a window of the left activity is
            // still on the current desktop — it must be demoted the
            // same way, or pass 2 un-tiles it below (#808, activity
            // variant).
            if (!w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
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
        // Activity term matches pass 1: a left-activity window is
        // still tiled in that activity's live session and must not be
        // restored/teleported here.
        if (!w || w->isDeleted() || !w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
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
        // Activities carry the same hazard: empty activities() means
        // all-activities, and a multi-activity window may be tiled in
        // another activity's live session.
        if (w->isOnAllDesktops() || w->desktops().size() > 1 || w->activities().isEmpty()
            || w->activities().size() > 1) {
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
        //
        // A windowed-fullscreen window leaving its strip on THIS
        // (current) context: the flag's owner is gone, so windowed
        // fullscreen ends. Forget membership NOW — with the entry
        // gone the tracked path's geometry restore below bails on
        // the still-requested fullscreen state instead of fighting
        // it — and queue the state release for after the managed-set
        // write (the split the release helpers document).
        const bool wasWindowedFs = m_effect->m_windowedFullscreenWindows.contains(windowId);
        if (wasWindowedFs) {
            forgetWindowedFullscreen(windowId);
            // The clear-in-flight marker dies with the hold it
            // guarded (the untrack funnel and the snap↔snap belt
            // drop it the same way) — a marker outliving the strip
            // membership can only refuse a future adopt.
            m_windowedFsClearInFlight.remove(windowId);
            windowedFsToRelease.append(windowId);
        } else if (w->isFullScreen()) {
            // Genuinely fullscreen (user F11): KWin owns its
            // geometry and re-asserts the fullscreen frame, so the
            // restore steps below would fight it. Demote the stale
            // tracking and release instead.
            if (m_notifiedWindows.remove(windowId)) {
                m_notifiedWindowScreens.remove(windowId);
            }
            // Same untrack outcome as the arm below, so the per-session
            // scroll companions go here too rather than being left
            // behind by this early exit.
            m_effect->m_scrollCommandedRects.remove(windowId);
            m_effect->m_scrollOfferedColumn.remove(windowId);
            if (m_effect->m_scrollVisualDelta.remove(windowId) > 0 && KWin::effects) {
                KWin::effects->addRepaintFull();
            }
            // The centring targets go with them, for the same reason the
            // arm below sheds them: a stale target consumed against the
            // fullscreen frame change would raw-moveResize the window to
            // its old zone and clamp it onto that zone's output.
            m_tileTargetZones.remove(windowId);
            m_centeredWaylandZones.remove(windowId);
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
        // Drop autotile tiled tracking on every screen (title-bar
        // restores flow through the rule path).
        clearWindowTiledAllScreens(windowId);
        unmaximizeMonocleWindow(windowId);
        // The column mirror goes with them for the windowed-fullscreen arm's
        // reason: this window's strip is ending, so no batch remains to carry
        // a cleared flag back, and the bit would otherwise outlive the mode.
        releaseColumnMaximized(windowId, w);
        // Drop stale zone-centering tracking so a later
        // frameGeometryChanged does not re-snap the window into an
        // old autotile zone.
        m_tileTargetZones.remove(windowId);
        m_centeredWaylandZones.remove(windowId);
        // The per-session scroll companions go with the untrack too,
        // exactly as the genuine-toggle arm below does it. This arm
        // reaches the same untracked outcome, so leaving them behind
        // means the counter-assert can re-arm against a rect from the
        // session that just ended, and a relocation from a dead strip
        // survives into the next one.
        m_effect->m_scrollCommandedRects.remove(windowId);
        m_effect->m_scrollOfferedColumn.remove(windowId);
        if (m_effect->m_scrollVisualDelta.remove(windowId) > 0 && KWin::effects) {
            KWin::effects->addRepaintFull();
        }
        // Apply the pre-autotile geometry — the ONLY thing that
        // un-tiles the window, since the daemon does not resnap on a
        // desktop switch. m_preTileGeometries is read non-
        // destructively (the entry stays for the next genuine toggle);
        // the all-bucket lookup covers windows whose rect is keyed
        // under a screen other than their current one. BOTH restore
        // branches are gated on wasTracked: the geometry bucket also
        // survives non-destructively, so a window already restored by
        // a previous switch (now untracked, user may have moved it)
        // would otherwise be re-teleported to the stale rect on every
        // later switch onto this desktop.
        const QRectF savedGeo = findPreTileGeometry(windowId);
        if (savedGeo.isValid() && wasTracked && wasWindowedFs) {
            // The apply below would bail inside applyWindowGeometry on
            // the still-requested fullscreen state (membership is
            // already forgotten, so the exemption no longer fires).
            // Queue it for after the deferred release drops the state.
            windowedFsPreTileRestore.insert(windowId, savedGeo);
        } else if (savedGeo.isValid() && wasTracked) {
            // applyWindowGeometry's moveResize, and the maximize-state
            // clear below, emit windowFrameGeometryChanged
            // synchronously; suppress the VS-crossing detectors
            // (autotile slotWindowFrameGeometryChanged and the
            // snapping windowFrameGeometryChanged handler) so this
            // same-screen restore is not mistaken for a virtual-
            // screen crossing — the genuine retile path guards the
            // same way (tiling.cpp).
            // Save/restore, not set/clear (nesting-safe).
            const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
            m_effect->m_daemonGate.inGeometryApply = true;
            const auto geomGuard = qScopeGuard([this, prevInApply] {
                m_effect->m_daemonGate.inGeometryApply = prevInApply;
            });
            // Clear any lingering KWin maximize flag before restoring
            // the pre-autotile geometry: a still-maximized window
            // makes KWin re-assert the maximize-area rect and defeat
            // the restore — the tile-request path clears it for the
            // same reason (discussion #461).
            //
            // Through the ledger when the ledger owns the bit, so
            // membership and the bit move TOGETHER. A bare clear here
            // would strip a column-maximize member's bit while leaving
            // the effect recorded as still holding it, which is the
            // exact split m_columnMaximizedWindows' contract forbids.
            //
            // The GUARD is what earns its place here, not the call behind
            // it. releaseColumnMaximized already ran unconditionally earlier
            // in this same iteration, so membership survives to here in
            // exactly one case: that call SKIPPED a still-fullscreen window
            // and retained the entry on purpose. Re-calling it now skips
            // again for the same reason, making the then-branch a no-op.
            //
            // Deleting the condition and keeping only the else-branch would
            // therefore not be equivalent — it would hand that retained
            // member the bare clear, which is precisely the ledger split the
            // paragraph above forbids. Keep the test; do not "simplify" it
            // away on the grounds that the call inside it does nothing.
            if (m_columnMaximizedWindows.contains(windowId)) {
                releaseColumnMaximized(windowId, w);
            } else if (KWin::Window* kw = w->window(); kw && kw->maximizeMode() != KWin::MaximizeRestore) {
                applyMaximizeSuppressed(kw, KWin::MaximizeRestore);
            }
            // Snap-out: leaving tile-managed sizing.
            m_effect->applyWindowGeometry(w, savedGeo.toRect(), /*allowDuringDrag=*/false,
                                          /*skipAnimation=*/false, PhosphorAnimation::ProfilePaths::WindowSnapOut);
            // Re-seed the tracked screen: the bracket above
            // suppressed the VS-crossing detectors whose early
            // return sits BEFORE their tracker write, and
            // applyWindowGeometry does not self-seed — the pre-tile
            // restore can legitimately land in a different virtual
            // screen than the tiled rect, and a stale entry makes
            // the next genuine geometry change read as a spurious
            // VS crossing (the daemon-fallback arm of this same
            // if/else chain re-seeds for exactly this reason).
            m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
        } else if (wasTracked) {
            // No local bucket entry but the window WAS tile-managed
            // here: it was snap-managed when it entered autotile, so
            // saveAndRecordPreTileGeometry deliberately stored
            // nothing (its frame was the zone rect). The daemon's
            // placement store holds the true pre-snap geometry —
            // fetch it async and restore once the reply lands.
            // Without this the window stays parked at its tiled
            // frame. Gated on wasTracked: see the capture above.
            requestDaemonPreTileRestore(w, windowId, screenId);
        }
    }
    m_effect->updateAllDecorations();
}

// Pass over the windows on screens that left the managed set because
// autotile was genuinely turned off for them (a mode toggle or a disable),
// as opposed to a desktop switch. Extracted from slotScreensChanged
// alongside its sibling above; this half untracks and restores, because no
// other session owns these windows any more.
void TilingHandler::untrackWindowsForDisabledScreens(const QSet<QString>& removed, const QSet<QString>& newScreens,
                                                     const QList<KWin::EffectWindow*>& windows,
                                                     QStringList& windowedFsToRelease)
{
    QSet<QString> windowsOnRemovedScreens;
    for (KWin::EffectWindow* w : windows) {
        // isDeleted: close-grabbed dying windows linger in the
        // stacking order — getWindowId on them would re-pollute the
        // scrubbed id caches (same hazard as the batch loop in
        // wiring.cpp).
        if (w && !w->isDeleted() && removed.contains(m_effect->getWindowScreenId(w))) {
            // Only restore borders for windows on the CURRENT desktop
            // AND activity. Windows in other contexts may still be
            // autotiled and must keep their borderless state —
            // restoring them here would leak title bars into those
            // contexts' autotile sessions.
            if (!w->isOnCurrentDesktop() || !w->isOnCurrentActivity()) {
                continue;
            }
            // Skip sticky (all-desktops) and multi-desktop windows when
            // some screens still use autotile. setNoBorder() is a global
            // KWin property — restoring the border here would remove it
            // on OTHER desktops where the window is still autotiled.
            // Only restore when autotile is fully disabled.
            //
            // Activities carry the identical hazard and the demote pass
            // already spells it out: an empty activities() means
            // all-activities, and a multi-activity window may be tiled in
            // another activity's live session. Without these terms such a
            // window has its border restored and its tracking dropped here
            // while the other activity still autotiles it.
            if ((w->isOnAllDesktops() || w->desktops().size() > 1 || w->activities().isEmpty()
                 || w->activities().size() > 1)
                && !newScreens.isEmpty()) {
                continue;
            }
            windowsOnRemovedScreens.insert(m_effect->getWindowId(w));
        }
    }
    m_notifiedWindows -= windowsOnRemovedScreens;
    for (const QString& wid : std::as_const(windowsOnRemovedScreens)) {
        m_notifiedWindowScreens.remove(wid);
        // A genuine mode toggle ends windowed fullscreen for the
        // windows it untracks: their strip is gone and no batch will
        // ever un-flag them. Same collection-set skips as the border
        // restore above (sticky/off-context windows keep their state
        // until their own context is processed) — deliberate, the
        // per-context philosophy borders already follow.
        if (m_effect->m_windowedFullscreenWindows.contains(wid)) {
            forgetWindowedFullscreen(wid);
            // Marker dies with the hold — same rationale as the
            // demote arm above and the untrack funnel.
            m_windowedFsClearInFlight.remove(wid);
            windowedFsToRelease.append(wid);
        }
    }

    // Drop autotile border tracking at the toggle instant so autotile
    // border OVERLAYS clear immediately — windows leaving autotile
    // should not keep autotile borders through the transition.
    // Title-bar restores flow through the rule path.
    // The per-session scroll companions go with the untrack, like
    // every other exit funnel (cleanupAutotileTracking, float
    // cleanup, the snap↔snap belt): the counter-assert must not
    // re-arm against a rect from the mode that just ended, and the
    // membership clear in this loop ends the paint relocation,
    // so the relocation-delta removal pairs with damage.
    bool anyVisualDeltaDropped = false;
    for (const QString& wid : std::as_const(windowsOnRemovedScreens)) {
        clearWindowTiledAllScreens(wid);
        m_effect->m_scrollCommandedRects.remove(wid);
        m_effect->m_scrollOfferedColumn.remove(wid);
        anyVisualDeltaDropped = (m_effect->m_scrollVisualDelta.remove(wid) > 0) || anyVisualDeltaDropped;
    }
    if (anyVisualDeltaDropped && KWin::effects) {
        KWin::effects->addRepaintFull();
    }

    // Save autotile stacking order before restoring snap-mode order.
    // This allows restoring the user's autotile z-order (e.g. floated
    // windows raised to front) when re-entering autotile mode.
    // Only save windows on the current desktop — other desktops' windows
    // are not being toggled and their stacking order is irrelevant here.
    for (const QString& screenId : removed) {
        QStringList autotileOrder;
        for (KWin::EffectWindow* w : windows) {
            if (w && !w->isDeleted() && m_effect->shouldHandleWindow(w) && w->isOnCurrentDesktop()
                && w->isOnCurrentActivity() && m_effect->getWindowScreenId(w) == screenId) {
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
        if (!w || w->isDeleted() || !m_effect->shouldHandleWindow(w) || !w->isOnCurrentDesktop()
            || !w->isOnCurrentActivity()) {
            continue;
        }
        const QString screenId = m_effect->getWindowScreenId(w);
        if (!removed.contains(screenId)) {
            continue;
        }
        unmaximizeMonocleWindow(m_effect->getWindowId(w));
        // Same pairing as the demote arm above: the removed screen's strip is
        // gone, so the column mirror has no later batch to un-flag it.
        releaseColumnMaximized(m_effect->getWindowId(w), w);
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
            // EXACT resolve: the entry is keyed to a specific
            // instance's id, and a fuzzy same-app hit would keep a
            // dead-keyed entry alive forever (its key never matches a
            // live window again, so the not-found prune is its only
            // exit).
            KWin::EffectWindow* mw = m_effect->findWindowByIdExact(it.key());
            if (!mw) {
                it = map.erase(it);
                continue;
            }
            const QPoint zoneCenter = it.value().center();
            const auto* output = KWin::effects->screenAt(zoneCenter);
            const QString entryScreen =
                output ? m_effect->resolveEffectiveScreenId(zoneCenter, output) : m_effect->getWindowScreenId(mw);
            if (removed.contains(entryScreen)) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    };
    pruneRemovedScreenEntries(m_tileTargetZones);
    pruneRemovedScreenEntries(m_centeredWaylandZones);
    // The offered column is pruned the same way, and needs to be: the per-window
    // loop above classifies by the window's CURRENT frame, but this entry is a
    // column RECT on a specific screen, and its consumer issues a real move()
    // into that rect. An entry surviving its screen's removal teleports a live
    // window into a dead column — strictly worse than the stale centring
    // targets this prune already covers.
    //
    // For a PARKED column the stored rect's centre sits below every output, so
    // screenAt() returns null and the lambda falls back to the window's own
    // screen. That fallback is the right answer here; do not "fix" it.
    pruneRemovedScreenEntries(m_effect->m_scrollOfferedColumn);

    // Expected-output-move markers, pruned BY VALUE rather than by key: the key
    // is a window id, but what leaves the managed set is a SCREEN, and the
    // marker names two of them.
    //
    // `removed` here is screens that left the MANAGED set (this function's own
    // contract above), which a mode toggle or a per-screen disable reaches as
    // well as a physical unplug.
    //
    // BOTH ends are prune triggers, because a marker is a one-shot that only
    // the expected outputChanged can consume:
    //   - target gone: the event it was armed for can never arrive.
    //   - source gone: the handoff it describes is not happening either, since
    //     the engine that would have driven it no longer manages that screen.
    // Either way the marker outlives its errand, and the general drain does not
    // reach it: that drain (in handleWindowOutputChanged) fires on the window's
    // NEXT outputChanged, and this function deliberately skips off-desktop,
    // off-activity and sticky windows, so exactly those keep their tracking and
    // their stale marker. When such a window later hops to the marked target,
    // the hop is consumed as bookkeeping-only and its real transfer never runs.
    //
    // The cost of pruning on source is that an echo still in flight for a
    // genuine handoff falls through to the positional transfer instead of the
    // marker's early return. That is the right trade here: with the source no
    // longer managed there is no engine left on that side for the marker to
    // hand off from, whereas a surviving stale marker silently eats a later
    // real move.
    for (auto it = m_expectedOutputMove.begin(); it != m_expectedOutputMove.end();) {
        if (removed.contains(it.value().targetScreenId) || removed.contains(it.value().sourceScreenId)) {
            it = m_expectedOutputMove.erase(it);
        } else {
            ++it;
        }
    }

    // Clear pre-autotile geometries captured for removed screens —
    // per ENTRY, not per bucket: the bucket key names the rect's
    // capture-time coordinate space, and a window transferred to a
    // still-managed screen keeps its rect filed under the OLD
    // screen's bucket (outputchange.cpp re-files it there on
    // purpose). Dropping the whole bucket would lose those windows'
    // effect-side float-backs and leave only the daemon copy.
    for (const QString& screenId : removed) {
        auto bucketIt = m_preTileGeometries.find(screenId);
        if (bucketIt == m_preTileGeometries.end()) {
            continue;
        }
        for (auto it = bucketIt->begin(); it != bucketIt->end();) {
            KWin::EffectWindow* w = m_effect->findWindowByIdExact(it.key());
            const QString current = (w && !w->isDeleted()) ? m_effect->getWindowScreenId(w) : QString();
            if (current.isEmpty() || removed.contains(current)) {
                it = bucketIt->erase(it);
            } else {
                ++it;
            }
        }
        if (bucketIt->isEmpty()) {
            m_preTileGeometries.erase(bucketIt);
        }
    }
}

void TilingHandler::fetchDaemonPreTileGeometries(const QSet<QString>& added, const QSet<QString>& expectedScreens)
{
    // Extracted from slotScreensChanged so the superseded arm below can
    // re-dispatch itself. It is a one-shot per genuine autotile toggle, so a
    // superseded reply that simply returned would lose the daemon's true
    // pre-tile geometries for the rest of the session.
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("getPreTileGeometries")),
        this);
    // The GENERATION at dispatch, alongside the expected screen set.
    // slotScreensChanged bumps it for exactly this purpose, and set-equality
    // alone does not answer the question: a toggle off and back, or a later
    // signal landing on a managed set that happens to match, leaves the sets
    // equal while the captured `added` set describes a transition that has been
    // superseded. The entries below are written as restore rects the
    // desktop-switch path later applies verbatim, so a stale apply teleports a
    // window.
    const quint64 generationAtDispatch = m_screensSignalGeneration;
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, added, expectedScreens, generationAtDispatch](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<PhosphorProtocol::PreTileGeometryList> reply = *w;
                if (!reply.isValid()) {
                    return;
                }
                // Bail if the autotile screen set changed while we were waiting
                if (m_managedScreens != expectedScreens) {
                    qCDebug(lcEffect) << "Stale async pre-autotile geometry reply, screen set changed";
                    return;
                }
                // A newer signal superseded this dispatch. RE-DISPATCH rather
                // than simply returning: the generation bumps on every
                // slotScreensChanged including a no-op re-emit with an
                // identical set, so this test is strictly stricter than the set
                // test above — and the fetch is a one-shot per genuine autotile
                // toggle, so a silent drop loses the daemon's true pre-tile
                // geometries for the session and leaves float-back on the
                // current-frame captures. Returning here would trade a rare
                // teleport for a more frequent silent loss.
                if (m_screensSignalGeneration != generationAtDispatch) {
                    qCDebug(lcEffect) << "Superseded async pre-autotile geometry reply, re-requesting for" << added;
                    fetchDaemonPreTileGeometries(added, m_managedScreens);
                    return;
                }
                const PhosphorProtocol::PreTileGeometryList entries = reply.value();
                QHash<QString, QHash<QString, int>> entryCounts;
                for (const auto& entry : entries) {
                    if (added.contains(entry.screenId) && entry.width > 0 && entry.height > 0) {
                        ++entryCounts[entry.appId][entry.screenId];
                    }
                }
                const auto allWindows = KWin::effects->stackingOrder();
                for (const auto& entry : entries) {
                    const QString stableId = entry.appId;
                    QRectF geom = QRectF(entry.toRect());
                    if (geom.width() <= 0 || geom.height() <= 0 || !added.contains(entry.screenId)
                        || entryCounts.value(stableId).value(entry.screenId) != 1) {
                        continue;
                    }
                    // Origin validation, not just extents. These rects
                    // are applied verbatim by the desktop-switch
                    // restore path, so a daemon-supplied origin on no
                    // connected output parks the window where nothing
                    // can show it. The capture side has a chokepoint
                    // for this; the ingest side did not.
                    if (!KWin::effects || !KWin::effects->screenAt(geom.center().toPoint())) {
                        qCDebug(lcEffect)
                            << "Ignoring daemon pre-tile geometry off every output for" << stableId << ":" << geom;
                        continue;
                    }
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
                        if (m_effect->getWindowScreenId(ew) != entry.screenId)
                            continue;
                        if (matchedWindow) {
                            ambiguous = true;
                            break;
                        }
                        matchedWindow = ew;
                    }
                    if (ambiguous || !matchedWindow) {
                        if (ambiguous) {
                            qCDebug(lcEffect) << "Skipping daemon geometry override for ambiguous stableId" << stableId
                                              << "(multiple live windows match)";
                        }
                        continue;
                    }
                    {
                        const QString wId = m_effect->getWindowId(matchedWindow);
                        // ALL-bucket lookup, matching the rule
                        // saveAndRecordPreTileGeometry documents: a
                        // per-screen check here let a transferred
                        // window (the cross-output path re-files its
                        // rect under the CAPTURE screen's bucket)
                        // gain a SECOND entry under its current
                        // screen, and findPreTileGeometry then
                        // returned whichever bucket hash order
                        // reached first — possibly a rect measured
                        // in the other monitor's coordinate space.
                        // Update the existing entry in place under
                        // its own bucket; insert under the current
                        // screen only on a genuine all-bucket miss.
                        QString bucketScreenId;
                        const QRectF existing = findPreTileGeometry(wId, &bucketScreenId);
                        if (!existing.isValid()) {
                            const QString scr = m_effect->getWindowScreenId(matchedWindow);
                            m_preTileGeometries[scr][wId] = geom;
                            qCDebug(lcEffect) << "Pre-populated pre-autotile geometry from daemon for" << stableId
                                              << "on" << scr << ":" << geom;
                        } else if (existing.toRect() != geom.toRect()) {
                            // Daemon stored a different geometry (likely from before the window
                            // was resnapped to a zone). Prefer the daemon's version as it's the
                            // true pre-autotile position.
                            qCDebug(lcEffect) << "Updated pre-autotile geometry from daemon for" << stableId
                                              << "in bucket" << bucketScreenId << ":" << existing << "->" << geom;
                            m_preTileGeometries[bucketScreenId][wId] = geom;
                        }
                    }
                }
            });
}

void TilingHandler::slotScreensChanged(const QStringList& screenIds, bool isDesktopSwitch)
{
    // Invalidate any in-flight loadSettings property reply — this signal
    // carries a newer screen set (see m_screensSignalGeneration doc).
    ++m_screensSignalGeneration;

    const QSet<QString> newScreens(screenIds.begin(), screenIds.end());
    const QSet<QString> removed = m_managedScreens - newScreens;
    const QSet<QString> added = newScreens - m_managedScreens;

    // Windowed-fullscreen release is SPLIT around the managed-set write
    // below: membership is forgotten inline (so the geometry paths in this
    // pass stop taking the apply exemption) but the setFullScreen(false)
    // lands only after m_managedScreens holds the NEW set — on Wayland the
    // committed exit signal arrives a round-trip later, and the generic exit
    // branch it falls into consults m_managedScreens.
    QStringList windowedFsToRelease;
    // Pre-tile restores for windowed-fullscreen demotes, applied AFTER the
    // deferred release: the inline restore in the demote pass bails on the
    // still-requested fullscreen state, so applying there would silently
    // spend the one-shot wasTracked evidence and leave the window at the
    // column rect KWin's own fullscreen restore was seeded with.
    QHash<QString, QRectF> windowedFsPreTileRestore;

    // Stagger-epoch scope. A desktop switch invalidates EVERY in-flight
    // staggered apply (geometry computed for the old desktop must never land
    // in the new one) — that is the global epoch's purpose. A plain
    // managed-set change must NOT bump the global epoch: a mode toggle emits
    // its tile batch and its managedScreensChanged in the same burst, so the
    // blanket bump voided the toggle's OWN batch after its first
    // (synchronous) entry — every later, timer-staggered entry died on the
    // supersession guard and the screen sat half-tiled while the daemon
    // believed it tiled (recovered only by a float/unfloat forcing a fresh
    // batch). Only REMOVED screens' pending applies are genuinely stale;
    // screens that remain or join keep their in-flight batch — for a joiner
    // that batch IS the mode flip's own retile.
    if (isDesktopSwitch) {
        ++m_tileStaggerGeneration;
    } else {
        for (const QString& screenId : removed) {
            ++m_tileStaggerGenByScreen[screenId];
        }
    }

    const auto windows = KWin::effects->stackingOrder();

    if (!removed.isEmpty()) {
        if (isDesktopSwitch) {
            demoteWindowsForDesktopSwitch(removed, windows, windowedFsToRelease, windowedFsPreTileRestore);
        } else {
            untrackWindowsForDisabledScreens(removed, newScreens, windows, windowedFsToRelease);
        }
    }

    // The Mode discriminator reads m_scrollingScreens INTERSECTED with the
    // union, so moving the union can change it even though the scrolling set
    // is untouched — and this assignment does not go through
    // setScrollingScreens, which is where that invalidation normally lives.
    // Without this, a screen marked scrolling before it joined the union kept
    // every `Mode == "scrolling"` rule memoised as non-matching.
    // The union itself is a discriminator input too: keepFloatingAboveDefault
    // reads isManagedScreen to tell a tiling screen from a snapping one, so a
    // snapping<->tiling flip must re-reconcile the window layer after the
    // assignment (the sweeps the demote/untrack passes above run see the OLD
    // set). Hence the gate below keys on any managed-set change, not only on
    // the scrolling intersection moving.
    const QSet<QString> scrollingBefore = scrollingScreenIntersection();
    const bool managedChanged = m_managedScreens != newScreens;
    m_managedScreens = newScreens;
    // The deferred half of the windowed-fullscreen release: membership was
    // forgotten in the passes above; only now that m_managedScreens holds
    // the new set may the compositor state drop, so the committed exit
    // signal's arms evaluate against the world as it is.
    for (const QString& wid : std::as_const(windowedFsToRelease)) {
        releaseWindowedFullscreenState(wid);
        // Re-drive the maximize releases the demote and untrack passes above
        // could not pay. Those passes call them inline, while this window is
        // still fullscreen in both the requested and the committed sense, so
        // both releases hit their fullscreen guard and RETAIN — deliberately,
        // so that a later call on a non-fullscreen window does the real
        // restore. This is that later call, and it is the first point at which
        // it can succeed: the requested bit went false one line above. Without
        // it the window keeps KWin's maximize on a desktop or a screen the
        // strip no longer manages, with the ledger still recording the debt.
        // Both are no-ops for a non-member.
        unmaximizeMonocleWindow(wid);
        releaseColumnMaximized(wid, m_effect->findWindowByIdExact(wid));
    }
    // Now that the fullscreen state is dropped, land the pre-tile restores the
    // demote pass queued. Same bracket as the inline restore path: the
    // moveResize emits windowFrameGeometryChanged synchronously and must not
    // read as a virtual-screen crossing.
    for (auto it = windowedFsPreTileRestore.cbegin(); it != windowedFsPreTileRestore.cend(); ++it) {
        KWin::EffectWindow* w = m_effect->findWindowByIdExact(it.key());
        if (!w || w->isDeleted()) {
            continue;
        }
        const bool prevInApply = m_effect->m_daemonGate.inGeometryApply;
        m_effect->m_daemonGate.inGeometryApply = true;
        const auto geomGuard = qScopeGuard([this, prevInApply] {
            m_effect->m_daemonGate.inGeometryApply = prevInApply;
        });
        // Same maximize-clear the inline restore branch carries, and the same
        // ledger routing: membership and the bit move together, never one
        // without the other.
        if (m_columnMaximizedWindows.contains(it.key())) {
            releaseColumnMaximized(it.key(), w);
        } else if (KWin::Window* kw = w->window(); kw && kw->maximizeMode() != KWin::MaximizeRestore
                   && !kw->isRequestedFullScreen() && !kw->isFullScreen() && !w->isUserMove() && !w->isUserResize()) {
            // The fullscreen and gesture pair every sibling maximize write in
            // this tree carries: maximize() has no fullscreen conditional and
            // would moveResize a presenting surface down to its restore rect,
            // and mid-gesture it snaps the window under the user's pointer.
            //
            // Unlike releaseColumnMaximized, which skips on the same
            // conditions and RETAINS membership so a later arm pays the bit,
            // this is the non-member arm and holds no ledger, so a skip here
            // is permanent rather than deferred.
            applyMaximizeSuppressed(kw, KWin::MaximizeRestore);
        }
        m_effect->applyWindowGeometry(w, it.value().toRect(), /*allowDuringDrag=*/false,
                                      /*skipAnimation=*/false, PhosphorAnimation::ProfilePaths::WindowSnapOut);
        // Re-seed the tracked screen — same pairing rule as the inline
        // restore arm and requestDaemonPreTileRestore: the bracket
        // suppressed the detectors' own tracker write, and the restore can
        // land in a different virtual screen than the tiled rect.
        m_effect->m_trackedScreenPerWindow[w] = m_effect->getWindowScreenId(w);
    }
    if (managedChanged || scrollingScreenIntersection() != scrollingBefore) {
        m_effect->invalidateAllRuleCaches();
        m_effect->scheduleBorderSweep();
        // Same Mode-flip repaint bookend setScrollingScreens takes: a
        // `Mode Equals "scrolling"` SetOpacity rule resolves in the paint
        // path, and the border sweep rebuilds decorations rather than the
        // per-frame alpha of windows that have none.
        if (m_effect->m_shaderManager.hasOpacityRules() && KWin::effects) {
            KWin::effects->addRepaintFull();
        }
    }
    QSet<QString> completedDeferredRoutes;
    bool completedInitialQuery = false;
    if (m_initialScreenQueryPending) {
        m_initialScreenQueryPending = false;
        completedDeferredRoutes = completeDeferredWindowRoutes();
        completedInitialQuery = true;
    }

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
                             << "managed screens:" << m_managedScreens;
            // One pass over the windows with a set lookup rather than a pass
            // per added screen. The screen-major form re-resolved
            // getWindowScreenId and getWindowId for every (screen, window)
            // pair, and this whole block runs three such nested loops on the
            // desktop-switch path — at fifty screens by five hundred windows
            // that is 25k resolves apiece. The screen id is needed inside, so
            // it is resolved once and reused.
            for (KWin::EffectWindow* w : windows) {
                if (!w || w->isDeleted() || !m_effect->shouldHandleWindow(w) || !w->isOnCurrentDesktop()
                    || !w->isOnCurrentActivity()) {
                    continue;
                }
                const QString screenId = m_effect->getWindowScreenId(w);
                if (!added.contains(screenId)) {
                    continue;
                }
                const QString windowId = m_effect->getWindowId(w);
                if (m_savedNotifiedForDesktopReturn.contains(windowId) || m_notifiedWindows.contains(windowId)) {
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
                    notifyWindowAdded(w, /*knownFreeFloating=*/true);
                }
            }
            // Only remove entries for windows on screens we just processed.
            // In multi-screen setups, windows on OTHER screens (not in `added`)
            // must remain in the set for when their screen returns.
            //
            // ITS NARROW PURPOSE IS MINIMIZED WINDOWS. The catch-scan below
            // runs over every managed screen under these same filters and
            // removes the same entries, so for a non-minimized window this
            // loop is redundant with it. What the catch-scan additionally
            // skips is `w->isMinimized()`, deliberately — it re-ADDS windows
            // to tiling, and a minimized window must not be. This loop only
            // clears bookkeeping, so it has no such reason to skip them, and
            // dropping it would strand a minimized window's entry until its
            // screen next left and returned.
            //
            // One pass over the windows with a set lookup, not a pass per
            // added screen: `added` is already a QSet, and the screen-major
            // form re-resolved getWindowScreenId and getWindowId for every
            // (screen, window) pair.
            for (KWin::EffectWindow* w : windows) {
                if (!w || w->isDeleted() || !m_effect->shouldHandleWindow(w) || !w->isOnCurrentDesktop()
                    || !w->isOnCurrentActivity()) {
                    continue;
                }
                if (added.contains(m_effect->getWindowScreenId(w))) {
                    m_savedNotifiedForDesktopReturn.remove(m_effect->getWindowId(w));
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
            for (const QString& screenId : m_managedScreens) {
                for (KWin::EffectWindow* w : windows) {
                    if (!w || w->isDeleted() || !m_effect->shouldHandleWindow(w) || !w->isOnCurrentDesktop()
                        || !w->isOnCurrentActivity() || w->isMinimized()) {
                        continue;
                    }
                    if (m_effect->getWindowScreenId(w) != screenId) {
                        continue;
                    }
                    const QString windowId = m_effect->getWindowId(w);
                    if (m_savedNotifiedForDesktopReturn.contains(windowId)) {
                        // PARKED, not moved. The park set means exactly "the
                        // daemon STILL holds this window in this desktop's
                        // state" (see the resetNotified comment in
                        // tilinghandler.cpp), and a window that genuinely
                        // changed desktops was un-parked by
                        // cleanupAutotileTracking on its way out. So this is a
                        // RE-TRACK, never a notify: notifyWindowAdded would
                        // append the window to the arrived-at desktop's state
                        // a second time, which on scrolling screens leaks the
                        // other desktop's windows into this strip and destroys
                        // the column order.
                        //
                        // The `added`-keyed re-track loop above does the same
                        // thing, but `added` is empty on an identical-set
                        // switch — every desktop assigned the same mode, which
                        // is the ordinary multi-desktop scrolling setup — so
                        // this scan is the only pass those windows reach.
                        m_notifiedWindows.insert(windowId);
                        m_notifiedWindowScreens[windowId] = screenId;
                    } else if (!m_notifiedWindows.contains(windowId)) {
                        // Restore preserved pre-autotile geometry so float-restore
                        // returns to the original position, not the tiled frame from
                        // the source desktop. Shared with the windowDesktopsChanged
                        // arrival arm, the other re-add path that has to consume this
                        // stash; see restorePreTileForDesktopMove for the
                        // cross-screen decline.
                        restorePreTileForDesktopMove(windowId, screenId);
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
                    // Whether this scan re-announced the window or found it
                    // already tracked, it is now on the current desktop and
                    // notified, so any parked desktop-return entry for it is
                    // spent. Sweeping here (not only in the `added`-keyed loop
                    // above) is what keeps a park from outliving its purpose:
                    // the batch paths that park an off-desktop window are not
                    // all desktop switches, and `added` is empty on an
                    // identical-set switch, so this scan is the only place
                    // some entries are ever reached. A surviving entry would
                    // later make the re-track branch treat the window as
                    // already known to the daemon and skip the notify.
                    m_savedNotifiedForDesktopReturn.remove(windowId);
                }
            }

            // Refresh active border for the focused window on the returned-to
            // desktop. This also re-asserts borderless state: KWin silently
            // resets noBorder for windows on non-current desktops and the
            // daemon skips the retile on desktop return, but updateAllDecorations
            // runs DecorationManager::resyncWindow for every window — a
            // self-guarding, owner-kind-agnostic re-hide of exactly the
            // windows the manager owns whose decoration came back.
            m_effect->updateAllDecorations();
        } else {
            // Genuine user toggle — process all added screens as new.

            // Save pre-autotile geometry for ALL eligible windows (including minimized).
            // The window's current position IS the pre-autotile geometry we want to save.
            // Floating windows additionally back-fill the daemon's pre-tile
            // entry — non-destructively (overwrite=false; the inner comment
            // explains why an overflow float must not clobber a correct
            // existing entry).
            for (KWin::EffectWindow* w : windows) {
                if (!w || w->isDeleted() || !m_effect->shouldHandleWindow(w)) {
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
                saveAndRecordPreTileGeometry(windowId, screenId, w, w->frameGeometry());
                if (m_effect->isWindowFloating(windowId) && m_effect->m_daemonGate.serviceRegistered) {
                    // Correct for maximize/fullscreen, the same correction
                    // saveAndRecordPreTileGeometry applies internally: a
                    // floating-but-maximized window's frame is the full monitor, which
                    // must not be pushed as the free-float geometry.
                    QRectF frame = m_effect->freeGeometryForCapture(w, w->frameGeometry());
                    if (frame.width() <= 0 || frame.height() <= 0) {
                        // Off-screen/parked frame rejected by the capture
                        // chokepoint — nothing valid to store for THIS window.
                        continue;
                    }
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
            // saveAndRecordPreTileGeometry is called inside notifyWindowsAddedBatch.
            QList<KWin::EffectWindow*> batchWindows;
            batchWindows.reserve(windows.size());
            for (KWin::EffectWindow* window : windows) {
                // isDeleted mirrors the batch loop in wiring.cpp — a dying
                // window's getWindowId would re-pollute the scrubbed caches.
                if (window && !window->isDeleted()
                    && !completedDeferredRoutes.contains(m_effect->getWindowId(window))) {
                    batchWindows.append(window);
                }
            }
            notifyWindowsAddedBatch(batchWindows, added, /*resetNotified=*/true,
                                    /*enteringAutotile=*/true);
            qCInfo(lcEffect) << "Saved pre-autotile geometries for screens:" << added;

            // Async fetch of daemon's persisted pre-autotile geometries from previous session.
            // These may be more accurate than the current frame for windows that were resnapped
            // to zones in manual mode (current frame = zone position, daemon value = original).
            // Non-blocking: the old synchronous QDBus::Block call (500ms timeout) froze the
            // compositor thread, causing jerky first-retile animations since QElapsedTimer
            // kept advancing while no frames were rendered.
            fetchDaemonPreTileGeometries(added, newScreens);
        } // else (genuine user toggle)
    }

    if (completedInitialQuery) {
        // Guarded: snap handler dies before this one during effect teardown.
        if (SnapHandler* snap = m_effect->snapHandler()) {
            snap->retryVisibleMinimizeFloats();
        }
    }
    qCInfo(lcEffect) << "Managed screens changed:" << m_managedScreens;
}

} // namespace PlasmaZones
