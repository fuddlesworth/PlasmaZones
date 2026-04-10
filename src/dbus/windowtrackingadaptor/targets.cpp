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

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════
// Static Result Builders
// ═══════════════════════════════════════════════════════════════════════════

static MoveTargetResult moveResult(bool success, const QString& reason, const QString& zoneId, const QRect& geometry,
                                   const QString& sourceZoneId, const QString& screenId)
{
    return {success,           reason,       zoneId,  geometry.x(), geometry.y(), geometry.width(),
            geometry.height(), sourceZoneId, screenId};
}

static FocusTargetResult focusResult(bool success, const QString& reason, const QString& windowIdToActivate,
                                     const QString& sourceZoneId, const QString& targetZoneId, const QString& screenId)
{
    return {success, reason, windowIdToActivate, sourceZoneId, targetZoneId, screenId};
}

static CycleTargetResult cycleResult(bool success, const QString& reason, const QString& windowIdToActivate,
                                     const QString& zoneId, const QString& screenId)
{
    return {success, reason, windowIdToActivate, zoneId, screenId};
}

static SwapTargetResult swapResult(bool success, const QString& reason, const QString& windowId1, int x1, int y1,
                                   int w1, int h1, const QString& zoneId1, const QString& windowId2, int x2, int y2,
                                   int w2, int h2, const QString& zoneId2, const QString& screenId,
                                   const QString& sourceZoneId, const QString& targetZoneId)
{
    return {success, reason, windowId1, x1, y1,      w1,       h1,           zoneId1,     windowId2,
            x2,      y2,     w2,        h2, zoneId2, screenId, sourceZoneId, targetZoneId};
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

MoveTargetResult WindowTrackingAdaptor::getMoveTargetForWindow(const QString& windowId, const QString& direction,
                                                               const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("getMoveTargetForWindow"))) {
        return moveResult(false, QStringLiteral("invalid_window"), QString(), QRect(), QString(), screenId);
    }
    if (!validateDirection(direction, QStringLiteral("move"))) {
        return moveResult(false, QStringLiteral("invalid_direction"), QString(), QRect(), QString(), screenId);
    }
    if (!m_zoneDetectionAdaptor) {
        return moveResult(false, QStringLiteral("no_zone_detection"), QString(), QRect(), QString(), screenId);
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
            return moveResult(false, QStringLiteral("no_zones"), QString(), QRect(), QString(), effectiveScreenId);
        }
    } else {
        targetZoneId = m_zoneDetectionAdaptor->getAdjacentZone(currentZoneId, direction, effectiveScreenId);
        if (targetZoneId.isEmpty()) {
            Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_adjacent_zone"), currentZoneId,
                                      QString(), effectiveScreenId);
            return moveResult(false, QStringLiteral("no_adjacent_zone"), QString(), QRect(), currentZoneId,
                              effectiveScreenId);
        }
    }

    QRect geo = m_service->zoneGeometry(targetZoneId, effectiveScreenId);
    if (!geo.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("geometry_error"), currentZoneId,
                                  targetZoneId, effectiveScreenId);
        return moveResult(false, QStringLiteral("geometry_error"), targetZoneId, QRect(), currentZoneId,
                          effectiveScreenId);
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("move"), direction, currentZoneId, targetZoneId, effectiveScreenId);
    return moveResult(true, QString(), targetZoneId, geo, currentZoneId, effectiveScreenId);
}

