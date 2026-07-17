// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

.pragma library

/**
 * Returns the screen aspect ratio clamped to [1.0, 3.6] to keep wizard
 * previews usable on extreme aspect ratios (e.g. 32:9 ultrawides or
 * portrait monitors).  Falls back to 16:9 if screen dimensions are
 * unavailable.
 *
 * @param {real} screenWidth  - Screen.width from the calling QML context
 * @param {real} screenHeight - Screen.height from the calling QML context
 * @returns {real}
 */
function clampedScreenAspectRatio(screenWidth, screenHeight) {
    if (screenWidth > 0 && screenHeight > 0)
        return Math.max(1, Math.min(3.6, screenWidth / screenHeight));
    return 16 / 9;
}

/**
 * Wizard color palette built from the proper Kirigami semantic tokens.
 * Centralizes the palette shared across wizard dialogs: surfaces use
 * alternateBackgroundColor, borders use the frameContrast-interpolated
 * border color, hover uses hoverColor — only the accent selection tints
 * are derived from highlightColor.
 *
 * @param {color} altBg          - Kirigami.Theme.alternateBackgroundColor
 * @param {color} borderColor    - Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
 * @param {color} highlightColor - Kirigami.Theme.highlightColor
 * @param {color} hoverColor     - Kirigami.Theme.hoverColor
 * @returns {Object} palette with the frame colors (subtleBg, subtleBorder,
 *          accentBorder) and the WizardTemplateCard state colors (highlightBg,
 *          hoverBg, defaultBg, selectedBorder, hoverBorder, defaultBorder)
 */
function wizardColors(altBg, borderColor, highlightColor, hoverColor) {
    return {
        "subtleBg":        altBg,
        "subtleBorder":    borderColor,
        "accentBorder":    Qt.rgba(highlightColor.r, highlightColor.g, highlightColor.b, 0.3),
        // Card-specific colors (WizardTemplateCard)
        "highlightBg":    Qt.rgba(highlightColor.r, highlightColor.g, highlightColor.b, 0.15),
        "hoverBg":        Qt.tint(altBg, Qt.alpha(hoverColor, 0.1)),
        "defaultBg":      altBg,
        "selectedBorder": Qt.rgba(highlightColor.r, highlightColor.g, highlightColor.b, 0.6),
        "hoverBorder":    hoverColor,
        "defaultBorder":  borderColor
    };
}
