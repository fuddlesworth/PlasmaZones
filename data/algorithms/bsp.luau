-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

-- ─── Node helpers ────────────────────────────────────────────────────────────
local function makeNode()
    return {
        first = nil,
        second = nil,
        geometry = { x = 0, y = 0, width = 0, height = 0 },
        cachedArea = 0,
        splitHorizontal = false,
        splitRatio = 0.5,
    }
end

local function isLeaf(node)
    return not node.first and not node.second
end

local function clampOrProportionalFallback(ratio, minFirstRatio, maxFirstRatio, firstDim, secondDim)
    if minFirstRatio <= maxFirstRatio then
        return math.max(minFirstRatio, math.min(maxFirstRatio, ratio))
    end
    local totalMin = firstDim + secondDim
    if totalMin > 0 then
        ratio = firstDim / totalMin
        return pluau.clampSplitRatio(ratio)
    end
    return ratio
end

local function countLeavesInSubtree(node, depth)
    if not node or depth > pluau.MAX_TREE_DEPTH then return 0 end
    if isLeaf(node) then return 1 end
    return countLeavesInSubtree(node.first, depth + 1) + countLeavesInSubtree(node.second, depth + 1)
end

local function computeSubtreeMinDims(node, minSizes, leafStartIdx, innerGap, depth)
    depth = depth or 0
    if not node or depth > pluau.MAX_TREE_DEPTH then return { w = 0, h = 0, leafCount = 0 } end
    if isLeaf(node) then
        if leafStartIdx < #minSizes then
            local ms = minSizes[leafStartIdx + 1]
            return { w = math.max(ms.w or 0, 0), h = math.max(ms.h or 0, 0), leafCount = 1 }
        end
        return { w = 0, h = 0, leafCount = 1 }
    end
    local firstResult = computeSubtreeMinDims(node.first, minSizes, leafStartIdx, innerGap, depth + 1)
    local secondResult =
        computeSubtreeMinDims(node.second, minSizes, leafStartIdx + firstResult.leafCount, innerGap, depth + 1)
    local totalLeaves = firstResult.leafCount + secondResult.leafCount
    if node.splitHorizontal then
        return { w = math.max(firstResult.w, secondResult.w),
            h = firstResult.h + innerGap + secondResult.h, leafCount = totalLeaves }
    else
        return { w = firstResult.w + innerGap + secondResult.w,
            h = math.max(firstResult.h, secondResult.h), leafCount = totalLeaves }
    end
end

local function findLargestLeaf(node, depth)
    if not node or depth > pluau.MAX_TREE_DEPTH then return nil end
    if isLeaf(node) then return node end
    local left = findLargestLeaf(node.first, depth + 1)
    local right = findLargestLeaf(node.second, depth + 1)
    if not left then return right end
    if not right then return left end
    local leftArea = left.cachedArea
    local rightArea = right.cachedArea
    if leftArea == 0 and rightArea == 0 then return right end
    return (leftArea >= rightArea) and left or right
end

local function chooseSplitDirection(geometry)
    return geometry.height > geometry.width
end

local function zeroLeaves(node)
    if not node then return end
    if not node.first and not node.second then
        node.geometry = { x = 0, y = 0, width = 0, height = 0 }
        node.cachedArea = 0
        return
    end
    if node.first then zeroLeaves(node.first) end
    if node.second then zeroLeaves(node.second) end
end

