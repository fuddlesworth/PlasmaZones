-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

-- Spread-specific minimum width fraction (below this, windows are too narrow)
local SpreadMinFraction = 0.3

return pluau.algorithm {
    metadata = {
        name = "Spread",
        id = "spread",
        description = "Windows spread evenly across the screen",
        producesOverlappingZones = true,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.8,
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
        local minSizes = ctx.minSizes

        local widthFraction = math.max(SpreadMinFraction, math.min(ctx.splitRatio, 1.0))

        local minWidths = {}
        local slotMinWidths = {}
        local minHeights = {}
        for i = 0, count - 1 do
            local entry = minSizes[i + 1]
            local mw = (i < #minSizes and entry and entry.w and entry.w > 0) and entry.w or 0
            local mh = (i < #minSizes and entry and entry.h and entry.h > 0) and entry.h or 0
            minWidths[#minWidths + 1] = mw
            slotMinWidths[#slotMinWidths + 1] = (mw > 0) and math.ceil(mw / widthFraction) or 0
            minHeights[#minHeights + 1] = mh
        end

        local slotWidths = pluau.distributeWithOptionalMins(area.width, count, gap, slotMinWidths)
        local baseHeight = math.max(pluau.MIN_ZONE_SIZE, math.floor(area.height * widthFraction))

        local zones = {}
        local currentX = area.x
        for i = 1, count do
            local slotW = slotWidths[i]

            local winWidth = math.max(pluau.MIN_ZONE_SIZE, math.floor(slotW * widthFraction))
            if minWidths[i] > 0 then winWidth = math.max(winWidth, minWidths[i]) end
            winWidth = math.min(winWidth, slotW)

            local winHeight = baseHeight
            if minHeights[i] > 0 then winHeight = math.max(winHeight, minHeights[i]) end
            winHeight = math.min(winHeight, area.height)

            local x = currentX + math.floor((slotW - winWidth) / 2)
            local yOffset = math.floor((area.height - winHeight) / 2)
            local y = area.y + yOffset
            zones[#zones + 1] = { x = x, y = y, width = winWidth, height = winHeight }
            currentX = currentX + slotW + gap
        end

        return zones
    end,
}
