-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Horizontal Deck",
        id = "horizontal-deck",
        description = "Focused window on top; remaining windows peek from the bottom edge",
        producesOverlappingZones = true,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.75,
        defaultMaxWindows = 5,
        minimumWindows = 1,
        zoneNumberDisplay = "firstAndLast",
        supportsMemory = false,
    },

    tile = function(ctx)
        -- Overlapping layout — innerGap intentionally ignored (zones overlap by design)
        local count = ctx.windowCount
        if count <= 0 then return {} end
        local splitRatio = pluau.clampSplitRatio(ctx.splitRatio)
        return pluau.deckLayout(ctx.area, count, splitRatio, true)
    end,

    -- Reflow on interactive resize: the focused window's height is the split ratio.
    -- Only its bottom edge maps to the ratio; the peek windows are derived and
    -- can't be resized independently, so they no-op.
    onWindowResized = function(state, resize)
        local count = state.windowCount
        if count <= 1 then return nil end
        local oldRatio = state.splitRatio
        local old, new, edges, idx = resize.oldRect, resize.newRect, resize.edges, resize.index
        if old.height <= 0 or oldRatio <= 0 then return nil end
        if idx == 0 and edges.bottom then
            return { splitRatio = new.height * oldRatio / old.height }
        end
        return nil
    end,
}
