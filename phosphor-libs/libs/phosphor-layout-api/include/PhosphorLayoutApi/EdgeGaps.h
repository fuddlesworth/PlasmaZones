// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace PhosphorLayout {

/**
 * @brief Per-side edge gap values (resolved, non-negative pixel values)
 *
 * Used wherever a layout has independent margins on each screen edge
 * (top/bottom/left/right).  Both manual zone layouts (PhosphorZones::Layout)
 * and tiling-algorithm output (PhosphorTiles::TilingAlgorithm) consume this
 * shape, so it lives in the shared API library that both depend on.
 *
 * When per-side edge gaps are disabled, callers materialise a uniform set
 * via `EdgeGaps::uniform(gap)`.  Default member values (8 px) represent the
 * application default.
 *
 * @note  Manual `Layout::rawOuterGaps()` returns an `EdgeGaps` whose fields
 *        may carry the @c UseGlobal sentinel (meaning "fall back to the
 *        global setting for this side"); those must be resolved via the
 *        layout's `getEffectiveOuterGaps()` before use in geometry
 *        calculations.
 */
struct EdgeGaps
{
    /// Sentinel value meaning "use the global edge-gap setting for this
    /// side" (rather than an explicit pixel override). Manual layouts
    /// store this in any of the four fields to defer to the global
    /// setting; `Layout::getEffectiveOuterGaps` resolves it to a
    /// concrete pixel value before geometry calculations consume the
    /// struct.
    static constexpr int UseGlobal = -1;

    int top = 8;
    int bottom = 8;
    int left = 8;
    int right = 8;
    bool operator==(const EdgeGaps&) const = default;
    bool isUniform() const
    {
        return top == bottom && bottom == left && left == right;
    }
    static EdgeGaps uniform(int gap)
    {
        return {gap, gap, gap, gap};
    }
};

} // namespace PhosphorLayout
