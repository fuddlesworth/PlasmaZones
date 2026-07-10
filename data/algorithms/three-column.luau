-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Three Column",
        id = "three-column",
        description = "Master window centered with columns on each side",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.5,
        defaultMaxWindows = 5,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        centerLayout = true,
        masterZoneIndex = 0,
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        local area = ctx.area
        local gap = ctx.innerGap
        local splitRatio = pluau.clampSplitRatio(ctx.splitRatio)
        local minSizes = ctx.minSizes

        local early = pluau.guardArea(area, count)
        if early then return early end

        if count == 1 then
            return { { x = area.x, y = area.y, width = area.width, height = area.height } }
        end

        if count == 2 then
            local contentWidth = math.max(1, area.width - gap)
            local masterWidth = math.floor(contentWidth * splitRatio)
            local stackWidth = contentWidth - masterWidth
            if #minSizes > 0 then
                local minMW = (minSizes[1] and minSizes[1].w and minSizes[1].w > 0) and minSizes[1].w or 0
                local minSW = (#minSizes > 1 and minSizes[2] and minSizes[2].w and minSizes[2].w > 0)
                    and minSizes[2].w or 0
                local solved = pluau.solveTwoPart(contentWidth, masterWidth, stackWidth, minMW, minSW)
                masterWidth = solved.first
                stackWidth = solved.second
            end
            return {
                { x = area.x, y = area.y, width = masterWidth, height = area.height },
                { x = area.x + masterWidth + gap, y = area.y, width = stackWidth, height = area.height },
            }
        end

        if count >= 3 and area.width < 3 * pluau.MIN_ZONE_SIZE + 2 * gap then
            return pluau.equalColumnsLayout(area, count, gap, minSizes)
        end

        return pluau.threeColumnLayout(area, count, gap, splitRatio, 1, minSizes)
    end,
}
