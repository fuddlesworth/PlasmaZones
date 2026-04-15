// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file navigation_actions.cpp
 * @brief Snap-mode navigation entry points moved out of WindowTrackingAdaptor.
 *
 * Before the Phase 5 cleanup these methods lived on WindowTrackingAdaptor,
 * which had grown into a partial engine: target resolution, feedback
 * emission, bookkeeping, and zone-routing logic all sat in the D-Bus
 * facade class. That violated "the adaptor is a thin facade" and made
 * the daemon branch on mode at every shortcut handler.
 *
 * Navigation is now SnapEngine's concern. The body of each method is
 * essentially the same as the WTA version — it still calls into the
 * SnapNavigationTargetResolver, the WindowTrackingService, and the
 * snap-mode bookkeeping helpers — but those are now accessed through
 * the WTA back-reference (m_wta) for state that hasn't been migrated
 * yet (target resolver, last-active shadow, frame-geometry shadow,
 * bookkeeping helpers). Future refactors should continue moving state
 * into either SnapEngine or WindowTrackingService and retire the
 * back-reference.
 *
 * Signals emitted by these methods are SnapEngine signals; SnapAdaptor
 * relays them to WindowTrackingAdaptor, which exposes them on D-Bus.
 */

#include "../SnapEngine.h"

#include "../../config/settings.h"
#include "../../core/interfaces.h"
#include "../../core/layout.h"
#include "../../core/layoutmanager.h"
#include "../../core/logging.h"
#include "../../core/screenmanager.h"
#include "../../core/utils.h"
#include "../../core/virtualdesktopmanager.h"
#include "../../core/virtualscreen.h"
#include "../../core/windowtrackingservice.h"
#include "../../dbus/snapnavigationtargets.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "shared/virtualscreenid.h"

#include <QGuiApplication>
#include <QScreen>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Private helpers
// ═══════════════════════════════════════════════════════════════════════════════

