-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later
--
-- The `pz` tiling standard library. Injected as a global by LuauTileAlgorithm
-- before the VM is sandboxed, then frozen.
--
-- Two layers:
--   1. A high-level Rect API (split*/columns/rows/join) + pz.algorithm{} wrapper
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

pz = {
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
}
