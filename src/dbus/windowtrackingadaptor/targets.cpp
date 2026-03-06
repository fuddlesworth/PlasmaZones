// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "../zonedetectionadaptor.h"
#include "../../core/interfaces.h"
#include "../../core/layoutmanager.h"
#include "../../core/layout.h"
#include "../../core/zone.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════
// Static Result Builders
// ═══════════════════════════════════════════════════════════════════════════

static QJsonObject moveResult(bool success, const QString& reason, const QString& zoneId, const QString& geometryJson,
                              const QString& sourceZoneId, const QString& screenName)
{
    QJsonObject obj;
    obj[QLatin1String("success")] = success;
    obj[QLatin1String("reason")] = reason;
    obj[QLatin1String("zoneId")] = zoneId;
    obj[QLatin1String("geometryJson")] = geometryJson;
    obj[QLatin1String("sourceZoneId")] = sourceZoneId;
    obj[QLatin1String("screenName")] = screenName;
    return obj;
}

static QJsonObject focusResult(bool success, const QString& reason, const QString& windowIdToActivate,
                               const QString& sourceZoneId, const QString& targetZoneId, const QString& screenName)
{
    QJsonObject obj;
    obj[QLatin1String("success")] = success;
    obj[QLatin1String("reason")] = reason;
    obj[QLatin1String("windowIdToActivate")] = windowIdToActivate;
    obj[QLatin1String("sourceZoneId")] = sourceZoneId;
    obj[QLatin1String("targetZoneId")] = targetZoneId;
    obj[QLatin1String("screenName")] = screenName;
    return obj;
}

static QJsonObject cycleResult(bool success, const QString& reason, const QString& windowIdToActivate,
                               const QString& zoneId, const QString& screenName)
{
    QJsonObject obj;
    obj[QLatin1String("success")] = success;
    obj[QLatin1String("reason")] = reason;
    obj[QLatin1String("windowIdToActivate")] = windowIdToActivate;
    obj[QLatin1String("zoneId")] = zoneId;
    obj[QLatin1String("screenName")] = screenName;
    return obj;
}

static QJsonObject swapResult(bool success, const QString& reason, const QString& windowId1, int x1, int y1, int w1,
                              int h1, const QString& zoneId1, const QString& windowId2, int x2, int y2, int w2, int h2,
                              const QString& zoneId2, const QString& screenName, const QString& sourceZoneId,
                              const QString& targetZoneId)
{
    QJsonObject obj;
    obj[QLatin1String("success")] = success;
    obj[QLatin1String("reason")] = reason;
    obj[QLatin1String("windowId1")] = windowId1;
    obj[QLatin1String("x1")] = x1;
    obj[QLatin1String("y1")] = y1;
    obj[QLatin1String("w1")] = w1;
    obj[QLatin1String("h1")] = h1;
    obj[QLatin1String("zoneId1")] = zoneId1;
    obj[QLatin1String("windowId2")] = windowId2;
    obj[QLatin1String("x2")] = x2;
    obj[QLatin1String("y2")] = y2;
    obj[QLatin1String("w2")] = w2;
    obj[QLatin1String("h2")] = h2;
    obj[QLatin1String("zoneId2")] = zoneId2;
    obj[QLatin1String("screenName")] = screenName;
    obj[QLatin1String("sourceZoneId")] = sourceZoneId;
    obj[QLatin1String("targetZoneId")] = targetZoneId;
    return obj;
}

// ═══════════════════════════════════════════════════════════════════════════
// Navigation Target Computation
// ═══════════════════════════════════════════════════════════════════════════

