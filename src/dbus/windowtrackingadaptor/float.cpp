// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "internal.h"
#include "../../core/interfaces.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/virtualdesktopmanager.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace PlasmaZones {

using namespace WindowTrackingInternal;

void WindowTrackingAdaptor::windowUnsnappedForFloat(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("prepare float"))) {
        return;
    }

    QString previousZoneId = m_service->zoneForWindow(windowId);
    if (previousZoneId.isEmpty()) {
        // Window was not snapped - no-op
        qCDebug(lcDbusWindow) << "windowUnsnappedForFloat: window not in any zone:" << windowId;
        return;
    }

    // Delegate to service
    m_service->unsnapForFloat(windowId);

    qCInfo(lcDbusWindow) << "Window" << windowId << "unsnapped for float from zone" << previousZoneId;
}

bool WindowTrackingAdaptor::getPreFloatZone(const QString& windowId, QString& zoneIdOut)
{
    if (windowId.isEmpty()) {
        qCDebug(lcDbusWindow) << "getPreFloatZone: empty windowId";
        zoneIdOut.clear();
        return false;
    }
    // Delegate to service
    zoneIdOut = m_service->preFloatZone(windowId);
    qCDebug(lcDbusWindow) << "getPreFloatZone for" << windowId << "-> found:" << !zoneIdOut.isEmpty()
                          << "zone:" << zoneIdOut;
    return !zoneIdOut.isEmpty();
}

void WindowTrackingAdaptor::clearPreFloatZone(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    // Only log if there was something to clear
    bool hadPreFloatZone = !m_service->preFloatZone(windowId).isEmpty();
    // Delegate to service
    m_service->clearPreFloatZone(windowId);
    if (hadPreFloatZone) {
        qCDebug(lcDbusWindow) << "Cleared pre-float zone for window" << windowId;
    }
}

QString WindowTrackingAdaptor::calculateUnfloatRestore(const QString& windowId, const QString& screenName)
{
    QJsonObject result;
    result[QLatin1String("found")] = false;

    if (windowId.isEmpty()) {
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }

    QStringList zoneIds = m_service->preFloatZones(windowId);
    if (zoneIds.isEmpty()) {
        qCDebug(lcDbusWindow) << "calculateUnfloatRestore: no pre-float zones for" << windowId;
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }

    // Use the saved pre-float screen (where the window was snapped before floating)
    // rather than the current window screen, since floating may have moved it cross-monitor.
    // If the saved screen name no longer exists (monitor replugged under a different
    // connector name), fall back to the caller's screen so unfloat still works.
    QString restoreScreen = m_service->preFloatScreen(windowId);
    if (!restoreScreen.isEmpty() && !Utils::findScreenByIdOrName(restoreScreen)) {
        qCInfo(lcDbusWindow) << "calculateUnfloatRestore: saved screen" << restoreScreen
                             << "no longer exists, falling back to" << screenName;
        restoreScreen.clear();
    }
    if (restoreScreen.isEmpty()) {
        restoreScreen = screenName;
    }

    // Calculate geometry (combined for multi-zone)
    QRect geo;
    if (zoneIds.size() > 1) {
        geo = m_service->multiZoneGeometry(zoneIds, restoreScreen);
    } else {
        geo = m_service->zoneGeometry(zoneIds.first(), restoreScreen);
    }

    if (!geo.isValid()) {
        qCDebug(lcDbusWindow) << "calculateUnfloatRestore: invalid geometry for zones" << zoneIds;
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }

    result[QLatin1String("found")] = true;
    QJsonArray zoneArray;
    for (const QString& z : zoneIds) {
        zoneArray.append(z);
    }
    result[QLatin1String("zoneIds")] = zoneArray;
    result[QLatin1String("x")] = geo.x();
    result[QLatin1String("y")] = geo.y();
    result[QLatin1String("width")] = geo.width();
    result[QLatin1String("height")] = geo.height();
    result[QLatin1String("screenName")] = restoreScreen;

    qCDebug(lcDbusWindow) << "calculateUnfloatRestore for" << windowId << "-> zones:" << zoneIds << "geo:" << geo;
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

bool WindowTrackingAdaptor::isWindowFloating(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return false;
    }
    // Delegate to service
    return m_service->isWindowFloating(windowId);
}

bool WindowTrackingAdaptor::queryWindowFloating(const QString& windowId)
{
    return isWindowFloating(windowId);
}

void WindowTrackingAdaptor::setWindowFloating(const QString& windowId, bool floating)
{
    if (!validateWindowId(windowId, QStringLiteral("set float state"))) {
        return;
    }
    // Delegate to service
    m_service->setWindowFloating(windowId, floating);
    qCInfo(lcDbusWindow) << "Window" << windowId << "is now" << (floating ? "floating" : "not floating");
    // Notify effect so it can update its local cache (use full windowId for per-instance tracking)
    Q_EMIT windowFloatingChanged(windowId, floating);
}

QStringList WindowTrackingAdaptor::getFloatingWindows()
{
    // Delegate to service
    return m_service->floatingWindows();
}