FocusTargetResult WindowTrackingAdaptor::getFocusTargetForWindow(const QString& windowId, const QString& direction,
                                                                 const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("getFocusTargetForWindow"))) {
        return focusResult(false, QStringLiteral("invalid_window"), QString(), QString(), QString(), screenId);
    }
    if (!validateDirection(direction, QStringLiteral("focus"))) {
        return focusResult(false, QStringLiteral("invalid_direction"), QString(), QString(), QString(), screenId);
    }
    if (!m_zoneDetectionAdaptor) {
        return focusResult(false, QStringLiteral("no_zone_detection"), QString(), QString(), QString(), screenId);
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("not_snapped"), QString(), QString(),
                                  screenId);
        return focusResult(false, QStringLiteral("not_snapped"), QString(), QString(), QString(), screenId);
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
        return focusResult(false, QStringLiteral("no_adjacent_zone"), QString(), currentZoneId, QString(),
                           effectiveScreenId);
    }

    QStringList windowsInZone = m_service->windowsInZone(targetZoneId);
    if (windowsInZone.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window_in_zone"), currentZoneId,
                                  targetZoneId, effectiveScreenId);
        return focusResult(false, QStringLiteral("no_window_in_zone"), QString(), currentZoneId, targetZoneId,
                           effectiveScreenId);
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("focus"), direction, currentZoneId, targetZoneId, effectiveScreenId);
    return focusResult(true, QString(), windowsInZone.first(), currentZoneId, targetZoneId, effectiveScreenId);
}

RestoreTargetResult WindowTrackingAdaptor::getRestoreForWindow(const QString& windowId, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("getRestoreForWindow"))) {
        return {false, false, 0, 0, 0, 0};
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
    bool success = found && w > 0 && h > 0;
    if (!success) {
        Q_EMIT navigationFeedback(false, QStringLiteral("restore"), QStringLiteral("not_snapped"), QString(), QString(),
                                  screenId);
    } else {
        Q_EMIT navigationFeedback(true, QStringLiteral("restore"), QString(), QString(), QString(), screenId);
    }
    return {success, success, x, y, w, h};
}

CycleTargetResult WindowTrackingAdaptor::getCycleTargetForWindow(const QString& windowId, bool forward,
                                                                 const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("getCycleTargetForWindow"))) {
        return cycleResult(false, QStringLiteral("invalid_window"), QString(), QString(), screenId);
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("not_snapped"), QString(), QString(),
                                  screenId);
        return cycleResult(false, QStringLiteral("not_snapped"), QString(), QString(), screenId);
    }

    QStringList windowsInZone = m_service->windowsInZone(currentZoneId);
    if (windowsInZone.size() < 2) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("single_window"), currentZoneId,
                                  currentZoneId, screenId);
        return cycleResult(false, QStringLiteral("single_window"), QString(), currentZoneId, screenId);
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
    return cycleResult(true, QString(), targetWindowId, currentZoneId, screenId);
}

