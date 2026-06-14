// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QColor>

namespace PhosphorZones {

/**
 * @brief Library-owned default values for zone presentation.
 *
 * These are the canonical defaults the zone primitives fall back to when
 * a value isn't explicitly persisted (e.g. an old layout file missing a
 * border-width key, a freshly constructed Zone with no overrides). The
 * application's config layer delegates its user-facing zone-default
 * accessors downward to these, so there is a single source of truth —
 * same pattern as `PhosphorTiles::AutotileDefaults`.
 */
namespace ZoneDefaults {

// Alpha values for semi-transparent zone colors.
constexpr int HighlightAlpha = 128;
constexpr int InactiveAlpha = 64;
constexpr int BorderAlpha = 200;
constexpr int OpaqueAlpha = 255;

// Default colors (use inline so they're defined exactly once across TUs).
inline const QColor HighlightColor{0, 120, 212, HighlightAlpha}; ///< Windows blue
inline const QColor InactiveColor{128, 128, 128, InactiveAlpha};
inline const QColor BorderColor{255, 255, 255, BorderAlpha};
inline const QColor LabelFontColor{255, 255, 255, OpaqueAlpha};

// Per-zone appearance defaults.
constexpr qreal Opacity = 0.5;
constexpr qreal InactiveOpacity = 0.3;
constexpr int BorderWidth = 2;
constexpr int BorderRadius = 8;

// Zone-detection defaults.
constexpr int AdjacentThreshold = 20; ///< Pixel distance considered "adjacent" for multi-zone span detection.

// Layout-factory split ratios (used when constructing template layouts —
// priority-grid main/secondary split, focus-mode side/main columns).
constexpr qreal PriorityGridMainRatio = 0.667;
constexpr qreal PriorityGridSecondaryRatio = 0.333;
constexpr qreal FocusSideRatio = 0.2;
constexpr qreal FocusMainRatio = 0.6;

} // namespace ZoneDefaults

} // namespace PhosphorZones
