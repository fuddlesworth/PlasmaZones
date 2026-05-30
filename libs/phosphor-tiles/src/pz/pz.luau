-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later
--
-- The `pz` tiling standard library. Injected as a global by LuauTileAlgorithm
-- before the VM is sandboxed, then frozen. Tiling algorithms consume it as:
--
--     local pz = pz
--     return pz.algorithm {
--         metadata = { id = "columns", name = "Columns" },
--         tile = function(ctx) return ctx.area:columns(ctx.count, ctx.gap) end,
--     }
--
-- `pz.algorithm` wraps the author's `tile` so that `ctx.area` arrives as a Rect
-- with gap-aware split/grid methods (the host marshals it as a plain table).

local Rect = {}
Rect.__index = Rect

local function rect(x, y, w, h)
    return setmetatable({ x = x, y = y, width = w, height = h }, Rect)
end

-- Each split returns (named-side, remainder) with `gap` between them.
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

-- Equal columns/rows; the last cell absorbs rounding so zones tile the area exactly.
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

-- Wrap an algorithm module: convert ctx.area (a plain host table) into a Rect
-- before the author's tile() runs, leaving metadata + override functions intact.
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
    rect = rect,
    join = join,
    algorithm = algorithm,
}