SwapTargetResult WindowTrackingAdaptor::getSwapTargetForWindow(const QString& windowId, const QString& direction,
                                                               const QString& screenId)
{
    // On failure, windowId1 is returned empty so that a caller which forgets
    // to check `success` cannot accidentally act on the calling window.
    if (!validateWindowId(windowId, QStringLiteral("getSwapTargetForWindow"))) {
        return swapResult(false, QStringLiteral("invalid_window"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0, 0,
                          0, QString(), screenId, QString(), QString());
    }
    if (!validateDirection(direction, QStringLiteral("swap"))) {
        return swapResult(false, QStringLiteral("invalid_direction"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0,
                          0, 0, QString(), screenId, QString(), QString());
    }
    if (!m_zoneDetectionAdaptor) {
        return swapResult(false, QStringLiteral("no_zone_detection"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0,
                          0, 0, QString(), screenId, QString(), QString());
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("not_snapped"), QString(), QString(),
                                  screenId);
        return swapResult(false, QStringLiteral("not_snapped"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0, 0, 0,
                          QString(), screenId, QString(), QString());
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
        return swapResult(false, QStringLiteral("no_adjacent_zone"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0,
                          0, 0, QString(), effectiveScreenId, currentZoneId, QString());
    }

    QRect targetGeom = m_service->zoneGeometry(targetZoneId, effectiveScreenId);
    QRect currentGeom = m_service->zoneGeometry(currentZoneId, effectiveScreenId);
    if (!targetGeom.isValid() || !currentGeom.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("geometry_error"), currentZoneId,
                                  targetZoneId, effectiveScreenId);
        return swapResult(false, QStringLiteral("geometry_error"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0, 0,
                          0, QString(), effectiveScreenId, currentZoneId, targetZoneId);
    }

    QStringList windowsInTargetZone = m_service->windowsInZone(targetZoneId);
    if (windowsInTargetZone.isEmpty()) {
        Q_EMIT navigationFeedback(true, QStringLiteral("swap"), direction, currentZoneId, targetZoneId,
                                  effectiveScreenId);
        return swapResult(true, QStringLiteral("moved_to_empty"), windowId, targetGeom.x(), targetGeom.y(),
                          targetGeom.width(), targetGeom.height(), targetZoneId, QString(), 0, 0, 0, 0, QString(),
                          effectiveScreenId, currentZoneId, targetZoneId);
    }

    QString targetWindowId = windowsInTargetZone.first();
    Q_EMIT navigationFeedback(true, QStringLiteral("swap"), direction, currentZoneId, targetZoneId, effectiveScreenId);
    return swapResult(true, QString(), windowId, targetGeom.x(), targetGeom.y(), targetGeom.width(),
                      targetGeom.height(), targetZoneId, targetWindowId, currentGeom.x(), currentGeom.y(),
                      currentGeom.width(), currentGeom.height(), currentZoneId, effectiveScreenId, currentZoneId,
                      targetZoneId);
}

MoveTargetResult WindowTrackingAdaptor::getPushTargetForWindow(const QString& windowId, const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("getPushTargetForWindow"))) {
        return moveResult(false, QStringLiteral("invalid_window"), QString(), QRect(), QString(), screenId);
    }

    QString emptyZoneId = m_service->findEmptyZone(screenId);
    if (emptyZoneId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("no_empty_zone"), QString(), QString(),
                                  screenId);
        return moveResult(false, QStringLiteral("no_empty_zone"), QString(), QRect(), QString(), screenId);
    }

    QRect geo = m_service->zoneGeometry(emptyZoneId, screenId);
    if (!geo.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"), QString(),
                                  emptyZoneId, screenId);
        return moveResult(false, QStringLiteral("geometry_error"), emptyZoneId, QRect(), QString(), screenId);
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("push"), QString(), QString(), emptyZoneId, screenId);
    return moveResult(true, QString(), emptyZoneId, geo, QString(), screenId);
}

MoveTargetResult WindowTrackingAdaptor::getSnapToZoneByNumberTarget(const QString& windowId, int zoneNumber,
                                                                    const QString& screenId)
{
    if (!validateWindowId(windowId, QStringLiteral("getSnapToZoneByNumberTarget"))) {
        return moveResult(false, QStringLiteral("invalid_window"), QString(), QRect(), QString(), screenId);
    }

    if (zoneNumber < 1 || zoneNumber > 9) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(),
                                  QString(), screenId);
        return moveResult(false, QStringLiteral("invalid_zone_number"), QString(), QRect(), QString(), screenId);
    }

    // resolveLayoutForScreen accepts both connector names and screen IDs;
    // screenIdForName is idempotent (returns input if already a screen ID).
    auto* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_active_layout"), QString(),
                                  QString(), screenId);
        return moveResult(false, QStringLiteral("no_active_layout"), QString(), QRect(), QString(), screenId);
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
        return moveResult(false, QStringLiteral("zone_not_found"), QString(), QRect(), QString(), screenId);
    }

    QString zoneId = targetZone->id().toString();
    QRect geo = m_service->zoneGeometry(zoneId, screenId);
    if (!geo.isValid()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("geometry_error"), QString(), zoneId,
                                  screenId);
        return moveResult(false, QStringLiteral("geometry_error"), zoneId, QRect(), QString(), screenId);
    }

    Q_EMIT navigationFeedback(true, QStringLiteral("snap"), QString(), QString(), zoneId, screenId);
    return moveResult(true, QString(), zoneId, geo, QString(), screenId);
}

} // namespace PlasmaZones
