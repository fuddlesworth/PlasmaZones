// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../SnapEngine.h"
#include "core/geometryutils.h"
#include <PhosphorZones/Layout.h>
#include "core/layoutmanager.h"
#include "core/logging.h"
#include "core/types.h"
#include "core/utils.h"
#include "core/windowtrackingservice.h"
#include <PhosphorZones/Zone.h>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Snap-mode resnap coordination
//
// SnapEngine does NOT implement per-window navigation (focus/swap/rotate/move).
// Snap navigation is driven entirely by WindowTrackingAdaptor on the daemon
// side: it computes zone geometry via the target helpers in
// src/dbus/windowtrackingadaptor/targets.cpp and emits applyGeometryRequested.
//
// The IEngineLifecycle interface (iwindowengine.h) is deliberately narrowed
// to lifecycle events only — lifecycle is the only set of operations where
// both engines have meaningful implementations. Navigation is autotile-
// specific and the daemon dispatches it on the concrete AutotileEngine
// pointer, not polymorphically.
//
// What remains here is snap-mode batch operations (layout switch resnap,
// current-assignment resnap, autotile → snap transition resnap) that don't
// fit into navigation or lifecycle categories — they're coordination calls
// the daemon triggers explicitly on SnapEngine in response to layout or
// mode changes.
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::resnapToNewLayout()
{
    qCDebug(lcCore) << "resnapToNewLayout: calculating entries from previous layout buffer";
    QVector<ZoneAssignmentEntry> resnapEntries = m_windowTracker->calculateResnapFromPreviousLayout();

    if (resnapEntries.isEmpty()) {
        Layout* layout = m_layoutManager->activeLayout();
        if (!layout) {
            qCWarning(lcCore) << "resnapToNewLayout: no active layout";
            Q_EMIT navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_active_layout"), QString(),
                                      QString(), m_lastActiveScreenId);
        } else {
            qCWarning(lcCore) << "resnapToNewLayout: buffer empty, activeLayout=" << layout->name()
                              << "zones=" << layout->zoneCount();
            Q_EMIT navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_windows_to_resnap"),
                                      QString(), QString(), m_lastActiveScreenId);
        }
        return;
    }

    QString resnapData = GeometryUtils::serializeZoneAssignments(resnapEntries);
    qCInfo(lcCore) << "Resnapping" << resnapEntries.size() << "windows to new layout";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

void SnapEngine::resnapCurrentAssignments(const QString& screenFilter)
{
    QVector<ZoneAssignmentEntry> entries = m_windowTracker->calculateResnapFromCurrentAssignments(screenFilter);

    if (entries.isEmpty()) {
        qCDebug(lcCore) << "No windows to resnap from current assignments";
        Q_EMIT navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_windows_to_resnap"), QString(),
                                  QString(), screenFilter.isEmpty() ? m_lastActiveScreenId : screenFilter);
        return;
    }

    QString resnapData = GeometryUtils::serializeZoneAssignments(entries);
    qCInfo(lcCore) << "Resnapping" << entries.size() << "windows to current zone assignments";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

void SnapEngine::resnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenId)
{
    QVector<ZoneAssignmentEntry> entries = calculateResnapEntriesFromAutotileOrder(autotileWindowOrder, screenId);

    if (entries.isEmpty()) {
        return; // calculateResnapEntriesFromAutotileOrder already tried fallback
    }

    QString resnapData = GeometryUtils::serializeZoneAssignments(entries);
    qCInfo(lcCore) << "Resnapping" << entries.size() << "windows from autotile order";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

QVector<ZoneAssignmentEntry> SnapEngine::calculateResnapEntriesFromAutotileOrder(const QStringList& autotileWindowOrder,
                                                                                 const QString& screenId)
{
    QVector<ZoneAssignmentEntry> entries =
        m_windowTracker->calculateResnapFromAutotileOrder(autotileWindowOrder, screenId);

    if (entries.isEmpty()) {
        qCDebug(lcCore) << "calculateResnapEntriesFromAutotileOrder: no entries from autotile order,"
                        << "falling back to current assignments for screen" << screenId;
        entries = m_windowTracker->calculateResnapFromCurrentAssignments(screenId);
    }

    return entries;
}

void SnapEngine::emitBatchedResnap(const QVector<ZoneAssignmentEntry>& entries)
{
    if (entries.isEmpty()) {
        return;
    }
    QString resnapData = GeometryUtils::serializeZoneAssignments(entries);
    qCInfo(lcCore) << "Emitting batched resnap for" << entries.size() << "windows";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

SnapAllResultList SnapEngine::calculateSnapAllWindows(const QStringList& windowIds, const QString& screenId)
{
    QVector<ZoneAssignmentEntry> entries = m_windowTracker->calculateSnapAllWindows(windowIds, screenId);

    SnapAllResultList result;
    result.reserve(entries.size());
    for (const auto& entry : entries) {
        SnapAllResultEntry r;
        r.windowId = entry.windowId;
        r.targetZoneId = entry.targetZoneId;
        r.sourceZoneId = entry.sourceZoneId;
        r.x = entry.targetGeometry.x();
        r.y = entry.targetGeometry.y();
        r.width = entry.targetGeometry.width();
        r.height = entry.targetGeometry.height();
        result.append(r);
    }

    qCDebug(lcCore) << "Calculated snap-all for" << result.size() << "windows";
    return result;
}

void SnapEngine::snapAllWindows(const QString& screenId)
{
    qCDebug(lcCore) << "snapAllWindows called for screen=" << screenId;
    Q_EMIT snapAllWindowsRequested(screenId);
}

} // namespace PlasmaZones
