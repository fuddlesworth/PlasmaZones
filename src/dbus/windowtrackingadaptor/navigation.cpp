// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "internal.h"
#include "../../snap/SnapEngine.h"
#include "../../core/logging.h"
#include "../../core/layoutmanager.h"
#include "../../core/layout.h"
#include "../../core/screenmanager.h"
#include "../../core/utils.h"
#include "../../core/virtualscreen.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon-driven navigation: compute geometry internally and emit
// applyGeometryRequested / activateWindowRequested. The effect just applies.
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Resolve the screen to use for navigation.
 *
 * For snapped windows, trusts the stored screen assignment (avoids
 * same-model multi-monitor mismatch). Otherwise uses cursor screen
 * with active-window fallback.
 */
static QString resolveNavScreen(const WindowTrackingAdaptor* adaptor, const QString& windowId,
                                WindowTrackingService* service)
{
    // If window is snapped, use stored screen (authoritative)
    QString zoneId = service->zoneForWindow(windowId);
    if (!zoneId.isEmpty()) {
        QString storedScreen = service->screenAssignments().value(windowId);
        if (!storedScreen.isEmpty()) {
            // For virtual screen IDs, verify the backing physical screen exists
            if (VirtualScreenId::isVirtual(storedScreen)) {
                QString physId = VirtualScreenId::extractPhysicalId(storedScreen);
                QScreen* physScreen = Utils::findScreenByIdOrName(physId);
                if (physScreen) {
                    // Physical screen exists — verify the virtual screen itself still
                    // exists (VS config may have changed, e.g. 3 VS → 2 VS)
                    auto* mgr = ScreenManager::instance();
                    if (mgr && mgr->effectiveScreenIds().contains(storedScreen)) {
                        return storedScreen; // Virtual screen is valid, return as-is
                    }
                    // Virtual screen gone (config changed), fall through
                }
                // Physical screen gone, fall through to cursor-based resolution
            } else if (Utils::findScreenByIdOrName(storedScreen)) {
                return storedScreen;
            }
        }
    }
    // Cursor screen (primary), active-window screen (fallback)
    QString screen = adaptor->lastCursorScreenName();
    if (screen.isEmpty()) {
        screen = adaptor->lastActiveScreenName();
    }
    return screen;
}

void WindowTrackingAdaptor::moveWindowToAdjacentZone(const QString& direction)
{
    qCInfo(lcDbusWindow) << "moveWindowToAdjacentZone: direction=" << direction;

    if (!validateDirection(direction, QStringLiteral("move"))) {
        return;
    }

    if (m_lastActiveWindowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("no_window"), QString(), QString(),
                                  m_lastActiveScreenId);
        return;
    }

    if (isWindowExcluded(m_lastActiveWindowId, QStringLiteral("move"))) {
        return;
    }

    QString screenId = resolveNavScreen(this, m_lastActiveWindowId, m_service);
    MoveTargetResult result = getMoveTargetForWindow(m_lastActiveWindowId, direction, screenId);

    // getMoveTargetForWindow already emits navigationFeedback on failure
    if (!result.success) {
        return;
    }

    QRect geo = result.toRect();
    if (!geo.isValid()) {
        qCWarning(lcDbusWindow) << "moveWindowToAdjacentZone: invalid geometry from nav result";
        return;
    }

    // Handle snap bookkeeping internally (pre-snap geometry is stored by the
    // effect in slotApplyGeometryRequested via ensurePreSnapGeometryStored)
    windowSnapped(m_lastActiveWindowId, result.zoneId, result.screenName);
    recordSnapIntent(m_lastActiveWindowId, true);

    // Tell effect to apply the geometry
    Q_EMIT applyGeometryRequested(m_lastActiveWindowId, geo.x(), geo.y(), geo.width(), geo.height(), result.zoneId,
                                  result.screenName, false);
}

void WindowTrackingAdaptor::focusAdjacentZone(const QString& direction)
{
    qCInfo(lcDbusWindow) << "focusAdjacentZone: direction=" << direction;

    if (!validateDirection(direction, QStringLiteral("focus"))) {
        return;
    }

    if (m_lastActiveWindowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window"), QString(), QString(),
                                  m_lastActiveScreenId);
        return;
    }

    QString screenId = resolveNavScreen(this, m_lastActiveWindowId, m_service);
    FocusTargetResult result = getFocusTargetForWindow(m_lastActiveWindowId, direction, screenId);

    if (!result.success) {
        return; // getFocusTargetForWindow already emitted feedback
    }

    if (!result.windowIdToActivate.isEmpty()) {
        Q_EMIT activateWindowRequested(result.windowIdToActivate);
    }
}

