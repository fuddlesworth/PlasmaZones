// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Navigation helpers: zone queries, geometry calculations, rotation.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include "../windowtrackingservice.h"
#include "../constants.h"
#include "../geometryutils.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "../virtualdesktopmanager.h"
#include "../utils.h"
#include "../logging.h"
#include <QScreen>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation Helpers
// ═══════════════════════════════════════════════════════════════════════════════

QSet<QUuid> WindowTrackingService::buildOccupiedZoneSet(const QString& screenFilter, int desktopFilter) const
{
    const QHash<QString, QStringList>& zones = m_snapState->zoneAssignments();
    const QHash<QString, QString>& screens = m_snapState->screenAssignments();
    const QHash<QString, int>& desktops = m_snapState->desktopAssignments();

    QSet<QUuid> occupiedZoneIds;
    for (auto it = zones.constBegin(); it != zones.constEnd(); ++it) {
        // Skip floating windows — they have preserved zone assignments (for resnap
        // on mode switch) but should not make zones appear occupied.
        if (isWindowFloating(it.key())) {
            continue;
        }
        // When screen filter is set, only count zones from windows on that screen.
        // This prevents windows on other screens (or desktops sharing the same layout)
        // from making zones appear occupied on the target screen.
        if (!screenFilter.isEmpty()) {
            QString windowScreen = screens.value(it.key());
            if (!Phosphor::Screens::ScreenIdentity::screensMatch(windowScreen, screenFilter)) {
                continue;
            }
        }
        // When desktop filter is set, only count zones from windows on that desktop.
        // Desktop 0 means "all desktops" (pinned window) — always include those.
        if (desktopFilter > 0) {
            int windowDesktop = desktops.value(it.key(), 0);
            if (windowDesktop != 0 && windowDesktop != desktopFilter) {
                continue;
            }
        }
        for (const QString& zoneId : it.value()) {
            if (zoneId.startsWith(ZoneSelectorIdPrefix)) {
                continue;
            }
            auto uuid = Utils::parseUuid(zoneId);
            if (uuid) {
                occupiedZoneIds.insert(*uuid);
            }
        }
    }
    return occupiedZoneIds;
}

QString WindowTrackingService::findEmptyZoneInLayout(PhosphorZones::Layout* layout, const QString& screenId,
                                                     int desktopFilter) const
{
    if (!layout) {
        return QString();
    }

    QSet<QUuid> occupiedZoneIds = buildOccupiedZoneSet(screenId, desktopFilter);

    // Sort by zone number so "first empty" is the lowest-numbered empty zone
    QVector<PhosphorZones::Zone*> sortedZones = layout->zones();
    PhosphorZones::LayoutUtils::sortZonesByNumber(sortedZones);

    for (PhosphorZones::Zone* zone : sortedZones) {
        if (!occupiedZoneIds.contains(zone->id())) {
            return zone->id().toString();
        }
    }
    return QString();
}

QString WindowTrackingService::findEmptyZone(const QString& screenId) const
{
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    const int desktopFilter = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    return findEmptyZoneInLayout(layout, screenId, desktopFilter);
}

EmptyZoneList WindowTrackingService::getEmptyZones(const QString& screenId) const
{
    PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout) {
        return {};
    }

    // Resolve physical screen for fallback (virtual screen IDs resolve to their backing QScreen*)
    QScreen* screen =
        (m_screenManager ? m_screenManager->physicalQScreenFor(screenId) : Utils::findScreenAtPosition(QPoint(0, 0)));
    if (!screen) {
        return {};
    }

    // Screen-filtered + desktop-filtered occupancy — without the screen filter,
    // zones occupied on screen A appear occupied on screen B when both use the
    // same layout (same zone IDs). Without the desktop filter, windows parked on
    // other virtual desktops keep their zone occupied on the current desktop,
    // blocking snap assist (discussion #323).
    const int desktopFilter = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    QSet<QUuid> occupied = buildOccupiedZoneSet(screenId, desktopFilter);
    return GeometryUtils::buildEmptyZoneList(m_screenManager, layout, screenId, screen, m_settings,
                                             [&occupied](const PhosphorZones::Zone* z) {
                                                 return !occupied.contains(z->id());
                                             });
}

QRect WindowTrackingService::zoneGeometry(const QString& zoneId, const QString& screenId) const
{
    auto uuidOpt = Utils::parseUuid(zoneId);
    if (!uuidOpt) {
        return QRect();
    }

    auto [zone, layout] = findZoneInAllLayouts(*uuidOpt);
    if (!zone) {
        return QRect();
    }

    // Resolve physical screen (virtual IDs resolve to backing QScreen*)
    QScreen* screen =
        (m_screenManager ? m_screenManager->physicalQScreenFor(screenId) : Utils::findScreenAtPosition(QPoint(0, 0)));
    if (!screen) {
        return QRect();
    }

    return GeometryUtils::getZoneGeometryForScreen(m_screenManager, zone, screen, screenId, layout, m_settings);
}

QRect WindowTrackingService::multiZoneGeometry(const QStringList& zoneIds, const QString& screenId) const
{
    // Unite zone geometries as QRectF first, then round once at the end.
    // Uniting independently-rounded QRects can produce 1px gaps at fractional
    // scaling factors (e.g. 1.2x on ultrawides).
    QRectF combined;
    QScreen* screen =
        (m_screenManager ? m_screenManager->physicalQScreenFor(screenId) : Utils::findScreenAtPosition(QPoint(0, 0)));
    if (!screen) {
        return combined.toAlignedRect();
    }
    for (const QString& zoneId : zoneIds) {
        auto uuidOpt = Utils::parseUuid(zoneId);
        if (!uuidOpt) {
            continue;
        }

        auto [zone, layout] = findZoneInAllLayouts(*uuidOpt);
        if (!zone) {
            continue;
        }

        QRectF geoF =
            GeometryUtils::getZoneGeometryForScreenF(m_screenManager, zone, screen, screenId, layout, m_settings);
        if (geoF.isValid()) {
            if (combined.isValid()) {
                combined = combined.united(geoF);
            } else {
                combined = geoF;
            }
        }
    }
    return combined.toAlignedRect();
}

} // namespace PlasmaZones
