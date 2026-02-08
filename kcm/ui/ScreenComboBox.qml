// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable combo box for selecting a screen/monitor
 *
 * Shows screen name, resolution, and primary indicator.
 * Rebuilds model reactively when kcm.screens changes.
 *
 * Properties:
 *   - kcm: required, the KCM backend object
 *   - noneText: label for the "any/all" option (default: "Any Screen")
 *   - showNoneOption: whether to show the none/all option (default: true)
 *   - currentScreenName: the currently selected screen name ("" for none)
 */
ComboBox {
    id: root

    required property var kcm
    property string noneText: i18n("Any Screen")
    property bool showNoneOption: true

    /** The screen name of the current selection ("" if none/all selected) */
    readonly property string currentScreenName: currentValue ?? ""

    textRole: "text"
    valueRole: "value"

    model: root._buildModel()

    // Rebuild model when screens change (hotplug)
    Connections {
        target: root.kcm
        function onScreensChanged() {
            let prev = root.currentScreenName
            root.model = root._buildModel()
            root.selectScreenName(prev)
        }
    }

    /** Select a screen by name, or reset to index 0 if not found */
    function selectScreenName(name) {
        if (!name || name === "") {
            currentIndex = 0
            return
        }
        for (let i = 0; i < model.length; i++) {
            if (model[i].value === name) {
                currentIndex = i
                return
            }
        }
        currentIndex = 0
    }

    /** Reset selection to the first item (none/all or first screen) */
    function reset() {
        currentIndex = 0
    }

    function _buildModel() {
        let items = []
        if (root.showNoneOption) {
            items.push({text: root.noneText, value: ""})
        }
        if (root.kcm && root.kcm.screens) {
            for (let i = 0; i < root.kcm.screens.length; ++i) {
                let s = root.kcm.screens[i]
                let label = s.name || (i18n("Monitor") + " " + (i + 1))
                if (s.resolution) {
                    label += " (" + s.resolution + ")"
                }
                if (s.isPrimary) {
                    label += " — " + i18n("Primary")
                }
                items.push({text: label, value: s.name})
            }
        }
        return items
    }
}
