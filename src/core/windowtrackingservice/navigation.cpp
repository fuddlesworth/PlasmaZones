// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Navigation helpers: zone queries, geometry calculations, rotation.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include "../windowtrackingservice.h"
#include "../geometryutils.h"
#include "../layout.h"
#include "../screenmanager.h"
#include "../zone.h"
#include "../layoutmanager.h"
#include "../utils.h"
#include "../logging.h"
#include <QScreen>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation Helpers
// ═══════════════════════════════════════════════════════════════════════════════

QSet<QUuid> WindowTrackingService::buildOccupiedZoneSet(const QString& screenFilter) const
{
    QSet<QUuid> occupiedZoneIds;
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        // Skip floating windows — they have preserved zone assignments (for resnap
        // on mode switch) but should not make zones appear occupied.
        if (isWindowFloating(it.key())) {
            continue;
        }
        // When screen filter is set, only count zones from windows on that screen.
        // This prevents windows on other screens (or desktops sharing the same layout)
        // from making zones appear occupied on the target screen.
        if (!screenFilter.isEmpty()) {
            QString windowScreen = m_windowScreenAssignments.value(it.key());
            if (!Utils::screensMatch(windowScreen, screenFilter)) {
                continue;
            }
        }
        for (const QString& zoneId : it.value()) {
            if (zoneId.startsWith(QStringLiteral("zoneselector-"))) {
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

QString WindowTrackingService::findEmptyZoneInLayout(Layout* layout, const QString& screenId) const
{
    if (!layout) {
        return QString();
    }

    QSet<QUuid> occupiedZoneIds = buildOccupiedZoneSet(screenId);

    // Sort by zone number so "first empty" is the lowest-numbered empty zone
    QVector<Zone*> sortedZones = layout->zones();
    std::sort(sortedZones.begin(), sortedZones.end(), [](const Zone* a, const Zone* b) {
        return a->zoneNumber() < b->zoneNumber();
    });

    for (Zone* zone : sortedZones) {
        if (!occupiedZoneIds.contains(zone->id())) {
            return zone->id().toString();
        }
    }
    return QString();
}

QString WindowTrackingService::findEmptyZone(const QString& screenId) const
{
    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    return findEmptyZoneInLayout(layout, screenId);
}

QString WindowTrackingService::getEmptyZonesJson(const QString& screenId) const
{
    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout) {
        return QStringLiteral("[]");
    }

    // Resolve physical screen for fallback (virtual screen IDs resolve to their backing QScreen*)
    QScreen* screen = ScreenManager::resolvePhysicalScreen(screenId);
    if (!screen) {
        return QStringLiteral("[]");
    }

    // Use screen-filtered occupancy check — without this, zones occupied on
    // screen A appear occupied on screen B when both use the same layout (same zone IDs).
    QSet<QUuid> occupied = buildOccupiedZoneSet(screenId);
    return GeometryUtils::buildEmptyZonesJson(layout, screenId, screen, m_settings, [&occupied](const Zone* z) {
        return !occupied.contains(z->id());
    });
}

QRect WindowTrackingService::zoneGeometry(const QString& zoneId, const QString& screenId) const
{
    auto uuidOpt = Utils::parseUuid(zoneId);
    if (!uuidOpt) {
        return QRect();
    }

    // Find zone and its parent layout (search all layouts for per-screen support)
    Zone* zone = nullptr;
    Layout* layout = nullptr;
    for (Layout* l : m_layoutManager->layouts()) {
        zone = l->zoneById(*uuidOpt);
        if (zone) {
            layout = l;
            break;
        }
    }
    if (!zone) {
        return QRect();
    }

    // Resolve physical screen (virtual IDs resolve to backing QScreen*)
    QScreen* screen = ScreenManager::resolvePhysicalScreen(screenId);
    if (!screen) {
        return QRect();
    }

    return GeometryUtils::getZoneGeometryForScreen(zone, screen, screenId, layout, m_settings);
}

QRect WindowTrackingService::multiZoneGeometry(const QStringList& zoneIds, const QString& screenId) const
{
    QRect combined;
    for (const QString& zoneId : zoneIds) {
        QRect geo = zoneGeometry(zoneId, screenId);
        if (geo.isValid()) {
            if (combined.isValid()) {
                combined = combined.united(geo);
            } else {
                combined = geo;
            }
        }
    }
    return combined;
}

QVector<ZoneAssignmentEntry> WindowTrackingService::calculateRotation(bool clockwise, const QString& screenFilter) const
{
    QVector<ZoneAssignmentEntry> result;

    // Group snapped windows by screen so each screen rotates independently
    // using its own per-screen layout (not the global active layout)
    QHash<QString, QVector<QPair<QString, QString>>> windowsByScreen; // screenId -> [(windowId, primaryZoneId)]
    for (auto it = m_windowZoneAssignments.constBegin(); it != m_windowZoneAssignments.constEnd(); ++it) {
        // User-initiated snap commands override floating state.
        // Floating windows are unsnapped (not in m_windowZoneAssignments), so this
        // is a no-op, but we remove the check for consistency across all snap paths.

        const QStringList& zoneIdList = it.value();
        if (zoneIdList.isEmpty()) {
            continue;
        }

        QString screenId = m_windowScreenAssignments.value(it.key());

        // When a screen filter is set, only include windows on that screen
        if (!screenFilter.isEmpty() && !Utils::screensMatch(screenId, screenFilter)) {
            continue;
        }

        windowsByScreen[screenId].append({it.key(), zoneIdList.first()});
    }

    // Process each screen independently
    for (auto screenIt = windowsByScreen.constBegin(); screenIt != windowsByScreen.constEnd(); ++screenIt) {
        const QString& screenId = screenIt.key();

        // Get the layout assigned to THIS screen (not the global active layout)
        Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
        if (!layout || layout->zoneCount() < 2) {
            continue;
        }

        // Get zones sorted by zone number
        QVector<Zone*> zones = layout->zones();
        std::sort(zones.begin(), zones.end(), [](Zone* a, Zone* b) {
            return a->zoneNumber() < b->zoneNumber();
        });

        // Build zone ID -> index map (with and without braces for format-agnostic matching)
        QHash<QString, int> zoneIdToIndex;
        for (int i = 0; i < zones.size(); ++i) {
            zoneIdToIndex[zones[i]->id().toString()] = i;
        }

        // Find zone indices for windows on this screen
        QVector<QPair<QString, int>> windowZoneIndices;
        for (const auto& windowEntry : screenIt.value()) {
            const QString& windowId = windowEntry.first;
            const QString& storedZoneId = windowEntry.second;
            int zoneIndex = -1;

            // Try direct match first
            if (zoneIdToIndex.contains(storedZoneId)) {
                zoneIndex = zoneIdToIndex.value(storedZoneId);
            } else {
                // Try matching by parsing as UUID (handles format differences)
                QUuid storedUuid = QUuid::fromString(storedZoneId);
                if (!storedUuid.isNull()) {
                    for (int i = 0; i < zones.size(); ++i) {
                        if (zones[i]->id() == storedUuid) {
                            zoneIndex = i;
                            break;
                        }
                    }
                }
            }

            if (zoneIndex >= 0) {
                windowZoneIndices.append({windowId, zoneIndex});
            } else {
                qCDebug(lcCore) << "Window" << windowId << "has zone ID" << storedZoneId
                                << "not found in layout for screen" << screenId << "- skipping rotation";
            }
        }

        // Resolve physical screen for zone geometry calculation
        QScreen* screen = ScreenManager::resolvePhysicalScreen(screenId);
        if (!screen) {
            continue;
        }

        // Calculate rotated positions within this screen's zones
        for (const auto& pair : windowZoneIndices) {
            int currentIdx = pair.second;
            int targetIdx =
                clockwise ? (currentIdx + 1) % zones.size() : (currentIdx - 1 + zones.size()) % zones.size();

            Zone* sourceZone = zones[currentIdx];
            Zone* targetZone = zones[targetIdx];
            QRect geo = GeometryUtils::getZoneGeometryForScreen(targetZone, screen, screenId, layout, m_settings);

            if (geo.isValid()) {
                ZoneAssignmentEntry entry;
                entry.windowId = pair.first;
                entry.sourceZoneId = sourceZone->id().toString();
                entry.targetZoneId = targetZone->id().toString();
                entry.targetGeometry = geo;
                result.append(entry);
            }
        }
    }

    return result;
}

} // namespace PlasmaZones
