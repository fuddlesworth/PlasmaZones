-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Master + Stack",
        id = "master-stack",
        description = "Large master area with stacked secondary windows",
        producesOverlappingZones = false,
        supportsMasterCount = true,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.6,
        defaultMaxWindows = 4,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        masterZoneIndex = 0,
        supportsMemory = false,
    },

    tile = function(ctx)
        if ctx.windowCount <= 0 then return {} end
        -- splitRatio is clamped inside masterStackLayout()
        return pluau.masterStackLayout(
            ctx.area, ctx.windowCount, ctx.innerGap, ctx.splitRatio, ctx.masterCount, ctx.minSizes, false)
    end,

    -- Reflow on interactive resize: the vertical master/stack column boundary maps
    -- to the split ratio (right edge grows the master, left edge shrinks the stack).
    onWindowResized = function(state, resize)
        return pluau.masterStackResize(state, resize, false)
    end,
}
