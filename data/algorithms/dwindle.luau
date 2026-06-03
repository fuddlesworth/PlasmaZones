-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pz = pz

return pz.algorithm {
    metadata = {
        name = "Dwindle",
        id = "dwindle",
        description = "Each new window gets a smaller split, alternating direction",
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
        if count <= 0 then return {} end
        local area = ctx.area
        if area.width < pz.MIN_ZONE_SIZE or area.height < pz.MIN_ZONE_SIZE then
            return pz.fillArea(area, count)
        end
        -- splitRatio is clamped inside dwindleLayout()
        return pz.dwindleLayout(area, count, ctx.splitRatio, ctx.innerGap, ctx.minSizes)
    end,
}
