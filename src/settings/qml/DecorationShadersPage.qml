// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Decoration → Shaders — installed surface-shader-pack browser.
 *
 * Thin wrapper around the pack-agnostic `ShaderBrowserPage` (the same
 * component behind Animations → Shaders and Snapping → Shaders). This
 * file only provides the bridge (the DecorationPageController surface)
 * and decoration-domain copy.
 *
 * Per-surface pack assignment lives in each surface card's chain editor;
 * this page exists so users can survey what's installed, see parameter
 * metadata, and drop in their own packs.
 */
ShaderBrowserPage {
    bridge: settingsController.decorationPage
    settingsCategory: "DecorationShadersFilterBar"
    infoBannerText: i18n("Browse installed decoration packs. Stack packs onto a surface's chain from the Windows, OSDs, and Popups pages.")
    // "Places", not "surfaces": the usage list can include the global default
    // chain (set over D-Bus), which is not a surface.
    usageHeaderTextFn: function (count) {
        return i18ncp("@info shader usage section header (decoration)", "Used in %n place", "Used in %n places", count);
    }
    usageChipTextFn: function (count) {
        return i18ncp("@info shader usage chip (decoration)", "%n place", "%n places", count);
    }
}
