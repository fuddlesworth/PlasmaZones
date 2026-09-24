// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Appearance → Overlays → Library → Sets.
 *
 * An overlay set is a snapshot of the zone-overlay shader assignments — the
 * global default plus every per-layout override — persisted as one JSON file
 * under `~/.local/share/plasmazones/overlaysets/<slug>.json`. Applying a set
 * merges: what it covers is replaced, what it does not cover keeps its current
 * assignment.
 *
 * Unlike a decoration or motion set, a set's per-layout entries name layouts by
 * id, and ids are per-computer. A set from elsewhere is still applied — its
 * global default and any layout this computer does have — and the rest is
 * skipped with a count, rather than the whole set being refused.
 *
 * Everything below is domain copy over the shared ShaderSetsPage.
 *
 * The strings here carry no i18nc context, and adding one now would be a
 * REGRESSION rather than an improvement. A Qt Linguist catalogue is keyed on
 * (source, comment), so giving a shipped string a context retires the existing
 * entry and opens a fresh untranslated one. These are already extracted and
 * translated in seven languages. Give a NEW string its context when you write
 * it; do not retrofit one onto these.
 */
ShaderSetsPage {
    bridge: settingsController.overlaysPage.setsBridge
    saveAnchor: "saveOverlaySet"
    importAnchor: "importOverlaySets"
    savedAnchor: "savedOverlaySets"

    infoBannerText: i18n("A set bundles your zone overlay shader assignments into one shareable JSON file. Applying a set merges into your current assignments. Layouts it doesn't cover are left unchanged.")
    saveDescription: i18n("Capture the global default and every per-layout override as a named set.")

    // Coverage tokens here are the reserved global-default token or a layout
    // id, neither of which is readable on its own, so the controller resolves
    // them: it holds the layout registry and the QML does not.
    coverageLabel: function (token) {
        return settingsController.overlaysPage.setCoverageLabel(token);
    }
    // "Assignment" rather than "Layout": one of the entries may be the global
    // default, which is not a layout.
    coverageCountLabel: function (count) {
        return i18np("%n Assignment", "%n Assignments", count);
    }
    applySubtitleFor: function (name) {
        return i18n("“%1” will replace the overlay shader on everything it covers. The change is not saved yet, so Discard still undoes it.", name);
    }
}
