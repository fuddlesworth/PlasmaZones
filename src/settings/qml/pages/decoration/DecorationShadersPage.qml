// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Decoration → Shaders — installed decoration-pack browser.
 *
 * Thin wrapper around the pack-agnostic `ShaderBrowserPage` (the same
 * component behind Animations → Shaders and Snapping → Shaders). This
 * file only provides the bridge (the DecorationPageController surface)
 * and decoration-domain copy.
 *
 * Both decoration pack families are listed: surface packs and pointer packs.
 * The Type axis separates them.
 *
 * Per-surface pack assignment lives in each surface card's chain editor;
 * this page exists so users can survey what's installed, see parameter
 * metadata, and drop in their own packs.
 */
ShaderBrowserPage {
    bridge: settingsController.decorationPage
    settingsCategory: "DecorationShadersFilterBar"
    infoBannerText: i18n("Browse installed decoration packs. Stack packs onto a surface's chain from the Windows, OSDs, Popups, Shell, and Pointer pages.")
    // Two pack families share this browser. Every row the decoration bridge
    // returns declares one of these as its type, so the Type filter, group-by
    // and card badge appear here and stay hidden on the single-family
    // browsers. The axis hides itself when only one type is installed.
    typeCatalog: [
        {
            "key": "surface",
            "label": i18nc("@item decoration pack family (windows, OSDs, popups, shell)", "Surface"),
            "order": 1
        },
        {
            "key": "pointer",
            "label": i18nc("@item decoration pack family (the mouse pointer)", "Pointer"),
            "order": 2
        }
    ]
    // "Places", not "surfaces": the usage list can include the global default
    // chain (set over D-Bus), which is not a surface.
    usageHeaderTextFn: function (count) {
        return i18ncp("@info shader usage section header (decoration)", "Used in %n place", "Used in %n places", count);
    }
    usageChipTextFn: function (count) {
        return i18ncp("@info shader usage chip (decoration)", "%n place", "%n places", count);
    }
}
