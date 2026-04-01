// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include "config/configdefaults.h"
#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>

namespace PlasmaZones {

/**
 * @brief Per-algorithm saved settings (split ratio + master count)
 *
 * Replaces AlgorithmSettings for clarity. Saved when switching away
 * from an algorithm, restored when switching back.
 */
struct AlgorithmSettings
{
    qreal splitRatio = ConfigDefaults::autotileSplitRatio();
    int masterCount = ConfigDefaults::autotileMasterCount();
    QVariantMap customParams; ///< Algorithm-declared custom parameter values
    bool operator==(const AlgorithmSettings& other) const
    {
        if (masterCount != other.masterCount || !qFuzzyCompare(1.0 + splitRatio, 1.0 + other.splitRatio)) {
            return false;
        }
        if (customParams.size() != other.customParams.size()) {
            return false;
        }
        // Fuzzy-compare numeric custom param values to match splitRatio semantics.
        // After JSON round-trip, numbers may arrive as Int/LongLong rather than Double,
        // so check canConvert<double> rather than requiring exact QMetaType::Double.
        for (auto it = customParams.constBegin(); it != customParams.constEnd(); ++it) {
            auto oit = other.customParams.constFind(it.key());
            if (oit == other.customParams.constEnd()) {
                return false;
            }
            const QVariant& a = it.value();
            const QVariant& b = oit.value();
            const bool aNumeric = AutotileDefaults::isNumericMetaType(a.typeId());
            const bool bNumeric = AutotileDefaults::isNumericMetaType(b.typeId());
            const bool aBool = a.typeId() == QMetaType::Bool;
            const bool bBool = b.typeId() == QMetaType::Bool;
            if (aNumeric && bNumeric) {
                if (!qFuzzyCompare(1.0 + a.toDouble(), 1.0 + b.toDouble())) {
                    return false;
                }
            } else if (aBool && bBool) {
                if (a.toBool() != b.toBool()) {
                    return false;
                }
            } else if (a != b) {
                return false;
            }
        }
        return true;
    }
};

/**
 * @brief Configuration for autotiling behavior
 *
 * AutotileConfig holds all user-configurable options for automatic
 * window tiling. This includes algorithm selection, gaps, master settings,
 * and focus behavior.
 *
 * This is a value type (not QObject) for easy copying and comparison.
 * It can be stored per-layout or as global defaults.
 *
 * @note Default values here must match AutotileDefaults in constants.h.
 *       Validation and clamping use those shared constants.
 */
struct PLASMAZONES_EXPORT AutotileConfig
{
    // ═══════════════════════════════════════════════════════════════════════
    // Algorithm Selection
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief ID of the tiling algorithm to use
     *
     * Common values: "master-stack", "bsp", "columns", "dwindle", "spiral", "monocle"
     * See AlgorithmRegistry for available algorithms.
     */
    QString algorithmId = ConfigDefaults::defaultAutotileAlgorithm();

    // ═══════════════════════════════════════════════════════════════════════
    // Master Area Settings
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Ratio of screen width for master area
     *
     * Range: 0.1 to 0.9
     * Default: 0.6 (60% master, 40% stack)
     */
    qreal splitRatio = ConfigDefaults::autotileSplitRatio();

    /**
     * @brief Number of windows in master area
     *
     * Range: 1 to 5
     * Default: 1
     */
    int masterCount = ConfigDefaults::autotileMasterCount();

    /// Per-algorithm saved settings (split ratio + master count).
    /// Saved when switching away from an algorithm, restored when switching back.
    /// Key: algorithm ID (e.g. "master-stack", "centered-master", "script:deck")
    QHash<QString, AlgorithmSettings> savedAlgorithmSettings;

    /// Convert per-algorithm settings from QVariantMap (Settings layer) to internal hash
    static QHash<QString, AlgorithmSettings> perAlgoFromVariantMap(const QVariantMap& map);
    /// Convert internal hash to QVariantMap for the Settings layer
    static QVariantMap perAlgoToVariantMap(const QHash<QString, AlgorithmSettings>& hash);

    // ═══════════════════════════════════════════════════════════════════════
    // Gap Settings
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Gap between tiled windows in pixels
     *
     * Range: 0 to 50
     * Default: 8
     */
    int innerGap = ConfigDefaults::autotileInnerGap();

    /**
     * @brief Gap from screen edges in pixels (uniform)
     *
     * Range: 0 to 50
     * Default: 8
     */
    int outerGap = ConfigDefaults::autotileOuterGap();

    /**
     * @brief Whether to use per-side outer gaps instead of uniform
     */
    bool usePerSideOuterGap = false;

    /**
     * @brief Per-side outer gap values (used when usePerSideOuterGap is true)
     */
    int outerGapTop = 8;
    int outerGapBottom = 8;
    int outerGapLeft = 8;
    int outerGapRight = 8;

    // ═══════════════════════════════════════════════════════════════════════
    // Window Insertion Behavior
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Where to insert new windows
     */
    enum class InsertPosition {
        End, ///< Add to end of stack (default)
        AfterFocused, ///< Insert after currently focused window
        AsMaster ///< New window becomes master
    };

    InsertPosition insertPosition = InsertPosition::End;

    // ═══════════════════════════════════════════════════════════════════════
    // Focus Behavior
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Focus follows mouse pointer
     *
     * When true, moving mouse over a window focuses it.
     * Default: false (click to focus)
     */
    bool focusFollowsMouse = false;

    /**
     * @brief Automatically focus newly opened windows
     *
     * Default: true
     */
    bool focusNewWindows = true;

    // ═══════════════════════════════════════════════════════════════════════
    // Smart Features
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Hide gaps when only one window is tiled
     *
     * Single window uses full available screen space.
     */
    bool smartGaps = true;

    /**
     * @brief Respect window minimum size constraints
     *
     * When true, windows won't be resized smaller than their minimum.
     * This may cause layout to not fill screen completely.
     */
    bool respectMinimumSize = true;

    // ═══════════════════════════════════════════════════════════════════════
    // Window Limits
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Maximum number of windows to tile
     *
     * Windows beyond this limit are not repositioned by the tiling algorithm.
     * Range: 1 to 12
     * Default: 6
     */
    int maxWindows = ConfigDefaults::autotileMaxWindows();

    // ═══════════════════════════════════════════════════════════════════════
    // Comparison and Serialization
    // ═══════════════════════════════════════════════════════════════════════

    bool operator==(const AutotileConfig& other) const;
    bool operator!=(const AutotileConfig& other) const;

    /**
     * @brief Serialize to JSON
     */
    QJsonObject toJson() const;

    /**
     * @brief Deserialize from JSON
     */
    static AutotileConfig fromJson(const QJsonObject& json);

    /**
     * @brief Get default configuration
     */
    static AutotileConfig defaults();
};

} // namespace PlasmaZones

// Enable use with QVariant
Q_DECLARE_METATYPE(PlasmaZones::AutotileConfig)
Q_DECLARE_METATYPE(PlasmaZones::AutotileConfig::InsertPosition)