# PlasmaZones RPM Spec File
# FancyZones-style window tiling for KDE Plasma
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build: rpmbuild -ba plasmazones.spec
# Clean build: mock -r fedora-43-x86_64 plasmazones-1.2.0-1.fc43.src.rpm
#
# Version is set by CI from the git tag (e.g. v1.3.4 -> 1.3.4). Local builds use the value below.

Name:           plasmazones
Version:        1.8.4
Release:        1%{?dist}
Summary:        FancyZones-style window tiling for KDE Plasma

License:        GPL-3.0-or-later
URL:            https://github.com/fuddlesworth/PlasmaZones
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

# Plasma 6 / Wayland only
ExclusiveArch:  x86_64 aarch64

# Build tools
BuildRequires:  cmake >= 3.16
BuildRequires:  ninja-build
BuildRequires:  extra-cmake-modules >= 6.0.0
BuildRequires:  gcc-c++
BuildRequires:  gettext

# Qt6
BuildRequires:  qt6-qtbase-devel >= 6.6.0
BuildRequires:  qt6-qtbase-private-devel
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qttools-devel
BuildRequires:  qt6-qtshadertools-devel

# KDE Frameworks 6
BuildRequires:  kf6-kconfig-devel >= 6.0.0
BuildRequires:  kf6-kconfigwidgets-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kdbusaddons-devel
BuildRequires:  kf6-ki18n-devel
BuildRequires:  kf6-kcmutils-devel
BuildRequires:  kf6-kwindowsystem-devel
BuildRequires:  kf6-kglobalaccel-devel
BuildRequires:  kf6-knotifications-devel
BuildRequires:  kf6-kcolorscheme-devel

# Plasma/KWin
BuildRequires:  kwin-devel
BuildRequires:  layer-shell-qt-devel

# Optional
BuildRequires:  plasma-activities-devel

# Systemd macros
BuildRequires:  systemd-rpm-macros
%{?systemd_requires}

# Runtime dependencies
Requires:       qt6-qtbase >= 6.6.0
Requires:       qt6-qtdeclarative
Requires:       qt6-qtshadertools
Requires:       kf6-kconfig
Requires:       kf6-kconfigwidgets
Requires:       kf6-kcoreaddons
Requires:       kf6-kdbusaddons
Requires:       kf6-ki18n
Requires:       kf6-kcmutils
Requires:       kf6-kwindowsystem
Requires:       kf6-kglobalaccel
Requires:       kf6-knotifications
Requires:       kf6-kcolorscheme
Requires:       layer-shell-qt
Requires:       kwin
Requires:       hicolor-icon-theme

%description
PlasmaZones brings FancyZones-style window tiling to KDE Plasma 6.

Features:
- Drag windows to predefined zones
- Custom zone layouts with visual editor
- Modifier key activation (Shift, Ctrl, etc.)
- Multi-monitor support
- Keyboard navigation between zones
- Activity-based layout switching
- Wayland-native using Layer Shell

%prep
%autosetup -n PlasmaZones-%{version}

%build
%cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=OFF
%cmake_build

%install
%cmake_install

%post
# Refresh KDE service cache
/usr/bin/kbuildsycoca6 --noincremental 2>/dev/null || :
# Update icon cache
/usr/bin/gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
%systemd_user_post plasmazones.service
echo ""
echo "PlasmaZones: the KWin effect is enabled by default, but KWin must"
echo "be restarted to load it. Log out and back in, or run:"
echo "  kwin_wayland --replace &"
echo ""

%preun
%systemd_user_preun plasmazones.service

%postun
/usr/bin/kbuildsycoca6 --noincremental 2>/dev/null || :
/usr/bin/gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
%systemd_user_postun_with_restart plasmazones.service

%files
%license LICENSE
%doc README.md

# Executables
%{_bindir}/plasmazonesd
%{_bindir}/plasmazones-editor

# Libraries
%{_libdir}/libplasmazones_core.so*
%{_libdir}/libplasmazones_rendering.so*

# KWin effect plugin
%{_libdir}/qt6/plugins/kwin/effects/plugins/kwin_effect_plasmazones.so

# KCM (System Settings module)
%{_libdir}/qt6/plugins/plasma/kcms/systemsettings/kcm_plasmazones.so

# QML modules (KDE KCM QML files)
%{_libdir}/qt6/qml/org/kde/kcms/plasmazones/

# D-Bus interfaces
%{_datadir}/dbus-1/interfaces/org.plasmazones.*.xml

# Desktop files
%{_datadir}/applications/org.plasmazones.editor.desktop
%{_datadir}/applications/org.plasmazones.daemon.desktop
%{_datadir}/applications/kcm_plasmazones.desktop

# Data files
%{_datadir}/plasmazones/

# Icons
%{_datadir}/icons/hicolor/scalable/apps/plasmazones.svg
%{_datadir}/icons/hicolor/scalable/apps/plasmazones-editor.svg
%{_datadir}/icons/hicolor-light/scalable/apps/plasmazones.svg
%{_datadir}/icons/hicolor-light/scalable/apps/plasmazones-editor.svg