QString WindowTrackingAdaptor::getMoveTargetForWindow(const QString& windowId, const QString& direction,
                                                      const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getMoveTargetForWindow"))) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_window"), QString(), QString(),
                                                          QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }
    if (!validateDirection(direction, QStringLiteral("move"))) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_direction"), QString(),
                                                          QString(), QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }
    if (!m_zoneDetectionAdaptor) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_zone_detection"), QString(),
                                                          QString(), QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    QString targetZoneId;
    if (currentZoneId.isEmpty()) {
        targetZoneId = m_zoneDetectionAdaptor->getFirstZoneInDirection(direction, screenName);
        if (targetZoneId.isEmpty()) {
            Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_zones"), QString(), QString(),
                                      screenName);
            return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_zones"), QString(), QString(),
                                                              QString(), screenName))
                                         .toJson(QJsonDocument::Compact));
        }
    } else {
        targetZoneId = m_zoneDetectionAdaptor->getAdjacentZone(currentZoneId, direction);
        if (targetZoneId.isEmpty()) {
            Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_adjacent_zone"), currentZoneId,
                                      QString(), screenName);
            return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_adjacent_zone"), QString(),
                                                              QString(), currentZoneId, screenName))
                                         .toJson(QJsonDocument::Compact));
        }
    }

    QRect geo = m_service->zoneGeometry(targetZoneId, screenName);
    if (!geo.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("geometry_error"), currentZoneId,
                                  targetZoneId, screenName);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("geometry_error"), targetZoneId,
                                                          QString(), currentZoneId, screenName))
                                     .toJson(QJsonDocument::Compact));
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("move"), direction, currentZoneId, targetZoneId, screenName);
    return QString::fromUtf8(
        QJsonDocument(moveResult(true, QString(), targetZoneId, rectToJson(geo), currentZoneId, screenName))
            .toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getFocusTargetForWindow(const QString& windowId, const QString& direction,
                                                       const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getFocusTargetForWindow"))) {
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("invalid_window"), QString(),
                                                           QString(), QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }
    if (!validateDirection(direction, QStringLiteral("focus"))) {
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("invalid_direction"), QString(),
                                                           QString(), QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }
    if (!m_zoneDetectionAdaptor) {
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("no_zone_detection"), QString(),
                                                           QString(), QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("not_snapped"), QString(), QString(),
                                  screenName);
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("not_snapped"), QString(), QString(),
                                                           QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }

    QString targetZoneId = m_zoneDetectionAdaptor->getAdjacentZone(currentZoneId, direction);
    if (targetZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_adjacent_zone"), currentZoneId,
                                  QString(), screenName);
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("no_adjacent_zone"), QString(),
                                                           currentZoneId, QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }

    QStringList windowsInZone = m_service->windowsInZone(targetZoneId);
    if (windowsInZone.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window_in_zone"), currentZoneId,
                                  targetZoneId, screenName);
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("no_window_in_zone"), QString(),
                                                           currentZoneId, targetZoneId, screenName))
                                     .toJson(QJsonDocument::Compact));
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("focus"), direction, currentZoneId, targetZoneId, screenName);
    return QString::fromUtf8(
        QJsonDocument(focusResult(true, QString(), windowsInZone.first(), currentZoneId, targetZoneId, screenName))
            .toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getRestoreForWindow(const QString& windowId, const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getRestoreForWindow"))) {
        QJsonObject obj;
        obj[QLatin1String("success")] = false;
        obj[QLatin1String("found")] = false;
        obj[QLatin1String("reason")] = QLatin1String("invalid_window");
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }

    int x, y, w, h;
    bool found = getValidatedPreTileGeometry(windowId, x, y, w, h);
    QJsonObject obj;
    obj[QLatin1String("success")] = found && w > 0 && h > 0;
    obj[QLatin1String("found")] = found && w > 0 && h > 0;
    obj[QLatin1String("x")] = x;
    obj[QLatin1String("y")] = y;
    obj[QLatin1String("width")] = w;
    obj[QLatin1String("height")] = h;
    if (!found || w <= 0 || h <= 0) {
        obj[QLatin1String("reason")] = QLatin1String("not_snapped");
        Q_EMIT navigationFeedback(false, QStringLiteral("restore"), QStringLiteral("not_snapped"), QString(), QString(),
                                  screenName);
    } else {
        Q_EMIT navigationFeedback(true, QStringLiteral("restore"), QString(), QString(), QString(), screenName);
    }
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getCycleTargetForWindow(const QString& windowId, bool forward, const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getCycleTargetForWindow"))) {
        return QString::fromUtf8(
            QJsonDocument(cycleResult(false, QStringLiteral("invalid_window"), QString(), QString(), screenName))
                .toJson(QJsonDocument::Compact));
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("not_snapped"), QString(), QString(),
                                  screenName);
        return QString::fromUtf8(
            QJsonDocument(cycleResult(false, QStringLiteral("not_snapped"), QString(), QString(), screenName))
                .toJson(QJsonDocument::Compact));
    }

    QStringList windowsInZone = m_service->windowsInZone(currentZoneId);
    if (windowsInZone.size() < 2) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("single_window"), currentZoneId,
                                  currentZoneId, screenName);
        return QString::fromUtf8(
            QJsonDocument(cycleResult(false, QStringLiteral("single_window"), QString(), currentZoneId, screenName))
                .toJson(QJsonDocument::Compact));
    }

    int currentIndex = windowsInZone.indexOf(windowId);
    if (currentIndex < 0) {
        currentIndex = 0;
        for (int i = 0; i < windowsInZone.size(); ++i) {
            if (Utils::extractAppId(windowsInZone[i]) == Utils::extractAppId(windowId)) {
                currentIndex = i;
                break;
            }
        }
    }
    int nextIndex = forward ? (currentIndex + 1) % windowsInZone.size()
                            : (currentIndex - 1 + windowsInZone.size()) % windowsInZone.size();
    QString targetWindowId = windowsInZone.at(nextIndex);

    Q_EMIT navigationFeedback(true, QStringLiteral("cycle"), QString(), currentZoneId, currentZoneId, screenName);
    return QString::fromUtf8(QJsonDocument(cycleResult(true, QString(), targetWindowId, currentZoneId, screenName))
                                 .toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getSwapTargetForWindow(const QString& windowId, const QString& direction,
                                                      const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getSwapTargetForWindow"))) {
        return QString::fromUtf8(
            QJsonDocument(swapResult(false, QStringLiteral("invalid_window"), windowId, 0, 0, 0, 0, QString(),
                                     QString(), 0, 0, 0, 0, QString(), screenName, QString(), QString()))
                .toJson(QJsonDocument::Compact));
    }
    if (!validateDirection(direction, QStringLiteral("swap"))) {
        return QString::fromUtf8(
            QJsonDocument(swapResult(false, QStringLiteral("invalid_direction"), windowId, 0, 0, 0, 0, QString(),
                                     QString(), 0, 0, 0, 0, QString(), screenName, QString(), QString()))
                .toJson(QJsonDocument::Compact));
    }
    if (!m_zoneDetectionAdaptor) {
        return QString::fromUtf8(
            QJsonDocument(swapResult(false, QStringLiteral("no_zone_detection"), windowId, 0, 0, 0, 0, QString(),
                                     QString(), 0, 0, 0, 0, QString(), screenName, QString(), QString()))
                .toJson(QJsonDocument::Compact));
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("not_snapped"), QString(), QString(),
                                  screenName);
        return QString::fromUtf8(
            QJsonDocument(swapResult(false, QStringLiteral("not_snapped"), windowId, 0, 0, 0, 0, QString(), QString(),
                                     0, 0, 0, 0, QString(), screenName, QString(), QString()))
                .toJson(QJsonDocument::Compact));
    }

    QString targetZoneId = m_zoneDetectionAdaptor->getAdjacentZone(currentZoneId, direction);
    if (targetZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_adjacent_zone"), currentZoneId,
                                  QString(), screenName);
        return QString::fromUtf8(
            QJsonDocument(swapResult(false, QStringLiteral("no_adjacent_zone"), windowId, 0, 0, 0, 0, QString(),
                                     QString(), 0, 0, 0, 0, QString(), screenName, currentZoneId, QString()))
                .toJson(QJsonDocument::Compact));
    }

    QRect targetGeom = m_service->zoneGeometry(targetZoneId, screenName);
    QRect currentGeom = m_service->zoneGeometry(currentZoneId, screenName);
    if (!targetGeom.isValid() || !currentGeom.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("geometry_error"), currentZoneId,
                                  targetZoneId, screenName);
        return QString::fromUtf8(
            QJsonDocument(swapResult(false, QStringLiteral("geometry_error"), windowId, 0, 0, 0, 0, QString(),
                                     QString(), 0, 0, 0, 0, QString(), screenName, currentZoneId, targetZoneId))
                .toJson(QJsonDocument::Compact));
    }

    QStringList windowsInTargetZone = m_service->windowsInZone(targetZoneId);
    if (windowsInTargetZone.isEmpty()) {
        Q_EMIT navigationFeedback(true, QStringLiteral("swap"), direction, currentZoneId, targetZoneId, screenName);
        return QString::fromUtf8(
            QJsonDocument(swapResult(true, QStringLiteral("moved_to_empty"), windowId, targetGeom.x(), targetGeom.y(),
                                     targetGeom.width(), targetGeom.height(), targetZoneId, QString(), 0, 0, 0, 0,
                                     QString(), screenName, currentZoneId, targetZoneId))
                .toJson(QJsonDocument::Compact));
    }

    QString targetWindowId = windowsInTargetZone.first();
    Q_EMIT navigationFeedback(true, QStringLiteral("swap"), direction, currentZoneId, targetZoneId, screenName);
    return QString::fromUtf8(
        QJsonDocument(swapResult(true, QString(), windowId, targetGeom.x(), targetGeom.y(), targetGeom.width(),
                                 targetGeom.height(), targetZoneId, targetWindowId, currentGeom.x(), currentGeom.y(),
                                 currentGeom.width(), currentGeom.height(), currentZoneId, screenName, currentZoneId,
                                 targetZoneId))
            .toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getPushTargetForWindow(const QString& windowId, const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getPushTargetForWindow"))) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_window"), QString(), QString(),
                                                          QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }

    QString emptyZoneId = m_service->findEmptyZone(screenName);
    if (emptyZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("no_empty_zone"), QString(), QString(),
                                  screenName);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_empty_zone"), QString(), QString(),
                                                          QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }

    QRect geo = m_service->zoneGeometry(emptyZoneId, screenName);
    if (!geo.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"), QString(),
                                  emptyZoneId, screenName);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("geometry_error"), emptyZoneId,
                                                          QString(), QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("push"), QString(), QString(), emptyZoneId, screenName);
    return QString::fromUtf8(
        QJsonDocument(moveResult(true, QString(), emptyZoneId, rectToJson(geo), QString(), screenName))
            .toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getSnapToZoneByNumberTarget(const QString& windowId, int zoneNumber,
                                                           const QString& screenName)
{
    if (!validateWindowId(windowId, QStringLiteral("getSnapToZoneByNumberTarget"))) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_window"), QString(), QString(),
                                                          QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }

    if (zoneNumber < 1 || zoneNumber > 9) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(),
                                  QString(), screenName);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_zone_number"), QString(),
                                                          QString(), QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }

    auto* layout = m_layoutManager->resolveLayoutForScreen(Utils::screenIdForName(screenName));
    if (!layout) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_active_layout"), QString(),
                                  QString(), screenName);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_active_layout"), QString(),
                                                          QString(), QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }

    Zone* targetZone = nullptr;
    for (Zone* zone : layout->zones()) {
        if (zone->zoneNumber() == zoneNumber) {
            targetZone = zone;
            break;
        }
    }

    if (!targetZone) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("zone_not_found"), QString(), QString(),
                                  screenName);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("zone_not_found"), QString(), QString(),
                                                          QString(), screenName))
                                     .toJson(QJsonDocument::Compact));
    }

    QString zoneId = targetZone->id().toString();
    QRect geo = m_service->zoneGeometry(zoneId, screenName);
    if (!geo.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("geometry_error"), QString(), zoneId,
                                  screenName);
        return QString::fromUtf8(
            QJsonDocument(moveResult(false, QStringLiteral("geometry_error"), zoneId, QString(), QString(), screenName))
                .toJson(QJsonDocument::Compact));
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("snap"), QString(), QString(), zoneId, screenName);
    return QString::fromUtf8(QJsonDocument(moveResult(true, QString(), zoneId, rectToJson(geo), QString(), screenName))
                                 .toJson(QJsonDocument::Compact));
}

} // namespace PlasmaZones
