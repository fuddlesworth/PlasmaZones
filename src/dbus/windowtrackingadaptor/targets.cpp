// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "../zonedetectionadaptor.h"
#include "../../core/interfaces.h"
#include "../../core/layoutmanager.h"
#include "../../core/layout.h"
#include "../../core/zone.h"
#include "../../core/logging.h"
#include "../../core/screenmanager.h"
#include "../../core/utils.h"
#include "../../core/geometryutils.h"
#include "../../core/virtualscreen.h"
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════
// Static Result Builders
// ═══════════════════════════════════════════════════════════════════════════

static QJsonObject moveResult(bool success, const QString& reason, const QString& zoneId, const QString& geometryJson,
                              const QString& sourceZoneId, const QString& screenId)
{
    QJsonObject obj;
    obj[QLatin1String("success")] = success;
    obj[QLatin1String("reason")] = reason;
    obj[QLatin1String("zoneId")] = zoneId;
    obj[QLatin1String("geometryJson")] = geometryJson;
    obj[QLatin1String("sourceZoneId")] = sourceZoneId;
    obj[QLatin1String("screenId")] = screenId;
    return obj;
}

static QJsonObject focusResult(bool success, const QString& reason, const QString& windowIdToActivate,
                               const QString& sourceZoneId, const QString& targetZoneId, const QString& screenId)
{
    QJsonObject obj;
    obj[QLatin1String("success")] = success;
    obj[QLatin1String("reason")] = reason;
    obj[QLatin1String("windowIdToActivate")] = windowIdToActivate;
    obj[QLatin1String("sourceZoneId")] = sourceZoneId;
    obj[QLatin1String("targetZoneId")] = targetZoneId;
    obj[QLatin1String("screenId")] = screenId;
    return obj;
}

static QJsonObject cycleResult(bool success, const QString& reason, const QString& windowIdToActivate,
                               const QString& zoneId, const QString& screenId)
{
    QJsonObject obj;
    obj[QLatin1String("success")] = success;
    obj[QLatin1String("reason")] = reason;
    obj[QLatin1String("windowIdToActivate")] = windowIdToActivate;
    obj[QLatin1String("zoneId")] = zoneId;
    obj[QLatin1String("screenId")] = screenId;
    return obj;
}

static QJsonObject swapResult(bool success, const QString& reason, const QString& windowId1, int x1, int y1, int w1,
                              int h1, const QString& zoneId1, const QString& windowId2, int x2, int y2, int w2, int h2,
                              const QString& zoneId2, const QString& screenId, const QString& sourceZoneId,
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
    obj[QLatin1String("screenId")] = screenId;
    obj[QLatin1String("sourceZoneId")] = sourceZoneId;
    obj[QLatin1String("targetZoneId")] = targetZoneId;
    return obj;
}

// ═══════════════════════════════════════════════════════════════════════════
// Stored Screen Validation
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Validate a stored screen ID, including virtual screen existence check.
 *
 * For virtual screen IDs, verifies both that the backing physical screen
 * is still connected AND that the virtual screen ID is still in the
 * effective screen list (guards against stale IDs after config removal).
 * For physical screen IDs, verifies the screen is connected.
 *
 * @return true if the stored screen ID is still valid
 */
