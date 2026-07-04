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
    infoBannerText: i18n("Browse installed decoration packs. Stack packs onto a surface's chain from the Windows, OSDs, and Popups pages.")
    usageHeaderTextFn: function (count) {
        return i18ncp("@info shader usage section header (decoration)", "Used on %n surface", "Used on %n surfaces", count);
    }
    usageChipTextFn: function (count) {
        return i18ncp("@info shader usage chip (decoration)", "%n surface", "%n surfaces", count);
    }
}
