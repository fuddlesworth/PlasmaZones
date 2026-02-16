// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <KColorScheme>
#include <QColor>
#include <QJsonObject>
#include <QString>

namespace PlasmaZones {

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
     * Common values: "master-stack", "bsp", "columns", "fibonacci", "monocle"
     * See AlgorithmRegistry for available algorithms.
     */
    QString algorithmId = QStringLiteral("master-stack");

    // ═══════════════════════════════════════════════════════════════════════
    // Master Area Settings
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Ratio of screen width for master area
     *
     * Range: 0.1 to 0.9
     * Default: 0.6 (60% master, 40% stack)
     */
    qreal splitRatio = 0.6;

    /**
     * @brief Number of windows in master area
     *
     * Range: 1 to 5
     * Default: 1
     */
    int masterCount = 1;

    // ═══════════════════════════════════════════════════════════════════════
    // Gap Settings
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Gap between tiled windows in pixels
     *
     * Range: 0 to 50
     * Default: 8
     */
    int innerGap = 8;

    /**
     * @brief Gap from screen edges in pixels
     *
     * Range: 0 to 50
     * Default: 8
     */
    int outerGap = 8;

    // ═══════════════════════════════════════════════════════════════════════
    // Window Insertion Behavior
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Where to insert new windows
     */
    enum class InsertPosition {
        End,          ///< Add to end of stack (default)
        AfterFocused, ///< Insert after currently focused window
        AsMaster      ///< New window becomes master
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
    // Visual Feedback
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Show border around active/focused window
     */
    bool showActiveBorder = true;

    /**
     * @brief Width of active window border in pixels
     */
    int activeBorderWidth = 2;

    /**
     * @brief Color of active window border
     *
     * Default is empty/invalid. Use systemHighlightColor() or defaults() for proper
     * KDE system color. At runtime, this is set from Settings which reads from KColorScheme.
     */
    QColor activeBorderColor;

    // ═══════════════════════════════════════════════════════════════════════
    // Monocle Mode Settings
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Hide other windows when in monocle mode
     *
     * If true, non-focused windows are minimized.
     * If false, they remain visible but behind the focused window.
     */
    bool monocleHideOthers = true;

    /**
     * @brief Show tab bar in monocle mode
     *
     * Displays a bar showing all windows for easy switching.
     */
    bool monocleShowTabs = false;

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
    // Comparison and Serialization
    // ═══════════════════════════════════════════════════════════════════════

    bool operator==(const AutotileConfig &other) const;
    bool operator!=(const AutotileConfig &other) const;

    /**
     * @brief Serialize to JSON
     */
    QJsonObject toJson() const;

    /**
     * @brief Deserialize from JSON
     */
    static AutotileConfig fromJson(const QJsonObject &json);

    /**
     * @brief Get default configuration with KDE system colors
     *
     * Returns a config with all default values, including the system highlight
     * color from KColorScheme for activeBorderColor.
     */
    static AutotileConfig defaults();

    /**
     * @brief Get the KDE system highlight color
     *
     * Uses KColorScheme to retrieve the current system highlight/selection color.
     * This respects the user's color scheme (light/dark themes, custom colors).
     *
     * @return System highlight color from KColorScheme
     */
    static QColor systemHighlightColor();
};

} // namespace PlasmaZones

// Enable use with QVariant
Q_DECLARE_METATYPE(PlasmaZones::AutotileConfig)
Q_DECLARE_METATYPE(PlasmaZones::AutotileConfig::InsertPosition)