local function bspApplyGeometry(node, rect, innerGap, minSizes, leafStartIdx, depth)
    if not node or depth > pluau.MAX_TREE_DEPTH then return end

    node.geometry = { x = rect.x, y = rect.y, width = rect.width, height = rect.height }
    node.cachedArea = (rect.width > 0 and rect.height > 0) and (rect.width * rect.height) or 0

    if isLeaf(node) then return end

    local ratio = pluau.clampSplitRatio(node.splitRatio)

    if #minSizes > 0 then
        local firstResult = computeSubtreeMinDims(node.first, minSizes, leafStartIdx, innerGap)
        local secondResult =
            computeSubtreeMinDims(node.second, minSizes, leafStartIdx + firstResult.leafCount, innerGap)

        if node.splitHorizontal then
            local contentHeight = rect.height - innerGap
            if contentHeight > 0 and (firstResult.h > 0 or secondResult.h > 0) then
                local minFirstRatio = (firstResult.h > 0) and (firstResult.h / contentHeight) or pluau.MIN_SPLIT
                local maxFirstRatio = (secondResult.h > 0) and (1.0 - secondResult.h / contentHeight) or pluau.MAX_SPLIT
                minFirstRatio = pluau.clampSplitRatio(minFirstRatio)
                maxFirstRatio = pluau.clampSplitRatio(maxFirstRatio)
                ratio = clampOrProportionalFallback(ratio, minFirstRatio, maxFirstRatio, firstResult.h, secondResult.h)
            end
        else
            local contentWidth = rect.width - innerGap
            if contentWidth > 0 and (firstResult.w > 0 or secondResult.w > 0) then
                local minFirstRatio = (firstResult.w > 0) and (firstResult.w / contentWidth) or pluau.MIN_SPLIT
                local maxFirstRatio = (secondResult.w > 0) and (1.0 - secondResult.w / contentWidth) or pluau.MAX_SPLIT
                minFirstRatio = pluau.clampSplitRatio(minFirstRatio)
                maxFirstRatio = pluau.clampSplitRatio(maxFirstRatio)
                ratio = clampOrProportionalFallback(ratio, minFirstRatio, maxFirstRatio, firstResult.w, secondResult.w)
            end
        end
    end

    local firstChildLeaves = countLeavesInSubtree(node.first, 0)

    if node.splitHorizontal then
        local contentHeight = rect.height - innerGap
        if contentHeight <= 0 then return end
        local firstHeight = math.floor(contentHeight * ratio)
        local secondHeight = contentHeight - firstHeight
        local firstRect = { x = rect.x, y = rect.y, width = rect.width, height = firstHeight }
        local secondRect = { x = rect.x, y = rect.y + firstHeight + innerGap, width = rect.width, height = secondHeight }
        if firstRect.width <= 0 or firstRect.height <= 0 or secondRect.width <= 0 or secondRect.height <= 0 then
            zeroLeaves(node.first)
            zeroLeaves(node.second)
            return
        end
        bspApplyGeometry(node.first, firstRect, innerGap, minSizes, leafStartIdx, depth + 1)
        bspApplyGeometry(node.second, secondRect, innerGap, minSizes, leafStartIdx + firstChildLeaves, depth + 1)
    else
        local contentWidth = rect.width - innerGap
        if contentWidth <= 0 then return end
        local firstWidth = math.floor(contentWidth * ratio)
        local secondWidth = contentWidth - firstWidth
        local firstRect = { x = rect.x, y = rect.y, width = firstWidth, height = rect.height }
        local secondRect = { x = rect.x + firstWidth + innerGap, y = rect.y, width = secondWidth, height = rect.height }
        if firstRect.width <= 0 or firstRect.height <= 0 or secondRect.width <= 0 or secondRect.height <= 0 then
            zeroLeaves(node.first)
            zeroLeaves(node.second)
            return
        end
        bspApplyGeometry(node.first, firstRect, innerGap, minSizes, leafStartIdx, depth + 1)
        bspApplyGeometry(node.second, secondRect, innerGap, minSizes, leafStartIdx + firstChildLeaves, depth + 1)
    end
end

local function growTree(root, defaultRatio)
    if not root then return false end
    local leaf = findLargestLeaf(root, 0)
    if not leaf or not isLeaf(leaf) then return false end
    leaf.first = makeNode()
    leaf.second = makeNode()
    if leaf.geometry.width > 0 and leaf.geometry.height > 0 then
        leaf.splitHorizontal = chooseSplitDirection(leaf.geometry)
    else
        leaf.splitHorizontal = false
    end
    leaf.splitRatio = defaultRatio
    return true
end

local function buildTree(windowCount, defaultRatio, refRect)
    if windowCount <= 0 then return nil end
    local ratio = math.min(math.max(defaultRatio, pluau.MIN_SPLIT), pluau.MAX_SPLIT)
    local root = makeNode()
    local buildRect
    if refRect and refRect.width > 0 and refRect.height > 0 then
        buildRect = refRect
    else
        buildRect = { x = 0, y = 0, width = 1920, height = 1080 }
    end
    local leafCount = 1
    local maxIterations = 1000
    local iterations = 0
    while leafCount < windowCount and iterations < maxIterations do
        iterations = iterations + 1
        bspApplyGeometry(root, buildRect, 0, {}, 0, 0)
        local result = growTree(root, ratio)
        if not result then break end
        leafCount = leafCount + 1
    end
    return root
end

local function collectLeaves(node, zones, depth)
    if not node or depth > pluau.MAX_TREE_DEPTH then return end
    if isLeaf(node) then
        zones[#zones + 1] = { x = node.geometry.x, y = node.geometry.y,
            width = node.geometry.width, height = node.geometry.height }
    else
        collectLeaves(node.first, zones, depth + 1)
        collectLeaves(node.second, zones, depth + 1)
    end
end

return pluau.algorithm {
    metadata = {
        name = "Binary Split",
        id = "bsp",
        description = "Balanced recursive splitting into equal regions",
        producesOverlappingZones = false,
        supportsMasterCount = false,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.5,
        defaultMaxWindows = 5,
        minimumWindows = 1,
        zoneNumberDisplay = "all",
        supportsMemory = false,
    },

    tile = function(ctx)
        local count = ctx.windowCount
        local area = ctx.area
        local gap = ctx.innerGap
        local minSizes = ctx.minSizes
        local splitRatio = ctx.splitRatio

        local early = pluau.guardArea(area, count)
        if early then return early end

        local root = buildTree(count, splitRatio, area)
        bspApplyGeometry(root, area, gap, minSizes, 0, 0)

        local zones = {}
        collectLeaves(root, zones, 0)

        local filtered = {}
        for _, z in ipairs(zones) do
            if z.width > 0 and z.height > 0 then
                filtered[#filtered + 1] = z
            end
        end
        zones = filtered

        if #zones == 0 then
            return pluau.equalColumnsLayout(area, count, gap, minSizes)
        end
        -- If the tree could not yield one leaf per window (e.g. a zero-size leaf
        -- was filtered at an extreme window count), fall back to an even split
        -- of the whole area — guaranteed `count` non-overlapping zones.
        -- (appendGracefulDegradation expects a genuine leftover rect, which a
        -- whole-area BSP doesn't have, so it can't be used here.)
        if #zones < count then
            return pluau.equalColumnsLayout(area, count, gap, minSizes)
        end

        return zones
    end,
}
