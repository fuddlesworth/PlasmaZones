-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Monocle",
        id = "monocle",
        description = "One window fullscreen at a time, cycle through others",
        producesOverlappingZones = true,
        overlapStacking = "lastOnTop",
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
        return pluau.fillArea(ctx.area, count)
    end,
}
