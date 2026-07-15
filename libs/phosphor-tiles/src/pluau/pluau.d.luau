--!strict
-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: LGPL-2.1-or-later
--
-- Type definitions for the `pluau` tiling standard library, for luau-analyze and
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
export type WindowInfo = { appId: string, focused: boolean, windowId: string }
export type ScreenInfo = { id: string, portrait: boolean, aspectRatio: number }
-- Which edges of a window moved during an interactive resize, plus the before/
-- after frames. Delivered to the onWindowResized hook (not via Context).
export type ResizeEvent = {
    index: number, -- 0-based tiled index of the resized window (NOT Luau-1-based)
    oldRect: Zone,
    newRect: Zone,
    edges: { left: boolean, right: boolean, top: boolean, bottom: boolean },
}
-- Snapshot of the engine's tiling state, passed as the first argument to the
-- lifecycle hooks (onWindowAdded / onWindowRemoved / onWindowResized). Built
-- fresh per call; mutating it has no effect — persist via the onWindowResized
-- return value instead.
export type HookState = {
    windowCount: number,
    masterCount: number,
    splitRatio: number,
    windows: { WindowInfo },
    focusedIndex: number, -- 0-based tiled index of the focused window (-1 if none)
    scriptState: { [string]: any }?, -- prior persistent bag; only with supportsScriptState set and a non-empty bag
    countAfterRemoval: number?, -- onWindowRemoved only: windowCount minus the departing window
}
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
-- (faithfully-ported) algorithms use.
--
-- `area` is a Rect (with its split/columns/rows methods) only for an algorithm
-- declared through `pluau.algorithm{}`, which re-wraps it on the way in. The
-- host marshals a plain {x,y,width,height} table, so a module exporting `tile`
-- directly gets that instead. Every bundled algorithm uses the wrapper.
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
    focusedIndex: number, -- 0-based tiled index of the focused window (-1 if none)
    screen: ScreenInfo?,
    tree: SplitNode?, -- present for memory-aware algorithms
    custom: { [string]: any }?,
    currentGeometries: { Zone }?, -- last applied zones (advisory; post-enforcement)
    state: { [string]: any }?, -- persistent script-state bag (see supportsScriptState)
}

export type Metadata = {
    id: string?,
    name: string?,
    description: string?,
    supportsMasterCount: boolean?,
    supportsSplitRatio: boolean?,
    supportsMemory: boolean?,
    supportsScriptState: boolean?, -- persists ctx.state across retiles (non-tree memory)
    supportsMinSizes: boolean?, -- defaults to TRUE when omitted (unlike every other flag); set false to opt out
    producesOverlappingZones: boolean?,
    centerLayout: boolean?,
    supportsSingleWindow: boolean?, -- algorithm lays out the lone window itself (else the host fills the work area for a lone window)
    retileOnFocus: boolean?, -- reflow when focus moves between tiled windows (focus-driven layouts, e.g. a spotlight that follows focus)
    -- The same clamps the override functions below are annotated with apply
    -- here: metadata seeds the value the override resolves, and both land in
    -- the one clamp.
    defaultSplitRatio: number?, -- clamped to MIN_SPLIT..MAX_SPLIT
    defaultMaxWindows: number?, -- clamped to 1..100
    minimumWindows: number?, -- clamped to 1..100
    masterZoneIndex: number?, -- 0-based index of the master zone, clamped to -1..255
    zoneNumberDisplay: string?,
    customParams: { any }?,
}

