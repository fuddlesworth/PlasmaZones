// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * Single input box for modifier and/or mouse trigger.
 * Uses TextField styling to match other form inputs (e.g. assignments tab).
 * Click in the box to capture; one key/modifier/mouse overwrites the value.
 * X inside the box clears. No pills; single value only.
 * acceptMode: MetaOnly (modifier keys only), MouseOnly (any mouse button: Right, Middle, Back, Forward, Extra 3–5), or All.
 *
 * Multi-bind mode (allowMultiple: true):
 * Shows a list of triggers with Add/Remove buttons. Each trigger is a separate
 * modifier or mouse button that can independently activate the feature.
 *
 * The caller owns the state in both modes. The component never writes
 * modifierValue/mouseButtonValue/triggers itself; it emits valueModified,
 * mouseButtonsModified, or triggersModified and the caller persists the
 * value and propagates it back through the property binding. A consumer
 * that ignores the signals will not see captures or clears reflected.
 */
Item {
    // Edit mode: replace the trigger at the edited index
    // Add mode: append

    id: root

    readonly property int acceptModeAll: 0
    readonly property int acceptModeMetaOnly: 1
    readonly property int acceptModeMouseOnly: 2
    // ═══════════════════════════════════════════════════════════════════════════
    // Single-mode properties (used when allowMultiple is false)
    // ═══════════════════════════════════════════════════════════════════════════
    property int modifierValue: 0
    property int mouseButtonValue: 0
    property int defaultModifierValue: 0
    property int defaultMouseButtonValue: 0
    property int acceptMode: acceptModeAll
    property bool tooltipEnabled: true
    //* When set, overrides the default tooltip for the input field (use instead of ToolTip.text; Item has no ToolTip attached type).
    property string customTooltipText: ""
    // ═══════════════════════════════════════════════════════════════════════════
    // Multi-mode properties (used when allowMultiple is true)
    // ═══════════════════════════════════════════════════════════════════════════
    property bool allowMultiple: false
    property var triggers: [] // [{modifier: bitmask, mouseButton: buttonBit}, ...]
    property var defaultTriggers: []
    readonly property int maxTriggers: 4
    // -1 = adding a new trigger, >= 0 = editing trigger at that index
    property int editingTriggerIndex: -1
    // Bits and labels come from the TriggerLabels singleton so this editor and
    // the profile diff name the same trigger the same way.
    readonly property int shiftFlag: TriggerLabels.shiftFlag
    readonly property int ctrlFlag: TriggerLabels.ctrlFlag
    readonly property int altFlag: TriggerLabels.altFlag
    readonly property int metaFlag: TriggerLabels.metaFlag
    readonly property var modifierChips: TriggerLabels.modifiers
    readonly property var mouseButtonList: TriggerLabels.mouseButtons

    signal valueModified(int modifierValue)
    signal mouseButtonsModified(int mouseButtonValue)
    signal triggersModified(var triggers)

    //* Scan a modifier bitmask + mouse button bit into a "A + B" label,
    //* honouring acceptMode. Returns emptyText when nothing is set — the two
    //* callers only differ in what an empty capture reads as.
    function _scanText(modifier, mouseButton, emptyText) {
        var parts = [];
        if (acceptMode !== acceptModeMouseOnly) {
            for (var i = 0; i < modifierChips.length; i++)
                if ((modifier & modifierChips[i].bit) !== 0) {
                    parts.push(modifierChips[i].label);
                }
        }
        if (acceptMode !== acceptModeMetaOnly) {
            for (var j = 0; j < mouseButtonList.length; j++)
                if ((mouseButton & mouseButtonList[j].bit) !== 0) {
                    parts.push(mouseButtonList[j].label);
                }
        }
        if (parts.length === 0)
            return emptyText;

        return parts.join(" + ");
    }

    function displayText() {
        return _scanText(modifierValue, mouseButtonValue, "");
    }

    //* Display text for a single trigger (modifier bitmask + mouse button bit)
    function triggerDisplayText(modifier, mouseButton) {
        return _scanText(modifier, mouseButton, i18n("(none)"));
    }

    function clearAll() {
        // Emit only; writing modifierValue/mouseButtonValue here would sever
        // the caller's bindings. The caller persists the defaults and the
        // bindings propagate them back.
        if (modifierValue !== defaultModifierValue || mouseButtonValue !== defaultMouseButtonValue) {
            valueModified(defaultModifierValue);
            mouseButtonsModified(defaultMouseButtonValue);
        }
    }

    //* Compare two trigger arrays for equality
    function triggersEqual(a, b) {
        if (!a || !b || a.length !== b.length)
            return false;

        for (var i = 0; i < a.length; i++) {
            if ((a[i].modifier || 0) !== (b[i].modifier || 0) || (a[i].mouseButton || 0) !== (b[i].mouseButton || 0))
                return false;
        }
        return true;
    }

    //* Apply a captured trigger: replace at editingTriggerIndex, or append if -1
    function applyTriggerCapture(modifier, mouseButton) {
        var newTriggers = [];
        for (var i = 0; i < triggers.length; i++)
            newTriggers.push(triggers[i]);
        if (editingTriggerIndex >= 0 && editingTriggerIndex < newTriggers.length)
            newTriggers[editingTriggerIndex] = {
                "modifier": modifier,
                "mouseButton": mouseButton
            };
        else
            newTriggers.push({
                "modifier": modifier,
                "mouseButton": mouseButton
            });
        // Deduplicate: remove any other entry that matches the new value
        var deduped = [];
        var seen = {};
        for (var j = 0; j < newTriggers.length; j++) {
            var key = (newTriggers[j].modifier || 0) + ":" + (newTriggers[j].mouseButton || 0);
            if (!seen[key]) {
                seen[key] = true;
                deduped.push(newTriggers[j]);
            }
        }
        editingTriggerIndex = -1;
        triggersModified(deduped);
    }

    // Match ShortcutCaptureField: no fixed width so FormLayout gives the same column width as shortcut fields
    implicitWidth: allowMultiple ? multiContainer.implicitWidth : field.implicitWidth
    implicitHeight: allowMultiple ? multiContainer.implicitHeight : field.implicitHeight

    // ═══════════════════════════════════════════════════════════════════════════
    // Multi-mode: list of triggers with Add/Remove
    // ═══════════════════════════════════════════════════════════════════════════
    Rectangle {
        id: multiContainer

        // Pin the View set so the container frame's fill and border resolve
        // against the content-surface palette wherever the control is hosted.
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false
        visible: root.allowMultiple
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
        border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
        border.width: 1
        radius: Kirigami.Units.smallSpacing
        implicitWidth: multiLayout.implicitWidth + Kirigami.Units.smallSpacing * 2
        implicitHeight: multiLayout.implicitHeight + Kirigami.Units.smallSpacing * 2

        ColumnLayout {
            id: multiLayout

            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing
            spacing: 2

            // Trigger rows
            Repeater {
                model: root.allowMultiple ? root.triggers : []

                RowLayout {
                    id: triggerRow

                    required property int index
                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.AbstractButton {
                        Layout.fillWidth: true
                        implicitHeight: triggerLabel.implicitHeight + Kirigami.Units.smallSpacing
                        onClicked: {
                            root.editingTriggerIndex = triggerRow.index;
                            multiInputCapture.startCapture();
                        }
                        QQC2.ToolTip.visible: hoverHandler.hovered && root.tooltipEnabled
                        QQC2.ToolTip.text: i18n("Click to change this trigger")
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                        HoverHandler {
                            id: hoverHandler
                        }

                        contentItem: QQC2.Label {
                            id: triggerLabel

                            text: root.triggerDisplayText(triggerRow.modelData.modifier || 0, triggerRow.modelData.mouseButton || 0)
                            elide: Text.ElideRight
                            color: hoverHandler.hovered ? Kirigami.Theme.hoverColor : Kirigami.Theme.textColor
                        }
                    }

                    QQC2.ToolButton {
                        icon.name: "list-remove"
                        Accessible.name: i18n("Remove trigger")
                        implicitWidth: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing * 2
                        implicitHeight: implicitWidth
                        enabled: root.triggers.length > 1
                        onClicked: {
                            var newTriggers = [];
                            for (var i = 0; i < root.triggers.length; i++) {
                                if (i !== triggerRow.index)
                                    newTriggers.push(root.triggers[i]);
                            }
                            root.triggersModified(newTriggers);
                        }
                        QQC2.ToolTip.visible: hovered && root.tooltipEnabled
                        QQC2.ToolTip.text: root.triggers.length <= 1 ? i18n("At least one trigger is required") : i18n("Remove this trigger")
                    }
                }
            }

            // Add and Reset row
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                QQC2.ToolButton {
                    text: i18n("Add…")
                    icon.name: "list-add"
                    enabled: root.triggers.length < root.maxTriggers
                    onClicked: {
                        root.editingTriggerIndex = -1;
                        multiInputCapture.startCapture();
                    }
                    QQC2.ToolTip.visible: hovered && root.tooltipEnabled
                    QQC2.ToolTip.text: i18n("Add another activation trigger")
                }

                Item {
                    Layout.fillWidth: true
                }

                QQC2.ToolButton {
                    icon.name: "edit-clear"
                    Accessible.name: i18n("Reset to defaults")
                    visible: !triggersEqual(root.triggers, root.defaultTriggers)
                    onClicked: {
                        var copy = [];
                        for (var i = 0; i < root.defaultTriggers.length; i++)
                            copy.push({
                                "modifier": root.defaultTriggers[i].modifier || 0,
                                "mouseButton": root.defaultTriggers[i].mouseButton || 0
                            });
                        root.triggersModified(copy);
                    }
                    QQC2.ToolTip.visible: hovered && root.tooltipEnabled
                    QQC2.ToolTip.text: i18n("Reset to defaults")
                }
            }
        }
    }

    // InputCapture for multi-mode (adds or replaces depending on editingTriggerIndex)
    InputCapture {
        id: multiInputCapture

        visible: false
        acceptMode: root.acceptMode
        tooltipEnabled: root.tooltipEnabled
        onModifierCaptured: mask => {
            if (!root.allowMultiple)
                return;

            root.applyTriggerCapture(mask, 0);
        }
        onMouseCaptured: bit => {
            if (!root.allowMultiple)
                return;

            if (root.acceptMode === root.acceptModeMetaOnly)
                return;

            root.applyTriggerCapture(0, bit);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Single-mode: existing TextField input
    // ═══════════════════════════════════════════════════════════════════════════
    QQC2.TextField {
        id: field

        visible: !root.allowMultiple
        anchors.fill: parent
        // Pin the View set on the control itself so the foreground (text and
        // placeholder colours below) resolves the same content-surface palette
        // as the fill and border, which the background Rectangle pins
        // separately — otherwise the foreground follows the inherited set of
        // whatever surface hosts the control.
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false
        readOnly: true
        text: root.displayText()
        placeholderText: i18n("Click to set shortcut")
        rightPadding: clearBtn.visible ? (clearBtn.width + Kirigami.Units.smallSpacing * 2) : (leftPadding + Kirigami.Units.smallSpacing)
        color: inputCapture.capturing ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
        font.italic: inputCapture.capturing
        placeholderTextColor: Kirigami.Theme.disabledTextColor
        // ToolTip attached to Control (TextField) - Item has no ToolTip attached type
        QQC2.ToolTip.visible: root.tooltipEnabled && clickArea.containsMouse && !inputCapture.capturing
        QQC2.ToolTip.text: root.customTooltipText !== "" ? root.customTooltipText : (root.acceptMode === root.acceptModeMetaOnly ? i18n("Click to set modifier key(s)") : (root.acceptMode === root.acceptModeMouseOnly ? i18n("Click to set any mouse button (Right, Middle, Back, Forward, etc.)") : i18n("Click to set key, modifier, or any mouse button")))
        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

        MouseArea {
            id: clickArea

            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.PointingHandCursor
            z: 10
            onClicked: mouse => {
                if (clearBtn.visible && mouse.x >= field.width - clearBtn.width - Kirigami.Units.smallSpacing * 2) {
                    root.clearAll();
                    return;
                }
                inputCapture.startCapture();
            }
        }

        QQC2.Button {
            id: clearBtn

            visible: !inputCapture.capturing && (root.modifierValue !== root.defaultModifierValue || root.mouseButtonValue !== root.defaultMouseButtonValue)
            anchors.right: parent.right
            anchors.rightMargin: Kirigami.Units.smallSpacing
            anchors.verticalCenter: parent.verticalCenter
            width: height
            height: parent.height - Kirigami.Units.smallSpacing * 2
            flat: true
            icon.name: "edit-clear"
            z: 1
            Accessible.role: Accessible.Button
            Accessible.name: i18n("Reset to defaults")
            onClicked: root.clearAll()
            QQC2.ToolTip.visible: hovered && root.tooltipEnabled
            QQC2.ToolTip.text: i18n("Reset to defaults")
        }

        background: Rectangle {
            // Pin the View set so the field's fill and border resolve against
            // the content-surface palette wherever the control is hosted —
            // the same rationale as multiContainer's pin above, so the two
            // modes render on identical surfaces.
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            color: inputCapture.capturing ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2) : (field.enabled ? Kirigami.Theme.backgroundColor : Qt.alpha(Kirigami.Theme.backgroundColor, 0.5))
            border.color: inputCapture.capturing ? Kirigami.Theme.highlightColor : (field.activeFocus ? Kirigami.Theme.focusColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast))
            border.width: 1
            radius: Kirigami.Units.smallSpacing
        }
    }

    InputCapture {
        id: inputCapture

        visible: false
        acceptMode: root.acceptMode
        tooltipEnabled: root.tooltipEnabled
        // Emit only, mirroring multi-mode's triggersModified contract:
        // self-assigning modifierValue/mouseButtonValue would sever the
        // caller's bindings on first capture.
        onModifierCaptured: mask => {
            root.valueModified(mask);
            root.mouseButtonsModified(0);
        }
        onMouseCaptured: bit => {
            if (root.acceptMode === root.acceptModeMetaOnly)
                return;

            root.valueModified(0);
            root.mouseButtonsModified(bit);
        }
    }
}
