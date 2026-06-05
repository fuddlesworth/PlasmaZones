-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pz = pz

return pz.algorithm {
    metadata = {
        name = "Monocle",
        id = "monocle",
        description = "One window fullscreen at a time, cycle through others",
        producesOverlappingZones = true,
        supportsMasterCount = false,
        supportsSplitRatio = false,
        defaultMaxWindows = 4,
        minimumWindows = 1,
        zoneNumberDisplay = "last",
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        if count <= 0 then return {} end
        return pz.fillArea(ctx.area, count)
    end,
}
