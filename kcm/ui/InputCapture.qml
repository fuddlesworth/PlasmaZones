// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * Own input capture: key, mouse, or modifier-only.
 * Uses a modal overlay when capturing so nothing else can steal focus.
 * Click the field to start; overlay captures key, mouse, or modifier-only. Escape cancels.
 * acceptMode: MetaOnly (modifier keys only), MouseOnly (mouse buttons only), or All.
 */
Control {
    id: root

    Accessible.role: Accessible.EditableText
    Accessible.name: root.placeholderText

    // acceptMode: 0 = All, 1 = MetaOnly (modifier keys only), 2 = MouseOnly (mouse buttons only)
    readonly property int acceptModeAll: 0
    readonly property int acceptModeMetaOnly: 1
    readonly property int acceptModeMouseOnly: 2

    focusPolicy: Qt.ClickFocus
    property bool capturing: false
    property int acceptMode: acceptModeAll
    property string placeholderText: acceptMode === acceptModeMetaOnly ? i18n("Click to capture modifier") : (acceptMode === acceptModeMouseOnly ? i18n("Click to capture mouse button") : i18n("Click to capture"))
    property string capturingText: acceptMode === acceptModeMetaOnly ? i18n("Press modifier(s)…") : (acceptMode === acceptModeMouseOnly ? i18n("Press mouse button…") : i18n("Press key, modifier, or mouse…"))
    property bool tooltipEnabled: true

    signal modifierCaptured(int modifierMask)
    signal mouseCaptured(int buttonBit)
    signal keySequenceCaptured(string sequence)
    signal captureCancelled()

    // Qt::KeyboardModifier bits (match Qt)
    readonly property int shiftFlag: 0x02000000
    readonly property int ctrlFlag: 0x04000000
    readonly property int altFlag: 0x08000000
    readonly property int metaFlag: 0x10000000

    topPadding: Kirigami.Units.smallSpacing
    bottomPadding: Kirigami.Units.smallSpacing
    leftPadding: Kirigami.Units.smallSpacing
    rightPadding: Kirigami.Units.smallSpacing

    implicitWidth: captureLabel.implicitWidth + leftPadding + rightPadding
    implicitHeight: captureLabel.implicitHeight + topPadding + bottomPadding

    property int pendingModifierMask: 0
    property bool nonModifierKeyPressed: false

    function startCapture() {
        root.capturing = true
        root.pendingModifierMask = 0
        root.nonModifierKeyPressed = false
        captureOverlay.open()
    }

    function endCapture() {
        root.capturing = false
        captureOverlay.close()
    }

    function cancelCapture() {
        endCapture()
        root.captureCancelled()
    }

    function qtModifiersToMask(modifiers) {
        var m = 0
        if (modifiers & Qt.ShiftModifier) m |= shiftFlag
        if (modifiers & Qt.ControlModifier) m |= ctrlFlag
        if (modifiers & Qt.AltModifier) m |= altFlag
        if (modifiers & Qt.MetaModifier) m |= metaFlag
        return m
    }

    function keyToSequenceString(key, modifiers) {
        var parts = []
        if (modifiers & Qt.ControlModifier) parts.push("Ctrl")
        if (modifiers & Qt.AltModifier) parts.push("Alt")
        if (modifiers & Qt.ShiftModifier) parts.push("Shift")
        if (modifiers & Qt.MetaModifier) parts.push("Meta")
        var keyStr = ""
        if (key >= Qt.Key_A && key <= Qt.Key_Z)
            keyStr = String.fromCharCode(65 + (key - Qt.Key_A))
        else if (key >= Qt.Key_0 && key <= Qt.Key_9)
            keyStr = String.fromCharCode(48 + (key - Qt.Key_0))
        else {
            var table = {}
            table[Qt.Key_F1] = "F1"; table[Qt.Key_F2] = "F2"; table[Qt.Key_F3] = "F3"; table[Qt.Key_F4] = "F4"
            table[Qt.Key_F5] = "F5"; table[Qt.Key_F6] = "F6"; table[Qt.Key_F7] = "F7"; table[Qt.Key_F8] = "F8"
            table[Qt.Key_F9] = "F9"; table[Qt.Key_F10] = "F10"; table[Qt.Key_F11] = "F11"; table[Qt.Key_F12] = "F12"
            table[Qt.Key_Return] = "Return"; table[Qt.Key_Enter] = "Enter"; table[Qt.Key_Space] = "Space"
            table[Qt.Key_Tab] = "Tab"; table[Qt.Key_Backspace] = "Backspace"; table[Qt.Key_Delete] = "Delete"
            table[Qt.Key_Left] = "Left"; table[Qt.Key_Right] = "Right"; table[Qt.Key_Up] = "Up"; table[Qt.Key_Down] = "Down"
            table[Qt.Key_Home] = "Home"; table[Qt.Key_End] = "End"; table[Qt.Key_PageUp] = "PageUp"; table[Qt.Key_PageDown] = "PageDown"
            table[Qt.Key_Minus] = "-"; table[Qt.Key_Equal] = "="; table[Qt.Key_BracketLeft] = "["; table[Qt.Key_BracketRight] = "]"
            table[Qt.Key_Backslash] = "\\"; table[Qt.Key_Semicolon] = ";"; table[Qt.Key_Apostrophe] = "'"
            table[Qt.Key_Comma] = ","; table[Qt.Key_Period] = "."; table[Qt.Key_Slash] = "/"
            keyStr = table[key] || (key > 0 && key < 128 ? String.fromCharCode(key) : "")
        }
        if (keyStr.length > 0) {
            parts.push(keyStr)
            return parts.join("+")
        }
        return ""
    }

    function isModifierKey(key) {
        return (key === Qt.Key_Shift || key === Qt.Key_Control || key === Qt.Key_Alt
                || key === Qt.Key_Meta || key === Qt.Key_AltGr)
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
        color: root.capturing ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15) : (triggerArea.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.05) : "transparent")
        border.color: root.capturing ? Kirigami.Theme.highlightColor : (triggerArea.containsMouse ? Kirigami.Theme.disabledTextColor : "transparent")
        border.width: 1
        radius: Kirigami.Units.smallSpacing
    }

    MouseArea {
        id: triggerArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        onClicked: (mouse) => {
            if (!root.capturing)
                root.startCapture()
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

        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, 0.4)
        }

        contentItem: Item {
            focus: true
            width: captureOverlay.width
            height: captureOverlay.height

            Keys.onPressed: (event) => {
                event.accepted = true
                if (event.key === Qt.Key_Escape) {
                    root.cancelCapture()
                    return
                }
                if (root.acceptMode === root.acceptModeMouseOnly)
                    return
                var modMask = root.qtModifiersToMask(event.modifiers)
                if (root.isModifierKey(event.key)) {
                    root.pendingModifierMask = modMask
                    return
                }
                if (root.acceptMode === root.acceptModeMetaOnly)
                    return
                root.nonModifierKeyPressed = true
                var seq = root.keyToSequenceString(event.key, event.modifiers)
                if (seq.length > 0) {
                    root.endCapture()
                    root.keySequenceCaptured(seq)
                }
            }

            Keys.onReleased: (event) => {
                event.accepted = true
                if (root.acceptMode !== root.acceptModeMetaOnly && root.acceptMode !== root.acceptModeAll)
                    return
                if (root.isModifierKey(event.key) && !root.nonModifierKeyPressed && root.pendingModifierMask !== 0) {
                    root.endCapture()
                    root.modifierCaptured(root.pendingModifierMask)
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
                onPressed: (mouse) => {
                    mouse.accepted = true
                    if (root.acceptMode === root.acceptModeMetaOnly)
                        return
                    if (mouse.button === Qt.LeftButton) {
                        // In MouseOnly mode, left click cancels (no capture); in All mode, ignore so user can press a key/mouse
                        if (root.acceptMode === root.acceptModeMouseOnly)
                            root.cancelCapture()
                        return
                    }
                    var bit = mouse.button
                    if (bit >= 0x02 && bit <= 0x80) {
                        root.endCapture()
                        root.mouseCaptured(bit)
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                text: root.acceptMode === root.acceptModeMetaOnly ? i18n("Press modifier(s) — Escape to cancel")
                    : (root.acceptMode === root.acceptModeMouseOnly ? i18n("Press any mouse button (Right, Middle, Back, Forward, etc.) — Escape to cancel")
                      : i18n("Press key, modifier(s), or mouse button — Escape to cancel"))
                color: Kirigami.Theme.textColor
                font.italic: true
            }
        }

        onClosed: {
            if (root.capturing) {
                root.capturing = false
                root.captureCancelled()
            }
        }
    }

    ToolTip.visible: tooltipEnabled && (triggerArea.containsMouse || root.capturing)
    ToolTip.text: root.capturing ? (root.acceptMode === root.acceptModeMetaOnly ? i18n("Press modifier key(s) only (Escape to cancel)")
        : (root.acceptMode === root.acceptModeMouseOnly ? i18n("Press any mouse button: Right, Middle, Back, Forward, or extra (Escape to cancel)")
          : i18n("Press a key, modifier only, or any mouse button (Escape to cancel)")))
        : (root.acceptMode === root.acceptModeMetaOnly ? i18n("Click then press modifier key(s)")
          : (root.acceptMode === root.acceptModeMouseOnly ? i18n("Click then press any mouse button (Right, Middle, Back, Forward, etc.)")
            : i18n("Click then press key, modifier(s), or any mouse button")))
    ToolTip.delay: Kirigami.Units.toolTipDelay
}
