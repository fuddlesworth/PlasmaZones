-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

-- Cascade-specific bounds (tighter than pluau.MIN_SPLIT/pluau.MAX_SPLIT)
local CascadeMinOffsetRatio = 0.02
local CascadeMaxOffsetRatio = 0.4
local CascadeMinOffsetPx = 20
local CascadeMinWindowPx = 100

return pluau.algorithm {
    metadata = {
        name = "Cascade",
        id = "cascade",
        description = "Overlapping windows in a diagonal cascade",
        producesOverlappingZones = true,
        overlapStacking = "lastOnTop",
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.15,
        defaultMaxWindows = 5,
        minimumWindows = 1,
        zoneNumberDisplay = "last",
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        local area = ctx.area

        local early = pluau.guardArea(area, count)
        if early then return early end

        -- The max-bound fallback mirrors what the old inline max/min clamp
        -- produced for a non-real ratio, keeping the NaN path byte-identical.
        local offsetRatio = pluau.clamp(ctx.splitRatio, CascadeMinOffsetRatio, CascadeMaxOffsetRatio)
            or CascadeMaxOffsetRatio

        -- Guard the divisor (the host short-circuits single-window layouts, so
        -- this is defensive); identical to `count - 1` for the reachable count>=2
        -- cases, and matches the guarded divisor on maxOffset below.
        local offsetX = math.max(CascadeMinOffsetPx, math.floor(area.width * offsetRatio / math.max(1, count - 1)))
        local offsetY = math.max(CascadeMinOffsetPx, math.floor(area.height * offsetRatio / math.max(1, count - 1)))

        local totalOffsetX = offsetX * (count - 1)
        local totalOffsetY = offsetY * (count - 1)
        local winWidth = math.min(math.max(CascadeMinWindowPx, area.width - totalOffsetX), area.width)
        local winHeight = math.min(math.max(CascadeMinWindowPx, area.height - totalOffsetY), area.height)

        local maxOffsetX = math.max(0, math.floor((area.width - winWidth) / math.max(1, count - 1)))
        local maxOffsetY = math.max(0, math.floor((area.height - winHeight) / math.max(1, count - 1)))
        offsetX = math.min(offsetX, maxOffsetX)
        offsetY = math.min(offsetY, maxOffsetY)

        local zones = {}
        for i = 0, count - 1 do
            local x = area.x + offsetX * i
            local y = area.y + offsetY * i
            local w = winWidth
            local h = winHeight

            local clamped = pluau.applyPerWindowMinSize(w, h, ctx.minSizes, i)
            w = math.max(1, math.min(clamped.w, area.x + area.width - x))
            h = math.max(1, math.min(clamped.h, area.y + area.height - y))

            zones[#zones + 1] = { x = x, y = y, width = w, height = h }
        end
        return zones
    end,
}
