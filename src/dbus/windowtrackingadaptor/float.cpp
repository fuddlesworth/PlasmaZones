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
    // Notify effect so it can update its local cache (use full windowId for per-instance tracking).
    // Use the window's tracked screen if available, otherwise fall back to last active screen.
    QString screen = m_service->screenAssignments().value(windowId, m_lastActiveScreenName);
    Q_EMIT windowFloatingChanged(windowId, floating, screen);
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

    // Route to autotile engine if this screen uses autotile mode
    if (m_isAutotileScreen && m_autotileToggleFloat && m_isAutotileScreen(screenName)) {
        qCInfo(lcDbusWindow) << "toggleFloatForWindow: routing to autotile engine for screen" << screenName;
        m_autotileToggleFloat(windowId, screenName);
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
        if (!unfloatToZone(windowId, screenName)) {
            Q_EMIT navigationFeedback(false, QStringLiteral("float"), QStringLiteral("no_pre_float_zone"), QString(),
                                      QString(), screenName);
            return;
        }
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("unfloated"), QString(), QString(),
                                  screenName);
    } else {
        // Float: unsnap and restore to original geometry.
        // Uses the same priority as autotile float (pre-snap → pre-autotile)
        // via applyGeometryForFloat(). Do NOT clear pre-snap geometry — it
        // must survive float/unfloat cycles so every float restores to the
        // same original position (matching autotile behavior).
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        Q_EMIT windowFloatingChanged(windowId, true, screenName);
        applyGeometryForFloat(windowId, screenName);
        Q_EMIT navigationFeedback(true, QStringLiteral("float"), QStringLiteral("floated"), QString(), QString(),
                                  screenName);
    }
}

bool WindowTrackingAdaptor::applyGeometryForFloat(const QString& windowId, const QString& screenName)
{
    auto geo = m_service->validatedPreTileGeometry(windowId);
    if (geo) {
        qCInfo(lcDbusWindow) << "applyGeometryForFloat:" << windowId << "geo:" << *geo << "screen:" << screenName;
        Q_EMIT applyGeometryRequested(windowId, rectToJson(*geo), QString(), screenName);
        return true;
    }

    qCInfo(lcDbusWindow) << "applyGeometryForFloat: NO geometry found for" << windowId;
    return false;
}

void WindowTrackingAdaptor::clearFloatingStateForSnap(const QString& windowId, const QString& screenName)
{
    if (m_service->isWindowFloating(windowId)) {
        qCDebug(lcDbusWindow) << "Window" << windowId << "was floating, clearing floating state for snap";
        m_service->setWindowFloating(windowId, false);
        m_service->clearPreFloatZone(windowId);
        Q_EMIT windowFloatingChanged(windowId, false, screenName);
    }
}

void WindowTrackingAdaptor::setWindowFloatingForScreen(const QString& windowId, const QString& screenName,
                                                       bool floating)
{
    if (!validateWindowId(windowId, QStringLiteral("set float for screen"))) {
        return;
    }

    // Route to autotile engine if this screen uses autotile mode
    if (m_isAutotileScreen && m_autotileSetFloat && m_isAutotileScreen(screenName)) {
        qCInfo(lcDbusWindow) << "setWindowFloatingForScreen: routing to autotile engine for" << windowId
                             << "floating=" << floating << "screen=" << screenName;
        m_autotileSetFloat(windowId, floating);
        return;
    }

    // Snap mode: manage zone state alongside floating flag.
    // Mirrors toggleFloatForWindow logic but directional (no toggle).
    qCInfo(lcDbusWindow) << "setWindowFloatingForScreen:" << windowId << "floating=" << floating
                         << "screen=" << screenName;

    if (floating) {
        // Unsnap from zone, preserve pre-float zone for unfloat restore
        m_service->unsnapForFloat(windowId);
        m_service->setWindowFloating(windowId, true);
        Q_EMIT windowFloatingChanged(windowId, true, screenName);
        applyGeometryForFloat(windowId, screenName);
    } else {
        if (!unfloatToZone(windowId, screenName)) {
            // No zone to restore to — just clear floating state
            m_service->setWindowFloating(windowId, false);
            Q_EMIT windowFloatingChanged(windowId, false, screenName);
        }
    }
}

bool WindowTrackingAdaptor::unfloatToZone(const QString& windowId, const QString& screenName)
{
    QStringList zoneIds = m_service->preFloatZones(windowId);
    if (zoneIds.isEmpty()) {
        qCDebug(lcDbusWindow) << "unfloatToZone: no pre-float zones for" << windowId;
        return false;
    }

    // Use saved pre-float screen; fall back to caller's screen if monitor was replugged.
    QString restoreScreen = m_service->preFloatScreen(windowId);
    if (!restoreScreen.isEmpty() && !Utils::findScreenByIdOrName(restoreScreen)) {
        qCInfo(lcDbusWindow) << "unfloatToZone: saved screen" << restoreScreen << "no longer exists, falling back to"
                             << screenName;
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

    if (!geo.isValid() || zoneIds.isEmpty()) {
        qCDebug(lcDbusWindow) << "unfloatToZone: invalid geometry for zones" << zoneIds;
        return false;
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

    Q_EMIT windowFloatingChanged(windowId, false, restoreScreen);
    Q_EMIT applyGeometryRequested(windowId, rectToJson(geo), QString(), restoreScreen);
    return true;
}

} // namespace PlasmaZones
