// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Decoration → Library → Decoration Sets.
 *
 * A decoration set is a snapshot of the decoration profile tree (the
 * baseline plus every per-surface override) persisted as one JSON file under
 * `~/.local/share/plasmazones/decorationsets/<slug>.json`. Applying a set
 * merges: surfaces it covers are replaced, surfaces it does not cover keep
 * their current chains.
 *
 * Everything below is domain copy over the shared ShaderSetsPage.
 */
ShaderSetsPage {
    bridge: settingsController.decorationPage.setsBridge
    saveAnchor: "saveDecorationSet"
    importAnchor: "importDecorationSets"
    savedAnchor: "savedDecorationSets"

    infoBannerText: i18n("Decoration sets bundle your per-surface pack chains into one shareable JSON file. Applying a set merges into your current decoration. Surfaces it doesn't cover are left unchanged.")
    saveDescription: i18n("Capture the baseline and every per-surface override as a named decoration set.")
    importDescription: i18n("Decoration sets are single JSON files under your data directory. Drop a set file here to import it, or use the buttons below.")
    emptyStateText: i18n("No decoration sets saved yet.")
    nameFieldAccessibleName: i18n("Decoration set name")
    descriptionFieldAccessibleName: i18n("Decoration set description")

    // Coverage chips are keyed on the root segment of a surface path
    // ("window.tiled" → "window"), matching the three Decoration surface pages.
    coverageLabel: function (token) {
        switch (token) {
        case "window":
            return i18nc("@label decoration surface group", "Windows");
        case "osd":
            return i18nc("@label decoration surface group", "OSDs");
        case "popup":
            return i18nc("@label decoration surface group", "Popups");
        default:
            return token;
        }
    }
    // Title-case noun, matching the Rules list's badge convention.
    coverageCountLabel: function (count) {
        return i18np("%n Surface", "%n Surfaces", count);
    }
    applySubtitleFor: function (name) {
        return i18n("“%1” will replace the decoration on every surface it covers.", name);
    }
}
