-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        id = "master-stack",
        name = "Master + Stack",
        supportsMasterCount = true,
        supportsSplitRatio = true,
        defaultSplitRatio = 0.6,
    },

    tile = function(ctx)
        if ctx.count <= ctx.masterCount then
            return ctx.area:columns(ctx.count, ctx.gap)
        end
        local master, stack = ctx.area:splitLeft(ctx.splitRatio, ctx.gap)
        return pluau.join(
            master:rows(ctx.masterCount, ctx.gap),
            stack:rows(ctx.count - ctx.masterCount, ctx.gap)
        )
    end,
}
