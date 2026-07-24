-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

-- Stair-specific ratio bounds (tighter than pluau.MIN_SPLIT/pluau.MAX_SPLIT)
local StairMinSizeRatio = 0.3
local StairMaxSizeRatio = 0.8
local StairMinWindowPx = 100

return pluau.algorithm {
    metadata = {
        name = "Stair",
        id = "stair",
        description = "Stepped staircase arrangement",
        producesOverlappingZones = true,
        overlapStacking = "lastOnTop",
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
        local area = ctx.area

        local early = pluau.guardArea(area, count)
        if early then return early end

        local sizeRatio = pluau.clamp(ctx.splitRatio, StairMinSizeRatio, StairMaxSizeRatio) or StairMaxSizeRatio

        local winWidth = math.max(StairMinWindowPx, math.floor(area.width * sizeRatio))
        local winHeight = math.max(StairMinWindowPx, math.floor(area.height * sizeRatio))
        winWidth = math.min(winWidth, area.width)
        winHeight = math.min(winHeight, area.height)

        local totalOffsetX = math.max(0, area.width - winWidth)
        local totalOffsetY = math.max(0, area.height - winHeight)
        -- Guard the count==1 divisor (the host short-circuits single-window
        -- layouts, so this is defensive); mirrors paper.luau's `count > 1` guard.
        local stepX = count > 1 and math.floor(totalOffsetX / (count - 1)) or 0
        local stepY = count > 1 and math.floor(totalOffsetY / (count - 1)) or 0

        local zones = {}
        for i = 0, count - 1 do
            local x = area.x + stepX * i
            local y = area.y + stepY * i
            local w = winWidth
            local h = winHeight

            local clamped = pluau.applyPerWindowMinSize(w, h, ctx.minSizes, i)
            w = math.max(1, math.min(clamped.w, area.x + area.width - x))
            h = math.max(1, math.min(clamped.h, area.y + area.height - y))

            zones[#zones + 1] = { x = x, y = y, width = w, height = h }
        end
        return zones
    end,
}
