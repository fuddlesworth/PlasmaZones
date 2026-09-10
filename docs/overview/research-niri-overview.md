<!-- SPDX-FileCopyrightText: 2026 fuddlesworth
     SPDX-License-Identifier: GPL-3.0-or-later -->

# niri Overview: research report

Sources: the niri wiki (git clone of `YaLTeR/niri.wiki`), the niri source tree at HEAD (clone of `YaLTeR/niri`, now also `niri-wm/niri`), the v25.05 release notes, and open GitHub issues. Line references are from the cloned source at HEAD on 2026-09-04.

Local working copies:
- Wiki: `/tmp/claude-1000/-home-nlavender-Projects-PlasmaZones/0832c788-2648-4dac-a05b-557d7a47d46e/scratchpad/wiki/`
- Source: `/tmp/claude-1000/-home-nlavender-Projects-PlasmaZones/0832c788-2648-4dac-a05b-557d7a47d46e/scratchpad/src/`

Key URLs:
- Overview wiki page: https://github.com/YaLTeR/niri/wiki/Overview
- Configuration: Miscellaneous (the `overview {}` block): https://github.com/YaLTeR/niri/wiki/Configuration:-Miscellaneous
- Configuration: Gestures: https://github.com/YaLTeR/niri/wiki/Configuration:-Gestures
- Gestures (list of all gestures): https://github.com/YaLTeR/niri/wiki/Gestures
- Configuration: Animations: https://github.com/YaLTeR/niri/wiki/Configuration:-Animations
- Configuration: Layer Rules (`place-within-backdrop`): https://github.com/YaLTeR/niri/wiki/Configuration:-Layer-Rules
- Configuration: Outputs (`backdrop-color`, per-output `hot-corners`): https://github.com/YaLTeR/niri/wiki/Configuration:-Outputs
- v25.05 release notes: https://github.com/niri-wm/niri/releases/tag/v25.05
- Original tracking issue: https://github.com/niri-wm/niri/issues/850 (closed)
- Original design discussion: https://github.com/niri-wm/niri/discussions/352

---

## 1. Visual model

- **Shipped in 25.05.** Wiki: "The Overview is a zoomed-out view of your workspaces and windows. It lets you see what's going on at a glance, navigate, and drag windows around."
  https://github.com/YaLTeR/niri/wiki/Overview

- **Zoom.** `overview { zoom }` ranges 0 to 0.75, default 0.5. Lower values make everything smaller. Wiki text: "Control how much the workspaces zoom out in the overview. `zoom` ranges from 0 to 0.75 where lower values make everything smaller."
  https://github.com/YaLTeR/niri/wiki/Configuration:-Miscellaneous

  In code the configured value is clamped to `0.0001..=0.75` and the live zoom is a linear interpolation on the open/close progress:

  ```rust
  // src/layout/mod.rs:5020
  fn compute_overview_zoom(options: &Options, overview_progress: Option<f64>) -> f64 {
      let zoom = options.overview.zoom.clamp(0.0001, 0.75);
      if let Some(p) = overview_progress {
          (1. - p * (1. - zoom)).max(0.0001)
      } else {
          1.
      }
  }
  ```

  Default lives at `niri-config/src/misc.rs:128` (`zoom: 0.5`, `backdrop_color: DEFAULT_BACKDROP_COLOR`, `workspace_shadow: default`). The KDL type is `FloatOrInt<0, 1>` (`niri-config/src/misc.rs:139-146`), so values above 0.75 parse but get clamped at runtime.

- **Per-monitor vertical stack.** Each monitor stacks its own workspaces vertically; each workspace renders its full column strip at the current zoom. From `src/layout/monitor.rs:1357-1372`:

  ```rust
  fn workspace_size(&self, zoom: f64) -> Size<f64, Logical> {
      let ws_size = self.view_size.upscale(zoom);
      let scale = self.scale.fractional_scale();
      ws_size.to_physical_precise_ceil(scale).to_logical(scale)
  }

  fn workspace_gap(&self, zoom: f64) -> f64 {
      let scale = self.scale.fractional_scale();
      let gap = self.view_size.h * 0.1 * zoom;
      round_logical_in_physical_max1(scale, gap)
  }
  ```

  So the gap between workspaces is 10% of a screen height, scaled by zoom. Workspaces are centred horizontally: `workspaces_render_geo` computes `static_offset = (view_size - ws_size) / 2` and `first_ws_y = -workspace_render_idx * ws_height_with_gap` (`src/layout/monitor.rs:1476-1500`). The iterator deliberately yields one extra entry: "Return position for one-past-last workspace too" — `(0..=self.workspaces.len())`.

  Everything is rounded to physical pixels, and the code post-rounds the final location with a comment explaining why: floating point addition can leave the current workspace at `y = 0.0000000000002` and thereby miss pointer hits at the monitor edge.

