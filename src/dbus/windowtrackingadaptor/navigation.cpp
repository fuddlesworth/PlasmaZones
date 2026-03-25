// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "internal.h"
#include "../../snap/SnapEngine.h"
#include "../../core/logging.h"
#include "../../core/geometryutils.h"
#include "../../core/layoutmanager.h"
#include "../../core/layout.h"
#include "../../core/screenmanager.h"

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
        if (!storedScreen.isEmpty() && Utils::findScreenByIdOrName(storedScreen)) {
            return storedScreen;
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

    QString screenId = resolveNavScreen(this, m_lastActiveWindowId, m_service);
    QString resultJson = getMoveTargetForWindow(m_lastActiveWindowId, direction, screenId);

    // getMoveTargetForWindow already emits navigationFeedback on failure
    QJsonDocument doc = QJsonDocument::fromJson(resultJson.toUtf8());
    QJsonObject obj = doc.object();
    if (!obj.value(QLatin1String("success")).toBool()) {
        return;
    }

    QString zoneId = obj.value(QLatin1String("zoneId")).toString();
    QString geometryJson = obj.value(QLatin1String("geometryJson")).toString();
    QString sourceZoneId = obj.value(QLatin1String("sourceZoneId")).toString();
    QString effectiveScreen = obj.value(QLatin1String("screenName")).toString();

    // Handle snap bookkeeping internally (pre-snap geometry is stored by the
    // effect in slotApplyGeometryRequested via ensurePreSnapGeometryStored)
    windowSnapped(m_lastActiveWindowId, zoneId, effectiveScreen);
    recordSnapIntent(m_lastActiveWindowId, true);

    // Tell effect to apply the geometry
    Q_EMIT applyGeometryRequested(m_lastActiveWindowId, geometryJson, zoneId, effectiveScreen);
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
    QString resultJson = getFocusTargetForWindow(m_lastActiveWindowId, direction, screenId);

    QJsonDocument doc = QJsonDocument::fromJson(resultJson.toUtf8());
    QJsonObject obj = doc.object();
    if (!obj.value(QLatin1String("success")).toBool()) {
        return; // getFocusTargetForWindow already emitted feedback
    }

    QString targetWindowId = obj.value(QLatin1String("windowIdToActivate")).toString();
    if (!targetWindowId.isEmpty()) {
        Q_EMIT activateWindowRequested(targetWindowId);
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

    QString effectiveScreen = screenId.isEmpty() ? resolveNavScreen(this, m_lastActiveWindowId, m_service) : screenId;
    QString resultJson = getPushTargetForWindow(m_lastActiveWindowId, effectiveScreen);

    QJsonDocument doc = QJsonDocument::fromJson(resultJson.toUtf8());
    QJsonObject obj = doc.object();
    if (!obj.value(QLatin1String("success")).toBool()) {
        return; // getPushTargetForWindow already emitted feedback
    }

    QString zoneId = obj.value(QLatin1String("zoneId")).toString();
    QString geometryJson = obj.value(QLatin1String("geometryJson")).toString();

    windowSnapped(m_lastActiveWindowId, zoneId, effectiveScreen);
    recordSnapIntent(m_lastActiveWindowId, true);

    Q_EMIT applyGeometryRequested(m_lastActiveWindowId, geometryJson, zoneId, effectiveScreen);
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
    QString resultJson = getRestoreForWindow(m_lastActiveWindowId, screenId);

    QJsonDocument doc = QJsonDocument::fromJson(resultJson.toUtf8());
    QJsonObject obj = doc.object();
    if (!obj.value(QLatin1String("success")).toBool()) {
        return; // getRestoreForWindow already emitted feedback
    }

    int x = obj.value(QLatin1String("x")).toInt();
    int y = obj.value(QLatin1String("y")).toInt();
    int w = obj.value(QLatin1String("width")).toInt();
    int h = obj.value(QLatin1String("height")).toInt();

    // Unsnap the window and clear pre-tile geometry
    windowUnsnapped(m_lastActiveWindowId);
    clearPreTileGeometry(m_lastActiveWindowId);

    // Emit with empty zoneId = restore (no snap)
    QString geometryJson = GeometryUtils::rectToJson(QRect(x, y, w, h));
    Q_EMIT applyGeometryRequested(m_lastActiveWindowId, geometryJson, QString(), screenId);
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

    QString screenId = resolveNavScreen(this, m_lastActiveWindowId, m_service);
    QString resultJson = getSwapTargetForWindow(m_lastActiveWindowId, direction, screenId);

    QJsonDocument doc = QJsonDocument::fromJson(resultJson.toUtf8());
    QJsonObject obj = doc.object();
    if (!obj.value(QLatin1String("success")).toBool()) {
        return; // getSwapTargetForWindow already emitted feedback
    }

    QString windowId1 = obj.value(QLatin1String("windowId1")).toString();
    QString zoneId1 = obj.value(QLatin1String("zoneId1")).toString();
    int x1 = obj.value(QLatin1String("x1")).toInt();
    int y1 = obj.value(QLatin1String("y1")).toInt();
    int w1 = obj.value(QLatin1String("w1")).toInt();
    int h1 = obj.value(QLatin1String("h1")).toInt();
    QString effectiveScreen = obj.value(QLatin1String("screenName")).toString();

    // Move window 1 to target zone
    windowSnapped(windowId1, zoneId1, effectiveScreen);
    recordSnapIntent(windowId1, true);
    Q_EMIT applyGeometryRequested(windowId1, GeometryUtils::rectToJson(QRect(x1, y1, w1, h1)), zoneId1,
                                  effectiveScreen);

    // If there's a second window (swap, not move-to-empty), move it to the source zone
    QString windowId2 = obj.value(QLatin1String("windowId2")).toString();
    if (!windowId2.isEmpty()) {
        QString zoneId2 = obj.value(QLatin1String("zoneId2")).toString();
        int x2 = obj.value(QLatin1String("x2")).toInt();
        int y2 = obj.value(QLatin1String("y2")).toInt();
        int w2 = obj.value(QLatin1String("w2")).toInt();
        int h2 = obj.value(QLatin1String("h2")).toInt();

        windowSnapped(windowId2, zoneId2, effectiveScreen);
        recordSnapIntent(windowId2, true);
        Q_EMIT applyGeometryRequested(windowId2, GeometryUtils::rectToJson(QRect(x2, y2, w2, h2)), zoneId2,
                                      effectiveScreen);
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

    QString effectiveScreen = screenId.isEmpty() ? resolveNavScreen(this, m_lastActiveWindowId, m_service) : screenId;
    QString resultJson = getSnapToZoneByNumberTarget(m_lastActiveWindowId, zoneNumber, effectiveScreen);

    QJsonDocument doc = QJsonDocument::fromJson(resultJson.toUtf8());
    QJsonObject obj = doc.object();
    if (!obj.value(QLatin1String("success")).toBool()) {
        return; // getSnapToZoneByNumberTarget already emitted feedback
    }

    QString zoneId = obj.value(QLatin1String("zoneId")).toString();
    QString geometryJson = obj.value(QLatin1String("geometryJson")).toString();

    windowSnapped(m_lastActiveWindowId, zoneId, effectiveScreen);
    recordSnapIntent(m_lastActiveWindowId, true);

    Q_EMIT applyGeometryRequested(m_lastActiveWindowId, geometryJson, zoneId, effectiveScreen);
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
            // Detect screen from zone geometry center (virtual-screen-aware)
            QString screenId;
            QPoint center = entry.targetGeometry.center();
            auto* mgr = ScreenManager::instance();
            if (mgr) {
                screenId = mgr->effectiveScreenAt(center);
            }
            if (screenId.isEmpty()) {
                // Fallback: no ScreenManager or point not in any effective screen
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

    // Serialize and emit batch for effect to apply geometries
    QString batchJson = GeometryUtils::serializeZoneAssignments(entries);
    Q_EMIT adaptor->applyGeometriesBatch(batchJson, action);
    return true;
}

void WindowTrackingAdaptor::rotateWindowsInLayout(bool clockwise, const QString& screenId)
{
    qCDebug(lcDbusWindow) << "rotateWindowsInLayout: clockwise=" << clockwise << "screen=" << screenId;

    QVector<ZoneAssignmentEntry> entries = m_service->calculateRotation(clockwise, screenId);

    if (entries.isEmpty()) {
        // Emit feedback for empty rotation (mirrors SnapEngine::rotateWindows logic)
        auto* layout = m_layoutManager->resolveLayoutForScreen(Utils::screenIdForName(screenId));
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
    QString resultJson = getCycleTargetForWindow(m_lastActiveWindowId, forward, screenId);

    QJsonDocument doc = QJsonDocument::fromJson(resultJson.toUtf8());
    QJsonObject obj = doc.object();
    if (!obj.value(QLatin1String("success")).toBool()) {
        return; // getCycleTargetForWindow already emitted feedback
    }

    QString targetWindowId = obj.value(QLatin1String("windowIdToActivate")).toString();
    if (!targetWindowId.isEmpty()) {
        Q_EMIT activateWindowRequested(targetWindowId);
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
                                                            const QString& geometryJson)
{
    qCDebug(lcDbusWindow) << "requestMoveSpecificWindowToZone: window=" << windowId << "zone=" << zoneId;
    Q_EMIT moveSpecificWindowToZoneRequested(windowId, zoneId, geometryJson);
}

QString WindowTrackingAdaptor::calculateSnapAllWindows(const QStringList& windowIds, const QString& screenId)
{
    qCDebug(lcDbusWindow) << "calculateSnapAllWindows: count=" << windowIds.size() << "screen=" << screenId;
    if (m_snapEngine) {
        return m_snapEngine->calculateSnapAllWindows(windowIds, screenId);
    }
    return QStringLiteral("[]");
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

} // namespace PlasmaZones
