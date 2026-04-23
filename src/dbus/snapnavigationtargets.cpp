// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapnavigationtargets.h"
#include "zonedetectionadaptor.h"

#include <PhosphorEngineApi/PlacementEngineBase.h>
#include "../core/interfaces.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "../core/logging.h"
#include <PhosphorScreens/Manager.h>
#include "../core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "../core/windowtrackingservice.h"
#include <PhosphorZones/Zone.h>

#include <QRect>
#include <QStringList>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

namespace {

// ═══════════════════════════════════════════════════════════════════════════
// Result builders
// ═══════════════════════════════════════════════════════════════════════════

MoveTargetResult moveResult(bool success, const QString& reason, const QString& zoneId, const QRect& geometry,
                            const QString& sourceZoneId, const QString& screenId)
{
    return {success,           reason,       zoneId,  geometry.x(), geometry.y(), geometry.width(),
            geometry.height(), sourceZoneId, screenId};
}

FocusTargetResult focusResult(bool success, const QString& reason, const QString& windowIdToActivate,
                              const QString& sourceZoneId, const QString& targetZoneId, const QString& screenId)
{
    return {success, reason, windowIdToActivate, sourceZoneId, targetZoneId, screenId};
}

CycleTargetResult cycleResult(bool success, const QString& reason, const QString& windowIdToActivate,
                              const QString& zoneId, const QString& screenId)
{
    return {success, reason, windowIdToActivate, zoneId, screenId};
}

SwapTargetResult swapResult(bool success, const QString& reason, const QString& windowId1, int x1, int y1, int w1,
                            int h1, const QString& zoneId1, const QString& windowId2, int x2, int y2, int w2, int h2,
                            const QString& zoneId2, const QString& screenId, const QString& sourceZoneId,
                            const QString& targetZoneId)
{
    return {success, reason, windowId1, x1, y1,      w1,       h1,           zoneId1,     windowId2,
            x2,      y2,     w2,        h2, zoneId2, screenId, sourceZoneId, targetZoneId};
}

// ═══════════════════════════════════════════════════════════════════════════
// Stored Screen Validation
//
// For virtual screen IDs, verifies both that the backing physical screen
// is still connected AND that the virtual screen ID is still in the
// effective screen list (guards against stale IDs after config removal).
// For physical screen IDs, verifies the screen is connected.
// ═══════════════════════════════════════════════════════════════════════════

bool isStoredScreenValid(Phosphor::Screens::ScreenManager* mgr, const QString& storedScreen)
{
    if (storedScreen.isEmpty()) {
        return false;
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(storedScreen)) {
        QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(storedScreen);
        if (!Phosphor::Screens::ScreenIdentity::findByIdOrName(physId)) {
            return false;
        }
        return mgr && mgr->effectiveScreenIds().contains(storedScreen);
    }
    return Phosphor::Screens::ScreenIdentity::findByIdOrName(storedScreen) != nullptr;
}

// Pre-call contract checks. Target-resolver callers (the WTA slots) are
// supposed to run validation *before* calling the resolver — dispatcher
// responsibility, not resolver responsibility. But defensively check here
// too so a misrouted call returns a clean no-op result instead of crashing.
bool checkWindowId(const QString& windowId)
{
    return !windowId.isEmpty();
}

bool checkDirection(const QString& direction)
{
    return !direction.isEmpty();
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════════════

SnapNavigationTargetResolver::SnapNavigationTargetResolver(WindowTrackingService* service,
                                                           PhosphorZones::LayoutRegistry* layoutManager,
                                                           ZoneDetectionAdaptor* zoneDetector, FeedbackFn feedback)
    : m_service(service)
    , m_layoutManager(layoutManager)
    , m_zoneDetector(zoneDetector)
    , m_feedback(std::move(feedback))
{
    Q_ASSERT(service);
    Q_ASSERT(layoutManager);
}

void SnapNavigationTargetResolver::setZoneDetector(ZoneDetectionAdaptor* zoneDetector)
{
    m_zoneDetector = zoneDetector;
}

// Feedback callback emission goes through SnapNavigationTargetResolver::emitFeedback
// (defined inline in the header) so call sites don't need to null-check the
// optional std::function at every call.

// ═══════════════════════════════════════════════════════════════════════════
// Navigation Target Computation
// ═══════════════════════════════════════════════════════════════════════════

MoveTargetResult SnapNavigationTargetResolver::getMoveTargetForWindow(const QString& windowId, const QString& direction,
                                                                      const QString& screenId)
{
    if (!checkWindowId(windowId)) {
        qCWarning(lcDbusWindow) << "Cannot getMoveTargetForWindow - empty window ID";
        return moveResult(false, QStringLiteral("invalid_window"), QString(), QRect(), QString(), screenId);
    }
    if (!checkDirection(direction)) {
        qCWarning(lcDbusWindow) << "Cannot move - empty direction";
        // Feedback and result carry the caller's screenId: a half-supplied
        // feedback (empty here, populated on the result) confuses OSD/telemetry
        // consumers that correlate the two.
        emitFeedback(false, QStringLiteral("move"), QStringLiteral("invalid_direction"), QString(), QString(),
                     screenId);
        return moveResult(false, QStringLiteral("invalid_direction"), QString(), QRect(), QString(), screenId);
    }
    if (!m_zoneDetector) {
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
        if (isStoredScreenValid(m_service ? m_service->screenManager() : nullptr, storedScreen)) {
            effectiveScreenId = storedScreen;
        }
    }

    QString targetZoneId;
    if (currentZoneId.isEmpty()) {
        targetZoneId = m_zoneDetector->getFirstZoneInDirection(direction, effectiveScreenId);
        if (targetZoneId.isEmpty()) {
            emitFeedback(false, QStringLiteral("move"), QStringLiteral("no_zones"), QString(), QString(),
                         effectiveScreenId);
            return moveResult(false, QStringLiteral("no_zones"), QString(), QRect(), QString(), effectiveScreenId);
        }
    } else {
        targetZoneId = m_zoneDetector->getAdjacentZone(currentZoneId, direction, effectiveScreenId);
        if (targetZoneId.isEmpty()) {
            emitFeedback(false, QStringLiteral("move"), QStringLiteral("no_adjacent_zone"), currentZoneId, QString(),
                         effectiveScreenId);
            return moveResult(false, QStringLiteral("no_adjacent_zone"), QString(), QRect(), currentZoneId,
                              effectiveScreenId);
        }
    }

    QRect geo = m_service->zoneGeometry(targetZoneId, effectiveScreenId);
    if (!geo.isValid()) {
        emitFeedback(false, QStringLiteral("move"), QStringLiteral("geometry_error"), currentZoneId, targetZoneId,
                     effectiveScreenId);
        return moveResult(false, QStringLiteral("geometry_error"), targetZoneId, QRect(), currentZoneId,
                          effectiveScreenId);
    }

    emitFeedback(true, QStringLiteral("move"), direction, currentZoneId, targetZoneId, effectiveScreenId);
    return moveResult(true, QString(), targetZoneId, geo, currentZoneId, effectiveScreenId);
}

FocusTargetResult SnapNavigationTargetResolver::getFocusTargetForWindow(const QString& windowId,
                                                                        const QString& direction,
                                                                        const QString& screenId)
{
    if (!checkWindowId(windowId)) {
        qCWarning(lcDbusWindow) << "Cannot getFocusTargetForWindow - empty window ID";
        return focusResult(false, QStringLiteral("invalid_window"), QString(), QString(), QString(), screenId);
    }
    if (!checkDirection(direction)) {
        qCWarning(lcDbusWindow) << "Cannot focus - empty direction";
        // Same feedback/result screenId consistency rule as getMoveTargetForWindow.
        emitFeedback(false, QStringLiteral("focus"), QStringLiteral("invalid_direction"), QString(), QString(),
                     screenId);
        return focusResult(false, QStringLiteral("invalid_direction"), QString(), QString(), QString(), screenId);
    }
    if (!m_zoneDetector) {
        return focusResult(false, QStringLiteral("no_zone_detection"), QString(), QString(), QString(), screenId);
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        emitFeedback(false, QStringLiteral("focus"), QStringLiteral("not_snapped"), QString(), QString(), screenId);
        return focusResult(false, QStringLiteral("not_snapped"), QString(), QString(), QString(), screenId);
    }

    // Trust stored screen for snapped windows — see getMoveTargetForWindow comment
    QString effectiveScreenId = screenId;
    {
        QString storedScreen = m_service->screenAssignments().value(windowId);
        if (isStoredScreenValid(m_service ? m_service->screenManager() : nullptr, storedScreen)) {
            effectiveScreenId = storedScreen;
        }
    }

    QString targetZoneId = m_zoneDetector->getAdjacentZone(currentZoneId, direction, effectiveScreenId);
    if (targetZoneId.isEmpty()) {
        emitFeedback(false, QStringLiteral("focus"), QStringLiteral("no_adjacent_zone"), currentZoneId, QString(),
                     effectiveScreenId);
        return focusResult(false, QStringLiteral("no_adjacent_zone"), QString(), currentZoneId, QString(),
                           effectiveScreenId);
    }

    QStringList windowsInZone = m_service->windowsInZone(targetZoneId);
    if (windowsInZone.isEmpty()) {
        emitFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window_in_zone"), currentZoneId, targetZoneId,
                     effectiveScreenId);
        return focusResult(false, QStringLiteral("no_window_in_zone"), QString(), currentZoneId, targetZoneId,
                           effectiveScreenId);
    }

    emitFeedback(true, QStringLiteral("focus"), direction, currentZoneId, targetZoneId, effectiveScreenId);
    return focusResult(true, QString(), windowsInZone.first(), currentZoneId, targetZoneId, effectiveScreenId);
}

RestoreTargetResult SnapNavigationTargetResolver::getRestoreForWindow(const QString& windowId, const QString& screenId)
{
    if (!checkWindowId(windowId)) {
        qCWarning(lcDbusWindow) << "Cannot getRestoreForWindow - empty window ID";
        return {false, false, 0, 0, 0, 0};
    }

    int x = 0, y = 0, w = 0, h = 0;
    // Look up unmanaged geometry from the engine (single store) and validate.
    std::optional<QRect> geo;
    auto* engine = m_service->snapEngine();
    if (engine) {
        if (engine->hasUnmanagedGeometry(windowId)) {
            geo = m_service->validateGeometryForScreen(engine->unmanagedGeometry(windowId),
                                                       engine->unmanagedScreen(windowId), screenId);
        } else {
            const QString appId = m_service->currentAppIdFor(windowId);
            if (appId != windowId && engine->hasUnmanagedGeometry(appId)) {
                geo = m_service->validateGeometryForScreen(engine->unmanagedGeometry(appId),
                                                           engine->unmanagedScreen(appId), screenId);
            }
        }
    }
    bool found = geo.has_value();
    if (found) {
        x = geo->x();
        y = geo->y();
        w = geo->width();
        h = geo->height();
    }
    bool success = found && w > 0 && h > 0;
    if (!success) {
        emitFeedback(false, QStringLiteral("restore"), QStringLiteral("not_snapped"), QString(), QString(), screenId);
    } else {
        emitFeedback(true, QStringLiteral("restore"), QString(), QString(), QString(), screenId);
    }
    return {success, success, x, y, w, h};
}

CycleTargetResult SnapNavigationTargetResolver::getCycleTargetForWindow(const QString& windowId, bool forward,
                                                                        const QString& screenId)
{
    if (!checkWindowId(windowId)) {
        qCWarning(lcDbusWindow) << "Cannot getCycleTargetForWindow - empty window ID";
        return cycleResult(false, QStringLiteral("invalid_window"), QString(), QString(), screenId);
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        emitFeedback(false, QStringLiteral("cycle"), QStringLiteral("not_snapped"), QString(), QString(), screenId);
        return cycleResult(false, QStringLiteral("not_snapped"), QString(), QString(), screenId);
    }

    QStringList windowsInZone = m_service->windowsInZone(currentZoneId);
    if (windowsInZone.size() < 2) {
        emitFeedback(false, QStringLiteral("cycle"), QStringLiteral("single_window"), currentZoneId, currentZoneId,
                     screenId);
        return cycleResult(false, QStringLiteral("single_window"), QString(), currentZoneId, screenId);
    }

    int currentIndex = windowsInZone.indexOf(windowId);
    if (currentIndex < 0) {
        currentIndex = 0;
        // Fallback: match by current class — handles the case where the
        // stored windowId and the incoming windowId represent the same
        // instance but carry different appIds due to a mid-session rename.
        const QString targetAppId = m_service->currentAppIdFor(windowId);
        for (int i = 0; i < windowsInZone.size(); ++i) {
            const QString entryAppId = m_service->currentAppIdFor(windowsInZone[i]);
            if (entryAppId == targetAppId) {
                currentIndex = i;
                break;
            }
        }
    }
    int nextIndex = forward ? (currentIndex + 1) % windowsInZone.size()
                            : (currentIndex - 1 + windowsInZone.size()) % windowsInZone.size();
    QString targetWindowId = windowsInZone.at(nextIndex);

    emitFeedback(true, QStringLiteral("cycle"), QString(), currentZoneId, currentZoneId, screenId);
    return cycleResult(true, QString(), targetWindowId, currentZoneId, screenId);
}

SwapTargetResult SnapNavigationTargetResolver::getSwapTargetForWindow(const QString& windowId, const QString& direction,
                                                                      const QString& screenId)
{
    // On failure, windowId1 is returned empty so that a caller which forgets
    // to check `success` cannot accidentally act on the calling window.
    if (!checkWindowId(windowId)) {
        qCWarning(lcDbusWindow) << "Cannot getSwapTargetForWindow - empty window ID";
        return swapResult(false, QStringLiteral("invalid_window"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0, 0,
                          0, QString(), screenId, QString(), QString());
    }
    if (!checkDirection(direction)) {
        qCWarning(lcDbusWindow) << "Cannot swap - empty direction";
        // Same feedback/result screenId consistency rule as getMoveTargetForWindow.
        emitFeedback(false, QStringLiteral("swap"), QStringLiteral("invalid_direction"), QString(), QString(),
                     screenId);
        return swapResult(false, QStringLiteral("invalid_direction"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0,
                          0, 0, QString(), screenId, QString(), QString());
    }
    if (!m_zoneDetector) {
        return swapResult(false, QStringLiteral("no_zone_detection"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0,
                          0, 0, QString(), screenId, QString(), QString());
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        emitFeedback(false, QStringLiteral("swap"), QStringLiteral("not_snapped"), QString(), QString(), screenId);
        return swapResult(false, QStringLiteral("not_snapped"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0, 0, 0,
                          QString(), screenId, QString(), QString());
    }

    // Trust stored screen for snapped windows — see getMoveTargetForWindow comment
    QString effectiveScreenId = screenId;
    {
        QString storedScreen = m_service->screenAssignments().value(windowId);
        if (isStoredScreenValid(m_service ? m_service->screenManager() : nullptr, storedScreen)) {
            effectiveScreenId = storedScreen;
        }
    }

    QString targetZoneId = m_zoneDetector->getAdjacentZone(currentZoneId, direction, effectiveScreenId);
    if (targetZoneId.isEmpty()) {
        emitFeedback(false, QStringLiteral("swap"), QStringLiteral("no_adjacent_zone"), currentZoneId, QString(),
                     effectiveScreenId);
        return swapResult(false, QStringLiteral("no_adjacent_zone"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0,
                          0, 0, QString(), effectiveScreenId, currentZoneId, QString());
    }

    QRect targetGeom = m_service->zoneGeometry(targetZoneId, effectiveScreenId);
    QRect currentGeom = m_service->zoneGeometry(currentZoneId, effectiveScreenId);
    if (!targetGeom.isValid() || !currentGeom.isValid()) {
        emitFeedback(false, QStringLiteral("swap"), QStringLiteral("geometry_error"), currentZoneId, targetZoneId,
                     effectiveScreenId);
        return swapResult(false, QStringLiteral("geometry_error"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0, 0,
                          0, QString(), effectiveScreenId, currentZoneId, targetZoneId);
    }

    QStringList windowsInTargetZone = m_service->windowsInZone(targetZoneId);
    if (windowsInTargetZone.isEmpty()) {
        emitFeedback(true, QStringLiteral("swap"), direction, currentZoneId, targetZoneId, effectiveScreenId);
        return swapResult(true, QStringLiteral("moved_to_empty"), windowId, targetGeom.x(), targetGeom.y(),
                          targetGeom.width(), targetGeom.height(), targetZoneId, QString(), 0, 0, 0, 0, QString(),
                          effectiveScreenId, currentZoneId, targetZoneId);
    }

    QString targetWindowId = windowsInTargetZone.first();
    emitFeedback(true, QStringLiteral("swap"), direction, currentZoneId, targetZoneId, effectiveScreenId);
    return swapResult(true, QString(), windowId, targetGeom.x(), targetGeom.y(), targetGeom.width(),
                      targetGeom.height(), targetZoneId, targetWindowId, currentGeom.x(), currentGeom.y(),
                      currentGeom.width(), currentGeom.height(), currentZoneId, effectiveScreenId, currentZoneId,
                      targetZoneId);
}

MoveTargetResult SnapNavigationTargetResolver::getPushTargetForWindow(const QString& windowId, const QString& screenId)
{
    if (!checkWindowId(windowId)) {
        qCWarning(lcDbusWindow) << "Cannot getPushTargetForWindow - empty window ID";
        return moveResult(false, QStringLiteral("invalid_window"), QString(), QRect(), QString(), screenId);
    }

    QString emptyZoneId = m_service->findEmptyZone(screenId);
    if (emptyZoneId.isEmpty()) {
        emitFeedback(false, QStringLiteral("push"), QStringLiteral("no_empty_zone"), QString(), QString(), screenId);
        return moveResult(false, QStringLiteral("no_empty_zone"), QString(), QRect(), QString(), screenId);
    }

    QRect geo = m_service->zoneGeometry(emptyZoneId, screenId);
    if (!geo.isValid()) {
        emitFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"), QString(), emptyZoneId, screenId);
        return moveResult(false, QStringLiteral("geometry_error"), emptyZoneId, QRect(), QString(), screenId);
    }

    emitFeedback(true, QStringLiteral("push"), QString(), QString(), emptyZoneId, screenId);
    return moveResult(true, QString(), emptyZoneId, geo, QString(), screenId);
}

MoveTargetResult SnapNavigationTargetResolver::getSnapToZoneByNumberTarget(const QString& windowId, int zoneNumber,
                                                                           const QString& screenId)
{
    if (!checkWindowId(windowId)) {
        qCWarning(lcDbusWindow) << "Cannot getSnapToZoneByNumberTarget - empty window ID";
        return moveResult(false, QStringLiteral("invalid_window"), QString(), QRect(), QString(), screenId);
    }

    if (zoneNumber < 1 || zoneNumber > 9) {
        emitFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(), QString(),
                     screenId);
        return moveResult(false, QStringLiteral("invalid_zone_number"), QString(), QRect(), QString(), screenId);
    }

    // resolveLayoutForScreen accepts both connector names and screen IDs;
    // screenIdForName is idempotent (returns input if already a screen ID).
    auto* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout) {
        emitFeedback(false, QStringLiteral("snap"), QStringLiteral("no_active_layout"), QString(), QString(), screenId);
        return moveResult(false, QStringLiteral("no_active_layout"), QString(), QRect(), QString(), screenId);
    }

    PhosphorZones::Zone* targetZone = nullptr;
    for (PhosphorZones::Zone* zone : layout->zones()) {
        if (zone->zoneNumber() == zoneNumber) {
            targetZone = zone;
            break;
        }
    }

    if (!targetZone) {
        emitFeedback(false, QStringLiteral("snap"), QStringLiteral("zone_not_found"), QString(), QString(), screenId);
        return moveResult(false, QStringLiteral("zone_not_found"), QString(), QRect(), QString(), screenId);
    }

    QString zoneId = targetZone->id().toString();
    QRect geo = m_service->zoneGeometry(zoneId, screenId);
    if (!geo.isValid()) {
        emitFeedback(false, QStringLiteral("snap"), QStringLiteral("geometry_error"), QString(), zoneId, screenId);
        return moveResult(false, QStringLiteral("geometry_error"), zoneId, QRect(), QString(), screenId);
    }

    emitFeedback(true, QStringLiteral("snap"), QString(), QString(), zoneId, screenId);
    return moveResult(true, QString(), zoneId, geo, QString(), screenId);
}

} // namespace PlasmaZones
