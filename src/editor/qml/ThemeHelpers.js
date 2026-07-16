// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

.pragma library

/**
 * @brief Shared theme utility functions for editor QML components
 */

// Durations don't live here: this is a .pragma library, so it cannot reach
// Kirigami.Units, and a bare number would stop following the user's
// animation-speed preference. The call sites read Kirigami.Units directly.
// (The former animEasing constant is gone too — it had no consumers, and each
// animation's easing shape comes from its profile.)

// Standard corner radius multiplier (smallSpacing * this)
var radiusMultiplier = 1.5;

// Background alpha levels
var panelAlpha = 0.95;   // PropertyPanel, NotificationBanner — more opaque, content-heavy
var toolbarAlpha = 0.90; // TopBar, ControlBar — lighter, chrome-level

// Focus ring border width in device-independent pixels. The renderer scales it
// for the screen's device pixel ratio, so call sites use it as-is.
var focusBorderWidth = 2;

// Border width for a surface whose outline carries a status accent colour, such
// as a notification banner. Same units as focusBorderWidth, different meaning:
// this one is always on, and it is not about keyboard focus.
var accentBorderWidth = 2;

// Alpha levels for the theme-derived zone colour defaults. A zone with no
// custom colours shows the theme colour at these alphas, and the editor seeds
// new zones with the same values, so every surface needing one of these
// defaults reads it from here. The base colour comes from the call site: this
// is a .pragma library and cannot reach Kirigami.Theme itself.
var zoneHighlightAlpha = 0.5;
var zoneInactiveAlpha = 0.25;
var zoneBorderAlpha = 0.8;

function withAlpha(baseColor, alpha) {
    return Qt.rgba(baseColor.r, baseColor.g, baseColor.b, alpha);
}
