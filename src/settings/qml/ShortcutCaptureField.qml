// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief A reusable shortcut capture TextField for Kirigami.FormLayout.
 *
 * Click the field to enter capture mode, then press a key combination.
 * Press Escape to cancel. Emits keySequenceModified when a valid
 * sequence is captured.
 */
TextField {
    id: root

    property string formLabel
    property string keySequence
    property string tooltipText
    property bool capturing: false

    signal keySequenceModified(string sequence)

    // Helper function to build key sequence string from Qt key event
    function _buildSequence(key, modifiers) {
        var parts = [];
        if (modifiers & Qt.ControlModifier)
            parts.push("Ctrl");

        if (modifiers & Qt.AltModifier)
            parts.push("Alt");

        if (modifiers & Qt.ShiftModifier)
            parts.push("Shift");

        if (modifiers & Qt.MetaModifier)
            parts.push("Meta");

        var keyStr = "";
        if (key >= Qt.Key_A && key <= Qt.Key_Z) {
            keyStr = String.fromCharCode('A'.charCodeAt(0) + (key - Qt.Key_A));
        } else if (key >= Qt.Key_0 && key <= Qt.Key_9) {
            keyStr = String.fromCharCode('0'.charCodeAt(0) + (key - Qt.Key_0));
        } else {
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
                if (key > 0 && key < 128)
                    keyStr = String.fromCharCode(key);

                break;
            }
        }
        if (keyStr.length > 0) {
            parts.push(keyStr);
            return parts.join("+");
        }
        return "";
    }

    Layout.fillWidth: true
    Kirigami.FormData.label: formLabel
    text: capturing ? i18n("Press keys...") : (keySequence || "")
    placeholderText: "Ctrl+Shift+X"
    readOnly: true
    color: capturing ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: (event) => {
        if (!capturing)
            return ;

        event.accepted = true;
        if (event.key === Qt.Key_Escape) {
            capturing = false;
            return ;
        }
        var key = event.key;
        if (key === Qt.Key_Shift || key === Qt.Key_Control || key === Qt.Key_Alt || key === Qt.Key_AltGr || key === Qt.Key_Meta)
            return ;

        var seq = _buildSequence(event.key, event.modifiers);
        if (seq.length > 0) {
            keySequence = seq;
            keySequenceModified(seq);
            capturing = false;
        }
    }
    onActiveFocusChanged: {
        if (!activeFocus && capturing)
            capturing = false;

    }
    ToolTip.visible: hovered && !capturing
    ToolTip.text: tooltipText

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (!root.capturing) {
                root.capturing = true;
                root.forceActiveFocus();
            }
        }
    }

}
