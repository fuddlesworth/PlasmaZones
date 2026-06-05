-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pz = pz

return pz.algorithm {
    metadata = {
        name = "Columns",
        id = "columns",
        description = "Equal-width vertical columns",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = false,
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
        return pz.equalColumnsLayout(area, count, ctx.innerGap, ctx.minSizes)
    end,
}
