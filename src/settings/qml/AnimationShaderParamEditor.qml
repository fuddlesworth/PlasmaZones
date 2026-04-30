// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Inline parameter editor for an animation shader effect.
 *
 * Embedded inside AnimationEventCard when the user has a shader assigned
 * to the event. Reads parameter metadata from
 * `AnimationsPageController.shaderParameters(effectId)` and binds each
 * row's control by `type` (float / int / bool / color).
 *
 * Pending edits are buffered locally; commits happen on slider release /
 * combo activation via `paramsChanged(QVariantMap)`. The hosting
 * AnimationEventCard merges them into the per-event ShaderProfile and
 * routes through `setShaderOverride`.
 *
 * Required properties:
 *   - effectId: string — empty hides the editor
 *   - currentParams: var — { paramId: value, ... }
 */
ColumnLayout {
    // ── Per-type row components ──────────────────────────────────────

    id: root

    required property string effectId
    required property var currentParams
    readonly property var _paramSchema: effectId.length > 0 ? settingsController.animationsPage.shaderParameters(effectId) : []

    signal paramsChanged(var newParams)

    function paramValue(paramId, fallback) {
        if (currentParams && currentParams[paramId] !== undefined)
            return currentParams[paramId];

        return fallback;
    }

    function commit(paramId, value) {
        var next = Object.assign({
        }, currentParams || {
        });
        next[paramId] = value;
        root.paramsChanged(next);
    }

    function resetToDefaults() {
        var next = {
        };
        for (var i = 0; i < _paramSchema.length; i++) next[_paramSchema[i].id] = _paramSchema[i].defaultValue
        root.paramsChanged(next);
    }

    spacing: Kirigami.Units.smallSpacing
    visible: effectId.length > 0 && _paramSchema.length > 0

    Repeater {
        model: root._paramSchema

        delegate: Loader {
            required property var modelData

            Layout.fillWidth: true
            sourceComponent: {
                switch (modelData.type) {
                case "float":
                    return floatRow;
                case "int":
                    return intRow;
                case "bool":
                    return boolRow;
                case "color":
                    return colorRow;
                default:
                    return null;
                }
            }
            // Pass parameter info through Loader.item via property binding
            // — simpler than dynamic component creation for typed delegates.
            onLoaded: {
                if (item) {
                    item.paramInfo = modelData;
                    item.value = root.paramValue(modelData.id, modelData.defaultValue);
                }
            }
        }

    }

    SettingsSeparator {
    }

    Button {
        Layout.alignment: Qt.AlignRight
        text: i18n("Reset to defaults")
        icon.name: "edit-undo"
        onClicked: root.resetToDefaults()
    }

    Component {
        id: floatRow

        SettingsRow {
            property var paramInfo: ({
            })
            property real value: 0

            title: paramInfo.name || ""

            SettingsSlider {
                Accessible.name: paramInfo.name || ""
                from: paramInfo.minValue !== undefined ? paramInfo.minValue : 0
                to: paramInfo.maxValue !== undefined ? paramInfo.maxValue : 1
                stepSize: 0.01
                value: parent.value
                formatValue: function(v) {
                    return v.toFixed(2);
                }
                onMoved: function(v) {
                    root.commit(parent.paramInfo.id, v);
                }
            }

        }

    }

    Component {
        id: intRow

        SettingsRow {
            property var paramInfo: ({
            })
            property int value: 0

            title: paramInfo.name || ""

            SettingsSlider {
                Accessible.name: paramInfo.name || ""
                from: paramInfo.minValue !== undefined ? paramInfo.minValue : 0
                to: paramInfo.maxValue !== undefined ? paramInfo.maxValue : 100
                stepSize: 1
                value: parent.value
                onMoved: function(v) {
                    root.commit(parent.paramInfo.id, Math.round(v));
                }
            }

        }

    }

    Component {
        id: boolRow

        SettingsRow {
            property var paramInfo: ({
            })
            property bool value: false

            title: paramInfo.name || ""

            SettingsSwitch {
                checked: parent.value
                accessibleName: paramInfo.name || ""
                onToggled: function(v) {
                    root.commit(parent.paramInfo.id, v);
                }
            }

        }

    }

    Component {
        id: colorRow

        SettingsRow {
            id: row

            property var paramInfo: ({
            })
            property var value: "#ffffffff"

            title: paramInfo.name || ""

            ColorButton {
                color: row.value
                onClicked: {
                    colorDialog.selectedColor = row.value;
                    colorDialog.open();
                }
            }

            ColorDialog {
                id: colorDialog

                title: row.paramInfo.name || i18n("Pick color")
                onAccepted: {
                    row.value = selectedColor;
                    root.commit(row.paramInfo.id, selectedColor.toString());
                }
            }

        }

    }

}