- **Empty trailing workspace.** This is not a special affordance. niri always keeps one empty workspace at the bottom, and with `empty-workspace-above-first` one at the top too, so in the overview those simply appear as ordinary empty slots you can drop into. The drop path reuses them rather than inserting a new workspace (`src/layout/mod.rs:4236-4247`):

  ```rust
  InsertWorkspace::NewAt(ws_idx) => {
      if mon.options.layout.empty_workspace_above_first && ws_idx == 0 {
          0                              // Reuse the top empty workspace.
      } else if mon.workspaces.len() - 1 <= ws_idx {
          mon.workspaces.len() - 1       // Reuse the bottom empty workspace.
      } else {
          mon.add_workspace_at(ws_idx);
          ws_idx
      }
  }
  ```

- **Backdrop.** `overview { backdrop-color }` defaults to `#262626`. The alpha channel is ignored. Wiki: "Set the backdrop color behind workspaces in the overview. The backdrop is also visible between workspaces when switching." Overridable per output via `output { backdrop-color }` (`src/niri.rs:1781`, `src/niri.rs:2860` fall back to the global value). So the backdrop is not overview-exclusive; it is what you already see between workspaces on a normal vertical switch.

- **Workspace shadow.** `overview { workspace-shadow { ... } }` mirrors the layout `shadow` config. Values are normalized to a 1080px-tall workspace and then zoomed with the workspace (`src/layout/workspace.rs:2079`):

  ```rust
  fn compute_workspace_shadow_config(config: WorkspaceShadow, view_size: Size<f64, Logical>) -> Shadow {
      // Gaps between workspaces are a multiple of the view height, so shadow settings should also
      // be normalized to the view height to prevent them from overlapping on lower resolutions.
      let norm = view_size.h / 1080.;
      let mut config = Shadow::from(config);
      config.softness *= norm;
      config.spread   *= norm;
      config.offset.x.0 *= norm;
      config.offset.y.0 *= norm;
      config
  }
  ```

  Wiki guidance: "you'll want bigger spread, offset, and softness compared to window shadows." Example config from the wiki:

  ```kdl
  overview {
      zoom 0.5
      backdrop-color "#262626"

      workspace-shadow {
          softness 40
          spread 10
          offset x=0 y=10
          color "#00000050"
      }
  }
  ```

  `workspace-shadow { off }` disables it.

- **All monitors at once.** The open state is a single boolean on `Layout`, pushed to every monitor (`src/layout/mod.rs:4605`):

  ```rust
  pub fn set_monitors_overview_state(&mut self) {
      let MonitorSet::Normal { monitors, .. } = &mut self.monitor_set else { return; };
      for mon in monitors {
          mon.overview_open = self.overview_open;
          mon.set_overview_progress(self.overview_progress.as_ref());
      }
  }
  ```

  Every monitor zooms out simultaneously and in sync, each showing its own workspace stack.

---

## 2. Interaction

- **Actions.** Three, defined in `niri-ipc/src/lib.rs:911-916` and mapped in `niri-config/src/binds.rs:365-367, 700-702`:
  - `toggle-overview` / `ToggleOverview {}`
  - `open-overview` / `OpenOverview {}`
  - `close-overview` / `CloseOverview {}`

  `open`/`close` are no-ops when already in that state and return `false`, so a bind will not restart the animation (`src/layout/mod.rs:4633-4649`).

- **Opening.** Wiki: "Open it with the `toggle-overview` bind, via the top-left hot corner, or using a touchpad four-finger swipe up."

- **All keyboard shortcuts keep working.** Wiki: "While in the overview, all keyboard shortcuts keep working, while pointing devices get easier."

