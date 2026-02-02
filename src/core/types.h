// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QString>
#include <QRect>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// Shared Types - Parameter Objects for Complex Method Signatures
// ═══════════════════════════════════════════════════════════════════════════════
// These types reduce the number of output parameters in D-Bus methods and
// provide clear semantic grouping of related data.
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Result of a snap calculation
 *
 * Used by WindowTrackingService and D-Bus adaptor to communicate
 * snap decisions and geometry in a clean, single-object format.
 */
struct PLASMAZONES_EXPORT SnapResult
{
    bool shouldSnap = false;    ///< Whether the window should be snapped
    QRect geometry;             ///< Target geometry for snapping (x, y, width, height)
    QString zoneId;             ///< UUID of target zone
    QString screenName;         ///< Screen where the zone is located

    /**
     * @brief Check if this result represents a valid snap operation
     * @return true if shouldSnap is true and geometry is valid
     */
    bool isValid() const
    {
        return shouldSnap && geometry.isValid() && !zoneId.isEmpty();
    }

    /**
     * @brief Create an empty/no-snap result
     */
    static SnapResult noSnap()
    {
        return SnapResult{false, QRect(), QString(), QString()};
    }
};

/**
 * @brief Information about a window being dragged
 *
 * Groups window identification and context data that's commonly
 * passed together during drag operations.
 */
struct PLASMAZONES_EXPORT DragInfo
{
    QString windowId;           ///< Full window ID (class:resource:pointer)
    QRect geometry;             ///< Current window geometry
    QString appName;            ///< Application name (for exclusion checks)
    QString windowClass;        ///< Window class (for pattern matching)
    QString screenName;         ///< Screen where window is located
    bool isSticky = false;      ///< Whether window is on all desktops
    int virtualDesktop = 0;     ///< Current virtual desktop (0 = all)

    /**
     * @brief Check if drag info has required fields
     */
    bool isValid() const
    {
        return !windowId.isEmpty();
    }

    // Note: Use Utils::extractStableId(dragInfo.windowId) to get stable ID
};

/**
 * @brief Navigation command for keyboard zone movement
 *
 * Encapsulates the parameters for zone navigation operations.
 */
struct PLASMAZONES_EXPORT NavigationCommand
{
    enum class Type {
        MoveToZone,     ///< Move window to a specific zone
        FocusZone,      ///< Focus window in a zone
        SwapWindows,    ///< Swap two windows between zones
        PushToEmpty,    ///< Push window to first empty zone
        Restore,        ///< Restore window to original size
        ToggleFloat,    ///< Toggle window floating state
        SnapToNumber,   ///< Snap to zone by number
        Rotate          ///< Rotate windows in layout
    };

    Type type = Type::MoveToZone;
    QString targetZoneId;
    QString targetWindowId;
    QString zoneGeometry;       ///< JSON geometry for D-Bus
    bool clockwise = true;      ///< For rotation commands

    /**
     * @brief Create a move-to-zone command
     */
    static NavigationCommand moveToZone(const QString& zoneId, const QString& geometry)
    {
        return NavigationCommand{Type::MoveToZone, zoneId, QString(), geometry, true};
    }

    /**
     * @brief Create a focus-zone command
     */
    static NavigationCommand focusZone(const QString& zoneId, const QString& windowId)
    {
        return NavigationCommand{Type::FocusZone, zoneId, windowId, QString(), true};
    }

    /**
     * @brief Create a swap-windows command
     */
    static NavigationCommand swapWindows(const QString& zoneId, const QString& windowId, const QString& geometry)
    {
        return NavigationCommand{Type::SwapWindows, zoneId, windowId, geometry, true};
    }
};

/**
 * @brief Window rotation entry for rotate-windows-in-layout feature
 *
 * Describes a single window movement in a rotation operation.
 */
struct PLASMAZONES_EXPORT RotationEntry
{
    QString windowId;           ///< Window to move
    QString sourceZoneId;       ///< Zone window is moving from (for OSD highlighting)
    QString targetZoneId;       ///< Zone to move to
    QRect targetGeometry;       ///< Target geometry in pixels
};

} // namespace PlasmaZones
