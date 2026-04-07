// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

.pragma library

/**
 * @brief Shared theme utility functions for editor QML components
 */

// Standard animation duration and easing for all editor transitions
var animDuration = 200;
var animEasing = Easing.OutCubic;

// Standard border width for subtle separators (1 device pixel)
var borderWidth = 1; // Use Math.round(Kirigami.Units.devicePixelRatio) at call site when needed

// Standard corner radius multiplier (smallSpacing * this)
var radiusMultiplier = 1.5;

function withAlpha(baseColor, alpha) {
    return Qt.rgba(baseColor.r, baseColor.g, baseColor.b, alpha);
}