- **Click on a window.** Left click does NOT activate on press. A `MoveGrab` starts immediately; on release with no movement, the window is activated and the overview closes with a synchronized workspace animation (`src/input/move_grab.rs:85-102`):

  ```rust
  GestureState::Recognizing => {
      // Activate the window on release. This is most prominent in the overview where
      // windows are not activated on click. In the overview, we also try to do a nice
      // synchronized workspace animation.
      if layout.is_overview_open() {
          let res = layout.workspaces().find_map(|(mon, ws_idx, ws)| { ... });
          if let Some((Some(output), ws_idx)) = res {
              layout.focus_output(&output);
              layout.toggle_overview_to_workspace(ws_idx);
          }
      }
      layout.activate_window(&self.window);
  }
  ```

  And `toggle_overview_to_workspace` (`src/layout/mod.rs:4651`) activates that workspace using the overview open/close animation config, then toggles:

  ```rust
  pub fn toggle_overview_to_workspace(&mut self, ws_idx: usize) {
      let config = self.options.animations.overview_open_close.0;
      if let Some(mon) = self.active_monitor() {
          mon.activate_workspace_with_anim_config(ws_idx, Some(config));
      }
      self.toggle_overview();
  }
  ```

  Note in `src/input/mod.rs:2924-2955`: the grabbing cursor is deliberately NOT set right away in the overview, "In the overview, we click to activate window and close the overview, in this case setting the cursor right away would be distracting."

- **Click on empty workspace area.** Focuses that output and calls `toggle_overview_to_workspace(ws_idx)` — same close animation, focused window unchanged (`src/input/mod.rs:3031-3038`).

- **Right click and drag.** Scrolls the strip horizontally on the workspace under the cursor. Implemented as a `SpatialMovementGrab` with `view_offset_gesture_begin`, cursor set to `all-scroll` (`src/input/mod.rs:2853-2878`). Only when the pointer is not already grabbed.

- **Middle click plus Mod.** Still the normal spatial-movement grab, but in the overview it targets the workspace under the cursor rather than the active one (`src/input/mod.rs:2882-2890`).

- **Wheel.** With no modifiers, niri synthesizes binds (`src/input/mod.rs:3227-3270`):
  - vertical up/down -> `FocusWorkspaceUpUnderMouse` / `FocusWorkspaceDownUnderMouse`, with a 50 ms cooldown
  - horizontal left/right -> `FocusColumnLeftUnderMouse` / `FocusColumnRightUnderMouse`
  - Shift + vertical -> `FocusColumnLeftUnderMouse` / `FocusColumnRightUnderMouse`

  No Mod is required. Crucially, this synthesis is gated on the pointer NOT being over a *top* or *overlay* layer surface (`src/input/mod.rs:3123-3141`), so your bar keeps its own scroll handling while the overview is open.

- **Touchpad.** Two-finger scrolling in the overview does what three-finger does normally: vertical switches workspaces, horizontal moves the view. Handled by a dedicated axis-locking `overview_scroll_swipe_gesture` (`src/input/mod.rs:3333-3410`, state at `src/niri.rs:376`).

- **Touchscreen.** One-finger scrolling, or one-finger long press to move a window (wiki). See `src/input/touch_overview_grab.rs`, which also calls `toggle_overview_to_workspace` on tap (`:209`).

- **Keyboard navigation while open.** When keyboard focus is `KeyboardFocus::Overview`, a hardcoded bind table applies on top of the user's binds (`hardcoded_overview_bind`, `src/input/mod.rs:4828`). Modifiers must be empty:

  | Key | Action | Repeat |
  |---|---|---|
  | Escape | ToggleOverview | no |
  | Return | ToggleOverview | no |
  | Left | FocusColumnLeft | yes |
  | Right | FocusColumnRight | yes |
  | Up | FocusWindowOrWorkspaceUp | yes |
  | Down | FocusWindowOrWorkspaceDown | yes |

  So Escape closes the overview. Focus becomes `KeyboardFocus::Overview` only when no layer surface claims it (`src/niri.rs:3459-3462`).

---

## 3. Drag and drop

- **Window DnD.** Left click and drag moves windows across the zoomed-out workspaces. The drop target comes from `Monitor::insert_position` (`src/layout/monitor.rs:1595-1642`), which walks the workspace render geometries top to bottom:
  - pointer above the first workspace -> `InsertWorkspace::NewAt(first_idx)`
  - pointer inside a workspace rect -> `InsertWorkspace::Existing(ws_id)` plus that rect
  - pointer inside a gap between two workspaces -> `InsertWorkspace::NewAt(idx)`
  - anything below the last -> `InsertWorkspace::NewAt(last_idx + 1)`

  That gap case is exactly the "drop above, below, or between existing workspaces to create one" behavior the wiki demonstrates with a video.

