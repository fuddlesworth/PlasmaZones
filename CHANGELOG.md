# Changelog

All notable changes to PlasmaZones are documented in this file.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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

[1.8.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.8.0...v1.8.1
[1.8.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.7.0...v1.8.0
[1.7.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.6.2...v1.7.0
[1.6.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.6.1...v1.6.2
[1.6.1]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.6.0...v1.6.1
[1.6.0]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.5.9...v1.6.0
[1.5.9]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.5.2...v1.5.9
[1.5.2]: https://github.com/fuddlesworth/PlasmaZones/compare/v1.3.4...v1.5.2
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
[#96]: https://github.com/fuddlesworth/PlasmaZones/pull/96
