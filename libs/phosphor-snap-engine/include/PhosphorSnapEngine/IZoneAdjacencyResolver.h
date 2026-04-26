// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorsnapengine_export.h>

#include <QString>

namespace PhosphorSnapEngine {

/**
 * @brief Narrow interface for zone-adjacency queries used by snap navigation.
 *
 * SnapNavigationTargetResolver needs two directional zone lookups that
 * live on the daemon's ZoneDetectionAdaptor:
 *   - getAdjacentZone   (find the zone next to a given zone in a direction)
 *   - getFirstZoneInDirection (find the edge zone when no current zone exists)
 *
 * Rather than holding an opaque QObject* and dispatching through
 * QMetaObject::invokeMethod, the resolver now depends on this typed
 * interface. The daemon's ZoneDetectionAdaptor must implement (or wrap)
 * IZoneAdjacencyResolver so that the engine receives a typed pointer.
 *
 * Not a QObject — pure data queries with no lifecycle signals.
 */
class PHOSPHORSNAPENGINE_EXPORT IZoneAdjacencyResolver
{
public:
    IZoneAdjacencyResolver() = default;
    virtual ~IZoneAdjacencyResolver();

    /**
     * @brief Find the zone adjacent to @p currentZoneId in @p direction.
     *
     * @param currentZoneId  Zone the window is currently snapped to
     * @param direction      "left", "right", "up", or "down"
     * @param screenId       Screen context for multi-monitor layouts
     * @return Zone ID of the adjacent zone, or empty string if none exists
     */
    virtual QString getAdjacentZone(const QString& currentZoneId, const QString& direction,
                                    const QString& screenId) const = 0;

    /**
     * @brief Find the edge zone in @p direction when no current zone exists.
     *
     * Used when a window is not yet snapped and the user presses a
     * navigation key. Returns the zone at the layout edge in the given
     * direction (e.g. leftmost zone for "left").
     *
     * @param direction  "left", "right", "up", or "down"
     * @param screenId   Screen context for multi-monitor layouts
     * @return Zone ID of the edge zone, or empty string if none exists
     */
    virtual QString getFirstZoneInDirection(const QString& direction, const QString& screenId) const = 0;

protected:
    IZoneAdjacencyResolver(const IZoneAdjacencyResolver&) = default;
    IZoneAdjacencyResolver& operator=(const IZoneAdjacencyResolver&) = default;
};

} // namespace PhosphorSnapEngine
