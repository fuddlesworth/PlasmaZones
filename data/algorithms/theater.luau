-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later
--
-- Theater
--
-- The focused window sits centered in a spotlight at a width you can set and
-- resize per monitor, so nothing ever stretches across a wide display. Every
-- other window lines up on rails in the margins on both sides. Focusing a rail
-- window brings it into the spotlight and the one it replaces drops back onto a
-- rail.
--
-- The spotlight follows focus because the host fills zones positionally: window
-- at tiled index i takes zone i. tile() places the centered zone at the focused
-- index and hands the remaining indices their rail slots in order, so the focused
-- window is always the one in the middle.
--
-- The spotlight width is remembered per monitor. Resize the centered window on a
-- given screen and that screen keeps it, independently of every other screen.
-- Each screen carries its own TilingState, so its persistent ctx.state bag is
-- already scoped to that screen and desktop; the onWindowResized hook stores the
-- dragged pixel width and tile() reconciles it against the current work area on
-- the next pass.

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

-- Resolve the spotlight width fraction. Priority:
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

local function resolveHeightRatio(ctx)
    return clampRatio(ctx.custom and ctx.custom.heightRatio) or 0.85
end

return pluau.algorithm {
    metadata = {
        name = "Theater",
        id = "theater",
        description = "Puts the focused window centered in a spotlight at a width you set per monitor. Every other window lines up on rails in the margins on both sides, and focusing one brings it into the spotlight",
        -- Opt in to owning the single-window case (host honors this gate).
        supportsSingleWindow = true,
        -- The spotlight follows focus, so the host must reflow when focus moves.
        retileOnFocus = true,
        supportsScriptState = true,
        supportsSplitRatio = false,
        centerLayout = true,
        supportsMasterCount = false,
        supportsMinSizes = false,
        defaultMaxWindows = 6,
        minimumWindows = 1,
        masterZoneIndex = 0,
        zoneNumberDisplay = "all",
        customParams = {
            { name = "widthRatio", type = "number", default = 0.6, min = 0.2, max = 1.0,
                description = "Spotlight width as a fraction of the screen" },
            { name = "heightRatio", type = "number", default = 0.85, min = 0.2, max = 1.0,
                description = "Spotlight height as a fraction of the screen" },
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
        local hr = resolveHeightRatio(ctx)
        local ch = math.max(pluau.MIN_ZONE_SIZE, math.floor(area.height * hr))
        local cy = area.y + math.floor((area.height - ch) / 2)

        if count == 1 then
            local bandW = math.max(pluau.MIN_ZONE_SIZE, math.floor(area.width * wr))
            local bandX = area.x + math.floor((area.width - bandW) / 2)
            return { { x = bandX, y = cy, width = bandW, height = ch } }
        end

        -- Two or more windows: the spotlight stays centered with the other windows
        -- split across rails in both side margins. Clamp the spotlight so each
        -- margin keeps room for a usable rail strip plus the gap; if even that
        -- cannot fit, fill the area.
        local centerW = math.floor(area.width * wr)
        local maxCenterW = area.width - 2 * (gap + pluau.MIN_ZONE_SIZE)
        if maxCenterW < pluau.MIN_ZONE_SIZE then
            return pluau.fillArea(area, count)
        end
        if centerW > maxCenterW then centerW = maxCenterW end
        if centerW < pluau.MIN_ZONE_SIZE then centerW = pluau.MIN_ZONE_SIZE end

        local sideW = math.floor((area.width - centerW) / 2)
        local centerX = area.x + sideW
        local centerZone = { x = centerX, y = cy, width = centerW, height = ch }

        -- Split the rail windows across the two margins, giving the extra one to
        -- the right when the count is odd so the column heights stay balanced.
        local rest = count - 1
        local rightCount = math.floor((rest + 1) / 2)
        local leftCount = rest - rightCount

        local slots = {}
        if rightCount > 0 then
            local rightX = centerX + centerW + gap
            local rightW = math.max(pluau.MIN_ZONE_SIZE, area.x + area.width - rightX)
            local r = pluau.rect(rightX, area.y, rightW, area.height):rows(rightCount, gap)
            for i = 1, #r do slots[#slots + 1] = r[i] end
        end
        if leftCount > 0 then
            local leftW = math.max(pluau.MIN_ZONE_SIZE, sideW - gap)
            local l = pluau.rect(area.x, area.y, leftW, area.height):rows(leftCount, gap)
            for i = 1, #l do slots[#slots + 1] = l[i] end
        end

        -- Place the spotlight at the focused index so the host (which fills zones
        -- positionally) lands the focused window in the middle. The remaining
        -- windows take the rail slots in their natural order.
        local f = ctx.focusedIndex
        if type(f) ~= "number" or f < 0 or f >= count then f = 0 end

        local zones = {}
        local slotIdx = 1
        for i = 1, count do
            if (i - 1) == f then
                zones[i] = centerZone
            else
                zones[i] = slots[slotIdx]
                slotIdx = slotIdx + 1
            end
        end
        return zones
    end,

    -- Only the spotlight (the focused window) carries the remembered width. A
    -- horizontal drag on it stores the new pixel width, which tile() converts back
    -- to a fraction of the current work area on the next pass. Rail windows are
    -- derived from what is left, so a drag on one of them no-ops.
    onWindowResized = function(state, resize)
        local f = state and state.focusedIndex
        if type(f) ~= "number" or f < 0 then f = 0 end
        if resize.index ~= f then
            return nil
        end
        local edges = resize.edges
        local widthChanged = edges and (edges.left or edges.right)
        if not widthChanged then
            return nil
        end
        local newW = resize.newRect and resize.newRect.width or 0
        if type(newW) ~= "number" or newW <= 0 then
            return nil
        end
        local result = (state and state.scriptState) or {}
        result.singleW = newW
        return result
    end,
}
