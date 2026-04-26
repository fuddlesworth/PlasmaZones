// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "snapenginelogging.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Float toggle / set
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::toggleWindowFloat(const QString& windowId, const QString& screenId)
{
    const bool currentlyFloating = m_snapState->isFloating(windowId);
    const bool currentlySnapped = m_snapState->isWindowSnapped(windowId);

    if (!currentlyFloating && !currentlySnapped) {
        return;
    }

    if (currentlyFloating) {
        if (!unfloatToZone(windowId, screenId)) {
            Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_pre_float_zone"), QString(),
                                      QString(), screenId);
            return;
        }
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("unfloated"), QString(), QString(),
                                  screenId);
    } else {
        m_windowTracker->unsnapForFloat(windowId);
        m_windowTracker->setWindowFloating(windowId, true);
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
        applyGeometryForFloat(windowId, screenId);
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"), QString(), QString(),
                                  screenId);
    }
}

void SnapEngine::setWindowFloat(const QString& windowId, bool shouldFloat)
{
    // IPlacementEngine::setWindowFloat has no screenId param, so resolve it:
    // 1. Try the window's tracked screen from WTS (most accurate)
    // 2. Fall back to m_lastActiveScreenId (from last windowFocused)
    // 3. Fall back to empty (unfloatToZone/applyGeometryForFloat handle gracefully)
    QString screenId = m_snapState->screenAssignments().value(windowId);
    if (screenId.isEmpty()) {
        screenId = m_lastActiveScreenId;
    }
    if (screenId.isEmpty()) {
        qCDebug(PhosphorSnapEngine::lcSnapEngine)
            << "setWindowFloat: no screen context for" << windowId << "- using empty screenId";
    }

    if (shouldFloat) {
        m_windowTracker->unsnapForFloat(windowId);
        m_windowTracker->setWindowFloating(windowId, true);
        Q_EMIT windowFloatingChanged(windowId, true, screenId);
        applyGeometryForFloat(windowId, screenId);
    } else {
        if (!unfloatToZone(windowId, screenId)) {
            // No pre-float zone to restore to — keep the window floating rather than
            // leaving it in a limbo state (not floating, not snapped to any zone).
            qCDebug(PhosphorSnapEngine::lcSnapEngine)
                << "setWindowFloat: cannot unfloat" << windowId << "- no pre-float zone, keeping floating";
            return;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Private helpers
// ═══════════════════════════════════════════════════════════════════════════════

bool SnapEngine::unfloatToZone(const QString& windowId, const QString& screenId)
{
    UnfloatResult unfloat = resolveUnfloatGeometry(windowId, screenId);
    if (!unfloat.found) {
        return false;
    }

    // Consume any saved snap-float entry — the window is being explicitly
    // zone-snapped, so it should not be restored as floating during a
    // future mode transition. This is a float-specific side effect that
    // doesn't belong in commitSnap's generic orchestration.
    restoreSnapFloating(windowId);

    // Commit the snap via the unified orchestration. User-initiated because
    // the user just toggled float off — they want this snap to update the
    // last-used-zone tracking. commitSnap handles clearing floating state
    // (and emits windowFloatingClearedForSnap which WTA relays as
    // windowFloatingChanged), plus the zone assignment.
    if (unfloat.zoneIds.size() > 1) {
        commitMultiZoneSnap(windowId, unfloat.zoneIds, unfloat.screenId, SnapIntent::UserInitiated);
    } else {
        commitSnap(windowId, unfloat.zoneIds.first(), unfloat.screenId, SnapIntent::UserInitiated);
    }

    Q_EMIT applyGeometryRequested(windowId, unfloat.geometry.x(), unfloat.geometry.y(), unfloat.geometry.width(),
                                  unfloat.geometry.height(), QString(), unfloat.screenId, false);
    return true;
}

bool SnapEngine::applyGeometryForFloat(const QString& windowId, const QString& screenId)
{
    auto geo = m_windowTracker->validatedUnmanagedGeometry(windowId, screenId);
    if (geo) {
        qCInfo(PhosphorSnapEngine::lcSnapEngine) << "applyGeometryForFloat:" << windowId << "restoring to" << *geo;
        Q_EMIT applyGeometryRequested(windowId, geo->x(), geo->y(), geo->width(), geo->height(), QString(), screenId,
                                      false);
        return true;
    }
    qCWarning(PhosphorSnapEngine::lcSnapEngine) << "applyGeometryForFloat:" << windowId << "no pre-tile geometry found";
    return false;
}

// SnapEngine::clearFloatingStateForSnap was removed — its two callers
// (windowOpened in lifecycle.cpp, unfloatToZone above) now go through
// WindowTrackingService::commitSnap which handles clearing floating
// state as step 1 of its orchestration. The D-Bus-visible behaviour is
// identical: commitSnap emits windowFloatingClearedForSnap, WTA relays
// it as windowFloatingChanged on the same D-Bus interface.

UnfloatResult SnapEngine::resolveUnfloatGeometry(const QString& windowId, const QString& fallbackScreen) const
{
    UnfloatResult result;

    QStringList zoneIds = m_windowTracker->preFloatZones(windowId);
    if (zoneIds.isEmpty()) {
        return result;
    }

    QString restoreScreen = m_windowTracker->preFloatScreen(windowId);
    if (!restoreScreen.isEmpty()) {
        restoreScreen = m_windowTracker->resolveEffectiveScreenId(restoreScreen);
        auto* mgr = m_windowTracker->screenManager();
        QScreen* physScreen = mgr ? mgr->physicalQScreenFor(restoreScreen)
                                  : Phosphor::Screens::ScreenIdentity::findByIdOrName(restoreScreen);
        if (!physScreen) {
            restoreScreen.clear();
        }
    }
    if (restoreScreen.isEmpty() && !fallbackScreen.isEmpty()) {
        restoreScreen = m_windowTracker->resolveEffectiveScreenId(fallbackScreen);
    }

    QRect geo = m_windowTracker->resolveZoneGeometry(zoneIds, restoreScreen);
    if (!geo.isValid()) {
        return result;
    }

    result.found = true;
    result.zoneIds = zoneIds;
    result.geometry = geo;
    result.screenId = restoreScreen;
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cross-engine handoff (see IPlacementEngine.h for contract)
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::handoffReceive(const HandoffContext& ctx)
{
    if (ctx.windowId.isEmpty() || ctx.toScreenId.isEmpty()) {
        return;
    }
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::handoffReceive:" << ctx.windowId << "to" << ctx.toScreenId
                                             << "from" << ctx.fromEngineId << "wasFloating=" << ctx.wasFloating;

    // Snap-engine policy on receive:
    //  - If the source had a snap zone (sourceZoneIds non-empty) AND this
    //    engine knows that zone in the destination screen's layout, restore
    //    the snap. Useful for same-mode cross-screen moves where the layouts
    //    happen to share a zone id.
    //  - Otherwise, treat as floating on the destination screen — preserves
    //    the user's geometry (sourceGeometry) and lets them drop-snap or use
    //    the float toggle to land in a zone explicitly. This intentionally
    //    matches the "drop on snap screen with no zone hit" behaviour rather
    //    than auto-snapping to a guessed zone.
    if (!ctx.sourceZoneIds.isEmpty()) {
        QRect zoneGeo = m_windowTracker->resolveZoneGeometry(ctx.sourceZoneIds, ctx.toScreenId);
        if (zoneGeo.isValid()) {
            if (ctx.sourceZoneIds.size() > 1) {
                commitMultiZoneSnap(ctx.windowId, ctx.sourceZoneIds, ctx.toScreenId, SnapIntent::UserInitiated);
            } else {
                commitSnap(ctx.windowId, ctx.sourceZoneIds.first(), ctx.toScreenId, SnapIntent::UserInitiated);
            }
            Q_EMIT applyGeometryRequested(ctx.windowId, zoneGeo.x(), zoneGeo.y(), zoneGeo.width(), zoneGeo.height(),
                                          QString(), ctx.toScreenId, false);
            return;
        }
    }

    // Floating placement: keep the snap state in sync (so future float toggles
    // route back to this engine via lastActiveScreenName's screenAssignment
    // lookup) but don't apply geometry here — the dropping caller already
    // committed the position.
    m_windowTracker->setWindowFloating(ctx.windowId, true);
    Q_EMIT windowFloatingChanged(ctx.windowId, true, ctx.toScreenId);
}

void SnapEngine::handoffRelease(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    qCInfo(PhosphorSnapEngine::lcSnapEngine) << "SnapEngine::handoffRelease:" << windowId;

    // Tracking-only release: drop snap-engine-private state directly without
    // routing through WindowTrackingService. WTS is the shared coordination
    // layer that BOTH engines react to via signals — calling its
    // unassignWindow / setWindowFloating from here would propagate state
    // changes back into the autotile engine that just took ownership,
    // causing it to drop the freshly-adopted window mid-handoff (the
    // drag-insert-from-snap-to-autotile bug).
    //
    // The daemon orchestrator owns the shared WTS-side floating-flag
    // transitions during a cross-engine handoff (see Daemon::
    // syncAutotileFloatStatePassive); this method only clears what's
    // private to the snap engine.
    //
    // pre-float / pre-tile captures are intentionally preserved — the
    // receive side may consult them via the HandoffContext for size
    // restoration on a future handoff back.
    if (m_snapState->isWindowSnapped(windowId)) {
        m_snapState->unassignWindow(windowId);
    }
    if (m_snapState->isFloating(windowId)) {
        m_snapState->setFloating(windowId, false);
    }
}

QString SnapEngine::screenForTrackedWindow(const QString& windowId) const
{
    // SnapState stores the screen for both snapped and (post-prior-fix)
    // floating windows that originated as snaps. Either populates the
    // screenAssignments map; an empty string means the snap engine doesn't
    // currently track this window at all.
    return m_snapState->screenAssignments().value(windowId);
}

} // namespace PlasmaZones
