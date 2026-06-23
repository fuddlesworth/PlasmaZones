-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Zen",
        id = "zen",
        description = "Centered column with margins for a focused, distraction-free layout",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.6,
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

        local splitRatio = pluau.clampSplitRatio(ctx.splitRatio)
        local columnWidth = math.floor(area.width * splitRatio)
        columnWidth = math.max(1, columnWidth)
        local offsetX = area.x + math.floor((area.width - columnWidth) / 2)

        local totalGaps = (count - 1) * gap
        if totalGaps >= area.height then
            return pluau.fillRegion(offsetX, area.y, columnWidth, area.height, count)
        end

        local slots = pluau.distributeEvenly(area.y, area.height, count, gap)
        local zones = {}
        for i = 1, #slots do
            zones[i] = { x = offsetX, y = slots[i].pos, width = columnWidth, height = slots[i].size }
        end
        return zones
    end,
}
