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
 * Per-screen keyed so per-VS retiles can update tracking in isolation
 * without cross-contaminating with windows on sibling virtual screens.
 * Shared across compositor plugins to avoid duplicating state management.
 */
struct BorderState
{
    /// windowId → screen bucket owning its borderless state.
    /// QHash keyed by screen id so per-VS retiles update only their own
    /// bucket and don't touch windows managed by a sibling VS's retile.
    QHash<QString, QSet<QString>> borderlessWindowsByScreen;
    /// Same shape for the full tiled set (superset of borderless).
    QHash<QString, QSet<QString>> tiledWindowsByScreen;
    /// PhosphorZones::Zone geometries remain windowId-keyed — a window only lives in
    /// one zone at a time, regardless of which screen owns it.
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
    for (auto it = border.borderlessWindowsByScreen.constBegin(); it != border.borderlessWindowsByScreen.constEnd();
         ++it) {
        if (it.value().contains(windowId)) {
            return true;
        }
    }
    return false;
}

inline bool isTiledWindow(const BorderState& border, const QString& windowId)
{
    for (auto it = border.tiledWindowsByScreen.constBegin(); it != border.tiledWindowsByScreen.constEnd(); ++it) {
        if (it.value().contains(windowId)) {
            return true;
        }
    }
    return false;
}

inline bool shouldShowBorderForWindow(const BorderState& border, const QString& windowId)
{
    if (!border.showBorder)
        return false;
    return isBorderlessWindow(border, windowId) || isTiledWindow(border, windowId);
}

inline bool shouldApplyBorderInset(const BorderState& border, const QString& windowId)
{
    return border.hideTitleBars && border.width > 0 && isBorderlessWindow(border, windowId);
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
    // Closed windows must be removed from every screen bucket — the effect
    // may have stale entries if the window crossed screens before closing.
    for (auto it = border.borderlessWindowsByScreen.begin(); it != border.borderlessWindowsByScreen.end();) {
        it.value().remove(windowId);
        if (it.value().isEmpty()) {
            it = border.borderlessWindowsByScreen.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = border.tiledWindowsByScreen.begin(); it != border.tiledWindowsByScreen.end();) {
        it.value().remove(windowId);
        if (it.value().isEmpty()) {
            it = border.tiledWindowsByScreen.erase(it);
        } else {
            ++it;
        }
    }
    border.zoneGeometries.remove(windowId);

    if (!screenId.isEmpty()) {
        auto it = state.preAutotileGeometries.find(screenId);
        if (it != state.preAutotileGeometries.end()) {
            it->remove(windowId);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-screen BorderState mutators
// ═══════════════════════════════════════════════════════════════════════════════

/// Add a window to a screen's borderless bucket. Idempotent.
inline void addBorderlessOnScreen(BorderState& border, const QString& screenId, const QString& windowId)
{
    border.borderlessWindowsByScreen[screenId].insert(windowId);
}

/// Add a window to a screen's tiled bucket. Idempotent.
inline void addTiledOnScreen(BorderState& border, const QString& screenId, const QString& windowId)
{
    border.tiledWindowsByScreen[screenId].insert(windowId);
}

/// Remove a window from a specific screen's borderless bucket.
/// Returns true if it was present. Erases the bucket if it becomes empty.
inline bool removeBorderlessOnScreen(BorderState& border, const QString& screenId, const QString& windowId)
{
    auto it = border.borderlessWindowsByScreen.find(screenId);
    if (it == border.borderlessWindowsByScreen.end()) {
        return false;
    }
    const bool removed = it.value().remove(windowId);
    if (it.value().isEmpty()) {
        border.borderlessWindowsByScreen.erase(it);
    }
    return removed;
}

/// Remove a window from a specific screen's tiled bucket.
/// Returns true if it was present. Erases the bucket if it becomes empty.
inline bool removeTiledOnScreen(BorderState& border, const QString& screenId, const QString& windowId)
{
    auto it = border.tiledWindowsByScreen.find(screenId);
    if (it == border.tiledWindowsByScreen.end()) {
        return false;
    }
    const bool removed = it.value().remove(windowId);
    if (it.value().isEmpty()) {
        border.tiledWindowsByScreen.erase(it);
    }
    return removed;
}

/// Remove a window from all screen buckets (both borderless and tiled).
/// Returns true if any bucket contained it.
inline bool removeFromAllScreens(BorderState& border, const QString& windowId)
{
    bool any = false;
    for (auto it = border.borderlessWindowsByScreen.begin(); it != border.borderlessWindowsByScreen.end();) {
        if (it.value().remove(windowId)) {
            any = true;
        }
        if (it.value().isEmpty()) {
            it = border.borderlessWindowsByScreen.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = border.tiledWindowsByScreen.begin(); it != border.tiledWindowsByScreen.end();) {
        if (it.value().remove(windowId)) {
            any = true;
        }
        if (it.value().isEmpty()) {
            it = border.tiledWindowsByScreen.erase(it);
        } else {
            ++it;
        }
    }
    return any;
}

/// Read-only view of the set of tiled windows on a given screen. Empty if none.
inline QSet<QString> tiledOnScreen(const BorderState& border, const QString& screenId)
{
    return border.tiledWindowsByScreen.value(screenId);
}

/// Read-only view of the set of borderless windows on a given screen. Empty if none.
inline QSet<QString> borderlessOnScreen(const BorderState& border, const QString& screenId)
{
    return border.borderlessWindowsByScreen.value(screenId);
}

/// Collect every borderless (windowId, screenId) pair for bulk operations
/// (feature disable, hide-titlebars toggle off). Returned as a flat vector
/// so callers can iterate without holding a reference into the hash.
inline QVector<QPair<QString, QString>> allBorderlessPairs(const BorderState& border)
{
    QVector<QPair<QString, QString>> result;
    for (auto it = border.borderlessWindowsByScreen.constBegin(); it != border.borderlessWindowsByScreen.constEnd();
         ++it) {
        for (const QString& wid : it.value()) {
            result.append({wid, it.key()});
        }
    }
    return result;
}

/// Same for tiled. Used by updateHideTitleBarsSetting's toggle-on path.
inline QVector<QPair<QString, QString>> allTiledPairs(const BorderState& border)
{
    QVector<QPair<QString, QString>> result;
    for (auto it = border.tiledWindowsByScreen.constBegin(); it != border.tiledWindowsByScreen.constEnd(); ++it) {
        for (const QString& wid : it.value()) {
            result.append({wid, it.key()});
        }
    }
    return result;
}

} // namespace AutotileStateHelpers
} // namespace PlasmaZones
