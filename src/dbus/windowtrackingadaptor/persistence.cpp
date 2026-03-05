// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "internal.h"
#include "../../core/interfaces.h"
#include "../../core/layoutmanager.h"
#include "../../core/layout.h"
#include "../../core/zone.h"
#include "../../core/geometryutils.h"
#include "../../core/screenmanager.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/virtualdesktopmanager.h"
#include <QGuiApplication>
#include <QScreen>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QTimer>

namespace PlasmaZones {

using namespace WindowTrackingInternal;

void WindowTrackingAdaptor::storePreSnapGeometry(const QString& windowId, int x, int y, int width, int height)
{
    if (!validateWindowId(windowId, QStringLiteral("store pre-snap geometry"))) {
        return;
    }

    if (width <= 0 || height <= 0) {
        qCWarning(lcDbusWindow) << "Invalid geometry for pre-snap storage:"
                                << "width=" << width << "height=" << height;
        return;
    }

    // Delegate to service
    m_service->storePreSnapGeometry(windowId, QRect(x, y, width, height));
    qCDebug(lcDbusWindow) << "Stored pre-snap geometry for window" << windowId;
}

bool WindowTrackingAdaptor::getPreSnapGeometry(const QString& windowId, int& x, int& y, int& width, int& height)
{
    x = y = width = height = 0;

    if (!validateWindowId(windowId, QStringLiteral("get pre-snap geometry"))) {
        return false;
    }

    // Delegate to service
    auto geo = m_service->preSnapGeometry(windowId);
    if (!geo) {
        qCDebug(lcDbusWindow) << "No pre-snap geometry stored for window" << windowId;
        return false;
    }

    x = geo->x();
    y = geo->y();
    width = geo->width();
    height = geo->height();
    qCDebug(lcDbusWindow) << "Retrieved pre-snap geometry for window" << windowId << "at" << *geo;
    return true;
}

bool WindowTrackingAdaptor::hasPreSnapGeometry(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return false;
    }
    // Delegate to service
    return m_service->hasPreSnapGeometry(windowId);
}

void WindowTrackingAdaptor::clearPreSnapGeometry(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("clear pre-snap geometry"))) {
        return;
    }
    // Only log if there was something to clear
    bool hadGeometry = m_service->hasPreSnapGeometry(windowId);
    // Delegate to service
    m_service->clearPreSnapGeometry(windowId);
    if (hadGeometry) {
        qCDebug(lcDbusWindow) << "Cleared pre-snap geometry for window" << windowId;
    }
}

void WindowTrackingAdaptor::recordPreAutotileGeometry(const QString& windowId, const QString& screenName, int x, int y,
                                                      int width, int height)
{
    Q_UNUSED(screenName)
    if (windowId.isEmpty() || width <= 0 || height <= 0) {
        return;
    }
    QRect geo(x, y, width, height);
    m_service->storePreAutotileGeometry(windowId, geo);
    qCDebug(lcDbusWindow) << "Recorded pre-autotile geometry for" << windowId << geo;
}

void WindowTrackingAdaptor::clearPreAutotileGeometry(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    m_service->clearPreAutotileGeometry(windowId);
    qCDebug(lcDbusWindow) << "Cleared pre-autotile geometry for" << windowId;
}

QString WindowTrackingAdaptor::getPreAutotileGeometriesJson()
{
    return serializeGeometryMap(m_service->preAutotileGeometries());
}

bool WindowTrackingAdaptor::getValidatedPreSnapGeometry(const QString& windowId, int& x, int& y, int& width,
                                                        int& height)
{
    x = y = width = height = 0;

    if (windowId.isEmpty()) {
        return false;
    }

    // Delegate to service (checks pre-snap first, then pre-autotile).
    // Used by manual float (toggleFloatForWindow) and restoreWindowSize — NOT autotile float
    // (which uses applyGeometryForFloat with the opposite priority).
    auto geo = m_service->validatedPreSnapOrAutotileGeometry(windowId);
    if (!geo) {
        return false;
    }

    x = geo->x();
    y = geo->y();
    width = geo->width();
    height = geo->height();
    return true;
}

bool WindowTrackingAdaptor::isGeometryOnScreen(int x, int y, int width, int height) const
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    QRect geometry(x, y, width, height);
    for (QScreen* screen : Utils::allScreens()) {
        QRect intersection = screen->geometry().intersected(geometry);
        if (intersection.width() >= MinVisibleWidth && intersection.height() >= MinVisibleHeight) {
            return true;
        }
    }
    return false;
}

