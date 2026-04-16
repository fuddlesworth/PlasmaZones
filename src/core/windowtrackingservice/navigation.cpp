// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Navigation helpers: zone queries, geometry calculations, rotation.
// Part of WindowTrackingService — split from windowtrackingservice.cpp for SRP.

#include "../windowtrackingservice.h"
#include "../geometryutils.h"
#include "../layout.h"
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

QSet<QUuid> WindowTrackingService::buildOccupiedZoneSet(const QString& screenFilter, int desktopFilter) const
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
        // When desktop filter is set, only count zones from windows on that desktop.
        // Desktop 0 means "all desktops" (pinned window) — always include those.
        // Without this, a window parked on another virtual desktop keeps its zone
        // "occupied" on the current desktop, blocking snap assist from offering the
        // zone to fill — even though the KWin effect's candidate list excludes
        // other-desktop windows (snapassisthandler.cpp:98). This asymmetry produces
        // discussion #323: snap-to-top never shows snap assist for the bottom zone
        // when other-desktop windows are parked there.
        if (desktopFilter > 0) {
            int windowDesktop = m_windowDesktopAssignments.value(it.key(), 0);
            if (windowDesktop != 0 && windowDesktop != desktopFilter) {
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

QString WindowTrackingService::findEmptyZoneInLayout(Layout* layout, const QString& screenId, int desktopFilter) const
{
    if (!layout) {
        return QString();
    }

    QSet<QUuid> occupiedZoneIds = buildOccupiedZoneSet(screenId, desktopFilter);

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
    const int desktopFilter = m_layoutManager ? m_layoutManager->currentVirtualDesktop() : 0;
    return findEmptyZoneInLayout(layout, screenId, desktopFilter);
}

QString WindowTrackingService::getEmptyZonesJson(const QString& screenId) const
{
    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout) {
        return QStringLiteral("[]");
    }

    QScreen* screen = screenId.isEmpty() ? Utils::primaryScreen() : Utils::findScreenByIdOrName(screenId);
    if (!screen) {
        screen = Utils::primaryScreen();
    }
    if (!screen) {
        return QStringLiteral("[]");
    }

    // Screen-filtered + desktop-filtered occupancy — without the screen filter,
    // zones occupied on screen A appear occupied on screen B when both use the
    // same layout (same zone IDs). Without the desktop filter, windows parked on
    // other virtual desktops keep their zone occupied on the current desktop,
    // blocking snap assist (discussion #323).
    const int desktopFilter = m_layoutManager ? m_layoutManager->currentVirtualDesktop() : 0;
    QSet<QUuid> occupied = buildOccupiedZoneSet(screenId, desktopFilter);
    return GeometryUtils::buildEmptyZonesJson(layout, screen, m_settings, [&occupied](const Zone* z) {
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

    QScreen* screen = screenId.isEmpty() ? Utils::primaryScreen() : Utils::findScreenByIdOrName(screenId);

    if (!screen) {
        screen = Utils::primaryScreen();
    }

    if (!screen) {
        return QRect();
    }

    // Use the zone's own layout for per-layout gap overrides
    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings, screenId);
    EdgeGaps outerGaps = GeometryUtils::getEffectiveOuterGaps(layout, m_settings, screenId);
    bool useAvail = !(layout && layout->useFullScreenGeometry());
    QRectF geoF = GeometryUtils::getZoneGeometryWithGaps(zone, screen, zonePadding, outerGaps, useAvail);

    return GeometryUtils::snapToRect(geoF);
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

QVector<RotationEntry> WindowTrackingService::calculateRotation(bool clockwise, const QString& screenFilter) const
{
    QVector<RotationEntry> result;

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

        // Get screen and gap settings for geometry calculation
        QScreen* screen = screenId.isEmpty() ? Utils::primaryScreen() : Utils::findScreenByIdOrName(screenId);
        if (!screen) {
            screen = Utils::primaryScreen();
        }
        if (!screen) {
            continue;
        }

        int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings, screenId);
        EdgeGaps outerGaps = GeometryUtils::getEffectiveOuterGaps(layout, m_settings, screenId);

        // Calculate rotated positions within this screen's zones
        for (const auto& pair : windowZoneIndices) {
            int currentIdx = pair.second;
            int targetIdx =
                clockwise ? (currentIdx + 1) % zones.size() : (currentIdx - 1 + zones.size()) % zones.size();

            Zone* sourceZone = zones[currentIdx];
            Zone* targetZone = zones[targetIdx];
            bool useAvail = !(layout && layout->useFullScreenGeometry());
            QRectF geoF = GeometryUtils::getZoneGeometryWithGaps(targetZone, screen, zonePadding, outerGaps, useAvail);
            QRect geo = GeometryUtils::snapToRect(geoF);

            if (geo.isValid()) {
                RotationEntry entry;
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
