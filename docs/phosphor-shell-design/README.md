<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Phosphor Shell: Design

The shell redesign centers on visual identity, good UX and riceability.
The current reference is [`mockups-v3/`](mockups-v3/), including Navigator,
Stage, compact quick settings, calendar, visualizers and scrolling with
four and ten windows.

The [shortcut-reference study](mockups-v3/index.html#navigator/shortcuts) is
implemented by the native shell. Tiling, Scrolling and Snapping each have a visual
guide, with searchable actions, expandable shortcut families, alternative
bindings and an assigned-only filter. General and Shell sections separate
registered actions from compositor-configured commands. The preview includes
project defaults, custom bindings, unassigned and unavailable states, layout
capability filtering, and large text. These are browser fixtures, not host
bindings. Captures: [Phosphor](mockups-v3/shortcuts.png),
[Scrolling](mockups-v3/shortcuts-scrolling.png),
[Paper](mockups-v3/shortcuts-paper.png), and [Ember](mockups-v3/shortcuts-ember.png).

The native reference consumes effective `ShortcutCatalog` bindings and every
alternative, with live layout capability filtering. The registered
`toggle_cheatsheet` action opens this surface on the cursor's display, as does
the shell's `cheatsheet.toggle` IPC command. The daemon overlay remains available
when the shell is absent. Shell actions whose bindings are managed externally
are marked “Set in compositor” without inventing defaults.

For isolated native visual checks, start a nested compositor and run
`scripts/nested-shell/shortcuts-preview.sh run` with the same `PZ_NESTED_SESSION`.
Its `preview` IPC target controls mode, custom/unassigned bindings, service
states, palette, viewport size and text scaling. Stop the foreground preview
with Ctrl+C before restarting it. See the implementation record for validation.

The new **quick-settings detail studies** are ready for design review:
[Wi-Fi](mockups-v3/index.html#navigator/controls/wifi),
[Bluetooth](mockups-v3/index.html#navigator/controls/bluetooth), and
[Audio](mockups-v3/index.html#navigator/controls/audio).
Both presentations use a compact popup next to the status area. A detail view
replaces its contents, with a fixed header and footer and a bounded scrolling
body. Back restores the main panel and its trigger focus; Escape first cancels
an inline task, then goes back, then closes the popup.

Wi-Fi includes network selection, password visibility, automatic connection,
retry, captive sign-in, and connection details. Bluetooth separates paired and
nearby devices, with code confirmation, PIN entry, disconnect, and confirmed
Forget. Audio separates outputs, microphones, and per-app volumes and routing;
mute preserves the chosen volume. The external **Example** selector exposes
23 states, including disabled radios, empty discovery, unavailable adapters,
pairing failures, and a disconnected audio device. The Wi-Fi demo password is
`phosphor`; these fixtures do not access hardware, transmit or save passwords,
or play or record audio. The new detail designs have not been ported to QML yet.

Captures: [Wi-Fi](mockups-v3/quick-wifi.png),
[password entry](mockups-v3/quick-wifi-password.png),
[Bluetooth](mockups-v3/quick-bluetooth.png),
[pairing](mockups-v3/quick-bluetooth-pairing.png),
[Audio output](mockups-v3/quick-audio.png),
[microphone](mockups-v3/quick-audio-input.png),
[app volumes](mockups-v3/quick-audio-apps.png),
[Paper](mockups-v3/quick-details-paper.png), and
[Ember](mockups-v3/quick-details-ember.png).

The [wallpaper and Appearance study](mockups-v3/index.html#navigator/appearance)
brings wallpaper browsing, style controls, bar composition and presets into one
window. The gallery includes eight wallpapers, searchable collections, image
import, two simulated displays, placement controls and wallpaper-derived colors.
A desktop preview hides windows so the background can be judged at full size.
Changes stay provisional until Apply. Revert restores the applied look, and
closing with changes offers Apply, Discard or Keep editing. Following a wallpaper
retints the shell backgrounds, cards, text, borders, shadows and accents together.
Light and dark tones retain readable contrast, and media visualization takes the
same palette immediately. See the [Understory color study](mockups-v3/appearance-wallpaper-colors.png).

Style includes palette and focus color, glass/solid/light materials, geometry,
fonts, motion, media and privacy options, with browser approximations of optional
surface effects. The Bar page supports drag, keyboard-accessible region/order
controls, hide/restore, edge and inset. Presets can include wallpapers and bar
layout explicitly. Saving, importing with validation, export, deletion and
retaining the applied configuration through a reload work in the browser.
Imported images stay in the current tab and are excluded from portable presets.
The external **Preview controls** drawer remains available for reviewing other
surfaces. The bar's Appearance button opens the new window.

The native shell now opens the same four-page Appearance workspace. Its display
list comes from the compositor, imported images are copied into a durable local
library, bar controls use the real widget registry, and saved looks can be
imported, inspected, exported and deleted. One appearance transaction owns
wallpapers, placement, colors and layout, including through QML reloads. Apply
writes the complete look atomically; Revert and Discard restore the applied look.
Window frames read an owned preview while it is active and return to persisted
settings if its producer exits. Glass and Motes use the existing shader packs
with the selected palette, and text scaling reaches the shell controls.

The browser study retains simulated displays and effect samples. Its artwork is
drawn in CSS except for the existing bundled picture; the native gallery ships
SVG counterparts. Neither needs downloaded assets or fonts.
Captures: [Wallpaper](mockups-v3/appearance.png),
[Style](mockups-v3/appearance-style.png), [Bar](mockups-v3/appearance-bar.png),
[Presets](mockups-v3/appearance-presets.png),
[Paper](mockups-v3/appearance-paper.png), [Ember](mockups-v3/appearance-ember.png),
[desktop preview](mockups-v3/appearance-preview.png), and
[saving a preset](mockups-v3/appearance-save-preset.png).

The [notification-center study](mockups-v3/index.html#navigator/notifications)
adds a bounded inbox shared by Navigator and Stage. It includes app groups,
an unread filter, urgent items, inline demo replies, Do Not Disturb, and Undo
for individual, group and bulk dismissal. Preview controls provide seven
everyday notifications, a busy inbox of thirty, an empty state and new
arrivals. Arrival variants show an app icon, a full-width picture, a long
message, or a picture and long message together. The popup and history both
expand message text, retain image proportions, and offer demo replies.
Dismissed popups remain in history. Do Not Disturb suppresses the popup;
the preview stays open until dismissed when it is shown.
Organization and message previews are customizable alongside the
shared palettes and density. These notification interactions are browser
fixtures for design review; the approved native migration is recorded in
`04-implementation-plan.md`.
Captures: [Phosphor](mockups-v3/notifications.png),
[Paper](mockups-v3/notifications-paper.png),
[Ember](mockups-v3/notifications-ember.png), and the
[empty state](mockups-v3/notifications-empty.png). Rich content:
[incoming notification](mockups-v3/notification-arrival.png) and
[expanded message](mockups-v3/notification-expanded.png). The picture fixture
reuses the shell's bundled `wallpaper.png` without modification.

The approved [lock-screen study](mockups-v3/index.html#navigator/lockscreen)
is implemented by `Phosphor.Lock`. It shares the shell's colors, materials,
fonts, density and surface packs, with split and centered compositions.
Appearance includes lock-screen layout, opt-in media and notification-count
controls. The production screen uses PAM, compositor keyboard state, MPRIS
and logind; notification content stays hidden. The browser study still uses
`demo` and simulated power actions for design review.

For native visual review, start the nested harness and run
`scripts/nested-shell/lock-preview.sh run` with the same `PZ_NESTED_SESSION`.
It renders the production QML with fixture authentication, media and power
services in its own config directory. `lock-preview.sh call preview.state`
reports the state without exposing the password. The virtual compositor
cannot exercise the real session-lock protocol; service tests cover the
PAM and release handshake.

The shell is Qt6/QML on the reusable `phosphor-*` libraries. This branch
integrates with the PlasmaZones daemon and KWin effect; the nested harness
runs those build-tree components without installing them. No other shell
framework is a dependency.

## Start here

- [`05-visual-identity.md`](05-visual-identity.md): current materials,
  presentations, shared components and customization behavior.
- [`mockups-v3/`](mockups-v3/): approved interactive studies and captures.
- [`04-implementation-plan.md`](04-implementation-plan.md): phase commits,
  validation and the historical implementation record.

## Supporting records

- [`01-feature-inventory.md`](01-feature-inventory.md): service and surface inventory.
- [`02-gap-analysis.md`](02-gap-analysis.md): remaining service work and verification limits.
- [`03-component-map.md`](03-component-map.md): modules and daemon-to-shell data flow.
- [`phosphor-ipc-followups.md`](phosphor-ipc-followups.md): IPC follow-ups.
- [`identity/`](identity/) and [`mockups-v2/`](mockups-v2/): historical thin-stroke studies.

## Conventions

Appearance is shared through `AppearanceStore`, `Appearance`, `Tokens` and
`Spectrum`. `ShellSurface` and `ShellButton` supply common material and
interaction styling. Paper and Ember are supported variants, not separate
component implementations. Configurable fonts and widget layout belong to
the saved appearance rather than hand-edited copies of individual surfaces.
