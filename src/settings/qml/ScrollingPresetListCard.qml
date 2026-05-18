// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Editor card for a scroll-mode preset list (column widths or window
 *        heights).
 *
 * Each entry is a fraction [0.1..1.0] of the working area, edited as a whole
 * percent. The card never mutates @c values in place — every edit emits the
 * complete edited list through @c valuesModified so the page can route it
 * back through the Settings setter.
 */
SettingsCard {
    id: card

    // Fractions [0.1..1.0]; the page binds this to a Settings QVariantList.
    property var values: []
    property string description: ""

    signal valuesModified(var values)

    collapsible: true

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            visible: card.description.length > 0
            text: card.description
            wrapMode: Text.WordWrap
            opacity: 0.7
            font: Kirigami.Theme.smallFont
        }

        Repeater {
            model: card.values

            delegate: RowLayout {
                id: presetRow

                required property int index
                required property var modelData

                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.smallSpacing
                Layout.rightMargin: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: i18n("Preset %1", presetRow.index + 1)
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 6
                }

                SpinBox {
                    from: 10
                    to: 100
                    stepSize: 5
                    value: Math.round(presetRow.modelData * 100)
                    Accessible.name: i18n("Preset %1 size in percent", presetRow.index + 1)
                    textFromValue: function(value, locale) {
                        return Number(value).toLocaleString(locale, 'f', 0) + "%";
                    }
                    onValueModified: {
                        let copy = card.values.slice();
                        copy[presetRow.index] = value / 100;
                        card.valuesModified(copy);
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    icon.name: "list-remove"
                    Accessible.name: i18n("Remove preset %1", presetRow.index + 1)
                    // Keep at least one preset — an empty list disables the
                    // cycle shortcut entirely.
                    enabled: card.values.length > 1
                    onClicked: {
                        let copy = card.values.slice();
                        copy.splice(presetRow.index, 1);
                        card.valuesModified(copy);
                    }
                }

            }

        }

        Button {
            Layout.leftMargin: Kirigami.Units.smallSpacing
            text: i18n("Add preset")
            icon.name: "list-add"
            onClicked: {
                let copy = card.values.slice();
                copy.push(0.5);
                card.valuesModified(copy);
            }
        }

    }

}
