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

} // namespace ScrollTestUtils