- **Column vs in-column.** For an existing workspace, the pointer position is converted to workspace-local coordinates and divided by zoom, then handed to `scrolling_insert_position` (`src/layout/mod.rs:4186-4200`):

  ```rust
  let pos_within_workspace = (move_.pointer_pos_within_output - geo.loc).downscale(zoom);
  let ws = &mut mon.workspaces[ws_idx];
  ws.scrolling_insert_position(pos_within_workspace)
  ```

  The result is one of `InsertPosition::NewColumn(usize)`, `InsertPosition::InColumn(column_idx, tile_idx)`, or `InsertPosition::Floating` (`src/layout/monitor.rs:131-135`). So you can drop into an existing column as a stacked tile, not only as a new column. Tabbed display is a column display mode in niri, so dropping into a tabbed column joins its tabs.

- **New workspace drops.** Always `InsertPosition::NewColumn(0)`, or `Floating` if the dragged window was floating (`src/layout/mod.rs:4202-4210`).

- **Tile size.** The dragged tile keeps its width and its full-width flag across the drop; `add_tile` is called with `move_.width` and `move_.is_full_width` (`src/layout/mod.rs:4252-4265`). Floating tiles get their position recomputed from the render location minus the workspace origin, divided by zoom, then converted to size-fraction coordinates:

  ```rust
  let pos = (tile_render_loc - offset).downscale(zoom);
  let pos = mon.workspaces[ws_idx].floating_logical_to_size_frac(pos);
  tile.floating_pos = Some(pos);
  ```

- **Cross-monitor.** Insert position uses the monitor under the pointer. If the pointer is over no known monitor output, niri falls back to the active monitor's first workspace with an insert position computed at `(0, 0)`, with the comment "No point in trying to use the pointer position on the wrong output" (`src/layout/mod.rs:4213-4229`).

- **Foreign DnD (files etc.).** Two overview affordances, both since 25.05:
  - `dnd-edge-workspace-switch`: scroll workspaces up/down at a monitor edge while in the overview. Defaults `trigger-height 50`, `delay-ms 100`, `max-speed 1500`, where "1500 corresponds to one screen height per second". The delay exists to avoid unwanted scrolling when dragging across monitors.
  - hold-to-activate: "While drag-and-dropping, hold your mouse over a window to activate it... In the overview, you can also hold the mouse over a workspace to switch to it."

  The hot corner also works mid-drag, which is what makes the mouse-only cross-workspace DnD flow possible. Wiki: "Combined with the hot corner, this lets you do a mouse-only DnD across workspaces."

  Related config, `dnd-edge-view-scroll` (since 25.02, works outside the overview too): `trigger-width 30`, `delay-ms 100`, `max-speed 1500`.

- **Hot corners.** Since 25.05, top-left by default. Since 25.11 you can name corners: `top-left`, `top-right`, `bottom-left`, `bottom-right`; if none is named, top-left is active. `off` disables. Per-output override via `output { hot-corners { ... } }` (since 25.11). Hot corner hit-testing short-circuits everything else under the cursor (`src/niri.rs:3457-3461`, `is_inside_hot_corner`).

---

## 4. Workspace management in the overview

There are no overview-specific workspace operations. Every normal bind keeps working while zoomed out, so `move-workspace-up` / `move-workspace-down`, named-workspace focus, column width changes, consume/expel, and window closing all apply to the focused workspace and are simply visible at zoom.

Two consequences show up as open issues:
- Workspace actions are not animated. That was mostly invisible before the overview and is now conspicuous — issue #1469, "Workspace actions are not animated. This was mostly invisible until the Overview, now it's visible."
- Dragging a whole workspace to reorder it or move it to another monitor is NOT implemented — issue #1468: "Right now you can drag-and-drop individual windows. A natural extension is do be able to drag-and-drop entire workspaces to reorder them or to a different monitor. I'm thinking Mod+LMB for this, and Mod+touch on a touchscreen... I expect this to require quite a big refactor."

---

## 5. Animation and rendering

