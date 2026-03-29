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
 * Wizard color palette derived from Kirigami theme colors.
 * Centralizes the Qt.rgba() calls shared across wizard dialogs.
 *
 * @param {color} textColor      - Kirigami.Theme.textColor
 * @param {color} highlightColor - Kirigami.Theme.highlightColor
 * @returns {Object} palette with subtleBg, subtleBorder, accentBorder, badgeBg, badgeBorder
 */
function wizardColors(textColor, highlightColor) {
    return {
        "subtleBg":        Qt.rgba(textColor.r, textColor.g, textColor.b, 0.03),
        "subtleBorder":    Qt.rgba(textColor.r, textColor.g, textColor.b, 0.08),
        "accentBorder":    Qt.rgba(highlightColor.r, highlightColor.g, highlightColor.b, 0.3),
        "badgeBg":         Qt.rgba(highlightColor.r, highlightColor.g, highlightColor.b, 0.2),
        "badgeBorder":     Qt.rgba(highlightColor.r, highlightColor.g, highlightColor.b, 0.4),
        // Card-specific colors (WizardTemplateCard)
        "highlightBg":    Qt.rgba(highlightColor.r, highlightColor.g, highlightColor.b, 0.15),
        "hoverBg":        Qt.rgba(textColor.r, textColor.g, textColor.b, 0.06),
        "defaultBg":      Qt.rgba(textColor.r, textColor.g, textColor.b, 0.03),
        "selectedBorder": Qt.rgba(highlightColor.r, highlightColor.g, highlightColor.b, 0.6),
        "hoverBorder":    Qt.rgba(highlightColor.r, highlightColor.g, highlightColor.b, 0.3),
        "defaultBorder":  Qt.rgba(textColor.r, textColor.g, textColor.b, 0.08)
    };
}