# KConfig
%{_datadir}/config.kcfg/plasmazones.kcfg

# Systemd user service
%{_userunitdir}/plasmazones.service

# Snap Assist: env.d config for KWin screenshot auth (system installs use .desktop; this is fallback)
%{_libdir}/environment.d/99-plasmazones-screenshot.conf

# Translations (optional - may not exist yet)
%{_datadir}/locale/*/LC_MESSAGES/plasmazonesd.mo

%changelog
* Wed Feb 11 2026 fuddlesworth - 1.8.4-1
- Shader preset load/save in editor ShaderSettingsDialog
- Preview shader effects in zone editor ([#132])
- Restore window size immediately when dragging between zones ([#133])
- Overlay follows cursor when dragging to another monitor ([#136])
- Defer window resize until drag release; keep restore-to-float on unsnap
- Hide shader preview overlay when dialogs open or app loses focus
- PR review feedback for shader preview
- Dead `zoneGeometryDuringDrag` slot

* Mon Feb  9 2026 fuddlesworth - 1.8.2-1
- Full zone label font customization: family, size scale, weight, italic, underline, strikeout ([#97])
- Font picker dialog in KCM Zones tab with live preview and search
- Sonic Ripple audio-reactive shader
- Rename `NumberColor` setting to `LabelFontColor` for consistent `LabelFont*` naming across all layers ([#97])
- Sort layouts alphabetically by name in KCM
- Use generic `adjustlevels` icon for shader settings button in editor (replaces app-specific icon)
- Self-referencing `font.family` QML binding preventing font reset from updating previews ([#97])
- Font reset button now also resets label size scale
- `qFuzzyCompare` edge case in KCM font scale setter (clamp before compare)
- Remove dead `labelFontColor` property from zone selector window
- Auto badge distinguished from Manual badge using `activeTextColor`

* Mon Feb  9 2026 fuddlesworth - 1.8.1-1
- Paint-to-span zone modifier: hold a modifier while dragging to progressively paint across zones, window snaps to bounding rectangle on release ([#94], [#96])
- Configurable "Paint-to-span modifier" in KCM Zones tab (default Alt+Meta)
- Renamed "Multi-zone modifier" to "Proximity snap modifier" for clarity
- Replaced `middleClickMultiZone` bool setting with `zoneSpanModifier` DragModifier enum
- Config migration: users who had middle-click multi-zone disabled keep zone span disabled after upgrade
- Extracted `prepareHandlerContext()`, `computeCombinedZoneGeometry()`, and `zoneIdsToStringList()` helpers in drag handling (DRY)
- Added `setOsdStyleInt` range validation
- Dead `skipSnapModifier` setting (fully scaffolded but never consumed in drag handling)
- Missing `restoreWindowsToZonesOnLoginChanged` signal in KCM defaults and settings sync
- 12 missing signal emissions in KCM `onSettingsChanged()`
- Painted zone state not cleared on `dragStarted()` causing stale highlights
- Modifier conflict warning using `static bool` instead of per-instance member

* Mon Feb  9 2026 fuddlesworth - 1.8.0-1
- CAVA audio visualization service for audio-reactive shaders ([#92])
- Spectrum Pulse shader: audio-reactive neon energy with bass glow, spectrum aurora, and CAVA integration ([#92])
- Audio-reactive shader uniforms: spectrum data and audio levels passed to GPU ([#92])
- KCM settings for audio visualizer (enable/disable, spectrum bar count)
- Auto-assign windows to first empty zone per layout ([#90])
- App-to-zone auto-snap rules per layout with screen-targeting
- Window picker dialog for exclude lists
- Per-monitor zone selector settings ([#89])
- Snap-all-windows shortcut (`Meta+Ctrl+S`)
- Replace global active layout with `defaultLayout()` for user-facing surfaces
- DRY per-screen config validation and shared layout computation
- Audit and normalize log levels across entire codebase
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

* Fri Feb  6 2026 fuddlesworth - 1.7.0-1
- Layout visibility filtering: control which layouts appear in zone selector per screen, virtual desktop, and activity
   - Tier 1 (KCM): eye toggle to globally hide a layout from the zone selector
   - Tier 2 (Editor): visibility popup to restrict layouts to specific screens, desktops, or activities
   - Empty allow-lists = visible everywhere (opt-in model)
   - Active layout always bypasses filters to prevent empty selector state
   - Undo/redo support for visibility changes in the editor
   - Filter badge on KCM layout cards when Tier 2 restrictions are active
- Layout cycling (Meta+[/]) now respects per-screen visibility filtering
- OSD style defaults to visual preview instead of text for new installs
- Duplicated and imported layouts no longer inherit visibility restrictions from the source
- Stale screen names auto-cleaned from layout restrictions when monitors are disconnected
- Layout cycling skips hidden/restricted layouts correctly in all directions

* Fri Feb  6 2026 fuddlesworth - 1.6.2-1
- Editor not moving to selected screen when switching monitors in TopBar or via D-Bus `openEditorForScreen`
- Editor defaulting to wrong screen on Wayland (now uses cursor screen instead of unreliable `primaryScreen`)

* Fri Feb  6 2026 fuddlesworth - 1.6.1-1
- Liquid Metal shader: mercury-like fluid surface with environment reflections, Fresnel, bloom, and mouse interaction
- AUR `-bin` package build failure due to `.INSTALL` dotfile left in package root
- Liquid Metal: surface drifting to bottom-left (use standing waves instead of travelling)
- Liquid Metal: inverted mouse Y coordinate
- Liquid Metal: outer glow rendering outside zones due to zoneParams swizzle bug
- 5 low-quality shaders: minimalist, aurora-sweep, warped-labels, prism-labels, glitch-labels

* Thu Feb  5 2026 fuddlesworth - 1.6.0-1
- Multi-zone snapping support in window tracking
- Shader parameters from previously-used shaders accumulating in layout JSON
- Atomic undo for shader switching (single undo step instead of two)
- Post-install messages now note that KWin restart is required to load the effect
- Dead properties: Layout::author, Layout::shortcut, Zone::shortcut (never wired up)
- Dead files: ZoneEditor.qml, LayoutPicker.qml, ShaderOverlay.qml, shadercompiler.cpp, zonedataprovider.cpp

* Thu Feb  5 2026 fuddlesworth - 1.5.9-1
- Release pipeline now generates Debian, RPM, and GitHub release notes from CHANGELOG.md
- Missing pacman hook files for sycoca cache refresh in Arch package
- POSIX awk compatibility in changelog generator (mawk on Ubuntu)
- AUR publish: mount PKGBUILD read-only and generate .SRCINFO via stdout to avoid docker chown breaking host git ownership

* Thu Feb  5 2026 fuddlesworth - 1.5.2-1
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
- Buffer pass alpha blending in RHI renderer (SrcAlpha darkened output on RGBA-cleared textures)
- Duplicate zone IDs and use-after-free in editor undo system
- Help dialog redesigned; fullscreen exit button repositioned
- Update check button layout shift when status message appears

* Tue Feb  3 2026 fuddlesworth - 1.3.4-1
- RHI/Vulkan zone overlay renderer replaces OpenGL path ([#76])
- Packaging updated to use Fedora 43
- `#include` directive support in shaders (`common.glsl`, `multipass.glsl`)
- Shader performance improvements and error recovery
- Context loss and reinitialization in overlay renderer

* Tue Feb  3 2026 fuddlesworth - 1.3.3-1
- Resnap-to-new-layout shortcut ([#75])
- Shortcut consolidation: merged redundant key bindings
- Build/install only installs files; packaging (postinst, RPM %post) handles sycoca refresh and daemon enable
- Logging alignment issues

* Tue Feb  3 2026 fuddlesworth - 1.3.2-1
- KWin PlasmaZones effect enabled by default on install
- Autotiling feature removed ([#74])

* Mon Feb  2 2026 fuddlesworth - 1.3.1-1
- Debian releases now include debug symbol packages (.ddeb)

* Mon Feb  2 2026 fuddlesworth - 1.3.0-1
- Build paths for Arch and Debian packaging
- Session restore validates layout matches before restoring windows
- CI and release version badges in README

* Mon Feb  2 2026 fuddlesworth - 1.2.6-1
- KWin effect metadata aligned with KWin conventions
- CI pipeline simplified and packaging reorganized
- Debug symbol packages enabled for all distros
- Float/unfloat preserves pre-snap geometry across window close/reopen cycles ([#72])
- Autotiling settings page in KCM ([#71])

* Sun Feb  1 2026 fuddlesworth - 1.2.5-1
- Navigation OSD with multi-monitor support and UX fixes ([#70])

* Sun Feb  1 2026 fuddlesworth - 1.2.4-1
- All remaining synchronous D-Bus calls in KWin effect converted to async, preventing compositor thread blocking
- Startup freezes from `syncFloatingWindowsFromDaemon()`
- Window event stutters from `ensurePreSnapGeometryStored()`
- Screen change delays from `applyScreenGeometryChange()`
- Navigation stutters across focus, restore, swap, cycle, and float toggle handlers

* Sun Feb  1 2026 fuddlesworth - 1.2.2-1
- Async D-Bus call for floating toggle prevents compositor freeze

* Sun Feb  1 2026 fuddlesworth - 1.2.1-1
- GitHub Actions CI/CD for Arch, Debian (Ubuntu 25.10), and Fedora builds
- Floating state persisted across sessions and restored correctly
- Debian packaging file paths and dependency declarations
- Packaging file paths and package names for release builds

* Sun Feb  1 2026 fuddlesworth - 1.2.0-1
- Initial packaged release. Wayland-only (X11 support removed). Requires KDE Plasma 6, KF6, and Qt 6.
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
- Settings freeze and excessive file saves ([#55])
- Session restoration and rotation after login ([#66])
- Window tracking: snap/restore behavior, zone clearing, startup timing, rotation zone ID matching, floating window exclusion ([#67])
