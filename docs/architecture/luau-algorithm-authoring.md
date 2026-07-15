<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Writing custom tiling algorithms in Luau

PlasmaZones autotiling layouts are [Luau](https://luau.org/) scripts. A script
takes the current window count and screen area and returns a list of zones
(rectangles) to place windows into. The 27 built-in layouts (columns, dwindle,
master-stack, …) are written with the exact same API documented here, so the
bundled `data/algorithms/*.luau` files are the best reference once you know the
shape.

> **Why Luau?** It is a small, fast, gradually-typed Lua dialect that runs in a
> read-only sandbox — your script cannot touch the filesystem, network, or the
> rest of the daemon. See [ADR-0001](adr/0001-luau-shell-scripting-language.md).

---

## 1. Where algorithms live

| | Path |
|---|---|
| **Your algorithms** | `~/.local/share/plasmazones/algorithms/*.luau` |
| **Bundled (read-only)** | `/usr/share/plasmazones/algorithms/*.luau` |
| **`pluau` type stubs** | `/usr/share/plasmazones/pluau.d.luau` |

Each `*.luau` file in the user directory is one algorithm. The file name (minus
extension) is the fallback id; `metadata.id` overrides it. User algorithms
override a bundled one with the same id. The daemon **hot-reloads** the
directory — save the file and the algorithm list refreshes; no restart needed.
The settings app's *Algorithms* page can also create, duplicate, and edit them.
Duplicating personalizes the copy's `name` and `id` in place, which needs them
written the way §2 writes them: `metadata = {` alone on its line, and one field
per line. A copy of a script that packs the table differently is refused rather
than guessed at, and the page says so.

---

## 2. Quick start

Save this as `~/.local/share/plasmazones/algorithms/my-columns.luau`:

```lua
local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "My Columns",
        id = "my-columns",
        description = "Equal-width vertical columns",
        defaultMaxWindows = 4,
        minimumWindows = 1,
    },

    tile = function(ctx)
        if ctx.windowCount <= 0 then
            return {}
        end
        -- Split the screen area into N equal, gap-aware columns.
        return ctx.area:columns(ctx.windowCount, ctx.innerGap)
    end,
}
```

That is a complete algorithm. `pluau.algorithm{…}` wraps the table and returns it
(it adapts `ctx.area` to a `Rect` for your `tile` function); `pluau` is a pre-loaded,
**frozen** global (do not reassign it).

---

## 3. The algorithm table

`pluau.algorithm{}` takes a table with these keys:

| Key | Required | Type | Purpose |
|---|---|---|---|
| `metadata` | yes | table | Display name, capabilities, defaults (§4) |
| `tile` | yes | `(ctx) -> {Zone}` | Computes the zones (§5) |
| `onWindowAdded` | no | `(state, index) -> ()` | Lifecycle hook (§8) |
| `onWindowRemoved` | no | `(state, index) -> ()` | Lifecycle hook (§8) |
| `onWindowResized` | no | `(state, resize) -> table?` | Interactive-resize hook (§9) |

Eight of the `metadata` fields can also be supplied as a function on the
algorithm table itself, alongside `metadata` rather than inside it, when a value
must be computed instead of declared: `masterZoneIndex`, `supportsMasterCount`,
`supportsSplitRatio`, `producesOverlappingZones`, `centerLayout`,
`minimumWindows`, `defaultMaxWindows`, `defaultSplitRatio`. A function there
takes precedence over the metadata field of the same name. Each is called once
at load and cached, so it cannot vary per retile. A function written inside
`metadata` is not an override, and the field does not receive the computed
value, so put the function on the algorithm table. The bundled algorithms all
use plain metadata fields.

---

## 4. `metadata`

All fields are optional except where your UI needs them. Unset fields fall back
to sensible defaults.

| Field | Type | Meaning |
|---|---|---|
| `name` | string | Display name in the settings UI |
| `id` | string | Stable identifier (defaults to the file name) |
| `description` | string | One-line description |
| `defaultMaxWindows` | number | Default window cap shown in the UI (omit it, or set it to 0 or less, to use the built-in default of 6, otherwise clamped to 1–100) |
| `minimumWindows` | number | Smallest window count the layout supports (clamped to 1–100) |
| `supportsMasterCount` | boolean | Exposes the “master count” control, so the user can adjust `ctx.masterCount` |
| `supportsSplitRatio` | boolean | Exposes the split-ratio slider, so the user can adjust `ctx.splitRatio` |
| `defaultSplitRatio` | number | Initial split ratio (0.1–0.9) |
| `supportsMinSizes` | boolean | Honours per-window minimum sizes (default `true`) |
| `supportsMemory` | boolean | Uses the persistent split tree (§10) |
| `supportsScriptState` | boolean | Persists an opaque `ctx.state` table across retiles (§9) |
| `supportsSingleWindow` | boolean | Owns the lone-window case. Without it the host fills the work area when one window is tiled |
| `retileOnFocus` | boolean | Re-runs `tile` when focus moves between tiled windows (focus-driven layouts, e.g. a spotlight) |
| `producesOverlappingZones` | boolean | Zones may overlap (e.g. stacked/deck layouts) |
| `centerLayout` | boolean | Layout is centered rather than filling the screen |
| `masterZoneIndex` | number | Index of the “master” zone, `-1` = none (clamped to -1–255). Describes the layout for a caller that asks, and no built-in feature reads it (“focus master” goes to the first tiled window regardless) |
| `zoneNumberDisplay` | string | `"all"`, `"last"`, `"firstAndLast"`, or `"none"` (omit to let the renderer decide) |
| `customParams` | list | User-tunable parameters (§7) |

---

## 5. `tile(ctx)` — the context

`tile` receives one `Context` table and returns a list of `Zone`s. Useful
fields:

| Field | Type | Notes |
|---|---|---|
| `windowCount` / `count` | number | Windows to place (same value, two names) |
| `area` | `Rect` | Screen work area, in **absolute pixels**, with split helpers (§6) |
| `innerGap` / `gap` | number | Pixels between adjacent zones |
| `masterCount` | number | Master windows (always set; user-adjustable only with `supportsMasterCount`) |
| `splitRatio` | number | Master/stack split 0.1–0.9 (always set; user-adjustable only with `supportsSplitRatio`) |
| `minSizes` | `{ {w, h} }` | Per-window minimum sizes, 1-indexed |
| `focusedIndex` | number | 0-based tiled index of the focused window (`-1` if none) |
| `windows` | `{ {appId, focused, windowId} }?` | Per-window info, when available |
| `screen` | `{id, portrait, aspectRatio}?` | Output info |
| `tree` | `SplitNode?` | Persistent split tree (memory algorithms only) |
| `custom` | `{[string]: any}?` | Your `customParams` values, keyed by name |
| `currentGeometries` | `{Zone}?` | Last applied zones (advisory; post-enforcement) |
| `state` | `{[string]: any}?` | Persistent script-state table (§9, `supportsScriptState` only) |

A `Zone` is a plain table of **absolute pixel** coordinates:

```lua
{ x = 0, y = 0, width = 960, height = 1080 }
```

Return `{}` for `windowCount <= 0`. Guard tiny areas with `pluau.guardArea`.
It returns `{}` for `count <= 0`, a plain fill when the work area is smaller
than `pluau.MIN_ZONE_SIZE` on either axis, and `nil` when the layout should
proceed normally:

```lua
local early = pluau.guardArea(ctx.area, ctx.windowCount)
if early then return early end
```

---

## 6. The `pluau` standard library

### `Rect` (what `ctx.area` is)

Gap-aware split helpers. Each `split*` returns the **named side first**, then the
remainder; `columns`/`rows` return a list.

```lua
local left, rest   = ctx.area:splitLeft(0.6, ctx.innerGap)   -- 60% on the left
local top,  rest2  = rest:splitTop(0.5, ctx.innerGap)
local cols         = ctx.area:columns(3, ctx.innerGap)        -- { Rect, Rect, Rect }
local rows         = ctx.area:rows(2, ctx.innerGap)
```

`Rect` also has the plain fields `x`, `y`, `width`, `height`. A `Rect` is a valid
`Zone`, so you can return them directly.

### Constants

`pluau.MIN_ZONE_SIZE` (50), `pluau.MIN_SPLIT` (0.1), `pluau.MAX_SPLIT` (0.9),
`pluau.MAX_TREE_DEPTH` (50).

### High-level layout helpers

These build a full `{Zone}` list for common patterns (the same ones the built-in
algorithms use):

```
pluau.fillArea(area, count)
pluau.fillRegion(x, y, w, h, count)
pluau.equalColumnsLayout(area, count, gap, minSizes)
pluau.masterStackLayout(area, count, gap, splitRatio, masterCount, minSizes, horizontal)
pluau.deckLayout(area, count, focusedFraction, horizontal)
pluau.lShapeLayout(area, count, gap, splitRatio, distribute, bottomWidth, rightHeight)
pluau.dwindleLayout(area, count, splitRatio, innerGap, minSizes)
pluau.threeColumnLayout(area, count, gap, splitRatio, masterCount, minSizes)
pluau.stripLayout(zones, startX, startY, panelW, panelH, count, gap, horizontal)  -- appends into `zones`, returns nothing
pluau.applyTreeGeometry(node, rect, gap, depth?)  -- memory algorithms; depth defaults to 0
```

### Guards & general utilities

```
pluau.guardArea(area, count)      -- early-return fill for tiny work areas
pluau.clamp(v, lo, hi)            -- returns nil for a non-number or NaN (pair with `or default`); ±inf clamps to the bound
pluau.center(start, total, size)
pluau.gridShape(count)            -- cols, rows for a near-square grid
pluau.rect(x, y, w, h)
pluau.join(...lists)
pluau.clampSplitRatio(r)
```

### Distribution & min-size primitives

`pluau.distributeWithGaps`, `pluau.distributeWithMinSizes`,
`pluau.distributeWithOptionalMins`, `pluau.distributeEvenly`,
`pluau.cumulativeOffsets`, `pluau.extractMinWidths`, `pluau.extractMinHeights`,
`pluau.extractRegionMaxMin`, `pluau.minSizeAt`, `pluau.applyPerWindowMinSize`,
`pluau.computeCumulativeMinDims`, `pluau.solveTwoPart`, `pluau.solveThreeColumn`,
`pluau.appendGracefulDegradation`.

`appendGracefulDegradation` appends into its `zones` argument and returns
nothing, like `stripLayout`.

### Resize helpers (§9)

`pluau.masterStackResize(state, resize, horizontal)` implements the standard
master/stack ratio-reflow for `onWindowResized`. For the lower-level ratio math,
use `pluau.resizeRatioGrow` and `pluau.resizeRatioShrink`.

Full signatures are in the type stubs (`pluau.d.luau`) and
`data/algorithms/*.luau`.

---

## 7. Custom parameters

Expose user-tunable knobs via `metadata.customParams`; read them from
`ctx.custom`:

```lua
metadata = {
    name = "Cluster",
    customParams = {
        { name = "focusBoost", type = "number", default = 0.2, min = 0.0, max = 0.5,
          description = "Extra width given to the focused cluster" },
        { name = "horizontal", type = "bool", default = false,
          description = "Stack horizontally" },
        { name = "mode", type = "enum", default = "even",
          options = { "even", "weighted" }, description = "Distribution mode" },
    },
},

tile = function(ctx)
    local boost = (ctx.custom and ctx.custom.focusBoost) or 0.2
    -- …
end,
```

Supported `type`s: `"number"` (with `min`/`max`), `"bool"`, `"enum"` (with
`options`). The settings UI renders a control for each and stores the value the
daemon passes back in `ctx.custom`.

---

## 8. Lifecycle hooks

`onWindowAdded(state, index)` and `onWindowRemoved(state, index)` fire when a
window joins or leaves the tiled set. `index` is the **0-based** tiled index
(not Luau-1-based). `state` is a read-only snapshot of the engine's tiling
state (the `HookState` type in the stubs):

| Field | Type | Notes |
|---|---|---|
| `windowCount` | number | Currently tiled windows |
| `masterCount` | number | Master windows |
| `splitRatio` | number | Current split ratio (clamped 0.1–0.9) |
| `windows` | `{ {appId, focused, windowId} }` | Per-window info |
| `focusedIndex` | number | 0-based; `-1` if none |
| `scriptState` | table? | Prior persistent state (§9); present only when `supportsScriptState` is set and the bag is non-empty |
| `countAfterRemoval` | number? | `onWindowRemoved` only: count minus the departing window |

The snapshot is built fresh per call and mutating it has no effect. Both hooks
return nothing, so they are notifications. Use them to observe the tiled set
rather than to change it. Memory algorithms (§10) do not need them, because the host
maintains the split tree itself. Script-state algorithms persist through the
`onWindowResized` return value instead.

---

## 9. Interactive resize & script state

Implement `onWindowResized(state, resize)` to react when the user drags a
tiled window's edge. `state` is the same snapshot as §8, and `resize` describes
the drag:

```lua
resize = {
    index = 2,                    -- 0-based tiled index of the resized window
    oldRect = { x, y, width, height },
    newRect = { x, y, width, height },
    edges = { left = false, right = true, top = false, bottom = false },
}
```

Return a table (or `nil` to do nothing). Two optional, independent outputs:

- **`splitRatio`** — a new master/split ratio the engine applies (clamped).
  This is how ratio-based layouts reflow on resize, and the change stays local
  to the current screen, desktop, and activity. `pluau.masterStackResize(state,
  resize, horizontal)` implements the standard master/stack case.
- **Any other keys** — persisted as the new `ctx.state` table, but only when
  `metadata.supportsScriptState = true`. On the next `tile` run, `ctx.state`
  holds what you returned, sanitized: non-finite numbers are dropped, nesting
  past 16 levels is trimmed away, as is everything past 4096 keys counted
  across the whole bag rather than per table, and a bag over 64 KiB
  of JSON is dropped entirely. Keep it small and flat. Read your prior values from
  `state.scriptState` inside the hook. See `data/algorithms/aligned-grid.luau`
  for a full example (per-column widths that survive retiles).

The table you return replaces the whole bag rather than merging into it, so
return every key you want to keep. When a branch has nothing to change, return
`state.scriptState` unchanged. This is why `aligned-grid` returns it verbatim on
its early-outs. Returning `nil` leaves the bag alone, but returning only
`splitRatio` clears it, because `splitRatio` is stripped before the rest is
stored. `pluau.masterStackResize` returns either `nil` or a table holding only
`splitRatio`, so a `supportsScriptState` algorithm cannot return it directly.
Build your own table and copy the helper's ratio into it when there is one. The
helper returns `nil` whenever it has no ratio to offer, which covers a layout
with no stack to split, a degenerate old size or ratio, and a drag on an edge
that is not the seam.

`supportsScriptState` is the lightweight alternative to the split tree: an
opaque table you shape however you like, persisted per screen, desktop, and
activity.

---

## 10. Memory-aware algorithms (advanced)

Set `metadata.supportsMemory = true` to receive a persistent `ctx.tree`
(`SplitNode`) that survives across window add/remove events. The host keeps the
tree in step for you, so a memory algorithm needs no lifecycle hooks (§8). It
can still implement `onWindowResized` (§9) to react to a drag. Use
`pluau.applyTreeGeometry(ctx.tree, ctx.area, ctx.innerGap)` in `tile` to turn
the tree into zones. See `data/algorithms/dwindle-memory.luau` for a full
example. Most layouts are stateless and don't need this.

---

## 11. Editor support & validation

Drop a `.luaurc` next to your algorithms so [luau-lsp](https://github.com/JohnnyMorganz/luau-lsp)
autocomplete and `luau-analyze` know about the injected `pluau` global:

```jsonc
// ~/.local/share/plasmazones/algorithms/.luaurc
{
    "languageMode": "nonstrict",
    "lint": { "*": true },
    "lintErrors": false,
    "globals": ["pluau"]
}
```

For full `pluau.*` type information, register the shipped stubs
(`/usr/share/plasmazones/pluau.d.luau`) as a definition file in luau-lsp, which
is what gives an editor `pluau.*` completion and type checking. See luau-lsp's
own documentation for the setting, which its editor integrations spell
differently. The bundled `luau-analyze` takes no definitions flag, so it never
reads the stub: §11's `.luaurc` is what keeps it from calling `pluau` an unknown
global, and a command-line check therefore tells you the script parses, not that
your `pluau` calls typecheck. The editor is where that happens.

Leave the stub where it is installed rather than copying it into the algorithms
directory, which the loader scans for `*.luau` and would log a skip warning for
it.

Type-check before relying on a layout (CI runs exactly this over the bundled
set):

```bash
luau-analyze ~/.local/share/plasmazones/algorithms/my-columns.luau
```

A parse error means the daemon will skip the algorithm, so this is the quickest
way to catch one. It is not a guarantee that a script loads or behaves: Luau
erases type annotations when it compiles and the engine does not typecheck, so a
type error is a lint for you rather than something the daemon refuses. Nor does
a clean run see a `pluau` call that does not exist (§11) or a file over the size
cap (§12).

---

## 12. Sandbox & limits

- `pluau` and the standard library are **frozen** before your script runs — you
  cannot monkey-patch them or reach outside the sandbox (no `io`, `os.execute`,
  filesystem, or network).
- A long-running or infinite-looping `tile` is **interrupted** by a watchdog, so
  a runaway script can't hang the compositor.
- Each script's heap is **capped** (default 64 MiB, enforced once the sandbox is
  active). A runaway allocation surfaces as a catchable Luau out-of-memory error
  rather than exhausting the compositor — the script fails, the daemon keeps
  running.
- A script over 1 MiB is **refused** before it loads, so it never reaches the
  list and a clean `luau-analyze` run will not tell you why. No hand-written
  layout comes near this, but a generated or vendored one might.
- Keep `tile` pure and fast: it runs on every relevant layout change. Return
  early for `windowCount <= 0`, and guard against tiny areas with
  `pluau.guardArea`.
