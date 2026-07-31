// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Editor for one scrolling preset list (column widths or window
 * heights): a compact band of removable percent chips plus an inline add
 * control, sized to sit as a SettingsRow's right-aligned control. Chips
 * read left to right in cycle order (new entries append); the chip recipe
 * follows MetadataChip, and the remove affordance follows the filter-chip
 * convention.
 *
 * The stored value stays the canonical comma-joined fraction string. Every
 * edit joins the working array and writes it through `commit`, then the
 * `presets` binding delivers back whatever the schema's canonicalizer kept
 * (dropped duplicates, the 16-entry cap, the nothing-survives default), so
 * the band always shows the effective presets.
 */
Flow {
    id: editor

    /// Canonical comma-joined fraction string (bind an appSettings property).
    required property string presets
    /// Called with the new comma-joined string on every edit.
    required property var commit
    /// Accessible label stem, e.g. "column width preset".
    required property string entryName

    readonly property var _values: presets.length > 0 ? presets.split(",") : []

    // Wrap inside a bounded band: SettingsRow's control container is a plain
    // Row (no width imposed on children), so an unconstrained Flow would lay
    // every chip on one line and overflow the row's control share. Capping
    // the width folds long lists onto extra lines instead.
    width: Math.min(implicitWidth, Kirigami.Units.gridUnit * 22)
    spacing: Kirigami.Units.smallSpacing

    function _commitList(list) {
        commit(list.join(","));
    }

    Repeater {
        model: editor._values

        delegate: Rectangle {
            id: chip

            required property string modelData
            required property int index

            readonly property int percent: Math.round(parseFloat(chip.modelData) * 100)

            implicitWidth: chipContent.implicitWidth + Kirigami.Units.largeSpacing
            implicitHeight: chipContent.implicitHeight + Kirigami.Units.smallSpacing
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.alternateBackgroundColor
            border.width: 1
            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)

            RowLayout {
                id: chipContent

                anchors.centerIn: parent
                spacing: Kirigami.Units.smallSpacing / 2

                Label {
                    text: i18n("%1%", chip.percent)
                    font: Kirigami.Theme.smallFont
                }

                ToolButton {
                    icon.name: "window-close-symbolic"
                    icon.width: Kirigami.Units.iconSizes.small
                    icon.height: Kirigami.Units.iconSizes.small
                    implicitWidth: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                    implicitHeight: implicitWidth
                    Accessible.name: i18n("Remove %1% %2", chip.percent, editor.entryName)
                    onClicked: {
                        var next = editor._values.slice();
                        next.splice(chip.index, 1);
                        editor._commitList(next);
                    }
                }
            }
        }
    }

    RowLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsSpinBox {
            id: addSpin

            accessibleName: i18n("New %1 percent", editor.entryName)
            unitText: "%"
            from: 1
            to: 100
            value: 50
            stepSize: 5
        }

        ToolButton {
            icon.name: "list-add"
            Accessible.name: i18n("Add %1", editor.entryName)
            onClicked: {
                var next = editor._values.slice();
                // Three decimals keeps 1/3-style entries distinct without
                // fighting the canonicalizer's number formatting.
                next.push((addSpin.value / 100).toFixed(3));
                editor._commitList(next);
            }
        }
    }
}
