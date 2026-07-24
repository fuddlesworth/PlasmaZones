// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "configdefaults_appearance.h"

namespace PlasmaZones {

// Chain link 2: shared inner/outer gap + adjacency default accessors.
class ConfigDefaultsGaps : public ConfigDefaultsAppearance
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // PhosphorZones::Zone Settings
    // ═══════════════════════════════════════════════════════════════════════════

    // Inner gap: the single shared inter-window gap used by BOTH snapping and
    // tiling (replaces the former snapping zonePadding + tiling autotileInnerGap).
    static int innerGap()
    {
        return Defaults::InnerGap;
    }
    static constexpr int innerGapMin()
    {
        return 0;
    }
    static constexpr int innerGapMax()
    {
        return Defaults::MaxGap;
    }
    static int outerGap()
    {
        return Defaults::OuterGap;
    }
    static constexpr int outerGapMin()
    {
        return 0;
    }
    static constexpr int outerGapMax()
    {
        return Defaults::MaxGap;
    }
    static bool usePerSideOuterGap()
    {
        return false;
    }
    static int outerGapTop()
    {
        return Defaults::OuterGap;
    }
    static constexpr int outerGapTopMin()
    {
        return 0;
    }
    static constexpr int outerGapTopMax()
    {
        return Defaults::MaxGap;
    }
    static int outerGapBottom()
    {
        return Defaults::OuterGap;
    }
    static constexpr int outerGapBottomMin()
    {
        return 0;
    }
    static constexpr int outerGapBottomMax()
    {
        return Defaults::MaxGap;
    }
    static int outerGapLeft()
    {
        return Defaults::OuterGap;
    }
    static constexpr int outerGapLeftMin()
    {
        return 0;
    }
    static constexpr int outerGapLeftMax()
    {
        return Defaults::MaxGap;
    }
    static int outerGapRight()
    {
        return Defaults::OuterGap;
    }
    static constexpr int outerGapRightMin()
    {
        return 0;
    }
    static constexpr int outerGapRightMax()
    {
        return Defaults::MaxGap;
    }
    static int adjacentThreshold()
    {
        return ::PhosphorZones::ZoneDefaults::AdjacentThreshold;
    }
    static constexpr int adjacentThresholdMin()
    {
        return 5;
    }
    static constexpr int adjacentThresholdMax()
    {
        return 500;
    }
};

} // namespace PlasmaZones