void WindowTrackingAdaptor::pushToEmptyZone(const QString& screenId)
{
    qCInfo(lcDbusWindow) << "pushToEmptyZone: screen=" << screenId;

    if (m_lastActiveWindowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("push"), QStringLiteral("no_window"), QString(), QString(),
                                  screenId.isEmpty() ? m_lastActiveScreenId : screenId);
        return;
    }

    if (isWindowExcluded(m_lastActiveWindowId, QStringLiteral("push"))) {
        return;
    }

    QString effectiveScreen = screenId.isEmpty() ? resolveNavScreen(this, m_lastActiveWindowId, m_service) : screenId;
    MoveTargetResult result = getPushTargetForWindow(m_lastActiveWindowId, effectiveScreen);

    if (!result.success) {
        return; // getPushTargetForWindow already emitted feedback
    }

    QRect geo = result.toRect();
    if (!geo.isValid()) {
        qCWarning(lcDbusWindow) << "pushToEmptyZone: invalid geometry from nav result";
        return;
    }

    windowSnapped(m_lastActiveWindowId, result.zoneId, effectiveScreen);
    recordSnapIntent(m_lastActiveWindowId, true);

    Q_EMIT applyGeometryRequested(m_lastActiveWindowId, geo.x(), geo.y(), geo.width(), geo.height(), result.zoneId,
                                  effectiveScreen, false);
}

void WindowTrackingAdaptor::restoreWindowSize()
{
    qCInfo(lcDbusWindow) << "restoreWindowSize";

    if (m_lastActiveWindowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("restore"), QStringLiteral("no_window"), QString(), QString(),
                                  m_lastActiveScreenId);
        return;
    }

    QString screenId = resolveNavScreen(this, m_lastActiveWindowId, m_service);
    RestoreTargetResult result = getRestoreForWindow(m_lastActiveWindowId, screenId);

    if (!result.success) {
        return; // getRestoreForWindow already emitted feedback
    }

    // Unsnap the window and clear pre-tile geometry
    windowUnsnapped(m_lastActiveWindowId);
    clearPreTileGeometry(m_lastActiveWindowId);

    // Emit with empty zoneId = restore (no snap)
    Q_EMIT applyGeometryRequested(m_lastActiveWindowId, result.x, result.y, result.width, result.height, QString(),
                                  screenId, false);
}

void WindowTrackingAdaptor::toggleWindowFloat()
{
    qCInfo(lcDbusWindow) << "toggleWindowFloat";
    // Delegate to toggleFloatForWindow which already handles the full flow
    // (pre-snap geometry, applyGeometryRequested, navigationFeedback)
    Q_EMIT toggleWindowFloatRequested(true);
}

void WindowTrackingAdaptor::swapWindowWithAdjacentZone(const QString& direction)
{
    qCInfo(lcDbusWindow) << "swapWindowWithAdjacentZone: direction=" << direction;

    if (!validateDirection(direction, QStringLiteral("swap"))) {
        return;
    }

    if (m_lastActiveWindowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("no_window"), QString(), QString(),
                                  m_lastActiveScreenId);
        return;
    }

    if (isWindowExcluded(m_lastActiveWindowId, QStringLiteral("swap"))) {
        return;
    }

    QString screenId = resolveNavScreen(this, m_lastActiveWindowId, m_service);
    SwapTargetResult result = getSwapTargetForWindow(m_lastActiveWindowId, direction, screenId);

    if (!result.success) {
        return; // getSwapTargetForWindow already emitted feedback
    }

    // Move window 1 to target zone
    windowSnapped(result.windowId1, result.zoneId1, result.screenName);
    recordSnapIntent(result.windowId1, true);
    Q_EMIT applyGeometryRequested(result.windowId1, result.x1, result.y1, result.w1, result.h1, result.zoneId1,
                                  result.screenName, false);

    // If there's a second window (swap, not move-to-empty), move it to the source zone.
    // Prefer window2's stored screen assignment, not window1's screen, so cross-VS swaps
    // assign each window to the correct virtual screen.
    if (!result.windowId2.isEmpty()) {
        QString screen2 = m_service->screenAssignments().value(result.windowId2);
        if (screen2.isEmpty()) {
            screen2 = result.screenName;
        }

        windowSnapped(result.windowId2, result.zoneId2, screen2);
        recordSnapIntent(result.windowId2, true);
        Q_EMIT applyGeometryRequested(result.windowId2, result.x2, result.y2, result.w2, result.h2, result.zoneId2,
                                      screen2, false);
    }
}

