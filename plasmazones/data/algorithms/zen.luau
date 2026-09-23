-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Zen",
        id = "zen",
        description = "Centered column with margins for a focused, distraction-free layout",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.6,
        defaultMaxWindows = 4,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        local area = ctx.area
        local early = pluau.guardArea(area, count)
        if early then return early end
        local gap = ctx.innerGap

        local splitRatio = pluau.clampSplitRatio(ctx.splitRatio)
        local columnWidth = math.floor(area.width * splitRatio)
        columnWidth = math.max(1, columnWidth)
        local offsetX = pluau.center(area.x, area.width, columnWidth)

        local totalGaps = (count - 1) * gap
        if totalGaps >= area.height then
            return pluau.fillRegion(offsetX, area.y, columnWidth, area.height, count)
        end

        local slots = pluau.distributeEvenly(area.y, area.height, count, gap)
        local zones = {}
        for i = 1, #slots do
            zones[i] = { x = offsetX, y = slots[i].pos, width = columnWidth, height = slots[i].size }
        end
        return zones
    end,

    -- Reflow on interactive resize: the centred column width is the split ratio.
    -- The column is centre-anchored, so a one-edge drag of Δ must change the column
    -- width by 2Δ for the dragged edge to land where the user released it. Every
    -- window is the full column width, so there is no index gate — any window's
    -- horizontal edge represents the same column boundary. setSplitRatio clamps a
    -- negative/over-wide result.
    onWindowResized = function(state, resize)
        local count = state.windowCount
        if count <= 1 then return nil end
        local oldRatio = state.splitRatio
        local old, new, edges = resize.oldRect, resize.newRect, resize.edges
        if old.width <= 0 or oldRatio <= 0 then return nil end
        if edges.left or edges.right then
            local newColumnWidth = 2 * new.width - old.width
            return { splitRatio = pluau.resizeRatioGrow(newColumnWidth, old.width, oldRatio) }
        end
        return nil
    end,
}
