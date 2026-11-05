-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Quadrant Priority",
        id = "quadrant-priority",
        description = "First window gets a large corner, and the rest fill the L-shaped remainder",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.6,
        defaultMaxWindows = 5,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        masterZoneIndex = 0,
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        local area = ctx.area
        local early = pluau.guardArea(area, count)
        if early then return early end
        local gap = ctx.innerGap
        local splitRatio = pluau.clampSplitRatio(ctx.splitRatio)

        -- ceil/floor distribution, bottom row spans full width, right column = master height
        return pluau.lShapeLayout(area, count, gap, splitRatio, "ceil-floor", "full", "master")
    end,
}