void WindowTrackingAdaptor::snapToZoneByNumber(int zoneNumber, const QString& screenId)
{
    qCInfo(lcDbusWindow) << "snapToZoneByNumber: zoneNumber=" << zoneNumber << "screen=" << screenId;

    if (zoneNumber < 1 || zoneNumber > 9) {
        qCWarning(lcDbusWindow) << "Invalid zone number:" << zoneNumber << "(must be 1-9)";
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(),
                                  QString(), QString());
        return;
    }

    if (m_lastActiveWindowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("no_window"), QString(), QString(),
                                  screenId.isEmpty() ? m_lastActiveScreenId : screenId);
        return;
    }

    if (isWindowExcluded(m_lastActiveWindowId, QStringLiteral("snap"))) {
        return;
    }

    QString effectiveScreen = screenId.isEmpty() ? resolveNavScreen(this, m_lastActiveWindowId, m_service) : screenId;
    MoveTargetResult result = getSnapToZoneByNumberTarget(m_lastActiveWindowId, zoneNumber, effectiveScreen);

    if (!result.success) {
        return; // getSnapToZoneByNumberTarget already emitted feedback
    }

    QRect geo = result.toRect();
    if (!geo.isValid()) {
        qCWarning(lcDbusWindow) << "snapToZoneByNumber: invalid geometry from nav result";
        return;
    }

    windowSnapped(m_lastActiveWindowId, result.zoneId, effectiveScreen);
    recordSnapIntent(m_lastActiveWindowId, true);

    Q_EMIT applyGeometryRequested(m_lastActiveWindowId, geo.x(), geo.y(), geo.width(), geo.height(), result.zoneId,
                                  effectiveScreen, false);
}

/**
 * @brief Process a vector of ZoneAssignmentEntry: call windowSnapped for each, emit applyGeometriesBatch.
 *
 * Shared by rotate, resnap, and snap-all. Handles bookkeeping (zone assignment,
 * floating state clear, snap intent recording) on the daemon side so the effect
 * only needs to apply geometries.
 *
 * @param entries Zone assignment entries to process
 * @param action Action name for the applyGeometriesBatch signal ("rotate", "resnap", "snap_all")
 * @return true if entries were processed and signal emitted
 */
static bool processBatchEntries(WindowTrackingAdaptor* adaptor, const QVector<ZoneAssignmentEntry>& entries,
                                const QString& action)
{
    if (entries.isEmpty()) {
        return false;
    }

    // Process bookkeeping for each entry
    for (const auto& entry : entries) {
        if (entry.targetZoneId == QLatin1String("__restore__")) {
            adaptor->windowUnsnapped(entry.windowId);
            adaptor->clearPreTileGeometry(entry.windowId);
        } else {
            // If the caller stamped an authoritative target screen, trust it.
            // This is the safe path for resnap-from-stored callers, where
            // re-deriving from geometry.center() risks landing in a sibling
            // virtual screen if the layout geometry resolved against a stale
            // or fallback rect. Empty means "no caller-known target" — legacy
            // callers like rotate / snap-all that compute geometry without
            // knowing the target screen — and falls through to the geometry-
            // based resolution below.
            QString screenId = entry.targetScreenId;
            QPoint center = entry.targetGeometry.center();
            auto* mgr = ScreenManager::instance();
            if (screenId.isEmpty() && mgr) {
                screenId = mgr->effectiveScreenAt(center);
            }
            if (screenId.isEmpty()) {
                // Final fallback: walk QGuiApplication's screens manually.
                // This path is hit only when both targetScreenId and
                // effectiveScreenAt(center) returned empty — typically when
                // ScreenManager hasn't been initialized yet (very early in
                // daemon lifecycle) or when the geometry's center falls
                // outside all known screens. effectiveScreenAt above already
                // covers the VS-aware case, so we just need a physical fall-
                // through here — no need to re-attempt VS resolution.
                for (QScreen* screen : QGuiApplication::screens()) {
                    if (screen->geometry().contains(center)) {
                        screenId = Utils::screenIdentifier(screen);
                        break;
                    }
                }
            }
            // Fallback: use cursor/active screen if geometry doesn't resolve to a screen
            // (possible if zone geometries reference a disconnected monitor)
            if (screenId.isEmpty()) {
                screenId = adaptor->lastCursorScreenName();
                if (screenId.isEmpty()) {
                    screenId = adaptor->lastActiveScreenName();
                }
            }
            if (entry.targetZoneIds.size() > 1) {
                adaptor->windowSnappedMultiZone(entry.windowId, entry.targetZoneIds, screenId);
            } else {
                adaptor->windowSnapped(entry.windowId, entry.targetZoneId, screenId);
            }
        }
    }

    // Build struct list and emit batch for effect to apply geometries
    WindowGeometryList geometries;
    geometries.reserve(entries.size());
    for (const ZoneAssignmentEntry& entry : entries) {
        geometries.append(WindowGeometryEntry::fromRect(entry.windowId, entry.targetGeometry));
    }
    Q_EMIT adaptor->applyGeometriesBatch(geometries, action);
    return true;
}

