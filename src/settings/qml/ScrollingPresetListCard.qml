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
 *
 * The Repeater binds to an internal ListModel rather than the @c values array
 * directly: a value edit updates that model in place and re-syncs only when
 * the round-tripped @c values genuinely differ. An in-range edit round-trips
 * to an identical list, so the delegates are not rebuilt and the spin box
 * being edited keeps focus.
 */
SettingsCard {
    id: card

    // Fractions [0.1..1.0]; the page binds this to a Settings QVariantList.
    property var values: []
    property string description: ""

    signal valuesModified(var values)

    // Repopulate the internal model from `values`, but only when the two
    // genuinely differ — an in-range edit round-trips back through Settings to
    // an identical list, and skipping the rebuild then keeps every delegate
    // (and the spin box's keyboard focus) alive.
    function syncModel() {
        const v = card.values || [];
        if (presetModel.count === v.length) {
            let identical = true;
            for (let i = 0; i < v.length; ++i) {
                if (presetModel.get(i).fraction !== v[i]) {
                    identical = false;
                    break;
                }
            }
            if (identical)
                return ;

        }
        presetModel.clear();
        for (let i = 0; i < v.length; ++i) presetModel.append({
            "fraction": v[i]
        })
    }

    // Emit the current model contents as the complete edited list.
    function emitValues() {
        let out = [];
        for (let i = 0; i < presetModel.count; ++i) out.push(presetModel.get(i).fraction)
        card.valuesModified(out);
    }

    collapsible: true
    onValuesChanged: syncModel()
    Component.onCompleted: syncModel()

    ListModel {
        id: presetModel
    }

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
            model: presetModel

            delegate: RowLayout {
                id: presetRow

                required property int index
                required property real fraction

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
                    value: Math.round(presetRow.fraction * 100)
                    Accessible.name: i18n("Preset %1 size in percent", presetRow.index + 1)
                    textFromValue: function(value, locale) {
                        return Number(value).toLocaleString(locale, 'f', 0) + "%";
                    }
                    onValueModified: {
                        // Update the model in place — no structural change, so
                        // the Repeater keeps this delegate (and its focus).
                        presetModel.setProperty(presetRow.index, "fraction", value / 100);
                        card.emitValues();
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
                    enabled: presetModel.count > 1
                    onClicked: {
                        presetModel.remove(presetRow.index);
                        card.emitValues();
                    }
                }

            }

        }

        Button {
            Layout.leftMargin: Kirigami.Units.smallSpacing
            text: i18n("Add preset")
            icon.name: "list-add"
            onClicked: {
                presetModel.append({
                    "fraction": 0.5
                });
                card.emitValues();
            }
        }

    }

}
