// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorCompositor/DecorationDefaults.h>

#include <QColor>
#include <QHash>
#include <QPair>
#include <QRect>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVector>

namespace PhosphorCompositor {

/**
 * @brief Compositor-agnostic autotile border state
 *
 * Tracks which windows are tile-managed (drives border RENDERING) plus the
 * shared border appearance settings. Title-bar/borderless state lives in
 * the DecorationManager's owner model, not here.
 * Per-screen keyed so per-VS retiles can update tracking in isolation
 * without cross-contaminating with windows on sibling virtual screens.
 * Shared across compositor plugins to avoid duplicating state management.
 */
struct BorderState
{
    /// windowId → screen bucket of tile-managed windows (drives border
    /// RENDERING). QHash keyed by screen id so per-VS retiles update only
    /// their own bucket and don't touch windows managed by a sibling VS's
    /// retile. Title-bar (borderless) state is NOT tracked here — that is
    /// the DecorationManager's owner model.
    QHash<QString, QSet<QString>> tiledWindowsByScreen;
    // Defaults shared with the daemon's ConfigDefaults via DecorationDefaults
    // so the effect's pre-settings-load rendering can't drift from what the
    // daemon would persist.
    bool hideTitleBars = DecorationDefaults::HideTitleBars;
    bool showBorder = DecorationDefaults::ShowBorder;
    int width = DecorationDefaults::BorderWidth;
    int radius = DecorationDefaults::BorderRadius;
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
// Border state accessors (pure data queries)
// ═══════════════════════════════════════════════════════════════════════════════

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
    return border.showBorder && isTiledWindow(border, windowId);
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
inline void cleanupClosedWindowState(const QString& windowId, BorderState& border, AutotileWindowState& state)
{
    state.notifiedWindows.remove(windowId);
    state.notifiedWindowScreens.remove(windowId);
    state.minimizeFloatedWindows.remove(windowId);
    state.autotileTargetZones.remove(windowId);
    state.centeredWaylandZones.remove(windowId);
    state.monocleMaximizedWindows.remove(windowId);
    // Closed windows must be removed from every screen bucket — the effect
    // may have stale entries if the window crossed screens before closing.
    for (auto it = border.tiledWindowsByScreen.begin(); it != border.tiledWindowsByScreen.end();) {
        it.value().remove(windowId);
        if (it.value().isEmpty()) {
            it = border.tiledWindowsByScreen.erase(it);
        } else {
            ++it;
        }
    }

    // Sweep the pre-autotile geometry out of EVERY screen bucket — the same
    // cross-screen-stale scenario the tiled sweep above defends against
    // (the window crossed screens before closing) would otherwise leak a
    // geometry entry in the old screen's bucket forever.
    for (auto it = state.preAutotileGeometries.begin(); it != state.preAutotileGeometries.end();) {
        it->remove(windowId);
        if (it->isEmpty()) {
            it = state.preAutotileGeometries.erase(it);
        } else {
            ++it;
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-screen BorderState mutators
// ═══════════════════════════════════════════════════════════════════════════════

/// Add a window to a screen's tiled bucket. Idempotent.
inline void addTiledOnScreen(BorderState& border, const QString& screenId, const QString& windowId)
{
    border.tiledWindowsByScreen[screenId].insert(windowId);
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

/// Remove a window from every screen's tiled bucket EXCEPT @p keepScreenId
/// (cross-screen transfer: a window is tile-managed by one screen at a time,
/// so re-recording on a new screen first strips any stale sibling claim).
inline void removeFromOtherScreens(BorderState& border, const QString& windowId, const QString& keepScreenId)
{
    for (auto it = border.tiledWindowsByScreen.begin(); it != border.tiledWindowsByScreen.end();) {
        if (it.key() != keepScreenId) {
            it.value().remove(windowId);
        }
        // The keep-bucket exclusion below is defensive symmetry only: this
        // function never mutates the keep bucket, so an empty keep bucket
        // could only pre-exist (and every other mutator erases empty
        // buckets on the way out).
        if (it.value().isEmpty() && it.key() != keepScreenId) {
            it = border.tiledWindowsByScreen.erase(it);
        } else {
            ++it;
        }
    }
}

/// Remove a window from every screen's tiled bucket.
/// Returns true if any bucket contained it.
inline bool removeFromAllScreens(BorderState& border, const QString& windowId)
{
    bool any = false;
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

/// Detached copy of the set of tiled windows on a given screen (QHash::value
/// copy, not a reference). Empty if none.
inline QSet<QString> tiledOnScreen(const BorderState& border, const QString& screenId)
{
    return border.tiledWindowsByScreen.value(screenId);
}

/// Collect every tiled (windowId, screenId) pair for bulk operations
/// (hide-titlebars toggle on). Returned as a flat vector so callers can
/// iterate without holding a reference into the hash.
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
} // namespace PhosphorCompositor
