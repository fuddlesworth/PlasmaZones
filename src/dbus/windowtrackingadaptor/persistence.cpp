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
#include <QTimer>

namespace PlasmaZones {

using namespace WindowTrackingInternal;

void WindowTrackingAdaptor::storePreTileGeometry(const QString& windowId, int x, int y, int width, int height,
                                                 const QString& screenId, bool overwrite)
{
    if (!validateWindowId(windowId, QStringLiteral("store pre-tile geometry"))) {
        return;
    }

    if (width <= 0 || height <= 0) {
        qCWarning(lcDbusWindow) << "Invalid geometry for pre-tile storage:"
                                << "width=" << width << "height=" << height;
        return;
    }

    m_service->storePreTileGeometry(windowId, QRect(x, y, width, height), screenId, overwrite);
    qCDebug(lcDbusWindow) << "Stored pre-tile geometry for" << windowId << "screen=" << screenId
                          << "overwrite=" << overwrite;
}

bool WindowTrackingAdaptor::getPreTileGeometry(const QString& windowId, int& x, int& y, int& width, int& height)
{
    x = y = width = height = 0;

    if (!validateWindowId(windowId, QStringLiteral("get pre-tile geometry"))) {
        return false;
    }

    auto geo = m_service->preTileGeometry(windowId);
    if (!geo) {
        return false;
    }

    x = geo->x();
    y = geo->y();
    width = geo->width();
    height = geo->height();
    return true;
}

bool WindowTrackingAdaptor::hasPreTileGeometry(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return false;
    }
    return m_service->hasPreTileGeometry(windowId);
}

void WindowTrackingAdaptor::clearPreTileGeometry(const QString& windowId)
{
    if (!validateWindowId(windowId, QStringLiteral("clear pre-tile geometry"))) {
        return;
    }
    bool hadGeometry = m_service->hasPreTileGeometry(windowId);
    m_service->clearPreTileGeometry(windowId);
    if (hadGeometry) {
        qCDebug(lcDbusWindow) << "Cleared pre-tile geometry for" << windowId;
    }
}

QString WindowTrackingAdaptor::getPreTileGeometriesJson()
{
    return serializeGeometryMap(m_service->preTileGeometries());
}

bool WindowTrackingAdaptor::getValidatedPreTileGeometry(const QString& windowId, int& x, int& y, int& width,
                                                        int& height)
{
    x = y = width = height = 0;

    if (windowId.isEmpty()) {
        return false;
    }

    auto geo = m_service->validatedPreTileGeometry(windowId);
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
    if (!m_service->pendingRestoreQueues().isEmpty()) {
        m_hasPendingRestores = true;
        qCDebug(lcDbusWindow) << "Layout available with" << m_service->pendingRestoreQueues().size()
                              << "pending restores, checking if panel geometry is ready";
        tryEmitPendingRestoresAvailable();
    }
}

void WindowTrackingAdaptor::onPanelGeometryReady()
{
    qCDebug(lcDbusWindow) << "Panel geometry: ready, checking if pending restores available";
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
        qCDebug(lcDbusWindow) << "pendingRestoresAvailable: cannot emit, no pending restores";
        return;
    }

    // Check if panel geometry is ready, or if ScreenManager doesn't exist (fallback)
    // If ScreenManager instance is null, we proceed anyway with a warning - this is
    // better than blocking window restoration indefinitely
    if (ScreenManager::instance() && !ScreenManager::isPanelGeometryReady()) {
        qCDebug(lcDbusWindow) << "pendingRestoresAvailable: cannot emit, panel geometry not ready yet";
        return;
    }

    // Both conditions met (or ScreenManager unavailable) - emit the signal
    m_pendingRestoresEmitted = true;
    if (!ScreenManager::instance()) {
        qCWarning(lcDbusWindow) << "pendingRestoresAvailable: no ScreenManager, geometry may be incorrect";
    } else {
        qCInfo(lcDbusWindow) << "Pending restores: panel geometry ready, notifying effect";
    }
    Q_EMIT pendingRestoresAvailable();
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
            return Utils::screenIdentifier(screen);
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
            return Utils::screenIdentifier(screen);
        }
    }
    return QString();
}

} // namespace PlasmaZones
