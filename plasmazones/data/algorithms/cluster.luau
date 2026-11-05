-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

-- Even-split fallback (no per-window context). Columns in landscape, rows in portrait.
local function evenSplit(area, count, gap, portrait)
    local gapSpace = (count > 1) and (count - 1) * gap or 0
    local total = portrait and area.height or area.width
    local available = total - gapSpace
    if available <= 0 then
        return pluau.fillArea(area, count)
    end
    local sliceSize = math.floor(available / count)
    local remainder = available - sliceSize * count
    local zones = {}
    local pos = portrait and area.y or area.x
    for i = 0, count - 1 do
        local s = sliceSize + ((i < remainder) and 1 or 0)
        if portrait then
            zones[#zones + 1] = { x = area.x, y = pos, width = area.width, height = s }
        else
            zones[#zones + 1] = { x = pos, y = area.y, width = s, height = area.height }
        end
        pos = pos + s + gap
    end
    return zones
end

return pluau.algorithm {
    metadata = {
        name = "Cluster",
        id = "cluster",
        description = "Groups windows by application so windows from the same app are placed adjacent",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = false,
        defaultMaxWindows = 8,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        supportsMemory = false,
        supportsMinSizes = false,
        customParams = {
            { name = "focusBoost", type = "number", default = 0.2, min = 0.0, max = 0.5,
                description = "Extra width given to the focused cluster" },
            { name = "minClusterRatio", type = "number", default = 0.1, min = 0.05, max = 0.5,
                description = "Minimum width ratio per cluster" },
        },
    },

    tile = function(ctx)
        local count = ctx.windowCount
        local area = ctx.area
        local gap = ctx.innerGap

        local early = pluau.guardArea(area, count)
        if early then return early end

        local portrait = false
        if ctx.screen then
            portrait = not not ctx.screen.portrait
        end

        local windows = ctx.windows
        if not windows or #windows == 0 then
            return evenSplit(area, count, gap, portrait)
        end

        local focusBoost = 0.2
        local minClusterRatio = 0.1
        if ctx.custom then
            if type(ctx.custom.focusBoost) == "number" then focusBoost = ctx.custom.focusBoost end
            if type(ctx.custom.minClusterRatio) == "number" then minClusterRatio = ctx.custom.minClusterRatio end
        end

        -- Build clusters from window appIds (first-seen order). indices hold the
        -- 0-based window index, so zone writes use [idx + 1].
        local clusterOrder = {}
        local clusterMap = {}
        local focusedIndex = (type(ctx.focusedIndex) == "number") and ctx.focusedIndex or -1
        local winLen = #windows

        for i = 0, count - 1 do
            local appId = "unknown"
            if i < winLen and windows[i + 1].appId then
                appId = windows[i + 1].appId
            end
            if not clusterMap[appId] then
                clusterMap[appId] = { indices = {}, hasFocus = false }
                clusterOrder[#clusterOrder + 1] = appId
            end
            local cm = clusterMap[appId]
            cm.indices[#cm.indices + 1] = i
            if i == focusedIndex then
                cm.hasFocus = true
            end
        end

        local numClusters = #clusterOrder

        local totalWeight = 0
        local weights = {}
        for ci = 1, numClusters do
            local cluster = clusterMap[clusterOrder[ci]]
            local w = #cluster.indices
            if cluster.hasFocus and focusBoost > 0 then
                w = w + focusBoost * #cluster.indices
            end
            weights[ci] = w
            totalWeight = totalWeight + w
        end

        local primaryTotal = portrait and area.height or area.width
        local secondaryTotal = portrait and area.width or area.height
        local primaryStart = portrait and area.y or area.x
        local secondaryStart = portrait and area.x or area.y

        local gapSpace = (numClusters > 1) and (numClusters - 1) * gap or 0
        local availablePrimary = primaryTotal - gapSpace
        if availablePrimary <= 0 then
            return pluau.fillArea(area, count)
        end

        local rawMinPx = math.max(pluau.MIN_ZONE_SIZE, math.floor(availablePrimary * minClusterRatio))
        local minClusterPx = math.min(rawMinPx, math.floor(availablePrimary / numClusters))
        minClusterPx = math.max(pluau.MIN_ZONE_SIZE, minClusterPx)

        local clusterSizes = {}
        local totalAssigned = 0
        for ci = 1, numClusters do
            local raw = math.floor(availablePrimary * weights[ci] / totalWeight)
            local clamped = math.max(minClusterPx, raw)
            clusterSizes[ci] = clamped
            totalAssigned = totalAssigned + clamped
        end

        if totalAssigned > availablePrimary then
            local scale = availablePrimary / totalAssigned
            totalAssigned = 0
            for ci = 1, numClusters do
                clusterSizes[ci] = math.max(pluau.MIN_ZONE_SIZE, math.floor(clusterSizes[ci] * scale))
                totalAssigned = totalAssigned + clusterSizes[ci]
            end
            if totalAssigned < availablePrimary then
                clusterSizes[numClusters] = clusterSizes[numClusters] + (availablePrimary - totalAssigned)
            end
        elseif totalAssigned < availablePrimary then
            local surplus = availablePrimary - totalAssigned
            local targetIdx = 1
            for ci = 1, numClusters do
                if clusterMap[clusterOrder[ci]].hasFocus then
                    targetIdx = ci
                    break
                end
            end
            clusterSizes[targetIdx] = clusterSizes[targetIdx] + surplus
        end

        -- Pre-fill with full-area fallback so no index is left undefined.
        local zones = {}
        for zi = 1, count do
            zones[zi] = { x = area.x, y = area.y, width = area.width, height = area.height }
        end
        local primaryPos = primaryStart

        for ci = 1, numClusters do
            local cluster = clusterMap[clusterOrder[ci]]
            local clusterPrimary = clusterSizes[ci]
            local windowsInCluster = #cluster.indices

            local innerGapSpace = (windowsInCluster > 1) and (windowsInCluster - 1) * gap or 0
            local availableSecondary = secondaryTotal - innerGapSpace
            if availableSecondary <= 0 then
                for wi = 1, windowsInCluster do
                    local idx = cluster.indices[wi]
                    if portrait then
                        zones[idx + 1] = { x = secondaryStart, y = primaryPos,
                            width = secondaryTotal, height = clusterPrimary }
                    else
                        zones[idx + 1] = { x = primaryPos, y = secondaryStart,
                            width = clusterPrimary, height = secondaryTotal }
                    end
                end
            else
                local secondaryPerWindow = math.floor(availableSecondary / windowsInCluster)
                local secondaryRemainder = availableSecondary - secondaryPerWindow * windowsInCluster
                local secondaryPos = secondaryStart

                for wi = 0, windowsInCluster - 1 do
                    local idx = cluster.indices[wi + 1]
                    local thisSecondary = secondaryPerWindow
                    if wi < secondaryRemainder then
                        thisSecondary = thisSecondary + 1
                    end

                    if portrait then
                        zones[idx + 1] = { x = secondaryPos, y = primaryPos,
                            width = thisSecondary, height = clusterPrimary }
                    else
                        zones[idx + 1] = { x = primaryPos, y = secondaryPos,
                            width = clusterPrimary, height = thisSecondary }
                    end

                    secondaryPos = secondaryPos + thisSecondary + gap
                end
            end

            primaryPos = primaryPos + clusterPrimary + gap
        end

        return zones
    end,
}
