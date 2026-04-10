# Changelog

All notable changes to PlasmaZones are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [2.8.4] - 2026-04-09

### Fixed
- **Ephemeral windows entering autotile tree** ([#271](https://github.com/fuddlesworth/PlasmaZones/discussions/271)): The KWin effect's minimum window size filter initialized to 0x0 and was only populated after an async D-Bus settings load. During that startup race window, all windows — including Steam splash screens and Electron notification popups — bypassed the size check and entered the tiling tree. The cache now initializes to 200x150 (matching daemon defaults) so the filter is active from effect load.

### Added
- **`--log-file` flag for daemon**: `plasmazonesd --log-file /tmp/pz.log` redirects all log output to a file (append mode, thread-safe). Combines with `--debug` for easy bug report capture without piping or `journalctl`.
- **Autotile eligibility diagnostics**: Windows accepted into the autotile tree now log their window class, skipSwitcher, keepAbove, and transient properties at debug level, making it possible to identify why specific windows pass the filter.

## [2.8.3] - 2026-04-09

### Added
- **`--debug` flag for daemon** ([#271](https://github.com/fuddlesworth/PlasmaZones/discussions/271)): `plasmazonesd --debug` (or `-d`) enables debug-level logging for all `plasmazones.*` categories, replacing the need for `qtlogging.ini` or environment variables when capturing diagnostic output.

### Improved
- **Autotile diagnostic depth**: Debug logging now includes per-window min-sizes used in zone calculation, before/after zone comparison from `enforceWindowMinSizes`, per-window applied geometries in `applyTiling`, min-size cap values, stale min-size clearing on unfloat, and split tree ratio restoration detail. These additions target the intermittent master-goes-to-100% ratio bug reported in #271.

## [2.8.2] - 2026-04-08

### Fixed
- **Autotile windows pushed off-screen on retile** ([#271](https://github.com/fuddlesworth/PlasmaZones/discussions/271)): When a window's minimum size exceeded its assigned zone (common with browsers on ultrawide monitors), the Wayland centering code centered the oversized window within the zone — pushing it to a negative x/y position, literally off the left edge of the screen. Oversized windows are now left/top-aligned in their zone instead of centered, staying on-screen while the daemon adjusts zone sizes.
- **Min-size clearing regression from 2.8.1**: Removed the indiscriminate `m_windowMinSizes` clearing in `onScreenGeometryChanged()` added in 2.8.1. The min-size feedback loop it guarded against was already eliminated by removing the `targetZone.width()` fallback, so the clearing just forced windows through unnecessary centering discovery cycles — triggering the off-screen push above.

### Improved
- **Autotile diagnostic logging**: Added logging to key autotile paths — window open/remove events now log IDs and min-sizes, `recalculateLayout` logs zone geometries and split ratios, screen geometry changes are logged, and window eligibility rejections now include the reason. This makes autotile ratio issues diagnosable from `journalctl` without code changes.

## [2.8.1] - 2026-04-08

### Fixed
- **Autotile ratio stuck after notification dismiss** ([#271](https://github.com/fuddlesworth/PlasmaZones/discussions/271)): Eliminated a min-size feedback loop where the KWin effect's Wayland centering code fell back to reporting the target zone width as a discovered minimum size for apps without a declared `minSize()`. This self-reinforcing constraint locked the master/stack split ratio until the user minimized and restored a window. Only the compositor's declared minimum is now reported.
- **Stale min-sizes after screen geometry change**: Discovered min-sizes for windows on a screen are now cleared when the available geometry changes (e.g., panel/systray resize), preventing stale constraints from overriding the user's split ratio on geometry-triggered retiles.

## [2.8.0] - 2026-04-07

### Added
- **Support report generator** ([#302]): `plasmazones-report` script collects daemon logs, config, and data directory into a tar.gz archive for bug reports and discussions.
- **Autotile window preservation** ([#301]): Autotiled windows now survive layout switches, mode toggles, and daemon restarts — matching the preservation behavior that snapped windows already had.
- **Disabled-context OSD** ([#297]): Visual feedback when toggling PlasmaZones on a disabled desktop, activity, or screen — shows why nothing happened and where to change it.
- **Config v2 nested schema** ([#295]): `config.json` restructured from flat keys to a nested hierarchy mirroring the settings UI. Existing v1 configs are migrated automatically.
- **Systemd service autostart**: Enabling the daemon toggle now also enables the systemd user service so PlasmaZones starts on login.

### Changed
- **Assignments split to `assignments.json`** ([#300]): Layout-to-screen assignments moved out of the main config into a dedicated file, reducing config churn and merge conflicts.
- **Session state split to `session.json`** ([#298]): Ephemeral session data (window positions, floating state) moved to its own file so it doesn't dirty the user config.
- **Autotile persistence refactor** ([#296]): Session persistence moved from `AutoTileState` to `WindowTrackingAdaptor` for cleaner separation between tiling logic and state serialization.
- **Settings consolidation**: `settings-window.conf` merged into `plasmazones-settings.conf`.

### Fixed
- **Assignment persistence across restart** ([#303]): Layout assignments and tiling window order now survive daemon restarts and mode-cycle toggling.
- **Autotile ratio retry** ([#299]): Bounded retry for transient geometry failures during autotile layout; stale min-size overrides are cleared after resize settles.
- **Config purge unknown keys** ([#300]): Unknown root-level groups are removed on save, preventing config pollution from obsolete or misspelled keys.
- **Window restore after config v2 migration** ([#295]): `loadState` was bypassing `IConfigBackend` group API after the schema change, breaking window restore on first launch.
- **Watcher double-delete guard** ([#302]): Fixed a use-after-free race in the file watcher teardown path.
- **Format warning in `splitDotPath`**: Fixed `qsizetype` vs `%d` printf mismatch.

## [2.7.1] - 2026-04-06

### Added
- **Custom algorithm parameter UI** ([#294]): Scripted algorithms declaring `@param` metadata now get auto-generated controls in the Tiling settings page — sliders for numbers, switches for bools, and combo boxes for enums.
- **Ratio step size slider** ([#292]): Configurable step size for master ratio keyboard shortcut adjustments.

### Fixed
- **Per-screen master ratio and count** ([#292]): Per-screen overrides for master ratio and count were not persisted correctly. Fixed key constants, slider bindings, and race conditions in the per-screen config path.
- **Reset to defaults clears per-algorithm settings** ([#292]): Resetting to defaults now properly clears saved per-algorithm autotile settings.
- **OSD shown at ratio bounds** ([#292]): Master ratio OSD was suppressed when the value hit min/max; now always shown on shortcut press.
- **Shader dark band at adjacent zone edges**: Eliminated a visible seam in the Aretha Shell shader where neighboring zones shared an edge.
- **Shader category duplication in settings**: Fixed duplicate shader categories and easing preview binding errors.
- **Editor shader error box**: Restored the shader compilation error display in the layout editor preview.

## [2.7.0] - 2026-04-04

### Added
- **JSON config backend** ([#286]): Config migrated from INI (`~/.config/plasmazonesrc`) to JSON (`~/.config/plasmazones/config.json`). Existing INI configs are migrated automatically on first launch. The JSON backend supports nested groups, proper array/object serialization, and atomic writes.
- **Master ratio/count OSD values** ([#289]): Shortcut adjustments now show the actual value in the navigation OSD: "Master ratio → 65%" and "Master count → 2".
- **Tumbleweed Drift shader improvements**: New wind-blown sand streams, stronger audio reactivity across all bands, faster animation speeds, larger dust devils, and full-surface treble sparkle. Sand streams follow the configured wind direction.

### Changed
- **Settings navigation**: App Rules moved under Snapping section. Child navigation pages have dividers for visual grouping.

### Fixed
- **Drag-to-float keeps drop position** ([#289]): Dragging a window off the autotile layout no longer snaps it back to its pre-autotile position. The daemon's geometry restore is skipped for drag-initiated floats.
- **Shortcut ratio/count persistence** ([#289]): Master ratio and count changes via keyboard shortcuts were only in memory due to QSignalBlocker suppressing the save path. Added debounced save so values survive reboots.
- **Algorithm switch persistence** ([#289]): Algorithm changes via settings also use QSignalBlocker and had the same missing-save issue. Now triggers the same debounced save.
- **Minimum window size for autotile** ([#290]): The user-configured minimum window width/height was never checked in the autotile path. Small utility windows like the Plasma emoji picker entered the tiling tree, causing rapid float/unfloat cycles that broke the master ratio. Now enforced in the KWin effect before windows reach the daemon.
- **Focus-follows-mouse through small windows** ([#290]): Windows below the minimum size threshold no longer steal focus on hover. They block focus-through like excluded apps and popups.
- **Shortcut debouncing** ([#288]): Rotate, float toggle, and layout cycle shortcuts are now debounced to prevent rapid-fire D-Bus calls from key repeat.
- **Overlay Vulkan surface churn**: Non-shader overlays are hidden instead of destroyed and recreated, reducing Vulkan surface create/destroy cycles.
- **Snap assist window pooling**: Snap assist reuses its window instead of creating a new one each time, and rejects stale layout requests.
- **JSON config serialization**: Arrays and objects in the config file are properly serialized through the flat map representation.

## [2.6.0] - 2026-04-03

### Added
- **Tumbleweed Drift shader**: openSUSE Tumbleweed branded zone overlay with animated desert landscape, rolling pinwheel logos, dust devils, sand particles, erosion flow lines, and responsive audio reactivity.
- **Neon Venom and Chrome Protocol shaders**.
- **Voxel-terrain improvements**: Multipass with depth buffer, highlight visibility, pulse-flow label effects.
- **Shader engine**: Depth buffer via MRT, per-channel filter/wrapping modes for multipass buffers, shader presets from registry.
- **Autotile JS API enrichment** ([#274]): Per-window context, custom parameters, and lifecycle hooks for scripted algorithms.
- **Layout ordering settings** ([#281]): Configure snapping and tiling cycle order in settings.
- **What's New dialog** ([#282]): Per-release highlights shown on first launch after update.
- **Settings `.desktop` file** for application launchers.

### Changed
- **Shared config backend** ([#276]): Single `QSettingsConfigBackend` shared across daemon instead of per-component instances.
- **Stale config key purging** ([#280]): Settings save removes obsolete keys to prevent config pollution.
- **ConfigDefaults reference constants** ([#278]): Autotile defaults reference `Defaults::` constants instead of duplicating values.

### Fixed
- **Shader parameter defaults**: `customParams` initialized to `-1.0` sentinel so fallback checks work on first frame.
- **DOF focal depth stability**: Multi-sample region averaging prevents jittery depth-of-field.
- **Vulkan surface lifecycle**: Fixes for surface creation on rapid show/hide, teardown crashes, keep-alive window management, deferred `QVulkanInstance` creation, scissor state, and swapchain colorspace.
- **Rendering pipeline stability**: RHI resource release on scene stop, multipass crash guards, SRB resets on label texture resize, texture upload ordering.
- **OSD improvements**: Navigation self-destruct fix, masterCount wiring, autotile metadata in layout OSD, per-screen dismiss.
- **Focus-follows-mouse**: No longer focuses through excluded or overlay windows.
- **Autotile shortcut sync**: Master ratio and count changes from shortcuts now persist to config.
- **Settings stability**: Single-instance enforcement, daemon toggle desync from broken QML bindings, RenderingBackend read from ungrouped config keys.
- **i18n**: Correct plural form selection in `i18np` without `.ts` files.
- **Layout picker**: No longer overwrites the global default layout when selecting per-screen.

## [2.5.3] - 2026-04-01

### Fixed
- **Autotile split ratio feedback loop** ([#273]): The KWin effect reported the window's actual frame geometry as its "minimum size" when a window resisted shrinking (e.g., during browser media loading). This inflated min-size was never cleared, creating a feedback loop that expanded the master zone to ~90% or full width. Now uses the window's declared `minSize()` from the compositor, with zone-size fallback for apps without declared minimums. Daemon caps all received min-sizes at 90% of screen dimension as a secondary safety net.
- **Overlay/popup focus guard for autotile** ([#272]): Windows with `keepAbove` set (Spectacle, color pickers, screen rulers) were entering the autotile tree and stealing focus when `focusNewWindows` was enabled. Added `keepAbove()` check to `isTileableWindow()` so overlay/utility tools are excluded from autotiling.
- **"Hold to activate" trigger reset on daemon restart** ([#275]): Qt's QConfFile cache was shared between the Settings and LayoutManager backends (same file, same format). When `reparseConfiguration()` destroyed the Settings QSettings, the LayoutManager's instance kept the stale QConfFile alive, preventing re-read from disk. Settings changed by the external settings app were invisible to the daemon's reload, silently reverting `DragActivationTriggers` to the default (Alt). Now reads the file directly and overwrites the QSettings cache.

## [2.5.2] - 2026-03-31

### Fixed
- **Layout popup algorithm previews**: Algorithm previews in the zone selector and layout picker popup now respect algorithm metadata (zone number display mode, producesOverlappingZones, master indicator dots) like the settings app does.
- **Window picker exclusion lists**: WindowPickerDialog was shadowing the `appSettings` context property, causing addExcludedApplication/addExcludedWindowClass to silently fail. Now routes through settingsController.settings.
- **Autotile split ratio state corruption**: Reset suspiciously high split ratios (> max - 0.05) on state load to prevent layouts from being stuck with unusable splits.
- **Autotile cursor hover focus**: The hover-to-focus check in AutotileHandler was using the old `shouldHandleWindow` method; now uses `isTileableWindow` for consistency.

## [2.5.1] - 2026-03-30

### Added
- **Independent autotile sticky window handling**: Separate setting for how autotiling handles sticky windows (on all desktops), independent from the snapping setting. Configurable in Tiling > Behavior.

### Fixed
- **Editor shader crash**: Null pointer dereference in `ZoneShaderNodeRhi::render()` when switching between multipass shaders — missing null check on `m_multiBufferTextures[i]`.
- **Editor undo crash**: Guard `m_undoController` dereferences in `setCurrentShaderParams()`, `setShaderParameter()`, `resetShaderParameters()`, and `switchShader()` to match existing pattern.
- **Arch packaging**: PKGBUILDs referenced `kbuildsycoca.hook` and `plasmazones-refresh-sycoca` as standalone source files instead of using in-tree paths, causing `makepkg` to fail with "cannot stat" errors.

## [2.5.0] - 2026-03-30

### Added
- **Scripted tiling algorithms** ([#256], [#259]): All 24 tiling algorithms are now JavaScript, running in a sandboxed QJSEngine with hot-reload. The 15 former C++ algorithms have been converted to JS with identical behavior. Six new algorithms added: Cascade, Corner Master, Floating Center, Horizontal Deck, Paper, and Stair. Custom user algorithms are loaded from `~/.local/share/plasmazones/algorithms/`.
- **Dwindle (Memory) algorithm**: Dwindle variant with a persistent split tree — resizing one tile does not affect others. Split positions survive window close/reopen.
- **Multi-compositor support** ([#261]): Custom `pz-layer-shell` QPA plugin replaces the `LayerShellQt` dependency. PlasmaZones now works on any Wayland compositor with `zwlr_layer_shell_v1` support (Hyprland, Sway, Wayfire, niri, COSMIC, river, labwc).
- **Vulkan rendering backend** ([#264]): Optional Vulkan backend for zone overlay rendering with automatic fallback to OpenGL on unsupported hardware or driver crash. User-selectable in **Settings → General**.
- **New Layout / New Algorithm wizards** ([#263]): Guided dialogs for creating zone layouts from templates and tiling algorithms from a starter script, accessible from the settings app.
- **Layout filter bar** ([#265]): Replace hardcoded layout groups with configurable group-by, sort-by, and filter controls in the layout list.
- **Disable per virtual desktop / activity** ([#260]): Disable PlasmaZones on specific virtual desktops or activities per screen. Overlay hides automatically on disabled contexts.
- **Settings UX polish** ([#268], [#269]): Two-line description rows, collapsible card sections, consolidated footer bar with Apply/Reset/Defaults, sidebar page badges, inline toggle switches replacing disable checkboxes.
- **Single-instance settings app** ([#270]): `plasmazones-settings` is now single-instance via D-Bus. Launching it again raises the existing window and navigates to the requested page (`--page <name>` / `-p <name>`).
- **Unsaved changes confirmation**: Settings app prompts before closing with unsaved changes; Reset and Defaults buttons require confirmation.
- **Master indicator dots**: Algorithm grid cards show dots indicating which zones are master positions.
- **Memory indicator icon**: Stateful algorithms (Dwindle Memory) show a persistence icon in the algorithm selector.
- **Per-algorithm settings storage**: Split ratio, master count, and other parameters are saved per-algorithm rather than globally.
- **Algorithm import and open folder**: Import `.js` algorithm files and open the user algorithm directory from the tiling settings page.
- **Algorithm capability grouping**: Tiling algorithms grouped by capability (Built-in / Extras / Custom) in the settings UI.
- **D-Bus `zoneIds` array**: Window state responses now include the full list of zone IDs a window occupies.

### Changed
- **LayerShellQt replaced**: Custom `pz-layer-shell` QPA plugin is now the sole layer-shell backend. Packagers: drop `layer-shell-qt` build dependency, add `qt6-wayland` and `wayland-scanner`.
- **Config keys centralized** ([#266]): All config group names and key strings extracted to `ConfigDefaults` accessors — no more inline string literals in settings code.
- **Settings page IDs renamed**: `snap-*` / `tile-*` page IDs renamed to `snapping-*` / `tiling-*` for consistency.
- **Algorithm registry**: Hardcoded algorithm ID constants replaced with a data-driven registry. Algorithm metadata (name, capabilities, flags) comes from JS `@tag` annotations.
- **README streamlined**: Detailed algorithm table, shader table, D-Bus API reference, and project structure moved to the wiki. README reduced from 742 to 593 lines with summary + wiki links.

### Removed
- **LayerShellQt dependency**: No longer required — replaced by `pz-layer-shell` QPA plugin.
- **C++ tiling algorithm implementations**: All algorithms are now JavaScript. The C++ implementations have been removed.
- **Legacy migration code**: Removed all config key migration and backward-compatibility shims from settings save/load paths.
- **kcfg/kcfgc schema files**: Unused KConfig schema files removed.
- **Accent stripe feature**: Removed from `SettingsCard` component.

### Fixed
- **Window restore on daemon restart**: Bypass QSettings cache in `loadState` so window-to-zone mappings are read fresh from disk after daemon restart ([#268]).
- **Multipass shader rendering**: Fix ping-pong buffer handling in overlay renderer for multi-pass shaders.
- **Editor shader menu crash**: Rewrite shader submenu to prevent Qt 6 `finalizeExitTransition` use-after-free when selecting a shader while the menu is animating closed.
- **Editor context menu crash**: Use shared context menu instance to prevent `QQmlData` use-after-free when zones update while the popup is open.
- **NVIDIA Vulkan crash**: Suppress spurious shutdown crash on NVIDIA drivers and auto-fallback to OpenGL.
- **NVIDIA EGL crash in editor**: Guard shader preview reload against EGL context loss on NVIDIA.
- **Layout picker dimmed backdrop**: Remove unintended dimmed background overlay from the fullscreen layout picker.
- **CAVA crash suppression**: Suppress misleading "CAVA Crashed" error message caused by process-group SIGTERM during daemon shutdown.
- **Shader preview bindings**: Restore reactive label and wallpaper texture bindings in the editor shader preview.
- **Settings dirty flag on navigation**: Prevent spurious unsaved-changes prompts when switching between settings pages.
- **Slider snap-back bug**: Fix master count control snapping back to previous value; replace SpinBox with SettingsSlider.
- **Aspect ratio menu**: Replace flat menu items with nested submenu for aspect ratio presets.
- **Layer-shell window recovery**: Recover shader preview when the Wayland `LayerSurface` is unexpectedly destroyed.

### Migration Notes (Packagers)
- Drop `layer-shell-qt` / `liblayershellqtinterface-dev` build dependency
- Add `qt6-wayland` / `qt6-wayland-dev` build dependency
- Add `wayland-scanner` build dependency (usually in `wayland-devel`)
- Add `vulkan-headers` and `vulkan-loader` build dependencies (optional, for Vulkan backend)

## [2.4.7] - 2026-03-27

### Added
- **Center-distance zone selection for overlapping zones** ([#258]): When zones overlap (e.g. quadrants + halves + fullscreen), the zone whose center is closest to the cursor now wins instead of always picking the smallest zone. This lets users reach background zones by dragging toward their center — matching the FancyZones behavior. The multi-zone span path is unaffected, preserving the [#211] fix.

### Fixed
- **Window picker inserts unmatchable values** ([#251]): "Pick from running windows" in App Rules and Exclusions inserted raw X11 window class format (e.g. `"signal signal"`) instead of the normalized form used for matching (`"signal"`). Manually typing the name worked; using the picker did not.
- **Keyboard shortcuts move excluded windows** ([#251]): Move, Push, and Swap keyboard shortcuts ignored exclusion rules, moving the window behind an excluded app instead of doing nothing. All navigation shortcuts now check exclusions consistently.
- **Drag-out unsnap doesn't clear persisted zone** ([#251]): Dragging a window out of its zone and closing it would still persist the zone, causing the window to snap back on reopen. The floating state flag was not always set due to an overly strict guard condition.
- **D-Bus zone detection missing geometry recalculation**: `detectMultiZoneAtPosition` did not call `recalculateZoneGeometries` before detection, potentially using stale zone coordinates.

## [2.4.6] - 2026-03-27

### Added
- **Plasma Sigil shader**: Animated energy sigil based on the PlasmaZones icon with glowing rune effects.

### Fixed
- **System Settings crash when opening PlasmaZones KCM**: The KCM linked the entire `plasmazones_core` library (with layer-shell QPA plugin, PlasmaActivities, 21 static initializers) just to read a version string. When the daemon was not running this caused heap corruption and SIGABRT during QML binding creation. Replaced with a compile-time version define — the KCM no longer loads the core library at all.
- **Editor context menu crash on zone updates**: Use shared context menu to prevent QQmlData use-after-free crash when zones update while the menu is open.

## [2.4.5] - 2026-03-26

### Added
- **SVG support for shader textures**: SVG/SVGZ files can now be used as user texture parameters in shaders, rasterized at configurable resolution (64–4096px, default 1024) via `QSvgRenderer`. An inline resolution spinbox appears in the shader settings UI when an SVG is selected.

### Fixed
- **Exclusions UI: can't add new entries** ([#251]): The QML JS array mutation pattern (`slice()` + `push()` + reassign) silently fails in Qt 6.10 due to `QStringList`↔JS Array round-trip type confusion. Replaced with `Q_INVOKABLE` C++ methods that modify the `QStringList` directly and emit proper `NOTIFY` signals.
- **Excluded app keyboard shortcuts: no feedback** ([#251]): Snap-to-zone shortcuts blocked by exclusion rules now emit OSD feedback instead of failing silently.
- **Neon Phantom shader white-out**: Reduced brightness multipliers and widened energy smoothstep range to prevent the effect from blowing out to featureless white at high energy accumulation.

## [2.4.3] - 2026-03-26

### Fixed
- **Identical monitors showing as duplicates in settings** ([#252]): Two monitors with the same EDID (manufacturer/model/serial) got the same screen ID, causing the settings UI to show the primary monitor twice and tiling/snapping to only work on one monitor. Screen IDs now append `/ConnectorName` when duplicates are detected, with backward-compatible fallback matching for saved configs.
- **App-to-Zone rules not working** ([#254]): Rule matching used raw substring comparison that failed when appId format differed from user input (e.g. "firefox" vs "org.mozilla.firefox"). Replaced with `appIdMatches()` — segment-aware dot-boundary matching that handles both directions and partial last-segment prefixes.
- **Exclusions ignored by auto-snap and keyboard shortcuts** ([#254]): The exclusion settings interface existed but was never checked. Added exclusion gates in both the auto-snap chain (`resolveWindowRestore`) and keyboard shortcut path (`snapToZoneByNumber`).
- **Unsnapped windows re-snap on reopen** ([#254]): Manually unsnapping a window didn't clear its pending restore entry, so closing and reopening it snapped it back. Now consumes the pending entry on unsnap (multi-instance safe).
- **Drag-out unsnap doesn't restore window size** ([#254]): The geometry validation path didn't pass the release screen ID, causing cross-screen coordinate validation to fail silently. Also fixed premature pre-tile geometry cleanup that prevented later float-toggle restore.
- **Render node use-after-free during hot-reload**: The scene graph render thread could dereference a dangling `QQuickItem` pointer after shader hot-reload when `bufferFeedback` (ping-pong) was active. Added atomic invalidation flag with acquire/release ordering.

### Added
- **Ember Trace shader**: Fractal fire patterns via ping-pong feedback buffer — the first shader to use the `bufferFeedback` feature. Zone borders emit flames that spiral inward via feedback zoom, with 7 layered visual systems including reaction-diffusion-like dynamics, curl-noise advection, and per-band audio (bass eruption shockwaves, mids feedback phase shift, treble turbulent mixing).
- **Neon Phantom shader**: Neon-lit cyberpunk zone overlay.

## [2.4.2] - 2026-03-25

### Fixed
- **Multi-zone span pulls in background zones** ([#249]): Spanning adjacent sub-zones (e.g. zones 7 & 9) while a larger zone existed underneath incorrectly included the background zone, making the window much larger than intended. Fixed both proximity-snap and paint-to-span code paths to exclude background/overlay zones from the span.
- **Settings UI not refreshing when daemon starts from toggle**: UI data now reloads when the daemon is started via the settings toggle.

## [2.4.1] - 2026-03-25

### Fixed
- **Edge threshold resets to 100px** ([#237]): The UI allowed up to 500px but the C++ setter/loader still clamped to 100. Values above 100 were silently clamped back on save. Also fixed the `.kcfg` schema max to match.
- **Per-screen autotile split ratio using wrong default**: Per-screen overrides fell back to the algorithm default (0.6) instead of the config default (0.5) when no override was stored.
- **Shortcuts KCM shows "plasmazonesd" with no icon**: Added KGlobalAccel component desktop file and restored `setApplicationDisplayName`/`setWindowIcon` lost during KAboutData removal.
- **Retile shortcut conflicts with Spectacle**: Changed default from Meta+Shift+R (Spectacle rectangular region capture) to Meta+Ctrl+R.

### Changed
- **ConfigDefaults is now the single source of truth** for all setting defaults, min/max bounds, and shortcut defaults. Previously duplicated across `configdefaults.h`, `settings.h`, `setters.cpp`, `loadsave.cpp`, `perscreen.cpp`, QML pages, `.kcfg`, and tests — now every consumer references `ConfigDefaults`. Changing a bound or default requires editing exactly one place.
- **Settings bounds exposed to QML** via `SettingsController` constant Q_PROPERTYs. All QML `from:`/`to:` values reference the controller instead of hardcoded literals.
- **Removed dead code**: Unused `ZoneSelectorCard.qml`.
- **Added `FilterLayoutsByAspectRatio` to `.kcfg`**: Was implemented in code but missing from the schema.
- **Editor defaults consolidated**: Previously hardcoded in `loadsave.cpp`, now in `ConfigDefaults`.

## [2.4.0] - 2026-03-25

### Added
- **KZones layout import** ([PR #244]): Import zone layouts from KZones configuration files.
- **Aspect ratio layouts** ([PR #242]): Auto-detect monitor aspect ratio, editor selector for ratio-specific layouts, and filter setting for the layout grid.
- **Portable Wayland build** ([PR #231]): `USE_KDE_FRAMEWORKS=ON/OFF` CMake option. Pluggable backends for config (`IConfigBackend`), shortcuts (`IShortcutBackend`), wallpaper (`IWallpaperProvider`), and i18n (Qt Linguist). Runs on non-KDE Wayland compositors.
- **Standalone settings app** ([PR #238]): `plasmazones-settings` with KCM-style sidebar drill-down, subpages, visual monitor selector, search, keyboard shortcuts overlay, unsaved-changes indicator, and DPI-aware animations. Meta+Shift+P shortcut to launch.
- **D-Bus API audit** ([PR #246]): New `CompositorBridge`, `Control`, and `Shader` interfaces. Convenience methods and full API specification. Unit tests for all new methods.
- **Arch Drift branded shader**: Terminal rain columns, isometric chevron grid, traveling data packets, and CRT scan lines. 95-vertex SDF polygon from the official Crystal SVG.
- **EndeavourOS Drift branded shader**: Constellation network background with 24 dots on Lissajous orbits, warm gradient wash, and tri-sail SDF logo (58 vertices total).
- **Wind currents + sails shader**: Replaced constellation network with flowing wind current effect.

### Changed
- **KWin effect reduced to thin interface layer** ([PR #245]): Effect code is now a minimal bridge between KWin and the daemon over D-Bus. All tiling/snapping logic lives in the daemon.
- **Removed unused `LayoutType` enum**: Cleaned up `Layout` model — the `type` field was never used.
- **Dropped `IConfigBackend` interface indirection**: `QSettingsConfigBackend` used directly after KConfig removal stabilized.
- **Removed KConfig dependency from test suite**: Tests use the standalone config backend.
- **Removed daemon toggle from KCM**: Moved `DaemonController` to `src/common`.

### Fixed
- **Synchronous D-Bus calls freeze compositor**: Eliminated blocking D-Bus calls in the KWin effect that caused compositor hangs.
- **Qt6 SIGSEGV in context menu**: Moved context menu outside `Loader` to avoid crash.
- **Qt6 crash in aspect ratio submenu**: Flattened submenu to avoid nested popup crash.
- **Tooltip flickering on layout cards in Flow layout** ([#235]): Fixed hover handler conflicts.
- **Zone previews clamped for fixed-geometry layouts**: Preview rendering no longer overflows card bounds.
- **Edge threshold max increased**: From 100px to 500px.
- **Settings live lock sync and screenId resolution**: OSD lock state now syncs immediately.
- **EndeavourOS Drift GLSL type errors**: Fixed vec2/float mismatches and wired dead parameters.
- **Shader file watcher dropped after cmake install**: Re-watches directories after inode replacement.
- **Layout card icon hover flicker** ([#235]): Replaced `HoverHandler` with `MouseArea` to fix grab conflicts.

## [2.3.16] - 2026-03-21

### Fixed
- **Layout card button flicker on hover** ([#235]): The Auto-assign and Visibility toggle buttons flickered when hovered. Two cooperating causes: (1) the right-anchored Row reflowed leftward when buttons toggled `visible:`, shifting button positions and destabilizing hover; (2) `ToolTip.visible: hovered` with no delay opened a popup that stole pointer focus on some compositors. Wrapped each ToolButton in a fixed-size Item to eliminate geometry reflow and added `ToolTip.delay` to break the feedback loop.
- **Zone selector grid ignoring columns/rows at fixed preview sizes**: The `maxRows` setting was only applied when preview size was "Auto". Fixed to apply for all size modes in Grid layout.

### Changed
- **Improved zone selector edge scrolling**: Widened auto-scroll trigger zone from 32px to 48px and increased max scroll speed by 75% for more responsive scrolling during window drag.
- **Zone selector scroll D-Bus API**: Added `selectorScrollWheel` D-Bus method and `applyScrollDelta` QML function for programmatic scrolling. Infrastructure is wired and ready for when KWin exposes pointer axis events to effects.

## [2.3.15] - 2026-03-21

### Fixed
- **Shader preview crash on hover** ([#235]): Moving the mouse over the shader preview in the editor dialog crashed with SIGSEGV in `QV4::Lookup::getterQObject`. The `MouseArea.onPositionChanged` handler's QObject-backed event parameter was invalidated mid-evaluation by cascading signal chains on Qt 6.10. Replaced `MouseArea` with `HoverHandler` which uses a value-type point property.
- **CAVA settings not applied dynamically**: Enabling/disabling CAVA audio visualizer or changing bar count/sample rate in KCM required a daemon restart. The KCM's batch `setSettings` applied values with `QSignalBlocker`, then `load()` saw no change (in-memory values already updated). Added `syncCavaState()` called from `updateSettings()` on every settings reload.
- **Layout card button flicker at fractional DPI**: The Auto-assign and Visibility toggle buttons flickered when hovered at 125% display scaling. The `visible:` property toggling caused Row geometry reflow that shifted button positions by sub-pixel amounts, creating a hover feedback loop. Replaced `visible:` with `opacity:`/`enabled:` and added `ToolTip.delay`.

### Changed
- **Enhanced label effects for branded shaders**: CachyOS, Fedora, Neon, and NixOS Drift shaders had label text bodies that appeared solid white. Text fill patterns used screen-space UV which barely varied within characters, and `smoothstep(0.3, 0.9, labels.a)` washed to white. Rewrote all four with pixel-space patterns, edge rim detection, and `x/(0.6+x)` tonemapping. Each shader gets a unique style: digital shatter (CachyOS), frost crystalline (Fedora), neon tube flicker (Neon), hash grid verification (NixOS).

## [2.3.14] - 2026-03-21

### Fixed
- **Flickering icons on layout card hover** ([#235]): The visibility and auto-assign toggle icons in the KCM layouts grid flickered when hovering over them. The `ToolButton` controls stole hover from the underlying `MouseArea`, causing a containsMouse feedback loop. Replaced `MouseArea` hover tracking with a `HoverHandler` which doesn't lose hover state when child controls intercept mouse events.

## [2.3.13] - 2026-03-21

### Fixed
- **Resnap buffer empty on layout change**: Cycling layouts, using the layout picker, or selecting from the zone selector never resnapped windows to the new layout's zones. `UnifiedLayoutController::applyEntry()` blocks `activeLayoutChanged` via `QSignalBlocker`, which prevented `onLayoutChanged()` from populating the resnap buffer. All layout change paths now explicitly populate the buffer via `populateResnapBufferForAllScreens()` before resnapping.
- **Per-screen mode toggle and layout cycle scoped to target screen**: Mode toggle (autotile/snapping) and layout cycling now correctly operate on the focused screen only, rather than affecting the global active layout state for all screens.

### Changed
- **Unified layout change resnap path**: Layout picker, zone selector, cycle, and quick-layout shortcuts all route through `resnapIfManualMode()` instead of using separate inline resnap logic. Eliminates duplicate code and ensures consistent resnap behavior across all layout change entry points.
- **Renamed screenName variables to screenId/connectorName**: Internal refactor for consistent naming of screen identifiers throughout the codebase.

## [2.3.12] - 2026-03-21

### Fixed
- **EDID serial mismatch between effect and daemon**: KWin 6's `Output::serialNumber()` returns the EDID text serial descriptor (e.g. `HNTY800697`), while Qt's `QScreen::serialNumber()` returns the EDID header serial (e.g. `810700097`). This caused the daemon to fail screen lookups for IDs received from the KWin effect. `findScreenByIdOrName` now falls back to manufacturer + model matching when serials differ and there's exactly one screen of that make/model.

## [2.3.11] - 2026-03-20

### Fixed
- **Move/Swap hotkeys on dual same-model monitors**: On setups with two identical-model monitors (e.g. dual Samsung Odyssey G93SC with different serials), KWin's `EffectWindow::screen()` can return the wrong output. Navigation hotkeys now trust the daemon's stored screen assignment for snapped windows (set at snap time, always correct) and fall back to the effect-provided screen only for unsnapped windows. Cross-screen moves are handled by `outputChanged` → `windowScreenChanged` which unsnaps the window before navigation fires.

## [2.3.10] - 2026-03-20

### Fixed
- **Move/Swap hotkeys use wrong screen's layout**: When a window was moved between monitors externally (KDE's Move-to-Screen shortcut, manual drag racing with `outputChanged`), keyboard navigation hotkeys used the stored screen assignment instead of the window's actual screen, snapping the window back to the old monitor or computing zone geometry from the wrong layout. Now always uses the effect-provided screen (`EffectWindow::screen()`) and detects stale assignments: if the stored screen differs from the actual screen, treats the window as unsnapped and navigates from scratch on the correct screen's layout.

## [2.3.9] - 2026-03-20

### Fixed
- **Modifier key settings zeroed on save**: Changing the activation modifier in the KCM and saving silently wrote all-zero triggers to KConfig, permanently disabling zone activation. Complex D-Bus types (`QVariantList` of `QVariantMap`s) arrived as `QDBusArgument` objects that `toList()`/`toMap()` couldn't extract. Now applies `DBusVariantUtils::convertDbusArgument()` in both `setSetting()` and `setSettings()` before passing values to setters. Affects `dragActivationTriggers`, `zoneSpanTriggers`, and `snapAssistTriggers`.

## [2.3.8] - 2026-03-19

### Added
- **Batch settings D-Bus method**: New `setSettings(QVariantMap)` method applies multiple settings in one D-Bus call with a single KConfig save. Complete settings registry with 87 entries covering all autotile, zone selector, shortcut, and behavior settings.
- **Per-screen settings D-Bus methods**: New `setPerScreenSetting`/`clearPerScreenSettings`/`getPerScreenSettings` D-Bus methods for autotile, snapping, and zone selector categories. Per-screen calls use async D-Bus to avoid blocking the KCM UI during slider interactions.
- **Zone previews in autotile dropdowns**: Autotile algorithm dropdowns now show layout thumbnails with zone previews, matching the snapping layout dropdown UX.

### Changed
- **Daemon is sole KConfig writer**: All KCM settings writes now route through D-Bus to the daemon. No KCM sub-page calls `m_settings->save()` directly. Eliminates dual-writer race conditions between the KCM and daemon.
- **Batch signal suppression**: `setSettings()` wraps setter calls in `QSignalBlocker` to prevent N intermediate `settingsChanged` emissions mid-batch. The KCM's `notifyReload()` triggers a single `settingsChanged` with all values committed.

### Fixed
- **Mode toggle affects all monitors**: `Meta+Shift+T` autotile toggle now only affects the focused screen's context. Previously, `seedAutotileOrderForScreen` and the resnap-from-autotile-order loop processed all screens instead of the shortcut's target screen.
- **Clearing autotile algorithm to "Use default" switches to snapping mode**: `activeLayoutId()` now returns `"autotile:"` (recognized by `isAutotile()`) when mode is Autotile with empty algorithm, instead of returning empty string which caused `updateAutotileScreens` to drop the screen from autotile.
- **showBorder and hideTitleBars are independent toggles**: Turning off borders no longer forces title bars to reappear. Turning on hideTitleBars now immediately hides title bars on all currently tiled windows instead of waiting for the next retile.
- **KCM save ordering for per-screen overrides**: `setDaemonSettings()` now runs before `m_settings->save()` so per-screen overrides written by the KCM are not overwritten by the daemon's stale values.
- **Navigation fallback screen**: Fall back to the KWin effect's screen when the daemon's stored screen assignment is gone, preventing navigation failures after monitor reconfiguration.

## [2.3.7] - 2026-03-19

### Fixed
- **Windows unsnap when monitor enters standby**: When a monitor (e.g., a TV) entered standby or disconnected, KWin reassigned orphaned windows to remaining outputs, firing `outputChanged` signals. The effect interpreted these as cross-screen moves and told the daemon to unsnap every affected window. Now suppresses `windowScreenChanged` when the old screen has disappeared from KWin's output list or a screen geometry change is in progress.

## [2.3.6] - 2026-03-19

### Fixed
- **Navigation shortcuts snap to wrong screen on multi-monitor**: MoveWindow and SwapWindow keyboard shortcuts computed zone geometry for the wrong screen when KWin's `EffectWindow::screen()` disagreed with the daemon's stored screen assignment (common with similarly-named monitors like dual Samsung Odyssey G93SC ultrawides). The window would land on a third screen and immediately unsnap. Navigation targets now use the daemon's authoritative stored screen assignment, and the effect reads the corrected screen from the daemon's response.

### Added
- **KDE dependency migration plan**: `docs/kde-dependency-migration.md` documents how to make the daemon portable across Wayland compositors (GNOME, Hyprland, Sway) while retaining full KDE integration, behind a `USE_KDE_FRAMEWORKS` CMake option.

## [2.3.5] - 2026-03-19

### Fixed
- **KCM assignment save rewrites modes**: The KCM decomposed per-screen AssignmentEntry (mode + snappingLayout + tilingAlgorithm) into two flat maps and merged them on save — losing mode information, reverting layouts after Apply, and corrupting autotile/snapping mode when editing the other mode's field. Replaced with per-entry D-Bus writes via new `setAssignmentEntry` method that preserves all fields independently.
- **Clearing per-desktop override inherits base autotile mode**: Setting a per-desktop layout to "Use default" removed the entire entry, inheriting the base screen's mode (often autotile). Now keeps a mode-only marker entry that preserves the desktop's mode while cascading layout resolution to the parent scope.
- **Monitor-level layout changes revert after Apply**: The batch `setAllScreenAssignments` D-Bus method used `fromLayoutId(id, existing)` which preserved the old mode instead of setting it from the layout ID type. Combined with the merge logic sending the wrong mode's ID, screens appeared to revert.
- **KCM combo shows resolved layout instead of "Use default"**: After clearing a per-desktop assignment, the combo showed the inherited layout name instead of "Use default" because the D-Bus echo signal overwrote the KCM cache with the cascaded base layout. Added `setSaveBatchMode` to suppress `screenLayoutChanged` signals during the entire save batch.
- **Layout cascade stops at mode-only entries**: `layoutForScreen` returned nullptr immediately when finding a mode-only entry (empty snapping in Snapping mode) instead of cascading to the parent scope. Now continues cascading through mode-only entries to find the effective layout.

### Added
- **Resnap/retile on KCM assignment changes**: Changing layouts via KCM assignments now triggers window resnap (snapping) or retile (autotile) with per-screen OSD, matching the behavior of the layout picker overlay and keyboard shortcuts. Uses dedicated `applyAssignmentChanges` D-Bus method to avoid feedback loops with the settings handler.
- **Per-screen resnap buffer**: New `populateResnapBufferForAllScreens` method builds resnap data using a global zoneId-to-position map from all loaded layouts, independent of the single global active layout. Supports multi-monitor setups where each screen has a different layout assignment.
- **Synchronous notifyReload**: KCM-to-daemon settings reload is now synchronous with `m_ignoreNextSettingsChanged` flag, preventing race conditions where the daemon's queued `settingsChanged` signal would trigger a spurious reload that reverts just-saved assignments.

### Changed
- **Assignment edits never change mode**: Selecting a snapping layout or tiling algorithm in the KCM only updates that field — mode is controlled exclusively by the daemon through global snapping/autotile toggle, not by individual assignment edits.
- **Full AssignmentEntry tracking in KCM**: Replaced redundant flat pending maps (`m_pendingDesktopAssignments`, `m_pendingActivityAssignments`) with full `AssignmentEntry` pending maps that track mode + snappingLayout + tilingAlgorithm per context.
- **Field-level clearing**: Clearing a snapping layout only clears the `snappingLayout` field, preserving mode and `tilingAlgorithm`. Prevents unintended mode inheritance from parent scopes.

## [2.3.3] - 2026-03-17

### Fixed
- **Autotile not activating when snapping disabled first**: When users disabled snapping (Apply) then enabled autotiling (Apply) in separate steps, per-screen autotile assignments were never created — the activation guard required both changes in the same settings event. Windows fell through to fullscreen stacking instead of tiling. Now also fires when autotile is toggled on while snapping is already off.

## [2.3.2] - 2026-03-17

### Changed
- **Batched D-Bus signals for autotile transitions**: Overflow float notifications, resnap snap confirmations, and window-opened announcements are now batched into single D-Bus calls instead of per-window round-trips. On a 15-window autotile toggle this reduces D-Bus messages from ~45 to 3, eliminating compositor-thread stalls during mode switches and daemon restarts.

## [2.3.1] - 2026-03-17

### Fixed
- **Window state lost on daemon reload**: Zone assignments were purged during `loadState()` when the saved active layout differed from the default layout. Restoring the active layout emitted `activeLayoutChanged` before `currentVirtualDesktop` was set, causing `onLayoutChanged()` to resolve effective layouts against the wrong desktop and fall back to `defaultLayout()` — whose zones didn't match the saved assignments. Fixed by suppressing the signal during state restoration.
- **Screen not found on Wayland (hex serial mismatch)**: The daemon's `screenIdentifier()` used `QScreen::serialNumber()` as-is, but on KDE Plasma Wayland this returns the EDID header serial in hex (e.g., `"0x0001C1A3"`). The KWin effect already normalized to decimal (`"115107"`), causing screen ID mismatches across the D-Bus boundary. Both sides now produce identical decimal serials.
- **Screen not found from KCM queries ([#223])**: `getScreenInfo()` only matched screens by connector name (`"eDP-1"`), but `getScreens()` returns EDID-based screen IDs (`"Sharp Corporation:LQ134N1JW53"`). Every KCM screen info query failed, causing autotile assignments to revert and persistent "screen not found" errors on multi-monitor setups. Now accepts both connector names and screen IDs.
- **Autotile window order lost on rapid mode toggle**: When `setInitialWindowOrder()` was called twice for the same screen, the first call's 10-second safety timer would fire and remove the second call's pending order. Added a generation counter so stale timers from superseded calls become no-ops.
- **KCM fallback screen IDs inconsistent**: When the daemon was unavailable, the KCM screen provider fell back to connector names instead of EDID-based screen IDs, causing per-screen config mismatches. Now uses `Utils::screenIdentifier()` in the fallback path.

## [2.3.0] - 2026-03-17

### Added
- **Stable EDID-based screen identifiers**: All screen identification now uses stable EDID-based IDs (e.g., `LG Electronics:LG Ultra HD:115107`) instead of connector names (`DP-2`). Monitors survive replug/reboot without losing layout assignments.
- **Per-screen layout filtering**: Layout cycle shortcuts, layout picker popup, and zone selector now filter layouts based on the focused screen's mode. Snapping screens only show zone layouts; autotile screens only show tiling algorithms.
- **Per-screen layout locking**: Lock the current layout or tiling algorithm per screen/desktop/activity context. Prevents accidental changes from layout cycling, zone selector, or keyboard shortcuts. Toggle with `Meta+Ctrl+L` shortcut. OSD notification shows lock/unlock state.
- **Per-mode context-aware locking**: Locking is mode-aware — locking on a snapping screen locks the zone layout, locking on an autotile screen locks the tiling algorithm. Each screen/desktop combination has independent lock state.
- **Shader parameter locking**: Lock individual shader parameters in the editor to preserve their values during randomize. Locked parameters show a lock icon and are excluded from random generation.
- **Quick Layout slot labels**: KCM now shows "Quick Layout 1" / "Quick Tiling 1" instead of raw shortcut keys (`Meta+Alt+1`). Shortcut key shown as secondary text.
- **Hot reload for shaders**: System and user shaders reload automatically on file changes.

### Fixed
- **Snap assist sending windows to wrong monitor**: On dual-monitor setups sharing the same layout, zones occupied on one screen appeared occupied on the other. Empty zone detection is now per-screen.
- **Cross-screen drag not clearing snap/float state**: Dragging a snapped window to a different monitor now clears the zone assignment and pre-tile geometry. Float toggle no longer restores to the original monitor.
- **Window restore unsnapping on cross-screen restore**: Windows persisted on a secondary monitor would immediately unsnap after restore. The daemon now decides whether to unsnap based on its own assignment state.
- **Snap assist flashing during rapid layout cycling**: Snap assist continuation now runs after stagger animations complete, with a generation counter to invalidate stale callbacks from previous layouts.
- **Zone selector showing on autotile-managed screens**: The zone selector popup now correctly skips screens in autotile mode.

### Changed
- **Screen ID migration**: 34+ files refactored to use EDID-based screen IDs consistently across the entire effect↔daemon D-Bus boundary, autotile engine, snap engine, and window tracking service.
- **Shader categories from metadata**: Removed hardcoded category translations — category names come directly from shader `metadata.json`.
- **German translations**: All three domains 100% complete (KCM: 529, Editor: 449, Daemon: 61).

## [2.2.1] - 2026-03-14

### Fixed
- **Primary screen detection on Wayland**: `QGuiApplication::primaryScreen()` could diverge from KDE Display Settings on some multi-monitor Wayland configurations. The KWin effect now queries `Workspace::outputOrder()` (the compositor's authoritative output priority) and pushes the true primary to the daemon via D-Bus. Updates automatically when display settings change.

### Changed
- **Complete German translations**: All three translation domains are now 100% translated (KCM: 520, Editor: 446, Daemon: 61 strings).
- **Translation extraction fix**: Sub-KCM QML files were missing from pot extraction (glob only scanned `kcm/ui/`, not `kcm/*/ui/`).

## [2.2.0] - 2026-03-14

### Added
- **Independent border and title bar toggles** (fixes [#210](https://github.com/fuddlesworth/PlasmaZones/discussions/210)): New "Show focus border" setting draws a colored border around the focused tiled window without requiring title bars to be hidden. Border width, corner radius, and color are configurable independently.
- **Border corner radius setting**: New corner radius option (0–20px) for the autotile focus border. For borderless windows, the window content is clipped to match the rounded border.
- **Right-click context menu for layouts**: Edit, Set as Default, Show/Hide from Zone Selector, Enable/Disable Auto-assign, Duplicate, Export, and Delete actions are now accessible via right-click on layout cards. The toolbar has been simplified to New Layout, Import, Open Folder, and view switching.
- **Monitor selector for layout editor**: Layouts KCM now shows a screen selector (multi-monitor setups) so the editor opens on the correct monitor instead of always using the first screen.

### Fixed
- **Double toggle-float on multi-monitor setups**: Navigation signal connections were registered twice (constructor + daemon ready), causing float toggle and other shortcuts to fire their handlers twice and immediately cancel themselves.
- **Snap-mode zone changes leaking into autotile engine**: The autotile engine's `windowZoneChanged` listener incorrectly called `onWindowAdded()` for snap-mode windows, triggering "not in m_windowToStateKey" warnings and potentially inserting windows into the wrong screen's tiling state.
- **Window tiling state preserved when moved to another desktop**: Moving a tiled window to a different virtual desktop now properly removes it from the source desktop's tiling and retiles to fill the gap. Title bars are restored for borderless windows.
- **Editor opens on wrong monitor** (fixes [#216](https://github.com/fuddlesworth/PlasmaZones/discussions/216)): The Layouts KCM always sent the editor to the first screen in the list. Now uses user selection, falling back to the primary monitor.
- **"Open Layouts Folder" error on fresh install** (fixes [#216](https://github.com/fuddlesworth/PlasmaZones/discussions/216)): The layouts directory is now created if it doesn't exist before opening.
- **Layout grid not refreshing after duplicate/delete/import**: The daemon never emitted layout change D-Bus signals, so the KCM grid relied on manual reloads. Added `scheduleLoad()` after all mutating operations.
- **QML compiler warning in layout editor**: Fixed `threshold` variable used before declaration in `DividerManager.qml`.
- **Update check button spacing**: Increased padding between the "Check for Updates" button and the result message.

### Changed
- **KCM layout reorganization**: Autotiling settings split into Appearance (Colors, Decorations, Focus Border) and Behavior cards. Snapping "Snapping Behavior" card renamed to "Behavior" for consistency.
- **Enable toggles restyled**: Snapping and Tiling enable toggles now use the same bold-label + switch pattern as the main "Enable PlasmaZones" toggle.
- **Layout toolbar simplified**: Per-layout actions moved to right-click context menu. Toolbar retains only global actions (New Layout, Import, Open Folder, view switcher).

## [2.1.0] - 2026-03-13

### Breaking Changes
- **Assignment storage migrated from JSON to KConfig**: Per-screen layout assignments (snapping layout and tiling algorithm) are now stored in `plasmazonesrc` KConfig groups (`[Assignment:*]`) instead of `assignments.json`. Existing `assignments.json` files are automatically migrated on first startup and renamed to `assignments.json.migrated`.

### Added
- **Per-desktop/activity tiling state isolation** (fixes [#212](https://github.com/fuddlesworth/PlasmaZones/discussions/212)): Each virtual desktop and activity now maintains independent tiling state (window membership, master count, split ratios, floating state). Switching desktops no longer interferes with tiling on other desktops.
- **Zone shaders**: Added Fedora Drift, NixOS Drift, openSUSE Drift, and KDE Neon gear zone shaders

### Fixed
- **Editor: new zones could not reach canvas edges during drag** (fixes [#215](https://github.com/fuddlesworth/PlasmaZones/discussions/215)): Grid-aligned snap clamping prevented zones from reaching canvas boundaries, and gap calculation used stale data during drag operations
- **Login freeze from startup OSD**: Prevented OSD from blocking the D-Bus event loop during daemon startup

### Changed
- **KCM refactored into sub-KCMs**: Split into Layouts, Snapping, Tiling, Shortcuts, Apps & Windows, and About sub-modules with a shared common library

## [2.0.2] - 2026-03-11

### Fixed
- **Missing `retileAllScreens` D-Bus slot**: KWin effect called `retileAllScreens` but the adaptor only exposed `retile(screenName)`, causing D-Bus errors on border width changes and other bulk retile triggers
- **Negative zone geometries from constraint solver**: When window minimum sizes exceed available space, the constraint solver could produce non-positive zone dimensions — now clamped to minimum 1x1 after layout calculation

### Changed
- **Generic tarball built on Arch Linux**: Switched the release pipeline's generic tarball build from Fedora 43 to Arch Linux, eliminating lib64/lib path mismatches at the source instead of working around them post-build

## [2.0.1] - 2026-03-11

### Fixed
- **Arch lib64 file conflict with generic tarball** (fixes [#203](https://github.com/fuddlesworth/PlasmaZones/discussions/203)): Force `CMAKE_INSTALL_LIBDIR=lib` in the generic tarball build so it doesn't inherit Fedora's `lib64` default, which conflicts with Arch's `filesystem` package owning `/usr/lib64` as a symlink
- **Global show-zone-numbers toggle ignored when layout active** (fixes [#208](https://github.com/fuddlesworth/PlasmaZones/discussions/208)): Changed zone number visibility so the global toggle is a master switch — per-layout setting can only further restrict, not override it
- **Easing preset dropdown UX** (fixes [#207](https://github.com/fuddlesworth/PlasmaZones/discussions/207)): Replaced single 30+ item flat dropdown with two-dropdown Style + Direction selector; clamped elastic/bounce preview animation to prevent overshoot overflow
- **Modifier key capture silently broken by qmlformat** (fixes [#205](https://github.com/fuddlesworth/PlasmaZones/discussions/205)): Replaced large Qt::KeyboardModifier integer literals with bit-shift expressions (`1 << 25` etc.) that qmlformat cannot mangle into lossy scientific notation
- **Fedora COPR repo name case sensitivity**: Fixed COPR repo reference to use correct casing

### Added
- **Fedora COPR and openSUSE OBS install instructions** in README (fixes [#209](https://github.com/fuddlesworth/PlasmaZones/discussions/209))

## [2.0.0] - 2026-03-10

### Added

#### Autotiling Engine
- **Pluggable algorithm architecture** with 10 tiling layouts: Master+Stack, Centered Master, BSP, Dwindle, Spiral, Columns, Rows, Grid, Wide, Three-Column, and Monocle
- **Per-screen algorithm selection** with independent settings per monitor
- **Separate centered-master settings**: Split ratio and master count are independent from master+stack (defaults: 0.5 vs 0.6)
- **Per-screen maxWindows cap** to limit tiled window count per monitor
- **Per-side outer gaps** (top/bottom/left/right) for independent screen edge spacing
- **Overflow window management**: Auto-float excess windows when maxWindows is reached, auto-recover when room opens
- **Hide title bars** on tiled windows with configurable active-window border rendering (color, width)
- **Deterministic zone-ordered window transitions** when switching between snapping and autotiling modes
- **Float/unfloat toggle** with geometry preservation and cross-screen fallback
- **Smart gaps**, insert position config, focus-follows-mouse, focus-new-windows
- **Minimum window size** respect with algorithm-level constraint solving
- **D-Bus interface** (`org.plasmazones.Autotile`) for runtime control
- **Read-only preview mode** for autotile layouts in the editor
- **Pre-autotile geometry persistence** across session restarts for accurate float restore

#### Animation System
- **Translate-only slide animations** that avoid Wayland buffer desync (no scale transforms)
- **Staggered cascading window animations** with configurable overlap
- **Cubic bezier easing curve editor** in KCM with live preview
- **Elastic and bounce easing curve types** with customizable parameters (amplitude, period, overshoot)

#### KCM Improvements
- **Dual-view layout picker** with separate Snapping/Tiling modes and default autotile algorithm selection
- **Dual-mode per-screen assignments** (snapping layouts + autotile algorithms per monitor)
- **Snapping enable/disable toggle**
- **Auto-select default layout** in Layout tab
- **Live algorithm preview widget**
- **Per-monitor snapping gap/padding overrides**

### Changed
- **Renamed Zones tab to Snapping** in KCM for clarity alongside the new Tiling tab
- **BSP algorithm made deterministic**: Removed persistent tree state that caused non-reproducible layouts
- **Major codebase refactoring**: Split 20+ oversized files (>500 lines) into DRY translation units organized in subdirectories
  - Extracted `OverflowManager`, `PerScreenConfigResolver`, `NavigationController`, `SettingsBridge` from `AutotileEngine`
  - Extracted `AssignmentManager`, `DaemonController`, `LayoutManager` from monolithic KCM
  - Extracted `AutotileHandler`, `ScreenChangeHandler`, `SnapAssistHandler` from KWin effect
  - Split `Settings`, `OverlayService`, `WindowTrackingService`, D-Bus adaptors, and editor into subdirectories
- **Comprehensive unit tests** for all algorithms, engine, tiling state, overflow manager, geometry utils, and algorithm registry (11 test suites)

## [1.15.15] - 2026-03-09

### Fixed
- **Shortcuts not working until window is manually snapped**: The async D-Bus key grab refactor (v1.15.14) bypassed `KGlobalAccel::setShortcut()`, which connects to the component's `globalShortcutPressed` D-Bus signal. Without this connection, key grabs succeeded but press events were never received. Fixed by bootstrapping the component signal with one synchronous `setShortcut()` call (~490ms) before firing async D-Bus for the remaining ~50 key grabs.

## [1.15.14] - 2026-03-09

### Fixed
- **Nix build failure after systemd service template change**: Removed stale `postInstall` `substituteInPlace` that tried to replace `/usr/bin/plasmazonesd` in the systemd service file. Since the service now uses `configure_file(@ONLY)` with `@KDE_INSTALL_FULL_BINDIR@`, the path resolves correctly at build time and no post-install patching is needed.

## [1.15.13] - 2026-03-08

### Fixed
- **Login freeze with many shortcuts** (fixes [#200](https://github.com/fuddlesworth/PlasmaZones/discussions/200)): Replaced blocking `KGlobalAccel::setGlobalShortcut()` with a two-step approach — `setDefaultShortcut()` registers all shortcuts without key grabs, then async D-Bus calls activate key grabs in parallel without blocking the event loop. Eliminates 20-40s hangs during login when kglobalacceld is under contention.

## [1.15.12] - 2026-03-08

### Fixed
- **Global shortcuts broken after v1.15.9/v1.15.10** (fixes [#200](https://github.com/fuddlesworth/PlasmaZones/discussions/200)): Reverted async D-Bus shortcut registration (v1.15.10) and deferred batching (v1.15.9) which left the KGlobalAccel component inactive, preventing all shortcut dispatch. Restored direct `KGlobalAccel::setGlobalShortcut()` calls which properly register actions and set up key grabs through the official API.

## [1.15.11] - 2026-03-08

### Fixed
- **Release workflow retry loop**: Replaced `softprops/action-gh-release` with native `gh` CLI to fix releases getting stuck in a retry loop ([action-gh-release#704](https://github.com/softprops/action-gh-release/issues/704)).

## [1.15.10] - 2026-03-08

### Fixed
- **Login freeze persisted despite v1.15.9 batching** (fixes [#200](https://github.com/fuddlesworth/PlasmaZones/discussions/200)): The v1.15.9 deferred batch approach still blocked because each batch made synchronous D-Bus round-trips whose replies stalled for ~25s while kglobalaccel processed key grabs (QTBUG-34698). Replaced with true async D-Bus: `setDefaultShortcut()` registers actions synchronously (fast — no key grabbing), then `setShortcutKeys` calls fire via `QDBusPendingCallWatcher` so the event loop never blocks on key grabbing.

## [1.15.9] - 2026-03-08

### Fixed
- **Login freeze with autostart apps** (fixes [#200](https://github.com/fuddlesworth/PlasmaZones/discussions/200)): Shortcut registration made 86+ synchronous D-Bus calls to KGlobalAccel at startup, blocking the event loop for 20-40 seconds when competing with other KDE services during login. Registration is now batched and deferred, yielding the event loop between batches.
- **systemd service ordering**: Added `After=plasma-kglobalaccel.service` to ensure the shortcut daemon is ready before PlasmaZones registers shortcuts.

## [1.15.8] - 2026-03-08

### Fixed
- **RPM: remove exact KWin version pin** (fixes [#199](https://github.com/fuddlesworth/PlasmaZones/discussions/199)): RPM package required `kwin = <build-version>` which blocked installation when KWin received patch updates (e.g. 6.6.1 -> 6.6.2). Changed to `kwin >= 6.6.0`; soname-level deps handle ABI safety automatically.

## [1.15.7] - 2026-03-06

### Fixed
- **KWin 6.6.2 compatibility**: Rebuild for KWin 6.6.2 minor release; effect plugin is version-locked and requires exact KWin version match to load.

## [1.15.6] - 2026-02-28

### Fixed
- **Debian package build**: Re-enabled .deb creation using KDE Neon container (`kdeneon/all:dev-stable`) which ships Plasma/KF6 6.6+, replacing the disabled Ubuntu 25.10 build.

## [1.15.5] - 2026-02-27

### Fixed
- **Multi-zone snap cascade in tiling layouts**: Edge-adjacent detection no longer flood-fill expands through shared edges, which caused all zones to highlight in tiling layouts. Seed zones are now used directly for multi-zone snap. Bounding-rect expansion is retained only for paint-to-span mode where rectangular gap-filling is needed.

## [1.15.4] - 2026-02-26

### Fixed
- **Overlapping zone multi-zone cascade**: Placing cursor on a zone fully inside a larger zone no longer highlights all zones. Fixed detectMultiZone to separate overlapping zones (cursor inside) from edge-adjacent zones (cursor near edge); only edge-adjacent zones trigger multi-zone snap. Replaced bounding-rect expansion with edge-adjacency flood-fill that skips zones spatially overlapping the seed. Removed duplicated smallest-area loop in paint-to-span.
- **Edge tolerance now respects settings**: Zone-to-zone edge detection uses the user's adjacentThreshold setting instead of a hardcoded 5px value, so manually-gapped layouts work correctly with the configured proximity.

## [1.15.3] - 2026-02-26

### Fixed
- **KWin effect plugin version lock**: Effect plugin embeds EffectPluginFactory version in its IID; it only loads when runtime KWin matches. Added build-time version visibility in CMake and RPM spec now requires exact KWin version match, preventing 6.6.0-built plugins from installing on 6.6.1 systems where they fail to load.

## [1.15.2] - 2026-02-22

### Fixed
- **Overlapping zone snapping**: When zones overlap, the smallest zone at the cursor position is now selected instead of the first in list order. Matches FancyZones' area-covered heuristic so the more specific zone wins.

## [1.15.1] - 2026-02-22

### Fixed
- **RPM packaging**: Added KCM and editor translation files (`kcm_plasmazones.mo`, `plasmazones-editor.mo`) to spec `%files` section, fixing "unpackaged file(s) found" build failure on Fedora.

## [1.15.0] - 2026-02-22

### Added
- **Mosaic Pulse shader**: Audio-reactive stained glass mosaic with colorful tiles, pulsing shapes (circles, diamonds, squares), sparkles, and dithered posterization. Bass drives shape pulse, mids shift hue, treble triggers sparkles. 12 configurable parameters across 6 groups.
- **User-supplied image textures**: Shader effects can now sample up to 4 user-provided images (bindings 7-10) with configurable wrap modes.
- **Shared GLSL utilities**: Extracted `common.glsl`, `audio.glsl`, `textures.glsl`, and `multipass.glsl` as shared includes — all shaders updated to use common helpers (hash, noise, SDF, blending, audio bands).

### Fixed
- **System layout restore**: Deleting a user layout override from KCM now correctly restores the system-provided layout instead of leaving a blank state.
- **System layout label**: Label now includes "zones" suffix for consistency with other layout names.
- **Translation extraction**: 33 missing source files added to the extraction list so all translatable strings are captured.
- **German .po headers**: Normalized header fields for consistency across all 3 translation domains.

### Changed
- **German translations**: Complete coverage for all 3 domains (daemon, KCM, effect). Removed 49 obsolete entries.
- Removed outdated shader presets.

## [1.14.1] - 2026-02-21

### Fixed
- **Zone persistence on daemon restart**: Windows that were snapped to zones are now correctly re-registered when the daemon is stopped and started. Root cause: `pendingRestoresAvailable` was never emitted because the layout was set before the WindowTrackingAdaptor connected to `activeLayoutChanged`. Now sets `m_hasPendingRestores` at init when pending assignments are loaded. Also saves window tracking state on daemon shutdown so snapped windows persist across restarts.

## [1.14.0] - 2026-02-21

### Added
- **Per-side edge gaps**: Independent top/bottom/left/right outer gap values instead of a single uniform gap — useful for transparent panels or asymmetric screen setups. Global toggle in KCM with per-layout overrides in the editor. Full undo/redo support. ([#187], [#188])
- **Per-zone fixed pixel geometry**: Zones can now use absolute pixel coordinates instead of relative 0.0-1.0 values, enabling precise pixel-perfect layouts that don't scale with resolution. Per-zone toggle between Relative and Fixed modes in the editor. ([#180], [#182])
- **Full screen geometry toggle**: Per-layout option to use the full screen area (ignoring panels/taskbars) for zone calculations, allowing zones to extend behind auto-hide or transparent panels. ([#179], [#181])
- **AlwaysActive zone activation**: New activation mode that shows zones on every window drag without requiring a modifier key or mouse button — configurable in KCM Zones tab. ([#185], [#186])

### Changed
- **Copy-on-write layout saving**: Layouts are only written to disk when actually modified, with per-layout dirty tracking to avoid unnecessary I/O during bulk operations.

### Fixed
- Hardcoded 1920x1080 fallback removed from D-Bus zone detection — uses actual screen geometry.
- Fixed preview rendering for fixed-geometry zones in KCM new layout dialog.
- All layouts recalculated on startup and screen changes to prevent stale geometry.
- Per-side edge gap: -1 sentinel no longer leaks into geometry calculations when settings are unavailable.
- Per-side edge gap: `usePerSideOuterGap` toggle now persists across save/load even with all-default side values.
- Per-side edge gap: clearing override in editor is now undoable.

## [1.13.0] - 2026-02-20

### Added
- **Layout Picker Overlay**: Full-screen interactive layout browser triggered via configurable keyboard shortcut. Browse all available layouts in a centered card grid with keyboard navigation (arrow keys + Enter) and mouse support. Selecting a layout switches to it and resnaps all windows. ([#176])
- **Shared LayoutCard component**: Extracted reusable `LayoutCard.qml` and `PopupFrame.qml` into `org.plasmazones.common` QML module, shared between the Zone Selector and Layout Picker overlays.
- **Snap Assist after resnap**: Snap Assist now triggers after resnapping windows when switching layouts via the layout picker, offering to fill any empty zones.

### Fixed
- **Zone activation broken when Zone Selector disabled** ([#175]): Disabling the Zone Selector popup caused the zone activation hotkey (e.g. Alt+drag) to stop working entirely. Root cause: D-Bus deserialization of trigger settings could silently fail (Qt delivering `QDBusArgument` instead of native `QVariantList<QVariantMap>`), but this was masked when `zoneSelectorEnabled=true` because a bypass gate let all drag events through regardless. Added robust `QDBusArgument` unwrapping, a permissive `m_triggersLoaded` flag that allows drags through until triggers are confirmed loaded, and diagnostic logging for trigger load failures.
- **Layout Picker double-trigger**: Rapidly pressing the layout picker shortcut could create multiple overlay windows with competing `KeyboardInteractivityExclusive` keyboard grabs on Wayland, causing shortcuts to stop working. Replaced toggle guard with a simple existence guard that prevents re-triggering while any picker window exists.

## [1.12.2] - 2026-02-19

### Fixed
- **Audio visualizer**: CAVA process silently failed when bar count was odd (exit code 1, "must have even number of bars with stereo output"). Even bar counts are now enforced at all layers: CavaService, KCM setter, and UI slider (stepSize=2).
- **Audio shader data**: QML `|| []` fallback on `audioSpectrum` binding forced V4 JavaScript conversion, losing the native `QVector<float>` type needed by `ZoneShaderItem`'s fast path. Replaced with a `Binding` element guarded by `when` to preserve type identity through the binding chain.
- **CAVA stderr capture**: Switched from `ForwardedErrorChannel` to `SeparateChannels` so CAVA error output is captured in daemon logs instead of lost.
- **CAVA exit diagnostics**: Moved `exitCode()`/`readAllStandardError()` from `stateChanged` to `finished` signal handler per Qt API contract. Only warns on non-zero exit code (stderr on exit 0 is normal for CAVA).

### Changed
- **Shared audio constants**: `Audio::MinBars`/`Audio::MaxBars` moved to `src/core/constants.h` with `static_assert` for even values, eliminating magic number duplication across CavaService, KCM, and QML.
- **Nix**: Re-enabled Nix CI, release builds, flake.lock updater, and `plasmazones.nix` release asset now that nixpkgs-unstable has the Plasma 6.6 stack (NixOS/nixpkgs#479797).

## [1.12.1] - 2026-02-18

### Fixed
- **KCM Editor tab**: Use unique QML type `PlasmaZonesKeySequenceInput` so the Editor tab always loads the bundled shortcut component (with `defaultKeySequence`). Fixes "Type EditorTab unavailable" / "Cannot assign to non-existent property 'defaultKeySequence'" when the KCM runs from system install (e.g. NixOS) where another `KeySequenceInput` could be resolved first.
- **KWin effect**: Remove explicit `Id` from plugin metadata so the loader uses the filename-derived id and the kf.coreaddons warning is resolved.

## [1.12.0] - 2026-02-18

### Added
- **Reapply window geometries after geometry updates**: When zones or panel geometry change (e.g. after closing the KDE panel editor), the daemon requests the KWin effect to reapply snapped window positions so windows stay correctly placed in zones.
- **D-Bus**: `reapplyWindowGeometriesRequested` signal and `getUpdatedWindowGeometries` method on WindowTracking for the effect to fetch and apply geometries.
- **ScreenManager**: `delayedPanelRequeryCompleted` signal when the delayed panel requery finishes (used for documentation; reapply path is unified).

### Fixed
- **Panel editor / geometry**: Zones and snapped windows no longer shift incorrectly after editing the KDE panel and closing the panel editor. Geometry debounce (400ms), delayed panel requery (400ms), and immediate reapply (0ms) after each geometry batch keep overlay and window positions correct.
- **Multiple windows in same zone**: Reapply now updates every snapped window; previously only one window per zone (same app) was updated due to stableId-only lookup. Effect now maps by full window ID with stableId fallback.
- **Effect reapply safety**: Reapply-in-progress guard prevents overlapping async reapply runs; QPointer in async callback avoids use-after-free if the effect is unloaded during reapply.
- **Effect JSON**: Robust validation and skip of invalid geometry entries; QLatin1String for JSON keys (Qt6); single-pass window map.

### Changed
- **Reapply timing**: Reapply runs after every geometry batch (0ms delay). Removed redundant 1100ms/450ms reapply path; delayed panel requery still triggers the same debounce → processPendingGeometryUpdates → reapply flow.
- **Daemon**: Reapply timer stopped in `stop()`; named constants for geometry and panel delays.
- **Nix**: Build asserts layer-shell QPA plugin compatibility and fails with a clear message when nixpkgs provides the 6.5 stack. Nix CI and release Nix build/artifact disabled until nixpkgs has Plasma 6.6.

## [1.11.8] - 2026-02-16

### Performance
- **Signal-driven drag detection**: Replaced the QTimer-based poll loop (32ms stacking-order scans during drag) with KWin's per-window `windowStartUserMovedResized` / `windowFinishUserMovedResized` signals for zero-cost, event-driven drag start/end detection. Eliminates the `m_pollTimer` entirely — no more periodic stacking-order iteration on the compositor thread, even as a safety net ([#167])

## [1.11.7] - 2026-02-16

### Performance
- **Event-driven cursor tracking**: Cursor position updates during drag are now driven by `slotMouseChanged` instead of the poll timer, eliminating QTimer jitter from the compositor frame path and providing more accurate cursor tracking at input-device cadence
- **Throttled dragMoved signals**: `DragTracker::updateCursorPosition()` throttles `dragMoved` emissions to ~30Hz via `QElapsedTimer`, preventing D-Bus flooding from high-frequency (1000Hz) mouse input
- **Eliminated QDBusInterface for WindowDrag**: Replaced `QDBusInterface` with `QDBusMessage::createMethodCall` for all WindowDrag D-Bus calls (`dragStarted`, `dragMoved`, `dragStopped`, `cancelSnap`), avoiding synchronous D-Bus introspection that could block the compositor thread with a ~25s timeout if the daemon is registered but slow to respond
- **Pre-parsed activation triggers**: Activation triggers are now parsed from `QVariantList` to POD structs (`ParsedTrigger`) once at load time, removing per-call `QVariant` unboxing overhead from `anyLocalTriggerHeld()` (~30 calls/sec during drag)

## [1.11.6] - 2026-02-16

### Performance
- **Dynamic poll timer**: Poll rate switches from 500ms (idle) to 32ms (~30Hz) only when LMB is pressed, eliminating continuous 60Hz stacking-order scans on the compositor thread when no drag is active ([#167])
- **Early-exit idle polls**: `DragTracker::pollWindowMoves()` skips the full stacking-order iteration when no drag is active and no button is held
- **Reduced D-Bus traffic during drag**: Active-drag poll rate lowered from 16ms (60Hz) to 32ms (30Hz) — zone detection doesn't need sub-33ms updates, and halving D-Bus message serialization on the compositor thread reduces frame-time jitter on high-refresh-rate displays ([#167])
- **Guard redundant daemon work**: `hideOverlayAndClearZoneState()` now short-circuits when overlay is already hidden and zone state is clear, preventing 30Hz `clearHighlights()`/`clearHighlight()` calls that could congest the daemon event loop and create D-Bus back-pressure ([#167])

## [1.11.5] - 2026-02-16

### Fixed
- **KCM tab buttons**: Prevent tab buttons from resizing when switching tabs

### Performance
- **Deferred D-Bus calls during drag**: Keyboard grab and D-Bus `dragStarted`/`dragMoved` calls are now deferred until an activation trigger is actually detected, eliminating 60Hz D-Bus traffic and keyboard grab/ungrab overhead for non-zone window drags ([#167])
- **Deduplicated trigger read**: `WindowDragAdaptor::dragMoved()` now reads activation triggers once per call instead of twice

## [1.11.4] - 2026-02-16

### Fixed
- **Layout grid cards**: Badge and zone count text were left-aligned instead of centered under each card

## [1.11.3] - 2026-02-16

### Added
- **Master toggle for zone span**: New checkbox to fully enable/disable paint-to-span zone selection. Defaults to on; when off, modifier and threshold controls are greyed out.
- **Master toggle for snap assist**: New checkbox to fully enable/disable the snap assist (window picker) feature. Defaults to on; when off, all snap assist sub-options are greyed out.

### Fixed
- **Nix flake evaluation error**: `lib.mkPackageOption` received a derivation instead of an attribute path, causing evaluation failures when `package` was not explicitly specified. Users can now use `programs.plasmazones.enable = true` without setting `package`.

## [1.11.2] - 2026-02-15

### Added
- **Snap Assist trigger override** ([#166]): When "Always show" is off, hold a configurable modifier or mouse button when releasing a window to enable Snap Assist for that snap only. Uses the same multi-trigger widget as zone activation and zone span.

### Changed
- **Snap Assist UI**: "Always show" checkbox; when off, configure hold-to-enable trigger via ModifierAndMouseCheckBoxes (shown disabled when always-on).
- **D-Bus breaking**: `org.plasmazones.WindowDrag.dragStopped` now requires `modifiers` and `mouseButtons` at release (for Snap Assist triggers). KWin effect and daemon must be from the same PlasmaZones version.

### Fixed
- **KCM UX consistency**: Section titles added for all groups in Zones tab cards — Appearance (Colors, Border), Effects (Visual Effects), Activation (Triggers) — so every section has a consistent heading

## [1.11.1] - 2026-02-15

### Fixed
- **Drag stutter**: Removed redundant `windowFrameGeometryChanged` handler that flooded D-Bus with 60-144+ calls/sec during window drag, causing visible stutter and leaving the daemon in a stale state ([#167])
- **Login hang**: Replaced synchronous `QDBusInterface` introspection in `loadExclusionSettings()` with async `QDBusMessage` call, preventing the compositor from blocking for up to 25 seconds during login when the daemon is registered but not yet responding ([#167])

## [1.11.0] - 2026-02-15

### Added
- **EDID-based monitor identification**: Monitors are now identified by manufacturer, model, and serial number instead of connector name (e.g. "DP-2"). Layouts stay assigned to the correct physical monitor regardless of which port it's connected to ([#164])
- **Nix flake**: Proper `flake.nix` with NixOS module (`programs.plasmazones.enable`), Home Manager module, overlay, and dev shell
- **Multi-arch Nix CI**: Flake-based CI builds on both x86_64-linux and aarch64-linux with magic-nix-cache
- Weekly `flake.lock` auto-update workflow

### Fixed
- **NixOS KCM loading failure**: KCM QML components failed to load in System Settings on NixOS due to missing `qmldir` files and unresolvable QML runtime dependencies (kirigami, qqc2-desktop-style) ([Discussion #160])
- Zone selector popup layouts are now sorted alphabetically by name
- D-Bus `toggleActivation` method was not registered in the settings adaptor

### Changed
- Nix CI uses `nix build`/`nix flake check` instead of legacy `nix-build --expr`
- Nix install action upgraded from v30 to v31
- Release notes now include flake-based NixOS installation instructions alongside standalone `plasmazones.nix`

## [1.10.6] - 2026-02-14

### Added
- **Toggle activation mode**: Zone activation modifier can be toggled on/off with a single press instead of requiring hold — useful for trackpad and accessibility users ([#159])

### Changed
- **Proximity snap always active**: Removed the multi-zone modifier setting entirely — adjacent zone detection now always works during drag with no modifier required
- **Zone span default changed to Ctrl**: Paint-to-span modifier defaults to Ctrl instead of Meta, avoiding conflict with KDE's Meta shortcut in toggle activation mode
- CI: Replaced DeterminateSystems/FlakeHub Nix actions with cachix/install-nix-action

### Fixed
- Nix build: ECM's KDEInstallDirs resolved an absolute systemd unit path outside the Nix store, causing postInstall to fail ([#159])

## [1.10.5] - 2026-02-13

### Fixed
- Snap Assist D-Bus calls made fully async to prevent compositor freeze when daemon is busy with overlay teardown ([#158])
- Zone selector snapping now uses the same geometry pipeline as overlay snapping, so gap handling is consistent

## [1.10.4] - 2026-02-13

### Fixed
- Session restore places windows on wrong display in multi-monitor setups — active screen and desktop assignments were lost on daemon restart ([#156])
- Escape during drag now dismisses overlay without cancelling the drag; re-pressing the activation trigger re-shows the overlay
- Keyboard grab released in effect destructor to prevent input loss if effect unloads mid-drag

## [1.10.3] - 2026-02-13

### Fixed
- CAVA audio visualizer not starting without daemon restart after enabling in KCM ([#150])
- Shader effects toggle, frame rate, and spectrum bar count changes also required restart — same root cause
- No default layout selected on fresh install — Columns (2) now gets the star badge out of the box
- `defaults()` uses `defaultOrder` from layout metadata instead of hardcoded name match

## [1.10.2] - 2026-02-13

### Fixed
- Release workflow: delete pre-existing GitHub release before recreating with build assets (fixes HTTP 422 on asset upload)
- RPM spec and Debian changelog no longer manually maintained — CI generates both from CHANGELOG.md via `generate-changelog.sh`
- RPM spec Version field uses `0.0.0` placeholder (CI replaces from git tag)
- Avoid literal `%changelog` in spec header comments (broke `sed` in changelog generator)

## [1.10.0] - 2026-02-13

### Added
- **Multiple binds per action**: Configure up to 4 independent triggers for zone activation, proximity snap, and paint-to-span — e.g. Alt key + Right mouse button as separate triggers for different input devices ([#150])
- Click-to-edit existing triggers in the KCM — click a trigger label to replace it in-place
- AND semantics for combined modifier+button triggers (both must be held)
- Conflict detection warns when the same trigger is used across multiple actions

### Fixed
- Multi-zone threshold setting not applied correctly ([#147])
- Modifier shortcuts now exclude the activation key to prevent conflicts
- Legacy config keys cleaned up on save (stale DragActivationModifier, mouse button keys removed)
- Empty trigger list prevented — at least one trigger is always required per action

### Changed
- Settings stored as JSON trigger lists (automatic migration from single-value format)
- KWin effect simplified — daemon handles all trigger matching via `anyTriggerHeld()`
- D-Bus API: new `dragActivationTriggers`, `multiZoneTriggers`, `zoneSpanTriggers` list properties replace individual modifier/mouse button getters

## [1.9.5] - 2026-02-13

### Added
- Zones can overlap Plasma panels set to autohide/dodge windows ([#148])
- Force-end drag on mouse button release for safer drag lifecycle
- Proximity snap always active by default (no modifier required)

### Fixed
- **Compositor freeze**: Remove `processEvents()` calls that deadlock with Wayland compositor during drag ([#152])
- **Compositor stall on layout change**: Hide overlay/zone selector before layout switch in zone selector drop path, skip heavy QML updates for hidden windows
- **Snap assist Escape not working**: Keep KGlobalAccel Escape shortcut registered through snap assist phase; add `snapAssistDismissed` signal for proper cleanup
- **Snap assist not dismissing**: Dismiss snap assist on any window zone change (navigation, snap, unsnap, float toggle)
- **Snap assist wrong window**: Use full windowId (not stableId) for per-instance floating/geometry tracking
- Snap assist Escape handling, dismiss on new drag, zone selector layout sync
- Paint-to-snap raycasting and shader highlight for multi-zone selection
- Mouse-button zone activation now latches until drag ends (no flicker)
- Shortcut clear button resets to default instead of empty
- Inverted panel-hiding check for usable geometry
- KCM linker errors from missing kcfg sources

### Changed
- Remove 66 dead code items across 48 files
- Remove dead multiZoneEnabled code

## [1.9.3] - 2026-02-12

### Fixed
- Proximity snap "always on" no longer bypasses overlay activation — it now only enables proximity snap when the overlay is already open via the activation key

## [1.9.2] - 2026-02-12

### Added
- KCM: "Proximity snap always on" checkbox — enables always-on proximity snap without holding the modifier (per [#143])
- Escape key cancels overlay during window drag — overlay stays hidden until the next drag of a stationary window

## [1.9.1] - 2026-02-12

### Added
- Snap Assist: Aero Snap style window picker after snapping, allowing users to fill empty zones with unsnapped windows ([#95])
- Snap Assist overlay with window thumbnails, zone-mapped layout, and KCM setting to enable/disable
- `getEmptyZonesJson` and `showSnapAssist` D-Bus APIs for Snap Assist integration

### Fixed
- Zone padding and outer gap in individual layout settings now persist correctly when saving ([#145])
- Default layouts no longer include redundant zone padding override (use global setting by default)
- Snap Assist: overlay zone appearance matches zone colors and borders; thumbnail caching across continuation
- Snap Assist: KWin effect default for snapAssistEnabled until D-Bus loaded (avoids race)

### Changed
- Packaging: add env.d to RPM %files; remove redundant Snap Assist message from Arch install

## [1.8.4] - 2026-02-11

### Added
- Shader preset load/save in editor ShaderSettingsDialog
- Preview shader effects in zone editor ([#132])
- Restore window size immediately when dragging between zones ([#133])

### Fixed
- Overlay follows cursor when dragging to another monitor ([#136])
- Defer window resize until drag release; keep restore-to-float on unsnap
- Hide shader preview overlay when dialogs open or app loses focus
- PR review feedback for shader preview

### Removed
- Dead `zoneGeometryDuringDrag` slot

## [1.8.2] - 2026-02-09

### Added
- Full zone label font customization: family, size scale, weight, italic, underline, strikeout ([#97])
- Font picker dialog in KCM Zones tab with live preview and search
- Sonic Ripple audio-reactive shader

### Changed
- Rename `NumberColor` setting to `LabelFontColor` for consistent `LabelFont*` naming across all layers ([#97])
- Sort layouts alphabetically by name in KCM
- Use generic `adjustlevels` icon for shader settings button in editor (replaces app-specific icon)

### Fixed
- Self-referencing `font.family` QML binding preventing font reset from updating previews ([#97])
- Font reset button now also resets label size scale
- `qFuzzyCompare` edge case in KCM font scale setter (clamp before compare)
- Remove dead `labelFontColor` property from zone selector window
- Auto badge distinguished from Manual badge using `activeTextColor`

## [1.8.1] - 2026-02-09

### Added
- Paint-to-span zone modifier: hold a modifier while dragging to progressively paint across zones, window snaps to bounding rectangle on release ([#94], [#96])
- Configurable "Paint-to-span modifier" in KCM Zones tab (default Alt+Meta)
- Renamed "Multi-zone modifier" to "Proximity snap modifier" for clarity

### Changed
- Replaced `middleClickMultiZone` bool setting with `zoneSpanModifier` DragModifier enum
- Config migration: users who had middle-click multi-zone disabled keep zone span disabled after upgrade
- Extracted `prepareHandlerContext()`, `computeCombinedZoneGeometry()`, and `zoneIdsToStringList()` helpers in drag handling (DRY)
- Added `setOsdStyleInt` range validation

### Removed
- Dead `skipSnapModifier` setting (fully scaffolded but never consumed in drag handling)

### Fixed
- Missing `restoreWindowsToZonesOnLoginChanged` signal in KCM defaults and settings sync
- 12 missing signal emissions in KCM `onSettingsChanged()`
- Painted zone state not cleared on `dragStarted()` causing stale highlights
- Modifier conflict warning using `static bool` instead of per-instance member

## [1.8.0] - 2026-02-09

### Added
- CAVA audio visualization service for audio-reactive shaders ([#92])
- Spectrum Pulse shader: audio-reactive neon energy with bass glow, spectrum aurora, and CAVA integration ([#92])
- Audio-reactive shader uniforms: spectrum data and audio levels passed to GPU ([#92])
- KCM settings for audio visualizer (enable/disable, spectrum bar count)
- Auto-assign windows to first empty zone per layout ([#90])
- App-to-zone auto-snap rules per layout with screen-targeting
- Window picker dialog for exclude lists
- Per-monitor zone selector settings ([#89])
- Snap-all-windows shortcut (`Meta+Ctrl+S`)

### Changed
- Replace global active layout with `defaultLayout()` for user-facing surfaces
- DRY per-screen config validation and shared layout computation
- Audit and normalize log levels across entire codebase

### Fixed
- Mutual exclusion between overlay and zone selector during drag ([#92])
- Per-screen shader decisions for multi-monitor setups ([#92])
- Comprehensive multi-monitor per-screen targeting and isolation ([#87])
- Per-screen layout isolation and shortcut screen guards ([#87])
- Zone selector showing on all monitors instead of target screen
- Per-screen zone selector validation and edge cases
- Zone selector defensive setActiveLayout and QML signal verification
- Per-screen override message/button not updating reactively in KCM
- Daemon survives monitor power-off (DP hotplug disconnect)
- Editor: defer window destroy during mid-session screen switch
- Unfloat: fall back when saved pre-float screen no longer exists
- Remove misleading shortcut hint from zone overlay
- WrapVulkanHeaders noise in feature summary; ColorUtils.js QML warning

## [1.7.0] - 2026-02-06

### Added
- Layout visibility filtering: control which layouts appear in zone selector per screen, virtual desktop, and activity
  - Tier 1 (KCM): eye toggle to globally hide a layout from the zone selector
  - Tier 2 (Editor): visibility popup to restrict layouts to specific screens, desktops, or activities
  - Empty allow-lists = visible everywhere (opt-in model)
  - Active layout always bypasses filters to prevent empty selector state
  - Undo/redo support for visibility changes in the editor
  - Filter badge on KCM layout cards when Tier 2 restrictions are active
- Layout cycling (Meta+[/]) now respects per-screen visibility filtering

### Changed
- OSD style defaults to visual preview instead of text for new installs

### Fixed
- Duplicated and imported layouts no longer inherit visibility restrictions from the source
- Stale screen names auto-cleaned from layout restrictions when monitors are disconnected
- Layout cycling skips hidden/restricted layouts correctly in all directions

## [1.6.2] - 2026-02-06

### Fixed
- Editor not moving to selected screen when switching monitors in TopBar or via D-Bus `openEditorForScreen`
- Editor defaulting to wrong screen on Wayland (now uses cursor screen instead of unreliable `primaryScreen`)

## [1.6.1] - 2026-02-06

### Added
- Liquid Metal shader: mercury-like fluid surface with environment reflections, Fresnel, bloom, and mouse interaction

### Fixed
- AUR `-bin` package build failure due to `.INSTALL` dotfile left in package root
- Liquid Metal: surface drifting to bottom-left (use standing waves instead of travelling)
- Liquid Metal: inverted mouse Y coordinate
- Liquid Metal: outer glow rendering outside zones due to zoneParams swizzle bug

### Removed
- 5 low-quality shaders: minimalist, aurora-sweep, warped-labels, prism-labels, glitch-labels

## [1.6.0] - 2026-02-05

### Added
- Multi-zone snapping support in window tracking

### Fixed
- Shader parameters from previously-used shaders accumulating in layout JSON
- Atomic undo for shader switching (single undo step instead of two)
- Post-install messages now note that KWin restart is required to load the effect

### Removed
- Dead properties: Layout::author, Layout::shortcut, Zone::shortcut (never wired up)
- Dead files: ZoneEditor.qml, LayoutPicker.qml, ShaderOverlay.qml, shadercompiler.cpp, zonedataprovider.cpp

## [1.5.9] - 2026-02-05

### Changed
- Release pipeline now generates Debian, RPM, and GitHub release notes from CHANGELOG.md

### Fixed
- Missing pacman hook files for sycoca cache refresh in Arch package
- POSIX awk compatibility in changelog generator (mawk on Ubuntu)
- AUR publish: mount PKGBUILD read-only and generate .SRCINFO via stdout to avoid docker chown breaking host git ownership

## [1.5.2] - 2026-02-05

### Added
- Multi-pass shader rendering with up to 4 buffer passes and inter-pass texture channels (iChannel0-3) ([#78])
- Multi-channel shaders: buffer passes read outputs from previous passes ([#79])
- Zone labels rendered as shader textures for custom number styling
- Voronoi Stained Glass shader with 3D raymarching and bloom
- Mouse button zone activation as alternative to modifier-key drag ([#80])
- Zone selector auto-scroll and screen-clamped positioning
- Update notification banner with GitHub release checking
- About tab with version info and links in KCM
- Pywal color import error feedback
- Automatic AUR publishing on release

### Fixed
- Buffer pass alpha blending in RHI renderer (SrcAlpha darkened output on RGBA-cleared textures)
- Duplicate zone IDs and use-after-free in editor undo system
- Help dialog redesigned; fullscreen exit button repositioned
- Update check button layout shift when status message appears

## [1.3.4] - 2026-02-03

### Changed
- RHI/Vulkan zone overlay renderer replaces OpenGL path ([#76])
- Packaging updated to use Fedora 43

### Added
- `#include` directive support in shaders (`common.glsl`, `multipass.glsl`)
- Shader performance improvements and error recovery

### Fixed
- Context loss and reinitialization in overlay renderer

## [1.3.3] - 2026-02-03

### Added
- Resnap-to-new-layout shortcut ([#75])
- Shortcut consolidation: merged redundant key bindings

### Changed
- Build/install only installs files; packaging (postinst, RPM %post) handles sycoca refresh and daemon enable

### Fixed
- Logging alignment issues

## [1.3.2] - 2026-02-03

### Changed
- KWin PlasmaZones effect enabled by default on install

### Removed
- Autotiling feature removed ([#74])

## [1.3.1] - 2026-02-02

### Fixed
- Debian releases now include debug symbol packages (.ddeb)

## [1.3.0] - 2026-02-02

### Fixed
- Build paths for Arch and Debian packaging
- Session restore validates layout matches before restoring windows

### Added
- CI and release version badges in README

## [1.2.6] - 2026-02-02

### Changed
- KWin effect metadata aligned with KWin conventions
- CI pipeline simplified and packaging reorganized
- Debug symbol packages enabled for all distros

### Fixed
- Float/unfloat preserves pre-snap geometry across window close/reopen cycles ([#72])

### Added
- Autotiling settings page in KCM ([#71])

## [1.2.5] - 2026-02-01

### Improved
- Navigation OSD with multi-monitor support and UX fixes ([#70])

## [1.2.4] - 2026-02-01

### Fixed
- All remaining synchronous D-Bus calls in KWin effect converted to async, preventing compositor thread blocking
- Startup freezes from `syncFloatingWindowsFromDaemon()`
- Window event stutters from `ensurePreSnapGeometryStored()`
- Screen change delays from `applyScreenGeometryChange()`
- Navigation stutters across focus, restore, swap, cycle, and float toggle handlers

## [1.2.2] - 2026-02-01

### Fixed
- Async D-Bus call for floating toggle prevents compositor freeze

## [1.2.1] - 2026-02-01

### Added
- GitHub Actions CI/CD for Arch, Debian (Ubuntu 25.10), and Fedora builds
- Floating state persisted across sessions and restored correctly

### Fixed
- Debian packaging file paths and dependency declarations
- Packaging file paths and package names for release builds

## [1.2.0] - 2026-02-01

Initial packaged release. Wayland-only (X11 support removed). Requires KDE Plasma 6, KF6, and Qt 6.

### Features
- Drag windows to predefined zones with modifier key activation (Shift, Ctrl, etc.)
- Custom zone layouts with visual editor
- Multi-monitor support
- Custom shader overlays for zone visualization ([#1])
- Visual layout preview OSD ([#13])
- Per-layout gap overrides with separate edge gap setting ([#14])
- Snap-to-zone shortcuts: Meta+Ctrl+1-9 ([#15])
- Swap windows between zones: Meta+Ctrl+Alt+Arrow ([#16])
- Rotate windows clockwise/counterclockwise: Meta+Ctrl+\[ / Meta+Ctrl+\] ([#18])
- Per-activity layout assignments ([#19])
- Cycle windows within same zone ([#20])
- Navigation OSD feedback ([#21])
- Autotiling with Master-Stack, Columns, and BSP algorithms ([#40], [#42]-[#51])
- Unified layout model with autotile integration ([#58], [#60])
- KCM split into modular tab components ([#56])
- German translation ([#57])
- Systemd user service for daemon management
- KWin effect for drag feedback
- D-Bus interface for external control
- Multi-distro packaging (Arch, Debian, RPM)

### Fixed
- Settings freeze and excessive file saves ([#55])
- Session restoration and rotation after login ([#66])
- Window tracking: snap/restore behavior, zone clearing, startup timing, rotation zone ID matching, floating window exclusion ([#67])

[Unreleased]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.8.2...HEAD
[2.8.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.8.1...v2.8.2
[2.8.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.8.0...v2.8.1
[2.8.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.7.1...v2.8.0
[2.7.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.7.0...v2.7.1
[2.7.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.6.0...v2.7.0
[2.6.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.5.3...v2.6.0
[#286]: https://github.com/fuddlesworth/PlasmaZones/pull/286
[#288]: https://github.com/fuddlesworth/PlasmaZones/pull/288
[#289]: https://github.com/fuddlesworth/PlasmaZones/pull/289
[#290]: https://github.com/fuddlesworth/PlasmaZones/pull/290
[#292]: https://github.com/fuddlesworth/PlasmaZones/pull/292
[#294]: https://github.com/fuddlesworth/PlasmaZones/pull/294
[#295]: https://github.com/fuddlesworth/PlasmaZones/pull/295
[#296]: https://github.com/fuddlesworth/PlasmaZones/pull/296
[#297]: https://github.com/fuddlesworth/PlasmaZones/pull/297
[#298]: https://github.com/fuddlesworth/PlasmaZones/pull/298
[#299]: https://github.com/fuddlesworth/PlasmaZones/pull/299
[#300]: https://github.com/fuddlesworth/PlasmaZones/pull/300
[#301]: https://github.com/fuddlesworth/PlasmaZones/pull/301
[#302]: https://github.com/fuddlesworth/PlasmaZones/pull/302
[#303]: https://github.com/fuddlesworth/PlasmaZones/pull/303
[2.5.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.5.2...v2.5.3
[2.5.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.5.1...v2.5.2
[2.5.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.5.0...v2.5.1
[2.5.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.7...v2.5.0
[2.4.7]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.6...v2.4.7
[2.4.6]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.5...v2.4.6
[2.4.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.3...v2.4.5
[2.4.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.2...v2.4.3
[2.4.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.1...v2.4.2
[#211]: https://github.com/fuddlesworth/PlasmaZones/discussions/211
[#249]: https://github.com/fuddlesworth/PlasmaZones/issues/249
[#251]: https://github.com/fuddlesworth/PlasmaZones/discussions/251
[#252]: https://github.com/fuddlesworth/PlasmaZones/issues/252
[#254]: https://github.com/fuddlesworth/PlasmaZones/issues/254
[#258]: https://github.com/fuddlesworth/PlasmaZones/discussions/258
[2.4.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.4.0...v2.4.1
[2.4.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.16...v2.4.0
[2.3.16]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.15...v2.3.16
[2.3.15]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.14...v2.3.15
[2.3.14]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.13...v2.3.14
[2.3.13]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.12...v2.3.13
[2.3.12]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.11...v2.3.12
[2.3.11]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.10...v2.3.11
[2.3.10]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.9...v2.3.10
[2.3.9]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.8...v2.3.9
[2.3.8]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.7...v2.3.8
[2.3.7]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.6...v2.3.7
[2.3.6]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.5...v2.3.6
[2.3.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.3...v2.3.5
[2.3.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.2...v2.3.3
[2.3.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.1...v2.3.2
[2.3.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.3.0...v2.3.1
[2.3.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.2.1...v2.3.0
[2.2.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.2.0...v2.2.1
[2.2.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.1.0...v2.2.0
[2.1.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.0.2...v2.1.0
[2.0.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.0.1...v2.0.2
[2.0.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v2.0.0...v2.0.1
[2.0.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.15...v2.0.0
[1.15.15]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.14...v1.15.15
[1.15.14]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.13...v1.15.14
[1.15.13]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.12...v1.15.13
[1.15.12]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.11...v1.15.12
[1.15.11]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.9...v1.15.11
[1.15.9]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.8...v1.15.9
[1.15.8]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.7...v1.15.8
[1.15.7]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.6...v1.15.7
[1.15.6]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.5...v1.15.6
[1.15.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.4...v1.15.5
[1.15.4]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.3...v1.15.4
[1.15.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.2...v1.15.3
[1.15.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.15.1...v1.15.2
[1.15.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.14.1...v1.15.1
[1.14.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.14.0...v1.14.1
[1.14.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.13.1...v1.14.0
[1.13.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.12.2...v1.13.1
[1.12.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.12.1...v1.12.2
[1.12.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.12.0...v1.12.1
[1.12.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.8...v1.12.0
[1.11.8]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.7...v1.11.8
[1.11.7]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.6...v1.11.7
[1.11.6]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.5...v1.11.6
[1.11.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.4...v1.11.5
[1.11.4]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.3...v1.11.4
[1.11.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.2...v1.11.3
[1.11.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.1...v1.11.2
[1.11.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.11.0...v1.11.1
[1.11.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.10.6...v1.11.0
[1.10.6]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.10.5...v1.10.6
[1.10.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.10.4...v1.10.5
[1.10.4]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.10.3...v1.10.4
[1.10.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.10.2...v1.10.3
[1.10.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.10.0...v1.10.2
[1.10.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.9.5...v1.10.0
[1.9.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.9.3...v1.9.5
[1.9.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.9.2...v1.9.3
[1.9.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.9.1...v1.9.2
[1.9.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.8.4...v1.9.1
[1.8.4]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.8.3...v1.8.4
[1.8.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.8.1...v1.8.2
[1.8.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.8.0...v1.8.1
[1.8.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.7.0...v1.8.0
[1.7.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.6.2...v1.7.0
[1.6.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.6.1...v1.6.2
[1.6.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.6.0...v1.6.1
[1.6.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.5.9...v1.6.0
[1.5.9]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.5.3...v1.5.9
[1.5.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.3.4...v1.5.3
[1.3.4]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.3.3...v1.3.4
[1.3.3]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.3.2...v1.3.3
[1.3.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.3.1...v1.3.2
[1.3.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.3.0...v1.3.1
[1.3.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.2.6...v1.3.0
[1.2.6]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.2.5...v1.2.6
[1.2.5]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.2.4...v1.2.5
[1.2.4]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.2.2...v1.2.4
[1.2.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.2.1...v1.2.2
[1.2.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.2.0...v1.2.1
[1.2.0]: https://github.com/fuddlesworth/PlasmaZones/releases/tag/v1.2.0

[#1]: https://github.com/fuddlesworth/PlasmaZones/pull/1
[#13]: https://github.com/fuddlesworth/PlasmaZones/pull/13
[#14]: https://github.com/fuddlesworth/PlasmaZones/pull/14
[#15]: https://github.com/fuddlesworth/PlasmaZones/pull/15
[#16]: https://github.com/fuddlesworth/PlasmaZones/pull/16
[#18]: https://github.com/fuddlesworth/PlasmaZones/pull/18
[#19]: https://github.com/fuddlesworth/PlasmaZones/pull/19
[#20]: https://github.com/fuddlesworth/PlasmaZones/pull/20
[#21]: https://github.com/fuddlesworth/PlasmaZones/pull/21
[#40]: https://github.com/fuddlesworth/PlasmaZones/pull/40
[#42]: https://github.com/fuddlesworth/PlasmaZones/pull/42
[#43]: https://github.com/fuddlesworth/PlasmaZones/pull/43
[#44]: https://github.com/fuddlesworth/PlasmaZones/pull/44
[#45]: https://github.com/fuddlesworth/PlasmaZones/pull/45
[#48]: https://github.com/fuddlesworth/PlasmaZones/pull/48
[#49]: https://github.com/fuddlesworth/PlasmaZones/pull/49
[#50]: https://github.com/fuddlesworth/PlasmaZones/pull/50
[#51]: https://github.com/fuddlesworth/PlasmaZones/pull/51
[#55]: https://github.com/fuddlesworth/PlasmaZones/pull/55
[#56]: https://github.com/fuddlesworth/PlasmaZones/pull/56
[#57]: https://github.com/fuddlesworth/PlasmaZones/pull/57
[#58]: https://github.com/fuddlesworth/PlasmaZones/pull/58
[#60]: https://github.com/fuddlesworth/PlasmaZones/pull/60
[#66]: https://github.com/fuddlesworth/PlasmaZones/pull/66
[#67]: https://github.com/fuddlesworth/PlasmaZones/pull/67
[#70]: https://github.com/fuddlesworth/PlasmaZones/pull/70
[#71]: https://github.com/fuddlesworth/PlasmaZones/pull/71
[#72]: https://github.com/fuddlesworth/PlasmaZones/pull/72
[#74]: https://github.com/fuddlesworth/PlasmaZones/pull/74
[#75]: https://github.com/fuddlesworth/PlasmaZones/pull/75
[#76]: https://github.com/fuddlesworth/PlasmaZones/pull/76
[#78]: https://github.com/fuddlesworth/PlasmaZones/pull/78
[#79]: https://github.com/fuddlesworth/PlasmaZones/pull/79
[#80]: https://github.com/fuddlesworth/PlasmaZones/pull/80
[#87]: https://github.com/fuddlesworth/PlasmaZones/pull/87
[#89]: https://github.com/fuddlesworth/PlasmaZones/pull/89
[#90]: https://github.com/fuddlesworth/PlasmaZones/pull/90
[#92]: https://github.com/fuddlesworth/PlasmaZones/pull/92
[#94]: https://github.com/fuddlesworth/PlasmaZones/issues/94
[#95]: https://github.com/fuddlesworth/PlasmaZones/issues/95
[#96]: https://github.com/fuddlesworth/PlasmaZones/pull/96
[#97]: https://github.com/fuddlesworth/PlasmaZones/pull/97
[#132]: https://github.com/fuddlesworth/PlasmaZones/pull/132
[#133]: https://github.com/fuddlesworth/PlasmaZones/pull/133
[#136]: https://github.com/fuddlesworth/PlasmaZones/pull/136
[#139]: https://github.com/fuddlesworth/PlasmaZones/pull/139
[#143]: https://github.com/fuddlesworth/PlasmaZones/discussions/143
[#145]: https://github.com/fuddlesworth/PlasmaZones/issues/145
[#147]: https://github.com/fuddlesworth/PlasmaZones/issues/147
[#148]: https://github.com/fuddlesworth/PlasmaZones/issues/148
[#150]: https://github.com/fuddlesworth/PlasmaZones/issues/150
[#152]: https://github.com/fuddlesworth/PlasmaZones/issues/152
[#158]: https://github.com/fuddlesworth/PlasmaZones/issues/158
[#159]: https://github.com/fuddlesworth/PlasmaZones/issues/159
[#160]: https://github.com/fuddlesworth/PlasmaZones/discussions/160
[#164]: https://github.com/fuddlesworth/PlasmaZones/issues/164
[#168]: https://github.com/fuddlesworth/PlasmaZones/issues/168
[#169]: https://github.com/fuddlesworth/PlasmaZones/pull/169
[#170]: https://github.com/fuddlesworth/PlasmaZones/pull/170
[#172]: https://github.com/fuddlesworth/PlasmaZones/issues/172
[#174]: https://github.com/fuddlesworth/PlasmaZones/pull/174
[#156]: https://github.com/fuddlesworth/PlasmaZones/discussions/156
[#166]: https://github.com/fuddlesworth/PlasmaZones/discussions/166
[#167]: https://github.com/fuddlesworth/PlasmaZones/issues/167
[#175]: https://github.com/fuddlesworth/PlasmaZones/issues/175
[#176]: https://github.com/fuddlesworth/PlasmaZones/pull/176
[#179]: https://github.com/fuddlesworth/PlasmaZones/issues/179
[#180]: https://github.com/fuddlesworth/PlasmaZones/issues/180
[#181]: https://github.com/fuddlesworth/PlasmaZones/pull/181
[#182]: https://github.com/fuddlesworth/PlasmaZones/pull/182
[#185]: https://github.com/fuddlesworth/PlasmaZones/issues/185
[#186]: https://github.com/fuddlesworth/PlasmaZones/pull/186
[#187]: https://github.com/fuddlesworth/PlasmaZones/issues/187
[#188]: https://github.com/fuddlesworth/PlasmaZones/pull/188
[#256]: https://github.com/fuddlesworth/PlasmaZones/pull/256
[#259]: https://github.com/fuddlesworth/PlasmaZones/pull/259
[#260]: https://github.com/fuddlesworth/PlasmaZones/pull/260
[#261]: https://github.com/fuddlesworth/PlasmaZones/pull/261
[#263]: https://github.com/fuddlesworth/PlasmaZones/pull/263
[#264]: https://github.com/fuddlesworth/PlasmaZones/pull/264
[#265]: https://github.com/fuddlesworth/PlasmaZones/pull/265
[#266]: https://github.com/fuddlesworth/PlasmaZones/pull/266
[#268]: https://github.com/fuddlesworth/PlasmaZones/pull/268
[#269]: https://github.com/fuddlesworth/PlasmaZones/pull/269
[#270]: https://github.com/fuddlesworth/PlasmaZones/pull/270
[#272]: https://github.com/fuddlesworth/PlasmaZones/discussions/272
[#273]: https://github.com/fuddlesworth/PlasmaZones/discussions/273
[#275]: https://github.com/fuddlesworth/PlasmaZones/discussions/275
[#274]: https://github.com/fuddlesworth/PlasmaZones/pull/274
[#276]: https://github.com/fuddlesworth/PlasmaZones/pull/276
[#277]: https://github.com/fuddlesworth/PlasmaZones/pull/277
[#278]: https://github.com/fuddlesworth/PlasmaZones/pull/278
[#279]: https://github.com/fuddlesworth/PlasmaZones/pull/279
[#280]: https://github.com/fuddlesworth/PlasmaZones/pull/280
[#281]: https://github.com/fuddlesworth/PlasmaZones/pull/281
[#282]: https://github.com/fuddlesworth/PlasmaZones/pull/282
