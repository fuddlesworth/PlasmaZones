-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Focus + Sidebar",
        id = "focus-sidebar",
        description = "Main window with a vertically stacked sidebar. Ratio controls main window width",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.7,
        defaultMaxWindows = 5,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        masterZoneIndex = 0,
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        local area = ctx.area
        local gap = ctx.innerGap

        if count <= 0 then return {} end
        if area.width < pluau.MIN_ZONE_SIZE or area.height < pluau.MIN_ZONE_SIZE then
            return pluau.fillArea(area, count)
        end

        local splitRatio = pluau.clampSplitRatio(ctx.splitRatio)
        local mainWidth = math.max(1, math.floor(area.width * splitRatio - gap / 2))
        local sidebarX = math.min(area.x + mainWidth + gap, area.x + area.width - 1)
        local sidebarWidth = math.max(1, area.x + area.width - sidebarX)

        local zones = {}
        zones[1] = { x = area.x, y = area.y, width = mainWidth, height = area.height }

        local sidebarCount = count - 1
        if sidebarCount <= 0 then return zones end
        local totalGaps = (sidebarCount - 1) * gap

        if totalGaps >= area.height then
            local fill = pluau.fillRegion(sidebarX, area.y, sidebarWidth, area.height, sidebarCount)
            for i = 1, #fill do zones[#zones + 1] = fill[i] end
            return zones
        end

        local slots = pluau.distributeEvenly(area.y, area.height, sidebarCount, gap)
        for i = 1, #slots do
            zones[#zones + 1] = { x = sidebarX, y = slots[i].pos, width = sidebarWidth, height = slots[i].size }
        end
        return zones
    end,
}
