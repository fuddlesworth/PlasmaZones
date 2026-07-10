-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Spiral",
        id = "spiral",
        description = "Windows spiral inward from the edges",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.5,
        defaultMaxWindows = 5,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        local area = ctx.area
        local gap = ctx.innerGap
        local splitRatio = ctx.splitRatio
        local minSizes = ctx.minSizes

        local early = pluau.guardArea(area, count)
        if early then return early end

        splitRatio = pluau.clampSplitRatio(splitRatio)

        -- dirs 0,2 are vertical (even), 1,3 are horizontal (odd) — matches the
        -- i%2 V/H alternation in computeCumulativeMinDims.
        local cumMinDims = pluau.computeCumulativeMinDims(count, minSizes, gap)
        local remainingMinW = cumMinDims.minW
        local remainingMinH = cumMinDims.minH

        local remaining = { x = area.x, y = area.y, width = area.width, height = area.height }
        local zones = {}

        for i = 0, count - 1 do
            if i == count - 1 or remaining.width < pluau.MIN_ZONE_SIZE or remaining.height < pluau.MIN_ZONE_SIZE then
                zones[#zones + 1] =
                    { x = remaining.x, y = remaining.y, width = remaining.width, height = remaining.height }
                pluau.appendGracefulDegradation(zones, remaining, count - i - 1, gap)
                break
            end

            local dir = i % 4

            if dir == 0 or dir == 2 then
                local contentWidth = remaining.width - gap
                if contentWidth <= 0 then
                    zones[#zones + 1] =
                        { x = remaining.x, y = remaining.y, width = remaining.width, height = remaining.height }
                    pluau.appendGracefulDegradation(zones, remaining, count - i - 1, gap)
                    break
                end
                local windowWidth = math.floor(contentWidth * splitRatio)
                local mw = pluau.minSizeAt(minSizes, i)
                if mw > 0 then
                    windowWidth = math.max(windowWidth, mw)
                end
                if #minSizes > 0 and remainingMinW[i + 1] and remainingMinW[i + 1] > 0 then
                    windowWidth = math.min(windowWidth, contentWidth - remainingMinW[i + 1])
                end
                windowWidth = math.max(1, math.min(windowWidth, contentWidth - 1))
                local otherWidth = contentWidth - windowWidth

                if dir == 0 then -- Right: window=left, remaining=right
                    zones[#zones + 1] =
                        { x = remaining.x, y = remaining.y, width = windowWidth, height = remaining.height }
                    remaining = { x = remaining.x + windowWidth + gap, y = remaining.y,
                        width = otherWidth, height = remaining.height }
                else -- Left: window=right, remaining=left
                    zones[#zones + 1] = { x = remaining.x + otherWidth + gap, y = remaining.y,
                        width = windowWidth, height = remaining.height }
                    remaining = { x = remaining.x, y = remaining.y, width = otherWidth, height = remaining.height }
                end
            else
                local contentHeight = remaining.height - gap
                if contentHeight <= 0 then
                    zones[#zones + 1] =
                        { x = remaining.x, y = remaining.y, width = remaining.width, height = remaining.height }
                    pluau.appendGracefulDegradation(zones, remaining, count - i - 1, gap)
                    break
                end
                local windowHeight = math.floor(contentHeight * splitRatio)
                local _, mh = pluau.minSizeAt(minSizes, i)
                if mh > 0 then
                    windowHeight = math.max(windowHeight, mh)
                end
                if #minSizes > 0 and remainingMinH[i + 1] and remainingMinH[i + 1] > 0 then
                    windowHeight = math.min(windowHeight, contentHeight - remainingMinH[i + 1])
                end
                windowHeight = math.max(1, math.min(windowHeight, contentHeight - 1))
                local otherHeight = contentHeight - windowHeight

                if dir == 1 then -- Down: window=top, remaining=bottom
                    zones[#zones + 1] =
                        { x = remaining.x, y = remaining.y, width = remaining.width, height = windowHeight }
                    remaining = { x = remaining.x, y = remaining.y + windowHeight + gap,
                        width = remaining.width, height = otherHeight }
                else -- Up: window=bottom, remaining=top
                    zones[#zones + 1] = { x = remaining.x, y = remaining.y + otherHeight + gap,
                        width = remaining.width, height = windowHeight }
                    remaining = { x = remaining.x, y = remaining.y, width = remaining.width, height = otherHeight }
                end
            end
        end

        return zones
    end,
}
