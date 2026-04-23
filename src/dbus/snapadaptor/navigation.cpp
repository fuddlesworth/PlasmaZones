// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../snapadaptor.h"
#include "../windowtrackingadaptor.h"
#include "../../core/logging.h"
#include "../../core/windowtrackingservice.h"
#include "../../snap/SnapEngine.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Snap-mode navigation D-Bus slots — thin forwarders to SnapEngine.
//
// Moved from WindowTrackingAdaptor::navigation.cpp to complete the D-Bus
// surface split. The bodies are identical: guard on m_engine, forward with
// a default NavigationContext.
// ═══════════════════════════════════════════════════════════════════════════════

void SnapAdaptor::moveWindowToAdjacentZone(const QString& direction)
{
    if (m_engine) {
        m_engine->moveFocusedInDirection(direction, NavigationContext{});
    }
}

void SnapAdaptor::focusAdjacentZone(const QString& direction)
{
    if (m_engine) {
        m_engine->focusInDirection(direction, NavigationContext{});
    }
}

void SnapAdaptor::pushToEmptyZone(const QString& screenId)
{
    if (m_engine) {
        m_engine->pushFocusedToEmptyZone(NavigationContext{QString(), screenId});
    }
}

void SnapAdaptor::restoreWindowSize()
{
    if (m_engine) {
        m_engine->restoreFocusedWindow(NavigationContext{});
    }
}

void SnapAdaptor::swapWindowWithAdjacentZone(const QString& direction)
{
    if (m_engine) {
        m_engine->swapFocusedInDirection(direction, NavigationContext{});
    }
}

void SnapAdaptor::snapToZoneByNumber(int zoneNumber, const QString& screenId)
{
    if (m_engine) {
        m_engine->moveFocusedToPosition(zoneNumber, NavigationContext{QString(), screenId});
    }
}

void SnapAdaptor::rotateWindowsInLayout(bool clockwise, const QString& screenId)
{
    if (m_engine) {
        m_engine->rotateWindowsInLayout(clockwise, screenId);
    }
}

void SnapAdaptor::cycleWindowsInZone(bool forward)
{
    if (m_engine) {
        m_engine->cycleFocus(forward, NavigationContext{});
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Batch helper used by methods that operate on pre-built
// ZoneAssignmentEntry vectors (resnapForVirtualScreenReconfigure,
// handleBatchedResnap). Thin wrapper over
// SnapEngine::applyBatchAssignments.
// ═══════════════════════════════════════════════════════════════════════════════
static bool processBatchEntries(WindowTrackingAdaptor* wta, SnapEngine* engine,
                                const QVector<ZoneAssignmentEntry>& entries, const QString& action)
{
    if (!engine || !wta) {
        return false;
    }

    WindowGeometryList geometries =
        engine->applyBatchAssignments(entries, SnapIntent::UserInitiated, [wta]() -> QString {
            const QString cursor = wta->lastCursorScreenName();
            return cursor.isEmpty() ? wta->lastActiveScreenName() : cursor;
        });

    if (geometries.isEmpty()) {
        return false;
    }
    Q_EMIT wta->applyGeometriesBatch(geometries, action);
    return true;
}

QStringList SnapAdaptor::resolveSnapModeScreensForResnap(const QString& screenFilter) const
{
    if (!m_adaptor) {
        return {};
    }
    return m_adaptor->resolveSnapModeScreensForResnap(screenFilter);
}

void SnapAdaptor::resnapToNewLayout()
{
    if (m_engine) {
        m_engine->resnapToNewLayout();
    }
}

void SnapAdaptor::resnapCurrentAssignments(const QString& screenFilter)
{
    if (m_engine) {
        m_engine->resnapCurrentAssignments(screenFilter);
    }
}

void SnapAdaptor::resnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenId)
{
    qCDebug(lcDbusWindow) << "resnapFromAutotileOrder: count=" << autotileWindowOrder.size() << "screen=" << screenId;

    if (m_engine) {
        QVector<ZoneAssignmentEntry> entries =
            m_engine->calculateResnapEntriesFromAutotileOrder(autotileWindowOrder, screenId);
        if (!entries.isEmpty()) {
            processBatchEntries(m_adaptor, m_engine, entries, QStringLiteral("resnap"));
        }
    }
}

void SnapAdaptor::resnapForVirtualScreenReconfigure(const QString& physicalScreenId)
{
    qCDebug(lcDbusWindow) << "resnapForVirtualScreenReconfigure: physId=" << physicalScreenId;

    if (!m_engine) {
        return;
    }

    const QStringList snapScreens = resolveSnapModeScreensForResnap(physicalScreenId);
    QVector<ZoneAssignmentEntry> entries;
    for (const QString& sid : snapScreens) {
        entries.append(m_engine->calculateResnapFromCurrentAssignments(sid));
    }

    if (entries.isEmpty()) {
        return;
    }

    // Tagged "vs_reconfigure" so the kwin-effect does NOT fire snap-assist
    // continuation — no user-initiated snap happened here, windows are just
    // following their VS's new geometry after a swap/rotate/split edit.
    processBatchEntries(m_adaptor, m_engine, entries, QStringLiteral("vs_reconfigure"));
}

SnapAllResultList SnapAdaptor::calculateSnapAllWindows(const QStringList& windowIds, const QString& screenId)
{
    qCDebug(lcDbusWindow) << "calculateSnapAllWindows: count=" << windowIds.size() << "screen=" << screenId;
    if (m_engine) {
        return m_engine->calculateSnapAllWindows(windowIds, screenId);
    }
    return {};
}

void SnapAdaptor::snapAllWindows(const QString& screenId)
{
    qCDebug(lcDbusWindow) << "snapAllWindows: screen=" << screenId;
    if (m_engine) {
        m_engine->snapAllWindows(screenId);
    }
}

void SnapAdaptor::handleBatchedResnap(const QString& resnapData)
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

    processBatchEntries(m_adaptor, m_engine, entries, QStringLiteral("resnap"));
}

} // namespace PlasmaZones
