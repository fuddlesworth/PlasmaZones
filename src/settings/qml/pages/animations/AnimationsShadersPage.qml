// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Animations → Shaders — installed animation-shader-pack browser.
 *
 * Thin wrapper around the pack-agnostic `ShaderBrowserPage`. The actual
 * UI (drop zone, filter bar, grouped card grid, detail dialog) lives in
 * the shared component; this file only provides the bridge (the
 * AnimationsPageController surface) and animation-domain copy.
 *
 * Per-event shader assignment lives in each event card
 * (AnimationEventCard's shader picker section); this page exists so
 * users can survey what's installed, see parameter metadata, and jump
 * to the user shader directory to drop in their own packs.
 */
ShaderBrowserPage {
    bridge: settingsController.animationsPage
    settingsCategory: "AnimationsShadersFilterBar"
    infoBannerText: i18n("Browse installed animation shaders. Assign shaders to specific events using the shader picker on any per-event sub-page (Window, Zone, OSD, etc.).")
    // Closures (not pre-evaluated strings) so `i18ncp` runs with the
    // LIVE usage count — required for locales with more than two plural
    // forms and so `%n` displays the actual count rather than a baked-in
    // 1 / 2 sentinel.
    usageHeaderTextFn: function (count) {
        return i18ncp("@info shader usage section header (animations)", "Used in %n event", "Used in %n events", count);
    }
    usageChipTextFn: function (count) {
        return i18ncp("@info shader usage chip (animations)", "%n event", "%n events", count);
    }
}
