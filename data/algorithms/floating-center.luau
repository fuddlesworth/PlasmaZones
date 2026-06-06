-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

local function renderPanel(zones, startX, startY, panelW, panelH, count, gap, horizontal)
    if count <= 0 then return end
    local totalSize = horizontal and panelW or panelH

    -- Degenerate gap: equal-split without gaps
    if (count - 1) * gap >= totalSize then
        local equalSize = math.max(1, math.floor(totalSize / count))
        local fill
        if horizontal then
            fill = pluau.fillRegion(startX, startY, equalSize, panelH, count)
        else
            fill = pluau.fillRegion(startX, startY, panelW, equalSize, count)
        end
        for i = 1, #fill do zones[#zones + 1] = fill[i] end
        return
    end

    local slots = pluau.distributeEvenly(horizontal and startX or startY, totalSize, count, gap)
    for i = 1, #slots do
        if horizontal then
            zones[#zones + 1] = { x = slots[i].pos, y = startY, width = slots[i].size, height = panelH }
        else
            zones[#zones + 1] = { x = startX, y = slots[i].pos, width = panelW, height = slots[i].size }
        end
    end
end

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
        if count <= 0 then return {} end
        local area = ctx.area
        local gap = ctx.innerGap

        if area.width < pluau.MIN_ZONE_SIZE or area.height < pluau.MIN_ZONE_SIZE then
            return pluau.fillArea(area, count)
        end

        if count == 1 then
            local splitRatio1 = pluau.clampSplitRatio(ctx.splitRatio)
            local cw = math.floor(area.width * splitRatio1)
            local ch = math.floor(area.height * splitRatio1)
            local cx = area.x + math.floor((area.width - cw) / 2)
            local cy = area.y + math.floor((area.height - ch) / 2)
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

        if rightCount > 0 then
            if rightW >= 1 then
                renderPanel(zones, rightX, sideTop, rightW, sideH, rightCount, gap, false)
            else
                for _ = 1, rightCount do
                    zones[#zones + 1] = { x = centerX, y = centerY, width = centerW, height = centerH }
                end
            end
        end

        if leftCount > 0 then
            if leftW >= 1 then
                renderPanel(zones, area.x, sideTop, leftW, sideH, leftCount, gap, false)
            else
                for _ = 1, leftCount do
                    zones[#zones + 1] = { x = centerX, y = centerY, width = centerW, height = centerH }
                end
            end
        end

        if bottomCount > 0 then
            if bottomH >= 1 and bottomY < area.y + area.height then
                renderPanel(zones, area.x, bottomY, area.width, bottomH, bottomCount, gap, true)
            else
                for _ = 1, bottomCount do
                    zones[#zones + 1] = { x = centerX, y = centerY, width = centerW, height = centerH }
                end
            end
        end

        if topCount > 0 then
            if topH >= 1 then
                renderPanel(zones, area.x, area.y, area.width, topH, topCount, gap, true)
            else
                for _ = 1, topCount do
                    zones[#zones + 1] = { x = centerX, y = centerY, width = centerW, height = centerH }
                end
            end
        end

        return zones
    end,
}
