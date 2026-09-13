<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# 05: Phosphor Shell Visual Identity

The approved reference is [`mockups-v3/`](mockups-v3/). The redesign balances
visual identity, usable controls and user customization. The older studies
in `identity/` and `mockups-v2/` record the preceding thin-stroke design;
their restrictions on fills, rounded surfaces and palettes no longer apply.

## Shared identity

The placement map remains the shell's recognizable feature. It shows the
actual snapping zones, tiling layout or scrolling viewport from the daemon.
A shared cyan, blue, violet and rose field connects the map, material edges,
selection and media visualization. Rounded floating surfaces, inset controls,
clear typography and restrained color washes give that field depth.

`AppearanceStore` persists the choices. `Appearance.qml`, `Tokens.qml` and
`Spectrum.qml` resolve them; surfaces use `ShellSurface` and `ShellButton`.
Semantic errors retain their service theme colors. Do not duplicate a private
dark palette inside a popup: Paper must remain readable throughout the shell.

## Presentations

| Presentation | Overview | Quick settings |
|---|---|---|
| Navigator | A bounded popup anchored to the workspace map. A preview and readable window list support direct activation. | A compact vertical panel with connection rows, levels and media. |
| Stage | Workspace maps on the left, a live desktop preview in the center, a selected-window inspector on the right, and a bounded window strip below. | A wide shelf at the bottom, grouped into connections, sound/display and media. It collapses to the vertical panel on narrow outputs. |

Stage uses the KWin effect's `org.plasmazones.ShellOverview` interface at
`/PlasmaZones/ShellOverview` on `org.kde.KWin`. The effect transforms the real
desktop windows and restores them when the shell closes, disconnects or loses
the output. Per-instance tokens prevent an old closing popup from restoring a
new preview on the same shell connection. The QML surface stays full-screen so its hit areas match its
preview. Without that interface, Stage displays a placement-map preview.
Window selection and activation are distinct: arrows select; Enter opens.

The bar stays compact with four, ten or twenty scrolling windows. Its mini
shows a viewport and capped offscreen markers with counts. Navigator and
Stage use fixed-width cards and scroll the list instead of shrinking cards
or widening the bar. Minimized windows, inactive tabs and offscreen columns
remain in the complete navigation model.

## Materials and customization

| Preset | Defaults |
|---|---|
| Phosphor | Spectrum, dark glass, comfortable density, top bar, 18 px corners. |
| Paper | Wallpaper palette, light surfaces, comfortable density, top bar, 24 px corners. |
| Ember | Warm palette, solid surfaces, compact density, bottom bar, 8 px corners. |

Palette, material, radius (4–30 px), bar inset (6–30 px), density, bar edge,
glow, animations and custom surface packs can be changed independently.
Presets preserve the user's presentation, widget arrangement, fonts, media
and motion preferences. Surface packs are opt-in; ordinary shared surfaces
supply the default appearance.

The appearance panel separates Style and Widgets. Widgets can move between
left, center and right, reorder, hide and return. The three bar regions bound
their width and expose overflow controls. Long focused-app and media labels
hide below 1100 px; their panel functions remain available through commands.
Preset JSON retains groups and extension widget IDs, with validation against
duplicates and malformed values.

Interface and number font families are independently configurable. Empty
values use `FontFaces` (Manrope and JetBrains Mono where available, named
fallbacks otherwise). No font binaries are bundled by this redesign.

Settings live in `~/.config/phosphor-shell/appearance.json`, as a versioned
JSON object containing `settings`. Import/export is atomic and validates the
whole document before replacing the current appearance. Colors, fonts and
widget moves update live. Bar geometry changes rebuild the shell surfaces.
The source watcher ignores neighboring settings writes and still handles
atomic replacements of `shell.qml`.

## Surface behavior

- Wi-Fi and Bluetooth rows have one continuous background, with an inset
  divider for their details button. Labels and trailing actions share the
  same geometry in every material.
- Media cards use real MPRIS metadata, artwork and player capabilities.
  Ribbon, Bars and Halo consume a shared CAVA spectrum. Off disables it.
  No decorative samples stand in for stopped or unavailable audio.
- Spectrum work runs only for a visible playing consumer. Pausing, closing
  the surface, disabling media/visualization or reducing motion releases it.
- The calendar opens from the clock, shows local time and date, and supports
  month browsing, Today, arrows, Home and Page Up/Down. It does not invent
  appointments or weather without a service.
- Launcher rows, notifications, OSDs, power, picker, lock and authentication
  surfaces share the selected text/material palette and keep their existing
  service and permission boundaries.
- Popouts respect the bar inset and available output size. Long settings
  panels scroll and bring a keyboard-focused control into view.

## Verification

Use `scripts/nested-shell/` with its private IPC socket. Headless screenshots
show shell surfaces; they bypass the compositor's window transforms. Verify
Stage's native preview using the visible nested compositor window as well.
The implementation record distinguishes these captures from unit tests and
from real hardware behavior that the nested session cannot exercise.
