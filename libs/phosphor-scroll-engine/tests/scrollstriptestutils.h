// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Shared fixture helpers for the ScrollStrip test suite. One definition so
// the work area / gap cannot silently diverge between the two files.

#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

namespace ScrollTestUtils {

inline PhosphorScrollEngine::ScrollLayoutParams defaultParams()
{
    PhosphorScrollEngine::ScrollLayoutParams p;
    p.workArea = QRect(0, 0, 1200, 800);
    p.gap = 10;
    return p;
}

inline const PhosphorScrollEngine::ColumnWidth kHalf = PhosphorScrollEngine::ColumnWidth::makeProportion(0.5);

/// The tile's resolved rect, or a null QRect when the window is ABSENT
/// from the resolve. Callers asserting on a rect property should pair it
/// with `resolveContains` — a null rect makes negative assertions (e.g.
/// `!isHidden`) pass vacuously for a dropped tile.
inline QRect rectOf(const PhosphorScrollEngine::ResolvedStrip& resolved, const QString& windowId)
{
    for (const PhosphorScrollEngine::ResolvedColumn& rc : resolved.columns) {
        for (const PhosphorScrollEngine::ResolvedTile& rt : rc.tiles) {
            if (rt.windowId == windowId) {
                return rt.rect;
            }
        }
    }
    return {};
}

/// True when @p windowId appears anywhere in the resolve.
inline bool resolveContains(const PhosphorScrollEngine::ResolvedStrip& resolved, const QString& windowId)
{
    for (const PhosphorScrollEngine::ResolvedColumn& rc : resolved.columns) {
        for (const PhosphorScrollEngine::ResolvedTile& rt : rc.tiles) {
            if (rt.windowId == windowId) {
                return true;
            }
        }
    }
    return false;
}

} // namespace ScrollTestUtils
