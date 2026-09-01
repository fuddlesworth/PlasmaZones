// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon — tiling-family windowsReleased handling
//
// Split out of autotile.cpp, which holds the screen-set recompute and the
// autotile order/restore capture. This is the return leg: what happens to a
// window when a tiling-family engine gives it up.
// ═══════════════════════════════════════════════════════════════════════════════

#include "daemon/daemon.h"

#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorZones/AssignmentEntry.h>

#include "core/types/constants.h"
#include "core/platform/logging.h"

#include <QRect>

namespace PlasmaZones {

// Shared windowsReleased handler for BOTH tiling-family engines: a screen
// flipping back to SNAPPING restores each window's snap float bit and
// snapped-zone geometry from its placement record, regardless of which
// engine released it. The releasing engine's own mode-specific float
// markers are cleared through the interface. Windows whose released screen
// re-resolves to the OTHER tiling mode are skipped — they are being adopted
// by that engine in the same synchronous pass, not returning to snapping.
void Daemon::handleEngineWindowsReleased(PhosphorEngine::IPlacementEngine* releasingEngine,
                                         const QStringList& windowIds, const QSet<QString>& releasedScreenIds)
{
    if (!releasingEngine) {
        return;
    }
    const bool releasedFromScroll =
        (releasingEngine == static_cast<PhosphorEngine::IPlacementEngine*>(m_scrollEngine.get()));
    const PhosphorZones::AssignmentEntry::Mode otherTilingMode =
        releasedFromScroll ? PhosphorZones::AssignmentEntry::Autotile : PhosphorZones::AssignmentEntry::Scrolling;

    // NOTE: the batch is cleared ONCE per recompute at the top of
    // updateEngineScreens — both engines can release synchronously in the
    // same pass and this handler runs once per engine, so clearing here
    // would wipe the first engine's entries. Consumers still clear after
    // use, which covers staleness across recomputes.
    if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
        PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
        for (const QString& windowId : windowIds) {
            // Only process windows whose current WTS screen is one of the
            // screens being released. A window that moved to a different
            // screen (e.g., dragged from autotile VS to snap VS and resnapped)
            // is no longer on the releasing screen — its state on the current
            // screen must not be disturbed.
            const QString windowScreen = wts->screenForWindow(windowId);
            if (!windowScreen.isEmpty() && !releasedScreenIds.contains(windowScreen)) {
                // Window is on a different screen — do NOT touch its state.
                // It may be on another autotile screen (flag still valid) or
                // a snap screen (flag already cleared by assignWindowToZones).
                qCDebug(lcDaemon) << "windowsReleased: skipping" << windowId << "on screen" << windowScreen
                                  << "(not in released set)";
                continue;
            }
            if (!m_snapEngine)
                continue;
            // A screen flipping autotile→SCROLLING releases its windows
            // here BEFORE updateScrollingScreens adopts them (the two
            // setActiveScreens calls run in one synchronous pass). Those
            // windows return to the scroll engine, not to snapping —
            // queuing snap-float/zone restores for them would corrupt the
            // next genuine toggle's restore batch. Gate on the OTHER
            // engine's DERIVED live claim, not the raw cascade: a
            // context-disabled scrolling target is excluded from the
            // derived set even though the cascade says Scrolling, and its
            // windows DO return to snapping and need the restore.
            // Outside the recompute (prunes fire this handler too) the
            // derived snapshots are LAST-pass values, so prefer the live
            // claim there; mid-pass (latch held) the derived sets are the
            // fresh side and the live sets are the stale one.
            const bool otherEngineClaims = m_updateEngineScreensInProgress
                ? ((otherTilingMode == PhosphorZones::AssignmentEntry::Scrolling
                    && m_derivedScrollingScreens.contains(windowScreen))
                   || (otherTilingMode == PhosphorZones::AssignmentEntry::Autotile
                       && m_derivedAutotileScreens.contains(windowScreen)))
                : ((otherTilingMode == PhosphorZones::AssignmentEntry::Scrolling && m_scrollEngine
                    && m_scrollEngine->isActiveOnScreen(windowScreen))
                   || (otherTilingMode == PhosphorZones::AssignmentEntry::Autotile && m_autotileEngine
                       && m_autotileEngine->isActiveOnScreen(windowScreen)));
            const bool headedToOtherEngine = !windowScreen.isEmpty() && otherEngineClaims;
            if (headedToOtherEngine) {
                releasingEngine->clearModeSpecificFloatMarker(windowId);
                qCDebug(lcDaemon) << "windowsReleased: skipping" << windowId
                                  << "- screen entering the other tiling mode";
                continue;
            }
            // Clear autotile-originated floats (they don't persist into snap mode)
            bool wasAutotileFloated = releasingEngine->isModeSpecificFloated(windowId);
            if (wasAutotileFloated) {
                m_windowTrackingAdaptor->setWindowFloating(windowId, false);
            }
            releasingEngine->clearModeSpecificFloatMarker(windowId);
            // Restore the snap-mode float from the SINGLE source of truth — the
            // window's placement record (its snap slot), captured when the screen
            // last left snapping. No parallel saved-float set. Float state is set
            // immediately; geometry restore is deferred to the batched resnap
            // signal to avoid individual D-Bus signals queuing behind the resnap.
            // Exact record only: this is a LIVE mid-session window (uuids stable),
            // so a same-app sibling's record must not float/zone-restore it.
            const auto rec = wts->placementStore().peekExact(windowId);
            const PhosphorEngine::EngineSlot snapSlot =
                rec ? rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()) : PhosphorEngine::EngineSlot{};
            const bool snapFloat = snapSlot.state == PhosphorEngine::WindowPlacement::stateFloating();
            // A window SNAPPED in snapping mode, then floated in autotile, keeps its
            // snap-engine state — float is PER ENGINE. Such a window is excluded from
            // the captured autotile tile order (floated windows aren't ordered), so the
            // order-driven resnap and buildAutotileRestoreEntries never see it; without
            // this branch it falls through every restore path and keeps its autotile-
            // float geometry on return to snapping (the "still floated" bug). Gated on
            // wasAutotileFloated so order-driven (tiled, non-floated) windows — which the
            // order-resnap path already handles — are not double-snapped here.
            //
            // A MINIMIZED window qualifies too: its tiling representation is a
            // suspension float that never sets the mode-specific marker, so a
            // snap-SNAPPED window that was tiled and then minimized would
            // otherwise take no restore branch at all and unminimize at a stale
            // rect before anything resnaps it.
            const bool wasMinimized =
                wts->windowRegistry() && wts->windowRegistry()->minimizedState(windowId).value_or(false);
            const bool snapSnapped = (wasAutotileFloated || wasMinimized)
                && snapSlot.state == PhosphorEngine::WindowPlacement::stateSnapped() && !snapSlot.zoneIds.isEmpty();
            if (snapFloat) {
                qCInfo(lcDaemon) << "windowsReleased: restoring snap-float for" << windowId;
                m_windowTrackingAdaptor->setWindowFloating(windowId, true);
                const QString screen = wts->screenForWindow(windowId);
                // Per-screen rect only. anyFreeGeometry() would return a rect
                // remembered for a DIFFERENT monitor and teleport the window
                // there on multi-monitor; with no rect for this screen the
                // window simply stays where it is.
                QRect g = rec->freeGeometryFor(screen.isEmpty() ? rec->screenId : screen);
                // A prune-origin release (monitor unplug) resolves the free
                // geometry against the screen that just went away, so the rect
                // can sit entirely on a dead output. Restoring it would push the
                // window off every live screen; the compositor has already
                // relocated it somewhere visible, so let that stand.
                if (g.isValid() && !intersectsAnyLiveScreen(g)) {
                    qCInfo(lcDaemon) << "windowsReleased: dropping snap-float restore for" << windowId << "geo=" << g
                                     << "— target intersects no live screen";
                    g = QRect();
                }
                // intersectsAnyLiveScreen is NOT a screen-agreement check and
                // must not be read as one: it asks whether the rect is on ANY
                // live output, and a mis-keyed rect is — the wrong one. It
                // therefore fails open for exactly the case that matters here.
                // The key here is the LIVE screenForWindow, falling back to
                // the record's own screen only when that is empty, so it can
                // genuinely select another monitor's entry when the window has
                // moved. The ZoneAssignmentEntry below would then carry a
                // targetGeometry on one screen while targetScreenId names
                // another, which is what the explicit check exists to stop.
                if (const QString restoreScreen = screen.isEmpty() ? rec->screenId : screen; g.isValid()
                    && m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()
                    && !m_windowTrackingAdaptor->service()->geometryBelongsToScreen(g, restoreScreen)) {
                    qCInfo(lcDaemon) << "windowsReleased: dropping snap-float restore for" << windowId << "geo=" << g
                                     << "— does not lie on" << restoreScreen;
                    g = QRect();
                }
                if (g.isValid()) {
                    ZoneAssignmentEntry entry;
                    entry.windowId = windowId;
                    entry.targetZoneId = RestoreSentinel;
                    entry.targetGeometry = g;
                    // The drain gate (emitPendingSnapFloatRestoresForResnapBuffer's
                    // snapOwnsEntryScreen) holds entries whose screen resolves to a
                    // tiling-family mode — but an unscreened entry takes its
                    // permissive path unconditionally. Without this stamp every
                    // float entry was unscreened, so the hold never bit and the
                    // restore replayed into the live strip/grid (dolphin popping
                    // to its float rect mid-flip into scrolling).
                    entry.targetScreenId = screen.isEmpty() ? rec->screenId : screen;
                    m_pendingSnapFloatRestores.append(entry);
                }
            } else if (snapSnapped) {
                const QString screen = wts->screenForWindow(windowId);
                const QString restoreScreen = screen.isEmpty() ? rec->screenId : screen;
                const QRect geo = wts->resolveZoneGeometry(snapSlot.zoneIds, restoreScreen);
                if (geo.isValid()) {
                    qCInfo(lcDaemon) << "windowsReleased: restoring snap-zone for" << windowId
                                     << "zones=" << snapSlot.zoneIds << "screen=" << restoreScreen;
                    // Float is already cleared above for autotile-floated windows; this
                    // window returns to its snapped state, not floating. Multiple windows
                    // may legitimately share a zone, so no cross-window zone dedup here.
                    ZoneAssignmentEntry entry;
                    entry.windowId = windowId;
                    entry.targetZoneId = snapSlot.zoneIds.first();
                    entry.targetZoneIds = snapSlot.zoneIds;
                    entry.targetGeometry = geo;
                    entry.targetScreenId = restoreScreen;
                    // The durable record knows which desktop this snap
                    // belongs to; carry it so the batch commit doesn't
                    // re-stamp the window onto the current desktop.
                    entry.virtualDesktop = rec->virtualDesktop;
                    m_pendingSnapFloatRestores.append(entry);
                } else {
                    qCWarning(lcDaemon) << "windowsReleased: snap-zone restore for" << windowId
                                        << "failed — zone geometry unresolved for" << snapSlot.zoneIds;
                }
            } else if (releasedFromScroll && rec && snapSlot.state != PhosphorEngine::WindowPlacement::stateSnapped()) {
                // Tiled-in-strip window with NO snap state to return to: it was
                // free (never zone-snapped, never explicitly floated) when the
                // screen entered scrolling, so neither the snap-float branch nor
                // the buffer resnap will place it — without this it keeps its
                // strip rect, which can sit entirely off the viewport (the
                // strip extends past the screen). Restore the recorded free
                // geometry, the same source the autotile return trip taps via
                // buildAutotileRestoreEntries; autotile releases are excluded
                // here because that path already covers them. Float state is
                // deliberately NOT set: a free window in snapping mode carries
                // no explicit float bit.
                const QString screen = wts->screenForWindow(windowId);
                const QString restoreScreen = screen.isEmpty() ? rec->screenId : screen;
                QRect g = rec->freeGeometryFor(restoreScreen);
                // Same dead-output guard as the snap-float branch: a rect on no
                // live screen would push the window off every output.
                if (g.isValid() && !intersectsAnyLiveScreen(g)) {
                    qCInfo(lcDaemon) << "windowsReleased: dropping pre-scroll free-geometry restore for" << windowId
                                     << "geo=" << g << "— target intersects no live screen";
                    g = QRect();
                }
                if (g.isValid()) {
                    qCInfo(lcDaemon) << "windowsReleased: restoring pre-scroll free geometry for" << windowId
                                     << "geo=" << g << "screen=" << restoreScreen;
                    ZoneAssignmentEntry entry;
                    entry.windowId = windowId;
                    entry.targetZoneId = RestoreSentinel;
                    entry.targetGeometry = g;
                    // Drain-gate stamp — see the snap-float branch above.
                    entry.targetScreenId = restoreScreen;
                    m_pendingSnapFloatRestores.append(entry);
                }
            } else {
                qCDebug(lcDaemon) << "windowsReleased: no snap-float to restore for" << windowId
                                  << "wasAutotileFloated:" << wasAutotileFloated;
            }
        }
    }
    // Prune-origin releases (screenRemoved, desktop/activity prunes) run
    // OUTSIDE updateEngineScreens, so its tail drain never sees the batch
    // this handler just appended; nothing else on those paths consumes it
    // either, and a stale batch would be wiped by the next recompute's
    // clear (losing the restore) or replayed by a later unrelated
    // consumer. Mid-recompute (latch held) the tail drain owns it.
    //
    // This is the FULL drain (preserveZoneEntries defaults to false), and
    // the snap-ZONE half is best-effort by design, not by oversight: no
    // prune-origin path has a zone re-claim consumer (screenRemoved's zones
    // reference an output that no longer exists, and desktop/activity
    // prunes have no resnap of their own), so the zone entries are handed
    // to an in-flight resnapToNewLayout if one happens to be running and
    // dropped otherwise. Preserving them instead would leave a batch keyed
    // to a dead context for an unrelated later consumer to replay.
    if (!m_updateEngineScreensInProgress) {
        emitPendingSnapFloatRestoresForResnapBuffer();
    }
}

} // namespace PlasmaZones