static bool isStoredScreenValid(const QString& storedScreen)
{
    if (storedScreen.isEmpty()) {
        return false;
    }
    if (VirtualScreenId::isVirtual(storedScreen)) {
        QString physId = VirtualScreenId::extractPhysicalId(storedScreen);
        if (!Utils::findScreenByIdOrName(physId)) {
            return false;
        }
        auto* mgr = ScreenManager::instance();
        return mgr && mgr->effectiveScreenIds().contains(storedScreen);
    }
    return Utils::findScreenByIdOrName(storedScreen) != nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// Navigation Target Computation
// ═══════════════════════════════════════════════════════════════════════════

QString WindowTrackingAdaptor::getMoveTargetForWindow(const QString& windowId, const QString& direction,
                                                      const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("getMoveTargetForWindow"))) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_window"), QString(), QString(),
                                                          QString(), screenId))
                                     .toJson(QJsonDocument::Compact));
    }
    if (!validateDirection(direction, QStringLiteral("move"))) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_direction"), QString(),
                                                          QString(), QString(), screenId))
                                     .toJson(QJsonDocument::Compact));
    }
    if (!m_zoneDetectionAdaptor) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_zone_detection"), QString(),
                                                          QString(), QString(), screenId))
                                     .toJson(QJsonDocument::Compact));
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);

    // When the window is snapped, trust the daemon's stored screen assignment
    // over the effect-reported screenId.  KWin's EffectWindow::screen() can
    // return the wrong output for same-model multi-monitor setups (e.g. dual
    // Samsung Odyssey G93SC with different serials).  The daemon's assignment
    // is authoritative because it was set at snap time.
    //
    // When the window is NOT snapped (e.g. moved between monitors via KDE's
    // Move-to-Screen shortcut — outputChanged fires and unsnaps it), we use
    // the effect-provided screen since there's no stored assignment.
    //
    // Only use the stored screen if it's still connected — when a monitor
    // enters standby, KWin rehomes windows but the stored assignment points
    // at the dead output.
    QString effectiveScreenId = screenId;
    if (!currentZoneId.isEmpty()) {
        QString storedScreen = m_service->screenAssignments().value(windowId);
        if (isStoredScreenValid(storedScreen)) {
            effectiveScreenId = storedScreen;
        }
    }

    QString targetZoneId;
    if (currentZoneId.isEmpty()) {
        targetZoneId = m_zoneDetectionAdaptor->getFirstZoneInDirection(direction, effectiveScreenId);
        if (targetZoneId.isEmpty()) {
            Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_zones"), QString(), QString(),
                                      effectiveScreenId);
            return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_zones"), QString(), QString(),
                                                              QString(), effectiveScreenId))
                                         .toJson(QJsonDocument::Compact));
        }
    } else {
        targetZoneId = m_zoneDetectionAdaptor->getAdjacentZone(currentZoneId, direction, effectiveScreenId);
        if (targetZoneId.isEmpty()) {
            Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_adjacent_zone"), currentZoneId,
                                      QString(), effectiveScreenId);
            return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_adjacent_zone"), QString(),
                                                              QString(), currentZoneId, effectiveScreenId))
                                         .toJson(QJsonDocument::Compact));
        }
    }

    QRect geo = m_service->zoneGeometry(targetZoneId, effectiveScreenId);
    if (!geo.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("geometry_error"), currentZoneId,
                                  targetZoneId, effectiveScreenId);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("geometry_error"), targetZoneId,
                                                          QString(), currentZoneId, effectiveScreenId))
                                     .toJson(QJsonDocument::Compact));
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("move"), direction, currentZoneId, targetZoneId, effectiveScreenId);
    return QString::fromUtf8(QJsonDocument(moveResult(true, QString(), targetZoneId, GeometryUtils::rectToJson(geo),
                                                      currentZoneId, effectiveScreenId))
                                 .toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getFocusTargetForWindow(const QString& windowId, const QString& direction,
                                                       const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("getFocusTargetForWindow"))) {
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("invalid_window"), QString(),
                                                           QString(), QString(), screenId))
                                     .toJson(QJsonDocument::Compact));
    }
    if (!validateDirection(direction, QStringLiteral("focus"))) {
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("invalid_direction"), QString(),
                                                           QString(), QString(), screenId))
                                     .toJson(QJsonDocument::Compact));
    }
    if (!m_zoneDetectionAdaptor) {
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("no_zone_detection"), QString(),
                                                           QString(), QString(), screenId))
                                     .toJson(QJsonDocument::Compact));
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("not_snapped"), QString(), QString(),
                                  screenId);
        return QString::fromUtf8(
            QJsonDocument(focusResult(false, QStringLiteral("not_snapped"), QString(), QString(), QString(), screenId))
                .toJson(QJsonDocument::Compact));
    }

    // Trust stored screen for snapped windows — see getMoveTargetForWindow comment
    QString effectiveScreenId = screenId;
    {
        QString storedScreen = m_service->screenAssignments().value(windowId);
        if (isStoredScreenValid(storedScreen)) {
            effectiveScreenId = storedScreen;
        }
    }

    QString targetZoneId = m_zoneDetectionAdaptor->getAdjacentZone(currentZoneId, direction, effectiveScreenId);
    if (targetZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_adjacent_zone"), currentZoneId,
                                  QString(), effectiveScreenId);
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("no_adjacent_zone"), QString(),
                                                           currentZoneId, QString(), effectiveScreenId))
                                     .toJson(QJsonDocument::Compact));
    }

    QStringList windowsInZone = m_service->windowsInZone(targetZoneId);
    if (windowsInZone.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window_in_zone"), currentZoneId,
                                  targetZoneId, effectiveScreenId);
        return QString::fromUtf8(QJsonDocument(focusResult(false, QStringLiteral("no_window_in_zone"), QString(),
                                                           currentZoneId, targetZoneId, effectiveScreenId))
                                     .toJson(QJsonDocument::Compact));
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("focus"), direction, currentZoneId, targetZoneId, effectiveScreenId);
    return QString::fromUtf8(QJsonDocument(focusResult(true, QString(), windowsInZone.first(), currentZoneId,
                                                       targetZoneId, effectiveScreenId))
                                 .toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getRestoreForWindow(const QString& windowId, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("getRestoreForWindow"))) {
        QJsonObject obj;
        obj[QLatin1String("success")] = false;
        obj[QLatin1String("found")] = false;
        obj[QLatin1String("reason")] = QLatin1String("invalid_window");
        return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }

    int x = 0, y = 0, w = 0, h = 0;
    auto geo = m_service->validatedPreTileGeometry(windowId, screenId);
    bool found = geo.has_value();
    if (found) {
        x = geo->x();
        y = geo->y();
        w = geo->width();
        h = geo->height();
    }
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
                                  screenId);
    } else {
        Q_EMIT navigationFeedback(true, QStringLiteral("restore"), QString(), QString(), QString(), screenId);
    }
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getCycleTargetForWindow(const QString& windowId, bool forward, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("getCycleTargetForWindow"))) {
        return QString::fromUtf8(
            QJsonDocument(cycleResult(false, QStringLiteral("invalid_window"), QString(), QString(), screenId))
                .toJson(QJsonDocument::Compact));
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("not_snapped"), QString(), QString(),
                                  screenId);
        return QString::fromUtf8(
            QJsonDocument(cycleResult(false, QStringLiteral("not_snapped"), QString(), QString(), screenId))
                .toJson(QJsonDocument::Compact));
    }

    QStringList windowsInZone = m_service->windowsInZone(currentZoneId);
    if (windowsInZone.size() < 2) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("single_window"), currentZoneId,
                                  currentZoneId, screenId);
        return QString::fromUtf8(
            QJsonDocument(cycleResult(false, QStringLiteral("single_window"), QString(), currentZoneId, screenId))
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

    Q_EMIT navigationFeedback(true, QStringLiteral("cycle"), QString(), currentZoneId, currentZoneId, screenId);
    return QString::fromUtf8(QJsonDocument(cycleResult(true, QString(), targetWindowId, currentZoneId, screenId))
                                 .toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getSwapTargetForWindow(const QString& windowId, const QString& direction,
                                                      const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("getSwapTargetForWindow"))) {
        return QString::fromUtf8(
            QJsonDocument(swapResult(false, QStringLiteral("invalid_window"), windowId, 0, 0, 0, 0, QString(),
                                     QString(), 0, 0, 0, 0, QString(), screenId, QString(), QString()))
                .toJson(QJsonDocument::Compact));
    }
    if (!validateDirection(direction, QStringLiteral("swap"))) {
        return QString::fromUtf8(
            QJsonDocument(swapResult(false, QStringLiteral("invalid_direction"), windowId, 0, 0, 0, 0, QString(),
                                     QString(), 0, 0, 0, 0, QString(), screenId, QString(), QString()))
                .toJson(QJsonDocument::Compact));
    }
    if (!m_zoneDetectionAdaptor) {
        return QString::fromUtf8(
            QJsonDocument(swapResult(false, QStringLiteral("no_zone_detection"), windowId, 0, 0, 0, 0, QString(),
                                     QString(), 0, 0, 0, 0, QString(), screenId, QString(), QString()))
                .toJson(QJsonDocument::Compact));
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("not_snapped"), QString(), QString(),
                                  screenId);
        return QString::fromUtf8(
            QJsonDocument(swapResult(false, QStringLiteral("not_snapped"), windowId, 0, 0, 0, 0, QString(), QString(),
                                     0, 0, 0, 0, QString(), screenId, QString(), QString()))
                .toJson(QJsonDocument::Compact));
    }

    // Trust stored screen for snapped windows — see getMoveTargetForWindow comment
    QString effectiveScreenId = screenId;
    {
        QString storedScreen = m_service->screenAssignments().value(windowId);
        if (isStoredScreenValid(storedScreen)) {
            effectiveScreenId = storedScreen;
        }
    }

    QString targetZoneId = m_zoneDetectionAdaptor->getAdjacentZone(currentZoneId, direction, effectiveScreenId);
    if (targetZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_adjacent_zone"), currentZoneId,
                                  QString(), effectiveScreenId);
        return QString::fromUtf8(
            QJsonDocument(swapResult(false, QStringLiteral("no_adjacent_zone"), windowId, 0, 0, 0, 0, QString(),
                                     QString(), 0, 0, 0, 0, QString(), effectiveScreenId, currentZoneId, QString()))
                .toJson(QJsonDocument::Compact));
    }

    QRect targetGeom = m_service->zoneGeometry(targetZoneId, effectiveScreenId);
    QRect currentGeom = m_service->zoneGeometry(currentZoneId, effectiveScreenId);
    if (!targetGeom.isValid() || !currentGeom.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("geometry_error"), currentZoneId,
                                  targetZoneId, effectiveScreenId);
        return QString::fromUtf8(
            QJsonDocument(swapResult(false, QStringLiteral("geometry_error"), windowId, 0, 0, 0, 0, QString(),
                                     QString(), 0, 0, 0, 0, QString(), effectiveScreenId, currentZoneId, targetZoneId))
                .toJson(QJsonDocument::Compact));
    }

    QStringList windowsInTargetZone = m_service->windowsInZone(targetZoneId);
    if (windowsInTargetZone.isEmpty()) {
        Q_EMIT navigationFeedback(true, QStringLiteral("swap"), direction, currentZoneId, targetZoneId,
                                  effectiveScreenId);
        return QString::fromUtf8(
            QJsonDocument(swapResult(true, QStringLiteral("moved_to_empty"), windowId, targetGeom.x(), targetGeom.y(),
                                     targetGeom.width(), targetGeom.height(), targetZoneId, QString(), 0, 0, 0, 0,
                                     QString(), effectiveScreenId, currentZoneId, targetZoneId))
                .toJson(QJsonDocument::Compact));
    }

    QString targetWindowId = windowsInTargetZone.first();
    Q_EMIT navigationFeedback(true, QStringLiteral("swap"), direction, currentZoneId, targetZoneId, effectiveScreenId);
    return QString::fromUtf8(
        QJsonDocument(swapResult(true, QString(), windowId, targetGeom.x(), targetGeom.y(), targetGeom.width(),
                                 targetGeom.height(), targetZoneId, targetWindowId, currentGeom.x(), currentGeom.y(),
                                 currentGeom.width(), currentGeom.height(), currentZoneId, effectiveScreenId,
                                 currentZoneId, targetZoneId))
            .toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getPushTargetForWindow(const QString& windowId, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("getPushTargetForWindow"))) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_window"), QString(), QString(),
                                                          QString(), screenId))
                                     .toJson(QJsonDocument::Compact));
    }

    QString emptyZoneId = m_service->findEmptyZone(screenId);
    if (emptyZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("no_empty_zone"), QString(), QString(),
                                  screenId);
        return QString::fromUtf8(
            QJsonDocument(moveResult(false, QStringLiteral("no_empty_zone"), QString(), QString(), QString(), screenId))
                .toJson(QJsonDocument::Compact));
    }

    QRect geo = m_service->zoneGeometry(emptyZoneId, screenId);
    if (!geo.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"), QString(),
                                  emptyZoneId, screenId);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("geometry_error"), emptyZoneId,
                                                          QString(), QString(), screenId))
                                     .toJson(QJsonDocument::Compact));
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("push"), QString(), QString(), emptyZoneId, screenId);
    return QString::fromUtf8(
        QJsonDocument(moveResult(true, QString(), emptyZoneId, GeometryUtils::rectToJson(geo), QString(), screenId))
            .toJson(QJsonDocument::Compact));
}

