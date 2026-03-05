// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "internal.h"
#include "../zonedetectionadaptor.h"
#include "../../core/interfaces.h"
#include "../../core/layoutmanager.h"
#include "../../core/layout.h"
#include "../../core/zone.h"
#include "../../core/geometryutils.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/types.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace PlasmaZones {

static QString serializeRotationEntries(const QVector<RotationEntry>& entries)
{
    if (entries.isEmpty()) {
        return QStringLiteral("[]");
    }
    QJsonArray array;
    for (const RotationEntry& entry : entries) {
        QJsonObject obj;
        obj[QLatin1String("windowId")] = entry.windowId;
        obj[QLatin1String("sourceZoneId")] = entry.sourceZoneId;
        obj[QLatin1String("targetZoneId")] = entry.targetZoneId;
        obj[QLatin1String("x")] = entry.targetGeometry.x();
        obj[QLatin1String("y")] = entry.targetGeometry.y();
        obj[QLatin1String("width")] = entry.targetGeometry.width();
        obj[QLatin1String("height")] = entry.targetGeometry.height();
        array.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void WindowTrackingAdaptor::moveWindowToAdjacentZone(const QString& direction)
{
    qCInfo(lcDbusWindow) << "moveWindowToAdjacentZone called with direction:" << direction;

    if (!validateDirection(direction, QStringLiteral("move"))) {
        return;
    }

    Q_EMIT moveWindowToZoneRequested(QStringLiteral("navigate:") + direction, QString());
}

void WindowTrackingAdaptor::focusAdjacentZone(const QString& direction)
{
    qCInfo(lcDbusWindow) << "focusAdjacentZone called with direction:" << direction;

    if (!validateDirection(direction, QStringLiteral("focus"))) {
        return;
    }

    Q_EMIT focusWindowInZoneRequested(QStringLiteral("navigate:") + direction, QString());
}

void WindowTrackingAdaptor::pushToEmptyZone(const QString& screenName)
{
    qCInfo(lcDbusWindow) << "pushToEmptyZone called, screen:" << screenName;
    Q_EMIT moveWindowToZoneRequested(QStringLiteral("push"), screenName);
}

void WindowTrackingAdaptor::restoreWindowSize()
{
    qCInfo(lcDbusWindow) << "restoreWindowSize called";
    Q_EMIT restoreWindowRequested();
}

void WindowTrackingAdaptor::toggleWindowFloat()
{
    qCInfo(lcDbusWindow) << "toggleWindowFloat called";
    Q_EMIT toggleWindowFloatRequested(true);
}

void WindowTrackingAdaptor::swapWindowWithAdjacentZone(const QString& direction)
{
    qCInfo(lcDbusWindow) << "swapWindowWithAdjacentZone called with direction:" << direction;

    if (!validateDirection(direction, QStringLiteral("swap"))) {
        return;
    }

    Q_EMIT swapWindowsRequested(QStringLiteral("swap:") + direction, QString(), QString());
}

void WindowTrackingAdaptor::snapToZoneByNumber(int zoneNumber, const QString& screenName)
{
    qCInfo(lcDbusWindow) << "snapToZoneByNumber called with zone number:" << zoneNumber << "screen:" << screenName;

    if (zoneNumber < 1 || zoneNumber > 9) {
        qCWarning(lcDbusWindow) << "Invalid zone number:" << zoneNumber << "(must be 1-9)";
        Q_EMIT navigationFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(),
                                  QString(), QString());
        return;
    }

    Q_EMIT moveWindowToZoneRequested(QStringLiteral("snap:") + QString::number(zoneNumber), screenName);
}

void WindowTrackingAdaptor::rotateWindowsInLayout(bool clockwise, const QString& screenName)
{
    qCDebug(lcDbusWindow) << "rotateWindowsInLayout called, clockwise:" << clockwise << "screen:" << screenName;

    // Delegate rotation calculation to service, filtered to cursor screen
    QVector<RotationEntry> rotationEntries = m_service->calculateRotation(clockwise, screenName);

    if (rotationEntries.isEmpty()) {
        auto* layout = getValidatedActiveLayout(QStringLiteral("rotateWindowsInLayout"));
        if (!layout) {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_active_layout"), QString(),
                                      QString(), QString());
        } else if (layout->zoneCount() < 2) {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("single_zone"), QString(),
                                      QString(), QString());
        } else {
            Q_EMIT navigationFeedback(false, QStringLiteral("rotate"), QStringLiteral("no_snapped_windows"), QString(),
                                      QString(), QString());
        }
        return;
    }

    QString rotationData = serializeRotationEntries(rotationEntries);
    qCInfo(lcDbusWindow) << "Rotating" << rotationEntries.size() << "windows"
                         << (clockwise ? "clockwise" : "counterclockwise");
    Q_EMIT rotateWindowsRequested(clockwise, rotationData);
    // NOTE: Don't emit navigationFeedback here. The KWin effect will report the actual
    // result via reportNavigationFeedback() after performing the rotation, and that
    // feedback will include the zone IDs for proper OSD highlighting. Emitting here
    // would trigger the OSD deduplication logic (same action+reason within 200ms),
    // causing the feedback with zone IDs to be discarded.
}