- **Open/close animation.** `animations { overview-open-close { ... } }`, since 25.05. Default is a spring (`niri-config/src/animations.rs:298-311`):

  ```kdl
  animations {
      overview-open-close {
          spring damping-ratio=1.0 stiffness=800 epsilon=0.0001
      }
  }
  ```

- **Toggle path.** `toggle_overview` flips the boolean, takes the current progress value as the animation start, and animates to 1.0 or 0.0 (`src/layout/mod.rs:4616-4631`). Interrupting mid-animation therefore reverses smoothly from wherever it is.

- **Gesture-tracked open.** The four-finger vertical touchpad swipe drives progress directly (`src/layout/mod.rs:3742-3810`). Constants at `src/layout/mod.rs:103-108`:

  ```rust
  const OVERVIEW_GESTURE_MOVEMENT: f64 = 300.;
  const OVERVIEW_GESTURE_RUBBER_BAND: RubberBand = RubberBand { stiffness: 0.5, limit: 0.05 };
  ```

  Progress is `start + tracker.pos() / 300`, rubber-banded into `0..1`. On release, the projected end position is clamped and rounded to 0 or 1, and velocity (scaled by the rubber band derivative) is carried into the spring.

- **Composition with the strip view offset.** Zoom and the vertical workspace switch are synchronized. `workspace_render_idx` corrects the switch position for the changing workspace height so that a simultaneous zoom-plus-switch does not jump; there is a long comment block at `src/layout/monitor.rs:1410-1465` deriving the correction from `from_ws_height_with_gap` vs `ws_height_with_gap`. `set_overview_progress` restarts the workspace-switch animation if the corrected index would jump: "If the view jumped (can happen when going from corrected to uncorrected render_idx, for example when toggling the overview in the middle of an overview animation), then restart the workspace switch to avoid jumps" (`src/layout/monitor.rs:1379-1394`).

  Horizontal strip scrolling stays per-workspace. The whole workspace, including its strip offset, is rendered and then rescaled by a `RescaleRenderElement` about `(0, 0)` with factor `zoom` (`src/layout/monitor.rs:1691-1700`, `:1829-1836`).

- **Live content, not thumbnails.** Windows render as ordinary surfaces that are scaled down. Two consequences, both open issues:
  - #1467 "Overview: fix thin border flickering, other 1 px jank" — "The root cause for all of these issues is that when we round element sizes and positions to physical pixels, we don't consider the overview zoom level." The issue contains a three-step refactor plan (an `OriginRescaleRenderElement` that rescales about (0,0) and passes `scale * zoom` through, threading `overview_progress` down from `Monitor` to `Workspace`/`ScrollingSpace`/`Tile`, and using zoom together with scale when rounding).
  - #1470 "Overview: generate mipmaps for nicer downscaled rendering" — "Window contents get aliasing artifacts when scaled down below 50% zoom level. Mipmaps solve this." Deferred pending GPU profiling (Tracy in Smithay) to judge the cost; gnome-shell debounces mipmap generation.

- **Layer shell.** Wiki tip, verbatim:

  > The overview needs to draw a background under every workspace.
  > So, layer-shell surfaces work this way: the *background* and *bottom* layers zoom out together with the workspaces, while the *top* and *overlay* layers remain on top of the overview.
  >
  > Put your bar on the *top* layer.

  In code, while the overview is open, bottom and background layers stop receiving pointer grabs and keyboard focus (`src/niri.rs:3439-3480`) and are excluded from hit-testing (`src/niri.rs:3465-3478`). `Monitor::render_above_top_layer` returns false whenever a workspace switch or an overview progress is active (`src/layout/monitor.rs:1645-1655`), so a fullscreen window never covers the bar during the overview.

- **Backdrop customization.** The `place-within-backdrop true` layer rule (since 25.05) moves a *background* layer surface into the backdrop, where it "will ignore all input". Wiki notes it only works for background surfaces that ignore exclusive zones, which is typical of wallpaper tools. The documented pattern is two wallpaper tools, one blurred in the backdrop and one sharp per workspace, optionally paired with `layout { background-color "transparent" }` and `overview { workspace-shadow { off } }` for a stationary wallpaper look.

- **Spawn while the overview is open.** Nothing auto-closes the overview when a window opens. The window is added to its target workspace normally. niri suppresses one focus-related behavior while the overview is open at `src/niri.rs:6238` (`if !self.layout.is_overview_open() && current_focus.window.as_ref() != Some(window)`), and `src/layout/mod.rs:4659` skips restarting the open animation for the tile currently being interactively moved.

