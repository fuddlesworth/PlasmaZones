// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Form editor for a native scrolling template.
 *
 * One dialog serves create and edit: openForNew() starts a blank template,
 * openForEdit(id) loads the stored one through
 * SettingsController.scrollingTemplateForEditing. Saving a bundled template
 * writes a user copy that shadows it (the daemon store's contract), so the
 * form is editable either way and only notes the shadowing.
 */
Kirigami.Dialog {
    id: root

    required property var controller // SettingsController — template CRUD
    property string templateId: ""
    property bool editingSystem: false

    readonly property var displayOptions: [i18n("Stacked"), i18n("Tabbed")]

    function openForNew() {
        root.templateId = "";
        root.editingSystem = false;
        nameField.text = "";
        descriptionField.text = "";
        columnsModel.clear();
        defaultWidthKindCombo.currentIndex = defaultWidthKindCombo.indexOfValue(3);
        defaultWidthValueSpin.value = 50;
        defaultPresetIndexSpin.value = 2;
        defaultDisplayCombo.currentIndex = 0;
        presetWidthsField.text = "0.333, 0.5, 0.667";
        presetHeightsField.text = "0.333, 0.5, 0.667";
        root.open();
    }

    function openForEdit(id) {
        const data = root.controller.scrollingTemplateForEditing(id);
        if (!data || !data.id) {
            if (typeof window !== "undefined" && window && window.showToast)
                window.showToast(i18n("That template is no longer available."));
            return;
        }
        root.templateId = data.id;
        root.editingSystem = data.isSystem === true;
        nameField.text = data.name || "";
        descriptionField.text = data.description || "";
        columnsModel.clear();
        const columns = data.columns || [];
        for (let i = 0; i < columns.length; i++)
            columnsModel.append({
                "widthPercent": Math.round((columns[i].width || 0.5) * 100),
                "display": columns[i].display === 1 ? 1 : 0
            });
        const defaultWidth = data.defaultColumnWidth || {};
        // A stored kind outside the combo's vocabulary makes indexOfValue
        // return -1, and commit() would then write an undefined kind back.
        // Fall back to the width-preset kind, the same default openForNew uses.
        const kindIndex = defaultWidthKindCombo.indexOfValue(defaultWidth.kind !== undefined ? defaultWidth.kind : 3);
        defaultWidthKindCombo.currentIndex = kindIndex >= 0 ? kindIndex : defaultWidthKindCombo.indexOfValue(3);
        defaultWidthValueSpin.value = Math.round((defaultWidth.value !== undefined ? defaultWidth.value : 0.5) * 100);
        defaultPresetIndexSpin.value = (defaultWidth.presetIndex !== undefined ? defaultWidth.presetIndex : 1) + 1;
        defaultDisplayCombo.currentIndex = data.defaultColumnDisplay === 1 ? 1 : 0;
        presetWidthsField.text = (data.presetColumnWidths || []).join(", ");
        presetHeightsField.text = (data.presetWindowHeights || []).join(", ");
        root.open();
    }

    // Entries outside (0, 1] are dropped rather than passed on, so the saved
    // list matches what the field shows. The daemon clamps anything below 0.05
    // to 0.05 regardless.
    function parseFractionList(text) {
        return text.split(",").map(part => parseFloat(part.trim())).filter(value => !isNaN(value) && value > 0 && value <= 1);
    }

    function commit() {
        let columns = [];
        for (let i = 0; i < columnsModel.count; i++) {
            const row = columnsModel.get(i);
            columns.push({
                "width": row.widthPercent / 100.0,
                "display": row.display
            });
        }
        const data = {
            "id": root.templateId,
            "name": nameField.text.trim(),
            "description": descriptionField.text.trim(),
            "columns": columns,
            "defaultColumnWidth": {
                "kind": defaultWidthKindCombo.currentValue,
                "value": defaultWidthValueSpin.value / 100.0,
                "presetIndex": defaultPresetIndexSpin.value - 1
            },
            "defaultColumnDisplay": defaultDisplayCombo.currentIndex,
            "presetColumnWidths": root.parseFractionList(presetWidthsField.text),
            "presetWindowHeights": root.parseFractionList(presetHeightsField.text)
        };
        if (root.controller.saveScrollingTemplate(data))
            root.close();
    }

    title: templateId.length > 0 ? i18n("Edit Scrolling Template") : i18n("New Scrolling Template")
    preferredWidth: Kirigami.Units.gridUnit * 28
    standardButtons: Kirigami.Dialog.NoButton
    customFooterActions: [
        Kirigami.Action {
            text: i18n("Save")
            icon.name: "document-save"
            enabled: nameField.text.trim().length > 0
            onTriggered: root.commit()
        },
        Kirigami.Action {
            text: i18n("Cancel")
            icon.name: "dialog-cancel"
            onTriggered: root.close()
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.editingSystem
            type: Kirigami.MessageType.Information
            text: i18n("This is a built-in template. Saving stores your own copy, and deleting that copy brings the built-in one back.")
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true

            TextField {
                id: nameField

                Kirigami.FormData.label: i18n("Name:")
                placeholderText: i18n("Template name")
            }

            TextField {
                id: descriptionField

                Kirigami.FormData.label: i18n("Description:")
                placeholderText: i18n("Optional description")
            }
        }

        Kirigami.Heading {
            level: 3
            text: i18n("Starting columns")
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: i18n("The first windows you open form these columns, left to right. Later windows use the default width below. Leave the list empty for a template that only sets the width presets.")
        }

        ListModel {
            id: columnsModel
        }

        Repeater {
            model: columnsModel

            delegate: RowLayout {
                id: columnRow

                required property int index
                required property var model
                // Lifted to the delegate root because inside the ComboBox below
                // `model` resolves to the ComboBox's OWN model (the display
                // options array), not the row.
                readonly property int columnDisplay: model.display

                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: i18n("Column %1", index + 1)
                }

                SpinBox {
                    from: 5
                    to: 100
                    value: model.widthPercent
                    textFromValue: (value, locale) => i18n("%1%", value)
                    valueFromText: (text, locale) => parseInt(text)
                    Accessible.name: i18n("Width of column %1", index + 1)
                    onValueModified: columnsModel.setProperty(index, "widthPercent", value)
                }

                ComboBox {
                    model: root.displayOptions
                    currentIndex: columnRow.columnDisplay
                    Accessible.name: i18n("Display mode of column %1", columnRow.index + 1)
                    onActivated: idx => columnsModel.setProperty(columnRow.index, "display", idx)
                }

                ToolButton {
                    icon.name: "list-remove"
                    Accessible.name: i18n("Remove column %1", index + 1)
                    onClicked: columnsModel.remove(index)
                }
            }
        }

        Button {
            text: i18n("Add Column")
            icon.name: "list-add"
            onClicked: columnsModel.append({
                "widthPercent": 50,
                "display": 0
            })
        }

        Kirigami.Heading {
            level: 3
            text: i18n("Later columns")
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true

            ComboBox {
                id: defaultWidthKindCombo

                Kirigami.FormData.label: i18n("Default width:")
                textRole: "text"
                valueRole: "value"
                model: [
                    {
                        "text": i18n("Fraction of the screen"),
                        "value": 0
                    },
                    {
                        "text": i18n("Fixed pixels"),
                        "value": 1
                    },
                    {
                        "text": i18n("The window decides"),
                        "value": 2
                    },
                    {
                        "text": i18n("Width preset"),
                        "value": 3
                    }
                ]
            }

            SpinBox {
                id: defaultWidthValueSpin

                Kirigami.FormData.label: defaultWidthKindCombo.currentValue === 1 ? i18n("Width in pixels:") : i18n("Width:")
                visible: defaultWidthKindCombo.currentValue === 0 || defaultWidthKindCombo.currentValue === 1
                from: defaultWidthKindCombo.currentValue === 1 ? 100 : 5
                to: defaultWidthKindCombo.currentValue === 1 ? 10000 : 100
                textFromValue: (value, locale) => defaultWidthKindCombo.currentValue === 1 ? String(value) : i18n("%1%", value)
                valueFromText: (text, locale) => parseInt(text)
            }

            SpinBox {
                id: defaultPresetIndexSpin

                Kirigami.FormData.label: i18n("Preset number:")
                visible: defaultWidthKindCombo.currentValue === 3
                from: 1
                to: 16
            }

            ComboBox {
                id: defaultDisplayCombo

                Kirigami.FormData.label: i18n("Show windows as:")
                model: root.displayOptions
            }

            TextField {
                id: presetWidthsField

                Kirigami.FormData.label: i18n("Width presets:")
                placeholderText: i18n("For example 0.333, 0.5, 0.667")
            }

            TextField {
                id: presetHeightsField

                Kirigami.FormData.label: i18n("Height presets:")
                placeholderText: i18n("For example 0.333, 0.5, 0.667")
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: i18n("Presets are the sizes the width and height cycling shortcuts step through while this template is assigned.")
        }
    }
}
