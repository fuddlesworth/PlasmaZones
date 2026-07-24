// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "configdefaults_gaps.h"

namespace PlasmaZones {

// Chain link 3: performance, exclusion, and animation/decoration window-filtering
// (min-size / transient) default accessors.
class ConfigDefaultsLimits : public ConfigDefaultsGaps
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Performance Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static int pollIntervalMs()
    {
        return Defaults::PollIntervalMs;
    }
    static constexpr int pollIntervalMsMin()
    {
        return 10;
    }
    static constexpr int pollIntervalMsMax()
    {
        return 1000;
    }
    static int minimumZoneSizePx()
    {
        return Defaults::MinimumZoneSizePx;
    }
    static constexpr int minimumZoneSizePxMin()
    {
        return 50;
    }
    static constexpr int minimumZoneSizePxMax()
    {
        return 500;
    }
    static int minimumZoneDisplaySizePx()
    {
        return Defaults::MinimumZoneDisplaySizePx;
    }
    static constexpr int minimumZoneDisplaySizePxMin()
    {
        return 1;
    }
    static constexpr int minimumZoneDisplaySizePxMax()
    {
        return 50;
    }
    // ═══════════════════════════════════════════════════════════════════════════
    // Exclusion Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static bool excludeTransientWindows()
    {
        return true;
    }
    static int minimumWindowWidth()
    {
        return 200;
    }
    static constexpr int minimumWindowWidthMin()
    {
        return 0;
    }
    static constexpr int minimumWindowWidthMax()
    {
        return 2000;
    }
    static int minimumWindowHeight()
    {
        return 150;
    }
    static constexpr int minimumWindowHeightMin()
    {
        return 0;
    }
    static constexpr int minimumWindowHeightMax()
    {
        return 2000;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Animation Window Filtering Settings
    //
    // Mirrors the snapping/tiling Exclusion settings but lives in its own
    // `Animations.WindowFiltering` group so a user can disable animations
    // for an app while still snapping it (or vice versa). The defaults
    // are deliberately permissive — every window animates unless the
    // user opts in to a filter — because animations don't have the
    // user-data-loss risk that drives the conservative snapping defaults.
    // App rules with a matching classPattern override the filter at the
    // resolver layer, so a class-targeted rule can re-enable animations
    // for an otherwise-excluded app.
    // ═══════════════════════════════════════════════════════════════════════════

    static bool animationExcludeTransientWindows()
    {
        return false;
    }
    // Notification and OSD surfaces are excluded from window-event
    // animations by default — unlike transient windows they are almost
    // never something a user wants a window-open shader on, and they
    // are driven by the shell rather than the user. Opt-in available.
    static bool animationExcludeNotificationsAndOsd()
    {
        return true;
    }
    static int animationMinimumWindowWidth()
    {
        return 0;
    }
    static constexpr int animationMinimumWindowWidthMin()
    {
        return 0;
    }
    static constexpr int animationMinimumWindowWidthMax()
    {
        return 2000;
    }
    static int animationMinimumWindowHeight()
    {
        return 0;
    }
    static constexpr int animationMinimumWindowHeightMin()
    {
        return 0;
    }
    static constexpr int animationMinimumWindowHeightMax()
    {
        return 2000;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Decoration Window Filtering Settings
    //
    // Mirrors the Animation Window Filtering settings but lives in its own
    // `Decorations.WindowFiltering` group so a user can tune which windows get a
    // border independently of which windows snap or animate. The defaults
    // preserve the pre-existing decoration behavior: transients were already
    // never decorated (the effect's app-window gate rejected them structurally),
    // so exclude-transient defaults on; no size threshold was ever applied, so
    // the min-size axes default off (0). On upgrade nothing changes until the
    // user opts into decorating transients or into a size threshold.
    // ═══════════════════════════════════════════════════════════════════════════

    static bool decorationExcludeTransientWindows()
    {
        return true;
    }
    static int decorationMinimumWindowWidth()
    {
        return 0;
    }
    static constexpr int decorationMinimumWindowWidthMin()
    {
        return 0;
    }
    static constexpr int decorationMinimumWindowWidthMax()
    {
        return 2000;
    }
    static int decorationMinimumWindowHeight()
    {
        return 0;
    }
    static constexpr int decorationMinimumWindowHeightMin()
    {
        return 0;
    }
    static constexpr int decorationMinimumWindowHeightMax()
    {
        return 2000;
    }
};

} // namespace PlasmaZones