---

## 6. Known limitations and open feature requests

From open issues on `niri-wm/niri`:

| # | Title | Substance |
|---|---|---|
| 1468 | Overview: DnD entire workspaces | Reorder or move workspaces across monitors by dragging. Not implemented, "quite a big refactor". Proposed Mod+LMB, Mod+touch. |
| 1469 | Overview: animate workspaces moving, dis/appearing | Workspace actions are unanimated; the overview made it visible. |
| 1470 | Overview: generate mipmaps for nicer downscaled rendering | Aliasing below 50% zoom. Blocked on GPU profiling. |
| 1467 | Overview: fix thin border flickering, other 1 px jank | Rounding ignores zoom. Detailed refactor plan in the issue. |
| 1639 | Support trackpoint scrolling in the overview | Trackpoint + middle button does not scroll the overview; the touchpad two-finger gesture does. |
| 1739 | Overview gestures with tablet pen | Missing. |
| 2282 | Workspace shadow doesn't show in overview animation when the workspace itself isn't rendered | Shadow pops in during a slow gesture-driven open, visible at the screen edge. |
| 3860 | Blurred workspace edges for xray in the overview | Xray blurs the workspace background and the backdrop separately, then composites, leaving a hard edge at workspace boundaries. |
| 2759 | Rendering Glitch in Overview Mode | Reported visual glitch. |
| 1473 | Hot corner with barriers | Requested for hot-corner reliability. |

Original design intent, from the closed tracking issue #850:

> Actual interactions in the overview (click on window to focus it scrolling it into view? moving windows around workspaces like in GNOME Shell?) will need some trying and evaluating hands-on.
> There are some questions on what to do with layer-shell surfaces. It probably makes sense to draw a separate background layer on each workspace in the overview, and the top layers should stay on top.

Both of those questions were resolved the way the shipped feature behaves.

---

## 7. Shell and bar integration via IPC

The IPC surface is small and stable.

- **Actions:** `niri msg action toggle-overview`, `open-overview`, `close-overview` (`niri-ipc/src/lib.rs:911-916`). Every bindable action is also invocable via `niri msg action`.
- **Request:** `Request::OverviewState` returns `Response::OverviewState(Overview { is_open: bool })` (`niri-ipc/src/lib.rs:118-119, 164-175`).
- **Event:** `Event::OverviewOpenedOrClosed { is_open: bool }` (`niri-ipc/src/lib.rs:1701-1704`). The event-stream state part replicates the current value on connect, so a bar receives the state immediately rather than having to poll (`niri-ipc/src/state.rs:265-276`).

Quickshell-based bars such as DankMaterialShell hook this event to show or hide chrome and to drive their workspace widget. Their niri setup guidance is to make the layout background transparent so a wallpaper shows through into the overview.
- https://github.com/AvengeMedia/DankMaterialShell
- https://danklinux.com/docs/dankmaterialshell/compositors

---

## 8. Relevance to PlasmaZones

Three structural points transfer directly.

1. **One global boolean, fanned out per monitor.** `Layout::overview_open` plus a single `OverviewProgress` is pushed to every monitor by `set_monitors_overview_state`. Each monitor derives its own zoom from the shared progress. That is a much smaller surface than per-screen overview state, and it gives simultaneous multi-monitor zoom for free.

2. **Drops are purely coordinate-driven.** A single `insert_position` maps a pointer y to either an existing workspace or a gap index, and gaps are what create workspaces. There is no separate "new workspace" drop zone widget; the empty leading and trailing workspaces are ordinary workspaces that the drop path reuses.

3. **Zoom composes with, rather than replaces, the strip view offset.** The workspace renders its normal strip at its normal offset, and the whole thing is rescaled. The only hard part is rounding: niri's own 1px jank issue (#1467) exists precisely because the rounding pipeline does not know about the zoom factor. If PlasmaZones builds an equivalent, thread the zoom into the rounding from the start rather than rescaling a pre-rounded result.

One caution worth carrying over: niri's synthesized overview wheel binds are suppressed when the pointer is over a top or overlay layer surface, so the bar keeps its own scroll behavior. Any equivalent in PlasmaZones needs the same carve-out for panel and shell surfaces.
