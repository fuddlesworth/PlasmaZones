// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Pointer → Pointer Packs — installed pointer-shader-pack browser.
 *
 * Thin wrapper around the pack-agnostic `ShaderBrowserPage` (the same component
 * behind Animations → Shaders, Snapping → Shaders and Decoration → Shaders).
 * This file only provides the bridge (the PointerPageController surface) and
 * pointer-domain copy.
 *
 * Adding a pack to the cursor happens on the Pointer page's chain editor; this
 * page exists so users can survey what is installed, see parameter metadata,
 * watch a pack run in the live preview, and drop in their own packs.
 */
ShaderBrowserPage {
    bridge: settingsController.pointerPage
    settingsCategory: "PointerShadersFilterBar"
    infoBannerText: i18n("Browse installed pointer packs. Stack them onto the cursor from the Pointer page.")
    usageHeaderTextFn: function (count) {
        return i18ncp("@info shader usage section header (pointer)", "Used in %n layer", "Used in %n layers", count);
    }
    usageChipTextFn: function (count) {
        return i18ncp("@info shader usage chip (pointer)", "%n layer", "%n layers", count);
    }
}
