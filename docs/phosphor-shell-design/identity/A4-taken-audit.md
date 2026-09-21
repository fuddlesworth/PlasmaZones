<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# A4 — "What is already taken" audit for the Phosphor shell

Date: 2026-09-04. Method: web search + fetch of project READMEs, docs, release
blogs, source files and vendor design pages. Every claim carries a URL; where a
fetch failed or a detail could not be confirmed it is marked **(unverified)**.
Screenshots and videos could not be viewed directly, so "silhouette" calls are
taken from the projects' own docs/config options and release notes.

---

## 1. Per-shell survey

Legend for bar silhouette: slab = full-width edge-attached strip; island(s) =
one or more detached rounded groups; capsule = single floating pill; edge = full
width but visually attached (concave/"gothic" corners); none.

### DankMaterialShell (DMS) — Quickshell + Go
- Bar: configurable. Default DankBar is an edge-attached slab with optional
  **"gothic corners"** (concave fillets tying the bar into the screen) and a
  **"connected surface" mode** that "ties modals (such as the launcher),
  popouts, and notifications into the bar and frame"
  ([1.5 blog](https://danklinux.com/blog/v1-5-release)). Since 1.6 there is also
  **Dank Island**, "a new bar instance built around a central island with
  satellite widgets"; "the face of the island reacts dynamically to user
  interactions and system events - such as notifications, media playback, and
  OSD triggers" and "opens into the dash and control center in place, on the
  spring" ([1.6 blog](https://danklinux.com/blog/v1-6-release)).
- Popouts: connected corner (connected-surface mode) or detached card; hover-to-open option.
- Launcher: "Spotlight-style" centred slab (DankSearch) ([README](https://github.com/AvengeMedia/DankMaterialShell)).
- Control center: unified panel (network/BT/audio/display/night mode), widget-organisable, plugin tiles.
- OSD: island face reacts to OSD triggers (island mode); conventional OSD otherwise.
- Notifications: grouped centre with history.
- Lock/greeter: dank-greeter, light/dark variants.
- Material: M3 with "reworked Material 3-like shadow system", elevation controls, blur across surfaces.
- Radius family: M3 (large radii, configurable).
- Motion: "spring physics animations", "Directional" and "Depth" animation modes (1.5 blog).
- Theming: matugen + dank16, retints GTK/Qt/terminals/editors ([README](https://github.com/AvengeMedia/DankMaterialShell)).
- Distinctive: the island as the single expansion point for dash/CC/launcher/notifications; connected-surface mode; display profiles.
- Stated refs: Material 3, macOS Spotlight; docs credit Noctalia for some visual effects.

### Noctalia — Quickshell (v5 moved to direct Wayland/GLES)
- Bar: fully parametric. `margin_edge > 0` floats it; per-corner `radius`; **negative radius / `concave_edge_corners`** carve inward corners on an attached bar; widgets sit in **capsules** (pill backgrounds, `capsule_thickness` default 0.76 of bar) ([bar docs](https://docs.noctalia.dev/noctalia/bar/)).
- Popouts: attached panels, `panel_overlap` pulls the panel into the bar and `contact_shadow` draws a gradient at the seam (same doc). Connected-corner language is explicit.
- Launcher: stylised list with favourites/recent/calc/clipboard providers.
- Control center: side panel with media, weather, power profiles, toggles ([README](https://github.com/noctalia-dev/noctalia-shell)).
- OSD: overlay pill; notifications: toasts + history panel; lock: session-lock; dock; desktop widgets.
- Material: soft, "warm lavender", flat-with-shadow; "quiet by design".
- Motion: "smooth animations", nothing signature (unverified beyond docs).
- Theming: wallpaper colour generation; template application (wallust fork exists).
- Traction: 2026 write-ups position it as the "middle ground" vs DMS/Caelestia "aesthetic-over-functionality rice" ([tux.fan 2026-07](https://tux.fan/2026/07/14/noctalia-wayland-desktop-shell-2026/)).

### HyprPanel — Astal/AGS (archived 2026-04-27; successor Wayle, Rust/GTK4)
- Bar: module **buttons as pills** ("islands"), floating or docked, corner radius option ([README](https://github.com/Jas-SinghFSU/HyprPanel), [config](https://hyprpanel.com/configuration/panel.html)).
- Popouts: detached dropdown menus under each module.
- Dashboard: profile card + power buttons + shortcuts grid + directories + resource bars ([modules doc](https://hyprpanel.com/configuration/modules.html)).
- OSD: slider pill; notifications: menu + toasts.
- Theming: named palettes (Catppuccin, Dracula, Gruvbox, Nord, Tokyo Night...) + pywal/matugen.
- Motion: generic GTK reveals. Wayle promises "a real design system" ([wayle](https://github.com/wayle-rs/wayle)).

### end-4 illogical-impulse (ii) — Quickshell
- Bar: slab, top, with **rounded screen corners** drawn by the shell (`screenRounding: large` = 23 px) ([Appearance.qml](https://raw.githubusercontent.com/end-4/dots-hyprland/main/dots/.config/quickshell/ii/modules/common/Appearance.qml)).
- Popouts: left/right **sliding sidebars** (quick settings, AI chat), overview grid with live previews, dock, crosshair overlay ([README](https://github.com/end-4/dots-hyprland)).
- Launcher: overview + search field (Windows-11-like).
- Radius family: 2/6/8/12/17/23/30, window 18.
- Motion: **M3 Expressive beziers with overshoot** — `expressiveFastSpatial [0.42,1.67,0.21,0.90]` 350 ms, default 500 ms, slow 650 ms, `emphasizedDecel [0.05,0.7,0.1,1]`, `elementMoveEnter 400 / Exit 200`, `clickBounce 400`.
- Theming: Material You from wallpaper.
- Stated refs: "Material Design 3, Windows 11, and sci-fi" ([README](https://github.com/end-4/dots-hyprland)). Second family "Waffle" = centred Windows-style taskbar.

### Caelestia shell — Quickshell + C++ plugins
- Bar: left **vertical** bar; screen gets a rounded **border frame** (`border.thickness/rounding/smoothing`) ([README](https://raw.githubusercontent.com/caelestia-dots/shell/main/README.md)). (Orientation from README config keys; screenshots not viewed.)
- Popouts: drawers that **morph** out of the bar ("A fluid, morphing shell"); dashboard/launcher/session/sidebar `showOnHover`.
- Launcher: command palette + app drawer.
- Motion: signature is shared-element style morphing between bar and drawers; `Appearance.qml` could not be fetched (404), so exact durations **unverified**.
- Theming: M3 via matugen; cava plugin in C++.
- Stated refs: M3.

### Ax-Shell — Fabric (GTK3); successor on Quickshell announced
- Bar: **"Notch" panel theme** — a top-centre notch that behaves as a dynamic island; community calls it "the Arch Linux dynamic island" ([Ax-Shell](https://github.com/Axenide/Ax-Shell), [Threads post](https://www.threads.com/@1ar.io/post/DGxnAZpsdWs/)).
- Dashboard tabs, launcher, wallpaper selector, notifications, power menu, cava dependency ([README](https://github.com/Axenide/Ax-Shell)).
- Theming: matugen. Stated ref: Material You + Dynamic Island.

### ML4W — Waybar/rofi/swaync + Hyprland
- Bar: several Waybar themes (slab and floating pill variants), rofi launcher, a sidebar/dashboard, "adaptive material color themes based on the selected wallpaper" ([README](https://github.com/mylinuxforwork/dotfiles)). Nothing signature; it is the mainstream Waybar look.

### Waybar ecosystem norms
- Modules grouped with `group/*` and CSS `border-radius` into **pills** ("some call them islands") ([HyDE Waybar](https://hydeproject.pages.dev/de/configuring/waybar/), [hyprdots #2046](https://github.com/prasanthrangan/hyprdots/discussions/2046)); workspaces as dots/numbers; Catppuccin palette; cava module with raw and GLSL frontends ([Waybar wiki](https://github.com/Alexays/Waybar/wiki/Module:-Cava)).

### AGS / Astal shells
- Official showcase: Marble Shell, kompass, Epik Shell, colorshell, Delta Shell, HyprPanel ([showcases](https://aylur.github.io/astal/showcases)). Matshell = "Material Design themed GTK4 desktop & laptop shell" ([matshell](https://github.com/Neurarian/matshell)). All M3 pill/slab variants; GTK reveal transitions.

### Quickshell showcase / 2026 traction
- quickshell.org showcase page fetch was blocked (403) **(unverified)**. GitHub topic `quickshell` in 2026 is dominated by DMS, Noctalia, Caelestia, ii and forks; ekremx25/quickshell adds a "10-band EQ"; iNiR = ii ported to niri ([iNiR](https://snowarch.github.io/iNiR/docs/)). The recurring 2026 blog theme is consolidation into one `Theme.qml` ([zackbartel 2026-07](https://zackbartel.com/blog/2026/07/quickshell/)).

### GNOME Shell 47–49
- Bar: top slab with a **pill indicator** for the quick-settings cluster; popouts are detached cards; launcher = fullscreen overview with grid; control center = Quick Settings tile grid; notifications stacked per-app (48) ([GNOME 48](https://alternativeto.net/news/2025/3/gnome-48-bengaluru-launches-with-stacked-notifications-hdr-support-and-much-more/)); 49 added "scale animations for notifications and pop-overs" and "quad" animations for the shade ([GNOME 49](https://www.omgubuntu.co.uk/2025/09/gnome-49-new-features)). Motion: ease-out-quad, ~250–300 ms; own tracker criticises curves that "accelerate excessively" ([gnome-shell #4856](https://gitlab.gnome.org/GNOME/gnome-shell/-/work_items/4856)). Material: flat Adwaita, no blur.

### COSMIC (System76)
- Bar: top panel + bottom dock, both applet-based; "square or rounded" corners, opacity; three shape styles "Round, slightly round and square" ([UX page](https://system76.com/cosmic/ux), [linuxiac](https://linuxiac.com/cosmic-desktop-adds-rounded-corners-and-window-shadows/)). Popouts: detached popovers. Launcher: centred search slab. Motion: sparse; workspace overview has spread animations; app-level animation still an open issue ([libcosmic #1116](https://github.com/pop-os/libcosmic/issues/1116)). Material: flat, Iced-rendered.

### KDE Plasma 6 (default + Panel Colorizer)
- Bar: **floating panel by default**, de-floats when a window touches it, popups "touch the edge of a floating Panel" and are centred on their icon ([pointieststick](https://pointieststick.com/2023/05/11/plasma-6-better-defaults/)). Nate Graham on the float: "Just fancy, no UX benefit", motivated by differentiation from Windows 11. Panel Colorizer injects per-widget pill backgrounds to "replicate the famous WM status bar look", 30+ presets ([Panel Colorizer](https://github.com/luisbocanegra/plasma-panel-colorizer)). 6.5 rounded all Breeze window corners ([OMG! Ubuntu](https://www.omgubuntu.co.uk/2025/10/kde-plasma-6-5-new-features-changes-release)). Material: blur behind translucent Breeze panels; motion: 100–250 ms ease-out (Kirigami units).

### niri's own overlays
- Overview (25.05): zoom-out that "fans out every workspace" with per-workspace backdrops; hot corner + 4-finger swipe ([release](https://github.com/niri-wm/niri/releases/tag/v25.05)). Hotkey overlay and exit-confirm are centred dialogs with full-screen dimming and "a nice open/close animation" ([discussion #3689](https://github.com/niri-wm/niri/discussions/3689)). All motion is **critically-damped springs**: workspace-switch stiffness 1000, view/window move/resize 800, overview 800, config-notification damping 0.6 / 1000; window-open 150 ms ease-out-expo, close 150 ms ease-out-quad ([animations wiki](https://github.com/niri-wm/niri/wiki/Configuration:-Animations)). Custom GLSL open/close/resize shaders are a niri-only capability among compositors. Community minimaps: nirimap, niri-ribbon ("viewport mini-map widget tracking off-screen windows") ([awesome-niri](https://github.com/niri-wm/awesome-niri)).

### Hyprland: hyprbars / hyprexpo / plugins
- hyprbars "adds title bars to windows"; borders-plus-plus adds extra borders; hyprfocus flashes focus ([plugins repo](https://github.com/hyprwm/hyprland-plugins)). hyprexpo is the workspace grid overview with swipe gesture (README fetch 404, **details unverified**). Core: bezier animations in deciseconds (`windows 7 myBezier`, `popin 80%`), gradient border with `borderangle` loop, 0.51 added 1:1 trackpad gestures and popup fades ([wiki](https://wiki.hypr.land/Configuring/Advanced-and-Cool/Animations/), [0.51](https://hypr.land/news/update51/)). hyprglass plugin brings Liquid-Glass refraction to Hyprland ([hyprglass](https://github.com/hyprnux/hyprglass)).

### macOS Tahoe (Liquid Glass)
- Menu bar has "no longer a visible background" (fully transparent); Dock/widgets are "multiple layers of Liquid Glass, with specular highlights"; controls "morph" and are "concentric with the rounded corners of modern hardware" ([Apple newsroom](https://www.apple.com/newsroom/2025/06/apple-introduces-a-delightful-and-elegant-new-software-design/), [MacRumors](https://www.macrumors.com/2025/09/16/10-macos-tahoe-features/)). Reception: legibility complaints; 26.1 added Clear/Tinted toggle that reviewers say barely helps ([Eclectic Light](https://eclecticlight.co/2025/11/09/last-week-on-my-mac-tahoe-26-1-disappointments/)). Control Center: tile grid, pages. Notifications: stacked cards top-right.

### Windows 11 (24H2–26H2)
- Centred taskbar slab, Mica/Acrylic backdrops on Quick Settings and Notification Center; 2026 update restores taskbar on any edge and adds Start size presets ([Windows Central](https://www.windowscentral.com/microsoft/windows-11/windows-11-2026-update-26h2-changes-for-start-menu-taskbar-and-search)). Motion: WinUI durations 83/167/250 ms, decelerate `cubic-bezier(0,0,0,1)`, accelerate `cubic-bezier(1,0,1,1)` ([MS Learn](https://learn.microsoft.com/en-us/windows/apps/design/motion/timing-and-easing)).

---

## 2. TAKEN table

| Idea | Who owns it visibly | Strength |
|---|---|---|
| Connected-corner popouts (popout fused to bar with fillets) | Noctalia (`panel_overlap`, `contact_shadow`), DMS connected-surface mode, Plasma 6 popup-touches-panel | **common → signature for Noctalia/DMS** |
| Concave / "gothic" bar corners into the screen edge | DMS gothic corners, Noctalia negative radius, ii/Caelestia rounded screen frame | **signature of the Quickshell scene** |
| Floating island pills (module groups as pills) | Waybar `group/*` pills, HyprPanel, Noctalia capsules, Panel Colorizer | **ubiquitous** |
| Dynamic-island expanding notch | Ax-Shell "Notch", DMS Dank Island (1.6) | **signature (two owners)**; also Apple's own |
| Glassmorphism / blur | macOS Liquid Glass, Windows Mica/Acrylic, Plasma Breeze, DMS blur, hyprglass | **ubiquitous** (and currently contested on legibility) |
| Matugen / M3 retint from wallpaper | DMS, Noctalia, ii, Caelestia, Ax-Shell, ML4W, HyprPanel, Matshell | **ubiquitous** (the default assumption of the scene) |
| Workspace dots / numbered pills that stretch for active | Waybar, ii, Noctalia, DMS, GNOME dots | **ubiquitous** |
| Raycast/Spotlight centred launcher slab | DMS "Spotlight-style", COSMIC launcher, Caelestia palette, macOS | **ubiquitous** |
| Tile-grid control center | GNOME Quick Settings, macOS/iOS CC, DMS CC, Windows Quick Settings | **ubiquitous** |
| Dashboard with tabs (media / perf / weather / shortcuts) | HyprPanel dashboard, Ax-Shell, Caelestia dashboard, ii sidebars | **common** |
| Pill OSD (volume/brightness slider capsule) | every shell surveyed; GNOME, Plasma, macOS | **ubiquitous** |
| Clock-over-media lockscreen | ii, Caelestia, DMS, Noctalia, GNOME/macOS/Windows | **ubiquitous** |
| cava bars in the bar | Waybar cava module (raw + GLSL), Noctalia widget, Caelestia C++ plugin, Ax-Shell | **common** |
| Neon / gradient borders, rotating border angle | Hyprland `borderangle`, borders-plus-plus, hyprdots rices | **common** (dated) |
| Scanline / CRT overlay on the whole desktop | ctr-glitch-overlay (GTK layer shell: scanlines, vignette, RGB shift, phosphor glow), cool-retro-term, RetroArch CRT-Royale, Cathode | **claimed as a gimmick, not as a design language**. No surveyed *shell* uses CRT/phosphor as its material metaphor; it exists only as a full-screen filter or a terminal skin. |
| Spring physics | niri (all motion), DMS 1.5+, M3 Expressive spec, ii beziers with overshoot | **common → signature for niri** |
| Shared-element morphs (bar ↔ drawer) | Caelestia ("fluid, morphing"), DMS island opening "in place", Liquid Glass control morphs | **signature (Caelestia)** |
| Gesture-driven drawers / overview | niri overview swipe, hyprexpo swipe, GNOME 3-finger, Hyprland 0.51 1:1 gestures | **common** |
| Bar reflecting the live window layout | **Nobody draws the tiling tree or strip position in the bar.** Closest: awesomeWM `layoutbox` (static icon per layout), dwm `[]=` symbol, niri community minimaps (nirimap, niri-ribbon) as separate overlay widgets, ii/iNiR reading `pos_in_scrolling_layout` only to order taskbar entries | **open** (see §3) |
| Fullscreen overview with live previews | GNOME, niri, hyprexpo, ii, COSMIC | **ubiquitous** |
| Rounded screen corners drawn by the shell | ii (`screenRounding 23`), Caelestia border frame, Hyprland `rounding` | **common** |
| Transparent bar with no background | macOS Tahoe menu bar | **signature (Apple)** |

## 3. OPEN list

1. **Bar as a live map of the placement engine.** No surveyed shell draws the snap-zone layout, the tiling tree, or the scrolling strip's viewport position in the bar itself. They cannot: they read compositor IPC after the fact, and niri/Hyprland expose window rects but not the layout *intent* (zones, tree splits, strip offset). Phosphor owns the engines, so the bar can render the model, not a reconstruction. (Minimaps exist only as separate overlays for niri.)
2. **Popouts that know what is under them.** External shells cannot ask "which window/zone is beneath this popout"; a compositor-shell can dodge, dim, or anchor to zone edges rather than bar edges.
3. **Mode-aware OSD/notification placement** (snap vs tile vs scroll choose different anchors). Nobody has three placement modes.
4. **Window animation and shell animation on one clock/curve family.** niri comes closest (springs everywhere) but its layer-shell shells cannot join the spring; everyone else has two motion systems (compositor beziers vs shell QML/GTK). Phosphor can make a popout's spring the same object as a window's snap-in spring.
5. **Shared-element morph between a *window* and a shell surface** (e.g. a window collapsing into its bar entry, a notification unfolding into the app window). Shells cannot capture window textures with layout intent; a compositor can.
6. **Drag feedback in the bar during a window drag** (zone highlights mirrored in the bar; the bar becoming a drop target that maps to zones). Requires drag state, which only the compositor has.
7. **Afterglow / persistence as a *state* signal** (surveyed as untaken; not adopted, see A1 §7 item 2 and §6 below) (a surface that was just active decays rather than snapping off): nobody uses temporal decay as UI semantics; CRT projects use it purely as a filter.
8. **Non-blur depth.** With glass contested (Tahoe legibility, GNOME's flat stance), a depth language built on emissive edges and decay instead of backdrop blur is untaken by any shell.
9. **Per-output overlays that are true compositor render passes** (niri does this for its own dialogs; no third-party shell can). Phosphor can render OSD/lock/picker without layer-shell round trips.
10. **Gesture drawers that scrub the strip itself** (a swipe on the bar pans the scrolling strip with 1:1 coupling). Requires owning the scroll engine.

## 4. RISKY list

| Open idea | Why it reads gimmicky/dated | How to keep the metaphor |
|---|---|---|
| Literal scanline overlay | Owned by ctr-glitch-overlay / cool-retro-term / RetroArch; reads as "retro terminal" instantly | Never a full-screen or full-surface pattern. If a raster texture exists at all it lives inside a single accent element at sub-1% contrast, or not at all. |
| Chromatic aberration / RGB shift | cool-retro-term default profile, glitch overlays | Do not use. Colour fringing signals malfunction, not persistence. |
| Barrel curvature / vignette | Cathode, cool-retro-term | Do not use; it breaks layout alignment and hits fractional-scale rendering. |
| Flicker / jitter / noise | cool-retro-term "flickering and static noise" | Do not use. Motion budget goes to decay envelopes, never to random perturbation. |
| Green/amber monochrome | Every CRT theme | Palette must stay full-colour and matugen-neutral; the phosphor idea is *decay over time*, not *colour of 1983*. |
| Bloom on everything | RetroArch presets, DMS elevation glow when abused | Reserve bloom for the one element whose state just changed, for the decay duration only, then zero. |
| Neon borders | Hyprland `borderangle` rices | If edges glow, they glow *down* (fading), never cycling hues. |
| "Burn-in" ghosting of stale UI | cool-retro-term burn-in | Acceptable only as sub-200 ms afterimages on surfaces that just left; never persistent. |

## 5. Motion norms table (so Phosphor can sit deliberately outside them)

| System | Enter | Exit | Move / resize | Curve family | Source |
|---|---|---|---|---|---|
| Material 3 (classic tokens) | 50–400 ms (short1 50 … medium2 300 … long) | shorter, accelerate | 300–500 ms emphasized | `emphasized-decelerate cubic-bezier(0.05,0.7,0.1,1)`, `standard (0.2,0,0,1)` | [m3 tokens](https://m3.material.io/styles/motion/easing-and-duration/tokens-specs) (values confirmed via secondary sources; page body did not fetch) |
| Material 3 Expressive | spring, spatial default/fast/slow with overshoot; effects springs no overshoot | same | spatial spring | stiffness/damping tokens | [M3 Expressive summary](https://zoewave.medium.com/compose-material-3-expressive-89f4147df5b8) |
| end-4 ii (M3E in QML) | 400 ms `elementMoveEnter` | 200 ms | 300 ms resize; 350/500/650 spatial | overshooting beziers, e.g. `[0.42,1.67,0.21,0.90]` | [Appearance.qml](https://raw.githubusercontent.com/end-4/dots-hyprland/main/dots/.config/quickshell/ii/modules/common/Appearance.qml) |
| niri | window-open 150 ms ease-out-expo | close 150 ms ease-out-quad | springs, damping 1.0, stiffness 800 (workspace 1000) | critically damped springs | [wiki](https://github.com/niri-wm/niri/wiki/Configuration:-Animations) |
| Hyprland (typical) | windows 700 ms (`7` ds) custom bezier, `popin 80%` | same, popin | workspaces 600 ms | user beziers, often overshoot | [wiki](https://wiki.hypr.land/Configuring/Advanced-and-Cool/Animations/) |
| GNOME Shell | ~250–300 ms ease-out-quad; 49 adds scale for popovers | same | ~250 ms | Clutter EASE_OUT_QUAD | [#4856](https://gitlab.gnome.org/GNOME/gnome-shell/-/work_items/4856) |
| Windows 11 / WinUI | 250 normal / 167 fast / 83 faster | accelerate `(1,0,1,1)` | — | decelerate `(0,0,0,1)` | [MS Learn](https://learn.microsoft.com/en-us/windows/apps/design/motion/timing-and-easing) |
| macOS Liquid Glass | spring-like morphs, unspecified; specular reacts to motion continuously | same | morph | (Apple does not publish values) | [Apple](https://www.apple.com/newsroom/2025/06/apple-introduces-a-delightful-and-elegant-new-software-design/) |
| KDE Plasma / Kirigami | short 100 / long 200–250 ms | same | 250 ms | OutCubic | (Kirigami.Units, not re-verified here) |
| DMS / Noctalia / Caelestia | springs (DMS 1.5+), unspecified ms | — | — | spring or M3 beziers | [DMS 1.5](https://danklinux.com/blog/v1-5-release) |

Where the field clusters: **150–300 ms ease-out for enter, faster accelerate for exit, symmetric in/out, and (2025–26) critically damped or lightly overshooting springs for anything spatial.** Untaken territory is *asymmetric temporal envelopes*: near-instant rise (≤ 60 ms) with a long, shaped decay (600–1200 ms) that is not a spring settle but an exponential/phosphor fall-off, and exits that are slower than entries. Nobody in the survey makes the exit the expressive half.

## 6. Phosphor / CRT-afterglow: three sentences on what to avoid

> **Superseded by A1 §7 item 2.** The CRT metaphor was considered as the naming
> layer for the motion system and dropped: the packs never use it as a visual
> device, and it drags the design toward retro pastiche. The asymmetric
> envelopes below are kept on their own merits (05 R6); the lore is not.
> This section stands as a record of the survey, not as direction to follow.

The metaphor must be carried entirely by *time* (rise-fast, decay-slow luminance envelopes on state changes) and never by *texture*, because scanlines, curvature, RGB fringing, noise and monochrome green/amber are exactly the five signals cool-retro-term, Cathode and the ctr-glitch-overlay use, and any one of them reclassifies the shell as a retro-terminal theme. Colour has to remain full-gamut and wallpaper-neutral so the afterglow reads as a material property of the interface rather than as a 1983 palette, which means the "glow" is a brief luminance lift of the element's own colour, not a fixed phosphor hue. Finally, persistence must be strictly bounded and semantic (a surface that just left leaves a sub-second trace; nothing burns in), because the CRT projects treat decay as decoration whereas Phosphor's claim to it is that decay tells the user what just changed and in which placement mode.
