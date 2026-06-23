-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

-- Aligned, resize-aware grid. Unlike the plain "grid" algorithm (equal cells,
-- stateless), this one remembers per-column widths and per-row heights as
-- normalized fractions in ctx.state, so dragging one cell's edge moves the whole
-- aligned column/row across every row/column — the thing a binary SplitTree
-- cannot express. It is the reference consumer of the scripting resize hook:
-- the engine calls onWindowResized() when a tiled window finishes an interactive
-- resize, this updates the shared fractions and returns them, and the follow-up
-- retile lays the grid out from the new fractions.

local pluau = pluau

-- Smallest fraction a column/row may shrink to, so a drag can never collapse a
-- neighbour to zero width/height.
local MIN_FRAC = 0.05

local function uniformFractions(n)
    local f = {}
    for i = 1, n do
        f[i] = 1.0 / n
    end
    return f
end

-- Validate + normalize a stored fraction array for exactly n cells. Any shape
-- mismatch, non-number, or non-positive entry falls back to a uniform split.
local function readFractions(arr, n)
    if type(arr) ~= "table" or #arr ~= n then
        return uniformFractions(n)
    end
    local sum = 0
    for i = 1, n do
        if type(arr[i]) ~= "number" or arr[i] <= 0 then
            return uniformFractions(n)
        end
        sum = sum + arr[i]
    end
    if sum <= 0 then
        return uniformFractions(n)
    end
    local f = {}
    for i = 1, n do
        f[i] = arr[i] / sum
    end
    return f
end

-- Distribute `content` pixels across n cells per fractions (which sum to 1), giving
-- the remainder to the last cell so the sizes sum exactly to content.
local function fractionsToSizes(content, fractions, n)
    local sizes = {}
    local used = 0
    for i = 1, n - 1 do
        sizes[i] = math.max(1, math.floor(content * fractions[i] + 0.5))
        used = used + sizes[i]
    end
    sizes[n] = math.max(1, content - used)
    return sizes
end

-- Move `delta` of the total (delta>0 grows `growIdx`, shrinks `shrinkIdx`),
-- clamped so neither cell drops below MIN_FRAC. The pair sum is preserved, so the
-- whole array stays normalized.
local function transfer(fr, growIdx, shrinkIdx, delta)
    if delta > 0 then
        delta = math.min(delta, fr[shrinkIdx] - MIN_FRAC)
    elseif delta < 0 then
        delta = -math.min(-delta, fr[growIdx] - MIN_FRAC)
    end
    if delta == 0 then
        return -- nothing to move, or clamped to nothing
    end
    fr[growIdx] = fr[growIdx] + delta
    fr[shrinkIdx] = fr[shrinkIdx] - delta
end

local function gridShape(count)
    local cols = math.ceil(math.sqrt(count))
    local rows = math.ceil(count / cols)
    return cols, rows
end

return pluau.algorithm {
    metadata = {
        name = "Aligned Grid",
        id = "aligned-grid",
        description = "Resize-aware grid: drag a cell edge to move the whole aligned column/row",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = false,
        defaultMaxWindows = 9,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        supportsMemory = false,
        supportsScriptState = true,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        if count <= 0 then
            return {}
        end
        local area = ctx.area
        if area.width < pluau.MIN_ZONE_SIZE or area.height < pluau.MIN_ZONE_SIZE then
            return pluau.fillArea(area, count)
        end
        local gap = ctx.innerGap
        local cols, rows = gridShape(count)

        -- Use stored fractions only if they were computed for this exact shape;
        -- a window add/remove that reshapes the grid resets to uniform.
        local st = ctx.state
        local colFr, rowFr
        if st and st.cols == cols and st.rows == rows then
            colFr = readFractions(st.colFractions, cols)
            rowFr = readFractions(st.rowFractions, rows)
        else
            colFr = uniformFractions(cols)
            rowFr = uniformFractions(rows)
        end

        local contentW = area.width - (cols - 1) * gap
        local contentH = area.height - (rows - 1) * gap
        local colW = fractionsToSizes(contentW, colFr, cols)
        local rowH = fractionsToSizes(contentH, rowFr, rows)

        local colX = { area.x }
        for c = 1, cols - 1 do
            colX[c + 1] = colX[c] + colW[c] + gap
        end
        local rowY = { area.y }
        for r = 1, rows - 1 do
            rowY[r + 1] = rowY[r] + rowH[r] + gap
        end

        local zones = {}
        for i = 0, count - 1 do
            local row = math.floor(i / cols)
            local col = i % cols
            local windowsInThisRow = (row == rows - 1) and (count - row * cols) or cols

            if windowsInThisRow == cols then
                zones[#zones + 1] = {
                    x = colX[col + 1],
                    y = rowY[row + 1],
                    width = colW[col + 1],
                    height = rowH[row + 1],
                }
            elseif col == 0 then
                -- Partial last row: span the full width, split equally. These cells
                -- don't take part in column alignment (their column drags are no-ops).
                local lastContentW = area.width - (windowsInThisRow - 1) * gap
                local lastW = fractionsToSizes(lastContentW, uniformFractions(windowsInThisRow), windowsInThisRow)
                local x = area.x
                for j = 0, windowsInThisRow - 1 do
                    zones[#zones + 1] = {
                        x = x,
                        y = rowY[row + 1],
                        width = lastW[j + 1],
                        height = rowH[row + 1],
                    }
                    x = x + lastW[j + 1] + gap
                end
                break
            end
        end

        return zones
    end,

    -- Map the moved edge to a shared column/row boundary, shift the fractions, and
    -- return the new bag. The engine retiles immediately afterwards using it.
    onWindowResized = function(state, resize)
        local count = state.windowCount
        if count <= 1 then
            return state.scriptState
        end
        local idx = resize.index
        if idx < 0 or idx >= count then
            return state.scriptState
        end

        local cols, rows = gridShape(count)
        local row = math.floor(idx / cols)
        local col = idx % cols

        local bag = state.scriptState or {}
        local colFr = (bag.cols == cols) and readFractions(bag.colFractions, cols) or uniformFractions(cols)
        local rowFr = (bag.rows == rows) and readFractions(bag.rowFractions, rows) or uniformFractions(rows)

        local windowsInLastRow = count - (rows - 1) * cols
        local inPartialRow = (row == rows - 1) and (windowsInLastRow < cols)

        local old = resize.oldRect
        local new = resize.newRect
        local edges = resize.edges

        -- Column boundary — skip partial-row windows (they aren't column-aligned).
        if not inPartialRow and old.width > 0 then
            local content = old.width / colFr[col + 1]
            if edges.right and col < cols - 1 then
                local delta = ((new.x + new.width) - (old.x + old.width)) / content
                transfer(colFr, col + 1, col + 2, delta)
            elseif edges.left and col > 0 then
                -- moving the left edge left (new.x < old.x) grows this cell
                local delta = (old.x - new.x) / content
                transfer(colFr, col + 1, col, delta)
            end
        end

        -- Row boundary — shared across the whole row, partial rows included.
        if old.height > 0 then
            local content = old.height / rowFr[row + 1]
            if edges.bottom and row < rows - 1 then
                local delta = ((new.y + new.height) - (old.y + old.height)) / content
                transfer(rowFr, row + 1, row + 2, delta)
            elseif edges.top and row > 0 then
                local delta = (old.y - new.y) / content
                transfer(rowFr, row + 1, row, delta)
            end
        end

        return { cols = cols, rows = rows, colFractions = colFr, rowFractions = rowFr }
    end,
}
