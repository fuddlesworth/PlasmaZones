// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace PlasmaZones {

class ILayoutManager;

/**
 * @brief Entry in the unified layout list (manual layouts + autotile algorithms)
 *
 * This struct represents either a manual zone-based layout or an autotile
 * algorithm acting as a layout. It's used for:
 * - Quick layout shortcuts (Meta+1-9)
 * - Layout cycling (Meta+[/])
 * - Zone selector display
 * - D-Bus layout list queries
 */
struct PLASMAZONES_EXPORT UnifiedLayoutEntry {
    QString id;        ///< Layout UUID or "autotile:<algorithm-id>"
    QString name;      ///< Display name for UI
    QString description; ///< Optional description
    bool isAutotile;   ///< True for autotile algorithms, false for manual layouts
    int zoneCount;     ///< Number of zones (0 for dynamic autotile)
    QVariantList zones; ///< Zone data for preview rendering

    /**
     * @brief Extract algorithm ID from an autotile entry
     * @return Algorithm ID, or empty string if not an autotile entry
     */
    QString algorithmId() const;

    /**
     * @brief Check equality based on ID
     */
    bool operator==(const UnifiedLayoutEntry& other) const { return id == other.id; }
    bool operator!=(const UnifiedLayoutEntry& other) const { return id != other.id; }
};

/**
 * @brief Utility functions for unified layout management (DRY)
 *
 * This namespace consolidates layout list building that was previously
 * duplicated in Daemon, ZoneSelectorController, LayoutAdaptor, and OverlayService.
 *
 * Usage:
 * @code
 * auto layouts = LayoutUtils::buildUnifiedLayoutList(layoutManager);
 * for (const auto& entry : layouts) {
 *     if (entry.isAutotile) {
 *         // Handle autotile algorithm
 *     } else {
 *         // Handle manual layout
 *     }
 * }
 * @endcode
 */
namespace LayoutUtils {

/**
 * @brief Build list of all available layouts (manual + autotile)
 *
 * Returns manual layouts first (in layout manager order), followed by
 * autotile algorithms (in registration order).
 *
 * @param layoutManager Layout manager interface (can be nullptr)
 * @return Vector of unified layout entries
 */
PLASMAZONES_EXPORT QVector<UnifiedLayoutEntry> buildUnifiedLayoutList(ILayoutManager* layoutManager);

/**
 * @brief Convert a unified layout entry to QVariantMap for QML
 *
 * Creates a map with keys matching the zone selector's expectations:
 * - id, name, description, type, zoneCount, zones, category
 *
 * @param entry The unified layout entry
 * @return QVariantMap suitable for QML consumption
 */
PLASMAZONES_EXPORT QVariantMap toVariantMap(const UnifiedLayoutEntry& entry);

/**
 * @brief Convert unified layout entries to QVariantList for QML
 *
 * Convenience function that converts a full list of entries.
 *
 * @param entries Vector of unified layout entries
 * @return QVariantList suitable for QML model
 */
PLASMAZONES_EXPORT QVariantList toVariantList(const QVector<UnifiedLayoutEntry>& entries);

/**
 * @brief Convert a unified layout entry to JSON for D-Bus
 *
 * Creates a JSON object with all layout metadata suitable for
 * serialization over D-Bus.
 *
 * @param entry The unified layout entry
 * @return QJsonObject for D-Bus serialization
 */
PLASMAZONES_EXPORT QJsonObject toJson(const UnifiedLayoutEntry& entry);

/**
 * @brief Find a layout entry by ID
 *
 * Searches the unified layout list for an entry with the given ID.
 *
 * @param entries List to search
 * @param layoutId ID to find
 * @return Index of found entry, or -1 if not found
 */
PLASMAZONES_EXPORT int findLayoutIndex(const QVector<UnifiedLayoutEntry>& entries, const QString& layoutId);

/**
 * @brief Get layout entry by ID
 *
 * @param entries List to search
 * @param layoutId ID to find
 * @return Pointer to entry if found, nullptr otherwise
 */
PLASMAZONES_EXPORT const UnifiedLayoutEntry* findLayout(const QVector<UnifiedLayoutEntry>& entries,
                                                         const QString& layoutId);

} // namespace LayoutUtils

} // namespace PlasmaZones