void WindowTrackingAdaptor::rotateWindowsInLayout(bool clockwise, const QString& screenId)
{
    qCDebug(lcDbusWindow) << "rotateWindowsInLayout: clockwise=" << clockwise << "screen=" << screenId;

    QVector<ZoneAssignmentEntry> entries = m_service->calculateRotation(clockwise, screenId);

    if (entries.isEmpty()) {
        // Emit feedback for empty rotation (mirrors SnapEngine::rotateWindows logic)
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

    processBatchEntries(this, entries, QStringLiteral("rotate"));

    QString direction = clockwise ? QStringLiteral("clockwise") : QStringLiteral("counterclockwise");
    QString reason = QStringLiteral("%1:%2").arg(direction).arg(entries.size());
    Q_EMIT navigationFeedback(true, QStringLiteral("rotate"), reason, entries.first().sourceZoneId,
                              entries.first().targetZoneId, screenId);
}

void WindowTrackingAdaptor::cycleWindowsInZone(bool forward)
{
    qCDebug(lcDbusWindow) << "cycleWindowsInZone: forward=" << forward;

    if (m_lastActiveWindowId.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("cycle"), QStringLiteral("no_window"), QString(), QString(),
                                  m_lastActiveScreenId);
        return;
    }

    QString screenId = resolveNavScreen(this, m_lastActiveWindowId, m_service);
    CycleTargetResult result = getCycleTargetForWindow(m_lastActiveWindowId, forward, screenId);

    if (!result.success) {
        return; // getCycleTargetForWindow already emitted feedback
    }

    if (!result.windowIdToActivate.isEmpty()) {
        Q_EMIT activateWindowRequested(result.windowIdToActivate);
    }
}

void WindowTrackingAdaptor::resnapToNewLayout()
{
    qCDebug(lcDbusWindow) << "resnapToNewLayout";

    QVector<ZoneAssignmentEntry> entries = m_service->calculateResnapFromPreviousLayout();

    if (entries.isEmpty()) {
        auto* layout = m_layoutManager->activeLayout();
        if (!layout) {
            Q_EMIT navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_active_layout"), QString(),
                                      QString(), m_lastActiveScreenId);
        } else {
            Q_EMIT navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_windows_to_resnap"),
                                      QString(), QString(), m_lastActiveScreenId);
        }
        return;
    }

    processBatchEntries(this, entries, QStringLiteral("resnap"));

    QString reason = QStringLiteral("resnap:%1").arg(entries.size());
    Q_EMIT navigationFeedback(true, QStringLiteral("resnap"), reason, QString(), entries.first().targetZoneId,
                              m_lastActiveScreenId);
}

void WindowTrackingAdaptor::resnapCurrentAssignments(const QString& screenFilter)
{
    qCDebug(lcDbusWindow) << "resnapCurrentAssignments: screen="
                          << (screenFilter.isEmpty() ? QStringLiteral("all") : screenFilter);

    QVector<ZoneAssignmentEntry> entries = m_service->calculateResnapFromCurrentAssignments(screenFilter);

    if (entries.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_windows_to_resnap"), QString(),
                                  QString(), screenFilter.isEmpty() ? m_lastActiveScreenId : screenFilter);
        return;
    }

    processBatchEntries(this, entries, QStringLiteral("resnap"));
}

void WindowTrackingAdaptor::resnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenId)
{
    qCDebug(lcDbusWindow) << "resnapFromAutotileOrder: count=" << autotileWindowOrder.size() << "screen=" << screenId;

    if (m_snapEngine) {
        // Use SnapEngine's fallback logic (autotile order → current assignments)
        QVector<ZoneAssignmentEntry> entries =
            m_snapEngine->calculateResnapEntriesFromAutotileOrder(autotileWindowOrder, screenId);
        if (!entries.isEmpty()) {
            processBatchEntries(this, entries, QStringLiteral("resnap"));
        }
    }
}

void WindowTrackingAdaptor::snapAllWindows(const QString& screenId)
{
    qCDebug(lcDbusWindow) << "snapAllWindows: screen=" << screenId;
    if (m_snapEngine) {
        m_snapEngine->snapAllWindows(screenId);
    }
}

