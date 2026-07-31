// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon — the KCM assignment-apply handler
//
// Split out of init_engines.cpp, which holds the engine wiring proper (the
// connect that installs this handler is still there). This is the apply pass
// itself: it classifies every effective screen by live mode, resnaps the
// snapping ones whose assignment changed, and announces the result per screen.
// ═══════════════════════════════════════════════════════════════════════════════

#include "daemon/daemon.h"
#include "helpers.h"

#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include <PhosphorContext/ContextResolver.h>
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "config/settings.h"
#include "core/platform/logging.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <utility>

namespace PlasmaZones {

void Daemon::handleAssignmentChangesApplied(const QStringList& changedScreenIdsList)
{
    const QSet<QString> changedScreenIds(changedScreenIdsList.begin(), changedScreenIdsList.end());
    if (!m_snapEngine || !m_windowTrackingAdaptor || !m_screenManager || !m_layoutManager)
        return;

    const QString activity = currentActivity();

    // Collect ENGINE-MANAGED screens (autotile AND scrolling — both
    // must be excluded from the snap resnap below) and per-screen OSD
    // data in one pass.
    //
    // Both keyed on the LIVE router mode rather than the raw
    // assignment id. updateEngineScreens leaves a context-disabled
    // scrolling or autotile assignment out of the engine set and the
    // router downgrades that screen to Snapping, so classifying by the
    // id alone excluded a screen snapping now owns from the resnap and
    // then announced a mode that is inert on it. Same live-mode gate
    // showOsdForScreens applies (osd.cpp).
    QSet<QString> engineManagedScreens;
    struct ScreenOsd
    {
        QString screenId;
        PhosphorZones::AssignmentEntry::Mode mode;
        QString algoId;
        DisabledReason disabled = DisabledReason::NotDisabled;
        int desktop = 0;
        // What the persisted assignment DECLARES, as opposed to the
        // live router mode: when the two disagree the router
        // downgraded the screen (its declared mode is disabled for
        // this context), and the OSD must explain that rather than
        // announce the fallback snapping layout.
        PhosphorZones::AssignmentEntry::Mode declaredMode = PhosphorZones::AssignmentEntry::Snapping;
    };
    QVector<ScreenOsd> osdEntries;
    const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
    for (const QString& screenId : effectiveIds) {
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int desktop = currentDesktopForScreen(screenId);
        const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
        const PhosphorZones::AssignmentEntry::Mode mode = currentModeFor(screenId);
        if (mode == PhosphorZones::AssignmentEntry::Autotile || mode == PhosphorZones::AssignmentEntry::Scrolling) {
            engineManagedScreens.insert(screenId);
        }
        // Only show OSD for screens that actually changed.
        if (!changedScreenIds.isEmpty() && !changedScreenIds.contains(screenId)) {
            continue;
        }
        // Disabled probe FIRST, mirroring showOsdForScreens: a
        // disabled context has no mode or layout worth announcing, so
        // the reason card has to win over every branch below.
        // handleForPersisted is the right axis because this apply has
        // already pinned the (desktop, activity) tuple while the
        // screen's mode stays live.
        DisabledReason why = DisabledReason::NotDisabled;
        if (m_contextResolver) {
            why = toDaemonDisabledReason(
                m_contextResolver->disabledReason(m_contextResolver->handleForPersisted(screenId, desktop, activity)));
        }
        // The algorithm display name still comes from the assignment
        // id: the router reports the MODE, not which algorithm the
        // cascade picked for the screen. Only asked for on an autotile
        // ID — extractAlgorithmId warns on anything else — and the
        // other modes never read the field.
        const QString algoId = PhosphorLayout::LayoutId::isAutotile(assignmentId)
            ? PhosphorLayout::LayoutId::extractAlgorithmId(assignmentId)
            : QString();
        const PhosphorZones::AssignmentEntry::Mode declared = PhosphorLayout::LayoutId::isScrolling(assignmentId)
            ? PhosphorZones::AssignmentEntry::Scrolling
            : (PhosphorLayout::LayoutId::isAutotile(assignmentId) ? PhosphorZones::AssignmentEntry::Autotile
                                                                  : PhosphorZones::AssignmentEntry::Snapping);
        osdEntries.append({screenId, mode, algoId, why, desktop, declared});
    }

    // Resnap only the snapping-mode screens whose assignments actually changed.
    // changedScreenIds scopes the resnap to avoid spurious geometry-set on
    // screens whose layout didn't change (prevents flicker on unrelated VS).
    // Restrict the resnap to each screen's CURRENT virtual desktop (the
    // filter compares every window against its own screen's desktop, so
    // multi-screen KCM applies stay correct). Without it, a per-desktop
    // assignment change resnaps windows parked on OTHER desktops into the
    // just-assigned layout's zones — the user sees one desktop's layout
    // leak onto every desktop. Mirrors resnapIfManualMode (navigation.cpp).
    // Arm exactly 1, co-located with the resnap call below: that call
    // produces exactly one resnap navigationFeedback (success or
    // "no_windows_to_resnap") whatever the screen mix, and the drain
    // in signals.cpp consumes one count per feedback. Arming a
    // per-screen count either under-armed (all-scrolling apply: 0,
    // letting the no-op resnap OSD fire over the mode card) or
    // over-armed (multi-screen: surplus parked until the watchdog).
    armResnapOsdSuppression(1);
    m_windowTrackingAdaptor->service()->populateResnapBufferForAllScreens(engineManagedScreens, changedScreenIds,
                                                                          currentDesktop());
    // m_snapAdaptor is assigned in initEnginesAndWiring and the
    // only path that nulls it is initCoreAdaptors' delete preamble,
    // which always runs immediately before that function re-assigns
    // it — so it is non-null whenever this handler can fire. stop()
    // only calls clearEngine() on it, and the m_snapEngine guard at
    // the top of this handler already refuses that state.
    m_snapAdaptor->resnapToNewLayout();
    // Restore snap-float positions for windows this KCM apply released
    // from autotile — the buffer-based resnap above cannot cover
    // floating windows (see the helper).
    emitPendingSnapFloatRestoresForResnapBuffer();

    // Show OSD for changed screens — use locked OSD variant when context is locked.
    // KCM Apply is an explicit user-driven layout assignment change, so the regular
    // preview OSDs gate on showOsdOnLayoutSwitch (matching cycle / quick-layout /
    // zone-selector-drop). The locked-context OSD bypasses the toggle by design — it
    // explains why a requested change had no visible effect on that screen, the same
    // pattern used for the mode-toggle locked feedback in connectShortcutSignals().
    const bool osdEnabled = m_settings && m_settings->showOsdOnLayoutSwitch();
    for (const auto& osd : std::as_const(osdEntries)) {
        // Disabled context → announce the reason, never a layout or a
        // mode. Bypasses showOsdOnLayoutSwitch for the same reason the
        // locked card below does: it explains why an explicit user
        // apply had no visible effect on this screen.
        if (osd.disabled != DisabledReason::NotDisabled) {
            showContextDisabledOsd(osd.screenId, osd.desktop, activity, osd.disabled);
            continue;
        }
        // Suppressed context → no active layout. Announce that, rather
        // than staying silent: this apply was an explicit user action, and
        // the sibling desktop-switch path (showOsdForScreens) shows the
        // same "not assigned" card for the same condition. The card
        // bypasses showOsdOnLayoutSwitch for the reason the disabled and
        // locked ones do.
        if (m_layoutManager->isContextActiveLayoutSuppressed(osd.screenId, osd.desktop, activity)) {
            showNotAssignedOsd(osd.screenId);
            continue;
        }
        // Downgraded: the assignment declares scrolling/autotile but
        // the router runs snapping because that mode is disabled for
        // this context. The resolver probe above asked the DOWNGRADED
        // mode and missed, so re-probe the declared mode's own lists
        // and explain — announcing the fallback snap layout here is
        // the misannouncement showOsdForScreens guards against.
        if (osd.declaredMode != osd.mode && osd.declaredMode != PhosphorZones::AssignmentEntry::Snapping) {
            const DisabledReason declaredWhy =
                contextDisabledReason(m_settings.get(), osd.declaredMode, osd.screenId, osd.desktop, activity);
            if (declaredWhy != DisabledReason::NotDisabled) {
                showContextDisabledOsd(osd.screenId, osd.desktop, activity, declaredWhy);
            } else {
                showNotAssignedOsd(osd.screenId);
            }
            continue;
        }
        if (isCurrentContextLockedForMode(osd.screenId, osd.mode)) {
            // Only a snapping screen has a layout the lock badge can
            // sit on. Autotile and scrolling screens would get a
            // fallback SNAP layout's zones drawn under the badge,
            // announcing zones the mode does not have, so they take
            // the text card instead.
            if (osd.mode == PhosphorZones::AssignmentEntry::Snapping) {
                showLockedPreviewOsd(osd.screenId);
            } else {
                showLockedOsd(osd.screenId);
            }
        } else if (!osdEnabled) {
            continue;
        } else if (osd.mode == PhosphorZones::AssignmentEntry::Scrolling) {
            // Scrolling has no layout entity to announce; the mode
            // switch itself is the OSD content (mirrors
            // cheatsheetModeString's three-way handling).
            showScrollingModeOsd(osd.screenId, OsdTrigger::LayoutSwitch);
        } else if (osd.mode == PhosphorZones::AssignmentEntry::Autotile) {
            if (!osd.algoId.isEmpty()) {
                // Resolve the algorithm's human-readable display
                // name via the registry instead of surfacing the
                // wire-format id (e.g. "bsp" → "Binary Split").
                // Mirrors the algorithm display-name resolution in the
                // per-screen OSD path (showOsdForScreens, osd.cpp).
                const auto* algo = m_algorithmRegistry ? m_algorithmRegistry->algorithm(osd.algoId) : nullptr;
                const QString displayName = algo ? algo->name() : osd.algoId;
                showLayoutOsdForAlgorithm(osd.algoId, displayName, osd.screenId);
            } else {
                // Autotile mode with no algorithm resolved. The three
                // autotile arms in this file each answer differently and
                // this one used to answer with silence and no trace, so a
                // user reporting "Apply did nothing" left nothing in the
                // journal to distinguish it from a dropped signal.
                qCWarning(lcDaemon) << "Assignment apply: autotile screen has no algorithm id, no OSD shown screen="
                                    << osd.screenId << "desktop=" << osd.desktop;
            }
        } else {
            // Per-output virtual desktops (#648): each screen resolves
            // its own desktop, captured with the entry above.
            PhosphorZones::Layout* layout = m_layoutManager->layoutForScreen(osd.screenId, osd.desktop, activity);
            if (layout)
                showLayoutOsd(layout, osd.screenId);
        }
    }

    // Refresh the active-assignment snapshot to what was just applied,
    // so a later rule edit diffs against reality (this path also runs
    // for legacy setAssignmentEntry-driven applies, which bypass
    // reconcileActiveAssignments and would otherwise leave it stale).
    diffActiveAssignments();
}

} // namespace PlasmaZones
