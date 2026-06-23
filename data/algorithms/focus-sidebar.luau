-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Focus + Sidebar",
        id = "focus-sidebar",
        description = "Main window with a vertically stacked sidebar. Ratio controls main window width",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.7,
        defaultMaxWindows = 5,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        masterZoneIndex = 0,
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        local area = ctx.area
        local gap = ctx.innerGap

        if count <= 0 then return {} end
        if area.width < pluau.MIN_ZONE_SIZE or area.height < pluau.MIN_ZONE_SIZE then
            return pluau.fillArea(area, count)
        end

        local splitRatio = pluau.clampSplitRatio(ctx.splitRatio)
        local mainWidth = math.max(1, math.floor(area.width * splitRatio - gap / 2))
        local sidebarX = math.min(area.x + mainWidth + gap, area.x + area.width - 1)
        local sidebarWidth = math.max(1, area.x + area.width - sidebarX)

        local zones = {}
        zones[1] = { x = area.x, y = area.y, width = mainWidth, height = area.height }

        local sidebarCount = count - 1
        if sidebarCount <= 0 then return zones end
        local totalGaps = (sidebarCount - 1) * gap

        if totalGaps >= area.height then
            local fill = pluau.fillRegion(sidebarX, area.y, sidebarWidth, area.height, sidebarCount)
            for i = 1, #fill do zones[#zones + 1] = fill[i] end
            return zones
        end

        local slots = pluau.distributeEvenly(area.y, area.height, sidebarCount, gap)
        for i = 1, #slots do
            zones[#zones + 1] = { x = sidebarX, y = slots[i].pos, width = sidebarWidth, height = slots[i].size }
        end
        return zones
    end,

    -- Reflow on interactive resize: the main/sidebar boundary maps to the split
    -- ratio. Dragging the main window's right edge (or a sidebar window's left
    -- edge) returns the new ratio for the engine to apply locally. tile() offsets
    -- the seam by -gap/2 (unlike master-stack), so the self-referential
    -- reconstruction is approximate to within ~gap/2 px; the follow-up re-snap
    -- absorbs the sub-gap difference.
    onWindowResized = function(state, resize)
        local count = state.windowCount
        if count <= 1 then return nil end
        local oldRatio = state.splitRatio
        local old, new, edges, idx = resize.oldRect, resize.newRect, resize.edges, resize.index
        if old.width <= 0 or oldRatio <= 0 or oldRatio >= 1 then return nil end
        if idx == 0 then
            if edges.right then
                return { splitRatio = new.width * oldRatio / old.width }
            end
        else
            if edges.left then
                return { splitRatio = 1 - new.width * (1 - oldRatio) / old.width }
            end
        end
        return nil
    end,
}
