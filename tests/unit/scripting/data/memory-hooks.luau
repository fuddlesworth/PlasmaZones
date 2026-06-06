-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later
--
-- Exercises BOTH the split-tree marshalling and the lifecycle hooks, with
-- observable effects so a test can prove they ran:
--   * when ctx.tree is present, tile() returns leafCount zones (proves the tree
--     marshalled across with the right leaf count)
--   * otherwise tile() returns `added` zones, where `added` is a module-local
--     counter mutated by the hooks (proves the hooks executed and persisted)

local pluau = pluau

local added = 0

return pluau.algorithm {
    metadata = {
        id = "memory-hooks",
        name = "Memory Hooks",
        supportsMemory = true,
    },

    tile = function(ctx)
        if ctx.tree then
            return ctx.area:columns(ctx.tree.leafCount, ctx.gap)
        end
        return ctx.area:columns(math.max(1, added), ctx.gap)
    end,

    onWindowAdded = function(state, index)
        added = added + 1
    end,

    onWindowRemoved = function(state, index)
        added = added - 1
    end,
}
