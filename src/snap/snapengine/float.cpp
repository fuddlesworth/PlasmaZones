// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../SnapEngine.h"
#include "core/geometryutils.h"
#include "core/logging.h"
#include "core/virtualdesktopmanager.h"
#include "core/windowtrackingservice.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Float toggle / set
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::toggleWindowFloat(const QString& windowId, const QString& screenName)
{
    const bool currentlyFloating = m_windowTracker->isWindowFloating(windowId);
    const bool currentlySnapped = m_windowTracker->isWindowSnapped(windowId);

    if (!currentlyFloating && !currentlySnapped) {
        return;
    }

    if (currentlyFloating) {
        if (!unfloatToZone(windowId, screenName)) {
            Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_pre_float_zone"), QString(),
                                      QString(), screenName);
            return;
        }
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("unfloated"), QString(), QString(),
                                  screenName);
    } else {
        m_windowTracker->unsnapForFloat(windowId);
        m_windowTracker->setWindowFloating(windowId, true);
        Q_EMIT windowFloatingChanged(windowId, true, screenName);
        applyGeometryForFloat(windowId, screenName);
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"), QString(), QString(),
                                  screenName);
    }
}

void SnapEngine::setWindowFloat(const QString& windowId, bool shouldFloat)
{
    // IWindowEngine::setWindowFloat has no screenName param, so resolve it:
    // 1. Try the window's tracked screen from WTS (most accurate)
    // 2. Fall back to m_lastActiveScreenName (from last windowFocused)
    // 3. Fall back to empty (unfloatToZone/applyGeometryForFloat handle gracefully)
    QString screenName = m_windowTracker->screenAssignments().value(windowId);
    if (screenName.isEmpty()) {
        screenName = m_lastActiveScreenName;
    }
    if (screenName.isEmpty()) {
        qCDebug(lcCore) << "setWindowFloat: no screen context for" << windowId << "- proceeding with empty screenName";
    }

    if (shouldFloat) {
        m_windowTracker->unsnapForFloat(windowId);
        m_windowTracker->setWindowFloating(windowId, true);
        Q_EMIT windowFloatingChanged(windowId, true, screenName);
        applyGeometryForFloat(windowId, screenName);
    } else {
        if (!unfloatToZone(windowId, screenName)) {
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

bool SnapEngine::unfloatToZone(const QString& windowId, const QString& screenName)
{
    UnfloatResult unfloat = m_windowTracker->resolveUnfloatGeometry(windowId, screenName);
    if (!unfloat.found) {
        return false;
    }

    m_windowTracker->setWindowFloating(windowId, false);
    m_windowTracker->clearPreFloatZone(windowId);
    assignToZones(windowId, unfloat.zoneIds, unfloat.screenName);

    Q_EMIT windowFloatingChanged(windowId, false, unfloat.screenName);
    Q_EMIT applyGeometryRequested(windowId, GeometryUtils::rectToJson(unfloat.geometry), QString(), unfloat.screenName);
    return true;
}

bool SnapEngine::applyGeometryForFloat(const QString& windowId, const QString& screenName)
{
    auto geo = m_windowTracker->validatedPreTileGeometry(windowId);
    if (geo) {
        qCInfo(lcCore) << "applyGeometryForFloat:" << windowId << "restoring to" << *geo;
        Q_EMIT applyGeometryRequested(windowId, GeometryUtils::rectToJson(*geo), QString(), screenName);
        return true;
    }
    qCWarning(lcCore) << "applyGeometryForFloat:" << windowId << "NO pre-tile geometry found";
    return false;
}

void SnapEngine::clearFloatingStateForSnap(const QString& windowId, const QString& screenName)
{
    if (m_windowTracker->clearFloatingForSnap(windowId)) {
        Q_EMIT windowFloatingChanged(windowId, false, screenName);
    }
}

} // namespace PlasmaZones
