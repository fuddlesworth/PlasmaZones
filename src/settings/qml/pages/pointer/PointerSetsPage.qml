// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Appearance → Pointer → Library → Pointer Sets.
 *
 * A pointer set is a snapshot of the whole pointer chain (every pack layer
 * with its parameters) persisted as one JSON file under
 * `~/.local/share/plasmazones/pointersets/<slug>.json`. There is only one
 * chain, so applying a set replaces it outright rather than merging. The
 * master switch is not part of a set.
 *
 * Everything below is domain copy over the shared ShaderSetsPage.
 */
ShaderSetsPage {
    bridge: settingsController.pointerPage.setsBridge
    saveAnchor: "savePointerSet"
    importAnchor: "importPointerSets"
    savedAnchor: "savedPointerSets"

    infoBannerText: i18n("Pointer sets bundle your whole pack chain into one shareable JSON file. Applying a set replaces the chain you have now. Whether pointer effects are switched on at all is a separate setting a set never touches.")
    saveDescription: i18n("Capture the current pointer chain as a named set.")
    importDescription: i18n("Pointer sets are single JSON files under your data directory. Drop a set file here to import it, or use the buttons below.")
    emptyStateText: i18n("No pointer sets saved yet.")
    nameFieldAccessibleName: i18n("Pointer set name")
    descriptionFieldAccessibleName: i18n("Pointer set description")

    // A pointer set always carries exactly one entry, at the synthetic
    // "pointer" path, so this map has a single real case. The default arm
    // renders a raw untranslated token, which is what a future path would show
    // until a case is added for it.
    coverageLabel: function (token) {
        switch (token) {
        case "pointer":
            return i18nc("@label pointer set coverage", "Pointer");
        default:
            return token;
        }
    }
    // Title-case noun, matching the Rules list's badge convention.
    coverageCountLabel: function (count) {
        return i18np("%n Chain", "%n Chains", count);
    }
    applySubtitleFor: function (name) {
        return i18n("“%1” will replace your whole pointer chain. The change is not saved yet, so Discard still undoes it.", name);
    }
}
