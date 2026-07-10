-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: LGPL-2.1-or-later
--
-- The `pluau` tiling standard library. Injected as a global by LuauTileAlgorithm
-- before the VM is sandboxed, then frozen.
--
-- Two layers:
--   1. A high-level Rect API (split*/columns/rows/join) + pluau.algorithm{} wrapper
--      for ergonomic NEW authoring (ctx.area arrives as a Rect with methods).
--   2. Faithful Luau ports of the C++/JS geometry builtins (distributeWithGaps,
--      distributeWithMinSizes, equalColumnsLayout, fillArea, …) that the bundled
--      algorithms use to stay byte-identical to the prior JS engine. These are
--      direct translations — preserve their arithmetic exactly.

-- ── Constants (mirror AutotileConstants.h; literals avoid locale float issues) ──
local MIN_ZONE_SIZE = 50
local MIN_SPLIT = 0.1
local MAX_SPLIT = 0.9
local MAX_TREE_DEPTH = 50

-- ── High-level Rect API ────────────────────────────────────────────────────
local Rect = {}
Rect.__index = Rect

local function rect(x, y, w, h)
    return setmetatable({ x = x, y = y, width = w, height = h }, Rect)
end

function Rect.splitLeft(self, ratio, gap)
    local avail = self.width - gap
    local lw = math.floor(avail * ratio)
    return rect(self.x, self.y, lw, self.height),
        rect(self.x + lw + gap, self.y, self.width - lw - gap, self.height)
end

function Rect.splitRight(self, ratio, gap)
    local left, rest = self:splitLeft(1 - ratio, gap)
    return rest, left
end

function Rect.splitTop(self, ratio, gap)
    local avail = self.height - gap
    local th = math.floor(avail * ratio)
    return rect(self.x, self.y, self.width, th),
        rect(self.x, self.y + th + gap, self.width, self.height - th - gap)
end

function Rect.splitBottom(self, ratio, gap)
    local top, rest = self:splitTop(1 - ratio, gap)
    return rest, top
end

function Rect.columns(self, n, gap)
    local out = {}
    if n <= 0 then return out end
    local cw = math.floor((self.width - gap * (n - 1)) / n)
    local x = self.x
    for i = 1, n do
        local w = (i == n) and (self.x + self.width - x) or cw
        out[i] = rect(x, self.y, w, self.height)
        x = x + cw + gap
    end
    return out
end

function Rect.rows(self, n, gap)
    local out = {}
    if n <= 0 then return out end
    local rh = math.floor((self.height - gap * (n - 1)) / n)
    local y = self.y
    for i = 1, n do
        local h = (i == n) and (self.y + self.height - y) or rh
        out[i] = rect(self.x, y, self.width, h)
        y = y + rh + gap
    end
    return out
end

