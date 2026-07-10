-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Floating Center",
        id = "floating-center",
        description = "Centered main window with peripheral panels on all sides",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.65,
        defaultMaxWindows = 6,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        masterZoneIndex = 0,
        supportsMemory = false,
        supportsMinSizes = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        local area = ctx.area
        local gap = ctx.innerGap

        local early = pluau.guardArea(area, count)
        if early then return early end

        if count == 1 then
            local splitRatio1 = pluau.clampSplitRatio(ctx.splitRatio)
            local cw = math.floor(area.width * splitRatio1)
            local ch = math.floor(area.height * splitRatio1)
            local cx = pluau.center(area.x, area.width, cw)
            local cy = pluau.center(area.y, area.height, ch)
            return { { x = cx, y = cy, width = cw, height = ch } }
        end

        local splitRatio = pluau.clampSplitRatio(ctx.splitRatio)

        local centerW = math.floor(area.width * splitRatio)
        local centerH = math.floor(area.height * splitRatio)
        local marginX = math.floor((area.width - centerW) / 2)
        local marginY = math.floor((area.height - centerH) / 2)

        local centerX = area.x + marginX
        local centerY = area.y + marginY

        if marginX <= gap or marginY <= gap then
            return pluau.fillArea(area, count)
        end

        local zones = {}
        local remaining = count - 1

        local leftCount, rightCount, bottomCount, topCount = 0, 0, 0, 0
        if remaining >= 1 then rightCount = 1 end
        if remaining >= 2 then leftCount = 1 end
        if remaining >= 3 then bottomCount = 1 end
        if remaining >= 4 then topCount = 1 end

        local extras = math.max(0, remaining - 4)
        bottomCount = bottomCount + math.ceil(extras / 2)
        topCount = topCount + math.floor(extras / 2)

        local topH = math.max(1, marginY - gap)
        local bottomY = area.y + marginY + centerH + gap
        local bottomH = math.max(1, area.y + area.height - bottomY)

        local sideTop = area.y + marginY
        local sideH = centerH

        local leftW = math.max(1, marginX - gap)
        local rightX = area.x + marginX + centerW + gap
        local rightW = math.max(1, area.x + area.width - rightX)

        zones[#zones + 1] = { x = centerX, y = centerY, width = centerW, height = centerH }

        -- rightW/leftW/topH are floored at 1 by math.max(1, ...), so their old
        -- `>= 1` guards were always true; the overlapping-fallback else arms were
        -- dead. bottomH keeps its guard: the ANDed bounds check can be false.
        if rightCount > 0 then
            pluau.stripLayout(zones, rightX, sideTop, rightW, sideH, rightCount, gap, false)
        end

        if leftCount > 0 then
            pluau.stripLayout(zones, area.x, sideTop, leftW, sideH, leftCount, gap, false)
        end

        if bottomCount > 0 then
            if bottomH >= 1 and bottomY < area.y + area.height then
                pluau.stripLayout(zones, area.x, bottomY, area.width, bottomH, bottomCount, gap, true)
            else
                for _ = 1, bottomCount do
                    zones[#zones + 1] = { x = centerX, y = centerY, width = centerW, height = centerH }
                end
            end
        end

        if topCount > 0 then
            pluau.stripLayout(zones, area.x, area.y, area.width, topH, topCount, gap, true)
        end

        return zones
    end,
}
