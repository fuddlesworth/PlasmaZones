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

    -- Reflow on interactive resize: the master/stack column boundary maps to the
    -- split ratio. Dragging a master window's right edge (or a stack window's left
    -- edge) returns the new ratio for the engine to apply locally. The gap cancels
    -- in the fraction, so we reconstruct it self-referentially from the old size.
    onWindowResized = function(state, resize)
        local count = state.windowCount
        local masterCount = state.masterCount or 1
        if count <= masterCount then return nil end -- no stack column → no seam
        local oldRatio = state.splitRatio
        local old, new, edges, idx = resize.oldRect, resize.newRect, resize.edges, resize.index
        if old.width <= 0 or oldRatio <= 0 or oldRatio >= 1 then return nil end
        if idx < masterCount then
            if edges.right then
                return { splitRatio = pluau.resizeRatioGrow(new.width, old.width, oldRatio) }
            end
        else
            if edges.left then
                return { splitRatio = pluau.resizeRatioShrink(new.width, old.width, oldRatio) }
            end
        end
        return nil
    end,
}
