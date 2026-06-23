-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Tatami",
        id = "tatami",
        description = "Japanese tatami mat pattern where no four corners ever meet at the same point",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = false,
        defaultMaxWindows = 6,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
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

        if area.width - gap <= 0 or area.height - gap <= 0 then
            return pluau.fillArea(area, count)
        end

        if count == 2 then
            local halfW = math.max(1, math.floor((area.width - gap) / 2))
            local secondW = math.max(1, area.width - halfW - gap)
            local secondX = math.min(area.x + halfW + gap, area.x + area.width - 1)
            return {
                { x = area.x, y = area.y, width = halfW, height = area.height },
                { x = secondX, y = area.y, width = secondW, height = area.height },
            }
        end

        local zones = {}
        local rows = math.ceil(count / 2)

        local availableHeight = area.height - gap * (rows - 1)
        if availableHeight <= 0 then
            return pluau.fillArea(area, count)
        end

        local rowHeight = math.floor(availableHeight / rows)
        if rowHeight < 1 then rowHeight = 1 end

        local EVEN_ROW_RATIO = 0.55
        local ODD_ROW_RATIO = 0.45

        for row = 0, rows - 1 do
            local y = area.y + row * (rowHeight + gap)
            local h = (row == rows - 1) and math.max(1, area.y + area.height - y) or rowHeight
            local windowsInRow = (row == rows - 1 and count % 2 ~= 0) and 1 or 2

            if windowsInRow == 1 then
                zones[#zones + 1] = { x = area.x, y = y, width = area.width, height = h }
            else
                local ratio = (row % 2 == 0) and EVEN_ROW_RATIO or ODD_ROW_RATIO
                local leftW = math.max(1, math.floor((area.width - gap) * ratio))
                local rightW = math.max(1, area.width - gap - leftW)

                zones[#zones + 1] = { x = area.x, y = y, width = leftW, height = h }
                zones[#zones + 1] = { x = area.x + leftW + gap, y = y, width = rightW, height = h }
            end
        end

        return zones
    end,
}
