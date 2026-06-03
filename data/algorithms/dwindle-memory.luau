-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pz = pz

return pz.algorithm {
    metadata = {
        name = "Dwindle (Memory)",
        id = "dwindle-memory",
        description = "Remembers split positions — resize one split without affecting others",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.5,
        defaultMaxWindows = 5,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        supportsMemory = true,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        if count <= 0 then return {} end

        local area = ctx.area
        local gap = ctx.innerGap
        local minSizes = ctx.minSizes

        if area.width < pz.MIN_ZONE_SIZE or area.height < pz.MIN_ZONE_SIZE then
            return pz.fillArea(area, count)
        end

        -- Use the persistent split tree when present and matching; otherwise fall
        -- back to stateless dwindle. (minSizes are not applied on the tree path.)
        if ctx.tree and ctx.tree.leafCount == count then
            return pz.applyTreeGeometry(ctx.tree, area, gap)
        end

        return pz.dwindleLayout(area, count, ctx.splitRatio, gap, minSizes)
    end,
}
