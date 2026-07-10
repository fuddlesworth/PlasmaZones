-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Grid",
        id = "grid",
        description = "Equal-sized grid layout",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = false,
        defaultMaxWindows = 9,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        local area = ctx.area
        local early = pluau.guardArea(area, count)
        if early then return early end
        local gap = ctx.innerGap
        local minSizes = ctx.minSizes

        local cols, rows = pluau.gridShape(count)

        -- 1-indexed; the original 0-based col/row indices map to [idx + 1].
        local colMinWidths = {}
        local rowMinHeights = {}
        for c = 1, cols do colMinWidths[c] = 0 end
        for r = 1, rows do rowMinHeights[r] = 0 end

        if #minSizes > 0 then
            local fullRows = (count % cols == 0) and rows or (rows - 1)
            for i = 0, count - 1 do
                local row = math.floor(i / cols)
                local col = i % cols
                local mw, mh = pluau.minSizeAt(minSizes, i)
                if row < fullRows then
                    if mw > colMinWidths[col + 1] then colMinWidths[col + 1] = mw end
                    if mh > rowMinHeights[row + 1] then rowMinHeights[row + 1] = mh end
                else
                    if mh > rowMinHeights[row + 1] then rowMinHeights[row + 1] = mh end
                end
            end
        end

        local columnWidths = pluau.distributeWithOptionalMins(area.width, cols, gap, colMinWidths)
        local rowHeights = pluau.distributeWithOptionalMins(area.height, rows, gap, rowMinHeights)

        local colX = pluau.cumulativeOffsets(area.x, columnWidths, gap)
        local rowY = pluau.cumulativeOffsets(area.y, rowHeights, gap)

        local zones = {}
        for i = 0, count - 1 do
            local row = math.floor(i / cols)
            local col = i % cols
            local windowsInThisRow = (row == rows - 1) and (count - row * cols) or cols

            if windowsInThisRow < cols and col == 0 then
                local lastRowMinWidths = {}
                if #minSizes > 0 then
                    for j = 0, windowsInThisRow - 1 do
                        local idx = row * cols + j
                        local e = minSizes[idx + 1]
                        lastRowMinWidths[#lastRowMinWidths + 1] =
                            (idx < #minSizes and e and e.w and e.w > 0) and e.w or 0
                    end
                end
                local lastRowWidths = pluau.distributeWithOptionalMins(area.width, windowsInThisRow, gap, lastRowMinWidths)
                local lastRowX = area.x
                for j = 0, windowsInThisRow - 1 do
                    zones[#zones + 1] = { x = lastRowX, y = rowY[row + 1],
                        width = lastRowWidths[j + 1], height = rowHeights[row + 1] }
                    lastRowX = lastRowX + lastRowWidths[j + 1] + gap
                end
                break
            end

            if windowsInThisRow == cols then
                zones[#zones + 1] = { x = colX[col + 1], y = rowY[row + 1],
                    width = columnWidths[col + 1], height = rowHeights[row + 1] }
            end
        end

        return zones
    end,
}
