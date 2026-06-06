-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local phosphor_luau = phosphor_luau

return phosphor_luau.algorithm {
    metadata = {
        name = "Deck",
        id = "deck",
        description = "Focused window takes the left portion; remaining windows peek from the right edge. Ratio controls focused window width",
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
        local splitRatio = phosphor_luau.clampSplitRatio(ctx.splitRatio)
        return phosphor_luau.deckLayout(ctx.area, count, splitRatio, false)
    end,
}
