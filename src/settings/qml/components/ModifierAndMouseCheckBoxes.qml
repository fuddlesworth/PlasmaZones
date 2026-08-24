// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * A list of activation triggers with Add / Edit / Remove. Each entry is one
 * modifier combination or mouse button that independently activates the
 * feature the hosting row controls.
 *
 * acceptMode narrows what a capture will take: MetaOnly (modifier keys),
 * MouseOnly (any mouse button, Right / Middle / Back / Forward / Extra 3-5),
 * or All.
 *
 * The CALLER owns the state. This component never writes `triggers` itself.
 * It emits triggersModified and the caller persists the value, which comes
 * back through the property binding. A host that ignores the signal will not
 * see captures or removals reflected, and self-assigning here would sever
 * that binding on the first edit.
 *
 * There used to be a single-value mode behind an `allowMultiple` flag,
 * roughly a third of the file, kept for a caller that never arrived: every
 * instantiation site in the tree passed true. Both the mode and the flag are
 * gone rather than left as a second untested path through the capture, clear
 * and display logic that no host could reach.
 */
Item {
    // Edit mode: replace the trigger at the edited index
    // Add mode: append

    id: root

    readonly property int acceptModeAll: 0
    readonly property int acceptModeMetaOnly: 1
    readonly property int acceptModeMouseOnly: 2
    /// What a capture will accept. See the class note.
    property int acceptMode: acceptModeAll
    property bool tooltipEnabled: true
    /// Names which trigger list this editor edits, folded into the announced
    /// accessible names. A card may host more than one editor (the Focus and
    /// view card hosts two), and without this a screen reader announces an
    /// identical "Remove trigger" / "Reset to defaults" for each of them.
    /// Empty leaves the plain names, which is right for a card with one.
    property string accessibleContext: ""
    property var triggers: [] // [{modifier: bitmask, mouseButton: buttonBit}, ...]
    property var defaultTriggers: []
    readonly property int maxTriggers: 4
    /// Stored slots that are spoken for but absent from `triggers`. Set to 1
    /// by a host whose master "always active" toggle is on: that toggle is
    /// stored as an AlwaysActive sentinel INSIDE the same capped list, and the
    /// controllers strip it before handing the list here, so `triggers` is one
    /// shorter than what is really stored. Gating Add on the stripped length
    /// let the user author four triggers, the merge re-added the sentinel, and
    /// the cap silently dropped the last one — a chip the user had just
    /// created vanished on the next read with nothing said.
    property int reservedTriggerSlots: 0
    /// What Add is actually allowed to reach.
    readonly property int availableTriggerSlots: root.maxTriggers - root.reservedTriggerSlots
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

    //* Display text for a single trigger (modifier bitmask + mouse button bit)
    function triggerDisplayText(modifier, mouseButton) {
        return _scanText(modifier, mouseButton, i18n("(none)"));
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

    // Match ShortcutCaptureField: no fixed width so FormLayout gives the same
    // column width as shortcut fields.
    implicitWidth: multiContainer.implicitWidth
    implicitHeight: multiContainer.implicitHeight

    Rectangle {
        id: multiContainer

        // Pin the View set so the container frame's fill and border resolve
        // against the content-surface palette wherever the control is hosted.
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false
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
                model: root.triggers

                RowLayout {
                    id: triggerRow

                    required property int index
                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.AbstractButton {
                        Layout.fillWidth: true
                        implicitHeight: triggerLabel.implicitHeight + Kirigami.Units.smallSpacing
                        // The label lives in a custom contentItem, so the
                        // button has no `text` to derive an accessible name
                        // from, and every trigger card that hosts this
                        // component disables the tooltip, leaving this the
                        // chip's only announced affordance.
                        Accessible.role: Accessible.Button
                        Accessible.name: root.accessibleContext.length > 0 ? i18nc("@action:button %1 is a key chord such as Meta+Shift, %2 names the setting being edited", "Change trigger %1 for %2", triggerLabel.text, root.accessibleContext) : i18n("Change trigger %1", triggerLabel.text)
                        // QQC2.Control defaults focusPolicy to Qt.NoFocus and
                        // AbstractButton, unlike Button/ToolButton, gets no
                        // style override that raises it. Without this the chip
                        // is announced but unreachable by keyboard: Tab walks
                        // straight past every chip to the row's tool buttons.
                        // Space is deliberately NOT handled here: QQC2's
                        // AbstractButton already turns a Space press into a
                        // clicked(), and adding an attached handler risks
                        // firing it twice. Return and Enter it does not
                        // handle, so those are ours.
                        focusPolicy: Qt.StrongFocus
                        Keys.onReturnPressed: clicked()
                        Keys.onEnterPressed: clicked()
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

                        // Keyboard focus needs to be visible, not just
                        // reachable. Transparent until focused so the chip
                        // keeps its current flat look under the mouse.
                        background: Rectangle {
                            color: "transparent"
                            radius: Kirigami.Units.smallSpacing
                            border.width: parent.activeFocus ? 1 : 0
                            border.color: Kirigami.Theme.focusColor
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
                        Accessible.name: root.accessibleContext.length > 0 ? i18nc("@action:button %1 names the setting being edited", "Remove trigger for %1", root.accessibleContext) : i18n("Remove trigger")
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
                        // Shown on HOVER, which a disabled ToolButton does not
                        // report, so the length<=1 branch could never appear
                        // and the one case a user needs explaining was the one
                        // that stayed silent. The button keeps its own state
                        // and the tooltip now always says what Remove does;
                        // the reason it is unavailable is legible from there
                        // being a single chip left.
                        QQC2.ToolTip.visible: hovered && root.tooltipEnabled
                        QQC2.ToolTip.text: i18n("Remove this trigger. At least one must remain.")
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
                    enabled: root.triggers.length < root.availableTriggerSlots
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
                    Accessible.name: root.accessibleContext.length > 0 ? i18nc("@action:button %1 names the setting being edited", "Reset %1 to defaults", root.accessibleContext) : i18n("Reset to defaults")
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
            root.applyTriggerCapture(mask, 0);
        }
        onMouseCaptured: bit => {
            if (root.acceptMode === root.acceptModeMetaOnly)
                return;

            root.applyTriggerCapture(0, bit);
        }
    }
}
