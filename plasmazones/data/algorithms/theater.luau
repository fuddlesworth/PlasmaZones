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

-- Resolve the spotlight width fraction. Priority:
--   1. a width the user dragged on this screen (stored in ctx.state, in pixels)
--   2. the widthRatio custom-param default from Settings
local function resolveWidthRatio(ctx, area)
    local wr = pluau.clamp(ctx.custom and ctx.custom.widthRatio, MIN_RATIO, MAX_RATIO) or 0.6
    local bag = ctx.state
    if bag and type(bag.singleW) == "number" and area.width > 0 then
        wr = pluau.clamp(bag.singleW / area.width, MIN_RATIO, MAX_RATIO) or wr
    end
    return wr
end

local function resolveHeightRatio(ctx)
    return pluau.clamp(ctx.custom and ctx.custom.heightRatio, MIN_RATIO, MAX_RATIO) or 0.85
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
        defaultMaxWindows = 6,
        minimumWindows = 1,
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
        local area = ctx.area
        local early = pluau.guardArea(area, count)
        if early then return early end

        local gap = ctx.innerGap
        local minSizes = ctx.minSizes
        local wr = resolveWidthRatio(ctx, area)
        local hr = resolveHeightRatio(ctx)

        -- The focused window occupies the spotlight, so the spotlight must
        -- hold at least the focused window's minimum size (clamped to the
        -- work area). Resolve the focused index up front; the rail helpers
        -- below need it too.
        local f = ctx.focusedIndex
        if type(f) ~= "number" or f < 0 or f >= count then f = 0 end
        local fMinW, fMinH = pluau.minSizeAt(minSizes, f)

        local ch = math.max(pluau.MIN_ZONE_SIZE, math.floor(area.height * hr), math.min(fMinH, area.height))
        local cy = pluau.center(area.y, area.height, ch)

        if count == 1 then
            local bandW = math.max(pluau.MIN_ZONE_SIZE, math.floor(area.width * wr))
            bandW = math.min(math.max(bandW, fMinW), area.width)
            local bandX = pluau.center(area.x, area.width, bandW)
            return { { x = bandX, y = cy, width = bandW, height = ch } }
        end

        -- Split the rail windows across the two margins, giving the extra one to
        -- the right rail when the number of rail windows is odd so the two
        -- columns stay as balanced as the count allows.
        local rest = count - 1
        local rightCount = math.floor((rest + 1) / 2)
        local leftCount = rest - rightCount

        -- Rail membership in landing order: the placement loop below hands the
        -- non-focused windows the rail slots in their natural index order,
        -- right rail first, so railIdx[1..rightCount] land on the right and
        -- the rest on the left.
        local railIdx = {}
        for i = 0, count - 1 do
            if i ~= f then railIdx[#railIdx + 1] = i end
        end
        local function railMaxMinW(from, to)
            local m = 0
            for k = from, to do
                local mw = pluau.minSizeAt(minSizes, railIdx[k])
                if mw > m then m = mw end
            end
            return m
        end
        local rightMinW = (rightCount > 0) and railMaxMinW(1, rightCount) or 0
        local leftMinW = (leftCount > 0) and railMaxMinW(rightCount + 1, rest) or 0

        -- Two or more windows: the spotlight stays centered with the other windows
        -- split across rails in both side margins. Clamp the spotlight so each
        -- margin keeps room for its rail's widest minimum (or the base strip)
        -- plus the gap; if even that cannot fit, fill the area. When the
        -- focused minimum and the rail requirement conflict, the rails win and
        -- the engine's post-layout min-size pass absorbs the residue.
        local sideNeed = math.max(pluau.MIN_ZONE_SIZE, rightMinW, leftMinW)
        local centerW = math.max(math.floor(area.width * wr), math.min(fMinW, area.width))
        local maxCenterW = area.width - 2 * (gap + sideNeed)
        if maxCenterW < pluau.MIN_ZONE_SIZE then
            return pluau.fillArea(area, count)
        end
        if centerW > maxCenterW then centerW = maxCenterW end
        if centerW < pluau.MIN_ZONE_SIZE then centerW = pluau.MIN_ZONE_SIZE end

        local sideW = math.floor((area.width - centerW) / 2)
        local centerX = area.x + sideW
        local centerZone = { x = centerX, y = cy, width = centerW, height = ch }

        -- Rail columns: rows sized with the members' minimum heights so a
        -- rail window with a tall minimum takes its space from its column
        -- neighbours instead of spilling over them.
        local slots = {}
        local function railColumn(x, w, from, to)
            local n = to - from + 1
            local mins = {}
            for k = from, to do
                local _, mh = pluau.minSizeAt(minSizes, railIdx[k])
                mins[#mins + 1] = mh
            end
            local heights = pluau.distributeWithMinSizes(area.height, n, gap, mins)
            local y = area.y
            for k = 1, n do
                slots[#slots + 1] = { x = x, y = y, width = w, height = heights[k] }
                y = y + heights[k] + gap
            end
        end
        if rightCount > 0 then
            local rightX = centerX + centerW + gap
            local rightW = math.max(pluau.MIN_ZONE_SIZE, area.x + area.width - rightX)
            railColumn(rightX, rightW, 1, rightCount)
        end
        if leftCount > 0 then
            local leftW = math.max(pluau.MIN_ZONE_SIZE, sideW - gap)
            railColumn(area.x, leftW, rightCount + 1, rest)
        end

        -- Place the spotlight at the focused index so the host (which fills zones
        -- positionally) lands the focused window in the middle. The remaining
        -- windows take the rail slots in their natural order.
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
