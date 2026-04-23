// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../SnapEngine.h"
#include <PhosphorZones/SnapState.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "core/logging.h"
#include "core/virtualdesktopmanager.h"
#include "core/windowtrackingservice.h"

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
        qCDebug(lcCore) << "setWindowFloat: no screen context for" << windowId << "- using empty screenId";
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
            qCDebug(lcCore) << "setWindowFloat: cannot unfloat" << windowId << "- no pre-float zone, keeping floating";
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
    // Look up unmanaged geometry from the engine (single store) and validate via WTS utility.
    std::optional<QRect> geo;
    if (hasUnmanagedGeometry(windowId)) {
        geo = m_windowTracker->validateGeometryForScreen(unmanagedGeometry(windowId), unmanagedScreen(windowId),
                                                         screenId);
    } else {
        // appId fallback: check by current class name
        const QString appId = m_windowTracker->currentAppIdFor(windowId);
        if (appId != windowId && hasUnmanagedGeometry(appId)) {
            geo =
                m_windowTracker->validateGeometryForScreen(unmanagedGeometry(appId), unmanagedScreen(appId), screenId);
        }
    }
    if (geo) {
        qCInfo(lcCore) << "applyGeometryForFloat:" << windowId << "restoring to" << *geo;
        Q_EMIT applyGeometryRequested(windowId, geo->x(), geo->y(), geo->width(), geo->height(), QString(), screenId,
                                      false);
        return true;
    }
    qCWarning(lcCore) << "applyGeometryForFloat:" << windowId << "no pre-tile geometry found";
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

} // namespace PlasmaZones
