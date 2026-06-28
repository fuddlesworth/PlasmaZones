-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later
--
-- Centered
--
-- Keeps windows inside a centered column whose width is a fraction of the work
-- area, so nothing ever stretches across a wide monitor. A lone window is
-- centered at that width and height. More windows share the same centered
-- column: the first takes the top, the rest stack vertically below it.
--
-- The width is remembered per monitor. Resize the lone window on a given screen
-- and that screen keeps it, independently of every other screen. Each screen
-- carries its own TilingState, so its persistent ctx.state bag is already scoped
-- to that screen and desktop; the onWindowResized hook stores the dragged pixel
-- width and tile() reconciles it against the current work area on the next pass.

local pluau = pluau

local MIN_RATIO = 0.2
local MAX_RATIO = 1.0

local function clampRatio(r)
    if type(r) ~= "number" or r ~= r then
        return nil
    end
    if r < MIN_RATIO then return MIN_RATIO end
    if r > MAX_RATIO then return MAX_RATIO end
    return r
end

-- Resolve the centered width fraction. Priority:
--   1. a width the user dragged on this screen (stored in ctx.state, in pixels)
--   2. the widthRatio custom-param default from Settings
local function resolveWidthRatio(ctx, area)
    local wr = clampRatio(ctx.custom and ctx.custom.widthRatio) or 0.6
    local bag = ctx.state
    if bag and type(bag.singleW) == "number" and area.width > 0 then
        wr = clampRatio(bag.singleW / area.width) or wr
    end
    return wr
end

-- The single-window height fraction (only the lone window uses it; the column
-- runs full height once there is more than one window).
local function resolveHeightRatio(ctx)
    return clampRatio(ctx.custom and ctx.custom.heightRatio) or 0.85
end

return pluau.algorithm {
    metadata = {
        name = "Centered",
        id = "centered-single",
        description = "Keeps windows in a centered column sized to a fraction of the screen width you can set per monitor, so nothing stretches across a wide display",
        -- Opt in to owning the single-window case (host honors this gate).
        supportsSingleWindow = true,
        supportsScriptState = true,
        supportsSplitRatio = true,
        centerLayout = true,
        supportsMasterCount = false,
        supportsMinSizes = false,
        defaultSplitRatio = 0.6,
        defaultMaxWindows = 6,
        minimumWindows = 1,
        masterZoneIndex = 0,
        zoneNumberDisplay = "all",
        customParams = {
            { name = "widthRatio", type = "number", default = 0.6, min = 0.2, max = 1.0,
                description = "Centered column width as a fraction of the screen" },
            { name = "heightRatio", type = "number", default = 0.85, min = 0.2, max = 1.0,
                description = "Single-window height as a fraction of the screen" },
        },
    },

    tile = function(ctx)
        local count = ctx.windowCount
        if count <= 0 then return {} end

        local area = ctx.area
        if area.width < pluau.MIN_ZONE_SIZE or area.height < pluau.MIN_ZONE_SIZE then
            return pluau.fillArea(area, count)
        end

        local gap = ctx.innerGap
        local wr = resolveWidthRatio(ctx, area)
        local bandW = math.max(pluau.MIN_ZONE_SIZE, math.floor(area.width * wr))
        local bandX = area.x + math.floor((area.width - bandW) / 2)

        if count == 1 then
            local hr = resolveHeightRatio(ctx)
            local ch = math.max(pluau.MIN_ZONE_SIZE, math.floor(area.height * hr))
            local cy = area.y + math.floor((area.height - ch) / 2)
            return { { x = bandX, y = cy, width = bandW, height = ch } }
        end

        -- Two or more windows: a full-height centered column. The first window
        -- takes the top portion (split ratio), the rest stack vertically below.
        local band = pluau.rect(bandX, area.y, bandW, area.height)
        local master, rest = band:splitTop(pluau.clampSplitRatio(ctx.splitRatio), gap)
        local zones = { master }
        local stacked = rest:rows(count - 1, gap)
        for i = 1, #stacked do
            zones[#zones + 1] = stacked[i]
        end
        return zones
    end,

    -- Remember what the user dragged the lone window to, in pixels, on THIS
    -- screen. The returned table becomes this state's persistent bag (kept
    -- because metadata.supportsScriptState is set); the next tile() converts it
    -- back to a fraction of the current work area. Only the single-window case
    -- feeds the remembered width; multi-window resizes are left to the split
    -- ratio so a master drag does not redefine the column width.
    onWindowResized = function(state, resize)
        if resize.index ~= 0 then
            return nil
        end
        if state and type(state.windowCount) == "number" and state.windowCount > 1 then
            return nil
        end
        local bag = (state and state.scriptState) or {}
        bag.singleW = resize.newRect.width
        return bag
    end,
}
