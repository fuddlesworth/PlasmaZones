-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Centered Master",
        id = "centered-master",
        description = "Master windows centered with stacks on both sides",
        producesOverlappingZones = false,
        supportsMasterCount = true,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.5,
        defaultMaxWindows = 7,
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
        local minSizes = ctx.minSizes

        local early = pluau.guardArea(area, count)
        if early then return early end

        local masterCount = math.max(1, math.min(ctx.masterCount, count))
        local stackCount = count - masterCount
        local splitRatio = pluau.clampSplitRatio(ctx.splitRatio)

        -- Case 1: only masters — delegate to masterStackLayout
        if stackCount == 0 then
            return pluau.masterStackLayout(area, count, gap, splitRatio, masterCount, minSizes, false)
        end

        -- Case 2: one stack window — 2-column layout
        if stackCount == 1 then
            if area.width - gap <= 0 or area.height < pluau.MIN_ZONE_SIZE then
                return pluau.fillArea(area, count)
            end
            local contentWidth = area.width - gap
            local masterWidth = math.floor(contentWidth * splitRatio)
            local stackWidth = contentWidth - masterWidth

            if #minSizes > 0 then
                local minMW = (minSizes[1] and minSizes[1].w) or 0
                local minSW = (masterCount < #minSizes)
                    and ((minSizes[masterCount + 1] and minSizes[masterCount + 1].w) or 0) or 0
                local solved = pluau.solveTwoPart(contentWidth, masterWidth, stackWidth, minMW, minSW)
                masterWidth = math.max(1, solved.first)
                stackWidth = math.max(1, solved.second)
            end

            local masterMinH = pluau.extractMinHeights(minSizes, masterCount)
            local mHeights = pluau.distributeWithOptionalMins(area.height, masterCount, gap, masterMinH)
            local zones = {}
            local currentY = area.y
            for i = 1, masterCount do
                zones[#zones + 1] = { x = area.x, y = currentY, width = masterWidth, height = mHeights[i] }
                currentY = currentY + mHeights[i] + gap
            end

            zones[#zones + 1] = { x = area.x + masterWidth + gap, y = area.y, width = stackWidth, height = area.height }
            return zones
        end

        -- Case 3: 3-column layout
        return pluau.threeColumnLayout(area, count, gap, splitRatio, masterCount, minSizes)
    end,
}
