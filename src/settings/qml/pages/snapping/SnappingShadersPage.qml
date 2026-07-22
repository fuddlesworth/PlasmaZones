// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Snapping → Shaders — installed overlay shader-pack browser.
 *
 * Thin wrapper around the pack-agnostic `ShaderBrowserPage`. The actual
 * UI (drop zone, filter bar, grouped card grid, detail dialog) lives in
 * the shared component; this file only provides the bridge (the
 * SnappingShadersPageController surface) and snapping-domain copy.
 *
 * Per-layout shader assignment lives in the editor (each layout's zone
 * shader picker); this page exists so users can survey what's installed,
 * see parameter metadata, and jump to the user shader directory to drop
 * in their own packs.
 */
ShaderBrowserPage {
    bridge: settingsController.snappingShadersPage
    settingsCategory: "SnappingShadersFilterBar"
    infoBannerText: i18n("Browse installed snapping overlay shaders. Assign a shader to a layout from the layout editor's appearance section.")
    // Closures (not pre-evaluated strings) so `i18ncp` runs with the
    // LIVE usage count — required for locales with more than two plural
    // forms and so `%n` displays the actual count rather than a baked-in
    // 1 / 2 sentinel.
    usageHeaderTextFn: function (count) {
        return i18ncp("@info shader usage section header (snapping overlay)", "Used by %n layout", "Used by %n layouts", count);
    }
    usageChipTextFn: function (count) {
        return i18ncp("@info shader usage chip (snapping overlay)", "%n layout", "%n layouts", count);
    }
}
