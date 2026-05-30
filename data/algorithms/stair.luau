-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pz = pz

-- Stair-specific ratio bounds (tighter than pz.MIN_SPLIT/pz.MAX_SPLIT)
local StairMinSizeRatio = 0.3
local StairMaxSizeRatio = 0.8
local StairMinWindowPx = 100

return pz.algorithm {
    metadata = {
        name = "Stair",
        id = "stair",
        description = "Stepped staircase arrangement",
        producesOverlappingZones = true,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.5,
        defaultMaxWindows = 4,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        if count <= 0 then return {} end
        local area = ctx.area

        if area.width < pz.MIN_ZONE_SIZE or area.height < pz.MIN_ZONE_SIZE then
            return pz.fillArea(area, count)
        end

        local sizeRatio = math.max(StairMinSizeRatio, math.min(StairMaxSizeRatio, ctx.splitRatio))

        local winWidth = math.max(StairMinWindowPx, math.floor(area.width * sizeRatio))
        local winHeight = math.max(StairMinWindowPx, math.floor(area.height * sizeRatio))
        winWidth = math.min(winWidth, area.width)
        winHeight = math.min(winHeight, area.height)

        local totalOffsetX = math.max(0, area.width - winWidth)
        local totalOffsetY = math.max(0, area.height - winHeight)
        local stepX = math.floor(totalOffsetX / (count - 1))
        local stepY = math.floor(totalOffsetY / (count - 1))

        local zones = {}
        for i = 0, count - 1 do
            local x = area.x + stepX * i
            local y = area.y + stepY * i
            local w = winWidth
            local h = winHeight

            local clamped = pz.applyPerWindowMinSize(w, h, ctx.minSizes, i)
            w = math.max(1, math.min(clamped.w, area.x + area.width - x))
            h = math.max(1, math.min(clamped.h, area.y + area.height - y))

            zones[#zones + 1] = { x = x, y = y, width = w, height = h }
        end
        return zones
    end,
}
