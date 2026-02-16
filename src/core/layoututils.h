// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QFlags>
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Forward declaration for autotile support
namespace PlasmaZones { class AlgorithmRegistry; }

namespace PlasmaZones {

class ILayoutManager;
class Layout;
class Zone;

/**
 * @brief Flags controlling which zone fields to include in conversion (OCP-compliant)
 *
 * These flags allow callers to request minimal or full zone data without duplicating
 * conversion logic. Use Minimal for preview thumbnails, Full for overlay rendering.
 */
enum class ZoneField {
    None = 0,
    Name = 1 << 0,              ///< Include zone name
    Appearance = 1 << 1,        ///< Include colors, opacities, border properties

    Minimal = None,             ///< Id, ZoneNumber, RelativeGeometry only (for previews)
    Full = Name | Appearance    ///< All fields (for overlay rendering)
};
Q_DECLARE_FLAGS(ZoneFields, ZoneField)
Q_DECLARE_OPERATORS_FOR_FLAGS(ZoneFields)

/**
 * @brief Entry in the unified layout list (manual and autotile layouts)
 *
 * Used for quick layout shortcuts (Meta+1-9), layout cycling (Meta+[/]),
 * zone selector display, and D-Bus layout list queries.
 *
 * When isAutotile is true, the entry represents an autotile algorithm rather
 * than a manual zone-based layout. The id will be prefixed with "autotile:".
 */
struct PLASMAZONES_EXPORT UnifiedLayoutEntry {
    QString id;          ///< Layout UUID or autotile prefixed ID (e.g. "autotile:master-stack")
    QString name;       ///< Display name for UI
    QString description; ///< Optional description
    int zoneCount = 0;   ///< Number of zones for manual layouts, or algorithm's defaultMaxWindows for autotile
    QVariantList zones;  ///< Zone data for preview rendering
    QVariantList previewZones; ///< Preview zones (used for autotile algorithm previews)
    bool autoAssign = false; ///< Auto-assign: new windows fill first empty zone
    bool isAutotile = false; ///< True if this entry represents an autotile algorithm

    /**
     * @brief Extract the algorithm ID from an autotile entry
     * @return Algorithm ID (e.g. "master-stack"), or empty string if not autotile
     */
    QString algorithmId() const;

    bool operator==(const UnifiedLayoutEntry& other) const { return id == other.id; }
    bool operator!=(const UnifiedLayoutEntry& other) const { return id != other.id; }
};

/**
 * @brief Utility functions for unified layout management
 *
 * This namespace consolidates layout list building that was previously
 * duplicated in Daemon, ZoneSelectorController, LayoutAdaptor, and OverlayService.
 *
 */
namespace LayoutUtils {

/**
 * @brief Build list of all available layouts (manual, and optionally autotile)
 *
 * @param layoutManager Layout manager interface (can be nullptr)
 * @param includeAutotile If true, append autotile algorithm entries from AlgorithmRegistry
 * @return Vector of unified layout entries
 */
PLASMAZONES_EXPORT QVector<UnifiedLayoutEntry> buildUnifiedLayoutList(ILayoutManager* layoutManager,
                                                                       bool includeAutotile = false);

/**
 * @brief Build filtered list of layouts visible in the given context
 *
 * Filters out layouts that are:
 * - hiddenFromSelector = true
 * - Not allowed on the given screen/desktop/activity (if allow lists are non-empty)
 *
 * @param layoutManager Layout manager interface
 * @param screenName Current screen name (empty = skip screen filter)
 * @param virtualDesktop Current virtual desktop (0 = skip desktop filter)
 * @param activity Current activity ID (empty = skip activity filter)
 * @param includeManual Include manual zone-based layouts (default: true)
 * @param includeAutotile Include dynamic autotile algorithm layouts (default: true)
 */
PLASMAZONES_EXPORT QVector<UnifiedLayoutEntry> buildUnifiedLayoutList(
    ILayoutManager* layoutManager,
    const QString& screenName,
    int virtualDesktop,
    const QString& activity,
    bool includeManual = true,
    bool includeAutotile = true);

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

// ═══════════════════════════════════════════════════════════════════════════
// Allow-list serialization (shared by Layout, LayoutAdaptor, EditorController)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Serialize visibility allow-lists to JSON (only writes non-empty lists)
 */
PLASMAZONES_EXPORT void serializeAllowLists(QJsonObject& json, const QStringList& screens,
                                             const QList<int>& desktops, const QStringList& activities);

/**
 * @brief Deserialize visibility allow-lists from JSON (clears output params first)
 */
PLASMAZONES_EXPORT void deserializeAllowLists(const QJsonObject& json, QStringList& screens,
                                               QList<int>& desktops, QStringList& activities);

// ═══════════════════════════════════════════════════════════════════════════
// Zone conversion utilities
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Convert all zones in a layout to QVariantList
 *
 * @param layout The layout containing zones (returns empty list if null)
 * @param fields Which fields to include for each zone
 * @return QVariantList of zone maps
 */
PLASMAZONES_EXPORT QVariantList zonesToVariantList(Layout* layout, ZoneFields fields = ZoneField::Minimal);

} // namespace LayoutUtils

} // namespace PlasmaZones
