// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../SnapEngine.h"
#include "core/logging.h"
#include "core/virtualdesktopmanager.h"
#include "core/windowtrackingservice.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Float toggle / set
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::toggleWindowFloat(const QString& windowId, const QString& screenId)
{
    const bool currentlyFloating = m_windowTracker->isWindowFloating(windowId);
    const bool currentlySnapped = m_windowTracker->isWindowSnapped(windowId);

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
    // IEngineLifecycle::setWindowFloat has no screenId param, so resolve it:
    // 1. Try the window's tracked screen from WTS (most accurate)
    // 2. Fall back to m_lastActiveScreenId (from last windowFocused)
    // 3. Fall back to empty (unfloatToZone/applyGeometryForFloat handle gracefully)
    QString screenId = m_windowTracker->screenAssignments().value(windowId);
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
    UnfloatResult unfloat = m_windowTracker->resolveUnfloatGeometry(windowId, screenId);
    if (!unfloat.found) {
        return false;
    }

    m_windowTracker->setWindowFloating(windowId, false);
    m_windowTracker->clearPreFloatZone(windowId);
    // Consume any saved snap-float entry — the window is being explicitly zone-snapped,
    // so it should not be restored as floating during a future mode transition.
    m_windowTracker->restoreSnapFloating(windowId); // consumes and discards
    assignToZones(windowId, unfloat.zoneIds, unfloat.screenId);

    Q_EMIT windowFloatingChanged(windowId, false, unfloat.screenId);
    Q_EMIT applyGeometryRequested(windowId, unfloat.geometry.x(), unfloat.geometry.y(), unfloat.geometry.width(),
                                  unfloat.geometry.height(), QString(), unfloat.screenId, false);
    return true;
}

bool SnapEngine::applyGeometryForFloat(const QString& windowId, const QString& screenId)
{
    auto geo = m_windowTracker->validatedPreTileGeometry(windowId, screenId);
    if (geo) {
        qCInfo(lcCore) << "applyGeometryForFloat:" << windowId << "restoring to" << *geo;
        Q_EMIT applyGeometryRequested(windowId, geo->x(), geo->y(), geo->width(), geo->height(), QString(), screenId,
                                      false);
        return true;
    }
    qCWarning(lcCore) << "applyGeometryForFloat:" << windowId << "no pre-tile geometry found";
    return false;
}

void SnapEngine::clearFloatingStateForSnap(const QString& windowId, const QString& screenId)
{
    if (m_windowTracker->clearFloatingForSnap(windowId)) {
        Q_EMIT windowFloatingChanged(windowId, false, screenId);
    }
}

} // namespace PlasmaZones
