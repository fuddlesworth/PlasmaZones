-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Paper",
        id = "paper",
        description = "Equal-width overlapping pages like a document viewer, where splitRatio controls page width",
        producesOverlappingZones = true,
        overlapStacking = "lastOnTop",
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.8,
        defaultMaxWindows = 6,
        minimumWindows = 1,
        zoneNumberDisplay = "last",
        supportsMemory = false,
    },

    tile = function(ctx)
        -- Overlapping layout — innerGap intentionally ignored (pages overlap by design)
        local count = ctx.windowCount
        if count <= 0 then return {} end
        local area = ctx.area

        local pageRatio = pluau.clampSplitRatio(ctx.splitRatio)
        local pageWidth = math.floor(area.width * pageRatio)
        if pageWidth < 1 then pageWidth = 1 end
        if pageWidth > area.width then pageWidth = area.width end

        local leftover = area.width - pageWidth
        local step
        if count > 1 then
            step = math.floor(leftover / (count - 1))
        else
            step = 0
        end

        local zones = {}
        for i = 0, count - 1 do
            local zoneX = math.min(area.x + i * step, area.x + area.width - 1)
            local zoneW = math.max(1, math.min(pageWidth, area.x + area.width - zoneX))
            zones[#zones + 1] = { x = zoneX, y = area.y, width = zoneW, height = area.height }
        end
        return zones
    end,
}