namespace {

/// Resolve the screen to use for a navigation operation on @p windowId.
/// Prefers the stored screen assignment for snapped windows (so the
/// operation stays on the monitor the user originally chose, rather than
/// the one KWin happens to think the window is on). Falls back to the
/// active cursor/focus screen from the WTA shadow.
QString resolveNavScreen(const WindowTrackingAdaptor* wta, const QString& windowId, WindowTrackingService* service)
{
    // Snapped window — trust the stored screen when it's still reachable.
    const QString zoneId = service->zoneForWindow(windowId);
    if (!zoneId.isEmpty()) {
        const QString storedScreen = service->screenAssignments().value(windowId);
        if (!storedScreen.isEmpty()) {
            if (VirtualScreenId::isVirtual(storedScreen)) {
                const QString physId = VirtualScreenId::extractPhysicalId(storedScreen);
                QScreen* physScreen = Utils::findScreenByIdOrName(physId);
                if (physScreen) {
                    auto* mgr = ScreenManager::instance();
                    if (mgr && mgr->effectiveScreenIds().contains(storedScreen)) {
                        return storedScreen;
                    }
                }
            } else if (Utils::findScreenByIdOrName(storedScreen)) {
                return storedScreen;
            }
        }
    }
    QString screen = wta->lastCursorScreenName();
    if (screen.isEmpty()) {
        screen = wta->lastActiveScreenName();
    }
    return screen;
}

/// Apply a vector of ZoneAssignmentEntry: do snap bookkeeping per entry,
/// build a batch geometry list, and emit it via @p emitBatch. Shared by
/// rotate, resnap, and resnap-current paths.
bool processBatchEntries(SnapEngine* engine, WindowTrackingAdaptor* wta, const QVector<ZoneAssignmentEntry>& entries,
                         const QString& action)
{
    if (entries.isEmpty()) {
        return false;
    }

    for (const auto& entry : entries) {
        if (entry.targetZoneId == QLatin1String("__restore__")) {
            wta->windowUnsnapped(entry.windowId);
            wta->clearPreTileGeometry(entry.windowId);
        } else {
            QString screenId = entry.targetScreenId;
            QPoint center = entry.targetGeometry.center();
            auto* mgr = ScreenManager::instance();
            if (screenId.isEmpty() && mgr) {
                screenId = mgr->effectiveScreenAt(center);
            }
            if (screenId.isEmpty()) {
                for (QScreen* screen : QGuiApplication::screens()) {
                    if (screen->geometry().contains(center)) {
                        screenId = Utils::screenIdentifier(screen);
                        break;
                    }
                }
            }
            if (screenId.isEmpty()) {
                screenId = wta->lastCursorScreenName();
                if (screenId.isEmpty()) {
                    screenId = wta->lastActiveScreenName();
                }
            }
            if (entry.targetZoneIds.size() > 1) {
                wta->windowSnappedMultiZone(entry.windowId, entry.targetZoneIds, screenId);
            } else {
                wta->windowSnapped(entry.windowId, entry.targetZoneId, screenId);
            }
        }
    }

    WindowGeometryList geometries;
    geometries.reserve(entries.size());
    for (const ZoneAssignmentEntry& entry : entries) {
        geometries.append(WindowGeometryEntry::fromRect(entry.windowId, entry.targetGeometry));
    }
    Q_EMIT engine->applyGeometriesBatch(geometries, action);
    return true;
}

bool isWindowExcludedForAction(const ISettings* settings, WindowTrackingService* service, const QString& windowId,
                               const QString& action, SnapEngine* engine, const QString& lastActiveScreenId)
{
    if (!settings) {
        return false;
    }
    const QString appId = service->currentAppIdFor(windowId);
    for (const QString& excluded : settings->excludedApplications()) {
        if (Utils::appIdMatches(appId, excluded)) {
            qCInfo(lcCore) << action << ":" << windowId << "excluded by app rule:" << excluded;
            Q_EMIT engine->navigationFeedback(false, action, QStringLiteral("excluded"), appId, QString(),
                                              lastActiveScreenId);
            return true;
        }
    }
    for (const QString& excluded : settings->excludedWindowClasses()) {
        if (Utils::appIdMatches(appId, excluded)) {
            qCInfo(lcCore) << action << ":" << windowId << "excluded by class rule:" << excluded;
            Q_EMIT engine->navigationFeedback(false, action, QStringLiteral("excluded"), appId, QString(),
                                              lastActiveScreenId);
            return true;
        }
    }
    return false;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation entry points
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::focusInDirection(const QString& direction)
{
    qCInfo(lcCore) << "SnapEngine::focusInDirection:" << direction;
    if (!m_wta || !m_windowTracker) {
        return;
    }
    auto* resolver = m_wta->targetResolver();
    if (!resolver) {
        return;
    }
    if (direction.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), QString());
        return;
    }
    const QString windowId = m_wta->lastActiveWindowId();
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window"), QString(), QString(),
                                  m_wta->lastActiveScreenName());
        return;
    }
    const QString screenId = resolveNavScreen(m_wta, windowId, m_windowTracker);
    FocusTargetResult result = resolver->getFocusTargetForWindow(windowId, direction, screenId);
    if (!result.success) {
        return; // resolver already emitted feedback via its callback
    }
    if (!result.windowIdToActivate.isEmpty()) {
        Q_EMIT activateWindowRequested(result.windowIdToActivate);
    }
}

void SnapEngine::moveFocusedInDirection(const QString& direction)
{
    qCInfo(lcCore) << "SnapEngine::moveFocusedInDirection:" << direction;
    if (!m_wta || !m_windowTracker) {
        return;
    }
    auto* resolver = m_wta->targetResolver();
    if (!resolver) {
        return;
    }
    if (direction.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), QString());
        return;
    }
    const QString windowId = m_wta->lastActiveWindowId();
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_window"), QString(), QString(),
                                  m_wta->lastActiveScreenName());
        return;
    }
    if (isWindowExcludedForAction(m_settings, m_windowTracker, windowId, QStringLiteral("move"), this,
                                  m_wta->lastActiveScreenName())) {
        return;
    }
    const QString screenId = resolveNavScreen(m_wta, windowId, m_windowTracker);
    MoveTargetResult result = resolver->getMoveTargetForWindow(windowId, direction, screenId);
    if (!result.success) {
        return;
    }
    const QRect geo = result.toRect();
    if (!geo.isValid()) {
        qCWarning(lcCore) << "SnapEngine::moveFocusedInDirection: invalid geometry from nav result";
        return;
    }
    m_wta->windowSnapped(windowId, result.zoneId, result.screenName);
    m_wta->recordSnapIntent(windowId, true);
    Q_EMIT applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), result.zoneId,
                                  result.screenName, false);
}

