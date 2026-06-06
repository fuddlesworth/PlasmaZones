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
        if count <= 0 then return {} end
        local area = ctx.area
        if area.width < pluau.MIN_ZONE_SIZE or area.height < pluau.MIN_ZONE_SIZE then
            return pluau.fillArea(area, count)
        end
        local gap = ctx.innerGap

        local minHeights = pluau.extractMinHeights(ctx.minSizes, count)
        local rowHeights = pluau.distributeWithOptionalMins(area.height, count, gap, minHeights)

        local zones = {}
        local currentY = area.y
        for i = 1, count do
            zones[i] = { x = area.x, y = currentY, width = area.width, height = rowHeights[i] }
            currentY = currentY + rowHeights[i] + gap
        end
        return zones
    end,
}
