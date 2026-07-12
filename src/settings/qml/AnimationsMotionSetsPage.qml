// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Animations → Library → Motion Sets.
 *
 * A motion set is a snapshot of every per-event override active at a given
 * moment, persisted as one JSON file under
 * `~/.local/share/plasmazones/motionsets/<slug>.json`. Applying a set merges
 * its overrides into the user's profiles dir; paths it does not cover are
 * preserved.
 *
 * Saving captures only path-named override files. User presets in the same
 * directory are intentionally excluded so a set stays portable and
 * self-contained.
 *
 * Everything below is domain copy over the shared ShaderSetsPage.
 */
ShaderSetsPage {
    bridge: settingsController.animationsPage.setsBridge
    saveAnchor: "saveMotionSet"
    importAnchor: "importMotionSets"
    savedAnchor: "savedMotionSets"

    infoBannerText: i18n("Motion sets bundle your per-event overrides into one shareable JSON file. Applying a set merges into your current overrides. Paths it doesn't cover are left unchanged.")
    saveDescription: i18n("Capture every per-event override file as a named motion set.")
    importDescription: i18n("Motion sets are single JSON files under your data directory. Drop a set file here to import it, or use the buttons below.")
    emptyStateText: i18n("No motion sets saved yet.")
    nameFieldAccessibleName: i18n("Motion set name")
    descriptionFieldAccessibleName: i18n("Motion set description")

    // Coverage chips are keyed on the root segment of an event path
    // ("window.appearance.open" → "window").
    coverageLabel: function (token) {
        switch (token) {
        case "global":
            return i18nc("@label motion event group", "Global");
        case "window":
            return i18nc("@label motion event group", "Windows");
        case "desktop":
            return i18nc("@label motion event group", "Desktop");
        case "editor":
            return i18nc("@label motion event group", "Editor");
        case "osd":
            return i18nc("@label motion event group", "OSDs");
        case "popup":
            return i18nc("@label motion event group", "Popups");
        case "panel":
            return i18nc("@label motion event group", "Panels");
        default:
            return token;
        }
    }
    // Title-case noun, matching the Rules list's badge convention.
    coverageCountLabel: function (count) {
        return i18np("%n Override", "%n Overrides", count);
    }
    applySubtitleFor: function (name) {
        return i18n("\"%1\" will overwrite every per-event override it covers.", name);
    }
}