void SnapEngine::swapFocusedInDirection(const QString& direction)
{
    qCInfo(lcCore) << "SnapEngine::swapFocusedInDirection:" << direction;
    if (!m_wta || !m_windowTracker) {
        return;
    }
    auto* resolver = m_wta->targetResolver();
    if (!resolver) {
        return;
    }
    if (direction.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), QString());
        return;
    }
    const QString windowId = m_wta->lastActiveWindowId();
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_window"), QString(), QString(),
                                  m_wta->lastActiveScreenName());
        return;
    }
    if (isWindowExcludedForAction(m_settings, m_windowTracker, windowId, QStringLiteral("swap"), this,
                                  m_wta->lastActiveScreenName())) {
        return;
    }
    const QString screenId = resolveNavScreen(m_wta, windowId, m_windowTracker);
    SwapTargetResult result = resolver->getSwapTargetForWindow(windowId, direction, screenId);
    if (!result.success) {
        return;
    }
    m_wta->windowSnapped(result.windowId1, result.zoneId1, result.screenName);
    m_wta->recordSnapIntent(result.windowId1, true);
    Q_EMIT applyGeometryRequested(result.windowId1, result.x1, result.y1, result.w1, result.h1, result.zoneId1,
                                  result.screenName, false);

    if (!result.windowId2.isEmpty()) {
        QString screen2 = m_windowTracker->screenAssignments().value(result.windowId2);
        if (screen2.isEmpty()) {
            screen2 = result.screenName;
        }
        m_wta->windowSnapped(result.windowId2, result.zoneId2, screen2);
        m_wta->recordSnapIntent(result.windowId2, true);
        Q_EMIT applyGeometryRequested(result.windowId2, result.x2, result.y2, result.w2, result.h2, result.zoneId2,
                                      screen2, false);
    }
}

void SnapEngine::moveFocusedToPosition(int zoneNumber, const QString& screenId)
{
    qCInfo(lcCore) << "SnapEngine::moveFocusedToPosition: zone" << zoneNumber << "screen" << screenId;
    if (!m_wta || !m_windowTracker) {
        return;
    }
    auto* resolver = m_wta->targetResolver();
    if (!resolver) {
        return;
    }
    if (zoneNumber < 1 || zoneNumber > 9) {
        qCWarning(lcCore) << "SnapEngine::moveFocusedToPosition: invalid zone number" << zoneNumber;
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(),
                                  QString(), QString());
        return;
    }
    const QString windowId = m_wta->lastActiveWindowId();
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_window"), QString(), QString(),
                                  screenId.isEmpty() ? m_wta->lastActiveScreenName() : screenId);
        return;
    }
    if (isWindowExcludedForAction(m_settings, m_windowTracker, windowId, QStringLiteral("snap"), this,
                                  m_wta->lastActiveScreenName())) {
        return;
    }
    const QString effectiveScreen = screenId.isEmpty() ? resolveNavScreen(m_wta, windowId, m_windowTracker) : screenId;
    MoveTargetResult result = resolver->getSnapToZoneByNumberTarget(windowId, zoneNumber, effectiveScreen);
    if (!result.success) {
        return;
    }
    const QRect geo = result.toRect();
    if (!geo.isValid()) {
        qCWarning(lcCore) << "SnapEngine::moveFocusedToPosition: invalid geometry from nav result";
        return;
    }
    m_wta->windowSnapped(windowId, result.zoneId, effectiveScreen);
    m_wta->recordSnapIntent(windowId, true);
    Q_EMIT applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), result.zoneId, effectiveScreen,
                                  false);
}

void SnapEngine::pushFocusedToEmptyZone(const QString& screenId)
{
    qCInfo(lcCore) << "SnapEngine::pushFocusedToEmptyZone: screen" << screenId;
    if (!m_wta || !m_windowTracker) {
        return;
    }
    auto* resolver = m_wta->targetResolver();
    if (!resolver) {
        return;
    }
    const QString windowId = m_wta->lastActiveWindowId();
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("no_window"), QString(), QString(),
                                  screenId.isEmpty() ? m_wta->lastActiveScreenName() : screenId);
        return;
    }
    if (isWindowExcludedForAction(m_settings, m_windowTracker, windowId, QStringLiteral("push"), this,
                                  m_wta->lastActiveScreenName())) {
        return;
    }
    const QString effectiveScreen = screenId.isEmpty() ? resolveNavScreen(m_wta, windowId, m_windowTracker) : screenId;
    MoveTargetResult result = resolver->getPushTargetForWindow(windowId, effectiveScreen);
    if (!result.success) {
        return;
    }
    const QRect geo = result.toRect();
    if (!geo.isValid()) {
        qCWarning(lcCore) << "SnapEngine::pushFocusedToEmptyZone: invalid geometry from nav result";
        return;
    }
    m_wta->windowSnapped(windowId, result.zoneId, effectiveScreen);
    m_wta->recordSnapIntent(windowId, true);
    Q_EMIT applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), result.zoneId, effectiveScreen,
                                  false);
}

