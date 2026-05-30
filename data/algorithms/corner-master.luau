-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pz = pz

return pz.algorithm {
    metadata = {
        name = "Corner Master",
        id = "corner-master",
        description = "Master window in a corner; rest fill the L-shaped remainder",
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
        local splitRatio = pz.clampSplitRatio(ctx.splitRatio)

        -- alternate distribution, bottom row = master width, right column = master height
        return pz.lShapeLayout(area, count, gap, splitRatio, "alternate", "master", "master")
    end,
}