local function join(...)
    local out = {}
    for _, group in ipairs({ ... }) do
        for _, v in ipairs(group) do
            out[#out + 1] = v
        end
    end
    return out
end

-- ── Faithful builtin ports (1-indexed; JS index i ↔ Lua index i) ───────────

-- Port of distributeWithGaps.js
local function distributeWithGaps(total, count, gap)
    gap = math.max(0, gap)
    if count <= 0 then return {} end
    if total <= 0 then
        local fallback = {}
        for f = 1, count do fallback[f] = 1 end
        return fallback
    end
    if count == 1 then return { total } end

    local totalGaps = (count - 1) * gap
    local available = math.max(count, total - totalGaps)
    local base = math.floor(available / count)
    local remainder = available - base * count
    local sizes = {}
    for i = 1, count do
        local s = base
        if remainder > 0 then
            s = s + 1
            remainder = remainder - 1
        end
        sizes[i] = s
    end

    local usedSpace = totalGaps
    for i = 1, count do usedSpace = usedSpace + sizes[i] end
    local excess = usedSpace - total
    while excess > 0 do
        local maxIdx = -1
        for i = 1, count do
            if sizes[i] > 1 and (maxIdx < 0 or sizes[i] > sizes[maxIdx]) then maxIdx = i end
        end
        if maxIdx < 0 then break end
        local take = math.min(excess, sizes[maxIdx] - 1)
        sizes[maxIdx] = sizes[maxIdx] - take
        excess = excess - take
    end
    return sizes
end

-- Port of distributeWithMinSizes.js
local function distributeWithMinSizes(total, count, gap, minDims)
    gap = math.max(0, gap)
    if count <= 0 then return {} end
    if total <= 0 then
        local fallback = {}
        for f = 1, count do fallback[f] = 1 end
        return fallback
    end
    if count == 1 then return { total } end
    local totalGaps = (count - 1) * gap
    local available = math.max(count, total - totalGaps)
    if not minDims or #minDims == 0 then
        return distributeWithGaps(total, count, gap)
    end

    local mins = {}
    local totalMin = 0
    for i = 1, count do
        local m = (i <= #minDims and minDims[i] > 0) and minDims[i] or 1
        mins[i] = m
        totalMin = totalMin + m
    end

    local sizes = {}
    if totalMin >= available then
        local remaining = available
        local remainingMin = totalMin
        for i = 1, count do
            local allocated
            if remainingMin > 0 then
                allocated = math.floor(remaining * mins[i] / remainingMin)
            else
                allocated = math.floor(remaining / math.max(1, count - i + 1))
            end
            allocated = math.max(1, math.min(allocated, remaining))
            sizes[i] = allocated
            remaining = remaining - allocated
            remainingMin = remainingMin - mins[i]
        end
        local currentSum = 0
        for i = 1, count do currentSum = currentSum + sizes[i] end
        local diff = currentSum - available
        if diff > 0 then
            for _ = 1, count do
                if diff <= 0 then break end
                for i = count, 1, -1 do
                    if diff <= 0 then break end
                    if sizes[i] > 1 then
                        sizes[i] = sizes[i] - 1
                        diff = diff - 1
                    end
                end
            end
        elseif diff < 0 then
            for _ = 1, count do
                if diff >= 0 then break end
                for i = 1, count do
                    if diff >= 0 then break end
                    sizes[i] = sizes[i] + 1
                    diff = diff + 1
                end
            end
        end
    else
        local base = math.floor(available / count)
        local rem = available % count
        local equalSizes = {}
        for i = 1, count do
            local s = base
            if rem > 0 then
                s = s + 1
                rem = rem - 1
            end
            equalSizes[i] = s
        end
        local equalSatisfies = true
        for i = 1, count do
            if equalSizes[i] < mins[i] then
                equalSatisfies = false
                break
            end
        end
        if equalSatisfies then
            sizes = equalSizes
        else
            local equalShare = math.floor(available / count)
            local surplus = available - totalMin
            local unconstrainedCount = 0
            for i = 1, count do
                sizes[i] = mins[i]
                if mins[i] <= equalShare then unconstrainedCount = unconstrainedCount + 1 end
            end
            if unconstrainedCount > 0 and surplus > 0 then
                local sBase = math.floor(surplus / unconstrainedCount)
                local sRem = surplus % unconstrainedCount
                for i = 1, count do
                    if mins[i] <= equalShare then
                        sizes[i] = sizes[i] + sBase
                        if sRem > 0 then
                            sizes[i] = sizes[i] + 1
                            sRem = sRem - 1
                        end
                    end
                end
            elseif surplus > 0 then
                local sBase = math.floor(surplus / count)
                local sRem = surplus % count
                for i = 1, count do
                    sizes[i] = sizes[i] + sBase
                    if sRem > 0 then
                        sizes[i] = sizes[i] + 1
                        sRem = sRem - 1
                    end
                end
            end
        end
    end
    return sizes
end

-- Port of distributeWithOptionalMins.js
local function distributeWithOptionalMins(total, count, gap, minDims)
    if not minDims or #minDims == 0 then
        return distributeWithGaps(total, count, gap)
    end
    return distributeWithMinSizes(total, count, gap, minDims)
end

-- Port of extractMinDims.js
local function _extractMinDims(minSizes, count, startIdx, axis)
    if not minSizes or #minSizes == 0 then return {} end
    local result = {}
    for i = 1, count do
        local idx = startIdx + (i - 1) -- 0-based source index
        local entry = minSizes[idx + 1] -- Lua 1-based
        local v = (entry and entry[axis] and entry[axis] > 0) and entry[axis] or 0
        result[i] = v
    end
    return result
end

local function extractMinWidths(minSizes, count, startIdx)
    return _extractMinDims(minSizes, count, startIdx or 0, "w")
end

local function extractMinHeights(minSizes, count, startIdx)
    return _extractMinDims(minSizes, count, startIdx or 0, "h")
end

-- Port of fillRegion.js / fillArea.js
local function fillRegion(x, y, w, h, count)
    local zones = {}
    for i = 1, count do
        zones[i] = { x = x, y = y, width = w, height = h }
    end
    return zones
end

local function fillArea(area, count)
    return fillRegion(area.x, area.y, math.max(1, area.width), math.max(1, area.height), count)
end

-- Port of equalColumnsLayout.js
local function equalColumnsLayout(area, count, gap, minSizes)
    if count <= 0 then return {} end
    local minWidths = extractMinWidths(minSizes, count)
    local columnWidths = distributeWithOptionalMins(area.width, count, gap, minWidths)
    local zones = {}
    local currentX = area.x
    for i = 1, count do
        zones[i] = { x = currentX, y = area.y, width = columnWidths[i], height = area.height }
        currentX = currentX + columnWidths[i] + gap
    end
    return zones
end

-- Port of clampSplitRatio.js
local function clampSplitRatio(ratio)
    return math.max(MIN_SPLIT, math.min(MAX_SPLIT, ratio))
end

-- Port of extractRegionMaxMin.js
local function extractRegionMaxMin(minSizes, startIdx, endIdx, axis)
    startIdx = math.max(0, startIdx)
    local maxVal = 0
    if not minSizes or #minSizes == 0 then return 0 end
    local i = startIdx -- 0-based source index
    while i < endIdx and i < #minSizes do
        local entry = minSizes[i + 1]
        -- Guard `entry` for a sparse minSizes, matching _extractMinDims /
        -- interleaveMinWidths (defensive; the host always marshals dense entries).
        local v = (entry and entry[axis] and entry[axis] > 0) and entry[axis] or 0
        if v > maxVal then maxVal = v end
        i = i + 1
    end
    return maxVal
end

-- Port of solveTwoPart.js
local function solveTwoPart(contentDim, firstDim, secondDim, minFirst, minSecond)
    local totalMin = math.max(minFirst, 0) + math.max(minSecond, 0)
    if totalMin > contentDim and totalMin > 0 then
        firstDim = math.floor(contentDim * math.max(minFirst, 1) / totalMin)
        secondDim = contentDim - firstDim
    else
        if minFirst > 0 and firstDim < minFirst then
            firstDim = minFirst
            secondDim = contentDim - firstDim
        end
        if minSecond > 0 and secondDim < minSecond then
            secondDim = minSecond
            firstDim = contentDim - secondDim
        end
    end
    firstDim = math.max(1, firstDim)
    secondDim = math.max(1, secondDim)
    return { first = firstDim, second = secondDim }
end

-- Port of masterStackLayout.js (horizontal=false → left/right, true → top/bottom)
local function masterStackLayout(area, count, gap, splitRatio, masterCount, minSizes, horizontal)
    if count <= 0 then return {} end
    if area.width < MIN_ZONE_SIZE or area.height < MIN_ZONE_SIZE then
        return fillArea(area, count)
    end
    masterCount = math.max(1, math.min(masterCount, count))
    local stackCount = count - masterCount
    splitRatio = math.max(MIN_SPLIT, math.min(splitRatio, MAX_SPLIT))

    local primaryAxis = horizontal and "h" or "w"
    local primarySize = horizontal and area.height or area.width
    local secondarySize = horizontal and area.width or area.height

    local minMasterPrimary = extractRegionMaxMin(minSizes, 0, masterCount, primaryAxis)
    local minStackPrimary = extractRegionMaxMin(minSizes, masterCount, count, primaryAxis)

    local masterPrimary, stackPrimary
    if stackCount == 0 then
        masterPrimary = primarySize
        stackPrimary = 0
    else
        local contentPrimary = primarySize - gap
        if contentPrimary <= 0 then
            return fillArea(area, count)
        end
        masterPrimary = math.floor(contentPrimary * splitRatio)
        stackPrimary = contentPrimary - masterPrimary
        local solved = solveTwoPart(contentPrimary, masterPrimary, stackPrimary, minMasterPrimary, minStackPrimary)
        masterPrimary = solved.first
        stackPrimary = solved.second
    end

    local masterMinSec = horizontal and extractMinWidths(minSizes, masterCount)
        or extractMinHeights(minSizes, masterCount)
    local stackMinSec = horizontal and extractMinWidths(minSizes, stackCount, masterCount)
        or extractMinHeights(minSizes, stackCount, masterCount)

    if secondarySize <= 0 then
        return fillArea(area, count)
    end

    local masterSecondary = distributeWithOptionalMins(secondarySize, masterCount, gap, masterMinSec)

    local zones = {}
    local primaryPos = horizontal and area.y or area.x
    local secondaryPos = horizontal and area.x or area.y
    local cursor = secondaryPos

    for i = 1, masterCount do
        if horizontal then
            zones[#zones + 1] = { x = cursor, y = primaryPos, width = masterSecondary[i], height = masterPrimary }
        else
            zones[#zones + 1] = { x = primaryPos, y = cursor, width = masterPrimary, height = masterSecondary[i] }
        end
        cursor = cursor + masterSecondary[i] + gap
    end

    if stackCount > 0 then
        local stackSecondary = distributeWithOptionalMins(secondarySize, stackCount, gap, stackMinSec)
        local stackStart = primaryPos + masterPrimary + gap
        cursor = secondaryPos
        for i = 1, stackCount do
            if horizontal then
                zones[#zones + 1] = { x = cursor, y = stackStart, width = stackSecondary[i], height = stackPrimary }
            else
                zones[#zones + 1] = { x = stackStart, y = cursor, width = stackPrimary, height = stackSecondary[i] }
            end
            cursor = cursor + stackSecondary[i] + gap
        end
    end

    return zones
end

-- Port of deckLayout.js (focused window + peeking background windows)
local function deckLayout(area, count, focusedFraction, horizontal)
    if count <= 0 then return {} end
    if area.width <= 0 or area.height <= 0 then
        return fillArea(area, count)
    end
    local axisSize = horizontal and area.height or area.width
    local bgCount = count - 1
    local focusedSize = math.max(1, math.floor(axisSize * focusedFraction))
    local peekTotal = axisSize - focusedSize
    local peekSize = (bgCount > 0) and math.max(1, math.floor(math.max(0, peekTotal) / bgCount)) or 0

    local zones = {}
    if horizontal then
        zones[1] = { x = area.x, y = area.y, width = area.width, height = focusedSize }
    else
        zones[1] = { x = area.x, y = area.y, width = focusedSize, height = area.height }
    end
    for i = 0, bgCount - 1 do
        local peekOffset = math.min(focusedSize + i * peekSize, axisSize - 1)
        if horizontal then
            local peekY = area.y + peekOffset
            zones[#zones + 1] = { x = area.x, y = math.min(peekY, area.y + area.height - 1),
                width = area.width, height = math.max(1, area.y + area.height - peekY) }
        else
            local peekX = area.x + peekOffset
            zones[#zones + 1] = { x = math.min(peekX, area.x + area.width - 1), y = area.y,
                width = math.max(1, area.x + area.width - peekX), height = area.height }
        end
    end
    return zones
end

-- Port of applyPerWindowMinSize.js
local function applyPerWindowMinSize(w, h, minSizes, i)
    if minSizes and i < #minSizes then
        local entry = minSizes[i + 1] -- i is 0-based
        -- Guard `entry` for a sparse minSizes, matching the other readers
        -- (defensive; the host always marshals dense entries).
        if entry and entry.w and entry.w > 0 then w = math.max(w, entry.w) end
        if entry and entry.h and entry.h > 0 then h = math.max(h, entry.h) end
    end
    return { w = w, h = h }
end

-- Port of distributeEvenly.js (returns { {pos=,size=}, ... })
local function distributeEvenly(start, total, count, gap)
    gap = math.max(0, gap)
    if count <= 0 then return {} end
    if total <= 0 then
        local r = {}
        for i = 1, count do r[i] = { pos = start, size = 1 } end
        return r
    end
    if count == 1 then return { { pos = start, size = total } } end
    local totalGaps = (count - 1) * gap
    if totalGaps >= total then
        local shrinkRatio = (total > 0) and (total / (totalGaps + count)) or 0
        local itemSize = math.max(1, math.floor(shrinkRatio))
        local shrunkGap = math.max(0, math.floor(gap * shrinkRatio))
        local r = {}
        local pos = start
        for i = 0, count - 1 do
            if i == count - 1 then
                pos = math.min(pos, start + total - 1)
                local clampedSize = math.max(1, math.min(itemSize, start + total - pos))
                r[#r + 1] = { pos = pos, size = clampedSize }
            else
                r[#r + 1] = { pos = pos, size = itemSize }
            end
            pos = pos + itemSize + shrunkGap
        end
        return r
    end
    local tileSize = math.max(1, math.floor((total - totalGaps) / count))
    local result = {}
    for i = 0, count - 1 do
        local pos = start + i * (tileSize + gap)
        local size = (i == count - 1) and math.max(1, start + total - pos) or tileSize
        result[#result + 1] = { pos = pos, size = size }
    end
    return result
end

-- Port of lShapeLayout.js (master corner + right column + bottom row)
local function lShapeLayout(area, count, gap, splitRatio, distribute, bottomWidth, rightHeight)
    distribute = distribute or "alternate"
    bottomWidth = bottomWidth or "master"
    rightHeight = rightHeight or "master"
    if count <= 0 then return {} end
    if area.width < MIN_ZONE_SIZE or area.height < MIN_ZONE_SIZE then
        return fillArea(area, count)
    end
    splitRatio = math.max(MIN_SPLIT, math.min(MAX_SPLIT, splitRatio))
    local masterW = math.max(1, math.floor(area.width * splitRatio - gap / 2))
    local masterH = math.max(1, math.floor(area.height * splitRatio - gap / 2))
    local zones = { { x = area.x, y = area.y, width = masterW, height = masterH } }
    if count <= 1 then return zones end

    local rH = (rightHeight == "master") and masterH or area.height
    if type(rightHeight) == "number" then rH = rightHeight end
    if count == 2 then
        zones[#zones + 1] = { x = area.x + masterW + gap, y = area.y,
            width = math.max(1, area.x + area.width - (area.x + masterW + gap)), height = rH }
        return zones
    end

    local rightCount, bottomCount
    if distribute == "alternate" then
        rightCount = 0
        bottomCount = 0
        for i = 1, count - 1 do
            if (i - 1) % 2 == 0 then rightCount = rightCount + 1 else bottomCount = bottomCount + 1 end
        end
    else
        local remaining = count - 1
        rightCount = math.ceil(remaining / 2)
        bottomCount = math.floor(remaining / 2)
    end

    local rightX = area.x + masterW + gap
    local rightW = math.max(1, area.x + area.width - rightX)
    if rightHeight == "master" and bottomCount > 0 then rH = masterH end
    local btmW = (bottomWidth == "full") and area.width or masterW
    if type(bottomWidth) == "number" then btmW = bottomWidth end
    local rightTotalGaps = (rightCount - 1) * gap
    local bottomTotalGaps = (bottomCount - 1) * gap
    if (rightCount > 0 and rH - rightTotalGaps <= 0) or (bottomCount > 0 and btmW - bottomTotalGaps <= 0) then
        for _ = 1, count - 1 do
            zones[#zones + 1] = { x = area.x, y = area.y, width = area.width, height = area.height }
        end
        return zones
    end

    local rightTileH = (rightCount > 0) and math.max(1, math.floor((rH - rightTotalGaps) / rightCount)) or 0
    for r = 0, rightCount - 1 do
        local ry = math.min(area.y + r * (rightTileH + gap), area.y + rH - 1)
        local rh = math.max(1, (r == rightCount - 1) and (area.y + rH - ry) or rightTileH)
        zones[#zones + 1] = { x = rightX, y = ry, width = rightW, height = rh }
    end
    if bottomCount > 0 then
        local bottomY = area.y + masterH + gap
        local bottomH = math.max(1, area.y + area.height - bottomY)
        local bottomTileW = math.max(1, math.floor((btmW - bottomTotalGaps) / bottomCount))
        for b = 0, bottomCount - 1 do
            local bx = math.min(area.x + b * (bottomTileW + gap), area.x + btmW - 1)
            local bw = math.max(1, (b == bottomCount - 1) and (area.x + btmW - bx) or bottomTileW)
            zones[#zones + 1] = { x = bx, y = bottomY, width = bw, height = bottomH }
        end
    end
    return zones
end

-- Port of computeCumulativeMinDims.js (minW/minH are keyed 0..windowCount)
local function computeCumulativeMinDims(windowCount, minSizes, innerGap)
    local minW = {}
    local minH = {}
    for i = 0, windowCount do
        minW[i] = 0
        minH[i] = 0
    end
    if not minSizes or #minSizes == 0 then return { minW = minW, minH = minH } end
    for i = windowCount - 1, 0, -1 do
        local entry = minSizes[i + 1] -- JS minSizes[i] (0-based)
        local mw = (i < #minSizes and entry and entry.w and entry.w > 0) and entry.w or 0
        local mh = (i < #minSizes and entry and entry.h and entry.h > 0) and entry.h or 0
        local splitV = (i % 2 == 0)
        if splitV then
            minW[i] = mw + ((i < windowCount - 1 and minW[i + 1] > 0) and (innerGap + minW[i + 1]) or 0)
            minH[i] = math.max(mh, minH[i + 1])
        else
            minH[i] = mh + ((i < windowCount - 1 and minH[i + 1] > 0) and (innerGap + minH[i + 1]) or 0)
            minW[i] = math.max(mw, minW[i + 1])
        end
    end
    return { minW = minW, minH = minH }
end

-- Port of appendGracefulDegradation.js (mutates `zones` in place)
local function appendGracefulDegradation(zones, remaining, leftover, innerGap)
    if #zones == 0 then return end
    if leftover <= 0 then return end

    local function degradeAxis(primarySize, primaryPos, buildZone)
        if primarySize <= 0 then return end
        local maxFit = math.max(1, math.floor(primarySize / MIN_ZONE_SIZE))
        local fitCount = math.min(leftover + 1, maxFit)
        local sizes = distributeWithGaps(primarySize, fitCount, innerGap)
        if #sizes == 0 then return end
        zones[#zones] = buildZone(primaryPos, sizes[1])
        local cursor = primaryPos + sizes[1] + innerGap
        for k = 2, fitCount do -- JS j=1..fitCount-1 ↔ Lua sizes[k], k=j+1
            zones[#zones + 1] = buildZone(cursor, sizes[k])
            cursor = cursor + sizes[k] + innerGap
        end
        for _ = fitCount, leftover do
            local last = zones[#zones]
            zones[#zones + 1] = { x = last.x, y = last.y, width = last.width, height = last.height }
        end
    end

    if remaining.width >= remaining.height then
        degradeAxis(remaining.width, remaining.x, function(pos, size)
            return { x = pos, y = remaining.y, width = size, height = remaining.height }
        end)
    else
        degradeAxis(remaining.height, remaining.y, function(pos, size)
            return { x = remaining.x, y = pos, width = remaining.width, height = size }
        end)
    end
end

-- Port of dwindleLayout.js (alternating V/H splits + graceful degradation)
local function dwindleLayout(area, count, splitRatio, innerGap, minSizes)
    if count <= 0 then return {} end
    splitRatio = math.min(math.max(splitRatio, MIN_SPLIT), MAX_SPLIT)
    local cumMinDims = computeCumulativeMinDims(count, minSizes, innerGap)
    local remainingMinW = cumMinDims.minW
    local remainingMinH = cumMinDims.minH
    local remaining = { x = area.x, y = area.y, width = area.width, height = area.height }
    local splitVertical = true
    local zones = {}

    local function trySplit(axis, i)
        local isWidth = (axis == "w")
        local totalDim = isWidth and remaining.width or remaining.height
        local contentDim = totalDim - innerGap
        if contentDim < 2 then return nil end
        local windowDim = math.floor(contentDim * splitRatio)
        local minDimArr = isWidth and remainingMinW or remainingMinH
        local minProp = isWidth and "w" or "h"
        local entry = minSizes and minSizes[i + 1]
        if minSizes and #minSizes > 0 and i < #minSizes and entry and entry[minProp] and entry[minProp] > 0 then
            windowDim = math.max(windowDim, entry[minProp])
        end
        if minSizes and #minSizes > 0 and minDimArr and minDimArr[i + 1] and minDimArr[i + 1] > 0 then
            windowDim = math.min(windowDim, contentDim - minDimArr[i + 1])
        end
        windowDim = math.max(1, math.min(windowDim, contentDim - 1))
        local zone, newRemaining
        if isWidth then
            zone = { x = remaining.x, y = remaining.y, width = windowDim, height = remaining.height }
            newRemaining = { x = remaining.x + windowDim + innerGap, y = remaining.y,
                width = contentDim - windowDim, height = remaining.height }
        else
            zone = { x = remaining.x, y = remaining.y, width = remaining.width, height = windowDim }
            newRemaining = { x = remaining.x, y = remaining.y + windowDim + innerGap,
                width = remaining.width, height = contentDim - windowDim }
        end
        return { zone = zone, newRemaining = newRemaining }
    end

    for i = 0, count - 1 do
        if i == count - 1 or remaining.width < MIN_ZONE_SIZE or remaining.height < MIN_ZONE_SIZE
            or (splitVertical and remaining.width <= innerGap)
            or ((not splitVertical) and remaining.height <= innerGap) then
            zones[#zones + 1] = { x = remaining.x, y = remaining.y, width = remaining.width, height = remaining.height }
            appendGracefulDegradation(zones, remaining, count - i - 1, innerGap)
            break
        else
            local result = trySplit(splitVertical and "w" or "h", i)
            if not result then
                zones[#zones + 1] = { x = remaining.x, y = remaining.y,
                    width = remaining.width, height = remaining.height }
                appendGracefulDegradation(zones, remaining, count - i - 1, innerGap)
                break
            end
            zones[#zones + 1] = result.zone
            remaining = result.newRemaining
            splitVertical = not splitVertical
        end
    end
    return zones
end

-- Port of solveThreeColumn.js
local function solveThreeColumn(areaX, contentWidth, innerGap, splitRatio, minL, minC, minR)
    if contentWidth < 3 then
        local cw = math.max(1, contentWidth - 2)
        return { leftWidth = 1, centerWidth = cw, rightWidth = 1,
            leftX = areaX, centerX = areaX + 1, rightX = areaX + 1 + cw }
    end
    local effMinLeft = math.max(MIN_ZONE_SIZE, minL)
    local effMinRight = math.max(MIN_ZONE_SIZE, minR)
    local maxCenter = math.min(MAX_SPLIT, 1.0 - (effMinLeft + effMinRight) / contentWidth)
    local centerRatio = math.min(math.max(splitRatio, MIN_SPLIT), math.max(MIN_SPLIT, maxCenter))
    if minC > 0 then
        local minCenterRatio = minC / contentWidth
        centerRatio = math.max(centerRatio, math.min(minCenterRatio, maxCenter))
    end
    local sideRatio = (1.0 - centerRatio) / 2.0
    local leftWidth = math.floor(contentWidth * sideRatio)
    local centerWidth = math.floor(contentWidth * centerRatio)
    local rightWidth = contentWidth - leftWidth - centerWidth
    local totalColumnMin = math.max(minL, 0) + math.max(minC, 0) + math.max(minR, 0)
    if totalColumnMin > contentWidth and totalColumnMin > 0 then
        local eL = math.max(minL, 1)
        local eC = math.max(minC, 1)
        local eR = math.max(minR, 1)
        local eT = eL + eC + eR
        leftWidth = math.floor(contentWidth * eL / eT)
        centerWidth = math.floor(contentWidth * eC / eT)
        rightWidth = contentWidth - leftWidth - centerWidth
    else
        if minL > 0 and leftWidth < minL then
            local deficit = minL - leftWidth
            leftWidth = minL
            local fromCenter = math.max(0, math.min(deficit, centerWidth - math.max(minC, 1)))
            centerWidth = centerWidth - fromCenter
            rightWidth = contentWidth - leftWidth - centerWidth
        end
        if minR > 0 and rightWidth < minR then
            local deficit = minR - rightWidth
            rightWidth = minR
            local fromCenter = math.max(0, math.min(deficit, centerWidth - math.max(minC, 1)))
            centerWidth = centerWidth - fromCenter
            leftWidth = contentWidth - rightWidth - centerWidth
            leftWidth = math.max(1, leftWidth)
        end
        if minC > 0 and centerWidth < minC then
            local deficit = minC - centerWidth
            centerWidth = minC
            if leftWidth >= rightWidth then
                local take = math.min(deficit, leftWidth - 1)
                leftWidth = leftWidth - take
                deficit = deficit - take
                if deficit > 0 then rightWidth = math.max(1, rightWidth - deficit) end
            else
                local take = math.min(deficit, rightWidth - 1)
                rightWidth = rightWidth - take
                deficit = deficit - take
                if deficit > 0 then leftWidth = math.max(1, leftWidth - deficit) end
            end
        end
        leftWidth = math.max(1, leftWidth)
        centerWidth = math.max(1, centerWidth)
        rightWidth = math.max(1, rightWidth)
        if (minL > 0 and leftWidth < minL) or (minR > 0 and rightWidth < minR) or (minC > 0 and centerWidth < minC) then
            local totalMin = minL + minR + minC
            if totalMin > 0 then
                leftWidth = math.max(1, math.floor(contentWidth * minL / totalMin))
                rightWidth = math.max(1, math.floor(contentWidth * minR / totalMin))
                centerWidth = math.max(1, contentWidth - leftWidth - rightWidth)
            else
                leftWidth = math.max(1, math.floor(contentWidth / 3))
                rightWidth = math.max(1, math.floor(contentWidth / 3))
                centerWidth = math.max(1, contentWidth - leftWidth - rightWidth)
            end
        end
    end
    leftWidth = math.max(1, leftWidth)
    centerWidth = math.max(1, centerWidth)
    rightWidth = math.max(1, rightWidth)
    local colSum = leftWidth + centerWidth + rightWidth
    while colSum > contentWidth do
        local excess = colSum - contentWidth
        if excess <= 0 then break end
        if centerWidth >= leftWidth and centerWidth >= rightWidth and centerWidth > 1 then
            centerWidth = centerWidth - math.min(excess, centerWidth - 1)
        elseif leftWidth >= rightWidth and leftWidth > 1 then
            leftWidth = leftWidth - math.min(excess, leftWidth - 1)
        elseif rightWidth > 1 then
            rightWidth = rightWidth - math.min(excess, rightWidth - 1)
        else
            break
        end
        colSum = leftWidth + centerWidth + rightWidth
    end
    return { leftWidth = leftWidth, centerWidth = centerWidth, rightWidth = rightWidth,
        leftX = areaX, centerX = areaX + leftWidth + innerGap,
        rightX = areaX + leftWidth + innerGap + centerWidth + innerGap }
end

-- Ports of interleaveStacks.js (stackIsLeft is 1-indexed; JS [i] ↔ Lua [i+1])
local function buildStackIsLeft(stackCount, leftCount, rightCount)
    local stackIsLeft = {}
    local li, ri = 0, 0
    for i = 0, stackCount - 1 do
        if i % 2 == 0 and li < leftCount then
            stackIsLeft[#stackIsLeft + 1] = true
            li = li + 1
        elseif ri < rightCount then
            stackIsLeft[#stackIsLeft + 1] = false
            ri = ri + 1
        elseif li < leftCount then
            stackIsLeft[#stackIsLeft + 1] = true
            li = li + 1
        else
            -- Faithful to interleaveStacks.js, which had a fourth
            -- `else if (ri < rightCount)` branch here. It is unreachable —
            -- the second branch above already established ri >= rightCount —
            -- so it is dropped; the fallback assigns left, exactly as before.
            stackIsLeft[#stackIsLeft + 1] = true
        end
    end
    return stackIsLeft
end

local function interleaveMinWidths(minSizes, stackIsLeft, stackCount, masterOffset)
    local minLeftWidth, minRightWidth = 0, 0
    for i = 0, stackCount - 1 do
        local zoneIdx = masterOffset + i
        if zoneIdx < #minSizes then
            local entry = minSizes[zoneIdx + 1]
            -- Guard `entry` for a sparse minSizes (mirrors interleaveMinHeights);
            -- the production host always marshals dense entries, so this is
            -- defensive only.
            local mw = (entry and entry.w and entry.w > 0) and entry.w or 0
            if stackIsLeft[i + 1] then
                if mw > minLeftWidth then minLeftWidth = mw end
            else
                if mw > minRightWidth then minRightWidth = mw end
            end
        end
    end
    return { minLeftWidth = minLeftWidth, minRightWidth = minRightWidth }
end

local function interleaveMinHeights(minSizes, stackIsLeft, stackCount, leftCount, rightCount, masterOffset)
    local leftMinH, rightMinH = {}, {}
    for i = 1, leftCount do leftMinH[i] = 0 end
    for i = 1, rightCount do rightMinH[i] = 0 end
    local li, ri = 0, 0
    for i = 0, stackCount - 1 do
        local zoneIdx = masterOffset + i
        local entry = minSizes[zoneIdx + 1]
        local mh = (zoneIdx < #minSizes and entry and entry.h and entry.h > 0) and entry.h or 0
        if stackIsLeft[i + 1] and li < leftCount then
            leftMinH[li + 1] = mh
            li = li + 1
        elseif (not stackIsLeft[i + 1]) and ri < rightCount then
            rightMinH[ri + 1] = mh
            ri = ri + 1
        end
    end
    return { leftMinH = leftMinH, rightMinH = rightMinH }
end

local function assignInterleavedStacks(zones, stackIsLeft, stackCount, leftX, rightX, leftWidth, rightWidth,
                                       leftHeights, rightHeights, areaY, gap)
    local leftIdx, rightIdx = 0, 0
    local leftY, rightY = areaY, areaY
    for i = 0, stackCount - 1 do
        if stackIsLeft[i + 1] and leftIdx < #leftHeights then
            zones[#zones + 1] = { x = leftX, y = leftY, width = leftWidth, height = leftHeights[leftIdx + 1] }
            leftY = leftY + leftHeights[leftIdx + 1] + gap
            leftIdx = leftIdx + 1
        elseif (not stackIsLeft[i + 1]) and rightIdx < #rightHeights then
            zones[#zones + 1] = { x = rightX, y = rightY, width = rightWidth, height = rightHeights[rightIdx + 1] }
            rightY = rightY + rightHeights[rightIdx + 1] + gap
            rightIdx = rightIdx + 1
        else
            if leftIdx < #leftHeights then
                zones[#zones + 1] = { x = leftX, y = leftY, width = leftWidth, height = leftHeights[leftIdx + 1] }
                leftY = leftY + leftHeights[leftIdx + 1] + gap
                leftIdx = leftIdx + 1
            elseif rightIdx < #rightHeights then
                zones[#zones + 1] = { x = rightX, y = rightY, width = rightWidth, height = rightHeights[rightIdx + 1] }
                rightY = rightY + rightHeights[rightIdx + 1] + gap
                rightIdx = rightIdx + 1
            else
                zones[#zones + 1] = { x = leftX, y = leftY, width = leftWidth, height = 1 }
            end
        end
    end
end

-- Port of threeColumnLayout.js
local function threeColumnLayout(area, count, gap, splitRatio, masterCount, minSizes)
    local stackCount = count - masterCount
    local leftCount = math.ceil(stackCount / 2)
    local rightCount = stackCount - leftCount

    local contentWidth = area.width - 2 * gap
    if contentWidth < 3 * MIN_ZONE_SIZE then
        return fillArea(area, count)
    end

    local stackIsLeft = buildStackIsLeft(stackCount, leftCount, rightCount)

    local minCenterWidth = extractRegionMaxMin(minSizes, 0, masterCount, "w")
    local sideMinW
    if #minSizes > 0 then
        sideMinW = interleaveMinWidths(minSizes, stackIsLeft, stackCount, masterCount)
    else
        sideMinW = { minLeftWidth = 0, minRightWidth = 0 }
    end

    local cols = solveThreeColumn(area.x, contentWidth, gap, splitRatio,
        sideMinW.minLeftWidth, minCenterWidth, sideMinW.minRightWidth)

    local masterMinH = extractMinHeights(minSizes, masterCount)
    local sideMinH
    if #minSizes > 0 then
        sideMinH = interleaveMinHeights(minSizes, stackIsLeft, stackCount, leftCount, rightCount, masterCount)
    else
        sideMinH = { leftMinH = {}, rightMinH = {} }
    end

    local masterHeights = distributeWithOptionalMins(area.height, masterCount, gap, masterMinH)
    local leftHeights = (leftCount > 0)
        and distributeWithOptionalMins(area.height, leftCount, gap, sideMinH.leftMinH) or {}
    local rightHeights = (rightCount > 0)
        and distributeWithOptionalMins(area.height, rightCount, gap, sideMinH.rightMinH) or {}

    local zones = {}
    local currentY = area.y
    for i = 1, masterCount do
        zones[#zones + 1] = { x = cols.centerX, y = currentY, width = cols.centerWidth, height = masterHeights[i] }
        currentY = currentY + masterHeights[i] + gap
    end

    assignInterleavedStacks(zones, stackIsLeft, stackCount, cols.leftX, cols.rightX,
        cols.leftWidth, cols.rightWidth, leftHeights, rightHeights, area.y, gap)

    return zones
end

-- Port of applyTreeGeometry.js. NOTE: reads the host's marshalled tree keys
-- (splitRatio/splitHorizontal/first/second/windowId), not the prior engine's
-- (ratio/horizontal) — the geometry is identical because the underlying tree is.
local function applyTreeGeometry(node, rect, gap, depth)
    depth = depth or 0
    if depth > MAX_TREE_DEPTH then return {} end
    if not node then return {} end
    if node.windowId and node.windowId ~= "" then
        return { { x = rect.x, y = rect.y, width = math.max(1, rect.width), height = math.max(1, rect.height) } }
    end
    if not node.first or not node.second then
        return { { x = rect.x, y = rect.y, width = math.max(1, rect.width), height = math.max(1, rect.height) } }
    end
    local ratio = math.max(MIN_SPLIT, math.min(MAX_SPLIT, node.splitRatio or 0.5))
    local zones = {}
    local function append(t)
        for i = 1, #t do zones[#zones + 1] = t[i] end
    end
    if node.splitHorizontal then
        local content = rect.height - gap
        if content <= 0 then
            append(applyTreeGeometry(node.first, rect, 0, depth + 1))
            append(applyTreeGeometry(node.second, rect, 0, depth + 1))
        else
            local h1 = math.floor(content * ratio)
            local h2 = content - h1
            append(applyTreeGeometry(node.first,
                { x = rect.x, y = rect.y, width = rect.width, height = h1 }, gap, depth + 1))
            append(applyTreeGeometry(node.second,
                { x = rect.x, y = rect.y + h1 + gap, width = rect.width, height = h2 }, gap, depth + 1))
        end
    else
        local content = rect.width - gap
        if content <= 0 then
            append(applyTreeGeometry(node.first, rect, 0, depth + 1))
            append(applyTreeGeometry(node.second, rect, 0, depth + 1))
        else
            local w1 = math.floor(content * ratio)
            local w2 = content - w1
            append(applyTreeGeometry(node.first,
                { x = rect.x, y = rect.y, width = w1, height = rect.height }, gap, depth + 1))
            append(applyTreeGeometry(node.second,
                { x = rect.x + w1 + gap, y = rect.y, width = w2, height = rect.height }, gap, depth + 1))
        end
    end
    return zones
end

-- ── Shared algorithm helpers ───────────────────────────────────────────────
-- Extracted from the bundled algorithms so many scripts share one copy. Each is
-- behaviour-preserving: identical output to the inline code it replaces.

-- Standard tile() preamble: no windows → empty; a work area below the minimum
-- tile size → the host's fill fallback. Returns the early-out zone list, or nil
-- when the caller should proceed. Call as:
--   local early = pluau.guardArea(ctx.area, count); if early then return early end
local function guardArea(area, count)
    if count <= 0 then return {} end
    if area.width < MIN_ZONE_SIZE or area.height < MIN_ZONE_SIZE then
        return fillArea(area, count)
    end
    return nil
end

-- NaN-safe clamp. Returns nil when v is not a real number (callers pair it with
-- `or default`), otherwise v bounded to [lo, hi].
local function clamp(v, lo, hi)
    if type(v) ~= "number" or v ~= v then
        return nil
    end
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

-- Centred start for a `size`-long span inside a `total`-long region beginning at
-- `start` (integer floor, matching the inline `start + floor((total - size) / 2)`).
local function center(start, total, size)
    return start + math.floor((total - size) / 2)
end

-- Square-ish grid shape for `count` cells: cols = ceil(sqrt), rows = ceil(count/cols).
local function gridShape(count)
    local cols = math.ceil(math.sqrt(count))
    local rows = math.ceil(count / cols)
    return cols, rows
end

-- Running start offsets for a run of `sizes` beginning at `start`, each separated
-- by `gap`. offsets[1] = start; offsets[i + 1] = offsets[i] + sizes[i] + gap.
-- Length equals #sizes for non-empty input; an empty `sizes` still yields
-- { start } (both consumers pass at least one size after guardArea).
local function cumulativeOffsets(start, sizes, gap)
    local offsets = { start }
    for i = 1, #sizes - 1 do
        offsets[i + 1] = offsets[i] + sizes[i] + gap
    end
    return offsets
end

-- Per-window minimum (w, h) at 0-based index i; 0 for either when absent or
-- non-positive. Mirrors the inline single-index reads in grid/spread/spiral.
local function minSizeAt(minSizes, i)
    local entry = minSizes and minSizes[i + 1]
    local hasEntry = minSizes and i < #minSizes and entry
    local w = (hasEntry and entry.w and entry.w > 0) and entry.w or 0
    local h = (hasEntry and entry.h and entry.h > 0) and entry.h or 0
    return w, h
end

-- Ratio reconstruction for the onWindowResized closed forms. A window whose size
-- IS the ratio (grow edge): newRatio = new * oldRatio / old. A window that is the
-- COMPLEMENT of the ratio (shrink edge): newRatio = 1 - new * (1 - oldRatio) / old.
local function resizeRatioGrow(newSize, oldSize, oldRatio)
    return newSize * oldRatio / oldSize
end

local function resizeRatioShrink(newSize, oldSize, oldRatio)
    return 1 - newSize * (1 - oldRatio) / oldSize
end

-- Shared onWindowResized glue for master/stack layouts. The master/stack seam
-- maps to the split ratio: dragging a master window's trailing edge grows the
-- ratio, a stack window's leading edge shrinks it. `horizontal` selects the axis
-- and the seam edges — false is the vertical seam (width axis; right grows, left
-- shrinks, as in master-stack), true is the horizontal seam (height axis; bottom
-- grows, top shrinks, as in wide). Returns a { splitRatio = ... } control table
-- for the engine to apply locally, or nil when there is no seam or the drag is
-- not on the boundary edge. The gap cancels in the fraction, so the new ratio is
-- reconstructed self-referentially from the old size.
local function masterStackResize(state, resize, horizontal)
    local count = state.windowCount
    local masterCount = state.masterCount or 1
    if count <= masterCount then return nil end -- no stack → no seam
    local oldRatio = state.splitRatio
    local old, new, edges, idx = resize.oldRect, resize.newRect, resize.edges, resize.index
    local oldDim, newDim, growEdge, shrinkEdge
    if horizontal then
        oldDim, newDim = old.height, new.height
        growEdge, shrinkEdge = edges.bottom, edges.top
    else
        oldDim, newDim = old.width, new.width
        growEdge, shrinkEdge = edges.right, edges.left
    end
    if oldDim <= 0 or oldRatio <= 0 or oldRatio >= 1 then return nil end
    if idx < masterCount then
        if growEdge then
            return { splitRatio = resizeRatioGrow(newDim, oldDim, oldRatio) }
        end
    else
        if shrinkEdge then
            return { splitRatio = resizeRatioShrink(newDim, oldDim, oldRatio) }
        end
    end
    return nil
end

-- General horizontal/vertical strip of `count` cells filling a panel, appended to
-- `zones`. When the gaps alone exceed the strip length it degrades to equal,
-- overlapping fills; otherwise cells are distributed evenly (gap-aware).
local function stripLayout(zones, startX, startY, panelW, panelH, count, gap, horizontal)
    if count <= 0 then return end
    local totalSize = horizontal and panelW or panelH

    -- Degenerate gap: equal-split without gaps
    if (count - 1) * gap >= totalSize then
        local equalSize = math.max(1, math.floor(totalSize / count))
        local fill
        if horizontal then
            fill = fillRegion(startX, startY, equalSize, panelH, count)
        else
            fill = fillRegion(startX, startY, panelW, equalSize, count)
        end
        for i = 1, #fill do zones[#zones + 1] = fill[i] end
        return
    end

    local slots = distributeEvenly(horizontal and startX or startY, totalSize, count, gap)
    for i = 1, #slots do
        if horizontal then
            zones[#zones + 1] = { x = slots[i].pos, y = startY, width = slots[i].size, height = panelH }
        else
            zones[#zones + 1] = { x = startX, y = slots[i].pos, width = panelW, height = slots[i].size }
        end
    end
end

-- ── Module wrapper ─────────────────────────────────────────────────────────
local function algorithm(def)
    local userTile = def.tile
    def.tile = function(ctx)
        local a = ctx.area
        if a then
            ctx.area = rect(a.x, a.y, a.width, a.height)
        end
        return userTile(ctx)
    end
    return def
end

pluau = {
    -- constants
    MIN_ZONE_SIZE = MIN_ZONE_SIZE,
    MIN_SPLIT = MIN_SPLIT,
    MAX_SPLIT = MAX_SPLIT,
    MAX_TREE_DEPTH = MAX_TREE_DEPTH,
    -- high-level API
    rect = rect,
    join = join,
    algorithm = algorithm,
    -- builtin ports
    distributeWithGaps = distributeWithGaps,
    distributeWithMinSizes = distributeWithMinSizes,
    distributeWithOptionalMins = distributeWithOptionalMins,
    extractMinWidths = extractMinWidths,
    extractMinHeights = extractMinHeights,
    fillRegion = fillRegion,
    fillArea = fillArea,
    equalColumnsLayout = equalColumnsLayout,
    clampSplitRatio = clampSplitRatio,
    extractRegionMaxMin = extractRegionMaxMin,
    solveTwoPart = solveTwoPart,
    masterStackLayout = masterStackLayout,
    deckLayout = deckLayout,
    applyPerWindowMinSize = applyPerWindowMinSize,
    distributeEvenly = distributeEvenly,
    lShapeLayout = lShapeLayout,
    computeCumulativeMinDims = computeCumulativeMinDims,
    appendGracefulDegradation = appendGracefulDegradation,
    dwindleLayout = dwindleLayout,
    solveThreeColumn = solveThreeColumn,
    threeColumnLayout = threeColumnLayout,
    applyTreeGeometry = applyTreeGeometry,
    -- shared algorithm helpers
    guardArea = guardArea,
    clamp = clamp,
    center = center,
    gridShape = gridShape,
    cumulativeOffsets = cumulativeOffsets,
    minSizeAt = minSizeAt,
    resizeRatioGrow = resizeRatioGrow,
    resizeRatioShrink = resizeRatioShrink,
    masterStackResize = masterStackResize,
    stripLayout = stripLayout,
}
