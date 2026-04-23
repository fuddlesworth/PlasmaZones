// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../SnapEngine.h"
#include <PhosphorZones/SnapState.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "core/logging.h"
#include "core/virtualdesktopmanager.h"
#include "core/windowtrackingservice.h"

#include <QGuiApplication>
#include <QScreen>

namespace PlasmaZones {

void SnapEngine::commitSnapImpl(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                SnapIntent intent)
{
    Q_ASSERT(m_snapState);
    const QString& primaryZoneId = zoneIds.first();

    if (m_windowTracker->clearFloatingForSnap(windowId)) {
        Q_EMIT windowFloatingClearedForSnap(windowId, screenId);
    }

    bool wasAutoSnapped = false;
    if (intent == SnapIntent::UserInitiated) {
        wasAutoSnapped = m_windowTracker->clearAutoSnapped(windowId);
        if (!wasAutoSnapped) {
            m_windowTracker->consumePendingAssignment(windowId);
        }
    }

    const int currentDesktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    if (zoneIds.size() > 1) {
        m_windowTracker->assignWindowToZones(windowId, zoneIds, screenId, currentDesktop);
    } else {
        m_windowTracker->assignWindowToZone(windowId, primaryZoneId, screenId, currentDesktop);
    }

    if (intent == SnapIntent::UserInitiated && !wasAutoSnapped
        && !primaryZoneId.startsWith(QStringLiteral("zoneselector-"))) {
        const QString windowClass = m_windowTracker->currentAppIdFor(windowId);
        m_windowTracker->updateLastUsedZone(primaryZoneId, screenId, windowClass, currentDesktop);
    }

    qCInfo(lcCore) << "commitSnap:" << windowId << "zones=" << zoneIds << "screen=" << screenId
                   << "intent=" << (intent == SnapIntent::UserInitiated ? "user" : "auto");

    Q_EMIT windowSnapStateChanged(
        windowId,
        WindowStateEntry{windowId, primaryZoneId, screenId, false, QStringLiteral("snapped"), zoneIds, false});
}

void SnapEngine::commitSnap(const QString& windowId, const QString& zoneId, const QString& screenId, SnapIntent intent)
{
    if (windowId.isEmpty() || zoneId.isEmpty()) {
        qCWarning(lcCore) << "commitSnap: empty windowId or zoneId";
        return;
    }
    commitSnapImpl(windowId, QStringList{zoneId}, screenId, intent);
}

void SnapEngine::commitMultiZoneSnap(const QString& windowId, const QStringList& zoneIds, const QString& screenId,
                                     SnapIntent intent)
{
    if (windowId.isEmpty() || zoneIds.isEmpty() || zoneIds.first().isEmpty()) {
        qCWarning(lcCore) << "commitMultiZoneSnap: empty windowId or zoneIds";
        return;
    }
    commitSnapImpl(windowId, zoneIds, screenId, intent);
}

void SnapEngine::uncommitSnap(const QString& windowId)
{
    Q_ASSERT(m_snapState);
    if (windowId.isEmpty()) {
        return;
    }

    const QString previousZoneId = m_snapState->zoneForWindow(windowId);
    if (previousZoneId.isEmpty()) {
        qCDebug(lcCore) << "uncommitSnap: window not in any zone:" << windowId;
        return;
    }

    m_windowTracker->consumePendingAssignment(windowId);
    m_windowTracker->unassignWindow(windowId);

    qCInfo(lcCore) << "uncommitSnap:" << windowId << "from zone" << previousZoneId;

    Q_EMIT windowSnapStateChanged(
        windowId,
        WindowStateEntry{windowId, QString(), QString(), false, QStringLiteral("unsnapped"), QStringList{}, false});
}

WindowGeometryList SnapEngine::applyBatchAssignments(const QVector<ZoneAssignmentEntry>& entries, SnapIntent intent,
                                                     std::function<QString()> fallbackScreenResolver)
{
    WindowGeometryList geometries;
    if (entries.isEmpty()) {
        return geometries;
    }

    auto* mgr = m_windowTracker->screenManager();

    for (const auto& entry : entries) {
        if (entry.targetZoneId == QLatin1String("__restore__")) {
            uncommitSnap(entry.windowId);
            removeUnmanagedGeometry(entry.windowId);
            continue;
        }

        QString screenId = entry.targetScreenId;
        const QPoint center = entry.targetGeometry.center();
        if (screenId.isEmpty() && mgr) {
            screenId = mgr->effectiveScreenAt(center);
        }
        if (screenId.isEmpty()) {
            for (QScreen* screen : QGuiApplication::screens()) {
                if (screen->geometry().contains(center)) {
                    screenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
                    break;
                }
            }
        }
        if (screenId.isEmpty() && fallbackScreenResolver) {
            screenId = fallbackScreenResolver();
        }

        if (entry.targetZoneIds.size() > 1) {
            commitMultiZoneSnap(entry.windowId, entry.targetZoneIds, screenId, intent);
        } else {
            commitSnap(entry.windowId, entry.targetZoneId, screenId, intent);
        }
    }

    geometries.reserve(entries.size());
    for (const auto& entry : entries) {
        geometries.append(WindowGeometryEntry::fromRect(entry.windowId, entry.targetGeometry));
    }
    return geometries;
}

} // namespace PlasmaZones
