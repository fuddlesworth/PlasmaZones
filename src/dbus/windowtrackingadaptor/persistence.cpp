// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "internal.h"
#include "../../core/interfaces.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "../../core/geometryutils.h"
#include <PhosphorScreens/Manager.h>
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/virtualdesktopmanager.h"
#include <QGuiApplication>
#include <QScreen>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <PhosphorScreens/ScreenIdentity.h>

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

PreTileGeometryList WindowTrackingAdaptor::getPreTileGeometries()
{
    PreTileGeometryList result;
    const auto& map = m_service->preTileGeometries();
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        PreTileGeometryEntry entry;
        // V3 added WindowRegistry-based appId lookup — use currentAppIdFor to
        // get the live class name (handles mid-session app renames).
        entry.appId = m_service->currentAppIdFor(it.key());
        entry.x = it.value().geometry.x();
        entry.y = it.value().geometry.y();
        entry.width = it.value().geometry.width();
        entry.height = it.value().geometry.height();
        if (!it.value().connectorName.isEmpty()) {
            entry.screenId = PhosphorIdentity::VirtualScreenId::isVirtual(it.value().connectorName)
                ? it.value().connectorName
                : Phosphor::Screens::ScreenIdentity::idForName(it.value().connectorName);
        }
        result.append(entry);
    }
    return result;
}

bool WindowTrackingAdaptor::getValidatedPreTileGeometry(const QString& windowId, int& x, int& y, int& width,
                                                        int& height)
{
    x = y = width = height = 0;

    if (windowId.isEmpty()) {
        return false;
    }

    QString screenId;
    if (m_service) {
        screenId = m_service->screenAssignments().value(windowId);
    }
    auto geo = m_service->validatedPreTileGeometry(windowId, screenId);
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
    // Use effective screen IDs (virtual if subdivided) for correct geometry checks
    auto* mgr = m_service->screenManager();
    if (mgr) {
        for (const QString& sid : mgr->effectiveScreenIds()) {
            QRect screenGeom = mgr->screenGeometry(sid);
            if (!screenGeom.isValid()) {
                continue;
            }
            QRect intersection = screenGeom.intersected(geometry);
            if (intersection.width() >= MinVisibleWidth && intersection.height() >= MinVisibleHeight) {
                return true;
            }
        }
    } else {
        // Fallback: no Phosphor::Screens::ScreenManager, use physical screens
        for (QScreen* screen : Utils::allScreens()) {
            QRect intersection = screen->geometry().intersected(geometry);
            if (intersection.width() >= MinVisibleWidth && intersection.height() >= MinVisibleHeight) {
                return true;
            }
        }
    }
    return false;
}

WindowGeometryList WindowTrackingAdaptor::getUpdatedWindowGeometries()
{
    QHash<QString, QRect> geometries = m_service->updatedWindowGeometries();
    WindowGeometryList result;
    result.reserve(geometries.size());
    for (auto it = geometries.constBegin(); it != geometries.constEnd(); ++it) {
        result.append(WindowGeometryEntry::fromRect(it.key(), it.value()));
    }
    qCDebug(lcDbusWindow) << "Returning updated geometries for" << result.size() << "windows";
    return result;
}

QString WindowTrackingAdaptor::getPendingRestoreGeometries()
{
    auto targets = m_service->pendingRestoreGeometries();
    if (targets.isEmpty()) {
        return QStringLiteral("{}");
    }

    QJsonObject result;
    for (auto it = targets.constBegin(); it != targets.constEnd(); ++it) {
        const auto& target = it.value();
        QJsonObject geoObj;
        geoObj[QLatin1String("x")] = target.geometry.x();
        geoObj[QLatin1String("y")] = target.geometry.y();
        geoObj[QLatin1String("width")] = target.geometry.width();
        geoObj[QLatin1String("height")] = target.geometry.height();
        geoObj[QLatin1String("screenId")] = target.screenId;
        result[it.key()] = geoObj;
    }

    qCDebug(lcDbusWindow) << "Returning pending restore geometries for" << result.size() << "apps";
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
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

    // Check if panel geometry is ready, or if Phosphor::Screens::ScreenManager doesn't exist (fallback)
    // If Phosphor::Screens::ScreenManager instance is null, we proceed anyway with a warning - this is
    // better than blocking window restoration indefinitely
    if (m_service->screenManager()
        && !(m_service->screenManager() && m_service->screenManager()->isPanelGeometryReady())) {
        qCDebug(lcDbusWindow) << "pendingRestoresAvailable: cannot emit, panel geometry not ready yet";
        return;
    }

    // Both conditions met (or Phosphor::Screens::ScreenManager unavailable) - emit the signal
    m_pendingRestoresEmitted = true;
    if (!m_service->screenManager()) {
        qCWarning(lcDbusWindow)
            << "pendingRestoresAvailable: no Phosphor::Screens::ScreenManager, geometry may be incorrect";
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
    // Use effective screen IDs (virtual + physical) so virtual screen layouts are searched too.
    const QStringList effectiveIds =
        (m_service->screenManager() ? m_service->screenManager()->effectiveScreenIds() : QStringList());
    for (const QString& sid : effectiveIds) {
        PhosphorZones::Layout* layout =
            m_layoutManager->layoutForScreen(sid, currentDesktop, m_layoutManager->currentActivity());
        if (layout && layout->zoneById(*zoneUuid)) {
            return sid;
        }
    }

    // Fallback: zone not in any screen-specific layout, try geometry projection
    // with the active layout (single-monitor or unconfigured multi-monitor)
    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        return QString();
    }
    PhosphorZones::Zone* zone = layout->zoneById(*zoneUuid);
    if (!zone) {
        return QString();
    }
    // Use effective screen IDs for virtual screen support
    auto* mgr = m_service->screenManager();
    if (mgr) {
        for (const QString& sid : mgr->effectiveScreenIds()) {
            QScreen* screen = mgr->physicalQScreenFor(sid);
            if (!screen) {
                continue;
            }
            QRect effGeom = mgr->screenGeometry(sid);
            if (!effGeom.isValid()) {
                continue;
            }
            // Use effGeom consistently for both normalization and containment
            // so the zone center projection matches the containment bounds.
            // Using different geometries causes mismatches when available
            // geometry differs from full geometry.
            QRectF effGeomF(effGeom);
            QRectF normGeom = zone->normalizedGeometry(effGeomF);
            QPoint zoneCenter(effGeom.x() + qRound(normGeom.center().x() * effGeom.width()),
                              effGeom.y() + qRound(normGeom.center().y() * effGeom.height()));
            if (effGeom.contains(zoneCenter)) {
                return sid;
            }
        }
    } else {
        for (QScreen* screen : Utils::allScreens()) {
            QRectF refGeom = GeometryUtils::effectiveScreenGeometry(m_service->screenManager(), layout, screen);
            QRectF normGeom = zone->normalizedGeometry(refGeom);
            QPoint zoneCenter(refGeom.x() + qRound(normGeom.center().x() * refGeom.width()),
                              refGeom.y() + qRound(normGeom.center().y() * refGeom.height()));
            if (screen->geometry().contains(zoneCenter)) {
                return Phosphor::Screens::ScreenIdentity::identifierFor(screen);
            }
        }
    }
    return QString();
}

} // namespace PlasmaZones
