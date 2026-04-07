// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

.pragma library

/**
 * @brief Shared theme utility functions for editor QML components
 */

// Standard animation duration and easing for all editor transitions
var animDuration = 200;
var animEasing = 3; // Easing.OutCubic (literal — .pragma library has no QML context)

// Standard corner radius multiplier (smallSpacing * this)
var radiusMultiplier = 1.5;

// Background alpha levels
var panelAlpha = 0.95;   // PropertyPanel, NotificationBanner — more opaque, content-heavy
var toolbarAlpha = 0.90; // TopBar, ControlBar — lighter, chrome-level

// Focus ring border width (device-pixel-aware)
var focusBorderWidth = 2; // Use Math.round(devicePixelRatio * focusBorderWidth) at call site

function withAlpha(baseColor, alpha) {
    return Qt.rgba(baseColor.r, baseColor.g, baseColor.b, alpha);
}
