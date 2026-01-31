// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @brief Color utility functions for ARGB hex string conversion
 *
 * QML's Qt.color() expects RGBA format, but we store colors in ARGB format
 * (QColor::HexArgb). These utilities handle the conversion.
 */

.pragma library

/**
 * @brief Transparent color constant in ARGB hex format
 */
var transparentArgbHex = "#00000000";

/**
 * @brief Parse ARGB hex strings (from QColor::HexArgb format)
 * @param hexString Color string in #AARRGGBB or #ARGB format
 * @return Qt.rgba color object
 *
 * QML's Qt.color() expects RGBA format, but we store ARGB.
 */
function parseArgbHex(hexString) {
    if (!hexString || typeof hexString !== 'string')
        return Qt.transparent;

    // Remove # if present
    var hex = hexString.replace('#', '');

    // ARGB format: AARRGGBB (8 chars) or ARGB (4 chars)
    if (hex.length === 8) {
        // Parse ARGB: AARRGGBB
        var a = parseInt(hex.substring(0, 2), 16) / 255;
        var r = parseInt(hex.substring(2, 4), 16) / 255;
        var g = parseInt(hex.substring(4, 6), 16) / 255;
        var b = parseInt(hex.substring(6, 8), 16) / 255;
        return Qt.rgba(r, g, b, a);
    } else if (hex.length === 4) {
        // Parse ARGB shorthand: ARGB
        var a = parseInt(hex.substring(0, 1), 16) / 15;
        var r = parseInt(hex.substring(1, 2), 16) / 15;
        var g = parseInt(hex.substring(2, 3), 16) / 15;
        var b = parseInt(hex.substring(3, 4), 16) / 15;
        return Qt.rgba(r, g, b, a);
    } else {
        // Try Qt.color() as fallback (might be RGB format)
        return Qt.color(hexString);
    }
}

/**
 * @brief Convert QColor to ARGB hex string (#AARRGGBB)
 * @param colorValue Qt color object
 * @return String in #AARRGGBB format
 *
 * QColor.toString() returns #RRGGBB without alpha, so we construct ARGB manually.
 */
function colorToArgbHex(colorValue) {
    if (!colorValue)
        return transparentArgbHex;

    // Convert color components to 0-255 range
    var a = Math.round(colorValue.a * 255);
    var r = Math.round(colorValue.r * 255);
    var g = Math.round(colorValue.g * 255);
    var b = Math.round(colorValue.b * 255);

    // Format as ARGB hex string: #AARRGGBB
    var aHex = a.toString(16).padStart(2, '0').toUpperCase();
    var rHex = r.toString(16).padStart(2, '0').toUpperCase();
    var gHex = g.toString(16).padStart(2, '0').toUpperCase();
    var bHex = b.toString(16).padStart(2, '0').toUpperCase();

    return "#" + aHex + rHex + gHex + bHex;
}

/**
 * @brief Extract color from zone data with fallback
 * @param zone Zone object (QVariantMap)
 * @param propertyName Color property name (e.g., "highlightColor")
 * @param fallbackColor Fallback color if property not found
 * @return Parsed color
 */
function extractZoneColor(zone, propertyName, fallbackColor) {
    if (!zone)
        return fallbackColor || Qt.transparent;

    var colorValue = zone[propertyName];
    if (!colorValue)
        return fallbackColor || Qt.transparent;

    if (typeof colorValue === 'string')
        return parseArgbHex(colorValue);

    return colorValue;
}
