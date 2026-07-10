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

    -- Reflow on interactive resize: the (horizontal) master/stack boundary maps to
    -- the split ratio. Dragging a master window's bottom edge (or a stack window's
    -- top edge) returns the new ratio for the engine to apply locally.
    onWindowResized = function(state, resize)
        local count = state.windowCount
        local masterCount = state.masterCount or 1
        if count <= masterCount then return nil end -- no stack row → no seam
        local oldRatio = state.splitRatio
        local old, new, edges, idx = resize.oldRect, resize.newRect, resize.edges, resize.index
        if old.height <= 0 or oldRatio <= 0 or oldRatio >= 1 then return nil end
        if idx < masterCount then
            if edges.bottom then
                return { splitRatio = pluau.resizeRatioGrow(new.height, old.height, oldRatio) }
            end
        else
            if edges.top then
                return { splitRatio = pluau.resizeRatioShrink(new.height, old.height, oldRatio) }
            end
        end
        return nil
    end,
}
