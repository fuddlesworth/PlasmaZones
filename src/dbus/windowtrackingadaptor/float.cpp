// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "../../autotile/AutotileEngine.h"
#include "../../snap/SnapEngine.h"
#include "../../core/geometryutils.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace PlasmaZones {

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

QString WindowTrackingAdaptor::calculateUnfloatRestore(const QString& windowId, const QString& screenId)
{
    QJsonObject result;
    result[QLatin1String("found")] = false;

    if (windowId.isEmpty()) {
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }

    UnfloatResult unfloat = m_service->resolveUnfloatGeometry(windowId, screenId);
    if (!unfloat.found) {
        qCDebug(lcDbusWindow) << "calculateUnfloatRestore: no restore target for" << windowId;
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }

    result[QLatin1String("found")] = true;
    QJsonArray zoneArray;
    for (const QString& z : unfloat.zoneIds) {
        zoneArray.append(z);
    }
    result[QLatin1String("zoneIds")] = zoneArray;
    result[QLatin1String("x")] = unfloat.geometry.x();
    result[QLatin1String("y")] = unfloat.geometry.y();
    result[QLatin1String("width")] = unfloat.geometry.width();
    result[QLatin1String("height")] = unfloat.geometry.height();
    result[QLatin1String("screenName")] = unfloat.screenId;

    qCDebug(lcDbusWindow) << "calculateUnfloatRestore for" << windowId << "-> zones:" << unfloat.zoneIds
                          << "geo:" << unfloat.geometry;
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
    // Notify effect so it can update its local cache (use full windowId for per-instance tracking).
    // Use the window's tracked screen if available, otherwise fall back to last active screen.
    QString screen = m_service->screenAssignments().value(windowId, m_lastActiveScreenId);
    Q_EMIT windowFloatingChanged(windowId, floating, screen);

    // Emit unified state change
    QJsonObject stateObj;
    stateObj[QLatin1String("windowId")] = windowId;
    stateObj[QLatin1String("zoneId")] = m_service->zoneForWindow(windowId);
    stateObj[QLatin1String("screenId")] = screen;
    stateObj[QLatin1String("isFloating")] = floating;
    stateObj[QLatin1String("changeType")] = floating ? QStringLiteral("floated") : QStringLiteral("unfloated");
    Q_EMIT windowStateChanged(windowId, QString::fromUtf8(QJsonDocument(stateObj).toJson(QJsonDocument::Compact)));
}

QStringList WindowTrackingAdaptor::getFloatingWindows()
{
    // Delegate to service
    return m_service->floatingWindows();
}

void WindowTrackingAdaptor::toggleFloatForWindow(const QString& windowId, const QString& screenId)
{
    qCInfo(lcDbusWindow) << "toggleFloatForWindow: windowId=" << windowId << "screen=" << screenId;

    if (!validateWindowId(windowId, QStringLiteral("toggle float"))) {
        Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("invalid_window"), QString(),
                                  QString(), screenId);
        return;
    }

    // Route to the correct engine based on screen mode
    if (m_autotileEngine && m_autotileEngine->isActiveOnScreen(screenId)) {
        m_autotileEngine->toggleWindowFloat(windowId, screenId);
    } else if (m_snapEngine) {
        m_snapEngine->toggleWindowFloat(windowId, screenId);
    }
}

bool WindowTrackingAdaptor::applyGeometryForFloat(const QString& windowId, const QString& screenId)
{
    auto geo = m_service->validatedPreTileGeometry(windowId, screenId);
    if (geo) {
        qCInfo(lcDbusWindow) << "applyGeometryForFloat: windowId=" << windowId << "geo=" << *geo
                             << "screen=" << screenId;
        Q_EMIT applyGeometryRequested(windowId, GeometryUtils::rectToJson(*geo), QString(), screenId);
        return true;
    }

    qCInfo(lcDbusWindow) << "applyGeometryForFloat: no geometry found for" << windowId;
    return false;
}

void WindowTrackingAdaptor::clearFloatingStateForSnap(const QString& windowId, const QString& screenId)
{
    if (m_service->clearFloatingForSnap(windowId)) {
        qCDebug(lcDbusWindow) << "Window" << windowId << "was floating, clearing floating state for snap";
        Q_EMIT windowFloatingChanged(windowId, false, screenId);
    }
}

void WindowTrackingAdaptor::setWindowFloatingForScreen(const QString& windowId, const QString& screenId, bool floating)
{
    if (!validateWindowId(windowId, QStringLiteral("set float for screen"))) {
        return;
    }

    qCInfo(lcDbusWindow) << "setWindowFloatingForScreen: windowId=" << windowId << "floating=" << floating
                         << "screen=" << screenId;

    // Route to the correct engine based on screen mode
    if (m_autotileEngine && m_autotileEngine->isActiveOnScreen(screenId)) {
        m_autotileEngine->setWindowFloat(windowId, floating);
    } else if (m_snapEngine) {
        m_snapEngine->setWindowFloat(windowId, floating);
    }
}

} // namespace PlasmaZones