void WindowTrackingAdaptor::requestMoveSpecificWindowToZone(const QString& windowId, const QString& zoneId,
                                                            const QRect& geometry)
{
    qCDebug(lcDbusWindow) << "requestMoveSpecificWindowToZone: window=" << windowId << "zone=" << zoneId;
    Q_EMIT moveSpecificWindowToZoneRequested(windowId, zoneId, geometry.x(), geometry.y(), geometry.width(),
                                             geometry.height());
}

SnapAllResultList WindowTrackingAdaptor::calculateSnapAllWindows(const QStringList& windowIds, const QString& screenId)
{
    qCDebug(lcDbusWindow) << "calculateSnapAllWindows: count=" << windowIds.size() << "screen=" << screenId;
    if (m_snapEngine) {
        return m_snapEngine->calculateSnapAllWindows(windowIds, screenId);
    }
    return {};
}

void WindowTrackingAdaptor::handleBatchedResnap(const QString& resnapData)
{
    qCDebug(lcDbusWindow) << "handleBatchedResnap: processing batched resnap from SnapEngine";

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(resnapData.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcDbusWindow) << "handleBatchedResnap: invalid JSON:" << parseError.errorString();
        return;
    }

    // Deserialize ZoneAssignmentEntry array
    QVector<ZoneAssignmentEntry> entries;
    const QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        ZoneAssignmentEntry entry;
        entry.windowId = obj.value(QLatin1String("windowId")).toString();
        entry.targetZoneId = obj.value(QLatin1String("targetZoneId")).toString();
        entry.sourceZoneId = obj.value(QLatin1String("sourceZoneId")).toString();
        // Deserialize multi-zone IDs (for zone-spanning windows)
        const QJsonArray zoneIdsArr = obj.value(QLatin1String("targetZoneIds")).toArray();
        if (!zoneIdsArr.isEmpty()) {
            for (const QJsonValue& v : zoneIdsArr)
                entry.targetZoneIds.append(v.toString());
        }
        entry.targetGeometry =
            QRect(obj.value(QLatin1String("x")).toInt(), obj.value(QLatin1String("y")).toInt(),
                  obj.value(QLatin1String("width")).toInt(), obj.value(QLatin1String("height")).toInt());
        if (!entry.windowId.isEmpty() && !entry.targetZoneId.isEmpty()) {
            entries.append(entry);
        }
    }

    processBatchEntries(this, entries, QStringLiteral("resnap"));
}

void WindowTrackingAdaptor::reportNavigationFeedback(bool success, const QString& action, const QString& reason,
                                                     const QString& sourceZoneId, const QString& targetZoneId,
                                                     const QString& screenId)
{
    qCDebug(lcDbusWindow) << "Navigation feedback: success=" << success << "action=" << action << "reason=" << reason
                          << "sourceZone=" << sourceZoneId << "targetZone=" << targetZoneId << "screen=" << screenId;
    Q_EMIT navigationFeedback(success, action, reason, sourceZoneId, targetZoneId, screenId);
}

bool WindowTrackingAdaptor::validateDirection(const QString& direction, const QString& action)
{
    if (direction.isEmpty()) {
        qCWarning(lcDbusWindow) << "Cannot" << action << "- empty direction";
        Q_EMIT navigationFeedback(false, action, QStringLiteral("invalid_direction"), QString(), QString(), QString());
        return false;
    }
    return true;
}

bool WindowTrackingAdaptor::isWindowExcluded(const QString& windowId, const QString& action)
{
    if (!m_settings) {
        return false;
    }

    const QString appId = Utils::extractAppId(windowId);
    for (const QString& excluded : m_settings->excludedApplications()) {
        if (Utils::appIdMatches(appId, excluded)) {
            qCInfo(lcDbusWindow) << action << ":" << windowId << "excluded by app rule:" << excluded;
            Q_EMIT navigationFeedback(false, action, QStringLiteral("excluded"), appId, QString(),
                                      m_lastActiveScreenId);
            return true;
        }
    }
    for (const QString& excluded : m_settings->excludedWindowClasses()) {
        if (Utils::appIdMatches(appId, excluded)) {
            qCInfo(lcDbusWindow) << action << ":" << windowId << "excluded by class rule:" << excluded;
            Q_EMIT navigationFeedback(false, action, QStringLiteral("excluded"), appId, QString(),
                                      m_lastActiveScreenId);
            return true;
        }
    }
    return false;
}

} // namespace PlasmaZones