void WindowTrackingAdaptor::cycleWindowsInZone(bool forward)
{
    qCInfo(lcDbusWindow) << "cycleWindowsInZone called, forward:" << forward;
    QString directive = forward ? QStringLiteral("cycle:forward") : QStringLiteral("cycle:backward");
    Q_EMIT cycleWindowsInZoneRequested(directive, QString());
}

void WindowTrackingAdaptor::resnapToNewLayout()
{
    qCDebug(lcDbusWindow) << "resnapToNewLayout called";

    QVector<RotationEntry> resnapEntries = m_service->calculateResnapFromPreviousLayout();

    if (resnapEntries.isEmpty()) {
        auto* layout = getValidatedActiveLayout(QStringLiteral("resnapToNewLayout"));
        if (!layout) {
            Q_EMIT navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_active_layout"), QString(),
                                      QString(), QString());
        } else {
            Q_EMIT navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_windows_to_resnap"),
                                      QString(), QString(), QString());
        }
        return;
    }

    QString resnapData = serializeRotationEntries(resnapEntries);
    qCInfo(lcDbusWindow) << "Resnapping" << resnapEntries.size() << "windows to new layout";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

void WindowTrackingAdaptor::resnapCurrentAssignments(const QString& screenFilter)
{
    qCDebug(lcDbusWindow) << "resnapCurrentAssignments called (autotile toggle-off restore)"
                          << "screen:" << (screenFilter.isEmpty() ? QStringLiteral("all") : screenFilter);

    QVector<RotationEntry> entries = m_service->calculateResnapFromCurrentAssignments(screenFilter);
    if (entries.isEmpty()) {
        qCDebug(lcDbusWindow) << "No windows to resnap from current assignments";
        Q_EMIT navigationFeedback(false, QStringLiteral("resnap"), QStringLiteral("no_windows_to_resnap"), QString(),
                                  QString(), QString());
        return;
    }

    QString resnapData = serializeRotationEntries(entries);
    qCInfo(lcDbusWindow) << "Resnapping" << entries.size() << "windows to current zone assignments";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

void WindowTrackingAdaptor::resnapFromAutotileOrder(const QStringList& autotileWindowOrder, const QString& screenName)
{
    qCDebug(lcDbusWindow) << "resnapFromAutotileOrder called with" << autotileWindowOrder.size()
                          << "windows for screen:" << screenName;

    QVector<RotationEntry> entries = m_service->calculateResnapFromAutotileOrder(autotileWindowOrder, screenName);

    if (entries.isEmpty()) {
        qCDebug(lcDbusWindow) << "No resnap entries from autotile order, falling back to current assignments";
        resnapCurrentAssignments(screenName);
        return;
    }

    QString resnapData = serializeRotationEntries(entries);
    qCInfo(lcDbusWindow) << "Resnapping" << entries.size() << "windows from autotile order";
    Q_EMIT resnapToNewLayoutRequested(resnapData);
}

void WindowTrackingAdaptor::snapAllWindows(const QString& screenName)
{
    qCDebug(lcDbusWindow) << "snapAllWindows called for screen:" << screenName;
    Q_EMIT snapAllWindowsRequested(screenName);
}

void WindowTrackingAdaptor::requestMoveSpecificWindowToZone(const QString& windowId, const QString& zoneId,
                                                            const QString& geometryJson)
{
    qCDebug(lcDbusWindow) << "requestMoveSpecificWindowToZone: window=" << windowId << "zone=" << zoneId;
    Q_EMIT moveSpecificWindowToZoneRequested(windowId, zoneId, geometryJson);
}

QString WindowTrackingAdaptor::calculateSnapAllWindows(const QStringList& windowIds, const QString& screenName)
{
    qCDebug(lcDbusWindow) << "calculateSnapAllWindows called with" << windowIds.size()
                          << "windows on screen:" << screenName;

    QVector<RotationEntry> entries = m_service->calculateSnapAllWindows(windowIds, screenName);

    qCInfo(lcDbusWindow) << "Calculated snap-all for" << entries.size() << "windows";
    return serializeRotationEntries(entries);
}

void WindowTrackingAdaptor::reportNavigationFeedback(bool success, const QString& action, const QString& reason,
                                                     const QString& sourceZoneId, const QString& targetZoneId,
                                                     const QString& screenName)
{
    qCDebug(lcDbusWindow) << "Navigation feedback: success=" << success << "action=" << action << "reason=" << reason
                          << "sourceZone=" << sourceZoneId << "targetZone=" << targetZoneId << "screen=" << screenName;
    Q_EMIT navigationFeedback(success, action, reason, sourceZoneId, targetZoneId, screenName);
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
