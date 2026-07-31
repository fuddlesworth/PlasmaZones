// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Editor for one scrolling preset list (column widths or window
 * heights), following the Virtual Screens page's preset-card convention: a
 * uniform card grid where each preset draws its share as a highlight band
 * inside a screen-shaped thumbnail well (horizontal share for widths,
 * vertical for heights), with a per-card remove button and an add row
 * beneath the grid. Cards read left to right, top to bottom in cycle
 * order; new entries append.
 *
 * The stored value stays the canonical comma-joined fraction string. Every
 * edit joins the working array and writes it through `commit`, then the
 * `presets` binding delivers back whatever the schema's canonicalizer kept
 * (dropped duplicates, the 16-entry cap, the nothing-survives default), so
 * the grid always shows the effective presets.
 */
ColumnLayout {
    id: editor

    /// Canonical comma-joined fraction string (bind an appSettings property).
    required property string presets
    /// Called with the new comma-joined string on every edit.
    required property var commit
    /// Accessible label stem, e.g. "column width preset".
    required property string entryName
    /// Heights preview as a vertical share of the well; widths (the
    /// default) as a horizontal one.
    property bool vertical: false

    readonly property var _values: presets.length > 0 ? presets.split(",") : []

    spacing: Kirigami.Units.smallSpacing

    function _commitList(list) {
        commit(list.join(","));
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 4
        uniformCellWidths: true
        columnSpacing: Kirigami.Units.smallSpacing
        rowSpacing: Kirigami.Units.smallSpacing

        Repeater {
            model: editor._values

            delegate: Rectangle {
                id: presetCard

                required property string modelData
                required property int index

                readonly property real fraction: parseFloat(presetCard.modelData)
                readonly property int percent: Math.round(presetCard.fraction * 100)
                readonly property real _cardPad: Kirigami.Units.largeSpacing

                Layout.fillWidth: true
                implicitHeight: cardRow.implicitHeight + presetCard._cardPad * 2
                radius: Kirigami.Units.smallSpacing * 1.5
                color: Kirigami.Theme.backgroundColor
                border.width: 1
                border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                Accessible.name: i18n("%1% %2", presetCard.percent, editor.entryName)

                RowLayout {
                    id: cardRow

                    anchors.fill: parent
                    anchors.margins: presetCard._cardPad
                    spacing: Kirigami.Units.largeSpacing

                    // Thumbnail: an inset screen-shaped well with the preset's
                    // share drawn as an accent band — the same visual grammar
                    // as the Virtual Screens preset thumbnails.
                    Rectangle {
                        id: thumbnail

                        readonly property real innerPad: Kirigami.Units.smallSpacing

                        Layout.preferredHeight: Kirigami.Units.gridUnit * 2.5
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 2.5 * 16 / 9
                        Layout.alignment: Qt.AlignVCenter
                        radius: Kirigami.Units.smallSpacing
                        color: Kirigami.Theme.alternateBackgroundColor
                        border.width: 1
                        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)

                        Rectangle {
                            anchors.centerIn: parent
                            width: editor.vertical ? parent.width - thumbnail.innerPad * 2 : Math.max(2, (parent.width - thumbnail.innerPad * 2) * presetCard.fraction)
                            height: editor.vertical ? Math.max(2, (parent.height - thumbnail.innerPad * 2) * presetCard.fraction) : parent.height - thumbnail.innerPad * 2
                            radius: Kirigami.Units.smallSpacing / 2
                            color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.25)
                            border.width: 1
                            border.color: Kirigami.Theme.highlightColor
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: i18n("%1%", presetCard.percent)
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    ToolButton {
                        Layout.alignment: Qt.AlignTop
                        icon.name: "edit-delete-remove"
                        Accessible.name: i18n("Remove %1% %2", presetCard.percent, editor.entryName)
                        display: AbstractButton.IconOnly
                        onClicked: {
                            var next = editor._values.slice();
                            next.splice(presetCard.index, 1);
                            editor._commitList(next);
                        }
                        ToolTip.text: i18n("Remove this preset")
                        ToolTip.visible: hovered
                    }
                }
            }
        }
    }

    RowLayout {
        spacing: Kirigami.Units.smallSpacing

        Label {
            text: i18n("Add preset:")
        }

        SettingsSpinBox {
            id: addSpin

            accessibleName: i18n("New %1 percent", editor.entryName)
            unitText: "%"
            from: 1
            to: 100
            value: 50
            stepSize: 5
        }

        Button {
            text: i18n("Add")
            icon.name: "list-add"
            flat: true
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
