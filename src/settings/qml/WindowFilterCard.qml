// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Reusable "Window filtering" card — the exclude-transient toggle plus the two
// minimum-size spin boxes shared by the Snapping (General), Animations, and
// Decorations pages. Each host binds the values to its own settings group and
// wires the change signals, so the values stay independent per feature while
// the UI is defined once. The animations host also supplies an extra
// notifications/OSD row through `insertAfterTransient`.
//
// Host-driven: this component holds no settings state of its own. Values come in
// as properties; edits go out as signals. Descriptions and accessible names are
// host-provided so each feature keeps its own wording (and its translations stay
// in the host page).
SettingsCard {
    id: card

    headerText: i18n("Window filtering")
    searchAnchor: "windowFiltering"
    collapsible: true

    // ── Exclude-transient toggle ──
    property bool excludeTransient: false
    property string transientDescription: ""
    property string transientAccessibleName: ""
    signal excludeTransientToggled(bool value)

    // ── Optional extra row, inserted between the transient toggle and the
    // minimum-width row (the Animations page uses it for notifications/OSD). ──
    property Component insertAfterTransient: null

    // ── Minimum width / height ──
    property int minWidth: 0
    property int minHeight: 0
    property int minWidthFrom: 0
    property int minWidthTo: 2000
    property int minHeightFrom: 0
    property int minHeightTo: 2000
    // Shown when the value is > 0; the disabled variant is shown at 0 ("Off").
    property string minWidthDescription: ""
    property string minWidthDisabledDescription: ""
    property string minHeightDescription: ""
    property string minHeightDisabledDescription: ""
    property string minWidthAccessibleName: ""
    property string minHeightAccessibleName: ""
    signal minWidthModified(int value)
    signal minHeightModified(int value)

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsRow {
            title: i18n("Exclude transient windows")
            description: card.transientDescription
            searchAnchor: "excludeTransient"

            SettingsSwitch {
                checked: card.excludeTransient
                accessibleName: card.transientAccessibleName
                onToggled: function (newValue) {
                    card.excludeTransientToggled(newValue);
                }
            }
        }

        // Extra host-supplied content (e.g. the animations notifications/OSD
        // toggle), inserted between the transient toggle and the size rows. The
        // component supplies its own leading separator so it composes with the
        // transient row above without a dangling divider.
        Loader {
            Layout.fillWidth: true
            active: card.insertAfterTransient !== null
            visible: active
            sourceComponent: card.insertAfterTransient
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Minimum window width")
            searchAnchor: "minimumWindowWidth"
            description: card.minWidth === 0 ? card.minWidthDisabledDescription : card.minWidthDescription

            SettingsSpinBox {
                // Schema-driven bounds — literal bounds would silently truncate
                // a saved value outside the literal range when the SpinBox
                // clamped the bound `value` on render.
                from: card.minWidthFrom
                to: card.minWidthTo
                stepSize: 10
                value: card.minWidth
                // textFromValue already emits the localised "%1 px" suffix;
                // suppress SettingsSpinBox's default "px" Label so the value
                // reads "100 px" rather than "100 px px" (and "Off" not "Off px").
                unitText: ""
                Accessible.name: card.minWidthAccessibleName
                onValueModified: value => {
                    card.minWidthModified(value);
                }
                textFromValue: function (value) {
                    return value === 0 ? i18n("Off") : i18nc("pixel-unit suffix in spin box", "%1 px", value);
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Minimum window height")
            searchAnchor: "minimumWindowHeight"
            description: card.minHeight === 0 ? card.minHeightDisabledDescription : card.minHeightDescription

            SettingsSpinBox {
                from: card.minHeightFrom
                to: card.minHeightTo
                stepSize: 10
                value: card.minHeight
                unitText: ""
                Accessible.name: card.minHeightAccessibleName
                onValueModified: value => {
                    card.minHeightModified(value);
                }
                textFromValue: function (value) {
                    return value === 0 ? i18n("Off") : i18nc("pixel-unit suffix in spin box", "%1 px", value);
                }
            }
        }
    }
}