QString WindowTrackingAdaptor::getSnapToZoneByNumberTarget(const QString& windowId, int zoneNumber,
                                                           const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("getSnapToZoneByNumberTarget"))) {
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_window"), QString(), QString(),
                                                          QString(), screenId))
                                     .toJson(QJsonDocument::Compact));
    }

    if (zoneNumber < 1 || zoneNumber > 9) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(),
                                  QString(), screenId);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("invalid_zone_number"), QString(),
                                                          QString(), QString(), screenId))
                                     .toJson(QJsonDocument::Compact));
    }

    // resolveLayoutForScreen accepts both connector names and screen IDs;
    // screenIdForName is idempotent (returns input if already a screen ID).
    auto* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_active_layout"), QString(),
                                  QString(), screenId);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("no_active_layout"), QString(),
                                                          QString(), QString(), screenId))
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
                                  screenId);
        return QString::fromUtf8(QJsonDocument(moveResult(false, QStringLiteral("zone_not_found"), QString(), QString(),
                                                          QString(), screenId))
                                     .toJson(QJsonDocument::Compact));
    }

    QString zoneId = targetZone->id().toString();
    QRect geo = m_service->zoneGeometry(zoneId, screenId);
    if (!geo.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("geometry_error"), QString(), zoneId,
                                  screenId);
        return QString::fromUtf8(
            QJsonDocument(moveResult(false, QStringLiteral("geometry_error"), zoneId, QString(), QString(), screenId))
                .toJson(QJsonDocument::Compact));
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("snap"), QString(), QString(), zoneId, screenId);
    return QString::fromUtf8(
        QJsonDocument(moveResult(true, QString(), zoneId, GeometryUtils::rectToJson(geo), QString(), screenId))
            .toJson(QJsonDocument::Compact));
}

} // namespace PlasmaZones
