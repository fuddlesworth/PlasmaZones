-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Wide",
        id = "wide",
        description = "Master area on top, remaining windows stacked below",
        producesOverlappingZones = false,
        supportsMasterCount = true,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.5,
        defaultMaxWindows = 5,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        masterZoneIndex = 0,
        supportsMemory = false,
    },

    tile = function(ctx)
        if ctx.windowCount <= 0 then return {} end
        -- splitRatio is clamped inside masterStackLayout()
        return pluau.masterStackLayout(
            ctx.area, ctx.windowCount, ctx.innerGap, ctx.splitRatio, ctx.masterCount, ctx.minSizes, true)
    end,

    -- Reflow on interactive resize: the horizontal master/stack row boundary maps
    -- to the split ratio (bottom edge grows the master, top edge shrinks the stack).
    onWindowResized = function(state, resize)
        return pluau.masterStackResize(state, resize, true)
    end,
}
