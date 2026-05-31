--!strict
-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later
--
-- Type definitions for the `pz` tiling standard library, for luau-analyze and
-- editor autocomplete. The actual implementation is injected by the host
-- (LuauTileAlgorithm) and frozen before user scripts run.

export type Rect = {
    x: number,
    y: number,
    width: number,
    height: number,
    -- Each split returns (named-side, remainder), gap-aware.
    splitLeft: (self: Rect, ratio: number, gap: number) -> (Rect, Rect),
    splitRight: (self: Rect, ratio: number, gap: number) -> (Rect, Rect),
    splitTop: (self: Rect, ratio: number, gap: number) -> (Rect, Rect),
    splitBottom: (self: Rect, ratio: number, gap: number) -> (Rect, Rect),
    columns: (self: Rect, n: number, gap: number) -> { Rect },
    rows: (self: Rect, n: number, gap: number) -> { Rect },
}

export type Zone = { x: number, y: number, width: number, height: number }

export type MinSize = { w: number, h: number }
export type WindowInfo = { appId: string, focused: boolean }
export type ScreenInfo = { id: string, portrait: boolean, aspectRatio: number }
export type SplitNode = {
    splitRatio: number,
    splitHorizontal: boolean,
    windowId: string,
    isLeaf: boolean,
    first: SplitNode?,
    second: SplitNode?,
    leafCount: number?, -- present on the root only
}

-- The context passed to an algorithm's tile() function. `count`/`gap` are the
-- ergonomic names; `windowCount`/`innerGap` are the equivalents the bundled
-- (faithfully-ported) algorithms use. `area` arrives as a Rect with methods.
export type Context = {
    count: number,
    windowCount: number,
    gap: number,
    innerGap: number,
    area: Rect,
    masterCount: number,
    splitRatio: number,
    minSizes: { MinSize },
    windows: { WindowInfo }?,
    focusedIndex: number,
    screen: ScreenInfo?,
    tree: SplitNode?, -- present for memory-aware algorithms
    custom: { [string]: any }?,
}

export type Metadata = {
    id: string?,
    name: string?,
    description: string?,
    supportsMasterCount: boolean?,
    supportsSplitRatio: boolean?,
    supportsMemory: boolean?,
    supportsMinSizes: boolean?,
    producesOverlappingZones: boolean?,
    centerLayout: boolean?,
    defaultSplitRatio: number?,
    defaultMaxWindows: number?,
    minimumWindows: number?,
    masterZoneIndex: number?,
    zoneNumberDisplay: string?,
    customParams: { any }?,
}

export type Algorithm = {
    metadata: Metadata,
    tile: (ctx: Context) -> { Zone },
    onWindowAdded: ((state: any, index: number) -> ())?,
    onWindowRemoved: ((state: any, index: number) -> ())?,
}

export type Pz = {
    -- constants
    MIN_ZONE_SIZE: number,
    MIN_SPLIT: number,
    MAX_SPLIT: number,
    MAX_TREE_DEPTH: number,
    -- high-level API
    rect: (x: number, y: number, w: number, h: number) -> Rect,
    join: (...{ Zone }) -> { Zone },
    algorithm: (def: Algorithm) -> Algorithm,
    -- geometry builtins (see pz.luau for full signatures)
    fillArea: (area: Rect, count: number) -> { Zone },
    fillRegion: (x: number, y: number, w: number, h: number, count: number) -> { Zone },
    equalColumnsLayout: (area: Rect, count: number, gap: number, minSizes: { MinSize }) -> { Zone },
    masterStackLayout: (area: Rect, count: number, gap: number, splitRatio: number, masterCount: number,
        minSizes: { MinSize }, horizontal: boolean) -> { Zone },
    deckLayout: (area: Rect, count: number, focusedFraction: number, horizontal: boolean) -> { Zone },
    lShapeLayout: (area: Rect, count: number, gap: number, splitRatio: number, distribute: string,
        bottomWidth: any, rightHeight: any) -> { Zone },
    dwindleLayout: (area: Rect, count: number, splitRatio: number, innerGap: number, minSizes: { MinSize }) -> { Zone },
    threeColumnLayout: (area: Rect, count: number, gap: number, splitRatio: number, masterCount: number,
        minSizes: { MinSize }) -> { Zone },
    applyTreeGeometry: (node: SplitNode, rect: Rect, gap: number) -> { Zone },
    clampSplitRatio: (ratio: number) -> number,
    distributeWithGaps: (total: number, count: number, gap: number) -> { number },
    distributeWithMinSizes: (total: number, count: number, gap: number, minDims: { number }) -> { number },
    distributeWithOptionalMins: (total: number, count: number, gap: number, minDims: { number }) -> { number },
    distributeEvenly: (start: number, total: number, count: number, gap: number) -> { { pos: number, size: number } },
    extractMinWidths: (minSizes: { MinSize }, count: number, startIdx: number?) -> { number },
    extractMinHeights: (minSizes: { MinSize }, count: number, startIdx: number?) -> { number },
    extractRegionMaxMin: (minSizes: { MinSize }, startIdx: number, endIdx: number, axis: string) -> number,
    solveTwoPart: (contentDim: number, firstDim: number, secondDim: number, minFirst: number, minSecond: number)
        -> { first: number, second: number },
    applyPerWindowMinSize: (w: number, h: number, minSizes: { MinSize }, i: number) -> { w: number, h: number },
    computeCumulativeMinDims: (windowCount: number, minSizes: { MinSize }, innerGap: number)
        -> { minW: { number }, minH: { number } },
    appendGracefulDegradation: (zones: { Zone }, remaining: Rect, leftover: number, innerGap: number) -> (),
}

declare pz: Pz

return nil
