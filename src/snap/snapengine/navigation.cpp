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
// IWindowEngine navigation methods
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::moveInDirection(const QString& direction)
{
    if (direction.isEmpty()) {
        qCWarning(lcCore) << "SnapEngine::moveInDirection: empty direction";
        Q_EMIT navigationFeedback(false, QStringLiteral("move"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), m_lastActiveScreenId);
        return;
    }

    Q_EMIT moveWindowToZoneRequested(QStringLiteral("navigate:") + direction, QString());
}

void SnapEngine::pushToEmptyZone(const QString& screenId)
{
    qCInfo(lcCore) << "SnapEngine::pushToEmptyZone on screen=" << screenId;
    Q_EMIT moveWindowToZoneRequested(QStringLiteral("push"), screenId);
}

void SnapEngine::focusInDirection(const QString& direction, const QString& action)
{
    Q_UNUSED(action)

    if (direction.isEmpty()) {
        qCWarning(lcCore) << "SnapEngine::focusInDirection: empty direction";
        Q_EMIT navigationFeedback(false, QStringLiteral("focus"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), m_lastActiveScreenId);
        return;
    }

    Q_EMIT focusWindowInZoneRequested(QStringLiteral("navigate:") + direction, QString());
}

void SnapEngine::swapInDirection(const QString& direction, const QString& action)
{
    Q_UNUSED(action)

    if (direction.isEmpty()) {
        qCWarning(lcCore) << "SnapEngine::swapInDirection: empty direction";
        Q_EMIT navigationFeedback(false, QStringLiteral("swap"), QStringLiteral("invalid_direction"), QString(),
                                  QString(), m_lastActiveScreenId);
        return;
    }

    Q_EMIT swapWindowsRequested(QStringLiteral("swap:") + direction, QString(), QString());
}

void SnapEngine::rotateWindows(bool clockwise, const QString& screenId)
{
    QVector<RotationEntry> rotationEntries = m_windowTracker->calculateRotation(clockwise, screenId);

    if (rotationEntries.isEmpty()) {
        Layout* layout = m_layoutManager->resolveLayoutForScreen(Utils::screenIdForName(screenId));
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

    QString rotationData = GeometryUtils::serializeRotationEntries(rotationEntries);
    qCInfo(lcCore) << "Rotating" << rotationEntries.size() << "windows"
                   << (clockwise ? "clockwise" : "counterclockwise");
    Q_EMIT rotateWindowsRequested(clockwise, rotationData);
}

void SnapEngine::moveToPosition(const QString& windowId, int position, const QString& screenId)
{
    Q_UNUSED(windowId)

    if (position < 1 || position > 9) {
        qCWarning(lcCore) << "SnapEngine::moveToPosition: invalid zone number=" << position;
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(),
                                  QString(), screenId);
        return;
    }

    Q_EMIT moveWindowToZoneRequested(QStringLiteral("snap:") + QString::number(position), screenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Snap-specific navigation methods
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
    qCDebug(lcCore) << "cycleWindowsInZone called, forward=" << forward;
    QString directive = forward ? QStringLiteral("cycle:forward") : QStringLiteral("cycle:backward");
    Q_EMIT cycleWindowsInZoneRequested(directive, QString());
}

} // namespace PlasmaZones
