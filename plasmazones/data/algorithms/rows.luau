-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Rows",
        id = "rows",
        description = "Equal-height horizontal rows",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = false,
        defaultMaxWindows = 4,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        local area = ctx.area
        local early = pluau.guardArea(area, count)
        if early then return early end
        local gap = ctx.innerGap

        local minHeights = pluau.extractMinHeights(ctx.minSizes, count)
        local rowHeights = pluau.distributeWithOptionalMins(area.height, count, gap, minHeights)
        local rowYs = pluau.cumulativeOffsets(area.y, rowHeights, gap)

        local zones = {}
        for i = 1, count do
            zones[i] = { x = area.x, y = rowYs[i], width = area.width, height = rowHeights[i] }
        end
        return zones
    end,
}
