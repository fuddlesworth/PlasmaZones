-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Floating Center",
        id = "floating-center",
        description = "Centered main window with peripheral panels on all sides",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.65,
        defaultMaxWindows = 6,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        masterZoneIndex = 0,
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        local area = ctx.area
        local gap = ctx.innerGap
        local minSizes = ctx.minSizes

        local early = pluau.guardArea(area, count)
        if early then return early end

        if count == 1 then
            local splitRatio1 = pluau.clampSplitRatio(ctx.splitRatio)
            local mw, mh = pluau.minSizeAt(minSizes, 0)
            local cw = math.min(math.max(math.floor(area.width * splitRatio1), mw), area.width)
            local ch = math.min(math.max(math.floor(area.height * splitRatio1), mh), area.height)
            local cx = pluau.center(area.x, area.width, cw)
            local cy = pluau.center(area.y, area.height, ch)
            return { { x = cx, y = cy, width = cw, height = ch } }
        end

        local splitRatio = pluau.clampSplitRatio(ctx.splitRatio)
        local remaining = count - 1

        local leftCount, rightCount, bottomCount, topCount = 0, 0, 0, 0
        if remaining >= 1 then rightCount = 1 end
        if remaining >= 2 then leftCount = 1 end
        if remaining >= 3 then bottomCount = 1 end
        if remaining >= 4 then topCount = 1 end

        local extras = math.max(0, remaining - 4)
        bottomCount = bottomCount + math.ceil(extras / 2)
        topCount = topCount + math.floor(extras / 2)

        -- Panel membership follows zone append order (the host fills zones
        -- positionally): window 0 is the center, window 1 the right panel,
        -- window 2 the left panel, then the bottom strip, then the top strip.
        local centerMinW, centerMinH = pluau.minSizeAt(minSizes, 0)
        local nextIdx = 1
        local rightMinW, rightMinH = 0, 0
        if rightCount > 0 then
            rightMinW, rightMinH = pluau.minSizeAt(minSizes, nextIdx)
            nextIdx = nextIdx + rightCount
        end
        local leftMinW, leftMinH = 0, 0
        if leftCount > 0 then
            leftMinW, leftMinH = pluau.minSizeAt(minSizes, nextIdx)
            nextIdx = nextIdx + leftCount
        end
        local bottomStart = nextIdx
        nextIdx = nextIdx + bottomCount
        local topStart = nextIdx

        local function stripMaxMinH(start, n)
            local m = 0
            for k = 0, n - 1 do
                local _, mh = pluau.minSizeAt(minSizes, start + k)
                if mh > m then m = mh end
            end
            return m
        end
        local bottomNeedH = (bottomCount > 0) and stripMaxMinH(bottomStart, bottomCount) or 0
        local topNeedH = (topCount > 0) and stripMaxMinH(topStart, topCount) or 0

        -- Size the center to the ratio, grown to its own minimum, then push
        -- the margins out far enough for the side panels' minimum widths and
        -- the strips' minimum heights. The margins win over the center's
        -- minimum when both cannot fit; the engine's post-layout min-size
        -- pass absorbs any residue.
        local centerW = math.max(math.floor(area.width * splitRatio), math.min(centerMinW, area.width))
        local centerH = math.floor(area.height * splitRatio)
        -- Side panels span the center's height, so their minimum heights are
        -- a floor on the center height as well.
        centerH = math.max(centerH, math.min(math.max(centerMinH, rightMinH, leftMinH), area.height))

        local marginX = math.floor((area.width - centerW) / 2)
        local marginY = math.floor((area.height - centerH) / 2)

        -- Only recompute the center from a pushed-out margin when a minimum
        -- actually binds; otherwise keep the ratio-derived size so the center
        -- rect is byte-identical to the min-free behavior (the right/bottom
        -- panels absorb the rounding pixel, as before). The top/bottom strips
        -- are not covered by this claim: they now distribute through
        -- distributeWithMinSizes, whose remainder handling differs from the
        -- old even split.
        local sideNeedW = math.max(rightMinW, leftMinW)
        if sideNeedW > 0 and marginX < sideNeedW + gap then
            marginX = sideNeedW + gap
            centerW = area.width - 2 * marginX
        end
        local stripNeedH = math.max(bottomNeedH, topNeedH)
        if stripNeedH > 0 and marginY < stripNeedH + gap then
            marginY = stripNeedH + gap
            centerH = area.height - 2 * marginY
        end

        local centerX = area.x + marginX
        local centerY = area.y + marginY

        if marginX <= gap or marginY <= gap or centerW < 1 or centerH < 1 then
            return pluau.fillArea(area, count)
        end

        local zones = {}

        local topH = math.max(1, marginY - gap)
        local bottomY = area.y + marginY + centerH + gap
        local bottomH = math.max(1, area.y + area.height - bottomY)

        local sideTop = area.y + marginY
        local sideH = centerH

        local leftW = math.max(1, marginX - gap)
        local rightX = area.x + marginX + centerW + gap
        local rightW = math.max(1, area.x + area.width - rightX)

        zones[#zones + 1] = { x = centerX, y = centerY, width = centerW, height = centerH }

        -- Min-aware horizontal strip: columns sized with the members' minimum
        -- widths so a wide-minimum window takes space from its strip
        -- neighbours instead of spilling over them.
        local function stripColumns(x, y, w, h, start, n)
            local mins = {}
            for k = 0, n - 1 do
                local mw = pluau.minSizeAt(minSizes, start + k)
                mins[#mins + 1] = mw
            end
            local widths = pluau.distributeWithMinSizes(w, n, gap, mins)
            local cx = x
            for k = 1, n do
                zones[#zones + 1] = { x = cx, y = y, width = widths[k], height = h }
                cx = cx + widths[k] + gap
            end
        end

        -- rightW/leftW/topH are floored at 1 by math.max(1, ...), so their old
        -- `>= 1` guards were always true; the overlapping-fallback else arms were
        -- dead. bottomH keeps its guard: the ANDed bounds check can be false.
        if rightCount > 0 then
            pluau.stripLayout(zones, rightX, sideTop, rightW, sideH, rightCount, gap, false)
        end

        if leftCount > 0 then
            pluau.stripLayout(zones, area.x, sideTop, leftW, sideH, leftCount, gap, false)
        end

        if bottomCount > 0 then
            if bottomH >= 1 and bottomY < area.y + area.height then
                stripColumns(area.x, bottomY, area.width, bottomH, bottomStart, bottomCount)
            else
                for _ = 1, bottomCount do
                    zones[#zones + 1] = { x = centerX, y = centerY, width = centerW, height = centerH }
                end
            end
        end

        if topCount > 0 then
            stripColumns(area.x, area.y, area.width, topH, topStart, topCount)
        end

        return zones
    end,
}
