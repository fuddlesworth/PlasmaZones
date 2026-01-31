// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Unified parameter delegate for shader settings
 *
 * Handles all parameter types (float, int, bool, color) with visibility-based
 * switching. This avoids the Loader/onLoaded timing issues that caused
 * binding evaluation before paramData was set.
 */
Item {
    id: paramDelegate

    // Required parameter data from Repeater modelData
    property var paramData: null

    // Dialog reference - passed explicitly from ShaderSettingsDialog
    // This is more reliable than parent chain walking through Loaders/FormLayouts
    property var dialogRoot: null

    // Fallback: Walk parent chain if dialogRoot not passed explicitly
    readonly property var resolvedDialogRoot: {
        if (dialogRoot) return dialogRoot;
        // Fallback parent chain walk
        var p = parent;
        while (p) {
            if (typeof p.pendingParams !== "undefined" &&
                typeof p.parameterValue === "function" &&
                typeof p.setPendingParam === "function") {
                return p;
            }
            p = p.parent;
        }
        return null;
    }

    // Track pendingParams changes to force value re-evaluation
    readonly property var _pendingRef: resolvedDialogRoot ? resolvedDialogRoot.pendingParams : null

    // Computed type for visibility switching
    readonly property string paramType: paramData ? (paramData.type || "") : ""

    implicitHeight: contentLayout.implicitHeight
    implicitWidth: contentLayout.implicitWidth

    RowLayout {
        id: contentLayout
        anchors.fill: parent
        spacing: Kirigami.Units.smallSpacing

        // ═══════════════════════════════════════════════════════════════
        // FLOAT PARAMETER
        // ═══════════════════════════════════════════════════════════════
        Slider {
            id: floatSlider
            visible: paramDelegate.paramType === "float"
            Layout.fillWidth: true

            from: paramDelegate.paramData && paramDelegate.paramData.min !== undefined ? paramDelegate.paramData.min : 0
            to: paramDelegate.paramData && paramDelegate.paramData.max !== undefined ? paramDelegate.paramData.max : 1
            stepSize: paramDelegate.paramData && paramDelegate.paramData.step !== undefined ? paramDelegate.paramData.step : 0.01

            value: {
                void(paramDelegate._pendingRef);
                if (!paramDelegate.paramData || !paramDelegate.resolvedDialogRoot) return 0.5;
                var val = paramDelegate.resolvedDialogRoot.parameterValue(
                    paramDelegate.paramData.id,
                    paramDelegate.paramData.default !== undefined ? paramDelegate.paramData.default : 0.5
                );
                // Ensure number type for Slider
                var num = Number(val);
                return isNaN(num) ? 0.5 : num;
            }

            ToolTip.text: paramDelegate.paramData ? (paramDelegate.paramData.description || "") : ""
            ToolTip.visible: hovered && paramDelegate.paramData && paramDelegate.paramData.description !== undefined && paramDelegate.paramData.description !== ""
            ToolTip.delay: Kirigami.Units.toolTipDelay

            onMoved: {
                if (paramDelegate.paramData && paramDelegate.resolvedDialogRoot) {
                    paramDelegate.resolvedDialogRoot.setPendingParam(paramDelegate.paramData.id, value);
                }
            }
        }

        Label {
            visible: paramDelegate.paramType === "float"
            text: floatSlider.value.toFixed(2)
            Layout.preferredWidth: paramDelegate.resolvedDialogRoot ? paramDelegate.resolvedDialogRoot.sliderValueLabelWidth : 50
            horizontalAlignment: Text.AlignRight
            font: Kirigami.Theme.fixedWidthFont
        }

        // ═══════════════════════════════════════════════════════════════
        // INT PARAMETER
        // ═══════════════════════════════════════════════════════════════
        SpinBox {
            id: intSpinBox
            visible: paramDelegate.paramType === "int"

            from: paramDelegate.paramData && paramDelegate.paramData.min !== undefined ? paramDelegate.paramData.min : 0
            to: paramDelegate.paramData && paramDelegate.paramData.max !== undefined ? paramDelegate.paramData.max : 100

            value: {
                void(paramDelegate._pendingRef);
                if (!paramDelegate.paramData || !paramDelegate.resolvedDialogRoot) return 0;
                var val = paramDelegate.resolvedDialogRoot.parameterValue(
                    paramDelegate.paramData.id,
                    paramDelegate.paramData.default !== undefined ? paramDelegate.paramData.default : 0
                );
                // Ensure integer type for SpinBox
                var num = parseInt(val, 10);
                return isNaN(num) ? 0 : num;
            }

            ToolTip.text: paramDelegate.paramData ? (paramDelegate.paramData.description || "") : ""
            ToolTip.visible: hovered && paramDelegate.paramData && paramDelegate.paramData.description !== undefined && paramDelegate.paramData.description !== ""
            ToolTip.delay: Kirigami.Units.toolTipDelay

            onValueModified: {
                if (paramDelegate.paramData && paramDelegate.resolvedDialogRoot) {
                    paramDelegate.resolvedDialogRoot.setPendingParam(paramDelegate.paramData.id, value);
                }
            }
        }

        Item {
            visible: paramDelegate.paramType === "int"
            Layout.fillWidth: true
        }

        // ═══════════════════════════════════════════════════════════════
        // BOOL PARAMETER
        // ═══════════════════════════════════════════════════════════════
        CheckBox {
            id: boolCheckBox
            visible: paramDelegate.paramType === "bool"

            text: paramDelegate.paramData ? (paramDelegate.paramData.description || "") : ""

            checked: {
                void(paramDelegate._pendingRef);
                if (!paramDelegate.paramData || !paramDelegate.resolvedDialogRoot) return false;
                var val = paramDelegate.resolvedDialogRoot.parameterValue(
                    paramDelegate.paramData.id,
                    paramDelegate.paramData.default !== undefined ? paramDelegate.paramData.default : false
                );
                // Ensure boolean type for CheckBox
                return Boolean(val);
            }

            onToggled: {
                if (paramDelegate.paramData && paramDelegate.resolvedDialogRoot) {
                    paramDelegate.resolvedDialogRoot.setPendingParam(paramDelegate.paramData.id, checked);
                }
            }
        }

        Item {
            visible: paramDelegate.paramType === "bool"
            Layout.fillWidth: true
        }

        // ═══════════════════════════════════════════════════════════════
        // COLOR PARAMETER
        // ═══════════════════════════════════════════════════════════════
        Rectangle {
            id: colorSwatch
            visible: paramDelegate.paramType === "color"

            property color currentColor: {
                void(paramDelegate._pendingRef);
                if (!paramDelegate.paramData || !paramDelegate.resolvedDialogRoot) return "#ffffff";
                var fallback = (typeof paramDelegate.paramData.default === "string" && paramDelegate.paramData.default.length > 0)
                    ? paramDelegate.paramData.default : "#ffffff";
                var colorStr = paramDelegate.resolvedDialogRoot.parameterValue(paramDelegate.paramData.id, fallback);
                if (typeof colorStr !== "string" || colorStr.length === 0) {
                    return fallback;
                }
                var parsed = Qt.color(colorStr);
                return parsed.valid ? colorStr : fallback;
            }

            width: paramDelegate.resolvedDialogRoot ? paramDelegate.resolvedDialogRoot.colorButtonSize : 36
            height: width
            radius: Kirigami.Units.smallSpacing
            color: currentColor
            border.width: Math.max(1, Math.round(Kirigami.Units.devicePixelRatio))
            border.color: paramDelegate.resolvedDialogRoot ? paramDelegate.resolvedDialogRoot.themeSeparatorColor : "#ccc"

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                Accessible.name: i18nc("@action:button", "Choose %1 color", paramDelegate.paramData ? (paramDelegate.paramData.name || paramDelegate.paramData.id) : "")
                Accessible.role: Accessible.Button

                onClicked: {
                    if (!paramDelegate.paramData || !paramDelegate.resolvedDialogRoot) return;
                    paramDelegate.resolvedDialogRoot.openColorDialog(paramDelegate.paramData.id, paramDelegate.paramData.name || paramDelegate.paramData.id, colorSwatch.currentColor);
                }
            }
        }

        Label {
            visible: paramDelegate.paramType === "color"
            text: colorSwatch.currentColor.toString().toUpperCase()
            Layout.preferredWidth: paramDelegate.resolvedDialogRoot ? paramDelegate.resolvedDialogRoot.colorLabelWidth : 80
            font: Kirigami.Theme.fixedWidthFont
            opacity: 0.7
        }

        Item {
            visible: paramDelegate.paramType === "color"
            Layout.fillWidth: true
        }
    }
}
