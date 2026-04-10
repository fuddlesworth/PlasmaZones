// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QHash>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>
#include <optional>

namespace PlasmaZones {

/**
 * @brief Compositor-agnostic autotile border state
 *
 * Tracks which windows are borderless/tiled and their zone geometries.
 * Shared across compositor plugins to avoid duplicating state management.
 */
struct BorderState
{
    QSet<QString> borderlessWindows;
    QSet<QString> tiledWindows; ///< all currently tiled windows (for showBorder without hideTitleBars)
    QHash<QString, QRect> zoneGeometries;
    bool hideTitleBars = false;
    bool showBorder = false;
    int width = 2;
    int radius = 0;
    QColor color;
    QColor inactiveColor;
};

/**
 * @brief Compositor-agnostic autotile state accessors and pure helpers
 *
 * These functions operate on BorderState and other pure data structures
 * without touching any compositor APIs. Shared by all compositor plugins.
 */
namespace AutotileStateHelpers {

// ═══════════════════════════════════════════════════════════════════════════════
// Saved geometry helpers (pure QHash operations)
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Find key in saved geometries map for a window (exact match only)
 */
inline QString findSavedGeometryKey(const QHash<QString, QRectF>& savedGeometries, const QString& windowId)
{
    auto it = savedGeometries.constFind(windowId);
    if (it != savedGeometries.constEnd()) {
        return it.key();
    }
    return QString();
}

/**
 * @brief Check if we already have saved geometry for this window
 */
inline bool hasSavedGeometryForWindow(const QHash<QString, QRectF>& savedGeometries, const QString& windowId)
{
    return !findSavedGeometryKey(savedGeometries, windowId).isEmpty();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Border state accessors (pure data queries)
// ═══════════════════════════════════════════════════════════════════════════════

inline bool isBorderlessWindow(const BorderState& border, const QString& windowId)
{
    return border.borderlessWindows.contains(windowId);
}

inline bool isTiledWindow(const BorderState& border, const QString& windowId)
{
    return border.tiledWindows.contains(windowId);
}

inline bool shouldShowBorderForWindow(const BorderState& border, const QString& windowId)
{
    if (!border.showBorder)
        return false;
    return isBorderlessWindow(border, windowId) || isTiledWindow(border, windowId);
}

inline bool shouldApplyBorderInset(const BorderState& border, const QString& windowId)
{
    return border.hideTitleBars && border.width > 0 && border.borderlessWindows.contains(windowId);
}

inline std::optional<QRect> borderZoneGeometry(const BorderState& border, const QString& windowId)
{
    auto it = border.zoneGeometries.constFind(windowId);
    if (it != border.zoneGeometries.constEnd()) {
        return it.value();
    }
    return std::nullopt;
}

inline QVector<QRect> allBorderZoneGeometries(const BorderState& border)
{
    QVector<QRect> result;
    result.reserve(border.zoneGeometries.size());
    for (auto it = border.zoneGeometries.constBegin(); it != border.zoneGeometries.constEnd(); ++it) {
        result.append(it.value());
    }
    return result;
}

/**
 * @brief Apply border inset to a geometry (shrink by border width on all sides)
 */
inline QRect applyBorderInset(const QRect& geo, int borderWidth)
{
    return geo.adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth);
}

/**
 * @brief Check if border inset should be applied for a window's geometry
 */
inline bool shouldInsetForBorder(const BorderState& border, const QString& windowId, const QRect& geo)
{
    return shouldApplyBorderInset(border, windowId) && geo.width() > border.width * 2
        && geo.height() > border.width * 2;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window closed cleanup (pure state bookkeeping)
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Bundles per-window state maps for cleanup operations.
 *
 * Avoids passing 8+ individual references to cleanupClosedWindowState().
 * The caller constructs this from their member variables.
 */
struct AutotileWindowState
{
    QSet<QString>& notifiedWindows;
    QHash<QString, QString>& notifiedWindowScreens;
    QSet<QString>& minimizeFloatedWindows;
    QHash<QString, QRect>& autotileTargetZones;
    QHash<QString, QRect>& centeredWaylandZones;
    QSet<QString>& monocleMaximizedWindows;
    QHash<QString, QHash<QString, QRectF>>& preAutotileGeometries;
};

/**
 * @brief Clean up all state tracking for a closed window.
 *
 * Removes the window from all QSet/QHash state maps.
 * Does NOT handle D-Bus calls or compositor-specific cleanup.
 */
inline void cleanupClosedWindowState(const QString& windowId, const QString& screenId, BorderState& border,
                                     AutotileWindowState& state)
{
    state.notifiedWindows.remove(windowId);
    state.notifiedWindowScreens.remove(windowId);
    state.minimizeFloatedWindows.remove(windowId);
    state.autotileTargetZones.remove(windowId);
    state.centeredWaylandZones.remove(windowId);
    state.monocleMaximizedWindows.remove(windowId);
    border.borderlessWindows.remove(windowId);
    border.tiledWindows.remove(windowId);
    border.zoneGeometries.remove(windowId);

    if (!screenId.isEmpty()) {
        auto it = state.preAutotileGeometries.find(screenId);
        if (it != state.preAutotileGeometries.end()) {
            it->remove(windowId);
        }
    }
}

} // namespace AutotileStateHelpers
} // namespace PlasmaZones
