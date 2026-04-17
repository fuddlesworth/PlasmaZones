// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// UnifiedLayoutEntry + picker-composition helpers.
//
// A "unified layout entry" is one row in the layout-picker UI — it can back
// either a manual zone-based layout OR an autotile algorithm, and the
// composition functions stitch both sources into a single sorted list for
// the overlay / zone selector / D-Bus layout list.
//
// Lives OUTSIDE layoututils.h because it pulls in autotile's
// AlgorithmRegistry to compose algorithm entries. Keeping the autotile
// coupling in this separate TU lets layoututils stay pure zone/layout
// primitives (prerequisite for extracting those into phosphor-zones
// without dragging autotile into the library's dependency graph).

#include "plasmazones_export.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace PlasmaZones {

class ILayoutManager;
class IOrderingSettings;
class Layout;

/**
 * @brief Entry in the unified layout list (manual and autotile layouts)
 *
 * Used for quick layout shortcuts (Meta+1-9), layout cycling (Meta+[/]),
 * zone selector display, and D-Bus layout list queries.
 *
 * When isAutotile is true, the entry represents an autotile algorithm rather
 * than a manual zone-based layout. The id will be prefixed with "autotile:".
 */
struct PLASMAZONES_EXPORT UnifiedLayoutEntry
{
    QString id; ///< Layout UUID or autotile prefixed ID (e.g. "autotile:master-stack")
    QString name; ///< Display name for UI
    QString description; ///< Optional description
    int zoneCount = 0; ///< Number of zones for manual layouts, or algorithm's defaultMaxWindows for autotile
    QVariantList zones; ///< Zone data for preview rendering
    QVariantList previewZones; ///< Preview zones (used for autotile algorithm previews)
    bool autoAssign = false; ///< Auto-assign: new windows fill first empty zone
    bool isAutotile = false; ///< True if this entry represents an autotile algorithm
    int aspectRatioClass = 0; ///< AspectRatioClass enum value (0=Any, 1=Standard, etc.)
    qreal referenceAspectRatio = 0.0; ///< For fixed-geometry layouts: the screen AR zones were designed for
    bool recommended = true; ///< True if layout matches the current screen's aspect ratio
    QString zoneNumberDisplay; ///< How zone numbers are displayed in previews ("all", "last", etc.)
    bool memory = false; ///< True if algorithm maintains persistent state (SplitTree)
    bool supportsMasterCount = false; ///< True if algorithm supports configurable master window count
    bool supportsSplitRatio = false; ///< True if algorithm supports configurable split ratio
    bool producesOverlappingZones = false; ///< True if algorithm can produce overlapping zones
    bool supportsCustomParams = false; ///< True if algorithm declares @param custom parameters
    bool isScripted = false; ///< True if algorithm is loaded from a .js script file
    bool isUserScript = false; ///< True if script is from the user's local directory

    // ── Generic section grouping (data-driven, consumed by LayoutsPage) ──
    QString sectionKey; ///< Grouping key (e.g. "any", "standard", "built-in", "custom")
    QString sectionLabel; ///< Display label for the section header (i18n'd)
    int sectionOrder = 0; ///< Sort priority (lower = first)

    /// Whether this autotile entry should show as a "system" (lock icon) item.
    /// Built-in C++ algorithms are always system entries. Scripted algorithms
    /// are system entries only if they are system-installed (not user scripts).
    bool isSystemEntry() const
    {
        if (!isAutotile)
            return false;
        if (!isScripted)
            return true; // Built-in C++ algorithm
        return !isUserScript; // System-installed script = system, user script = not system
    }

    /**
     * @brief Extract the algorithm ID from an autotile entry
     * @return Algorithm ID (e.g. "master-stack"), or empty string if not autotile
     */
    QString algorithmId() const;

    bool operator==(const UnifiedLayoutEntry& other) const
    {
        return id == other.id;
    }
    bool operator!=(const UnifiedLayoutEntry& other) const
    {
        return id != other.id;
    }
};

/**
 * @brief Picker-composition functions for unified layout lists.
 *
 * These live in the LayoutUtils namespace for historical reasons (consumers
 * used to include a single layoututils.h). The namespace is shared with
 * the pure-layout helpers in layoututils.h; include whichever header you
 * need.
 */
namespace LayoutUtils {

/**
 * @brief Build list of all available layouts (manual, and optionally autotile)
 */
PLASMAZONES_EXPORT QVector<UnifiedLayoutEntry> buildUnifiedLayoutList(ILayoutManager* layoutManager,
                                                                      bool includeAutotile = false,
                                                                      const QStringList& customOrder = {});

/**
 * @brief Build filtered list of layouts visible in the given context
 *
 * Filters out layouts that are:
 * - hiddenFromSelector = true
 * - Not allowed on the given screen/desktop/activity (if allow lists are non-empty)
 * - Not matching the screen's aspect ratio class (if layout has an aspectRatioClass tag)
 *
 * Layouts tagged with a non-matching aspect ratio class are not removed entirely;
 * they are moved to the end of the list so the selector can show them in a
 * collapsed "Other" section. The `recommended` field in the returned entry
 * indicates whether the layout matches the current screen's aspect ratio.
 */
PLASMAZONES_EXPORT QVector<UnifiedLayoutEntry>
buildUnifiedLayoutList(ILayoutManager* layoutManager, const QString& screenId, int virtualDesktop,
                       const QString& activity, bool includeManual = true, bool includeAutotile = true,
                       qreal screenAspectRatio = 0.0, bool filterByAspectRatio = false,
                       const QStringList& customOrder = {});

/**
 * @brief Build a combined custom order list from settings
 */
PLASMAZONES_EXPORT QStringList buildCustomOrder(const IOrderingSettings* settings, bool includeManual,
                                                bool includeAutotile);

/**
 * @brief Convert a unified layout entry to QVariantMap for QML
 */
PLASMAZONES_EXPORT QVariantMap toVariantMap(const UnifiedLayoutEntry& entry);

/**
 * @brief Convert unified layout entries to QVariantList for QML
 */
PLASMAZONES_EXPORT QVariantList toVariantList(const QVector<UnifiedLayoutEntry>& entries);

/**
 * @brief Convert a unified layout entry to JSON for D-Bus
 */
PLASMAZONES_EXPORT QJsonObject toJson(const UnifiedLayoutEntry& entry);

/**
 * @brief Find a layout entry by ID
 * @return Index of found entry, or -1 if not found
 */
PLASMAZONES_EXPORT int findLayoutIndex(const QVector<UnifiedLayoutEntry>& entries, const QString& layoutId);

/**
 * @brief Get layout entry by ID
 * @return Pointer to entry if found, nullptr otherwise
 */
PLASMAZONES_EXPORT const UnifiedLayoutEntry* findLayout(const QVector<UnifiedLayoutEntry>& entries,
                                                        const QString& layoutId);

} // namespace LayoutUtils

} // namespace PlasmaZones
