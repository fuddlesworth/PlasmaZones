// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../SnapEngine.h"
#include "core/windowtrackingservice.h"
#include "core/geometryutils.h"
#include "core/layoutmanager.h"
#include "core/layout.h"
#include "core/zone.h"
#include "core/types.h"
#include "core/logging.h"
#include "core/utils.h"

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// IWindowEngine navigation stubs
//
// These methods are part of the IWindowEngine interface (shared with AutotileEngine)
// but are no longer called for snap-mode screens. The Daemon routes snap-mode
// navigation through the WTA's daemon-driven methods instead. These stubs exist
// only to satisfy the pure virtual interface.
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::moveInDirection(const QString& direction)
{
    // Snap navigation is daemon-driven (WTA computes geometry, emits applyGeometryRequested).
    // This stub only validates the direction for tests that check navigationFeedback emission.
    if (direction.isEmpty()) {
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), m_lastActiveScreenId);
        return;
    }
    qCWarning(lcCore) << "SnapEngine::moveInDirection called but snap navigation is daemon-driven; use WTA";
}

void SnapEngine::pushToEmptyZone(const QString& screenId)
{
    Q_UNUSED(screenId)
    qCWarning(lcCore) << "SnapEngine::pushToEmptyZone called but snap navigation is daemon-driven; use WTA";
}

void SnapEngine::focusInDirection(const QString& direction, const QString& action)
{
    Q_UNUSED(direction)
    Q_UNUSED(action)
    qCWarning(lcCore) << "SnapEngine::focusInDirection called but snap navigation is daemon-driven; use WTA";
}

void SnapEngine::swapInDirection(const QString& direction, const QString& action)
{
    Q_UNUSED(direction)
    Q_UNUSED(action)
    qCWarning(lcCore) << "SnapEngine::swapInDirection called but snap navigation is daemon-driven; use WTA";
}

void SnapEngine::rotateWindows(bool clockwise, const QString& screenId)
{
    Q_UNUSED(clockwise)
    Q_UNUSED(screenId)
    qCWarning(lcCore) << "SnapEngine::rotateWindows called but snap navigation is daemon-driven; use WTA";
}

void SnapEngine::moveToPosition(const QString& windowId, int position, const QString& screenId)
{
    Q_UNUSED(windowId)
    Q_UNUSED(position)
    Q_UNUSED(screenId)
    qCWarning(lcCore) << "SnapEngine::moveToPosition called but snap navigation is daemon-driven; use WTA";
}

// ═══════════════════════════════════════════════════════════════════════════════
// Snap-specific methods (still active)
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::resnapToNewLayout()
{
    qCDebug(lcCore) << "resnapToNewLayout: calculating entries from previous layout buffer";
    QVector<RotationEntry> resnapEntries = m_windowTracker->calculateResnapFromPreviousLayout();

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

    QString resnapData = GeometryUtils::serializeRotationEntries(resnapEntries);
    qCInfo(lcCore) << "Resnapping" << resnapEntries.size() << "windows to new layout";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

void SnapEngine::resnapCurrentAssignments(const QString& screenFilter)
{
    QVector<RotationEntry> entries = m_windowTracker->calculateResnapFromCurrentAssignments(screenFilter);

    if (entries.isEmpty()) {
        qCDebug(lcCore) << "No windows to resnap from current assignments";
        Q_EMIT navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_windows_to_resnap"), QString(),
                                  QString(), screenFilter.isEmpty() ? m_lastActiveScreenId : screenFilter);
        return;
    }

    QString resnapData = GeometryUtils::serializeRotationEntries(entries);
    qCInfo(lcCore) << "Resnapping" << entries.size() << "windows to current zone assignments";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

void SnapEngine::resnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenId)
{
    QVector<RotationEntry> entries = calculateResnapEntriesFromAutotileOrder(autotileWindowOrder, screenId);

    if (entries.isEmpty()) {
        return; // calculateResnapEntriesFromAutotileOrder already tried fallback
    }

    QString resnapData = GeometryUtils::serializeRotationEntries(entries);
    qCInfo(lcCore) << "Resnapping" << entries.size() << "windows from autotile order";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

QVector<RotationEntry> SnapEngine::calculateResnapEntriesFromAutotileOrder(const QStringList& autotileWindowOrder,
                                                                           const QString& screenId)
{
    QVector<RotationEntry> entries = m_windowTracker->calculateResnapFromAutotileOrder(autotileWindowOrder, screenId);

    if (entries.isEmpty()) {
        qCDebug(lcCore) << "calculateResnapEntriesFromAutotileOrder: no entries from autotile order,"
                        << "falling back to current assignments for screen" << screenId;
        entries = m_windowTracker->calculateResnapFromCurrentAssignments(screenId);
    }

    return entries;
}

void SnapEngine::emitBatchedResnap(const QVector<RotationEntry>& entries)
{
    if (entries.isEmpty()) {
        return;
    }
    QString resnapData = GeometryUtils::serializeRotationEntries(entries);
    qCInfo(lcCore) << "Emitting batched resnap for" << entries.size() << "windows";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

QString SnapEngine::calculateSnapAllWindows(const QStringList& windowIds, const QString& screenId)
{
    QVector<RotationEntry> entries = m_windowTracker->calculateSnapAllWindows(windowIds, screenId);

    qCDebug(lcCore) << "Calculated snap-all for" << entries.size() << "windows";
    return GeometryUtils::serializeRotationEntries(entries);
}

void SnapEngine::snapAllWindows(const QString& screenId)
{
    qCDebug(lcCore) << "snapAllWindows called for screen=" << screenId;
    Q_EMIT snapAllWindowsRequested(screenId);
}

void SnapEngine::cycleWindowsInZone(bool forward)
{
    Q_UNUSED(forward)
    qCWarning(lcCore) << "SnapEngine::cycleWindowsInZone called but snap navigation is daemon-driven; use WTA";
}

} // namespace PlasmaZones
