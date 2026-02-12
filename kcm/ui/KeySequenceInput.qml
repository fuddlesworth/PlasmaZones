// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * Key sequence input that captures keys pressed by user
 * Follows KDE HIG pattern for shortcut configuration
 */
QQC2.TextField {
    // This is just a modifier key being pressed, wait for the actual key

    id: root

    property string keySequence: ""
    property string defaultKeySequence: ""
    property bool capturing: false

    signal keySequenceModified(string sequence)

    function formatKeySequence(seq) {
        if (!seq || seq.length === 0)
            return "";

        // Convert to KDE standard format if needed
        return seq;
    }

    function parseKeySequence(seq) {
        // Parse key sequence string and extract modifiers and keys
        if (!seq || seq.length === 0)
            return {
            "modifiers": 0,
            "key": 0
        };

        // Basic parsing - would need more sophisticated implementation
        return {
            "modifiers": 0,
            "key": 0
        };
    }

    text: capturing ? i18n("Press keys...") : (keySequence || "")
    placeholderText: i18n("Click to set shortcut")
    // Always read-only - we handle all input via key capture
    readOnly: true
    // Keys.BeforeItem lets us handle keys before the TextField does.
    Keys.priority: Keys.BeforeItem
    // Capture keys - this must come BEFORE default TextField handling
    Keys.onPressed: (event) => {
        if (!capturing)
            return ;

        // Accept right away so the TextField doesn't process it.
        event.accepted = true;
        // Handle Escape to cancel
        if (event.key === Qt.Key_Escape) {
            capturing = false;
            return ;
        }
        var modifiers = event.modifiers;
        var key = event.key;
        // Ignore modifier-only key presses (wait for actual key)
        if (key === Qt.Key_Shift || key === Qt.Key_Control || key === Qt.Key_Alt || key === Qt.Key_AltGr || key === Qt.Key_Meta)
            return ;

        // Build key sequence string with modifiers
        var parts = [];
        if (modifiers & Qt.ControlModifier)
            parts.push("Ctrl");

        if (modifiers & Qt.AltModifier)
            parts.push("Alt");

        if (modifiers & Qt.ShiftModifier)
            parts.push("Shift");

        if (modifiers & Qt.MetaModifier)
            parts.push("Meta");

        // Convert key to string
        var keyStr = "";
        // Map key codes to their string representation
        if (key >= Qt.Key_A && key <= Qt.Key_Z) {
            // For letters, use uppercase letter (QKeySequence format)
            keyStr = String.fromCharCode('A'.charCodeAt(0) + (key - Qt.Key_A));
        } else if (key >= Qt.Key_0 && key <= Qt.Key_9) {
            keyStr = String.fromCharCode('0'.charCodeAt(0) + (key - Qt.Key_0));
        } else {
            // Special keys
            switch (key) {
            case Qt.Key_F1:
                keyStr = "F1";
                break;
            case Qt.Key_F2:
                keyStr = "F2";
                break;
            case Qt.Key_F3:
                keyStr = "F3";
                break;
            case Qt.Key_F4:
                keyStr = "F4";
                break;
            case Qt.Key_F5:
                keyStr = "F5";
                break;
            case Qt.Key_F6:
                keyStr = "F6";
                break;
            case Qt.Key_F7:
                keyStr = "F7";
                break;
            case Qt.Key_F8:
                keyStr = "F8";
                break;
            case Qt.Key_F9:
                keyStr = "F9";
                break;
            case Qt.Key_F10:
                keyStr = "F10";
                break;
            case Qt.Key_F11:
                keyStr = "F11";
                break;
            case Qt.Key_F12:
                keyStr = "F12";
                break;
            case Qt.Key_Return:
                keyStr = "Return";
                break;
            case Qt.Key_Enter:
                keyStr = "Enter";
                break;
            case Qt.Key_Space:
                keyStr = "Space";
                break;
            case Qt.Key_Tab:
                keyStr = "Tab";
                break;
            case Qt.Key_Backspace:
                keyStr = "Backspace";
                break;
            case Qt.Key_Delete:
                keyStr = "Delete";
                break;
            case Qt.Key_Escape:
                keyStr = "Escape";
                break;
            case Qt.Key_Left:
                keyStr = "Left";
                break;
            case Qt.Key_Right:
                keyStr = "Right";
                break;
            case Qt.Key_Up:
                keyStr = "Up";
                break;
            case Qt.Key_Down:
                keyStr = "Down";
                break;
            case Qt.Key_Home:
                keyStr = "Home";
                break;
            case Qt.Key_End:
                keyStr = "End";
                break;
            case Qt.Key_PageUp:
                keyStr = "PageUp";
                break;
            case Qt.Key_PageDown:
                keyStr = "PageDown";
                break;
            case Qt.Key_Insert:
                keyStr = "Insert";
                break;
            case Qt.Key_QuoteLeft:
                keyStr = "`";
                break;
            case Qt.Key_AsciiTilde:
                keyStr = "~";
                break;
            case Qt.Key_Minus:
                keyStr = "-";
                break;
            case Qt.Key_Equal:
                keyStr = "=";
                break;
            case Qt.Key_BracketLeft:
                keyStr = "[";
                break;
            case Qt.Key_BracketRight:
                keyStr = "]";
                break;
            case Qt.Key_Backslash:
                keyStr = "\\";
                break;
            case Qt.Key_Semicolon:
                keyStr = ";";
                break;
            case Qt.Key_Apostrophe:
                keyStr = "'";
                break;
            case Qt.Key_Comma:
                keyStr = ",";
                break;
            case Qt.Key_Period:
                keyStr = ".";
                break;
            case Qt.Key_Slash:
                keyStr = "/";
                break;
            default:
                // Try to convert key code to character
                if (key > 0 && key < 128) {
                    keyStr = String.fromCharCode(key);
                } else {
                    // Unknown key, cancel capturing
                    capturing = false;
                    return ;
                }
            }
        }
        // If we have a valid key, create the sequence
        if (keyStr.length > 0) {
            parts.push(keyStr);
            var newSeq = parts.join("+");
            keySequence = newSeq;
            capturing = false;
            root.keySequenceModified(newSeq);
        }
    }
    onActiveFocusChanged: {
        if (!activeFocus && capturing)
            capturing = false;

    }
    // Visual feedback
    color: capturing ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
    // Sync keySequence property changes
    onKeySequenceChanged: {
        if (!capturing && text !== (capturing ? i18n("Press keys...") : (keySequence || "")))
            text = capturing ? i18n("Press keys...") : (keySequence || "");

    }

    // Handle click to start capturing
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        z: 10
        onClicked: (mouse) => {
            if (clearButton.visible && mouse.x >= root.width - clearButton.width - Kirigami.Units.smallSpacing * 2) {
                root.keySequence = root.defaultKeySequence;
                root.keySequenceModified(root.defaultKeySequence);
                return;
            }
            if (!root.capturing) {
                root.capturing = true;
                root.focus = true;
                root.forceActiveFocus(Qt.MouseFocusReason);
            }
        }
    }

    // Clear button
    QQC2.Button {
        id: clearButton

        anchors.right: parent.right
        anchors.rightMargin: Kirigami.Units.smallSpacing
        anchors.verticalCenter: parent.verticalCenter
        width: height
        height: parent.height - Kirigami.Units.smallSpacing * 2
        visible: !root.capturing && root.keySequence !== root.defaultKeySequence
        icon.name: "edit-clear"
        flat: true
        z: 1
        onClicked: {
            root.keySequence = root.defaultKeySequence;
            root.keySequenceModified(root.defaultKeySequence);
        }
    }

    background: Rectangle {
        // Note: Kirigami.Theme.disabledBackgroundColor doesn't exist, use semi-transparent backgroundColor instead
        color: root.capturing ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2) : (parent.enabled ? Kirigami.Theme.backgroundColor : Qt.alpha(Kirigami.Theme.backgroundColor, 0.5))
        border.color: root.capturing ? Kirigami.Theme.highlightColor : (parent.activeFocus ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor)
        border.width: 1
        radius: Kirigami.Units.smallSpacing
    }

}
