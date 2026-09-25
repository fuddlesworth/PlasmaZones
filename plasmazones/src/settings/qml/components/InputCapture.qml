// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * Own input capture: a modifier combination or a mouse button, never a plain
 * key — every host authors trigger chords, not keyboard shortcuts.
 * Uses a modal overlay when capturing so nothing else can steal focus.
 * Click the field to start; Escape cancels.
 * acceptMode: MetaOnly (modifier keys only), MouseOnly (mouse buttons only),
 * or All (either shape, still one at a time).
 */
Control {
    id: root

    // acceptMode: 0 = All, 1 = MetaOnly (modifier keys only), 2 = MouseOnly (mouse buttons only)
    readonly property int acceptModeAll: 0
    readonly property int acceptModeMetaOnly: 1
    readonly property int acceptModeMouseOnly: 2
    property bool capturing: false
    property int acceptMode: acceptModeAll
    property string placeholderText: acceptMode === acceptModeMetaOnly ? i18n("Click to capture modifier") : (acceptMode === acceptModeMouseOnly ? i18n("Click to capture mouse button") : i18n("Click to capture"))
    property string capturingText: acceptMode === acceptModeMetaOnly ? i18n("Press modifier(s)…") : (acceptMode === acceptModeMouseOnly ? i18n("Press mouse button…") : i18n("Press modifier(s) or mouse button…"))
    property bool tooltipEnabled: true
    // Qt::KeyboardModifier bits, from the shared TriggerLabels tables so the
    // capture masks and every display surface agree on one vocabulary.
    readonly property int shiftFlag: TriggerLabels.shiftFlag
    readonly property int ctrlFlag: TriggerLabels.ctrlFlag
    readonly property int altFlag: TriggerLabels.altFlag
    readonly property int metaFlag: TriggerLabels.metaFlag
    property int pendingModifierMask: 0
    property bool nonModifierKeyPressed: false

    signal modifierCaptured(int modifierMask)
    signal mouseCaptured(int buttonBit)
    signal captureCancelled

    function startCapture() {
        root.capturing = true;
        root.pendingModifierMask = 0;
        root.nonModifierKeyPressed = false;
        unsupportedButtonHintTimer.stop();
        unsupportedButtonHint.visible = false;
        captureOverlay.open();
    }

    function endCapture() {
        root.capturing = false;
        captureOverlay.close();
    }

    function cancelCapture() {
        endCapture();
        root.captureCancelled();
    }

    function qtModifiersToMask(modifiers) {
        var m = 0;
        if (modifiers & Qt.ShiftModifier)
            m |= shiftFlag;

        if (modifiers & Qt.ControlModifier)
            m |= ctrlFlag;

        if (modifiers & Qt.AltModifier)
            m |= altFlag;

        if (modifiers & Qt.MetaModifier)
            m |= metaFlag;

        return m;
    }

    function isModifierKey(key) {
        return (key === Qt.Key_Shift || key === Qt.Key_Control || key === Qt.Key_Alt || key === Qt.Key_Meta || key === Qt.Key_AltGr);
    }

    Accessible.role: Accessible.EditableText
    Accessible.name: root.placeholderText
    focusPolicy: Qt.ClickFocus
    topPadding: Kirigami.Units.smallSpacing
    bottomPadding: Kirigami.Units.smallSpacing
    leftPadding: Kirigami.Units.smallSpacing
    rightPadding: Kirigami.Units.smallSpacing
    implicitWidth: captureLabel.implicitWidth + leftPadding + rightPadding
    implicitHeight: captureLabel.implicitHeight + topPadding + bottomPadding
    ToolTip.visible: tooltipEnabled && (triggerArea.containsMouse || root.capturing)
    ToolTip.text: root.capturing ? (root.acceptMode === root.acceptModeMetaOnly ? i18n("Press modifier key(s) only (Escape to cancel)") : (root.acceptMode === root.acceptModeMouseOnly ? i18n("Press any mouse button: Right, Middle, Back, Forward, or extra (Escape to cancel)") : i18n("Press modifier key(s) or any mouse button (Escape to cancel)"))) : (root.acceptMode === root.acceptModeMetaOnly ? i18n("Click then press modifier key(s)") : (root.acceptMode === root.acceptModeMouseOnly ? i18n("Click then press any mouse button (Right, Middle, Back, Forward, etc.)") : i18n("Click then press modifier(s) or any mouse button")))
    ToolTip.delay: Kirigami.Units.toolTipDelay

    MouseArea {
        id: triggerArea

        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onClicked: mouse => {
            if (!root.capturing)
                root.startCapture();
        }
    }

    // Modal overlay: our own input capture so we always get key/mouse
    Popup {
        id: captureOverlay

        parent: root.Window ? root.Window.contentItem : root
        x: 0
        y: 0
        width: parent ? parent.width : 400
        height: parent ? parent.height : 300
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        dim: true
        onOpened: {
            captureOverlay.contentItem.forceActiveFocus();
        }
        onClosed: {
            if (root.capturing) {
                root.capturing = false;
                root.captureCancelled();
            }
        }

        // Black on purpose, not a theme colour. A modal scrim darkens what it
        // covers in every scheme, which is what QQC2's own default does; a
        // theme-derived tint follows the scheme instead and inverts on one
        // side of it (backgroundColor washes the page with its own colour,
        // textColor brightens a dark page rather than dimming it). The
        // overlay is also parented to the window, not to this item, so the
        // colour set in effect here is not the one behind it.
        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.4)
        }

        contentItem: Item {
            focus: true
            width: captureOverlay.width
            height: captureOverlay.height
            Keys.onPressed: event => {
                event.accepted = true;
                if (event.key === Qt.Key_Escape) {
                    root.cancelCapture();
                    return;
                }
                if (root.acceptMode === root.acceptModeMouseOnly)
                    return;

                var modMask = root.qtModifiersToMask(event.modifiers);
                if (root.isModifierKey(event.key)) {
                    root.pendingModifierMask = modMask;
                    return;
                }
                // A plain key is never captured — every host authors modifier
                // or mouse chords, not keyboard shortcuts. Record the press so
                // releasing a modifier after e.g. Ctrl+A does not count as a
                // bare-modifier capture; releasing the key clears the latch.
                root.nonModifierKeyPressed = true;
            }
            Keys.onReleased: event => {
                event.accepted = true;
                if (root.acceptMode !== root.acceptModeMetaOnly && root.acceptMode !== root.acceptModeAll)
                    return;

                // Releasing the non-modifier key clears the rejection latch so
                // a later clean modifier press can still capture within the
                // same session. A modifier released while the non-modifier is
                // still held falls through to the guard below and stays
                // rejected.
                if (!root.isModifierKey(event.key)) {
                    root.nonModifierKeyPressed = false;
                    return;
                }
                if (!root.nonModifierKeyPressed && root.pendingModifierMask !== 0) {
                    root.endCapture();
                    root.modifierCaptured(root.pendingModifierMask);
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
                onPressed: mouse => {
                    mouse.accepted = true;
                    if (root.acceptMode === root.acceptModeMetaOnly)
                        return;

                    if (mouse.button === Qt.LeftButton) {
                        // In MouseOnly mode, left click cancels (no capture); in All mode, ignore so the user can still press a modifier or another button
                        if (root.acceptMode === root.acceptModeMouseOnly)
                            root.cancelCapture();

                        return;
                    }
                    var bit = mouse.button;
                    // Deliberate cap at Qt.ExtraButton5 (bit 128): the trigger
                    // storage and every display surface (mouseButtonList in
                    // ModifierAndMouseCheckBoxes, the kcfg Extra1/2/3 naming)
                    // only label buttons up to Extra 5, so a higher capture
                    // would persist as an unlabelled, un-editable trigger.
                    // Out-of-range presses show a hint instead of being
                    // silently swallowed.
                    if (bit >= 2 && bit <= 128) {
                        root.endCapture();
                        root.mouseCaptured(bit);
                    } else {
                        unsupportedButtonHint.visible = true;
                        unsupportedButtonHintTimer.restart();
                    }
                }
            }

            Label {
                id: captureHintLabel

                anchors.centerIn: parent
                text: root.acceptMode === root.acceptModeMetaOnly ? i18n("Press modifier(s). Escape to cancel.") : (root.acceptMode === root.acceptModeMouseOnly ? i18n("Press any mouse button (Right, Middle, Back, Forward, etc.). Escape to cancel.") : i18n("Press modifier(s) or a mouse button. Escape to cancel."))
                color: Kirigami.Theme.textColor
                font.italic: true
            }

            Label {
                id: unsupportedButtonHint

                anchors.top: captureHintLabel.bottom
                anchors.topMargin: Kirigami.Units.smallSpacing
                anchors.horizontalCenter: parent.horizontalCenter
                visible: false
                text: i18n("That mouse button is not supported. Buttons up to Extra 5 can be captured.")
                color: Kirigami.Theme.negativeTextColor

                Timer {
                    id: unsupportedButtonHintTimer

                    interval: Kirigami.Units.humanMoment
                    onTriggered: unsupportedButtonHint.visible = false
                }
            }
        }
    }

    contentItem: Item {
        implicitWidth: captureLabel.implicitWidth
        implicitHeight: captureLabel.implicitHeight

        Label {
            id: captureLabel

            anchors.fill: parent
            text: root.capturing ? root.capturingText : root.placeholderText
            color: root.capturing ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
            font.italic: root.capturing
        }
    }

    background: Rectangle {
        color: root.capturing ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15) : (triggerArea.containsMouse ? Qt.alpha(Kirigami.Theme.hoverColor, 0.15) : "transparent")
        border.color: root.capturing ? Kirigami.Theme.highlightColor : (triggerArea.containsMouse ? Kirigami.Theme.disabledTextColor : "transparent")
        border.width: 1
        radius: Kirigami.Units.smallSpacing
    }
}
