-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Corner Master",
        id = "corner-master",
        description = "Master window in a corner, and the rest fill the L-shaped remainder",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.55,
        defaultMaxWindows = 5,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        masterZoneIndex = 0,
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        if count <= 0 then return {} end
        local area = ctx.area
        local gap = ctx.innerGap
        local splitRatio = pluau.clampSplitRatio(ctx.splitRatio)

        -- alternate distribution, bottom row = master width, right column = master height
        return pluau.lShapeLayout(area, count, gap, splitRatio, "alternate", "master", "master")
    end,
}
