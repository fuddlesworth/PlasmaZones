// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One editable action row inside ActionListEditor.
 *
 * An action is a `{ type, ...params }` JSON object. The row exposes a type
 * dropdown and a single value field whose shape depends on the type. Two-way:
 * edits emit `actionChanged(updatedAction)`; the parent owns the list.
 */
RowLayout {
    id: row

    /// The action JSON object being edited — `{ type, ...params }`.
    required property var action
    /// Registered action types from `WindowRuleController.actionTypes()`.
    required property var actionTypeOptions

    signal actionChanged(var updatedAction)
    signal removeRequested()

    /// Shallow-clone the action so a mutation produces a fresh object QML
    /// rebinds against (mutating in place would not re-trigger bindings).
    function _withParam(key, value) {
        var next = {
        };
        for (var k in row.action) next[k] = row.action[k]
        next[key] = value;
        return next;
    }

    function _typeIndex() {
        for (var i = 0; i < row.actionTypeOptions.length; ++i) {
            if (row.actionTypeOptions[i].value === row.action.type)
                return i;

        }
        return 0;
    }

    spacing: Kirigami.Units.smallSpacing

    Kirigami.Icon {
        source: "arrow-right"
        Layout.preferredWidth: Kirigami.Units.iconSizes.small
        Layout.preferredHeight: Kirigami.Units.iconSizes.small
        Layout.alignment: Qt.AlignVCenter
    }

    ComboBox {
        id: typeCombo

        Layout.preferredWidth: Kirigami.Units.gridUnit * 13
        textRole: "label"
        valueRole: "value"
        model: row.actionTypeOptions
        currentIndex: row._typeIndex()
        Accessible.name: i18n("Action type")
        onActivated: function(index) {
            if (currentValue !== row.action.type)
                row.actionChanged({
                "type": currentValue
            });

        }
    }

    // Value editor — shape depends on the action type.
    Loader {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        sourceComponent: {
            var t = row.action.type;
            if (t === "setEngineMode")
                return engineModeEditor;

            if (t === "setSnappingLayout")
                return layoutIdEditor;

            if (t === "setTilingAlgorithm")
                return algorithmEditor;

            if (t === "setOpacity")
                return opacityEditor;

            if (t === "overrideAnimationShader" || t === "overrideAnimationTiming")
                return animationEventEditor;

            // float / disableEngine / exclude carry no params.
            return null;
        }
    }

    ToolButton {
        icon.name: "edit-delete"
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("Remove action")
        ToolTip.visible: hovered
        Accessible.name: i18n("Remove this action")
        onClicked: row.removeRequested()
    }

    Component {
        id: engineModeEditor

        ComboBox {
            model: ["snapping", "autotile"]
            currentIndex: Math.max(0, model.indexOf(row.action.mode || "snapping"))
            Accessible.name: i18n("Engine mode")
            onActivated: row.actionChanged(row._withParam("mode", currentText))
        }

    }

    Component {
        id: layoutIdEditor

        TextField {
            text: row.action.layoutId || ""
            placeholderText: i18n("Snapping layout id")
            Accessible.name: i18n("Snapping layout id")
            onEditingFinished: row.actionChanged(row._withParam("layoutId", text))
        }

    }

    Component {
        id: algorithmEditor

        TextField {
            text: row.action.algorithm || ""
            placeholderText: i18n("Tiling algorithm id")
            Accessible.name: i18n("Tiling algorithm id")
            onEditingFinished: row.actionChanged(row._withParam("algorithm", text))
        }

    }

    Component {
        id: opacityEditor

        SpinBox {
            from: 0
            to: 100
            value: Math.round((row.action.value !== undefined ? row.action.value : 1) * 100)
            Accessible.name: i18n("Opacity percentage")
            onValueModified: row.actionChanged(row._withParam("value", value / 100))
        }

    }

    Component {
        id: animationEventEditor

        TextField {
            text: row.action.event || ""
            placeholderText: i18n("Event path, e.g. window.open")
            Accessible.name: i18n("Animation event path")
            onEditingFinished: row.actionChanged(row._withParam("event", text))
        }

    }

}
