// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Animations → Library → Motion Sets.
 *
 * A motion set is a snapshot of every per-event override active at a given
 * moment, persisted as one JSON file under
 * `~/.local/share/plasmazones/motionsets/<slug>.json`. Applying a set merges:
 * events it covers are replaced, events it does not cover are preserved.
 *
 * An override means BOTH halves of what the event card holds — the timing
 * (curve, duration) from the motion profile tree, and the animation pack
 * assigned to that event from the shader profile tree. Both are config keys.
 * Carrying only the timing would
 * capture half of what the user sets in one place, and would leave this page
 * doing strictly less than its Decoration Sets counterpart, whose single tree
 * holds pack ids and parameters together.
 *
 * Saving captures the two trees' entries for the events it covers, plus the
 * pack each remaining event resolves to, so a set describes a whole look
 * rather than only the parts the user happened to change. The saved-curve
 * PRESET library is a separate thing and is never captured: a preset is a
 * named entry in a library, not a property of any event.
 *
 * Everything below is domain copy over the shared ShaderSetsPage.
 */
ShaderSetsPage {
    bridge: settingsController.animationsPage.setsBridge
    saveAnchor: "saveMotionSet"
    importAnchor: "importMotionSets"
    savedAnchor: "savedMotionSets"

    infoBannerText: i18n("Motion sets bundle your per-event animation packs and timing into one shareable JSON file. Applying a set merges into your current overrides. Events it doesn't cover are left unchanged.")
    saveDescription: i18n("Capture every per-event animation pack and its timing as a named motion set.")
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
        case "scrolling":
            return i18nc("@label motion event group", "Scrolling strip");
        case "shell":
            return i18nc("@label motion event group", "Plasma shell");
        case "widget":
            return i18nc("@label motion event group", "Widgets");
        case "cursor":
            return i18nc("@label motion event group", "Cursor");
        default:
            return token;
        }
    }
    // Title-case noun, matching the Rules list's badge convention.
    coverageCountLabel: function (count) {
        return i18np("%n Override", "%n Overrides", count);
    }
    applySubtitleFor: function (name) {
        return i18n("“%1” will overwrite every per-event override it covers.", name);
    }
}