void WindowTrackingAdaptor::toggleFloatForWindow(const QString& windowId, const QString& screenName)
{
    qCInfo(lcDbusWindow) << "toggleFloatForWindow called: windowId=" << windowId << "screen=" << screenName;

    if (!validateWindowId(windowId, QStringLiteral("toggle float"))) {
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("invalid_window"), QString(),
                                  QString(), screenName);
        return;
    }

    const bool currentlyFloating = m_service->isWindowFloating(windowId);
    const bool currentlySnapped = m_service->isWindowSnapped(windowId);

    // If the window is neither snapped nor floating, it was never managed — nothing to toggle.
    if (!currentlyFloating && !currentlySnapped) {
        qCInfo(lcDbusWindow) << "toggleFloatForWindow: window" << windowId << "is not snapped or floating, ignoring";
        return;
    }

    if (currentlyFloating) {
        // Unfloat: restore to pre-float zone
        QString restoreJson = calculateUnfloatRestore(windowId, screenName);
        QJsonDocument doc = QJsonDocument::fromJson(restoreJson.toUtf8());
        if (!doc.isObject()) {
            Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_pre_float_zone"), QString(),
                                      QString(), screenName);
            return;
        }
        QJsonObject obj = doc.object();
        if (!obj.value(QLatin1String("found")).toBool(false)) {
            Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_pre_float_zone"), QString(),
                                      QString(), screenName);
            return;
        }
        QStringList zoneIds;
        for (const QJsonValue& v : obj.value(QLatin1String("zoneIds")).toArray()) {
            zoneIds.append(v.toString());
        }
        QRect geo(obj.value(QLatin1String("x")).toInt(), obj.value(QLatin1String("y")).toInt(),
                  obj.value(QLatin1String("width")).toInt(), obj.value(QLatin1String("height")).toInt());
        QString restoreScreen = obj.value(QLatin1String("screenName")).toString();
        if (zoneIds.isEmpty() || !geo.isValid()) {
            Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_pre_float_zone"), QString(),
                                      QString(), screenName);
            return;
        }
        m_service->setWindowFloating(windowId, false);
        m_service->clearPreFloatZone(windowId);

        // Re-assign window to zone(s) directly via service (handles multi-zone correctly).
        // The effect receives applyGeometryRequested with empty zoneId so it only applies geometry
        // without calling windowSnapped (which would lose multi-zone assignment).
        int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
        if (zoneIds.size() > 1) {
            m_service->assignWindowToZones(windowId, zoneIds, restoreScreen, currentDesktop);
        } else {
            m_service->assignWindowToZone(windowId, zoneIds.first(), restoreScreen, currentDesktop);
        }

        Q_EMIT windowFloatingChanged(windowId, false);
        Q_EMIT applyGeometryRequested(windowId, rectToJson(geo), QString(), restoreScreen);
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("unfloated"), QString(), QString(),
                                  restoreScreen);
    } else {
        // Float (manual path): restore to pre-snap geometry (prefers pre-snap over
        // pre-autotile — opposite of applyGeometryForFloat which is the autotile path).
        int x, y, w, h;
        if (!getValidatedPreSnapGeometry(windowId, x, y, w, h) || w <= 0 || h <= 0) {
            // Snapped but no pre-snap geometry — still mark as floating, no geometry restore
            m_service->unsnapForFloat(windowId);
            m_service->setWindowFloating(windowId, true);
            Q_EMIT windowFloatingChanged(windowId, true);
            Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"), QString(), QString(),
                                      screenName);
            return;
        }
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        m_service->clearPreSnapGeometry(windowId);
        // NOTE: Do NOT clear pre-autotile geometry — see applyGeometryForFloat comment.
        QRect geo(x, y, w, h);
        Q_EMIT applyGeometryRequested(windowId, rectToJson(geo), QString(), screenName);
        Q_EMIT windowFloatingChanged(windowId, true);
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"), QString(), QString(),
                                  screenName);
    }
}

bool WindowTrackingAdaptor::applyGeometryForFloat(const QString& windowId, const QString& screenName)
{
    // Floating geometry is shared between autotile and snapping modes.
    // Prefer pre-snap geometry (the window's original position before any zone
    // snapping) so that floating in autotile restores to the same position as
    // floating in snapping mode. Fall back to pre-autotile geometry for windows
    // that were opened during autotile (no pre-snap geometry exists).
    auto snapGeo = m_service->validatedPreSnapGeometry(windowId);
    if (snapGeo) {
        QRect geo = *snapGeo;
        Q_EMIT applyGeometryRequested(windowId, rectToJson(geo), QString(), screenName);
        qCDebug(lcDbusWindow) << "Applied pre-snap geometry for float:" << windowId << geo;
        return true;
    }

    auto autotileGeo = m_service->validatedPreAutotileGeometry(windowId);
    if (autotileGeo) {
        QRect geo = *autotileGeo;
        Q_EMIT applyGeometryRequested(windowId, rectToJson(geo), QString(), screenName);
        qCDebug(lcDbusWindow) << "Applied pre-autotile geometry for float:" << windowId << geo;
        return true;
    }

    qCDebug(lcDbusWindow) << "No geometry found for float:" << windowId;
    return false;
}

void WindowTrackingAdaptor::clearFloatingStateForSnap(const QString& windowId)
{
    if (m_service->isWindowFloating(windowId)) {
        qCDebug(lcDbusWindow) << "Window" << windowId << "was floating, clearing floating state for snap";
        m_service->setWindowFloating(windowId, false);
        m_service->clearPreFloatZone(windowId);
        Q_EMIT windowFloatingChanged(windowId, false);
    }
}

} // namespace PlasmaZones
