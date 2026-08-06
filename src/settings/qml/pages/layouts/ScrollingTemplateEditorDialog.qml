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
    // Width kind vocabulary, value bounds and the template authoring caps, read
    // once through the controller like the Scrolling pages do
    // (SettingsController::scrollingConstants) so the form never restates a
    // number the C++ side owns.
    readonly property var scrollingConstants: root.controller.scrollingConstants()
    // Largest storable preset index.
    readonly property int presetIndexMax: root.scrollingConstants.presetIndexMax
    // Proportion bounds arrive as fractions and the width spins show percent.
    // The floor is ConfigDefaults::scrollingDefaultColumnWidthProportionMin.
    // It holds for templates too because the store floors a template fraction
    // at PhosphorZones::MinTemplateFraction, and both of those are hand-written
    // mirrors of the engine's PhosphorScrollEngine::MinColumnWidthFraction.
    readonly property int proportionMinPercent: Math.round(root.scrollingConstants.proportionMin * 100)
    readonly property int proportionMaxPercent: Math.round(root.scrollingConstants.proportionMax * 100)
    // How many width presets the field currently parses to. Bound through the
    // field's text so it re-evaluates on every edit, which is what keeps the
    // preset-number spin and the Save gate below honest while the user types.
    readonly property int presetWidthCount: root.parseFractionList(presetWidthsField.text).length
    // The Preset width kind resolves an index into the width presets, so a
    // template that picks it while offering no presets would silently save as a
    // proportion. Gates Save and raises the message beneath the preset fields.
    readonly property bool presetDefaultNeedsWidths: defaultWidthKindCombo.currentValue === root.scrollingConstants.kindPreset && root.presetWidthCount === 0

    function openForNew() {
        root.templateId = "";
        root.editingSystem = false;
        nameField.text = "";
        descriptionField.text = "";
        columnsModel.clear();
        defaultWidthKindCombo.currentIndex = defaultWidthKindCombo.indexOfValue(root.scrollingConstants.kindPreset);
        defaultWidthValueSpin.value = 50;
        // The preset fields go in before the preset-number spin: that spin's
        // ceiling counts the widths this field holds, and a SpinBox clamps an
        // assigned value against the ceiling it has at that moment.
        presetWidthsField.text = "0.333, 0.5, 0.667";
        presetHeightsField.text = "0.333, 0.5, 0.667";
        defaultPresetIndexSpin.value = 2;
        defaultDisplayCombo.currentIndex = 0;
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
            // Clamped to the width spin's own range on the way in. The spin
            // would display-clamp a stored width outside it without touching
            // the model, and commit() writes the model back, so an out-of-range
            // width would silently survive a round trip through the form.
            columnsModel.append({
                "widthPercent": Math.max(root.proportionMinPercent, Math.min(root.proportionMaxPercent, Math.round((columns[i].width || 0.5) * 100))),
                "display": columns[i].display === 1 ? 1 : 0
            });
        const defaultWidth = data.defaultColumnWidth || {};
        // A stored kind outside the combo's vocabulary makes indexOfValue
        // return -1, and commit() would then write an undefined kind back.
        // Fall back to the width-preset kind, the same default openForNew uses.
        const kindIndex = defaultWidthKindCombo.indexOfValue(defaultWidth.kind !== undefined ? defaultWidth.kind : root.scrollingConstants.kindPreset);
        defaultWidthKindCombo.currentIndex = kindIndex >= 0 ? kindIndex : defaultWidthKindCombo.indexOfValue(root.scrollingConstants.kindPreset);
        // The Fixed kind stores a PIXEL count, every other kind a fraction of
        // the screen, so only the fraction kinds scale by 100 for the percent
        // spin. Read the STORED kind rather than the combo: the combo's index
        // is assigned on the line above and depending on that ordering is
        // needlessly fragile.
        const storedWidthValue = defaultWidth.value !== undefined ? defaultWidth.value : 0.5;
        defaultWidthValueSpin.value = defaultWidth.kind === root.scrollingConstants.kindFixed ? Math.round(storedWidthValue) : Math.round(storedWidthValue * 100);
        // The preset fields go in before the preset-number spin: that spin's
        // ceiling counts the widths this field holds, and a SpinBox clamps an
        // assigned value against the ceiling it has at that moment, so filling
        // them afterwards would strand the stored index at the previous
        // template's count.
        presetWidthsField.text = (data.presetColumnWidths || []).join(", ");
        presetHeightsField.text = (data.presetWindowHeights || []).join(", ");
        defaultPresetIndexSpin.value = (defaultWidth.presetIndex !== undefined ? defaultWidth.presetIndex : 1) + 1;
        defaultDisplayCombo.currentIndex = data.defaultColumnDisplay === 1 ? 1 : 0;
        root.open();
    }

    // Mirrors the store's normalizeFractionList (phosphor-zones): clamp
    // anything above 1 down to 1, drop anything below the proportion floor,
    // sort ascending, drop an entry within the dedupe epsilon of the one kept
    // before it, and keep at most as many as a template may have columns.
    // commit() writes the surviving list back into the field, so what stays on
    // screen is what was sent, and the preset-number spin counts the same
    // entries the store will keep. The daemon remains the authority and
    // normalizes again on the way in.
    function parseFractionList(text) {
        const parsed = text.split(",").map(part => parseFloat(part.trim())).filter(value => isFinite(value) && value >= root.scrollingConstants.proportionMin).map(value => Math.min(value, 1)).sort((a, b) => a - b);
        let kept = [];
        for (let i = 0; i < parsed.length && kept.length < root.scrollingConstants.maxTemplateColumns; i++) {
            if (kept.length === 0 || Math.abs(kept[kept.length - 1] - parsed[i]) >= root.scrollingConstants.fractionDedupeEpsilon)
                kept.push(parsed[i]);
        }
        return kept;
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
        const widths = root.parseFractionList(presetWidthsField.text);
        const heights = root.parseFractionList(presetHeightsField.text);
        presetWidthsField.text = widths.join(", ");
        presetHeightsField.text = heights.join(", ");
        const data = {
            "id": root.templateId,
            "name": nameField.text.trim(),
            "description": descriptionField.text.trim(),
            "columns": columns,
            "defaultColumnWidth": {
                "kind": defaultWidthKindCombo.currentValue,
                // Fixed is a pixel count and goes over as typed. The other
                // kinds are fractions the spin shows as a percentage.
                "value": defaultWidthKindCombo.currentValue === root.scrollingConstants.kindFixed ? defaultWidthValueSpin.value : defaultWidthValueSpin.value / 100.0,
                "presetIndex": defaultPresetIndexSpin.value - 1
            },
            "defaultColumnDisplay": defaultDisplayCombo.currentIndex,
            "presetColumnWidths": widths,
            "presetWindowHeights": heights
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
            enabled: nameField.text.trim().length > 0 && !root.presetDefaultNeedsWidths
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
                // The same cap the D-Bus boundary applies in the layout
                // adaptor's saveScrollingTemplate (clampName), read from the
                // constants map so typing stops exactly where the store would
                // have cut silently.
                maximumLength: root.scrollingConstants.nameMaxLength
            }

            TextField {
                id: descriptionField

                Kirigami.FormData.label: i18n("Description:")
                placeholderText: i18n("Optional description")
                // Same boundary clamp, description arm.
                maximumLength: root.scrollingConstants.descriptionMaxLength
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
                    from: root.proportionMinPercent
                    to: root.proportionMaxPercent
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

        RowLayout {
            id: addColumnRow

            // The store truncates a template's column list at
            // PhosphorZones::MaxTemplateColumns (mirroring the engine's
            // blueprint cap), so rows past that would vanish on save.
            readonly property int columnLimit: root.scrollingConstants.maxTemplateColumns

            spacing: Kirigami.Units.smallSpacing

            Button {
                text: i18n("Add Column")
                icon.name: "list-add"
                enabled: columnsModel.count < addColumnRow.columnLimit
                onClicked: columnsModel.append({
                    "widthPercent": 50,
                    "display": 0
                })
            }

            // A disabled Button takes no hover, so the ceiling is spelled out
            // beside it rather than in a tooltip nobody could reach.
            Label {
                Layout.fillWidth: true
                visible: columnsModel.count >= addColumnRow.columnLimit
                wrapMode: Text.WordWrap
                opacity: 0.7
                text: i18n("A template can start at most %1 columns.", addColumnRow.columnLimit)
            }
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
                        "value": root.scrollingConstants.kindProportion
                    },
                    {
                        "text": i18n("Fixed pixels"),
                        "value": root.scrollingConstants.kindFixed
                    },
                    {
                        "text": i18n("The window decides"),
                        "value": root.scrollingConstants.kindClientDecides
                    },
                    {
                        "text": i18n("Width preset"),
                        "value": root.scrollingConstants.kindPreset
                    }
                ]
            }

            SpinBox {
                id: defaultWidthValueSpin

                readonly property bool isFixed: defaultWidthKindCombo.currentValue === root.scrollingConstants.kindFixed

                Kirigami.FormData.label: defaultWidthValueSpin.isFixed ? i18n("Width in pixels:") : i18n("Width:")
                visible: defaultWidthKindCombo.currentValue === root.scrollingConstants.kindProportion || defaultWidthValueSpin.isFixed
                // The Fixed floor is deliberate UI policy rather than a hard
                // limit: ConfigDefaults sets it at 100px, well above the
                // engine's own 1px floor, because a column narrower than that
                // has nowhere to draw a window.
                from: defaultWidthValueSpin.isFixed ? Math.round(root.scrollingConstants.fixedMin) : root.proportionMinPercent
                to: defaultWidthValueSpin.isFixed ? Math.round(root.scrollingConstants.fixedMax) : root.proportionMaxPercent
                textFromValue: (value, locale) => defaultWidthValueSpin.isFixed ? String(value) : i18n("%1%", value)
                valueFromText: (text, locale) => parseInt(text)
            }

            SpinBox {
                id: defaultPresetIndexSpin

                Kirigami.FormData.label: i18n("Preset number:")
                visible: defaultWidthKindCombo.currentValue === root.scrollingConstants.kindPreset
                // Stored 0-based, shown 1-based, the same offset the Scrolling
                // pages' preset spins use. The ceiling is the smaller of the
                // schema cap (ConfigDefaults::scrollingPresetIndexMax) and the
                // presets the width-presets field yields once normalized the
                // way the store normalizes them, so the spin can never offer a
                // number the stored list has no entry for. The floor of 1 keeps
                // the spin valid while that field is empty.
                from: 1
                to: Math.max(1, Math.min(root.presetWidthCount, root.presetIndexMax + 1))
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

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.presetDefaultNeedsWidths
            type: Kirigami.MessageType.Warning
            text: i18n("The default width is set to a width preset, so this template needs at least one width preset before it can be saved.")
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            opacity: 0.7
            text: i18n("Presets are the sizes the width and height cycling shortcuts step through while this template is assigned.")
        }
    }
}
