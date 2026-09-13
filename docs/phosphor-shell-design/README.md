<!-- SPDX-FileCopyrightText: 2026 fuddlesworth -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Phosphor Shell: Design

The shell redesign centers on visual identity, good UX and riceability.
The current reference is [`mockups-v3/`](mockups-v3/), including Navigator,
Stage, the quick-settings shelf, calendar, visualizers and scrolling with
four and ten windows.

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
fixtures for design review; the native notification center is unchanged.
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