void SnapEngine::restoreFocusedWindow()
{
    qCInfo(lcCore) << "SnapEngine::restoreFocusedWindow";
    if (!m_wta || !m_windowTracker) {
        return;
    }
    auto* resolver = m_wta->targetResolver();
    if (!resolver) {
        return;
    }
    const QString windowId = m_wta->lastActiveWindowId();
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("restore"), QStringLiteral("no_window"), QString(), QString(),
                                  m_wta->lastActiveScreenName());
        return;
    }
    const QString screenId = resolveNavScreen(m_wta, windowId, m_windowTracker);
    RestoreTargetResult result = resolver->getRestoreForWindow(windowId, screenId);
    if (!result.success) {
        return;
    }
    m_wta->windowUnsnapped(windowId);
    m_wta->clearPreTileGeometry(windowId);
    Q_EMIT applyGeometryRequested(windowId, result.x, result.y, result.width, result.height, QString(), screenId,
                                  false);
}

void SnapEngine::toggleFocusedFloat()
{
    qCInfo(lcCore) << "SnapEngine::toggleFocusedFloat";
    if (!m_wta) {
        return;
    }
    // Delegates to the WTA-side toggleWindowFloat which owns the frame-
    // geometry shadow and the pre-tile geometry capture logic for the
    // "currently floating" branch. Moving that state surgery into SnapEngine
    // is a follow-up; for now the entry point lives here so the navigator
    // dispatch stays uniform.
    m_wta->toggleWindowFloat();
}

void SnapEngine::cycleFocus(bool forward)
{
    qCInfo(lcCore) << "SnapEngine::cycleFocus: forward=" << forward;
    if (!m_wta || !m_windowTracker) {
        return;
    }
    auto* resolver = m_wta->targetResolver();
    if (!resolver) {
        return;
    }
    const QString windowId = m_wta->lastActiveWindowId();
    if (windowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("no_window"), QString(), QString(),
                                  m_wta->lastActiveScreenName());
        return;
    }
    const QString screenId = resolveNavScreen(m_wta, windowId, m_windowTracker);
    CycleTargetResult result = resolver->getCycleTargetForWindow(windowId, forward, screenId);
    if (!result.success) {
        return;
    }
    if (!result.windowIdToActivate.isEmpty()) {
        Q_EMIT activateWindowRequested(result.windowIdToActivate);
    }
}

void SnapEngine::rotateWindowsInLayout(bool clockwise, const QString& screenId)
{
    qCDebug(lcCore) << "SnapEngine::rotateWindowsInLayout: clockwise=" << clockwise << "screen=" << screenId;
    if (!m_wta || !m_windowTracker || !m_layoutManager) {
        return;
    }
    QVector<ZoneAssignmentEntry> entries = m_windowTracker->calculateRotation(clockwise, screenId);
    if (entries.isEmpty()) {
        auto* layout = m_layoutManager->resolveLayoutForScreen(screenId);
        if (!layout) {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_active_layout"), QString(),
                                      QString(), screenId);
        } else if (layout->zoneCount() < 2) {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("single_zone"), QString(),
                                      QString(), screenId);
        } else {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_snapped_windows"), QString(),
                                      QString(), screenId);
        }
        return;
    }
    processBatchEntries(this, m_wta, entries, QStringLiteral("rotate"));
    const QString direction = clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise");
    const QString reason = QStringLiteral("%1:%2").arg(direction).arg(entries.size());
    Q_EMIT navigationFeedback(true, QStringLiteral("rotate"), reason, entries.first().sourceZoneId,
                              entries.first().targetZoneId, screenId);
}

// Note: resnapToNewLayout() and resnapCurrentAssignments(const QString&)
// live in snapengine/navigation.cpp. They existed before this file and use
// the emitBatchedResnap → resnapToNewLayoutRequested → WTA::handleBatchedResnap
// pipeline, which remains the canonical batch-resnap path.

} // namespace PlasmaZones