QString WindowTrackingAdaptor::getUpdatedWindowGeometries()
{
    // Delegate to service
    QHash<QString, QRect> geometries = m_service->updatedWindowGeometries();

    if (geometries.isEmpty()) {
        return QStringLiteral("[]");
    }

    QJsonArray windowGeometries;
    for (auto it = geometries.constBegin(); it != geometries.constEnd(); ++it) {
        QJsonObject windowObj;
        windowObj[QLatin1String("windowId")] = it.key();
        windowObj[QLatin1String("x")] = it.value().x();
        windowObj[QLatin1String("y")] = it.value().y();
        windowObj[QLatin1String("width")] = it.value().width();
        windowObj[QLatin1String("height")] = it.value().height();
        windowGeometries.append(windowObj);
    }

    qCDebug(lcDbusWindow) << "Returning updated geometries for" << windowGeometries.size() << "windows";
    return QString::fromUtf8(QJsonDocument(windowGeometries).toJson(QJsonDocument::Compact));
}

void WindowTrackingAdaptor::onLayoutChanged()
{
    // Delegate to service
    m_service->onLayoutChanged();

    // After layout becomes available, check if we have pending restores
    if (!m_service->pendingZoneAssignments().isEmpty()) {
        m_hasPendingRestores = true;
        qCDebug(lcDbusWindow) << "Layout available with" << m_service->pendingZoneAssignments().size()
                              << "pending restores - checking if panel geometry is ready";
        tryEmitPendingRestoresAvailable();
    }
}

void WindowTrackingAdaptor::onPanelGeometryReady()
{
    qCDebug(lcDbusWindow) << "Panel geometry ready - checking if pending restores available";
    tryEmitPendingRestoresAvailable();
}

void WindowTrackingAdaptor::tryEmitPendingRestoresAvailable()
{
    // Don't emit more than once per session
    if (m_pendingRestoresEmitted) {
        return;
    }

    // Check both conditions: layout has pending restores AND panel geometry is known
    if (!m_hasPendingRestores) {
        qCDebug(lcDbusWindow) << "Cannot emit pendingRestoresAvailable - no pending restores";
        return;
    }

    // Check if panel geometry is ready, or if ScreenManager doesn't exist (fallback)
    // If ScreenManager instance is null, we proceed anyway with a warning - this is
    // better than blocking window restoration indefinitely
    if (ScreenManager::instance() && !ScreenManager::isPanelGeometryReady()) {
        qCDebug(lcDbusWindow) << "Cannot emit pendingRestoresAvailable - panel geometry not ready yet";
        return;
    }

    // Both conditions met (or ScreenManager unavailable) - emit the signal
    m_pendingRestoresEmitted = true;
    if (!ScreenManager::instance()) {
        qCWarning(lcDbusWindow)
            << "Emitting pendingRestoresAvailable without ScreenManager - geometry may be incorrect";
    } else {
        qCInfo(lcDbusWindow) << "Panel geometry ready AND pending restores available - notifying effect";
    }
    Q_EMIT pendingRestoresAvailable();
}

Layout* WindowTrackingAdaptor::getValidatedActiveLayout(const QString& operation) const
{
    if (!m_layoutManager) {
        qCWarning(lcDbusWindow) << "No layout manager for" << operation;
        return nullptr;
    }

    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        qCDebug(lcDbusWindow) << "No active layout for" << operation;
        return nullptr;
    }

    return layout;
}

QString WindowTrackingAdaptor::detectScreenForZone(const QString& zoneId) const
{
    if (!m_layoutManager) {
        return QString();
    }
    auto zoneUuid = Utils::parseUuid(zoneId);
    if (!zoneUuid) {
        return QString();
    }

    int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;

    // Search per-screen layouts to find which screen's layout contains this zone.
    // This correctly handles multi-monitor setups where each screen has a different layout.
    for (QScreen* screen : Utils::allScreens()) {
        Layout* layout = m_layoutManager->layoutForScreen(Utils::screenIdentifier(screen), currentDesktop,
                                                          m_layoutManager->currentActivity());
        if (layout && layout->zoneById(*zoneUuid)) {
            return screen->name();
        }
    }

    // Fallback: zone not in any screen-specific layout, try geometry projection
    // with the active layout (single-monitor or unconfigured multi-monitor)
    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        return QString();
    }
    Zone* zone = layout->zoneById(*zoneUuid);
    if (!zone) {
        return QString();
    }
    for (QScreen* screen : Utils::allScreens()) {
        QRectF refGeom = GeometryUtils::effectiveScreenGeometry(layout, screen);
        QRectF normGeom = zone->normalizedGeometry(refGeom);
        QPoint zoneCenter(refGeom.x() + static_cast<int>(normGeom.center().x() * refGeom.width()),
                          refGeom.y() + static_cast<int>(normGeom.center().y() * refGeom.height()));
        if (screen->geometry().contains(zoneCenter)) {
            return screen->name();
        }
    }
    return QString();
}

QString WindowTrackingAdaptor::rectToJson(const QRect& rect) const
{
    return QString::fromUtf8(QJsonDocument(rectToJsonObject(rect)).toJson(QJsonDocument::Compact));
}

} // namespace PlasmaZones