export type Algorithm = {
    metadata: Metadata,
    tile: (ctx: Context) -> { Zone },
    -- index is the 0-based tiled index (NOT Luau-1-based).
    onWindowAdded: ((state: HookState, index: number) -> ())?,
    onWindowRemoved: ((state: HookState, index: number) -> ())?,
    -- Called after an interactive resize. Return a control table (or nil to do
    -- nothing). Two optional, independent outputs:
    --   * splitRatio: number — a new master/split ratio the engine applies to this
    --     state (clamped); ratio-based algorithms reflow on resize this way. The
    --     change stays local to the current screen+desktop+activity.
    --   * any other keys — the new ctx.state bag; persisted only when
    --     metadata.supportsScriptState is set (see `state` in Context). The
    --     table replaces the whole bag rather than merging into it, so return
    --     every key you want to keep, and return nil to leave it untouched.
    --     Returning only splitRatio from a supportsScriptState algorithm
    --     therefore clears the bag, since splitRatio is stripped first.
    onWindowResized: ((state: HookState, resize: ResizeEvent) -> { [string]: any }?)?,
    -- Module-level overrides: when present, each takes precedence over the
    -- metadata field of the same name, so a value can be computed instead of
    -- declared. Each is called once when the script loads and the result is
    -- cached for the algorithm's lifetime, so it cannot vary per retile. The
    -- bundled algorithms all use plain metadata fields.
    masterZoneIndex: (() -> number)?, -- clamped to -1..255
    supportsMasterCount: (() -> boolean)?,
    supportsSplitRatio: (() -> boolean)?,
    producesOverlappingZones: (() -> boolean)?,
    centerLayout: (() -> boolean)?,
    minimumWindows: (() -> number)?, -- clamped to 1..100
    defaultMaxWindows: (() -> number)?, -- clamped to 1..100
    defaultSplitRatio: (() -> number)?, -- clamped to MIN_SPLIT..MAX_SPLIT
}

export type Pluau = {
    -- constants
    MIN_ZONE_SIZE: number,
    MIN_SPLIT: number,
    MAX_SPLIT: number,
    MAX_TREE_DEPTH: number,
    -- high-level API
    rect: (x: number, y: number, w: number, h: number) -> Rect,
    join: (...{ Zone }) -> { Zone },
    algorithm: (def: Algorithm) -> Algorithm,
    -- geometry builtins (see pluau.luau for full signatures)
    fillArea: (area: Zone, count: number) -> { Zone },
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
    -- rect is Zone, not Rect: this recurses on plain {x,y,width,height} tables
    -- it builds itself, and only ever reads those four fields.
    applyTreeGeometry: (node: SplitNode, rect: Zone, gap: number, depth: number?) -> { Zone },
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
    solveThreeColumn: (areaX: number, contentWidth: number, innerGap: number, splitRatio: number, minL: number,
        minC: number, minR: number) -> { leftWidth: number, centerWidth: number, rightWidth: number,
        leftX: number, centerX: number, rightX: number },
    applyPerWindowMinSize: (w: number, h: number, minSizes: { MinSize }, i: number) -> { w: number, h: number },
    -- minW/minH are keyed 0..windowCount inclusive, not 1-based arrays.
    computeCumulativeMinDims: (windowCount: number, minSizes: { MinSize }, innerGap: number)
        -> { minW: { [number]: number }, minH: { [number]: number } },
    appendGracefulDegradation: (zones: { Zone }, remaining: Zone, leftover: number, innerGap: number) -> (),
    -- shared algorithm helpers
    guardArea: (area: Rect, count: number) -> { Zone }?,
    clamp: (v: any, lo: number, hi: number) -> number?,
    center: (start: number, total: number, size: number) -> number,
    gridShape: (count: number) -> (number, number), -- returns cols, rows
    cumulativeOffsets: (start: number, sizes: { number }, gap: number) -> { number },
    minSizeAt: (minSizes: { MinSize }?, i: number) -> (number, number), -- returns w, h
    resizeRatioGrow: (newSize: number, oldSize: number, oldRatio: number) -> number,
    resizeRatioShrink: (newSize: number, oldSize: number, oldRatio: number) -> number,
    masterStackResize: (state: HookState, resize: ResizeEvent, horizontal: boolean) -> { [string]: any }?,
    stripLayout: (zones: { Zone }, startX: number, startY: number, panelW: number, panelH: number,
        count: number, gap: number, horizontal: boolean) -> (),
}

declare pluau: Pluau

return nil
