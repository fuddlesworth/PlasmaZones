// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>

// EdgeGaps is the per-side gap shape shared between manual layout and tiling;
// it lives in libs/phosphor-layout-api so neither side has to depend on the
// other's headers.
#include <PhosphorLayoutApi/EdgeGaps.h>

#include <QRect>
#include <QSize>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <functional>

namespace PhosphorTiles {

// Shorthand for the shared shape — re-declared here (rather than relying on a
// transitive include of core/constants.h) so this header is self-contained.
using EdgeGaps = ::PhosphorLayout::EdgeGaps;

class TilingState;

/**
 * @brief Per-window metadata passed to algorithms
 *
 * Provides identity and state for each tiled window so algorithms
 * can make app-aware layout decisions (e.g., "browser always master").
 */
struct WindowInfo
{
    QString appId; ///< Application identifier (e.g., "firefox", "org.kde.dolphin")
    bool focused = false; ///< Whether this window currently has focus
};

/**
 * @brief Screen metadata passed to tiling algorithms
 *
 * Provides physical screen context so algorithms can adapt to
 * monitor orientation and multi-monitor setups.
 */
struct TilingScreenInfo
{
    QString id; ///< Screen connector name (e.g., "HDMI-1", "DP-2")
    bool portrait = false; ///< True if height > width (portrait orientation)
    qreal aspectRatio = 0.0; ///< width/height (e.g., 1.78 for 16:9, 0.56 for portrait)
};

/**
 * @brief Parameters for zone calculation
 *
 * Bundles all inputs to calculateZones() into a single struct so new
 * parameters can be added without changing the virtual interface.
 */
struct TilingParams
{
    int windowCount = 0; ///< Number of windows to tile
    QRect screenGeometry; ///< Available screen area in absolute pixels
    /// Current tiling state. Must be non-null for all algorithm calls —
    /// algorithms may dereference without checking. The engine guarantees
    /// this by constructing TilingState before calling calculateZones().
    const TilingState* state = nullptr;
    int innerGap = 0; ///< Gap between adjacent zones in pixels
    EdgeGaps outerGaps; ///< Gaps at screen edges in pixels (per-side)
    QVector<QSize> minSizes = {}; ///< Per-window minimum sizes (may be empty)

    // ── Enriched context (v2) ──────────────────────────────────────────
    QVector<WindowInfo> windowInfos; ///< Per-window metadata (parallel to window list)
    int focusedIndex = -1; ///< Index of focused window in tiled list (-1 = unknown)
    TilingScreenInfo screenInfo; ///< Physical screen metadata
    QVariantMap customParams; ///< Algorithm-declared custom parameters

    /// Create minimal params for preview rendering (no per-window/screen context)
    static TilingParams forPreview(int count, const QRect& rect, const TilingState* state)
    {
        TilingParams p;
        p.windowCount = count;
        p.screenGeometry = rect;
        p.state = state;
        return p;
    }
};

/**
 * @brief Build per-window metadata from a TilingState
 *
 * Shared between AutotileEngine (for TilingParams construction) and
 * ScriptedAlgorithm (for lifecycle hook JS state). Identifies the focused
 * window; app class is resolved via the caller-supplied @p appIdResolver so
 * live class lookups hit the WindowRegistry instead of parsing stale strings.
 *
 * TilingState::m_windowOrder contains bare instance ids; parsing them as
 * "appId|uuid" would hand hex strings to user-authored JS algorithms. The
 * resolver lets each caller plug in whatever knows the live class for a
 * given instance id (typically AutotileEngine::currentAppIdFor bound as a
 * lambda, which consults the shared WindowRegistry).
 *
 * @param state         Current tiling state (may be null — returns empty vector)
 * @param windowCount   Number of windows to process (may differ from state->tiledWindowCount())
 * @param appIdResolver Function that maps an instance id to its current class.
 *                      Pass a no-op returning QString() to keep info.appId empty.
 * @param[out] focusedIndex Set to the index of the focused window, or -1
 * @return WindowInfo vector (empty if state is null; size may be less than windowCount)
 */
PHOSPHORTILES_EXPORT QVector<WindowInfo> buildWindowInfos(const TilingState* state, int windowCount,
                                                          const std::function<QString(const QString&)>& appIdResolver,
                                                          int& focusedIndex);

} // namespace PhosphorTiles
